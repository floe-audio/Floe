// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_preset_browser.hpp"

#include "os/filesystem.hpp"

#include "engine/engine.hpp"
#include "engine/favourite_items.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui2_common_browser.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "preset_server/preset_server.hpp"

constexpr String k_no_preset_author = "<no author>"_s;

inline prefs::Key FavouriteItemKey() { return "favourite-preset"_s; }

static FolderNode const* FindFolderByHash(PresetBrowserContext const& context, u64 folder_hash) {
    FolderNode const* result = nullptr;

    for (auto root : context.presets_snapshot.preset_banks) {
        ForEachNode((FolderNode*)root, [&](FolderNode const* node) {
            if (result) return;
            if (node->Hash() == folder_hash) result = node;
        });
    }

    return result;
}

static Optional<String> FolderPath(FolderNode const* folder, ArenaAllocator& arena) {
    if (!folder) return k_nullopt;

    DynamicArrayBounded<String, 20> parts;
    for (auto f = folder; f; f = f->parent)
        dyn::Append(parts, f->name);
    Reverse(parts);

    return path::Join(arena, parts);
}

struct PresetCursor {
    bool operator==(PresetCursor const& o) const = default;
    usize folder_index;
    usize preset_index;
};

static Optional<PresetCursor> CurrentCursor(PresetBrowserContext const& context, Optional<String> path) {
    if (!path) return k_nullopt;

    for (auto const [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        ASSERT(folder->folder);
        auto const preset_index = folder->folder->MatchFullPresetPath(*path);
        if (preset_index) return PresetCursor {folder_index, *preset_index};
    }

    return k_nullopt;
}

static bool ShouldSkipPreset(PresetBrowserContext const& context,
                             PresetBrowserState const& state,
                             PresetFolderListing const& folder,
                             PresetFolder::Preset const& preset) {
    ASSERT(folder.folder);
    if (state.common_state.search.size &&
        (!ContainsCaseInsensitiveAscii(preset.name, state.common_state.search) &&
         !ContainsCaseInsensitiveAscii(folder.folder->folder, state.common_state.search)))
        return true;

    bool filtering_on = false;

    if (state.common_state.favourites_only) {
        filtering_on = true;
        if (!IsFavourite(context.prefs, FavouriteItemKey(), preset.file_hash)) {
            if (state.common_state.filter_mode == FilterMode::MultipleAnd ||
                state.common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
        }
    }

    if (state.common_state.selected_folder_hashes.HasSelected()) {
        filtering_on = true;
        for (auto const& folder_hash : state.common_state.selected_folder_hashes) {
            if (!IsInsideFolder(&folder, folder_hash.hash)) {
                if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (state.common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    // If multiple preset types exist, we offer a way to filter by them.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1) {
        if (state.selected_preset_types.HasSelected()) {
            filtering_on = true;
            if (!state.selected_preset_types.Contains(ToInt(preset.file_format))) {
                if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (state.common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    if (state.common_state.selected_library_hashes.HasSelected()) {
        filtering_on = true;
        for (auto const& selected_hash : state.common_state.selected_library_hashes) {
            if (!preset.used_libraries.ContainsSkipKeyCheck(selected_hash.hash)) {
                if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (state.common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    if (state.common_state.selected_library_author_hashes.HasSelected()) {
        filtering_on = true;

        for (auto [lib_id, _] : preset.used_libraries) {
            auto const maybe_lib = context.frame_context.lib_table.Find(lib_id);
            if (!maybe_lib) continue;
            auto const& lib = *maybe_lib;

            auto const author_hash = Hash(lib->author);
            auto const contains = state.common_state.selected_library_author_hashes.Contains(author_hash);
            if (!contains) {
                if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (state.common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    if (state.selected_author_hashes.HasSelected()) {
        filtering_on = true;
        auto const author_hash = Hash(preset.metadata.author);
        if (!(state.selected_author_hashes.Contains(author_hash) ||
              (preset.metadata.author.size == 0 &&
               state.selected_author_hashes.Contains(Hash(k_no_preset_author))))) {
            if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                return true;
            else if (state.common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
        }
    }

    if (state.common_state.selected_tags_hashes.HasSelected()) {
        filtering_on = true;
        for (auto const& selected_hash : state.common_state.selected_tags_hashes) {
            if (!(preset.metadata.tags.ContainsSkipKeyCheck(selected_hash.hash) ||
                  (selected_hash.hash == Hash(k_untagged_tag_name) && preset.metadata.tags.size == 0))) {
                if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (state.common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    if (filtering_on && state.common_state.filter_mode == FilterMode::MultipleOr) {
        // Filtering is applied, but the item does not match any of the selected filters.
        return true;
    }

    return false;
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

    if (scroll) state.scroll_to_show_selected = true;
}

static Optional<String> CurrentPath(Engine const& engine) {
    if (engine.pending_state_change) return engine.pending_state_change->snapshot.name.Path();
    return engine.last_snapshot.name_or_path.Path();
}

void LoadAdjacentPreset(PresetBrowserContext const& context,
                        PresetBrowserState& state,
                        SearchDirection direction) {
    ASSERT(context.init);
    auto const current_path = CurrentPath(context.engine);

    if (current_path) {
        if (auto const current = CurrentCursor(context, *current_path)) {
            if (auto const next = IteratePreset(context, state, *current, direction, false))
                LoadPreset(context, state, *next, true);
        }
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

void PresetRightClickMenu(GuiBoxSystem& box_system,
                          PresetBrowserContext& context,
                          PresetBrowserState&,
                          RightClickMenuState const& menu_state) {
    auto const root = DoBox(box_system,
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

    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Open Containing Folder",
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const preset = find_preset(menu_state.item_hash)) {
            OpenFolderInFileBrowser(
                path::Join(box_system.arena, Array {preset->folder.scan_folder, preset->folder.folder}));
        }
    }
    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Send file to " TRASH_NAME,
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const preset = find_preset(menu_state.item_hash)) {
            auto const outcome =
                TrashFileOrDirectory(preset->folder.FullPathForPreset(preset->preset, box_system.arena),
                                     box_system.arena);
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

void PresetFolderRightClickMenu(GuiBoxSystem& box_system,
                                PresetBrowserContext& context,
                                PresetBrowserState& state,
                                RightClickMenuState const& menu_state) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    auto const folder = FindFolderByHash(context, menu_state.item_hash);

    if (MenuItem(box_system,
                 root,
                 {
                     .text = fmt::Format(box_system.arena, "Open Folder in {}", GetFileBrowserAppName()),
                     .is_selected = false,
                 })
            .button_fired) {
        if (auto const filepath = FolderPath(folder, box_system.arena)) OpenFolderInFileBrowser(*filepath);
    }

    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Uninstall (Send folder to " TRASH_NAME ")",
                     .is_selected = false,
                 })
            .button_fired) {
        if (({
                bool has_child_pack = false;
                auto root_pack = PresetBankInfoAtNode(*folder);
                ForEachNode((FolderNode*)folder, [&](FolderNode const* node) {
                    if (has_child_pack) return;
                    if (node == folder) return;
                    auto bank = PresetBankInfoAtNode(*node);
                    if (!bank) return;
                    if (root_pack != bank) has_child_pack = true;
                });
                has_child_pack;
            })) {
            auto const error_id = Hash(Array {SourceLocationHash(), folder->Hash()});
            if (auto item = context.engine.error_notifications.BeginWriteError(error_id)) {
                DEFER { context.engine.error_notifications.EndWriteError(*item); };
                item->title = "Cannot to delete preset folder"_s;
                item->message =
                    "This folder contains one or more preset banks as subfolders. Please delete them first."_s;
            }
        } else if (auto const folder_path = FolderPath(folder, box_system.arena)) {
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
                                                          cloned_path](ConfirmationDialogResult result) {
                DEFER { Malloc::Instance().Free(cloned_path.ToByteSpan()); };
                if (result == ConfirmationDialogResult::Ok) {
                    ArenaAllocatorWithInlineStorage<Kb(1)> scratch_arena {Malloc::Instance()};
                    auto const outcome = TrashFileOrDirectory(cloned_path, scratch_arena);
                    auto const id = HashMultiple(Array {"preset-folder-delete"_s, cloned_path});

                    if (outcome.HasValue()) {
                        error_notifications.RemoveError(id);
                        *gui_notifications.FindOrAppendUninitalisedOverwrite(id) = {
                            .get_diplay_info =
                                [p = DynamicArrayBounded<char, 200>(path::Filename(cloned_path))](
                                    ArenaAllocator&) {
                                    return NotificationDisplayInfo {
                                        .title = "Preset Folder Deleted",
                                        .message = p,
                                        .dismissable = true,
                                        .icon = NotificationDisplayInfo::IconType::Success,
                                    };
                                },
                            .id = id,
                        };
                    } else if (auto item = error_notifications.BeginWriteError(id)) {
                        DEFER { error_notifications.EndWriteError(*item); };
                        item->title = "Failed to send preset folder to trash"_s;
                        item->error_code = outcome.Error();
                    }
                }
            };

            context.confirmation_dialog_state.open = true;
            state.common_state.open = false;
        }
    }
}

void PresetBrowserItems(GuiBoxSystem& box_system, PresetBrowserContext& context, PresetBrowserState& state) {
    auto const root = DoBrowserItemsRoot(box_system);

    auto const first =
        IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    PresetFolderListing const* previous_folder = nullptr;

    Optional<BrowserSection> folder_section;

    auto cursor = *first;
    while (true) {
        auto const& preset_folder = context.presets_snapshot.folders[cursor.folder_index];
        auto const& preset = preset_folder->folder->presets[cursor.preset_index];
        auto const new_folder = preset_folder != previous_folder;

        if (new_folder) {
            previous_folder = preset_folder;
            folder_section = BrowserSection {
                .state = state.common_state,
                .id = preset_folder->node.Hash(),
                .parent = root,
                .folder = &preset_folder->node,
                .skip_root_folder = true,
                .right_click_menu =
                    [&context, &state](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                        PresetFolderRightClickMenu(box_system, context, state, menu_state);
                    },
            };
        }

        if (folder_section->Do(box_system).tag != BrowserSection::State::Collapsed) {
            auto const is_current = ({
                bool c {};
                if (auto const current_path = CurrentPath(context.engine))
                    c = cursor.preset_index == preset_folder->folder->MatchFullPresetPath(*current_path);
                c;
            });

            auto const is_favourite = IsFavourite(context.prefs, FavouriteItemKey(), preset.file_hash);

            auto const item = DoBrowserItem(
                box_system,
                state.common_state,
                {
                    .parent = folder_section->Do(box_system).Get<Box>(),
                    .text = preset.name,
                    .tooltip = FunctionRef<String()>([&preset,
                                                      &scratch = box_system.arena,
                                                      &frame_context = context.frame_context]() -> String {
                        DynamicArray<char> buffer {scratch};

                        fmt::Append(buffer, "{}", preset.name);
                        if (preset.metadata.author.size)
                            fmt::Append(buffer, " by {}.", preset.metadata.author);
                        if (preset.metadata.description.size)
                            fmt::Append(buffer, "\n\n{}", preset.metadata.description);

                        dyn::AppendSpan(buffer, "\n\nTags: ");
                        if (preset.metadata.tags.size) {
                            for (auto const [tag, _] : preset.metadata.tags)
                                fmt::Append(buffer, "{}, ", tag);
                            dyn::Pop(buffer, 2);
                        } else {
                            dyn::AppendSpan(buffer, "none");
                        }

                        if (preset.used_libraries.size) {
                            dyn::AppendSpan(buffer, "\n\nRequires libraries: ");
                            for (auto const [library, _] : preset.used_libraries) {
                                auto const maybe_lib = frame_context.lib_table.Find(library);
                                if (!maybe_lib || *maybe_lib)
                                    fmt::Append(buffer, "{} (not installed)", library);
                                else
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

                        return buffer.ToOwnedSpan();
                    }),
                    .item_id = preset.full_path_hash,
                    .is_current = is_current,
                    .is_favourite = is_favourite,
                    .is_tab_item = new_folder,
                    .icons = ({
                        // The items are normally ordered, but we want special handling for the
                        // Mirage Compatibility library and unknown libraries.

                        decltype(BrowserItemOptions::icons) icons {};
                        usize icons_index = 0;
                        Optional<graphics::ImageID> mirage_compat_icon = k_nullopt;
                        usize num_unknown = 0;
                        for (auto const [lib_id, _] : preset.used_libraries) {
                            auto const imgs = GetLibraryImages(context.library_images,
                                                               box_system.imgui,
                                                               lib_id,
                                                               context.sample_library_server,
                                                               LibraryImagesTypes::All);
                            if (!imgs.icon)
                                ++num_unknown;
                            else if (lib_id == sample_lib::k_mirage_compat_library_id)
                                mirage_compat_icon = imgs.icon;
                            else
                                icons[icons_index++] = imgs.icon;
                        }
                        for (auto const _ : Range(num_unknown))
                            icons[icons_index++] = *context.unknown_library_icon;
                        if (mirage_compat_icon) icons[icons_index++] = *mirage_compat_icon;
                        icons;
                    }),
                    .notifications = context.notifications,
                    .store = context.persistent_store,
                });

            // Right-click menu.
            DoRightClickMenuForBox(box_system,
                                   state.common_state,
                                   item.box,
                                   preset.full_path_hash,
                                   [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                       PresetRightClickMenu(box_system, context, state, menu_state);
                                   });

            if (is_current &&
                box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
                Exchange(state.scroll_to_show_selected, false)) {
                box_system.imgui.ScrollWindowToShowRectangle(
                    layout::GetRect(box_system.layout, item.box.layout_id));
            }

            if (item.fired) {
                if (!is_current)
                    LoadPreset(context, state, cursor, false);
                else
                    SetToDefaultState(context.engine);
            }

            if (item.favourite_toggled)
                dyn::Append(box_system.state->deferred_actions,
                            [&prefs = context.prefs, hash = preset.file_hash, is_favourite]() {
                                ToggleFavourite(prefs, FavouriteItemKey(), hash, is_favourite);
                            });
        }

        if (auto next = IteratePreset(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void PresetBrowserExtraFilters(GuiBoxSystem& box_system,
                               PresetBrowserContext& context,
                               OrderedHashTable<String, FilterItemInfo> const& preset_authors,
                               Array<FilterItemInfo, ToInt(PresetFormat::Count)>& preset_type_filter_info,
                               PresetBrowserState& state,
                               Box const& parent,
                               u8& num_sections) {
    // We only show the preset type filter if we have both types of presets.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1 &&
        !AllOf(preset_type_filter_info, [](FilterItemInfo const& i) { return i.total_available == 0; })) {
        BrowserSection section {
            .state = state.common_state,
            .num_sections_rendered = &num_sections,
            .id = HashComptime("preset-type-section"),
            .parent = parent,
            .heading = "PRESET TYPE",
            .multiline_contents = true,
        };

        for (auto const type_index : Range(ToInt(PresetFormat::Count))) {
            auto const is_selected = state.selected_preset_types.Contains(type_index);
            auto const info = preset_type_filter_info[type_index];
            if (info.total_available == 0) continue;

            if (!MatchesFilterSearch(({
                                         String n {};
                                         switch ((PresetFormat)type_index) {
                                             case PresetFormat::Floe: n = "Floe"; break;
                                             case PresetFormat::Mirage: n = "Mirage"; break;
                                             case PresetFormat::Count: PanicIfReached(); break;
                                         }
                                         n;
                                     }),
                                     state.common_state.filter_search))
                continue;

            if (section.Do(box_system) == BrowserSection::State::Collapsed) break;

            DoFilterButton(box_system,
                           state.common_state,
                           preset_type_filter_info[type_index],
                           {
                               .common =
                                   {
                                       .parent = section.Do(box_system).Get<Box>(),
                                       .is_selected = is_selected,
                                       .text = ({
                                           String s {};
                                           switch ((PresetFormat)type_index) {
                                               case PresetFormat::Floe: s = "Floe"; break;
                                               case PresetFormat::Mirage: s = "Mirage"; break;
                                               default: PanicIfReached();
                                           }
                                           s;
                                       }),
                                       .hashes = state.selected_preset_types,
                                       .clicked_hash = type_index,
                                       .filter_mode = state.common_state.filter_mode,
                                   },
                           });
        }
    }

    if (preset_authors.size) {
        BrowserSection section {
            .state = state.common_state,
            .num_sections_rendered = &num_sections,
            .id = HashComptime("preset-author-section"),
            .parent = parent,
            .heading = "AUTHOR",
            .multiline_contents = true,
        };

        for (auto const [author, author_info, author_hash] : preset_authors) {
            if (!MatchesFilterSearch(author, state.common_state.filter_search)) continue;
            if (section.Do(box_system) == BrowserSection::State::Collapsed) break;

            auto const is_selected = state.selected_author_hashes.Contains(author_hash);

            DoFilterButton(box_system,
                           state.common_state,
                           author_info,
                           {
                               .common =
                                   {
                                       .parent = section.Do(box_system).Get<Box>(),
                                       .is_selected = is_selected,
                                       .text = author,
                                       .hashes = state.selected_author_hashes,
                                       .clicked_hash = author_hash,
                                       .filter_mode = state.common_state.filter_mode,
                                   },
                           });
        }
    }
}

void DoPresetBrowser(GuiBoxSystem& box_system, PresetBrowserContext& context, PresetBrowserState& state) {
    if (!state.common_state.open) return;

    context.Init(box_system.arena);
    DEFER { context.Deinit(); };

    auto tags = HashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                          context.presets_snapshot.used_tags.size + 1);

    auto libraries = OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo>::Create(
        box_system.arena,
        context.presets_snapshot.used_libraries.size);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                         context.presets_snapshot.used_libraries.size);

    auto preset_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                         context.presets_snapshot.authors.size + 1);

    Array<FilterItemInfo, ToInt(PresetFormat::Count)> preset_type_filter_info;

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(box_system.arena, 64);

    FilterItemInfo favourites_info {};

    for (auto const& [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        auto const folder_pack = ContainingPresetBank(&folder->node);
        for (auto const& preset : folder->folder->presets) {
            bool const skip = ShouldSkipPreset(context, state, *folder, preset);

            if (IsFavourite(context.prefs, FavouriteItemKey(), preset.file_hash)) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

            for (auto const [tag, tag_hash] : preset.metadata.tags) {
                auto& i = tags.FindOrInsertWithoutGrowing(tag, {}, tag_hash).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            if (!preset.metadata.tags.size) {
                auto& i = tags.FindOrInsertWithoutGrowing(k_untagged_tag_name, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            DynamicArrayBounded<String, k_num_layers + 1> library_authors_used;

            for (auto const [lib_id, lib_id_hash] : preset.used_libraries) {
                auto& i = libraries.FindOrInsertWithoutGrowing(lib_id, {}, lib_id_hash).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;

                if (auto const lib = context.frame_context.lib_table.Find(lib_id))
                    dyn::AppendIfNotAlreadyThere(library_authors_used, (*lib)->author);
            }

            for (auto const& author : library_authors_used) {
                auto& i = library_authors.FindOrInsertWithoutGrowing(author, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            {
                auto const author = preset.metadata.author.size ? preset.metadata.author : k_no_preset_author;
                auto const hash = Hash(author);
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
                auto& i = folders.FindOrInsertGrowIfNeeded(box_system.arena, f, {}).element.data;
                if (ContainingPresetBank(f) != folder_pack) break;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }
        }
    }

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoBrowserPopup(
        box_system,
        {
            .sample_library_server = context.sample_library_server,
            .preferences = context.prefs,
            .store = context.persistent_store,
            .state = state.common_state,
        },
        BrowserPopupOptions {
            .title = "Presets",
            .height = ({
                auto const window_height = box_system.imgui.frame_input.window_size.height;
                auto const button_bottom = state.common_state.absolute_button_rect.Bottom();
                auto const available_height = window_height - button_bottom - 20;
                box_system.imgui.PixelsToVw(available_height);
            }),
            .rhs_width = 320,
            .filters_col_width = 320,
            .item_type_name = "preset",
            .rhs_do_items = [&](GuiBoxSystem& box_system) { PresetBrowserItems(box_system, context, state); },
            .filter_search_placeholder_text = "Search preset banks/tags",
            .item_search_placeholder_text = "Search presets",
            .on_load_previous = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomPreset(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters =
                LibraryFilters {
                    .libraries_table = context.frame_context.lib_table,
                    .library_images = context.library_images,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .unknown_library_icon = context.unknown_library_icon,
                    .error_notifications = context.engine.error_notifications,
                    .notifications = context.notifications,
                    .confirmation_dialog_state = context.confirmation_dialog_state,
                },
            .tags_filters =
                TagsFilters {
                    .tags = tags,
                },
            .do_extra_filters_top =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    if (num_sections) DoModalDivider(box_system, parent, {.horizontal = true});
                    ++num_sections;

                    auto constexpr k_section_id = HashComptime("preset-folders-section");
                    BrowserSection section {
                        .state = state.common_state,
                        .id = k_section_id,
                        .parent = parent,
                        .heading =
                            ShowPrimaryFilterSectionHeader(state.common_state, context.prefs, k_section_id)
                                ? Optional<String> {"FOLDER"_s}
                                : k_nullopt,
                        .multiline_contents = false,
                        .right_click_menu =
                            [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                PresetFolderRightClickMenu(box_system, context, state, menu_state);
                            },
                    };

                    auto const do_card = [&](FolderNode const* folder, FilterItemInfo const& info) {
                        auto const folder_name =
                            folder->display_name.size ? folder->display_name : folder->name;
                        if (!MatchesFilterSearch(folder_name, state.common_state.filter_search)) return;
                        if (section.Do(box_system).tag == BrowserSection::State::Collapsed) return;

                        DoFilterCard(
                            box_system,
                            state.common_state,
                            info,
                            FilterCardOptions {
                                .common =
                                    {
                                        .parent = section.Do(box_system).Get<Box>(),
                                        .is_selected = state.common_state.selected_folder_hashes.Contains(
                                            folder->Hash()),
                                        .text =
                                            folder->display_name.size ? folder->display_name : folder->name,
                                        .tooltip = folder->display_name.size ? TooltipString {folder->name}
                                                                             : k_nullopt,
                                        .hashes = state.common_state.selected_folder_hashes,
                                        .clicked_hash = folder->Hash(),
                                        .filter_mode = state.common_state.filter_mode,
                                    },
                                .library_id = AllPresetsSingleLibrary(*folder),
                                .library_images = context.library_images,
                                .sample_library_server = context.sample_library_server,
                                .subtext = ({
                                    String s {};
                                    if (auto const m = PresetBankInfoAtNode(*folder))
                                        s = m->subtitle;
                                    else
                                        s = "Preset folder";
                                    s;
                                }),
                                .folder_infos = folders,
                                .folder = folder,
                                .right_click_menu =
                                    [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                        PresetFolderRightClickMenu(box_system, context, state, menu_state);
                                    },
                            });
                    };

                    for (auto const folder : context.presets_snapshot.preset_banks) {
                        auto const info = folders.Find(folder);
                        if (!info) continue;
                        do_card(folder, *info);
                    }
                },
            .do_extra_filters_bottom =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    PresetBrowserExtraFilters(box_system,
                                              context,
                                              preset_authors,
                                              preset_type_filter_info,
                                              state,
                                              parent,
                                              num_sections);
                },
            .has_extra_filters = state.selected_author_hashes.HasSelected() != 0,
            .favourites_filter_info = favourites_info,
        });
}
