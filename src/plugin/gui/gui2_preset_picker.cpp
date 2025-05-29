// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_preset_picker.hpp"

#include "engine/engine.hpp"
#include "gui2_common_picker.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "preset_server/preset_server.hpp"

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

            if (state.search.size && (!ContainsCaseInsensitiveAscii(preset.name, state.search) &&
                                      !ContainsCaseInsensitiveAscii(folder.folder, state.search)))
                continue;

            // If multiple preset types exist, we offer a way to filter by them.
            if (context.presets_snapshot.has_preset_type.NumSet() > 1 &&
                Contains(state.selected_preset_types, true) &&
                !state.selected_preset_types[ToInt(preset.file_format)])
                continue;

            if (state.selected_library_hashes.size) {
                bool found = false;
                for (auto const lib_id : preset.used_libraries) {
                    if (Contains(state.selected_library_hashes, lib_id.Hash())) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            if (state.selected_library_author_hashes.size) {
                bool found = false;
                for (auto const lib_id : preset.used_libraries) {
                    auto const author_hash = Hash(lib_id.author);
                    if (Contains(state.selected_library_author_hashes, author_hash)) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

            if (state.selected_author_hashes.size) {
                auto const author_hash = Hash(preset.metadata.author);
                if (!Contains(state.selected_author_hashes, author_hash)) continue;
            }

            if (state.selected_tags_hashes.size) {
                bool found = false;
                for (auto const tag : preset.metadata.tags) {
                    auto const tag_hash = Hash(tag);
                    if (Contains(state.selected_tags_hashes, tag_hash)) {
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }

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

        auto const item = DoPickerItem(box_system,
                                       {
                                           .parent = folder_box,
                                           .text = preset.name,
                                           .is_current = is_current,
                                           .icon = k_nullopt,
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
            auto const is_selected = state.selected_preset_types[type_index];

            if (DoFilterButton(box_system, section, is_selected, k_nullopt, ({
                                   String s {};
                                   switch ((PresetFormat)type_index) {
                                       case PresetFormat::Floe: s = "Floe"; break;
                                       case PresetFormat::Mirage: s = "Mirage"; break;
                                       default: PanicIfReached();
                                   }
                                   s;
                               }))
                    .button_fired) {
                dyn::Append(box_system.state->deferred_actions,
                            [&selected_preset_types = state.selected_preset_types,
                             type_index,
                             is_selected,
                             select_mode = state.filter_select_mode]() {
                                switch (select_mode) {
                                    case FilterSelectMode::Single:
                                        selected_preset_types = {};
                                        selected_preset_types[type_index] = !is_selected;
                                        break;
                                    case FilterSelectMode::Multi:
                                        selected_preset_types[type_index] = !is_selected;
                                        break;
                                }
                            });
            }
        }
    }

    if (context.presets_snapshot.authors.size) {
        if (num_sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++num_sections;

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "AUTHOR",
                                                               .multiline_contents = true,
                                                           });

        for (auto const& element : context.presets_snapshot.authors.Elements()) {
            if (!element.active) continue;
            auto const author_hash = element.hash;
            auto const author = element.key;

            auto const is_selected = Contains(state.selected_author_hashes, author_hash);

            if (DoFilterButton(box_system, section, is_selected, k_nullopt, author).button_fired) {
                dyn::Append(box_system.state->deferred_actions,
                            [&selected_authors = state.selected_author_hashes,
                             author_hash,
                             is_selected,
                             select_mode = state.filter_select_mode]() {
                                FilterClicked(selected_authors, author_hash, is_selected, select_mode);
                            });
            }
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

    Set<String> used_tags {};
    used_tags.Assign(context.presets_snapshot.used_tags, box_system.arena);
    for (auto const hash : state.selected_library_author_hashes)
        if (auto tags = context.presets_snapshot.used_tags_by_library_author_hash.Find(hash))
            used_tags.IntersectWith(*tags);
    for (auto const hash : state.selected_author_hashes)
        if (auto tags = context.presets_snapshot.used_tags_by_preset_author_hash.Find(hash))
            used_tags.IntersectWith(*tags);
    for (auto const hash : state.selected_library_hashes)
        if (auto tags = context.presets_snapshot.used_tags_by_library_hash.Find(hash))
            used_tags.IntersectWith(*tags);

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoPickerPopup(
        box_system,
        popup_id,
        absolute_button_rect,
        PickerPopupOptions {
            .sample_library_server = context.sample_library_server,
            .title = "Presets",
            .height = box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.75f),
            .rhs_width = 320,
            .filters_col_width = 320,
            .item_type_name = "preset",
            .items_section_heading = "Presets",
            .filter_select_mode = state.filter_select_mode,
            .rhs_do_items = [&](GuiBoxSystem& box_system) { PresetPickerItems(box_system, context, state); },
            .search = &state.search,
            .on_load_previous = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentPreset(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomPreset(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .libraries = ({
                DynamicArray<sample_lib::LibraryIdRef> libraries {box_system.arena};
                for (auto const [lib, _] : context.presets_snapshot.used_libraries) {
                    if (state.selected_tags_hashes.size) {
                        if (auto lib_tags =
                                context.presets_snapshot.used_tags_by_library_hash.Find(lib.Hash())) {
                            bool found = false;
                            for (auto const& tag_element : lib_tags->Elements()) {
                                if (!tag_element.active) continue;
                                if (Contains(state.selected_tags_hashes, tag_element.hash)) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) continue;
                        }
                    }
                    dyn::Append(libraries, lib);
                }
                libraries.ToOwnedSpan();
            }),
            .library_filters =
                LibraryFilters {
                    .selected_library_hashes = state.selected_library_hashes,
                    .selected_library_author_hashes = state.selected_library_author_hashes,
                    .library_images = context.library_images,
                    .sample_library_server = context.sample_library_server,
                },
            .tags_filters =
                TagsFilters {
                    .selected_tags_hashes = state.selected_tags_hashes,
                    .tags = used_tags,
                },
            .do_extra_filters =
                [&](GuiBoxSystem& box_system, Box const& parent, u8& num_sections) {
                    PresetPickerExtraFilters(box_system, context, state, parent, num_sections);
                },
            .has_extra_filters = state.selected_author_hashes.size != 0,
            .on_clear_all_filters =
                [&]() {
                    dyn::Clear(state.selected_author_hashes);
                    state.selected_preset_types = {};
                },
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
                        for (auto const& tag : context.hovering_preset->metadata.tags)
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
