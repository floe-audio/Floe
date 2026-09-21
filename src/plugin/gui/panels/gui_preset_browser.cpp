// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_preset_browser.hpp"

#include "os/filesystem.hpp"

#include "engine/default_preset.hpp"
#include "engine/engine.hpp"
#include "engine/favourite_items.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui/panels/gui_common_browser.hpp"
#include "gui_framework/gui_builder.hpp"
#include "preset_server/preset_server.hpp"

constexpr String k_no_preset_author = "<no author>"_s;
constexpr u64 k_no_preset_author_hash = HashFnv1a(k_no_preset_author);

inline prefs::Key FavouriteItemKey() { return k_favourite_preset_key; }

static FolderNode const* FindFolderByHash(PresetBrowserContext const& context, u64 folder_hash) {
    FolderNode const* result = nullptr;

    for (auto listing : context.presets_snapshot.banks) {
        ForEachNode(const_cast<FolderNode*>(&listing->node), [&](FolderNode const* node) {
            if (result) return;
            if (node->Hash() == folder_hash) result = node;
        });
    }

    return result;
}

struct PresetCursor {
    bool operator==(PresetCursor const& o) const = default;
    usize folder_index;
    usize preset_index;
};

static u64 CurrentLoadedPresetUuid(PresetBrowserContext const& context) {
    auto const& engine = context.engine;
    if (engine.pending_state_change) return engine.pending_state_change->snapshot.extras.preset_uuid;
    return engine.pinned_snapshot.state.extras.preset_uuid;
}

static Optional<PresetCursor> ResolveCurrentLoadedCursor(PresetBrowserContext const& context) {
    auto const uuid = CurrentLoadedPresetUuid(context);
    if (uuid == 0) return k_nullopt;

    if (context.cached_current_loaded && context.cached_current_loaded->uuid == uuid) {
        if (context.cached_current_loaded->folder_index && context.cached_current_loaded->preset_index)
            return PresetCursor {*context.cached_current_loaded->folder_index,
                                 *context.cached_current_loaded->preset_index};
        return k_nullopt;
    }

    // Collect every cursor whose preset_uuid matches. Usually 0 or 1; only when the user has
    // copy-pasted preset files via the OS file manager will multiple matches exist (the copies
    // share the UUID embedded in the file). In that rare case, we tiebreak by path equality
    // below — so no path comparison happens on the normal path.
    DynamicArrayBounded<PresetCursor, 4> matches;
    for (auto const [folder_index, folder_listing] : Enumerate(context.presets_snapshot.folders)) {
        ASSERT(folder_listing->folder);
        for (auto const [preset_index, preset] : Enumerate(folder_listing->folder->presets)) {
            if (preset.preset_uuid == uuid) {
                if (matches.size < matches.Capacity())
                    dyn::Append(matches, PresetCursor {folder_index, preset_index});
            }
        }
    }

    Optional<PresetCursor> result;
    if (matches.size == 1) {
        result = matches[0];
    } else if (matches.size > 1) {
        auto const& engine = context.engine;
        String const loaded_path = engine.pending_state_change
                                       ? (String)engine.pending_state_change->preset_path
                                       : (String)engine.pinned_snapshot.preset_path;
        if (loaded_path.size) {
            PathArena scratch {Malloc::Instance()};
            for (auto const cursor : matches) {
                scratch.ResetCursorAndConsolidateRegions();
                auto const& folder = *context.presets_snapshot.folders[cursor.folder_index]->folder;
                auto const& preset = folder.presets[cursor.preset_index];
                if (path::Equal(folder.FullPathForPreset(preset, scratch), loaded_path)) {
                    result = cursor;
                    break;
                }
            }
        }
        // No path or no path match: fall back to the first occurrence — same row stays
        // highlighted across frames, navigation remains deterministic.
        if (!result) result = matches[0];
    }

    context.cached_current_loaded = PresetBrowserContext::CachedCurrentLoaded {
        .uuid = uuid,
        .folder_index = result.HasValue() ? Optional<usize> {result->folder_index} : k_nullopt,
        .preset_index = result.HasValue() ? Optional<usize> {result->preset_index} : k_nullopt};
    return result;
}

static bool ShouldSkipPreset(PresetBrowserContext const& context,
                             PresetBrowserState const& state,
                             PresetFolderListing const& folder,
                             PresetFolder::Preset const& preset) {
    ASSERT(folder.folder);
    if (state.common_state.search.size &&
        !ContainsCaseInsensitiveAscii(preset.name, state.common_state.search))
        return true;

    if (state.common_state.favourites.HasSelected() &&
        !IsFavourite(context.prefs, FavouriteItemKey(), preset.preset_uuid))
        return true;

    return IsFilteredOut(state.common_state, [&](usize index, FilterSelection const& filter) -> bool {
        switch ((BrowserFilter)index) {
            case BrowserFilter::Folder:
                return MatchesFilterValues(filter, state.common_state.filter_mode, [&](String, u64 key) {
                    return IsInsideFolder(&folder, key);
                });
            case BrowserFilter::Library:
                return MatchesFilterValues(filter, state.common_state.filter_mode, [&](String, u64 key) {
                    return preset.used_libraries.ContainsSkipKeyCheck(key);
                });
            case BrowserFilter::LibraryAuthor:
                return MatchesFilterValues(filter, state.common_state.filter_mode, [&](String, u64 key) {
                    for (auto [lib_id, _] : preset.used_libraries) {
                        auto const maybe_lib = context.frame_context.lib_table.Find(lib_id);
                        if (!maybe_lib) continue;
                        if ((*maybe_lib)->author_hash == key) return true;
                    }
                    return false;
                });
            case BrowserFilter::Tags:
                return ItemMatchesTagFilter(filter, preset.metadata.tags, state.common_state.filter_mode);
            case BrowserFilter::CommonCount: break;
        }

        // Preset-specific filters (by index beyond CommonCount).
        switch ((PresetBrowserFilter)index) {
            case PresetBrowserFilter::PresetType:
                return MatchesFilterValues(filter, state.common_state.filter_mode, [&](String, u64 key) {
                    return key == ToInt(preset.file_format);
                });
            case PresetBrowserFilter::Author:
                return MatchesFilterValues(filter, state.common_state.filter_mode, [&](String, u64 key) {
                    return key == preset.author_hash ||
                           (preset.metadata.author.size == 0 && key == k_no_preset_author_hash);
                });
            case PresetBrowserFilter::Count: PanicIfReached();
        }

        return false;
    });
}

static Optional<PresetCursor> IteratePreset(PresetBrowserContext const& context,
                                            PresetBrowserState const& state,
                                            PresetCursor cursor,
                                            SearchDirection direction,
                                            bool first) {
    if (context.presets_snapshot.folders.size == 0) return k_nullopt;

    if (cursor.folder_index >= context.presets_snapshot.folders.size) cursor.folder_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.preset_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.preset_index)>);
                --cursor.preset_index;
                break;
        }
    }

    for (usize preset_step = 0; preset_step < context.presets_snapshot.folders.size + 1; (
             {
                 ++preset_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.folder_index =
                             (cursor.folder_index + 1) % context.presets_snapshot.folders.size;
                         cursor.preset_index = 0;
                         break;
                     case SearchDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.folder_index)>);
                         --cursor.folder_index;
                         if (cursor.folder_index >= context.presets_snapshot.folders.size) // check wraparound
                             cursor.folder_index = context.presets_snapshot.folders.size - 1;
                         cursor.preset_index =
                             context.presets_snapshot.folders[cursor.folder_index]->folder->presets.size - 1;
                         break;
                 }
             })) {
        auto const& folder = context.presets_snapshot.folders[cursor.folder_index];

        for (; cursor.preset_index < folder->folder->presets.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.preset_index; break;
                         case SearchDirection::Backward: --cursor.preset_index; break;
                     }
                 })) {
            auto const& preset = folder->folder->presets[cursor.preset_index];

            if (ShouldSkipPreset(context, state, *folder, preset)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void
LoadPreset(PresetBrowserContext const& context, PresetBrowserState& state, PresetCursor cursor, bool scroll) {
    auto const& folder = context.presets_snapshot.folders[cursor.folder_index];
    auto const& preset = folder->folder->presets[cursor.preset_index];

    PathArena path_arena {PageAllocator::Instance()};
    LoadPresetFromFile(context.engine, folder->folder->FullPathForPreset(preset, path_arena));

    if (scroll) state.common_state.scroll_to_show_current = true;
}

void LoadAdjacentPreset(PresetBrowserContext const& context,
                        PresetBrowserState& state,
                        SearchDirection direction) {
    ASSERT(context.init);
    if (auto const current = ResolveCurrentLoadedCursor(context)) {
        if (auto const next = IteratePreset(context, state, *current, direction, false))
            LoadPreset(context, state, *next, true);
    } else if (auto const first =
                   IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, direction, true)) {
        LoadPreset(context, state, *first, true);
    }
}

void LoadRandomPreset(PresetBrowserContext const& context, PresetBrowserState& state) {
    ASSERT(context.init);
    auto const first =
        IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    auto cursor = *first;

    usize num_presets = 1;
    while (true) {
        if (auto const next = IteratePreset(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
            ++num_presets;
        } else {
            break;
        }
    }

    auto const random_pos = RandomIntInRange<usize>(context.engine.random_seed, 0, num_presets - 1);

    cursor = *first;
    for (usize i = 0; i < random_pos; ++i)
        cursor = *IteratePreset(context, state, cursor, SearchDirection::Forward, false);

    LoadPreset(context, state, cursor, true);
}

void PresetRightClickMenu(GuiBuilder& builder,
                          BrowserPopupContext& common_context,
                          BrowserPopupOptions const& options) {
    auto& context = *(PresetBrowserContext*)options.right_click_menu_user_data;
    auto const& menu_state = common_context.state.right_click_menu_state;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    struct PresetAndFolder {
        PresetFolder const& folder;
        PresetFolder::Preset const& preset;
    };

    auto const find_preset = [&](u64 hash) -> Optional<PresetAndFolder> {
        for (auto const& folder : context.presets_snapshot.folders) {
            for (auto const& preset : folder->folder->presets)
                if (preset.full_path_hash == hash) return PresetAndFolder {*folder->folder, preset};
        }
        return k_nullopt;
    };

    if (MenuItem(builder,
                 root,
                 {
                     .text = "Open Containing Folder",
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const preset = find_preset(menu_state.item_hash)) {
            OpenFolderInFileBrowser(
                path::Join(builder.arena, Array {preset->folder.scan_folder, preset->folder.folder}));
        }
    }
    {
        auto const preset = find_preset(menu_state.item_hash);
        auto const full_path =
            preset ? preset->folder.FullPathForPreset(preset->preset, builder.arena) : String {};
        auto const is_default = full_path.size && IsDefaultPreset(context.prefs, full_path);
        if (MenuItem(builder,
                     root,
                     {
                         .text = is_default ? "Clear Default Preset"_s : "Set as Default Preset"_s,
                         .is_selected = false,
                     })
                .button_fired) {
            if (is_default)
                ClearDefaultPreset(context.prefs);
            else if (full_path.size)
                SetDefaultPreset(context.prefs, full_path);
        }
    }
    if (MenuItem(builder,
                 root,
                 {
                     .text = "Send file to " TRASH_NAME,
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const preset = find_preset(menu_state.item_hash)) {
            auto const outcome =
                TrashFileOrDirectory(preset->folder.FullPathForPreset(preset->preset, builder.arena),
                                     builder.arena);
            auto const error_id = ({
                auto id = HashInit();
                HashUpdate(id, "preset-trash"_s);
                HashUpdate(id, preset->preset.full_path_hash);
                id;
            });
            if (outcome.HasValue()) {
                context.engine.error_notifications.RemoveError(error_id);
            } else if (auto item = context.engine.error_notifications.BeginWriteError(error_id)) {
                item->title = "Failed to send preset to trash"_s;
                item->error_code = outcome.Error();
            }
        }
    }
    // TODO: add rename option
}

void PresetFolderRightClickMenu(GuiBuilder& builder,
                                BrowserPopupContext& common_context,
                                BrowserPopupOptions const& options) {
    auto& context = *(PresetBrowserContext*)options.right_click_menu_user_data;
    auto const& menu_state = common_context.state.right_click_menu_state;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                                .name = "preset-browser.folder-menu"_s,
                            });

    auto const folder = FindFolderByHash(context, menu_state.item_hash);
    if (!folder) return;

    if (MenuItem(builder,
                 root,
                 {
                     .text = fmt::Format(builder.arena, "Open Folder in {}", GetFileBrowserAppName()),
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const filepath = FolderPath(folder, builder.arena)) OpenFolderInFileBrowser(*filepath);
    }

    if (MenuItem(builder,
                 root,
                 {
                     .text = "Uninstall (Send folder to " TRASH_NAME ")",
                     .is_selected = false,
                 })
            .button_fired) {
        if (HasNestedBank(*folder)) {
            auto const error_id = Hash(Array {SourceLocationHash(), folder->Hash()});
            if (auto item = context.engine.error_notifications.BeginWriteError(error_id)) {
                DEFER { context.engine.error_notifications.EndWriteError(*item); };
                item->title = "Cannot to delete preset folder"_s;
                item->message =
                    "This folder contains one or more preset banks as subfolders. Please delete them first."_s;
            }
        } else if (auto const folder_path = FolderPath(folder, builder.arena)) {
            auto cloned_path = Malloc::Instance().Clone(*folder_path);

            dyn::AssignFitInCapacity(context.confirmation_dialog_state.title, "Delete Preset Folder");
            fmt::Assign(
                context.confirmation_dialog_state.body_text,
                "Are you sure you want to delete the preset folder '{}'?\n\nThis will move the folder and all its contents to the {}. You can restore it from there if needed.",
                path::Filename(*folder_path),
                TRASH_NAME);

            context.confirmation_dialog_state.callback = [&error_notifications =
                                                              context.engine.error_notifications,
                                                          &gui_notifications = context.notifications,
                                                          &preset_server = context.preset_server,
                                                          cloned_path](ConfirmationDialogResult result) {
                DEFER { Malloc::Instance().Free(cloned_path.ToByteSpan()); };
                if (result == ConfirmationDialogResult::Ok) {
                    ArenaAllocatorWithInlineStorage<Kb(1)> scratch_arena {Malloc::Instance()};
                    auto const outcome = TrashFileOrDirectory(cloned_path, scratch_arena);
                    auto const id = HashMultiple(Array {"preset-folder-delete"_s, cloned_path});

                    if (outcome.HasValue()) {
                        error_notifications.RemoveError(id);
                        gui_notifications.AddOrUpdate(
                            id,
                            [p = DynamicArrayBounded<char, 200>(path::Filename(cloned_path))](
                                ArenaAllocator&) {
                                return NotificationDisplayInfo {
                                    .title = "Preset Folder Deleted",
                                    .message = p,
                                    .dismissable = true,
                                    .icon = NotificationDisplayInfo::IconType::Success,
                                };
                            });
                        if (auto const d = path::Directory(cloned_path)) RescanFolder(preset_server, *d);

                    } else if (auto item = error_notifications.BeginWriteError(id)) {
                        DEFER { error_notifications.EndWriteError(*item); };
                        item->title = "Failed to send preset folder to trash"_s;
                        item->error_code = outcome.Error();
                    }
                }
            };

            builder.imgui.OpenModalViewport(context.confirmation_dialog_state.k_id);
        }
    }
}

void PresetBrowserItems(GuiBuilder& builder, PresetBrowserContext& context, PresetBrowserState& state) {
    auto const root = DoBrowserItemsRoot(builder);

    auto const first =
        IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, SearchDirection::Forward, true);
    if (!first) {
        DoBrowserEmptyListMessage(builder, state.common_state, root, "presets"_s);
        return;
    }

    auto const current_loaded_cursor = ResolveCurrentLoadedCursor(context);

    auto const default_preset_path = ({
        auto const d = ResolveDefaultPreset(context.prefs);
        d ? d->path : String {};
    });

    Optional<u64> previous_folder_hash = {};

    Optional<BrowserSection> folder_section;

    auto const total_presets = ({
        usize n = 0;
        for (auto const& folder : context.presets_snapshot.folders)
            n += folder->folder->presets.size;
        n;
    });

    struct PendingFavouriteToggle {
        u64 uuid;
        bool was_favourite;
    };
    Optional<PendingFavouriteToggle> pending_favourite_toggle {};

    auto cursor = *first;
    for (usize guard = 0;; ++guard) {
        ASSERT(guard <= total_presets, "render loop exceeded preset count — filter set mutated mid-frame");
        auto const& preset_folder = context.presets_snapshot.folders[cursor.folder_index];
        auto const& preset = preset_folder->folder->presets[cursor.preset_index];
        auto const folder_hash = preset_folder->node.Hash();
        auto const new_folder = folder_hash != previous_folder_hash;

        if (new_folder) {
            previous_folder_hash = folder_hash;
            folder_section = BrowserSection {
                .state = state.common_state,
                .id = folder_hash,
                .parent = root,
                .folder = &preset_folder->node,
                .skip_root_folder = true,
                .skip_heading = IsSingleFolderFilterSelected(state.common_state, folder_hash),
                .tooltip_placement = TooltipPlacement::RightThenLeft,
                .right_click_menu = PresetFolderRightClickMenu,
            };
        }

        auto const is_current = current_loaded_cursor && *current_loaded_cursor == cursor;

        if (folder_section->Do(builder).tag != BrowserSection::State::Collapsed) {
            auto const is_favourite = IsFavourite(context.prefs, FavouriteItemKey(), preset.preset_uuid);

            auto const item = DoBrowserItem(
                builder,
                state.common_state,
                BrowserItemOptions {
                    .parent = folder_section->Do(builder).Get<Box>(),
                    .id_extra = preset.full_path_hash,
                    .text = preset.name,
                    .value_popup =
                        FunctionRef<String()>([&preset,
                                               &scratch = builder.arena,
                                               &frame_context = context.frame_context]() -> String {
                            DynamicArray<char> buffer {scratch};

                            if (preset.metadata.description.size)
                                fmt::Append(buffer, "{}\n\n", preset.metadata.description);

                            dyn::AppendSpan(buffer, "Tags: ");
                            if (preset.metadata.tags.AnyValuesSet()) {
                                bool first = true;
                                preset.metadata.tags.ForEachSetBit([&](usize bit) {
                                    if (!first) dyn::AppendSpan(buffer, ", ");
                                    first = false;
                                    dyn::AppendSpan(buffer, GetTagInfo((TagType)bit).name);
                                });
                            } else {
                                dyn::AppendSpan(buffer, "none");
                            }

                            if (preset.used_libraries.size) {
                                dyn::AppendSpan(buffer, "\n\nRequires libraries: ");
                                for (auto const [library, _] : preset.used_libraries) {
                                    auto const maybe_lib = frame_context.lib_table.Find(library);
                                    if (!maybe_lib || !*maybe_lib) {
                                        auto const lib_name =
                                            sample_lib::LookupLibraryIdString(library).ValueOr("Unknown"_s);
                                        fmt::Append(buffer, "{} (not installed)", lib_name);
                                    } else
                                        dyn::AppendSpan(buffer, (*maybe_lib)->name);
                                    if (preset.used_libraries.size == 2)
                                        dyn::AppendSpan(buffer, " and ");
                                    else
                                        dyn::AppendSpan(buffer, ", ");
                                }
                                if (preset.used_libraries.size == 2)
                                    dyn::Pop(buffer, 5);
                                else
                                    dyn::Pop(buffer, 2);
                                dyn::AppendSpan(buffer, ".");
                            }

                            if (preset.metadata.author.size)
                                fmt::Append(buffer, "\n\nAuthor: {}.", preset.metadata.author);

                            return buffer.ToOwnedSpan();
                        }),
                    .tooltip = BrowserItemLoadTooltip(builder.arena, "preset"_s),
                    .tooltip_footer = k_right_click_tooltip_footer,
                    .item_id = preset.full_path_hash,
                    .is_current = is_current,
                    .is_favourite = is_favourite,
                    .is_default = default_preset_path.size &&
                                  path::Equal(preset_folder->folder->FullPathForPreset(preset, builder.arena),
                                              default_preset_path),
                    .is_tab_item = new_folder,
                    .icons = ({
                        // The items are normally ordered, but we want special handling for the
                        // Mirage Compatibility library and unknown libraries.

                        decltype(BrowserItemOptions::icons) icons {};
                        Optional<ImageID> mirage_compat_icon = k_nullopt;
                        usize num_unknown = 0;
                        for (auto const [lib_id, _] : preset.used_libraries) {
                            auto const imgs = GetLibraryImages(context.library_images,
                                                               builder.imgui,
                                                               lib_id,
                                                               context.sample_library_server,
                                                               context.engine.instance_index,
                                                               LibraryImagesTypes::Icon);
                            if (!imgs.icon)
                                ++num_unknown;
                            else if (lib_id == sample_lib::k_mirage_compat_library_id)
                                mirage_compat_icon = imgs.icon;
                            else
                                dyn::Emplace(icons, *imgs.icon);
                        }
                        for (auto const _ : Range(num_unknown))
                            dyn::Emplace(icons, String(ICON_FA_CIRCLE_QUESTION));
                        if (mirage_compat_icon) dyn::Emplace(icons, *mirage_compat_icon);

                        if (!PRODUCTION_BUILD && preset.file_format == PresetFormat::Floe &&
                            (preset.metadata.tags.NumSet() <= 3 || preset.metadata.author.size == 0 ||
                             preset.metadata.description.size == 0))
                            dyn::Emplace(icons, String(ICON_FA_TRIANGLE_EXCLAMATION));

                        icons;
                    }),
                    .notifications = context.notifications,
                    .store = context.persistent_store,
                });

            // Right-click menu.
            DoRightClickMenuForBox(builder,
                                   state.common_state,
                                   item.box,
                                   preset.full_path_hash,
                                   PresetRightClickMenu);

            if (is_current) ScrollBrowserToShowCurrent(builder, state.common_state, item.box);

            if (item.fired && !is_current) LoadPreset(context, state, cursor, false);

            if (item.favourite_toggled)
                pending_favourite_toggle = PendingFavouriteToggle {preset.preset_uuid, is_favourite};
        } else if (is_current) {
            ScrollBrowserToShowCurrent(builder, state.common_state, folder_section->heading_box);
        }

        if (auto next = IteratePreset(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }

    if (pending_favourite_toggle)
        ToggleFavourite(context.prefs,
                        FavouriteItemKey(),
                        pending_favourite_toggle->uuid,
                        pending_favourite_toggle->was_favourite);
}

static String PresetFormatName(PresetFormat format) {
    switch (format) {
        case PresetFormat::Floe: return "Floe";
        case PresetFormat::Mirage: return "Mirage";
        case PresetFormat::Count: break;
    }
    PanicIfReached();
}

// The values of the preset-type and preset-author attributes. get_parent gives the box to put each value
// in: Browse mode's page, or Filter mode's section, which gives nothing back when collapsed.
static void
DoPresetTypeValues(GuiBuilder& builder,
                   Array<FilterItemInfo, ToInt(PresetFormat::Count)> const& preset_type_filter_info,
                   PresetBrowserState& state,
                   TrivialFunctionRef<Optional<Box>()> get_parent) {
    for (auto const type_index : Range(ToInt(PresetFormat::Count))) {
        auto const& info = preset_type_filter_info[type_index];
        if (info.total_available == 0) continue;

        auto const name = PresetFormatName((PresetFormat)type_index);
        if (!MatchesFilterSearch(name, state.common_state.filter_search)) continue;

        auto const parent = get_parent();
        if (!parent) break;

        DoFilterButton(
            builder,
            state.common_state,
            info,
            {
                .common =
                    {
                        .parent = *parent,
                        .id_extra = (u64)type_index,
                        .is_selected =
                            state.common_state.Filter(PresetBrowserFilter::PresetType).Contains(type_index),
                        .text = name,
                        .match_phrase = "of this type"_s,
                        .filter = state.common_state.Filter(PresetBrowserFilter::PresetType),
                        .clicked_key = type_index,
                        .filter_mode = state.common_state.filter_mode,
                    },
            });
    }
}

static void DoPresetAuthorValues(GuiBuilder& builder,
                                 OrderedHashTable<String, FilterItemInfo> const& preset_authors,
                                 PresetBrowserState& state,
                                 TrivialFunctionRef<Optional<Box>()> get_parent) {
    for (auto const [author, author_info, author_hash] : preset_authors) {
        if (!MatchesFilterSearch(author, state.common_state.filter_search)) continue;

        auto const parent = get_parent();
        if (!parent) break;

        DoFilterButton(
            builder,
            state.common_state,
            author_info,
            {
                .common =
                    {
                        .parent = *parent,
                        .id_extra = author_hash,
                        .is_selected =
                            state.common_state.Filter(PresetBrowserFilter::Author).Contains(author_hash),
                        .text = author,
                        .match_phrase = "by this author"_s,
                        .filter = state.common_state.Filter(PresetBrowserFilter::Author),
                        .clicked_key = author_hash,
                        .filter_mode = state.common_state.filter_mode,
                    },
            });
    }
}

// Filter mode: the browser's own sections of the tree, below the common ones.
static void
PresetBrowserExtraFilters(GuiBuilder& builder,
                          PresetBrowserContext& context,
                          OrderedHashTable<String, FilterItemInfo> const& preset_authors,
                          Array<FilterItemInfo, ToInt(PresetFormat::Count)> const& preset_type_filter_info,
                          PresetBrowserState& state,
                          Box const& parent) {
    // We only show the preset type filter if we have both types of presets.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1 &&
        !AllOf(preset_type_filter_info, [](FilterItemInfo const& i) { return i.total_available == 0; })) {
        BrowserSection section {
            .state = state.common_state,
            .id = HashFnv1a("preset-type-section"),
            .parent = parent,
            .heading = "Preset type"_s,
            .icon = ICON_FA_FLOPPY_DISK,
            .capitalise = true,
            .multiline_contents = true,
            .default_collapsed = true,
            .dark_mode = true,
            .keyboard_focusable = true,
            .store = &context.persistent_store,
        };
        DoPresetTypeValues(builder, preset_type_filter_info, state, [&]() -> Optional<Box> {
            return SectionContents(builder, section);
        });
    }

    if (preset_authors.size) {
        BrowserSection section {
            .state = state.common_state,
            .id = HashFnv1a("preset-author-section"),
            .parent = parent,
            // "Preset authors" to distinguish from the library-authors section.
            .heading = "Preset authors"_s,
            .icon = ICON_FA_PEN,
            .capitalise = true,
            .multiline_contents = true,
            .default_collapsed = true,
            .dark_mode = true,
            .keyboard_focusable = true,
            .store = &context.persistent_store,
        };
        DoPresetAuthorValues(builder, preset_authors, state, [&]() -> Optional<Box> {
            return SectionContents(builder, section);
        });
    }
}

void DoPresetBrowser(GuiBuilder& builder, PresetBrowserContext& context, PresetBrowserState& state) {
    constexpr auto k_folders_section_id = HashFnv1a("preset-folders-section");
    constexpr auto k_factory_banks_section_id = HashFnv1a("preset-factory-banks-section");
    constexpr auto k_user_banks_section_id = HashFnv1a("preset-user-banks-section");

    bool const is_screenshot_request = IsScreenshotRequest("uninstall-preset-bank"_s);
    if (is_screenshot_request) {
        if (!builder.imgui.IsModalOpen(state.k_panel_id)) builder.imgui.OpenModalViewport(state.k_panel_id);
        for (auto const id :
             Array {k_folders_section_id, k_factory_banks_section_id, k_user_banks_section_id})
            if (!Contains(state.common_state.expanded_filter_headers, id))
                dyn::Append(state.common_state.expanded_filter_headers, id);
    }

    if (IsScreenshotRequest("browser-preset-browse"_s) && !builder.imgui.IsModalOpen(state.k_panel_id))
        builder.imgui.OpenModalViewport(state.k_panel_id);

    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    context.Init(builder.arena);
    DEFER { context.Deinit(); };

    TagsFilters tags_filters {};
    tags_filters.available_tags = context.presets_snapshot.used_tags;

    auto libraries =
        OrderedHashTable<sample_lib::LibraryId, FilterItemInfo, NoHash, LibraryIdLessThanFilterInfo>::Create(
            builder.arena,
            context.presets_snapshot.used_libraries.size);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(builder.arena,
                                                         context.presets_snapshot.used_libraries.size);

    auto preset_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(builder.arena,
                                                         context.presets_snapshot.authors.size + 1);

    Array<FilterItemInfo, ToInt(PresetFormat::Count)> preset_type_filter_info;

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(builder.arena, 64);

    FilterItemInfo favourites_info {};
    u32 num_results = 0;

    for (auto const& [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        auto const folder_pack = ContainingPresetBank(&folder->node);
        for (auto const& preset : folder->folder->presets) {
            bool const skip = ShouldSkipPreset(context, state, *folder, preset);
            if (!skip) ++num_results;

            if (IsFavourite(context.prefs, FavouriteItemKey(), preset.preset_uuid)) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

            preset.metadata.tags.ForEachSetBit([&](usize bit) {
                auto& i = tags_filters.tags[bit];
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            });

            if (!preset.metadata.tags.AnyValuesSet()) {
                tags_filters.has_untagged = true;
                auto& i = tags_filters.untagged_info;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            DynamicArrayBounded<Pair<String, u64>, k_num_layers + 1> library_authors_used;

            for (auto const [lib_id, lib_id_hash] : preset.used_libraries) {
                auto& i = libraries.FindOrInsertWithoutGrowing(lib_id, {}, lib_id_hash).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;

                if (auto const lib = context.frame_context.lib_table.Find(lib_id))
                    if (!FindIf(library_authors_used, [&](Pair<String, u64> const& la) {
                            return la.second == (*lib)->author_hash;
                        })) {
                        dyn::Append(library_authors_used, {(*lib)->author, (*lib)->author_hash});
                    }
            }

            for (auto const& author : library_authors_used) {
                auto& i =
                    library_authors.FindOrInsertWithoutGrowing(author.first, {}, author.second).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            {
                auto const author = preset.metadata.author.size ? preset.metadata.author : k_no_preset_author;
                auto const hash = preset.metadata.author.size ? preset.author_hash : k_no_preset_author_hash;
                auto& i = preset_authors.FindOrInsertWithoutGrowing(author, {}, hash).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            {
                auto& i = preset_type_filter_info[ToInt(preset.file_format)];
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            for (auto f = &folder->node; f; f = f->parent) {
                auto& i = folders.FindOrInsertGrowIfNeeded(builder.arena, f, {}).element.data;
                if (ContainingPresetBank(f) != folder_pack) break;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }
        }
    }

    state.common_state.items_still_loading = AreFoldersScanning(context.preset_server);

    // Factory and user-made banks get their own sections, but only when both kinds exist: a single-kind
    // pool shouldn't pay for a distinction it doesn't have.
    auto const grouped_banks = ({
        bool any_factory = false;
        bool any_user = false;
        for (auto const listing : context.presets_snapshot.banks)
            (IsFactoryPresetBank(listing->node) ? any_factory : any_user) = true;
        any_factory&& any_user;
    });

    auto const bank_section_id = [&](FolderNode const& bank) -> u64 {
        if (!grouped_banks) return k_folders_section_id;
        return IsFactoryPresetBank(bank) ? k_factory_banks_section_id : k_user_banks_section_id;
    };
    // The library whose artwork stands in for the bank's.
    auto const bank_visual_library_id = [&](FolderNode const& bank) -> Optional<sample_lib::LibraryId> {
        if (auto const m = PresetBankAtNode(bank); m && m->library_for_visuals_id) {
            auto const maybe_lib = context.frame_context.lib_table.Find(*m->library_for_visuals_id);
            if (maybe_lib && *maybe_lib) return *m->library_for_visuals_id;
        }
        return AllPresetsSingleLibrary(bank);
    };
    auto const collection_of_bank = [&](FolderNode const& bank) -> Optional<BrowserCollection> {
        auto const info = folders.Find(&bank);
        auto const metadata = PresetBankAtNode(bank);
        return BrowserCollection {
            .filter = BrowserFilter::Folder,
            .key = bank.Hash(),
            .name = bank.display_name.size ? bank.display_name : bank.name,
            .section_id = bank_section_id(bank),
            .library_id = bank_visual_library_id(bank),
            .num_items = info ? info->total_available : 0,
            .subtext = metadata ? metadata->subtitle : "Preset bank"_s,
            .version = metadata && metadata->revision ? Optional<u32> {(u32)metadata->revision} : k_nullopt,
            .right_click_menu = PresetFolderRightClickMenu,
        };
    };

    auto const current_item = ({
        using Visibility = CurrentItemStatus::Visibility;
        CurrentItemStatus status {};
        auto const& engine = context.engine;
        if (auto const cursor = ResolveCurrentLoadedCursor(context)) {
            auto const& folder = *context.presets_snapshot.folders[cursor->folder_index];
            auto const& preset = folder.folder->presets[cursor->preset_index];
            status.name = preset.name;
            status.collection = collection_of_bank(*CollectionRootFolder(&folder.node));
            if (ShouldSkipPreset(context, state, folder, preset)) {
                status.visibility = Visibility::HiddenByFilters;
            } else {
                status.section_id = folder.node.Hash();
                status.visibility =
                    IsBrowserSectionCollapsed(state.common_state, status.section_id, status.section_id)
                        ? Visibility::InCollapsedSection
                        : Visibility::Shown;
            }
        } else if (auto const path = engine.pending_state_change
                                         ? (String)engine.pending_state_change->preset_path
                                         : (String)engine.pinned_snapshot.preset_path;
                   path.size) {
            status.name = engine.pending_state_change
                              ? (String)engine.pending_state_change->snapshot.extras.display_name
                              : (String)engine.pinned_snapshot.state.extras.display_name;
            if (state.common_state.items_still_loading) {
                status.visibility = Visibility::Loading;
            } else {
                status.visibility = Visibility::NotInList;
                status.not_in_list_reason = "it isn't in any scanned preset folder"_s;
            }
        }
        status;
    });

    auto const do_preset_type_values = [&](GuiBuilder& builder, Box const& parent) {
        DoPresetTypeValues(builder, preset_type_filter_info, state, [&]() -> Optional<Box> {
            return parent;
        });
    };
    auto const do_preset_author_values = [&](GuiBuilder& builder, Box const& parent) {
        DoPresetAuthorValues(builder, preset_authors, state, [&]() -> Optional<Box> { return parent; });
    };

    DynamicArrayBounded<BrowseAttribute, 2> extra_browse_attributes {};
    {
        auto const num_types = ({
            u32 n = 0;
            for (auto const& info : preset_type_filter_info)
                if (info.total_available) ++n;
            n;
        });
        if (context.presets_snapshot.has_preset_type.NumSet() > 1 && num_types) {
            dyn::Append(
                extra_browse_attributes,
                {
                    .filter_index = (u8)PresetBrowserFilter::PresetType,
                    .entry =
                        {
                            .name = "Preset type"_s,
                            .icon = ICON_FA_FLOPPY_DISK,
                            .count = num_types,
                            .tooltip =
                                "Separate presets made in Floe from ones made in Mirage, Floe's predecessor."_s,
                        },
                    .do_values = do_preset_type_values,
                });
        }
        if (preset_authors.size) {
            dyn::Append(extra_browse_attributes,
                        {
                            .filter_index = (u8)PresetBrowserFilter::Author,
                            .entry =
                                {
                                    .name = "Preset authors"_s,
                                    .icon = ICON_FA_PEN,
                                    .count = (u32)preset_authors.size,
                                    .tooltip = "Find presets by who made them."_s,
                                },
                            .do_values = do_preset_author_values,
                        });
        }
    }

    auto const bank_matches_group = [&](FolderNode const& folder, Optional<bool> factory_only) {
        if (factory_only && IsFactoryPresetBank(folder) != *factory_only) return false;
        if (!folders.Find(&folder)) return false;
        return MatchesFilterSearch(folder.display_name.size ? folder.display_name : folder.name,
                                   state.common_state.filter_search);
    };

    auto const do_bank = [&](GuiBuilder& builder,
                             Box const& parent,
                             FolderNode const* folder,
                             FilterItemInfo const& info,
                             bool name_for_screenshot) {
        auto const folder_name = folder->display_name.size ? folder->display_name : folder->name;
        auto const folder_hash = folder->Hash();

        auto const screenshot_name = name_for_screenshot ? "preset-browser.first-bank"_s : String {};

        DoFilterCollection(
            builder,
            state.common_state,
            info,
            FilterCollectionOptions {
                .common =
                    {
                        .parent = parent,
                        .id_extra = folder_hash,
                        .is_selected = state.common_state.Filter(BrowserFilter::Folder).Contains(folder_hash),
                        .text = folder_name,
                        .value_popup = folder->name != folder_name ? TooltipString {folder->name}
                                                                   : TooltipString {k_nullopt},
                        .filter = state.common_state.Filter(BrowserFilter::Folder),
                        .clicked_key = folder_hash,
                        .filter_mode = state.common_state.filter_mode,
                    },
                .icon =
                    {
                        .library_id = bank_visual_library_id(*folder),
                        .library_images = context.library_images,
                        .sample_library_server = context.sample_library_server,
                        .instance_index = context.engine.instance_index,
                    },
                .folder_infos = folders,
                .folder = folder,
                .all_items_suffix = " Presets"_s,
                .collection_noun = "preset bank"_s,
                .default_collapsed = true,
                .right_click_menu = PresetFolderRightClickMenu,
                .store = &context.persistent_store,
                .name = screenshot_name,
            });
    };

    // The banks of one group. get_parent gives the box to put each bank in: in Browse mode that's the
    // filters panel itself, in Filter mode the tree section, which creates itself on demand and gives
    // nothing back when collapsed. first_group names its first bank for the screenshot that wants a bank
    // and its menu.
    auto const do_banks = [&](GuiBuilder& builder,
                              Optional<bool> factory_only,
                              bool first_group,
                              TrivialFunctionRef<Optional<Box>()> get_parent) {
        bool named_a_bank = false;
        for (auto const listing : context.presets_snapshot.banks) {
            if (!bank_matches_group(listing->node, factory_only)) continue;
            auto const parent = get_parent();
            if (!parent) break;
            do_bank(builder,
                    *parent,
                    &listing->node,
                    *folders.Find(&listing->node),
                    first_group && !named_a_bank);
            named_a_bank = true;
        }
    };

    auto const num_banks = [&](Optional<bool> factory_only) {
        u32 count = 0;
        for (auto const listing : context.presets_snapshot.banks)
            if (bank_matches_group(listing->node, factory_only)) ++count;
        return count;
    };

    auto const do_factory_banks = [&](GuiBuilder& builder, Box const& parent) {
        do_banks(builder, true, true, [&]() -> Optional<Box> { return parent; });
    };
    auto const do_user_banks = [&](GuiBuilder& builder, Box const& parent) {
        do_banks(builder, false, false, [&]() -> Optional<Box> { return parent; });
    };
    auto const do_all_banks = [&](GuiBuilder& builder, Box const& parent) {
        do_banks(builder, k_nullopt, true, [&]() -> Optional<Box> { return parent; });
    };

    auto const browse_collection_sections = ({
        DynamicArrayBounded<BrowseCollectionSection, 2> sections {};
        if (grouped_banks) {
            dyn::Append(sections,
                        {
                            .id = k_factory_banks_section_id,
                            .entry =
                                {
                                    .name = "Factory preset banks"_s,
                                    .icon = ICON_FA_INDUSTRY,
                                    .count = num_banks(true),
                                    .tooltip = "Preset banks that came with your libraries."_s,
                                },
                            .right_click_menu = PresetFolderRightClickMenu,
                            .do_collections = do_factory_banks,
                        });
            dyn::Append(sections,
                        {
                            .id = k_user_banks_section_id,
                            .entry =
                                {
                                    .name = "User presets"_s,
                                    .icon = ICON_FA_USER,
                                    .count = num_banks(false),
                                    .tooltip = "Preset banks you've made or installed yourself."_s,
                                },
                            .right_click_menu = PresetFolderRightClickMenu,
                            .do_collections = do_user_banks,
                        });
        } else {
            dyn::Append(
                sections,
                {
                    .id = k_folders_section_id,
                    .entry =
                        {
                            .name = "Preset banks"_s,
                            .icon = ICON_FA_BOX_OPEN,
                            .count = num_banks(k_nullopt),
                            .tooltip =
                                "Your preset banks, one per row. A preset bank is a folder of presets, either from a library or one you've added yourself."_s,
                        },
                    .right_click_menu = PresetFolderRightClickMenu,
                    .do_collections = do_all_banks,
                });
        }
        sections;
    });

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoBrowserModal(
        builder,
        {
            .browser_id = state.k_panel_id,
            .sample_library_server = context.sample_library_server,
            .library_images = context.library_images,
            .preferences = context.prefs,
            .store = context.persistent_store,
            .state = state.common_state,
            .instance_index = context.engine.instance_index,
        },
        BrowserPopupOptions {
            .height = ({
                auto const window_height = GuiIo().in.window_size.height;
                auto const button_bottom = state.common_state.absolute_button_rect.Bottom();
                auto const available_height = window_height - button_bottom - WwToPixels(20.0f);
                PixelsToWw(available_height);
            }),
            .results_width = 320,
            .filters_col_width = 320,
            .size_store_id = HashFnv1a("preset-browser"),
            .flush_with_opener = true,
            .item_type_name = "preset",
            .plural_item_type_name = "presets",
            .do_items = [&](GuiBuilder& builder) { PresetBrowserItems(builder, context, state); },
            .filter_search_placeholder_text = "Search preset banks/tags",
            .item_search_placeholder_text = "Search presets",
            .item_search_tooltip = "Search the current results by preset name only. " MODIFIER_KEY_NAME
                                   "+F jumps here from anywhere in the browser.",
            .current_item = current_item,
            .browse_scope = CurrentBrowseScope(state.common_state,
                                               {
                                                   .collection_noun = "preset bank"_s,
                                                   .folders = folders,
                                                   .collection_of_root = collection_of_bank,
                                               }),
            .library_filters =
                LibraryFilters {
                    .libraries_table = context.frame_context.lib_table,
                    .library_images = context.library_images,
                    .instance_index = context.engine.instance_index,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .error_notifications = context.engine.error_notifications,
                    .notifications = context.notifications,
                    .confirmation_dialog_state = context.confirmation_dialog_state,
                },
            .tags_filters = tags_filters,
            .extra_browse_attributes = extra_browse_attributes,
            .browse_collection_sections = browse_collection_sections,
            .do_extra_filters_top =
                [&](GuiBuilder& builder, Box const& parent) {
                    auto const do_section =
                        [&](u64 id, String heading, String icon, Optional<bool> group, bool first_group) {
                            BrowserSection section {
                                .state = state.common_state,
                                .id = id,
                                .parent = parent,
                                .heading = heading,
                                .icon = icon,
                                .multiline_contents = false,
                                .dark_mode = true,
                                .keyboard_focusable = true,
                                .right_click_menu = PresetFolderRightClickMenu,
                                .store = &context.persistent_store,
                            };
                            do_banks(builder, group, first_group, [&]() -> Optional<Box> {
                                if (section.Do(builder).tag == BrowserSection::State::Collapsed)
                                    return k_nullopt;
                                return section.Do(builder).Get<Box>();
                            });
                        };

                    if (!grouped_banks) {
                        do_section(k_folders_section_id, "PRESET BANKS"_s, ICON_FA_BOX_OPEN, k_nullopt, true);
                    } else {
                        do_section(k_factory_banks_section_id,
                                   "FACTORY PRESET BANKS"_s,
                                   ICON_FA_INDUSTRY,
                                   true,
                                   true);
                        do_section(k_user_banks_section_id, "USER PRESETS"_s, ICON_FA_USER, false, false);
                    }
                },
            .do_extra_filters_bottom =
                [&](GuiBuilder& builder, Box const& parent) {
                    PresetBrowserExtraFilters(builder,
                                              context,
                                              preset_authors,
                                              preset_type_filter_info,
                                              state,
                                              parent);
                },
            .favourites_filter_info = favourites_info,
            .num_results = num_results,
            .right_click_menu_user_data = &context,
        });
}
