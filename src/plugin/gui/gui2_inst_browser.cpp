// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_inst_browser.hpp"

#include "engine/favourite_items.hpp"
#include "gui2_common_browser.hpp"

constexpr sample_lib::LibraryIdRef k_waveform_library_id = {.author = FLOE_VENDOR, .name = "Waveforms"};

inline prefs::Key FavouriteItemKey() { return "favourite-instrument"_s; }

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

static Optional<InstrumentCursor> CurrentCursor(InstBrowserContext const& context,
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

static bool ShouldSkipInstrument(InstBrowserContext const& context,
                                 InstBrowserState const& state,
                                 sample_lib::Instrument const& inst) {
    auto& common_state = state.common_state;

    if (common_state.search.size && !InstMatchesSearch(inst, common_state.search)) return true;

    bool filtering_on = false;

    if (state.common_state.favourites_only) {
        filtering_on = true;
        if (!IsFavourite(context.prefs, FavouriteItemKey(), sample_lib::InstHash(inst))) {
            if (common_state.filter_mode == FilterMode::MultipleAnd ||
                common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (common_state.filter_mode == FilterMode::MultipleOr) return false;
        }
    }

    if (common_state.selected_folder_hashes.HasSelected()) {
        filtering_on = true;
        for (auto const& folder_hash : common_state.selected_folder_hashes) {
            if (!IsInsideFolder(inst.folder, folder_hash.hash)) {
                if (common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
        }
    }

    if (common_state.selected_library_hashes.HasSelected()) {
        filtering_on = true;
        if (!common_state.selected_library_hashes.Contains(inst.library.Id().Hash())) {
            if (common_state.filter_mode == FilterMode::MultipleAnd)
                return true;
            else if (common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (common_state.filter_mode == FilterMode::MultipleOr)
                return false;
            else if (common_state.filter_mode == FilterMode::MultipleAnd &&
                     common_state.selected_library_hashes.hashes.size != 1)
                return true;
        }
    }

    if (common_state.selected_library_author_hashes.HasSelected()) {
        filtering_on = true;
        if (!common_state.selected_library_author_hashes.Contains(Hash(inst.library.author))) {
            if (common_state.filter_mode == FilterMode::MultipleAnd)
                return true;
            else if (common_state.filter_mode == FilterMode::Single)
                return true;
        } else {
            if (common_state.filter_mode == FilterMode::MultipleOr) return false;
        }
    }

    if (common_state.selected_tags_hashes.HasSelected()) {
        filtering_on = true;
        for (auto const& selected_hash : common_state.selected_tags_hashes)
            if (!(inst.tags.ContainsSkipKeyCheck(selected_hash.hash) ||
                  (selected_hash.hash == Hash(k_untagged_tag_name) && inst.tags.size == 0))) {
                if (common_state.filter_mode == FilterMode::MultipleAnd)
                    return true;
                else if (common_state.filter_mode == FilterMode::Single)
                    return true;
            } else {
                if (common_state.filter_mode == FilterMode::MultipleOr) return false;
            }
    }

    if (filtering_on && common_state.filter_mode == FilterMode::MultipleOr) {
        // Filtering is applied, but the item does not match any of the selected filters.
        return true;
    }

    return false;
}

static Optional<InstrumentCursor> IterateInstrument(InstBrowserContext const& context,
                                                    InstBrowserState const& state,
                                                    InstrumentCursor cursor,
                                                    SearchDirection direction,
                                                    bool first) {
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

            if (ShouldSkipInstrument(context, state, inst)) continue;

            return cursor;
        }
    }

    return k_nullopt;
}

static void LoadInstrument(InstBrowserContext const& context,
                           InstBrowserState& state,
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

void LoadAdjacentInstrument(InstBrowserContext const& context,
                            InstBrowserState& state,
                            SearchDirection direction) {
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
            if (auto const cursor = IterateInstrument(context, state, {0, 0}, direction, true))
                LoadInstrument(context, state, *cursor, true);
            break;
        }
        case InstrumentType::Sampler: {
            auto const inst_id = context.layer.instrument_id.Get<sample_lib::InstrumentId>();

            if (auto const cursor = CurrentCursor(context, inst_id)) {
                if (auto const prev = IterateInstrument(context, state, *cursor, direction, false))
                    LoadInstrument(context, state, *prev, true);
            }
            break;
        }
    }
}

void LoadRandomInstrument(InstBrowserContext const& context, InstBrowserState& state) {
    auto const first =
        IterateInstrument(context, state, {.lib_index = 0, .inst_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    auto cursor = *first;

    usize num_instruments = 1;
    while (true) {
        if (auto const next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false)) {
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
        cursor = *IterateInstrument(context, state, cursor, SearchDirection::Forward, false);

    LoadInstrument(context, state, cursor, true);
}

static void InstBrowserWaveformItems(GuiBoxSystem& box_system,
                                     InstBrowserContext& context,
                                     InstBrowserState& state,
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

    auto& common_state = state.common_state;

    sample_lib::Library const pseudo_lib {
        .name = k_waveform_library_id.name,
        .author = k_waveform_library_id.author,
        .file_format_specifics = sample_lib::LuaSpecifics {},
    };
    FolderNode pseudo_folder {
        .name = "Waveforms"_s,
    };

    for (auto const waveform_type : EnumIterator<WaveformType>()) {
        sample_lib::Instrument const pseudo_inst {
            .library = pseudo_lib,
            .name = k_waveform_type_names[ToInt(waveform_type)],
            .folder = &pseudo_folder,
        };

        if (ShouldSkipInstrument(context, state, pseudo_inst)) continue;

        auto const inst_hash = sample_lib::InstHash(pseudo_inst);
        auto const is_current = waveform_type == context.layer.instrument_id.TryGetOpt<WaveformType>();
        auto const is_favourite = IsFavourite(context.prefs, FavouriteItemKey(), inst_hash);

        auto const item = DoBrowserItem(
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
                .item_id = inst_hash,
                .is_current = is_current,
                .is_favourite = is_favourite,
                .notifications = context.notifications,
                .store = context.persistent_store,
            });

        if (item.fired) {
            if (is_current)
                LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
            else
                LoadInstrument(context.engine, context.layer.index, waveform_type);
        }

        if (item.favourite_toggled)
            ToggleFavourite(context.prefs, FavouriteItemKey(), inst_hash, is_favourite);
    }
}

static void InstBrowserItems(GuiBoxSystem& box_system, InstBrowserContext& context, InstBrowserState& state) {
    auto& common_state = state.common_state;

    auto const root = DoBrowserItemsRoot(box_system);

    DEFER { InstBrowserWaveformItems(box_system, context, state, root); };

    Optional<FolderNode*> previous_folder {};
    Optional<BrowserSection> folder_section {};

    auto const first =
        IterateInstrument(context, state, {.lib_index = 0, .inst_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    Optional<graphics::ImageID> lib_icon {};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.libraries[cursor.lib_index];
        auto const& inst = *lib.sorted_instruments[cursor.inst_index];
        auto const& folder = inst.folder;
        auto const new_folder = folder != previous_folder;

        if (new_folder) {
            previous_folder = folder;

            folder_section = BrowserSection {
                .state = common_state,
                .id = folder->Hash(),
                .parent = root,
                .folder = folder,
            };
        }

        if (folder_section->Do(box_system).tag != BrowserSection::State::Collapsed) {
            auto const inst_id = sample_lib::InstrumentId {lib.Id(), inst.name};
            auto const inst_hash = sample_lib::InstHash(inst);
            auto const is_current = context.layer.instrument_id == inst_id;
            auto const is_favourite = IsFavourite(context.prefs, FavouriteItemKey(), inst_hash);

            // TODO: a Panic was hit here where the GUI changed between layout and render passes while
            // updating a floe.lua file. It's rare though.
            auto const item =
                DoBrowserItem(box_system,
                              common_state,
                              {
                                  .parent = folder_section->Do(box_system).Get<Box>(),
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
                                  .item_id = inst_hash,
                                  .is_current = is_current,
                                  .is_favourite = is_favourite,
                                  .is_tab_item = new_folder,
                                  .icons = ({
                                      if (&lib != previous_library) {
                                          previous_library = &lib;
                                          auto const imgs = GetLibraryImages(context.library_images,
                                                                             box_system.imgui,
                                                                             lib.Id(),
                                                                             context.sample_library_server,
                                                                             LibraryImagesTypes::Icon);
                                          lib_icon = imgs.icon ? imgs.icon : context.unknown_library_icon;
                                      }
                                      decltype(BrowserItemOptions::icons) {lib_icon};
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

            if (item.fired) {
                if (is_current)
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                else
                    LoadInstrument(context.engine, context.layer.index, inst_id);
            }

            if (item.favourite_toggled) {
                dyn::Append(box_system.state->deferred_actions,
                            [&prefs = context.prefs, hash = inst_hash, is_favourite]() {
                                ToggleFavourite(prefs, FavouriteItemKey(), hash, is_favourite);
                            });
            }
        }

        if (auto next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoInstBrowserPopup(GuiBoxSystem& box_system, InstBrowserContext& context, InstBrowserState& state) {
    if (!state.common_state.open) return;

    HashTable<String, FilterItemInfo> tags {};
    auto libraries =
        OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo>::Create(box_system.arena,
                                                                           context.libraries.size + 1);
    auto library_authors =
        OrderedHashTable<String, FilterItemInfo>::Create(box_system.arena, context.libraries.size + 1);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(box_system.arena, 16);
    auto root_folder = FolderRootSet::Create(box_system.arena, 8);

    FilterItemInfo favourites_info {};

    for (auto const l : context.libraries) {
        if (l->sorted_instruments.size == 0) continue;

        auto& lib = libraries.FindOrInsertWithoutGrowing(l->Id(), {}).element.data;
        auto& author = library_authors.FindOrInsertWithoutGrowing(l->author, {}).element.data;

        root_folder.InsertGrowIfNeeded(box_system.arena,
                                       &l->root_folders[ToInt(sample_lib::ResourceType::Instrument)]);

        for (auto const& inst : l->sorted_instruments) {
            auto const skip = ShouldSkipInstrument(context, state, *inst);

            if (IsFavourite(context.prefs, FavouriteItemKey(), sample_lib::InstHash(*inst))) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

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

    FilterCardOptions const waveform_card {
        .common =
            {
                .is_selected =
                    state.common_state.selected_library_hashes.Contains(k_waveform_library_id.Hash()),
                .text = k_waveform_library_id.name,
                .hashes = state.common_state.selected_library_hashes,
                .clicked_hash = k_waveform_library_id.Hash(),
                .filter_mode = state.common_state.filter_mode,
            },
        .library_id = sample_lib::k_builtin_library_id,
        .library_images = context.library_images,
        .sample_library_server = context.sample_library_server,
        .subtext = "Basic waveforms built into Floe",
    };

    FilterItemInfo const waveform_info = {
        .num_used_in_items_lists = state.common_state.HasFilters() ? 0 : ToInt(WaveformType::Count),
        .total_available = ToInt(WaveformType::Count),
    };

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
            .title = fmt::Format(box_system.arena, "Layer {} Instrument", context.layer.index + 1),
            .height = ({
                auto const window_height = box_system.imgui.frame_input.window_size.height;
                auto const button_bottom = state.common_state.absolute_button_rect.Bottom();
                auto const available_height = window_height - button_bottom - 20;
                box_system.imgui.PixelsToVw(available_height);
            }),
            .rhs_width = 300,
            .filters_col_width = 250,
            .item_type_name = "instrument",
            .rhs_top_button =
                BrowserPopupOptions::Button {
                    .text = fmt::Format(
                        box_system.arena,
                        "Unload {}",
                        context.layer.instrument_id.tag == InstrumentType::None ? "Instrument"_s : ({
                            auto n = context.layer.InstName();
                            if (n.size > 14)
                                n = fmt::Format(box_system.arena,
                                                "{}…",
                                                n.SubSpan(0, FindUtf8TruncationPoint(n, 14)));
                            n;
                        })),
                    .tooltip = "Unload the current instrument.",
                    .disabled = context.layer.instrument_id.tag == InstrumentType::None,
                    .on_fired = TrivialFunctionRef<void()>([&]() {
                                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                                    state.common_state.open = false;
                                }).CloneObject(box_system.arena),
                },
            .rhs_do_items = [&](GuiBoxSystem& box_system) { InstBrowserItems(box_system, context, state); },
            .show_search = true,
            .filter_search_placeholder_text = "Search libraries/tags",
            .item_search_placeholder_text = "Search instruments",
            .on_load_previous = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomInstrument(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters = ({
                Optional<LibraryFilters> f = LibraryFilters {
                    .library_images = context.library_images,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .unknown_library_icon = context.unknown_library_icon,
                    .card_view = true,
                    .resource_type = sample_lib::ResourceType::Instrument,
                    .folders = folders,
                    .additional_pseudo_card = &waveform_card,
                    .additional_pseudo_card_info = &waveform_info,
                    .error_notifications = context.engine.error_notifications,
                    .notifications = context.notifications,
                    .confirmation_dialog_state = context.confirmation_dialog_state,
                };
                f;
            }),
            .tags_filters = ({
                Optional<TagsFilters> f = TagsFilters {
                    .tags = tags,
                };
                f;
            }),
            .favourites_filter_info = favourites_info,
        });
}
