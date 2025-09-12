// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_ir_picker.hpp"

#include "engine/favourite_items.hpp"
#include "gui2_common_picker.hpp"

inline prefs::Key FavouriteIr() { return "favourite-ir"_s; }

static Optional<IrCursor> CurrentCursor(IrPickerContext const& context, sample_lib::IrId const& ir_id) {
    for (auto const [lib_index, l] : Enumerate(context.libraries)) {
        if (l->Id() != ir_id.library) continue;
        for (auto const [ir_index, i] : Enumerate(l->sorted_irs))
            if (i->name == ir_id.ir_name) return IrCursor {lib_index, ir_index};
    }

    return k_nullopt;
}

static bool IrMatchesSearch(sample_lib::ImpulseResponse const& ir, String search) {
    if (ContainsCaseInsensitiveAscii(ir.name, search)) return true;
    return false;
}

static bool ShouldSkipIr(IrPickerContext const& context,
                         IrPickerState const& state,
                         sample_lib::ImpulseResponse const& ir) {
    if (state.common_state.search.size && !IrMatchesSearch(ir, state.common_state.search)) return true;

    bool filtering_on = false;

    if (state.common_state.favourites_only) {
        filtering_on = true;
        if (!IsFavourite(context.prefs, FavouriteIr(), (s64)sample_lib::IrHash(ir))) {
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
            if (!IsInsideFolder(ir.folder, folder_hash.hash)) {
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
        if (!state.common_state.selected_library_hashes.Contains(ir.library.Id().Hash())) {
            if (state.common_state.filter_mode == FilterMode::MultipleAnd)
                return true;
            else if (state.common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (state.common_state.filter_mode == FilterMode::MultipleOr) return false;
        }
    }

    if (state.common_state.selected_library_author_hashes.HasSelected()) {
        filtering_on = true;
        if (!state.common_state.selected_library_author_hashes.Contains(Hash(ir.library.author))) {
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
            if (!(ir.tags.ContainsSkipKeyCheck(selected_hash.hash) ||
                  (selected_hash.hash == Hash(k_untagged_tag_name) && ir.tags.size == 0))) {
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

static Optional<IrCursor> IterateIr(IrPickerContext const& context,
                                    IrPickerState const& state,
                                    IrCursor cursor,
                                    SearchDirection direction,
                                    bool first) {
    if (context.libraries.size == 0) return k_nullopt;

    if (cursor.lib_index >= context.libraries.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.ir_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.ir_index)>);
                --cursor.ir_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < context.libraries.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % context.libraries.size;
                         cursor.ir_index = 0;
                         break;
                     case SearchDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.lib_index)>);
                         --cursor.lib_index;
                         if (cursor.lib_index >= context.libraries.size) // check wraparound
                             cursor.lib_index = context.libraries.size - 1;
                         cursor.ir_index = context.libraries[cursor.lib_index]->irs_by_name.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *context.libraries[cursor.lib_index];

        if (lib.irs_by_name.size == 0) continue;

        // PERF: we could skip early here based on the library and filters, but only for some filter modes.

        for (; cursor.ir_index < lib.sorted_irs.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.ir_index; break;
                         case SearchDirection::Backward: --cursor.ir_index; break;
                     }
                 })) {
            auto const& ir = *lib.sorted_irs[cursor.ir_index];

            if (ShouldSkipIr(context, state, ir)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void LoadIr(IrPickerContext const& context, IrPickerState& state, IrCursor const& cursor) {
    auto const& lib = *context.libraries[cursor.lib_index];
    auto const& ir = *lib.sorted_irs[cursor.ir_index];
    LoadConvolutionIr(context.engine, sample_lib::IrId {lib.Id(), ir.name});
    state.scroll_to_show_selected = true;
}

void LoadAdjacentIr(IrPickerContext const& context, IrPickerState& state, SearchDirection direction) {
    auto const ir_id = context.engine.processor.convo.ir_id;

    if (ir_id) {
        if (auto const cursor = CurrentCursor(context, *ir_id)) {
            if (auto const next = IterateIr(context, state, *cursor, direction, false))
                LoadIr(context, state, *next);
        }
    } else if (auto const first = IterateIr(context, state, {0, 0}, direction, true)) {
        LoadIr(context, state, *first);
    }
}

void LoadRandomIr(IrPickerContext const& context, IrPickerState& state) {
    auto const first =
        IterateIr(context, state, {.lib_index = 0, .ir_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    auto cursor = *first;

    usize num_irs = 1;
    while (true) {
        if (auto const next = IterateIr(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
            ++num_irs;
        } else {
            break;
        }
    }

    auto const random_pos = RandomIntInRange<usize>(context.engine.random_seed, 0, num_irs - 1);

    cursor = *first;
    for (usize i = 0; i < random_pos; ++i)
        cursor = *IterateIr(context, state, cursor, SearchDirection::Forward, false);

    LoadIr(context, state, cursor);
}

void IrPickerItems(GuiBoxSystem& box_system, IrPickerContext& context, IrPickerState& state) {
    auto const root = DoPickerItemsRoot(box_system);

    Optional<FolderNode*> previous_folder {};
    Optional<Box> folder_box {};

    auto const first =
        IterateIr(context, state, {.lib_index = 0, .ir_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    Optional<graphics::ImageID> lib_icon {};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.libraries[cursor.lib_index];
        auto const& ir = *lib.sorted_irs[cursor.ir_index];
        auto const& folder = ir.folder;

        if (folder != previous_folder) {
            previous_folder = folder;
            folder_box = DoPickerSectionContainer(box_system,
                                                  folder->Hash(),
                                                  state.common_state,
                                                  {
                                                      .parent = root,
                                                      .folder = folder,
                                                  });
        }

        auto const ir_id = sample_lib::IrId {lib.Id(), ir.name};
        auto const is_current = context.engine.processor.convo.ir_id == ir_id;
        auto const is_favourite = IsFavourite(context.prefs, FavouriteIr(), (s64)sample_lib::IrHash(ir));

        if (folder_box) {
            auto const item = DoPickerItem(
                box_system,
                state.common_state,
                {
                    .parent = *folder_box,
                    .text = ir.name,
                    .tooltip = FunctionRef<String()>([&]() -> String {
                        DynamicArray<char> buffer {box_system.arena};

                        fmt::Append(buffer, "{}. Tags: ", ir.name);
                        if (ir.tags.size) {
                            for (auto const& [tag, _] : ir.tags)
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
                        if (&lib != previous_library) {
                            lib_icon = k_nullopt;
                            previous_library = &lib;
                            if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                             box_system.imgui,
                                                                             lib.Id(),
                                                                             context.sample_library_server,
                                                                             box_system.arena,
                                                                             true)) {
                                lib_icon =
                                    (imgs && !imgs->icon_missing) ? imgs->icon : context.unknown_library_icon;
                            }
                        }
                        decltype(PickerItemOptions::icons) {lib_icon};
                    }),
                    .notifications = context.notifications,
                    .store = context.persistent_store,
                });

            if (is_current &&
                box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
                Exchange(state.scroll_to_show_selected, false)) {
                box_system.imgui.ScrollWindowToShowRectangle(
                    layout::GetRect(box_system.layout, item.box.layout_id));
            }

            if (item.box.button_fired) {
                if (is_current) {
                    LoadConvolutionIr(context.engine, k_nullopt);
                } else {
                    LoadConvolutionIr(context.engine,
                                      sample_lib::IrId {
                                          .library = lib.Id(),
                                          .ir_name = ir.name,
                                      });
                }
            }

            if (item.favourite_toggled) {
                dyn::Append(box_system.state->deferred_actions,
                            [&prefs = context.prefs, hash = (s64)sample_lib::IrHash(ir), is_favourite]() {
                                ToggleFavourite(prefs, FavouriteIr(), hash, is_favourite);
                            });
            }
        }

        if (auto next = IterateIr(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoIrPickerPopup(GuiBoxSystem& box_system, IrPickerContext& context, IrPickerState& state) {
    if (!state.common_state.open) return;

    auto& ir_id = context.engine.processor.convo.ir_id;

    HashTable<String, FilterItemInfo> tags {};
    for (auto const& l_ptr : context.libraries) {
        auto const& lib = *l_ptr;
        for (auto const& ir : lib.sorted_irs)
            for (auto const& [tag, tag_hash] : ir->tags)
                tags.InsertGrowIfNeeded(box_system.arena, tag, {.num_used_in_items_lists = 0}, tag_hash);
    }

    auto libraries =
        OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo>::Create(box_system.arena,
                                                                           context.libraries.size);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena, context.libraries.size);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(box_system.arena, 16);
    auto root_folder = FolderRootSet::Create(box_system.arena, 8);

    FilterItemInfo favourites_info {};

    for (auto const l : context.libraries) {
        if (l->irs_by_name.size == 0) continue;
        auto& lib_found = libraries.FindOrInsertWithoutGrowing(l->Id(), {}).element.data;
        auto& author_found = library_authors.FindOrInsertWithoutGrowing(l->author, {}).element.data;

        if (auto& f = l->root_folders[ToInt(sample_lib::ResourceType::Ir)]; f.first_child)
            root_folder.InsertGrowIfNeeded(box_system.arena, &f);

        for (auto const& ir : l->sorted_irs) {
            auto const skip = ShouldSkipIr(context, state, *ir);

            if (IsFavourite(context.prefs, FavouriteIr(), (s64)sample_lib::IrHash(*ir))) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

            ++lib_found.total_available;
            if (!skip) ++lib_found.num_used_in_items_lists;

            ++author_found.total_available;
            if (!skip) ++author_found.num_used_in_items_lists;

            for (auto f = ir->folder; f; f = f->parent) {
                auto& i = folders.FindOrInsertGrowIfNeeded(box_system.arena, f, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            for (auto const& [tag, tag_hash] : ir->tags) {
                auto& tag_found =
                    tags.FindOrInsertGrowIfNeeded(box_system.arena, tag, {}, tag_hash).element.data;
                ++tag_found.total_available;
                if (!skip) ++tag_found.num_used_in_items_lists;
            }
            if (!ir->tags.size) {
                auto& tag_found =
                    tags.FindOrInsertGrowIfNeeded(box_system.arena, k_untagged_tag_name, {}).element.data;
                ++tag_found.total_available;
                if (!skip) ++tag_found.num_used_in_items_lists;
            }
        }
    }

    DoPickerPopup(
        box_system,
        {
            .sample_library_server = context.sample_library_server,
            .state = state.common_state,
        },
        PickerPopupOptions {
            .title = "Select Impulse Response",
            .height = 600,
            .rhs_width = 210,
            .filters_col_width = 210,
            .item_type_name = "impulse response",
            .items_section_heading = "IRs",
            .rhs_top_button =
                PickerPopupOptions::Button {
                    .text =
                        fmt::Format(box_system.arena, "Unload {}", ir_id ? (String)ir_id->ir_name : "IR"_s),
                    .tooltip = "Unload the current impulse response.",
                    .disabled = !ir_id,
                    .on_fired = TrivialFunctionRef<void()>([&]() {
                                    LoadConvolutionIr(context.engine, k_nullopt);
                                    state.common_state.open = false;
                                }).CloneObject(box_system.arena),
                },
            .rhs_do_items = [&](GuiBoxSystem& box_system) { IrPickerItems(box_system, context, state); },
            .on_load_previous = [&]() { LoadAdjacentIr(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentIr(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomIr(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters =
                LibraryFilters {
                    .library_images = context.library_images,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .unknown_library_icon = context.unknown_library_icon,
                    .card_view = true,
                    .resource_type = sample_lib::ResourceType::Ir,
                    .folders = folders,
                },
            .tags_filters =
                TagsFilters {
                    .tags = tags,
                },
            .favourites_filter_info = favourites_info,
        });
}
