// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_preset_picker.hpp"

#include "engine/engine.hpp"
#include "engine/favourite_items.hpp"
#include "gui2_common_picker.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "preset_server/preset_server.hpp"

constexpr String k_no_preset_author = "<no author>"_s;

inline prefs::Key FavouriteKey() { return "favourite-preset"_s; }

struct PresetCursor {
    bool operator==(PresetCursor const& o) const = default;
    usize folder_index;
    usize preset_index;
};

static Optional<PresetCursor> CurrentCursor(PresetPickerContext const& context, Optional<String> path) {
    if (!path) return k_nullopt;

    for (auto const [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        auto const preset_index = folder.folder.MatchFullPresetPath(*path);
        if (preset_index) return PresetCursor {folder_index, *preset_index};
    }

    return k_nullopt;
}

static bool ShouldSkipPreset(PresetPickerContext const& context,
                             PresetPickerState const& state,
                             PresetFolderWithNode const& folder,
                             PresetFolder::Preset const& preset) {
    if (state.common_state.search.size &&
        (!ContainsCaseInsensitiveAscii(preset.name, state.common_state.search) &&
         !ContainsCaseInsensitiveAscii(folder.folder.folder, state.common_state.search)))
        return true;

    bool filtering_on = false;

    if (state.common_state.favourites_only) {
        filtering_on = true;
        if (!IsFavourite(context.prefs, FavouriteKey(), (s64)preset.file_hash)) {
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
            if (!IsInsideFolder(&folder.node, folder_hash.hash)) {
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
        for (auto const& selected_hash : state.common_state.selected_library_author_hashes) {
            if (!preset.used_library_authors.ContainsSkipKeyCheck(selected_hash.hash)) {
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

static Optional<PresetCursor> IteratePreset(PresetPickerContext const& context,
                                            PresetPickerState const& state,
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
                             context.presets_snapshot.folders[cursor.folder_index].folder.presets.size - 1;
                         break;
                 }
             })) {
        auto const& folder = context.presets_snapshot.folders[cursor.folder_index];

        for (; cursor.preset_index < folder.folder.presets.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.preset_index; break;
                         case SearchDirection::Backward: --cursor.preset_index; break;
                     }
                 })) {
            auto const& preset = folder.folder.presets[cursor.preset_index];

            if (ShouldSkipPreset(context, state, folder, preset)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void
LoadPreset(PresetPickerContext const& context, PresetPickerState& state, PresetCursor cursor, bool scroll) {
    auto const& folder = context.presets_snapshot.folders[cursor.folder_index];
    auto const& preset = folder.folder.presets[cursor.preset_index];

    PathArena path_arena {PageAllocator::Instance()};
    LoadPresetFromFile(context.engine, folder.folder.FullPathForPreset(preset, path_arena));

    if (scroll) state.scroll_to_show_selected = true;
}

static Optional<String> CurrentPath(Engine const& engine) {
    if (engine.pending_state_change) return engine.pending_state_change->snapshot.name.Path();
    return engine.last_snapshot.name_or_path.Path();
}

void LoadAdjacentPreset(PresetPickerContext const& context,
                        PresetPickerState& state,
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

void LoadRandomPreset(PresetPickerContext const& context, PresetPickerState& state) {
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
                          PresetPickerContext& context,
                          PresetPickerState&,
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

    auto const find_preset = [&](u64 file_hash) -> Optional<PresetAndFolder> {
        for (auto const& folder : context.presets_snapshot.folders) {
            for (auto const& preset : folder.folder.presets)
                if (preset.file_hash == file_hash) return PresetAndFolder {folder.folder, preset};
        }
        return k_nullopt;
    };

    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Open Containing Folder",
                     .is_selected = false,
                 })) {
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
                 })) {
        if (auto const preset = find_preset(menu_state.item_hash)) {
            auto const outcome =
                TrashFileOrDirectory(preset->folder.FullPathForPreset(preset->preset, box_system.arena),
                                     box_system.arena);
            auto const error_id = ({
                auto id = HashInit();
                HashUpdate(id, "preset-trash"_s);
                HashUpdate(id, preset->preset.file_hash);
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
                                PresetPickerContext& context,
                                PresetPickerState&,
                                RightClickMenuState const& menu_state) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (MenuItem(box_system,
                 root,
                 {
                     .text = fmt::Format(box_system.arena, "Open Folder in {}", GetFileBrowserAppName()),
                     .is_selected = false,
                 })) {
        auto const find_folder = [&](u64 folder_hash) -> PresetFolder const* {
            for (auto const& folder : context.presets_snapshot.folders)
                if (folder.node.Hash() == folder_hash) return &folder.folder;
            return nullptr;
        };

        if (auto const folder = find_folder(menu_state.item_hash)) {
            OpenFolderInFileBrowser(
                path::Join(box_system.arena, Array {folder->scan_folder, folder->folder}));
        }
    }
}

void PresetPickerItems(GuiBoxSystem& box_system, PresetPickerContext& context, PresetPickerState& state) {
    auto const root = DoPickerItemsRoot(box_system);

    auto const first =
        IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    PresetFolderWithNode const* previous_folder = nullptr;

    Optional<Box> folder_box;

    auto cursor = *first;
    while (true) {
        auto const& preset_folder = context.presets_snapshot.folders[cursor.folder_index];
        auto const& preset = preset_folder.folder.presets[cursor.preset_index];

        if (&preset_folder != previous_folder) {
            previous_folder = &preset_folder;
            folder_box = DoPickerSectionContainer(
                box_system,
                preset_folder.node.Hash(),
                state.common_state,
                {
                    .parent = root,
                    .folder = &preset_folder.node,
                    .skip_root_folder = true,
                    .right_click_menu =
                        [&context, &state](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                            PresetFolderRightClickMenu(box_system, context, state, menu_state);
                        },
                });
        }

        if (folder_box) {
            auto const is_current = ({
                bool c {};
                if (auto const current_path = CurrentPath(context.engine))
                    c = cursor.preset_index == preset_folder.folder.MatchFullPresetPath(*current_path);
                c;
            });

            auto const is_favourite = IsFavourite(context.prefs, FavouriteKey(), (s64)preset.file_hash);

            auto const item = DoPickerItem(
                box_system,
                state.common_state,
                {
                    .parent = *folder_box,
                    .text = preset.name,
                    .tooltip = FunctionRef<String()>([&]() -> String {
                        DynamicArray<char> buffer {box_system.arena};

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

                        return buffer.ToOwnedSpan();
                    }),
                    .is_current = is_current,
                    .is_favourite = is_favourite,
                    .icons = ({
                        decltype(PickerItemOptions::icons) icons {};
                        usize icons_index = 0;
                        for (auto const [lib_id, _] : preset.used_libraries) {
                            if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                             box_system.imgui,
                                                                             lib_id,
                                                                             context.sample_library_server,
                                                                             box_system.arena,
                                                                             true);
                                imgs && imgs->icon) {
                                icons[icons_index++] = imgs->icon;
                            } else if (context.unknown_library_icon) {
                                icons[icons_index++] = *context.unknown_library_icon;
                            }
                        }
                        icons;
                    }),
                    .notifications = context.notifications,
                    .store = context.persistent_store,
                });

            // Right-click menu.
            DoRightClickForBox(box_system,
                               state.common_state,
                               item.box,
                               preset.file_hash,
                               [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                   PresetRightClickMenu(box_system, context, state, menu_state);
                               });

            if (is_current &&
                box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
                Exchange(state.scroll_to_show_selected, false)) {
                box_system.imgui.ScrollWindowToShowRectangle(
                    layout::GetRect(box_system.layout, item.box.layout_id));
            }

            if (item.box.button_fired) LoadPreset(context, state, cursor, false);

            if (item.favourite_toggled)
                dyn::Append(box_system.state->deferred_actions,
                            [&prefs = context.prefs, hash = (s64)preset.file_hash, is_favourite]() {
                                ToggleFavourite(prefs, FavouriteKey(), hash, is_favourite);
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

void PresetPickerExtraFilters(GuiBoxSystem& box_system,
                              PresetPickerContext& context,
                              OrderedHashTable<String, FilterItemInfo> const& preset_authors,
                              Array<FilterItemInfo, ToInt(PresetFormat::Count)>& preset_type_filter_info,
                              PresetPickerState& state,
                              Box const& parent,
                              u8& num_sections) {
    // We only show the preset type filter if we have both types of presets.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1) {
        if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++num_sections;

        auto const section = DoPickerSectionContainer(box_system,
                                                      53847912837, // never change
                                                      state.common_state,
                                                      {
                                                          .parent = parent,
                                                          .heading = "PRESET TYPE",
                                                          .multiline_contents = true,
                                                      });

        if (section) {
            for (auto const type_index : Range(ToInt(PresetFormat::Count))) {
                auto const is_selected = state.selected_preset_types.Contains(type_index);

                DoFilterButton(box_system,
                               state.common_state,
                               preset_type_filter_info[type_index],
                               {
                                   .parent = *section,
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
                               });
            }
        }
    }

    if (preset_authors.size) {
        if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++num_sections;

        auto const section = DoPickerSectionContainer(box_system,
                                                      125342985712309, // never change
                                                      state.common_state,
                                                      {
                                                          .parent = parent,
                                                          .heading = "AUTHOR",
                                                          .multiline_contents = true,
                                                      });

        if (section) {
            for (auto const [author, author_info, author_hash] : preset_authors) {
                auto const is_selected = state.selected_author_hashes.Contains(author_hash);

                DoFilterButton(box_system,
                               state.common_state,
                               author_info,
                               {
                                   .parent = *section,
                                   .is_selected = is_selected,
                                   .text = author,
                                   .hashes = state.selected_author_hashes,
                                   .clicked_hash = author_hash,
                                   .filter_mode = state.common_state.filter_mode,
                               });
            }
        }
    }
}

void DoPresetPicker(GuiBoxSystem& box_system, PresetPickerContext& context, PresetPickerState& state) {
    if (!state.common_state.open) return;

    context.Init(box_system.arena);
    DEFER { context.Deinit(); };

    auto tags =
        HashTable<String, FilterItemInfo>::Create(box_system.arena, context.presets_snapshot.used_tags.size);
    for (auto const [tag, tag_hash] : context.presets_snapshot.used_tags)
        tags.InsertWithoutGrowing(tag, {.num_used_in_items_lists = 0}, tag_hash);

    auto libraries = OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo>::Create(
        box_system.arena,
        context.presets_snapshot.used_libraries.size);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                         context.presets_snapshot.used_libraries.size);
    for (auto const [lib, lib_hash] : context.presets_snapshot.used_libraries) {
        libraries.InsertWithoutGrowing(lib, {.num_used_in_items_lists = 0}, lib_hash);
        library_authors.InsertWithoutGrowing(lib.author, {.num_used_in_items_lists = 0});
    }

    auto preset_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                         context.presets_snapshot.authors.size);
    for (auto const& [author, author_hash] : context.presets_snapshot.authors)
        preset_authors.InsertWithoutGrowing(author, {.num_used_in_items_lists = 0}, author_hash);

    Array<FilterItemInfo, ToInt(PresetFormat::Count)> preset_type_filter_info;

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(box_system.arena, 64);
    auto root_folder = FolderRootSet::Create(box_system.arena, 8);

    FilterItemInfo favourites_info {};

    for (auto const& [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        for (auto const& preset : folder.folder.presets) {
            bool const skip = ShouldSkipPreset(context, state, folder, preset);

            if (IsFavourite(context.prefs, FavouriteKey(), (s64)preset.file_hash)) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

            for (auto const [tag, tag_hash] : preset.metadata.tags) {
                auto i = tags.Find(tag, tag_hash);
                if (!skip) ++i->num_used_in_items_lists;
                ++i->total_available;
            }

            if (!preset.metadata.tags.size) {
                auto& i =
                    tags.FindOrInsertGrowIfNeeded(box_system.arena, k_untagged_tag_name, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            for (auto const [lib_id, lib_id_hash] : preset.used_libraries) {
                auto i = libraries.Find(lib_id, lib_id_hash);
                if (!skip) ++i->num_used_in_items_lists;
                ++i->total_available;
            }

            for (auto const [author, author_hash] : preset.used_library_authors) {
                auto i = library_authors.Find(author, author_hash);
                if (!skip) ++i->num_used_in_items_lists;
                ++i->total_available;
            }

            if (preset.metadata.author.size) {
                auto i = preset_authors.Find(preset.metadata.author);
                if (!skip) ++i->num_used_in_items_lists;
                ++i->total_available;
            } else {
                auto& i = preset_authors.FindOrInsertGrowIfNeeded(box_system.arena, k_no_preset_author, {})
                              .element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            {
                auto& i = preset_type_filter_info[ToInt(preset.file_format)];
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            for (auto f = &folder.node; f; f = f->parent) {
                auto& i = folders.FindOrInsertGrowIfNeeded(box_system.arena, f, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
                if (!f->parent) root_folder.InsertGrowIfNeeded(box_system.arena, f);
            }
        }
    }

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoPickerPopup(
        box_system,
        {
            .sample_library_server = context.sample_library_server,
            .state = state.common_state,
        },
        PickerPopupOptions {
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
            .items_section_heading = "Presets",
            .rhs_do_items = [&](GuiBoxSystem& box_system) { PresetPickerItems(box_system, context, state); },
            .on_load_previous = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomPreset(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters =
                LibraryFilters {
                    .library_images = context.library_images,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .unknown_library_icon = context.unknown_library_icon,
                },
            .tags_filters =
                TagsFilters {
                    .tags = tags,
                },
            .do_extra_filters_top =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
                    ++num_sections;

                    auto const section = DoPickerSectionContainer(
                        box_system,
                        SourceLocationHash(),
                        state.common_state,
                        {
                            .parent = parent,
                            .heading = "FOLDER",
                            .multiline_contents = false,
                            .right_click_menu =
                                [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                    PresetFolderRightClickMenu(box_system, context, state, menu_state);
                                },
                        });

                    if (section) {
                        auto const do_card = [&](FolderNode const* folder, FilterItemInfo const& info) {
                            Optional<graphics::ImageID> icon = {};
                            Optional<graphics::ImageID> background_image1 = {};
                            Optional<graphics::ImageID> background_image2 = {};
                            if (auto const single_library = AllPresetsSingleLibrary(*folder)) {
                                if (auto imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                           box_system.imgui,
                                                                           *single_library,
                                                                           context.sample_library_server,
                                                                           box_system.arena,
                                                                           false)) {
                                    if (!imgs->icon_missing) icon = imgs->icon;
                                    if (!imgs->background_missing) {
                                        background_image1 = imgs->blurred_background;
                                        background_image2 = imgs->background;
                                    }
                                }
                            }

                            DoFilterCard(
                                box_system,
                                state.common_state,
                                info,
                                FilterCardOptions {
                                    .parent = *section,
                                    .is_selected =
                                        state.common_state.selected_folder_hashes.Contains(folder->Hash()),
                                    .icon = icon.NullableValue(),
                                    .background_image1 = background_image1.NullableValue(),
                                    .background_image2 = background_image2.NullableValue(),
                                    .text = folder->display_name.size ? folder->display_name : folder->name,
                                    .subtext = ({
                                        String s {};
                                        if (auto const m = MetadataForFolderNode(*folder)) s = m->subtitle;
                                        s;
                                    }),
                                    .tooltip =
                                        folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
                                    .hashes = state.common_state.selected_folder_hashes,
                                    .clicked_hash = folder->Hash(),
                                    .filter_mode = state.common_state.filter_mode,
                                    .folder_infos = folders,
                                    .folder = folder,
                                    .right_click_menu =
                                        [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                                            PresetFolderRightClickMenu(box_system,
                                                                       context,
                                                                       state,
                                                                       menu_state);
                                        },
                                });

                            [[clang::no_destroy]] static DynamicSet<String> printed_names {
                                Malloc::Instance()};

                            if (printed_names.FindOrInsert(folder->name, {}).inserted)
                                LogInfo(ModuleName::Gui,
                                        "Folder {}: {}",
                                        folder->name,
                                        FolderContentsHash(folder));
                        };

                        for (auto const [root, _] : root_folder) {
                            if (!root->first_child) {
                                do_card(root, *folders.Find(root));
                            } else {
                                if (auto const preset_folder = root->user_data.As<PresetFolder const>();
                                    preset_folder && preset_folder->presets.size) {
                                    auto folder = *root;
                                    folder.first_child = nullptr;
                                    do_card(&folder, *folders.Find(root));
                                }

                                for (auto* child = root->first_child; child; child = child->next)
                                    do_card(child, *folders.Find(child));
                            }
                        }
                    }
                },
            .do_extra_filters_bottom =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    PresetPickerExtraFilters(box_system,
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
