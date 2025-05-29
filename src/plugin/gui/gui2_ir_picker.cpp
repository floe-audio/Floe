// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_ir_picker.hpp"

#include "gui2_common_picker.hpp"

static Optional<IrCursor> CurrentCursor(IrPickerContext const& context, sample_lib::IrId const& ir_id) {
    for (auto const [lib_index, l] : Enumerate(context.libraries)) {
        if (l->Id() != ir_id.library) continue;
        for (auto const [ir_index, i] : Enumerate(l->sorted_irs))
            if (i->name == ir_id.ir_name) return IrCursor {lib_index, ir_index};
    }

    return k_nullopt;
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
                         cursor.ir_index = context.libraries[cursor.lib_index]->sorted_irs.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *context.libraries[cursor.lib_index];

        if (lib.sorted_irs.size == 0) continue;

        if (state.selected_library_hashes.size && !Contains(state.selected_library_hashes, lib.Id().Hash()))
            continue;

        if (state.selected_library_author_hashes.size &&
            !Contains(state.selected_library_author_hashes, Hash(lib.author)))
            continue;

        for (; cursor.ir_index < lib.sorted_irs.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.ir_index; break;
                         case SearchDirection::Backward: --cursor.ir_index; break;
                     }
                 })) {
            auto const& ir = *lib.sorted_irs[cursor.ir_index];

            if (state.search.size && (!ContainsCaseInsensitiveAscii(ir.name, state.search) &&
                                      !ContainsCaseInsensitiveAscii(ir.folder.ValueOr({}), state.search)))
                continue;

            if (state.selected_tags_hashes.size) {
                bool found = false;
                for (auto const tag : ir.tags) {
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

    Optional<Optional<String>> previous_folder {};
    Box folder_box {};

    auto const first =
        IterateIr(context, state, {.lib_index = 0, .ir_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    Optional<graphics::TextureHandle> lib_icon_tex {};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.libraries[cursor.lib_index];
        auto const& ir = *lib.sorted_irs[cursor.ir_index];
        auto const& folder = ir.folder;

        if (folder != previous_folder) {
            previous_folder = folder;
            folder_box = DoPickerItemsSectionContainer(box_system,
                                                       {
                                                           .parent = root,
                                                           .heading = folder,
                                                           .heading_is_folder = true,
                                                       });
        }

        auto const ir_id = sample_lib::IrId {lib.Id(), ir.name};
        auto const is_current = context.engine.processor.convo.ir_id == ir_id;

        auto const item = DoPickerItem(
            box_system,
            {
                .parent = folder_box,
                .text = ir.name,
                .is_current = is_current,
                .icon = ({
                    if (&lib != previous_library) {
                        lib_icon_tex = k_nullopt;
                        previous_library = &lib;
                        if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                         box_system.imgui,
                                                                         lib.Id(),
                                                                         context.sample_library_server,
                                                                         box_system.arena,
                                                                         true);
                            imgs && imgs->icon) {
                            lib_icon_tex =
                                box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs->icon);
                        }
                    }
                    lib_icon_tex;
                }),
            });

        if (is_current && box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
            Exchange(state.scroll_to_show_selected, false)) {
            box_system.imgui.ScrollWindowToShowRectangle(layout::GetRect(box_system.layout, item.layout_id));
        }

        if (item.is_hot) context.hovering_ir = &ir;
        if (item.button_fired) {
            if (is_current) {
                LoadConvolutionIr(context.engine, k_nullopt);
            } else {
                LoadConvolutionIr(context.engine,
                                  sample_lib::IrId {
                                      .library = lib.Id(),
                                      .ir_name = ir.name,
                                  });
                box_system.imgui.CloseCurrentPopup();
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

void DoIrPickerPopup(GuiBoxSystem& box_system,
                     imgui::Id popup_id,
                     Rect absolute_button_rect,
                     IrPickerContext& context,
                     IrPickerState& state) {
    auto& ir_id = context.engine.processor.convo.ir_id;

    DynamicSet<String> all_tags {box_system.arena};
    for (auto const& l_ptr : context.libraries) {
        if (state.selected_library_hashes.size &&
            !Contains(state.selected_library_hashes, l_ptr->Id().Hash()))
            continue;
        if (state.selected_library_author_hashes.size &&
            !Contains(state.selected_library_author_hashes, Hash(l_ptr->author)))
            continue;
        auto const& lib = *l_ptr;
        for (auto const& ir : lib.sorted_irs)
            for (auto const& tag : ir->tags)
                all_tags.Insert(tag);
    }

    DoPickerPopup(
        box_system,
        popup_id,
        absolute_button_rect,
        PickerPopupOptions {
            .sample_library_server = context.sample_library_server,
            .title = "Select Impulse Response",
            .height = box_system.imgui.PixelsToVw(box_system.imgui.frame_input.window_size.height * 0.5f),
            .rhs_width = 200,
            .filters_col_width = 200,
            .item_type_name = "impulse response",
            .items_section_heading = "IRs",
            .rhs_top_button = ({
                Optional<PickerPopupOptions::Button> unload_button {};
                if (ir_id) {
                    unload_button = PickerPopupOptions::Button {
                        .text = fmt::Format(box_system.arena, "Unload {}", ir_id->ir_name),
                        .tooltip = "Unload the current impulse response.",
                        .on_fired = TrivialFunctionRef<void()>([&]() {
                                        LoadConvolutionIr(context.engine, k_nullopt);
                                        box_system.imgui.CloseCurrentPopup();
                                    }).CloneObject(box_system.arena),
                    };
                }
                unload_button;
            }),
            .rhs_do_items = [&](GuiBoxSystem& box_system) { IrPickerItems(box_system, context, state); },
            .search = &state.search,
            .on_load_previous = [&]() { LoadAdjacentIr(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentIr(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomIr(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .libraries = ({
                DynamicArray<sample_lib::LibraryIdRef> libraries {box_system.arena};
                for (auto const l : context.libraries) {
                    if (l->irs_by_name.size == 0) continue;
                    dyn::Append(libraries, l->Id());
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
                    .tags = {all_tags.table},
                },
            .on_clear_all_filters = [&]() { state.ClearAllFilters(); },
            .status_bar_height = 58,
            .status = [&]() -> Optional<String> {
                Optional<String> status {};

                if (context.hovering_ir) {
                    DynamicArray<char> buffer {box_system.arena};

                    fmt::Append(buffer, "{}. Tags: ", context.hovering_ir->name);
                    if (context.hovering_ir->tags.size) {
                        for (auto const& tag : context.hovering_ir->tags)
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
