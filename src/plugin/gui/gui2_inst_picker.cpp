// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_inst_picker.hpp"

#include "gui2_common_picker.hpp"

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

static Optional<InstrumentCursor> CurrentCursor(InstPickerContext const& context,
                                                sample_lib::InstrumentId const& inst_id) {
    for (auto const [lib_index, l] : Enumerate(context.libraries)) {
        if (l->Id() != inst_id.library) continue;
        for (auto const [inst_index, i] : Enumerate(l->sorted_instruments))
            if (i->name == inst_id.inst_name) return InstrumentCursor {lib_index, inst_index};
    }

    return k_nullopt;
}

static bool InstMatchesSearch(sample_lib::Instrument const& inst, String search) {
    if (ContainsCaseInsensitiveAscii(inst.name, search)) return true;
    return false;
}

static CommonPickerState& CommonState(InstPickerState& state) {
    return (state.tab == InstPickerState::Tab::MirageLibraries) ? state.common_state_mirage_libraries
                                                                : state.common_state_floe_libraries;
}
static CommonPickerState const& CommonState(InstPickerState const& state) {
    return CommonState(const_cast<InstPickerState&>(state));
}

static bool ShouldSkipInstrument(InstPickerState const& state,
                                 sample_lib::Instrument const& inst,
                                 bool picker_gui_is_open) {
    auto& common_state = CommonState(state);

    if (common_state.search.size && !InstMatchesSearch(inst, common_state.search)) return true;

    bool filtering_on = false;

    if (common_state.selected_folder_hashes.size) {
        filtering_on = true;
        for (auto const folder_hash : common_state.selected_folder_hashes) {
            if (!IsInsideFolder(inst.folder, folder_hash)) {
                if (common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            } else {
                if (common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            }
        }
    }

    if (common_state.selected_library_hashes.size) {
        filtering_on = true;
        if (!Contains(common_state.selected_library_hashes, inst.library.Id().Hash())) {
            if (common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
        } else {
            if (common_state.filter_mode == FilterMode::AdditiveSelection) return false;
        }
    }

    if (common_state.selected_library_author_hashes.size) {
        filtering_on = true;
        if (!Contains(common_state.selected_library_author_hashes, Hash(inst.library.author))) {
            if (common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
        } else {
            if (common_state.filter_mode == FilterMode::AdditiveSelection) return false;
        }
    }

    if ((!picker_gui_is_open || state.tab == InstPickerState::Tab::FloeLibaries) &&
        common_state.selected_tags_hashes.size) {
        filtering_on = true;
        for (auto const selected_hash : common_state.selected_tags_hashes)
            if (!(inst.tags.ContainsSkipKeyCheck(selected_hash) ||
                  (selected_hash == Hash(k_untagged_tag_name) && inst.tags.size == 0))) {
                if (common_state.filter_mode == FilterMode::ProgressiveNarrowing) return true;
            } else {
                if (common_state.filter_mode == FilterMode::AdditiveSelection) return false;
            }
    }

    if (filtering_on && common_state.filter_mode == FilterMode::AdditiveSelection) {
        // Filtering is applied, but the item does not match any of the selected filters.
        return true;
    }

    return false;
}

static Optional<InstrumentCursor> IterateInstrument(InstPickerContext const& context,
                                                    InstPickerState const& state,
                                                    InstrumentCursor cursor,
                                                    SearchDirection direction,
                                                    bool first,
                                                    bool picker_gui_is_open) {
    if (context.libraries.size == 0) return k_nullopt;

    if (cursor.lib_index >= context.libraries.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.inst_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.inst_index)>);
                --cursor.inst_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < context.libraries.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % context.libraries.size;
                         cursor.inst_index = 0;
                         break;
                     case SearchDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.lib_index)>);
                         --cursor.lib_index;
                         if (cursor.lib_index >= context.libraries.size) // check wraparound
                             cursor.lib_index = context.libraries.size - 1;
                         cursor.inst_index = context.libraries[cursor.lib_index]->sorted_instruments.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *context.libraries[cursor.lib_index];

        if (lib.sorted_instruments.size == 0) continue;
        if (picker_gui_is_open && lib.file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

        // PERF: we could skip early here based on the library and filters, but only for some filter
        // modes.

        for (; cursor.inst_index < lib.sorted_instruments.size; (
                 {
                     switch (direction) {
                         case SearchDirection::Forward: ++cursor.inst_index; break;
                         case SearchDirection::Backward: --cursor.inst_index; break;
                     }
                 })) {
            auto const& inst = *lib.sorted_instruments[cursor.inst_index];

            if (ShouldSkipInstrument(state, inst, picker_gui_is_open)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void LoadInstrument(InstPickerContext const& context,
                           InstPickerState& state,
                           InstrumentCursor const& cursor,
                           bool scroll) {
    auto const& lib = *context.libraries[cursor.lib_index];
    auto const& inst = *lib.sorted_instruments[cursor.inst_index];
    LoadInstrument(context.engine,
                   context.layer.index,
                   sample_lib::InstrumentId {
                       .library = lib.Id(),
                       .inst_name = inst.name,
                   });
    if (scroll) state.scroll_to_show_selected = true;
}

void LoadAdjacentInstrument(InstPickerContext const& context,
                            InstPickerState& state,
                            SearchDirection direction,
                            bool picker_gui_is_open) {
    switch (context.layer.instrument_id.tag) {
        case InstrumentType::WaveformSynth: {
            auto waveform_index = ToInt(context.layer.instrument_id.Get<WaveformType>());
            switch (direction) {
                case SearchDirection::Forward:
                    if (waveform_index == ToInt(WaveformType::Count) - 1)
                        waveform_index = 0;
                    else
                        ++waveform_index;
                    break;
                case SearchDirection::Backward:
                    if (waveform_index == 0)
                        waveform_index = ToInt(WaveformType::Count) - 1;
                    else
                        --waveform_index;
                    break;
            }
            LoadInstrument(context.engine, context.layer.index, WaveformType(waveform_index));
            break;
        }
        case InstrumentType::None: {
            if (picker_gui_is_open && state.tab == InstPickerState::Tab::Waveforms) {
                LoadInstrument(context.engine, context.layer.index, WaveformType(0));
                break;
            }

            if (auto const cursor =
                    IterateInstrument(context, state, {0, 0}, direction, true, picker_gui_is_open)) {
                LoadInstrument(context, state, *cursor, true);
            }
            break;
        }
        case InstrumentType::Sampler: {
            auto const inst_id = context.layer.instrument_id.Get<sample_lib::InstrumentId>();

            if (auto const cursor = CurrentCursor(context, inst_id)) {
                if (auto const prev =
                        IterateInstrument(context, state, *cursor, direction, false, picker_gui_is_open)) {
                    LoadInstrument(context, state, *prev, true);
                }
            }
            break;
        }
    }
}

void LoadRandomInstrument(InstPickerContext const& context, InstPickerState& state, bool picker_gui_is_open) {
    if (picker_gui_is_open && state.tab == InstPickerState::Tab::Waveforms) {
        LoadInstrument(
            context.engine,
            context.layer.index,
            WaveformType(
                RandomIntInRange<u32>(context.engine.random_seed, 0, ToInt(WaveformType::Count) - 1)));
        return;
    }

    auto const first = IterateInstrument(context,
                                         state,
                                         {.lib_index = 0, .inst_index = 0},
                                         SearchDirection::Forward,
                                         true,
                                         picker_gui_is_open);
    if (!first) return;

    auto cursor = *first;

    usize num_instruments = 1;
    while (true) {
        if (auto const next = IterateInstrument(context,
                                                state,
                                                cursor,
                                                SearchDirection::Forward,
                                                false,
                                                picker_gui_is_open)) {
            cursor = *next;
            if (cursor == *first) break;
            ++num_instruments;
        } else {
            break;
        }
    }

    auto const random_pos = RandomIntInRange<usize>(context.engine.random_seed, 0, num_instruments - 1);

    cursor = *first;
    for (usize i = 0; i < random_pos; ++i)
        cursor =
            *IterateInstrument(context, state, cursor, SearchDirection::Forward, false, picker_gui_is_open);

    LoadInstrument(context, state, cursor, true);
}

static void InstPickerWaveformItems(GuiBoxSystem& box_system,
                                    InstPickerContext& context,
                                    InstPickerState& state,
                                    Box const root) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = root,
                                     .layout =
                                         {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_direction = layout::Direction::Column,
                                         },
                                 });

    auto& common_state = CommonState(state);

    for (auto const waveform_type : EnumIterator<WaveformType>()) {
        auto const is_current = waveform_type == context.layer.instrument_id.TryGetOpt<WaveformType>();

        auto const item = DoPickerItem(
            box_system,
            common_state,
            {
                .parent = container,
                .text = k_waveform_type_names[ToInt(waveform_type)],
                .tooltip = FunctionRef<String()>([&]() -> String {
                    return fmt::Format(
                        box_system.arena,
                        "{} waveform. A simple waveform useful for layering with sample instruments.",
                        k_waveform_type_names[ToInt(waveform_type)]);
                }),
                .is_current = is_current,
                .notifications = context.notifications,
                .store = context.persistent_store,
            });

        if (item.button_fired) {
            if (is_current)
                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
            else
                LoadInstrument(context.engine, context.layer.index, waveform_type);
        }
    }
}

static void InstPickerItems(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    auto const root = DoPickerItemsRoot(box_system);

    if (state.tab == InstPickerState::Tab::Waveforms) {
        InstPickerWaveformItems(box_system, context, state, root);
        return;
    }

    Optional<FolderNode*> previous_folder {};
    Optional<Box> folder_box {};

    auto& common_state = CommonState(state);

    auto const first = IterateInstrument(context,
                                         state,
                                         {.lib_index = 0, .inst_index = 0},
                                         SearchDirection::Forward,
                                         true,
                                         true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    graphics::TextureHandle lib_icon_tex {};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.libraries[cursor.lib_index];
        auto const& inst = *lib.sorted_instruments[cursor.inst_index];
        auto const& folder = inst.folder;

        if (folder != previous_folder) {
            previous_folder = folder;

            folder_box = DoPickerSectionContainer(box_system,
                                                  folder->Hash(),
                                                  common_state,
                                                  {
                                                      .parent = root,
                                                      .folder = folder,
                                                  });
        }

        if (folder_box) {
            auto const inst_id = sample_lib::InstrumentId {lib.Id(), inst.name};
            auto const is_current = context.layer.instrument_id == inst_id;

            // TODO: a Panic was hit here where the GUI changed between layout and render passes while
            // updating a floe.lua file. It's rare though.
            auto const item = DoPickerItem(
                box_system,
                common_state,
                {
                    .parent = *folder_box,
                    .text = inst.name,
                    .tooltip = FunctionRef<String()>([&]() -> String {
                        DynamicArray<char> buf {box_system.arena};
                        fmt::Append(buf,
                                    "{} from {} by {}.\n\n",
                                    inst.name,
                                    inst.library.name,
                                    inst.library.author);

                        if (inst.description) fmt::Append(buf, "{}", inst.description);

                        fmt::Append(buf, "\n\nTags: ");
                        if (inst.tags.size == 0)
                            fmt::Append(buf, "None");
                        else {
                            for (auto const [t, _] : inst.tags)
                                fmt::Append(buf, "{}, ", t);
                            dyn::Pop(buf, 2);
                        }

                        return buf.ToOwnedSpan();
                    }),
                    .is_current = is_current,
                    .icons = ({
                        if (&lib != previous_library) {
                            lib_icon_tex = nullptr;
                            previous_library = &lib;
                            if (auto const imgs = LibraryImagesFromLibraryId(context.library_images,
                                                                             box_system.imgui,
                                                                             lib.Id(),
                                                                             context.sample_library_server,
                                                                             box_system.arena,
                                                                             true)) {
                                auto opt = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(
                                    (imgs && !imgs->icon_missing) ? imgs->icon
                                                                  : context.unknown_library_icon);
                                lib_icon_tex = opt ? *opt : nullptr;
                            }
                        }
                        decltype(PickerItemOptions::icons) {lib_icon_tex};
                    }),
                    .notifications = context.notifications,
                    .store = context.persistent_store,
                });

            if (is_current &&
                box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender &&
                Exchange(state.scroll_to_show_selected, false)) {
                box_system.imgui.ScrollWindowToShowRectangle(
                    layout::GetRect(box_system.layout, item.layout_id));
            }

            if (item.button_fired) {
                if (is_current) {
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                } else {
                    LoadInstrument(context.engine,
                                   context.layer.index,
                                   sample_lib::InstrumentId {
                                       .library = lib.Id(),
                                       .inst_name = inst.name,
                                   });
                }
            }
        }

        if (auto next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false, true)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoInstPickerPopup(GuiBoxSystem& box_system, InstPickerContext& context, InstPickerState& state) {
    if (!state.common_state_floe_libraries.open) return;

    HashTable<String, FilterItemInfo> tags {};
    auto libraries =
        OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo>::Create(box_system.arena,
                                                                           context.libraries.size);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena, context.libraries.size);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(box_system.arena, 16);
    auto root_folder = FolderRootSet::Create(box_system.arena, 8);

    if (state.tab != InstPickerState::Tab::Waveforms) {
        for (auto const l : context.libraries) {
            if (l->sorted_instruments.size == 0) continue;
            if (l->file_format_specifics.tag != state.FileFormatForCurrentTab()) continue;

            auto& lib = libraries.FindOrInsertWithoutGrowing(l->Id(), {}).element.data;
            auto& author = library_authors.FindOrInsertWithoutGrowing(l->author, {}).element.data;

            root_folder.InsertGrowIfNeeded(box_system.arena,
                                           &l->root_folders[ToInt(sample_lib::ResourceType::Instrument)]);

            for (auto const& inst : l->sorted_instruments) {
                auto const skip = ShouldSkipInstrument(state, *inst, true);

                {
                    if (!skip) ++lib.num_used_in_items_lists;
                    ++lib.total_available;
                }

                {
                    if (!skip) ++author.num_used_in_items_lists;
                    ++author.total_available;
                }

                for (auto f = inst->folder; f; f = f->parent) {
                    auto& i = folders.FindOrInsertGrowIfNeeded(box_system.arena, f, {}).element.data;
                    if (!skip) ++i.num_used_in_items_lists;
                    ++i.total_available;
                }

                for (auto const& [tag, tag_hash] : inst->tags) {
                    auto& i = tags.FindOrInsertGrowIfNeeded(box_system.arena, tag, {}, tag_hash).element.data;
                    if (!skip) ++i.num_used_in_items_lists;
                    ++i.total_available;
                }
                if (!inst->tags.size) {
                    auto& i =
                        tags.FindOrInsertGrowIfNeeded(box_system.arena, k_untagged_tag_name, {}).element.data;
                    if (!skip) ++i.num_used_in_items_lists;
                    ++i.total_available;
                }
            }
        }
    }

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoPickerPopup(
        box_system,
        {
            .sample_library_server = context.sample_library_server,
            .state = CommonState(state),
        },
        PickerPopupOptions {
            .title = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
            .height = ({
                auto const window_height = box_system.imgui.frame_input.window_size.height;
                auto const button_bottom = state.common_state_floe_libraries.absolute_button_rect.Bottom();
                auto const available_height = window_height - button_bottom - 20;
                box_system.imgui.PixelsToVw(available_height);
            }),
            .rhs_width = 300,
            .filters_col_width = 250,
            .item_type_name = "instrument",
            .items_section_heading = "Instruments",
            .tab_config = ({
                DynamicArray<ModalTabConfig> tab_config {box_system.arena};
                dyn::Append(tab_config,
                            {
                                .text = context.has_mirage_libraries ? "Floe Instruments"_s : "Instruments",
                                .index = ToInt(InstPickerState::Tab::FloeLibaries),
                            });
                if (context.has_mirage_libraries)
                    dyn::Append(tab_config,
                                {.text = "Mirage Instruments",
                                 .index = ToInt(InstPickerState::Tab::MirageLibraries)});
                dyn::Append(tab_config,
                            {.text = "Waveforms", .index = ToInt(InstPickerState::Tab::Waveforms)});
                tab_config.ToOwnedSpan();
            }),
            .current_tab_index = &ToIntRef(state.tab),
            .rhs_top_button =
                PickerPopupOptions::Button {
                    .text = fmt::Format(box_system.arena,
                                        "Unload {}",
                                        context.layer.instrument_id.tag == InstrumentType::None
                                            ? "Instrument"
                                            : context.layer.InstName()),
                    .tooltip = "Unload the current instrument.",
                    .disabled = context.layer.instrument_id.tag == InstrumentType::None,
                    .on_fired = TrivialFunctionRef<void()>([&]() {
                                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                                    CommonState(state).open = false;
                                }).CloneObject(box_system.arena),
                },
            .rhs_do_items = [&](GuiBoxSystem& box_system) { InstPickerItems(box_system, context, state); },
            .show_search = state.tab != InstPickerState::Tab::Waveforms,
            .on_load_previous =
                [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Backward, true); },
            .on_load_next = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Forward, true); },
            .on_load_random = [&]() { LoadRandomInstrument(context, state, true); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters = ({
                Optional<LibraryFilters> f {};
                if (state.tab != InstPickerState::Tab::Waveforms) {
                    f = LibraryFilters {
                        .library_images = context.library_images,
                        .libraries = libraries,
                        .library_authors = library_authors,
                        .unknown_library_icon = context.unknown_library_icon,
                    };
                }
                f;
            }),
            .tags_filters = ({
                Optional<TagsFilters> f {};
                if (state.tab == InstPickerState::Tab::FloeLibaries) {
                    f = TagsFilters {
                        .tags = tags,
                    };
                }
                f;
            }),
            .folder_filters = ({
                Optional<FolderFilters> f {};
                if (state.tab != InstPickerState::Tab::Waveforms) {
                    f = FolderFilters {
                        .folders = folders,
                        .root_folders = root_folder,
                    };
                }
                f;
            }),
        });
}
