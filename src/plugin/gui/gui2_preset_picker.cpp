// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_preset_picker.hpp"

#include "engine/engine.hpp"
#include "gui2_common_picker.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "preset_server/preset_server.hpp"

constexpr String k_no_preset_author = "<no author>"_s;

struct PresetCursor {
    bool operator==(PresetCursor const& o) const = default;
    usize folder_index;
    usize preset_index;
};

static Optional<PresetCursor> CurrentCursor(PresetPickerContext const& context, Optional<String> path) {
    if (!path) return k_nullopt;

    for (auto const [folder_index, folder] : Enumerate(context.presets_snapshot.folders)) {
        auto const preset_index = folder->MatchFullPresetPath(*path);
        if (preset_index) return PresetCursor {folder_index, *preset_index};
    }

    return k_nullopt;
}

static bool ShouldSkipPreset(PresetPickerContext const& context,
                             PresetPickerState const& state,
                             PresetFolder const& folder,
                             PresetFolder::Preset const& preset) {
    if (state.common_state.search.size &&
        (!ContainsCaseInsensitiveAscii(preset.name, state.common_state.search) &&
         !ContainsCaseInsensitiveAscii(folder.folder, state.common_state.search)))
        return true;

    bool filtering_on = false;

    // If multiple preset types exist, we offer a way to filter by them.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1) {
        if (state.selected_preset_types.size) {
            filtering_on = true;
            if (Contains(state.selected_preset_types, ToInt(preset.file_format))) {
                if (state.common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            } else {
                if (state.common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            }
        }
    }

    if (state.common_state.selected_library_hashes.size) {
        filtering_on = true;
        for (auto const selected_hash : state.common_state.selected_library_hashes) {
            if (!preset.used_libraries.ContainsSkipKeyCheck(selected_hash)) {
                if (state.common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            }
        }
    }

    if (state.common_state.selected_library_author_hashes.size) {
        filtering_on = true;
        for (auto const selected_hash : state.common_state.selected_library_author_hashes) {
            if (!preset.used_library_authors.ContainsSkipKeyCheck(selected_hash)) {
                if (state.common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            }
        }
    }

    if (state.selected_author_hashes.size) {
        filtering_on = true;
        auto const author_hash = Hash(preset.metadata.author);
        if (!(Contains(state.selected_author_hashes, author_hash) ||
              (preset.metadata.author.size == 0 &&
               Contains(state.selected_author_hashes, Hash(k_no_preset_author))))) {
            if (state.common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
        } else {
            if (state.common_state.filter_mode == FilterMode::AdditiveSelection) return false;
        }
    }

    if (state.common_state.selected_tags_hashes.size) {
        filtering_on = true;
        for (auto const selected_hash : state.common_state.selected_tags_hashes) {
            if (!(preset.metadata.tags.ContainsSkipKeyCheck(selected_hash) ||
                  (selected_hash == Hash(k_untagged_tag_name) && preset.metadata.tags.size == 0))) {
                if (state.common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            } else {
                if (state.common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            }
        }
    }

    if (filtering_on && state.common_state.filter_mode == FilterMode::AdditiveSelection) {
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
                             context.presets_snapshot.folders[cursor.folder_index]->presets.size - 1;
                         break;
                 }
             })) {
        auto const& folder = *context.presets_snapshot.folders[cursor.folder_index];

        for (; cursor.preset_index < folder.presets.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.preset_index; break;
                         case SearchDirection::Backward: --cursor.preset_index; break;
                     }
                 })) {
            auto const& preset = folder.presets[cursor.preset_index];

            if (ShouldSkipPreset(context, state, folder, preset)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void
LoadPreset(PresetPickerContext const& context, PresetPickerState& state, PresetCursor cursor, bool scroll) {
    auto const& folder = *context.presets_snapshot.folders[cursor.folder_index];
    auto const& preset = folder.presets[cursor.preset_index];

    PathArena path_arena {PageAllocator::Instance()};
    LoadPresetFromFile(context.engine, folder.FullPathForPreset(preset, path_arena));

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

void PresetPickerItems(GuiBoxSystem& box_system, PresetPickerContext& context, PresetPickerState& state) {
    auto const root = DoPickerItemsRoot(box_system);

    auto const first =
        IteratePreset(context, state, {.folder_index = 0, .preset_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    PresetFolder const* previous_folder = nullptr;

    Box folder_box;

    auto cursor = *first;
    while (true) {
        auto const& preset_folder = *context.presets_snapshot.folders[cursor.folder_index];
        auto const& preset = preset_folder.presets[cursor.preset_index];

        if (&preset_folder != previous_folder) {
            previous_folder = &preset_folder;
            folder_box = DoPickerItemsSectionContainer(box_system,
                                                       {
                                                           .parent = root,
                                                           .heading = preset_folder.folder,
                                                           .heading_is_folder = true,
                                                       });
        }

        auto const is_current = ({
            bool c {};
            if (auto const current_path = CurrentPath(context.engine))
                c = cursor.preset_index == preset_folder.MatchFullPresetPath(*current_path);
            c;
        });

        auto const item = DoPickerItem(
            box_system,
            {
                .parent = folder_box,
                .text = preset.name,
                .is_current = is_current,
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
                            auto tex =
                                box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);
                            if (tex) icons[icons_index++] = *tex;
                        } else if (context.unknown_library_icon) {
                            auto const tex = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(
                                *context.unknown_library_icon);
                            if (tex) icons[icons_index++] = *tex;
                        }
                    }
                    icons;
                }),
            });

        if (is_current && box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
            Exchange(state.scroll_to_show_selected, false)) {
            box_system.imgui.ScrollWindowToShowRectangle(layout::GetRect(box_system.layout, item.layout_id));
        }

        if (item.is_hot) context.hovering_preset = &preset;
        if (item.button_fired) LoadPreset(context, state, cursor, false);

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
                              OrderedHashTable<String, FilterItemInfo>& preset_authors,
                              Array<FilterItemInfo, ToInt(PresetFormat::Count)>& preset_type_filter_info,
                              PresetPickerState& state,
                              Box const& parent,
                              u8& num_sections) {
    // We only show the preset type filter if we have both types of presets.
    if (context.presets_snapshot.has_preset_type.NumSet() > 1) {
        if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++num_sections;

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "PRESET TYPE",
                                                               .multiline_contents = true,
                                                           });

        for (auto const type_index : Range(ToInt(PresetFormat::Count))) {
            auto const is_selected = Contains(state.selected_preset_types, type_index);

            DoFilterButton(box_system,
                           state.common_state,
                           preset_type_filter_info[type_index],
                           {
                               .parent = section,
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

    if (preset_authors.size) {
        if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++num_sections;

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "AUTHOR",
                                                               .multiline_contents = true,
                                                           });

        for (auto const [author, author_info, author_hash] : preset_authors) {
            auto const is_selected = Contains(state.selected_author_hashes, author_hash);

            DoFilterButton(box_system,
                           state.common_state,
                           author_info,
                           {
                               .parent = section,
                               .is_selected = is_selected,
                               .text = author,
                               .hashes = state.selected_author_hashes,
                               .clicked_hash = author_hash,
                               .filter_mode = state.common_state.filter_mode,
                           });
        }
    }
}

void DoPresetPicker(GuiBoxSystem& box_system,
                    imgui::Id popup_id,
                    Rect absolute_button_rect,
                    PresetPickerContext& context,
                    PresetPickerState& state) {
    if (!box_system.imgui.IsPopupOpen(popup_id)) return;

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
                                                         context.presets_snapshot.authors.size);
    for (auto const [lib, lib_hash] : context.presets_snapshot.used_libraries) {
        libraries.InsertWithoutGrowing(lib, {.num_used_in_items_lists = 0}, lib_hash);
        library_authors.InsertWithoutGrowing(lib.author, {.num_used_in_items_lists = 0}, Hash(lib.author));
    }

    auto preset_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena,
                                                         context.presets_snapshot.authors.size);
    for (auto const& [author, author_hash] : context.presets_snapshot.authors)
        preset_authors.InsertWithoutGrowing(author, {.num_used_in_items_lists = 0}, author_hash);

    Array<FilterItemInfo, ToInt(PresetFormat::Count)> preset_type_filter_info;

    for (auto const& folder : context.presets_snapshot.folders) {
        for (auto const& preset : folder->presets) {
            bool const skip = ShouldSkipPreset(context, state, *folder, preset);
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
        popup_id,
        absolute_button_rect,
        PickerPopupOptions {
            .title = "Presets",
            .height = box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.75f),
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
                },
            .tags_filters =
                TagsFilters {
                    .tags = tags,
                },
            .do_extra_filters =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    PresetPickerExtraFilters(box_system,
                                             context,
                                             preset_authors,
                                             preset_type_filter_info,
                                             state,
                                             parent,
                                             num_sections);
                },
            .has_extra_filters = state.selected_author_hashes.size != 0,
            .status_bar_height = 58,
            .status = [&]() -> Optional<String> {
                Optional<String> status {};

                if (context.hovering_preset) {
                    DynamicArray<char> buffer {box_system.arena};

                    fmt::Append(buffer, "{}", context.hovering_preset->name);
                    if (context.hovering_preset->metadata.author.size)
                        fmt::Append(buffer, " by {}.", context.hovering_preset->metadata.author);
                    if (context.hovering_preset->metadata.description.size)
                        fmt::Append(buffer, " {}", context.hovering_preset->metadata.description);

                    dyn::AppendSpan(buffer, "\nTags: ");
                    if (context.hovering_preset->metadata.tags.size) {
                        for (auto const [tag, _] : context.hovering_preset->metadata.tags)
                            fmt::Append(buffer, "{}, ", tag);
                        dyn::Pop(buffer, 2);
                    } else {
                        dyn::AppendSpan(buffer, "none");
                    }

                    status = buffer.ToOwnedSpan();
                }

                return status;
            },
        });
}
