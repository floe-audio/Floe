// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_inst_browser.hpp"

#include "engine/favourite_items.hpp"
#include "gui/panels/gui_common_browser.hpp"

constexpr sample_lib::LibraryId k_waveform_library_id =
    sample_lib::HashLibraryIdStringWithoutRegistration("Waveforms - " FLOE_VENDOR);

struct InstrumentCursor {
    bool operator==(InstrumentCursor const& o) const = default;
    usize lib_index;
    usize inst_index;
};

static Optional<InstrumentCursor> CurrentCursor(InstBrowserContext const& context,
                                                sample_lib::InstrumentId const& inst_id) {
    for (auto const [lib_index, l] : Enumerate(context.frame_context.libraries)) {
        if (l->id != inst_id.library) continue;
        for (auto const [inst_index, i] : Enumerate(l->sorted_instruments))
            if (i->id == inst_id.inst_id) return InstrumentCursor {lib_index, inst_index};
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
        if (!IsFavourite(context.prefs, k_favourite_inst_key, sample_lib::PersistentInstHash(inst))) {
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
        if (!common_state.selected_library_hashes.Contains(Hash(inst.library.id))) {
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

    if (common_state.HasTagFilters()) {
        filtering_on = true;

        bool const untagged_matched = common_state.selected_untagged && !inst.tags.AnyValuesSet();
        auto const tags_intersection = common_state.selected_tags & inst.tags;

        switch (common_state.filter_mode) {
            case FilterMode::Single:
            case FilterMode::MultipleAnd: {
                if (common_state.selected_untagged && !untagged_matched) return true;
                if (tags_intersection != common_state.selected_tags) return true;
                break;
            }
            case FilterMode::MultipleOr: {
                if (untagged_matched || tags_intersection.AnyValuesSet()) return false;
                break;
            }
            case FilterMode::Count: break;
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
    auto const& libs = context.frame_context.libraries;
    if (libs.size == 0) return k_nullopt;

    if (cursor.lib_index >= libs.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.inst_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.inst_index)>);
                --cursor.inst_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < libs.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % libs.size;
                         cursor.inst_index = 0;
                         break;
                     case SearchDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.lib_index)>);
                         --cursor.lib_index;
                         if (cursor.lib_index >= libs.size) // check wraparound
                             cursor.lib_index = libs.size - 1;
                         cursor.inst_index = libs[cursor.lib_index]->sorted_instruments.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *libs[cursor.lib_index];

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
    auto const& lib = *context.frame_context.libraries[cursor.lib_index];
    auto const& inst = *lib.sorted_instruments[cursor.inst_index];
    LoadInstrument(context.engine,
                   context.layer.index,
                   sample_lib::InstrumentId {
                       .library = lib.id,
                       .inst_id = inst.id,
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

static void InstBrowserWaveformItems(GuiBuilder& builder,
                                     InstBrowserContext& context,
                                     InstBrowserState& state,
                                     Box const root) {
    auto const container = DoBox(builder,
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
        .name = "Waveforms"_s,
        .id = k_waveform_library_id,
        .author = FLOE_VENDOR,
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

        auto const inst_hash = sample_lib::PersistentInstHash(pseudo_inst);
        auto const is_current = waveform_type == context.layer.instrument_id.TryGetOpt<WaveformType>();
        auto const is_favourite = IsFavourite(context.prefs, k_favourite_inst_key, inst_hash);

        auto const item = DoBrowserItem(
            builder,
            common_state,
            {
                .parent = container,
                .id_extra = (u64)waveform_type,
                .text = k_waveform_type_names[ToInt(waveform_type)],
                .tooltip = FunctionRef<String()>([&]() -> String {
                    return fmt::Format(
                        builder.arena,
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
            ToggleFavourite(context.prefs, k_favourite_inst_key, inst_hash, is_favourite);
    }
}

static void InstBrowserItems(GuiBuilder& builder, InstBrowserContext& context, InstBrowserState& state) {
    auto& common_state = state.common_state;

    auto const root = DoBrowserItemsRoot(builder);

    DEFER { InstBrowserWaveformItems(builder, context, state, root); };

    Optional<u64> previous_folder_hash {};
    Optional<BrowserSection> folder_section {};

    auto const first =
        IterateInstrument(context, state, {.lib_index = 0, .inst_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    ItemIcon lib_icon {ItemIconType::None};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.frame_context.libraries[cursor.lib_index];
        auto const& inst = *lib.sorted_instruments[cursor.inst_index];
        auto const& folder = inst.folder;
        auto folder_hash = folder->Hash();
        HashUpdate(folder_hash, lib.id);
        auto const new_folder = folder_hash != previous_folder_hash;

        if (new_folder) {
            previous_folder_hash = folder_hash;

            folder_section = BrowserSection {
                .state = common_state,
                .id = folder_hash,
                .parent = root,
                .folder = folder,
            };
        }

        if (folder_section->Do(builder).tag != BrowserSection::State::Collapsed) {
            auto const inst_id = sample_lib::InstrumentId {lib.id, inst.id};
            auto const inst_hash = sample_lib::PersistentInstHash(inst);
            auto const is_current = context.layer.instrument_id == inst_id;
            auto const is_favourite = IsFavourite(context.prefs, k_favourite_inst_key, inst_hash);

            auto const item =
                DoBrowserItem(builder,
                              common_state,
                              {
                                  .parent = folder_section->Do(builder).Get<Box>(),
                                  .id_extra = inst_hash,
                                  .text = inst.name,
                                  .tooltip = FunctionRef<String()>([&]() -> String {
                                      DynamicArray<char> buf {builder.arena};
                                      fmt::Append(buf,
                                                  "{} from {} by {}.\n\n",
                                                  inst.name,
                                                  inst.library.name,
                                                  inst.library.author);

                                      if (inst.description) fmt::Append(buf, "{}", inst.description);

                                      fmt::Append(buf, "\n\nTags: ");
                                      if (!inst.tags.AnyValuesSet())
                                          fmt::Append(buf, "None");
                                      else {
                                          bool first = true;
                                          inst.tags.ForEachSetBit([&](usize bit) {
                                              if (!first) fmt::Append(buf, ", ");
                                              first = false;
                                              fmt::Append(buf, "{}", GetTagInfo((TagType)bit).name);
                                          });
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
                                                                             builder.imgui,
                                                                             lib.id,
                                                                             context.sample_library_server,
                                                                             context.engine.instance_index,
                                                                             LibraryImagesTypes::Icon);
                                          if (imgs.icon)
                                              lib_icon = *imgs.icon;
                                          else
                                              lib_icon = ItemIconType::None;
                                      }
                                      decltype(BrowserItemOptions::icons) result {};
                                      dyn::Emplace(result, lib_icon);
                                      result;
                                  }),
                                  .notifications = context.notifications,
                                  .store = context.persistent_store,
                              });

            if (is_current) {
                if (auto const r = BoxRect(builder, item.box)) {
                    if (Exchange(state.scroll_to_show_selected, false))
                        builder.imgui.ScrollViewportToShowRectangle(*r);
                }
            }

            if (item.fired) {
                if (is_current)
                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                else
                    LoadInstrument(context.engine, context.layer.index, inst_id);
            }

            if (item.favourite_toggled)
                ToggleFavourite(context.prefs, k_favourite_inst_key, inst_hash, is_favourite);
        }

        if (auto next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoInstBrowserPopup(GuiBuilder& builder, InstBrowserContext& context, InstBrowserState& state) {
    if (!builder.imgui.IsModalOpen(state.id)) return;
    auto const& libs = context.frame_context.libraries;

    TagsFilters tags_filters {};
    auto libraries =
        OrderedHashTable<sample_lib::LibraryId, FilterItemInfo, NoHash, LibraryIdLessThanFilterInfo>::Create(
            builder.arena,
            libs.size + 1);
    auto library_authors = OrderedHashTable<String, FilterItemInfo>::Create(builder.arena, libs.size + 1);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(builder.arena, 16);
    auto root_folder = FolderRootSet::Create(builder.arena, 8);

    FilterItemInfo favourites_info {};

    for (auto const l : libs) {
        if (l->sorted_instruments.size == 0) continue;

        auto& lib = libraries.FindOrInsertWithoutGrowing(l->id, {}).element.data;
        auto& author = library_authors.FindOrInsertWithoutGrowing(l->author, {}).element.data;

        root_folder.InsertGrowIfNeeded(builder.arena,
                                       &l->root_folders[ToInt(sample_lib::ResourceType::Instrument)]);

        for (auto const& inst : l->sorted_instruments) {
            auto const skip = ShouldSkipInstrument(context, state, *inst);

            if (IsFavourite(context.prefs, k_favourite_inst_key, sample_lib::PersistentInstHash(*inst))) {
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
                auto& i = folders.FindOrInsertGrowIfNeeded(builder.arena, f, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            inst->tags.ForEachSetBit([&](usize bit) {
                tags_filters.available_tags.Set(bit);
                auto& i = tags_filters.tags[bit];
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            });
            if (!inst->tags.AnyValuesSet()) {
                tags_filters.has_untagged = true;
                auto& i = tags_filters.untagged_info;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }
        }
    }

    auto const waveform_library_hash = state.id ^ HashFnv1a("built-in-waveforms");

    FilterCardOptions const waveform_card {
        .common =
            {
                .id_extra = SourceLocationHash(),
                .is_selected = state.common_state.selected_library_hashes.Contains(waveform_library_hash),
                .text = "Built-in Waveforms",
                .hashes = state.common_state.selected_library_hashes,
                .clicked_hash = waveform_library_hash,
                .filter_mode = state.common_state.filter_mode,
            },
        .library_id = k_waveform_library_id,
        .library_images = context.library_images,
        .sample_library_server = context.sample_library_server,
        .instance_index = context.engine.instance_index,
        .subtext = "Basic waveforms built into Floe",
        .default_collapsed = true,
        .store = &context.persistent_store,
    };

    FilterItemInfo const waveform_info = {
        .num_used_in_items_lists = state.common_state.HasFilters() ? 0 : ToInt(WaveformType::Count),
        .total_available = ToInt(WaveformType::Count),
    };

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoBrowserModal(
        builder,
        {
            .browser_id = state.id,
            .sample_library_server = context.sample_library_server,
            .preferences = context.prefs,
            .store = context.persistent_store,
            .state = state.common_state,
            .instance_index = context.engine.instance_index,
        },
        BrowserPopupOptions {
            .title = fmt::Format(builder.arena, "Layer {} Instrument", context.layer.index + 1),
            .height = ({
                auto const window_height = GuiIo().in.window_size.height;
                auto const button_bottom = state.common_state.absolute_button_rect.Bottom();
                auto const available_height = window_height - button_bottom - 20;
                PixelsToWw(available_height);
            }),
            .rhs_width = 300,
            .filters_col_width = 250,
            .item_type_name = "instrument",
            .rhs_top_button =
                BrowserPopupOptions::Button {
                    .text = fmt::Format(
                        builder.arena,
                        "Unload {}",
                        context.layer.instrument_id.tag == InstrumentType::None ? "Instrument"_s : ({
                            auto n = context.layer.InstName();
                            if (n.size > 14)
                                n = fmt::Format(builder.arena,
                                                "{}…",
                                                n.SubSpan(0, FindUtf8TruncationPoint(n, 14)));
                            n;
                        })),
                    .tooltip = "Unload the current instrument.",
                    .disabled = context.layer.instrument_id.tag == InstrumentType::None,
                    .on_fired = TrivialFunctionRef<void()>([&]() {
                                    LoadInstrument(context.engine, context.layer.index, InstrumentType::None);
                                    builder.imgui.CloseTopModal();
                                }).CloneObject(builder.arena),
                },
            .rhs_do_items = [&](GuiBuilder& builder) { InstBrowserItems(builder, context, state); },
            .show_search = true,
            .filter_search_placeholder_text = "Search libraries/tags",
            .item_search_placeholder_text = "Search instruments",
            .on_load_previous = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentInstrument(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomInstrument(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters = ({
                Optional<LibraryFilters> f = LibraryFilters {
                    .libraries_table = context.frame_context.lib_table,
                    .library_images = context.library_images,
                    .instance_index = context.engine.instance_index,
                    .libraries = libraries,
                    .library_authors = library_authors,
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
                Optional<TagsFilters> f = tags_filters;
                f;
            }),
            .favourites_filter_info = favourites_info,
        });
}
