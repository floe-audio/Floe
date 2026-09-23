// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_inst_browser.hpp"

#include "foundation/container/dynamic_array.hpp"

#include "engine/favourite_items.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/panels/gui_common_browser.hpp"

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

    if (common_state.favourites.HasSelected() &&
        !IsFavourite(context.prefs, k_favourite_inst_key, sample_lib::PersistentInstHash(inst)))
        return true;

    return IsFilteredOut(common_state, [&](usize index, FilterSelection const& filter) -> bool {
        switch ((BrowserFilter)index) {
            case BrowserFilter::Folder:
                return MatchesFilterValues(filter, common_state.filter_mode, [&](String, u64 key) {
                    return IsInsideFolder(inst.folder, key);
                });
            case BrowserFilter::Library:
                return MatchesFilterValues(filter, common_state.filter_mode, [&](String, u64 key) {
                    return inst.library.id == key;
                });
            case BrowserFilter::LibraryAuthor:
                return MatchesFilterValues(filter, common_state.filter_mode, [&](String, u64 key) {
                    return inst.library.author_hash == key;
                });
            case BrowserFilter::Tags:
                return ItemMatchesTagFilter(filter, inst.tags, common_state.filter_mode);
            case BrowserFilter::CommonCount: break;
        }
        return false;
    });
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

static sample_lib::InstrumentId InstrumentIdFromCursor(InstBrowserContext const& context,
                                                       InstrumentCursor const& cursor) {
    auto const& lib = *context.frame_context.libraries[cursor.lib_index];
    auto const& inst = *lib.sorted_instruments[cursor.inst_index];
    return {
        .library = lib.id,
        .inst_id = inst.id,
    };
}

static void LoadInstrument(InstBrowserContext const& context,
                           InstBrowserState& state,
                           InstrumentCursor const& cursor,
                           bool scroll) {
    LoadInstrument(context.engine, context.layer.index, InstrumentIdFromCursor(context, cursor));
    if (scroll) state.common_state.scroll_to_show_current = true;
}

static Optional<InstrumentCursor> PickRandomInstrumentCursor(InstBrowserContext const& context,
                                                             InstBrowserState& state) {
    auto const first =
        IterateInstrument(context, state, {.lib_index = 0, .inst_index = 0}, SearchDirection::Forward, true);
    if (!first) return k_nullopt;

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

    return cursor;
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
    if (auto const cursor = PickRandomInstrumentCursor(context, state))
        LoadInstrument(context, state, *cursor, true);
}

Optional<sample_lib::InstrumentId> RandomInstrumentId(InstBrowserContext const& context,
                                                      InstBrowserState& state) {
    if (auto const cursor = PickRandomInstrumentCursor(context, state))
        return InstrumentIdFromCursor(context, *cursor);
    return k_nullopt;
}

// Built-in waveforms are presented as instruments of a pseudo library so the filters apply to them too.
struct WaveformPseudoLibrary {
    sample_lib::Instrument Instrument(WaveformType waveform_type) {
        return {
            .library = lib,
            .name = k_waveform_type_names[ToInt(waveform_type)],
            .id = k_waveform_type_names[ToInt(waveform_type)],
            .folder = &folder,
        };
    }

    sample_lib::Library const lib {
        .name = "Waveforms"_s,
        .id = sample_lib::k_waveform_library_id,
        .id_string = sample_lib::k_waveform_library_id_string,
        .author = FLOE_VENDOR,
        .file_format_specifics = sample_lib::LuaSpecifics {},
    };
    FolderNode folder {
        .name = "Waveforms"_s,
    };
};

// Returns true if any waveform items were drawn.
static bool InstBrowserWaveformItems(GuiBuilder& builder,
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
    WaveformPseudoLibrary pseudo_lib {};
    bool any_drawn = false;

    auto const icons = ({
        auto const imgs = GetLibraryImages(context.library_images,
                                           builder.imgui,
                                           sample_lib::k_waveform_library_id,
                                           context.sample_library_server,
                                           context.engine.instance_index,
                                           LibraryImagesTypes::Icon);
        decltype(BrowserItemOptions::icons) result {};
        dyn::Emplace(result, imgs.icon ? ItemIcon {*imgs.icon} : ItemIcon {ItemIconType::None});
        result;
    });

    for (auto const waveform_type : EnumIterator<WaveformType>()) {
        auto const pseudo_inst = pseudo_lib.Instrument(waveform_type);

        if (ShouldSkipInstrument(context, state, pseudo_inst)) continue;
        any_drawn = true;

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
                .value_popup = FunctionRef<String()>([&]() -> String {
                    return fmt::Format(
                        builder.arena,
                        "{} waveform. A simple waveform useful for layering with sampled Instruments.",
                        k_waveform_type_names[ToInt(waveform_type)]);
                }),
                .tooltip = BrowserItemLoadTooltip(builder.arena, "Instrument"_s),
                .item_id = inst_hash,
                .is_current = is_current,
                .is_favourite = is_favourite,
                .icons = icons,
                .notifications = context.notifications,
                .store = context.persistent_store,
            });

        if (is_current) ScrollBrowserToShowCurrent(builder, common_state, item.box);

        if (item.fired && !is_current) LoadInstrument(context.engine, context.layer.index, waveform_type);

        if (item.favourite_toggled)
            ToggleFavourite(context.prefs, k_favourite_inst_key, inst_hash, is_favourite);
    }

    return any_drawn;
}

static void InstBrowserItems(GuiBuilder& builder, InstBrowserContext& context, InstBrowserState& state) {
    auto& common_state = state.common_state;

    auto const root = DoBrowserItemsRoot(builder);

    bool any_sampled_items = false;
    DEFER {
        auto const any_waveform_items = InstBrowserWaveformItems(builder, context, state, root);
        if (!any_sampled_items && !any_waveform_items)
            DoBrowserEmptyListMessage(builder, common_state, root, "instruments"_s);
    };

    Optional<u64> previous_folder_hash {};
    Optional<BrowserSection> folder_section {};

    auto const first =
        IterateInstrument(context, state, {.lib_index = 0, .inst_index = 0}, SearchDirection::Forward, true);
    if (!first) return;
    any_sampled_items = true;

    auto const total_instruments = ({
        usize n = 0;
        for (auto const& lib : context.frame_context.libraries)
            n += lib->sorted_instruments.size;
        n;
    });

    struct PendingFavouriteToggle {
        u64 hash;
        bool was_favourite;
    };
    Optional<PendingFavouriteToggle> pending_favourite_toggle {};

    sample_lib::Library const* previous_library {};
    ItemIcon lib_icon {ItemIconType::None};
    auto cursor = *first;
    for (usize guard = 0;; ++guard) {
        ASSERT(guard <= total_instruments,
               "render loop exceeded instrument count — filter set mutated mid-frame");
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
                .skip_heading = IsSingleFolderFilterSelected(common_state, folder->Hash()),
                .tooltip_placement = TooltipPlacement::RightThenLeft,
            };
        }

        auto const inst_id = sample_lib::InstrumentId {lib.id, inst.id};
        auto const is_current = context.layer.instrument_id == inst_id;

        if (folder_section->Do(builder).tag != BrowserSection::State::Collapsed) {
            auto const inst_hash = sample_lib::PersistentInstHash(inst);
            auto const is_favourite = IsFavourite(context.prefs, k_favourite_inst_key, inst_hash);

            auto const item = DoBrowserItem(
                builder,
                common_state,
                {
                    .parent = folder_section->Do(builder).Get<Box>(),
                    .id_extra = inst_hash,
                    .text = inst.name,
                    .value_popup = FunctionRef<String()>([&]() -> String {
                        DynamicArray<char> buf {builder.arena};

                        if (inst.description) fmt::Append(buf, "{}\n\n", inst.description);

                        fmt::Append(buf, "Tags: ");
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

                        fmt::Append(buf, "\n\nLibrary: {} by {}.", inst.library.name, inst.library.author);

                        return buf.ToOwnedSpan();
                    }),
                    .tooltip = BrowserItemLoadTooltip(builder.arena, "Instrument"_s),
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

            if (is_current) ScrollBrowserToShowCurrent(builder, common_state, item.box);

            if (item.fired && !is_current) LoadInstrument(context.engine, context.layer.index, inst_id);

            if (item.favourite_toggled)
                pending_favourite_toggle = PendingFavouriteToggle {inst_hash, is_favourite};
        } else if (is_current) {
            ScrollBrowserToShowCurrent(builder, common_state, folder_section->heading_box);
        }

        if (auto next = IterateInstrument(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }

    if (pending_favourite_toggle)
        ToggleFavourite(context.prefs,
                        k_favourite_inst_key,
                        pending_favourite_toggle->hash,
                        pending_favourite_toggle->was_favourite);
}

void DoInstBrowserPopup(GuiBuilder& builder, InstBrowserContext& context, InstBrowserState& state) {

    bool const is_browser_screenshot = context.layer.index == 0 && IsScreenshotRequest("browser-full"_s);
    bool const is_browser_menu_screenshot = context.layer.index == 0 && IsScreenshotRequest("browser-menu"_s);
    bool const is_browse_list_screenshot =
        context.layer.index == 0 && IsScreenshotRequest("browser-browse"_s);

    if ((is_browser_screenshot || is_browser_menu_screenshot || is_browse_list_screenshot) &&
        !builder.imgui.IsModalOpen(state.id))
        builder.imgui.OpenModalViewport(state.id);

    if (!builder.imgui.IsModalOpen(state.id)) return;
    auto const& libs = context.frame_context.libraries;

    if (is_browser_screenshot || is_browser_menu_screenshot) {
        // Add a tag filter so the screenshot is more interesting than an empty browser. The browser-menu
        // screenshot captures the match menu, which needs 2 or more selected filters to exist.
        auto& tag_filter = state.common_state.Filter(BrowserFilter::Tags);
        if (!tag_filter.Contains((u64)TagType::Ambient)) tag_filter.Add((u64)TagType::Ambient, "ambient"_s);
        if (is_browser_menu_screenshot && !tag_filter.Contains((u64)TagType::Warm))
            tag_filter.Add((u64)TagType::Warm, "warm"_s);
        // A library too, so the Filter-mode screenshot shows filters combining.
        if (is_browser_screenshot) {
            auto& library_filter = state.common_state.Filter(BrowserFilter::Library);
            for (auto const l : libs) {
                if (l->name != "Lost Reveries"_s) continue;
                if (!library_filter.Contains(l->id)) library_filter.Add(l->id, l->name);
                break;
            }
        }
    }

    TagsFilters tags_filters {};
    auto libraries =
        OrderedHashTable<sample_lib::LibraryId, FilterItemInfo, NoHash, LibraryIdLessThanFilterInfo>::Create(
            builder.arena,
            libs.size + 1);
    auto library_authors = OrderedHashTable<String, FilterItemInfo>::Create(builder.arena, libs.size + 1);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(builder.arena, 16);
    auto root_folder = FolderRootSet::Create(builder.arena, 8);

    FilterItemInfo favourites_info {};
    u32 num_results = 0;

    for (auto const l : libs) {
        if (l->sorted_instruments.size == 0) continue;

        auto& lib = libraries.FindOrInsertWithoutGrowing(l->id, {}).element.data;
        auto& author = library_authors.FindOrInsertWithoutGrowing(l->author, {}, l->author_hash).element.data;

        root_folder.InsertGrowIfNeeded(builder.arena,
                                       &l->root_folders[ToInt(sample_lib::ResourceType::Instrument)]);

        for (auto const& inst : l->sorted_instruments) {
            auto const skip = ShouldSkipInstrument(context, state, *inst);
            if (!skip) ++num_results;

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

    FilterCollectionOptions const waveform_collection {
        .common =
            {
                .id_extra = SourceLocationHash(),
                .is_selected = state.common_state.Filter(BrowserFilter::Library)
                                   .Contains(sample_lib::k_waveform_library_id),
                .text = "Built-in Waveforms",
                .filter = state.common_state.Filter(BrowserFilter::Library),
                .clicked_key = sample_lib::k_waveform_library_id,
                .filter_mode = state.common_state.filter_mode,
            },
        .icon =
            {
                .library_id = sample_lib::k_waveform_library_id,
                .library_images = context.library_images,
                .sample_library_server = context.sample_library_server,
                .instance_index = context.engine.instance_index,
            },
        .collection_noun = "library"_s,
        .default_collapsed = true,
        .store = &context.persistent_store,
    };

    FilterItemInfo const waveform_info = {
        .num_used_in_items_lists = state.common_state.HasFilters() ? 0u : ToInt(WaveformType::Count),
        .total_available = ToInt(WaveformType::Count),
    };

    {
        WaveformPseudoLibrary pseudo_lib {};
        for (auto const waveform_type : EnumIterator<WaveformType>())
            if (!ShouldSkipInstrument(context, state, pseudo_lib.Instrument(waveform_type))) ++num_results;
    }

    state.common_state.items_still_loading =
        sample_lib_server::AreLibrariesScanning(context.sample_library_server);

    auto const current_item = ({
        using Visibility = CurrentItemStatus::Visibility;
        CurrentItemStatus status {};
        switch (context.layer.instrument_id.tag) {
            case InstrumentType::None: break;
            case InstrumentType::WaveformSynth: {
                WaveformPseudoLibrary pseudo_lib {};
                auto const inst = pseudo_lib.Instrument(context.layer.instrument_id.Get<WaveformType>());
                status.name = inst.name;
                status.collection = BrowserCollection {
                    .filter = BrowserFilter::Library,
                    .key = sample_lib::k_waveform_library_id,
                    .name = "Built-in Waveforms"_s,
                };
                status.visibility = ShouldSkipInstrument(context, state, inst) ? Visibility::HiddenByFilters
                                                                               : Visibility::Shown;
                break;
            }
            case InstrumentType::Sampler: {
                auto const& id = context.layer.instrument_id.GetFromTag<InstrumentType::Sampler>();
                status.name = context.layer.InstName();

                auto const lib = context.frame_context.lib_table.Find(id.library);
                auto const inst = lib ? (*lib)->insts_by_id.Find((String)id.inst_id) : nullptr;
                if (!inst) {
                    if (state.common_state.items_still_loading) {
                        status.visibility = Visibility::Loading;
                        break;
                    }
                    status.visibility = Visibility::NotInList;
                    status.not_in_list_reason =
                        lib ? "it's no longer in its library"_s : "its library isn't installed"_s;
                    break;
                }

                status.collection = BrowserCollection {
                    .filter = BrowserFilter::Library,
                    .key = (*lib)->id,
                    .name = (*lib)->name,
                };

                if (ShouldSkipInstrument(context, state, **inst)) {
                    status.visibility = Visibility::HiddenByFilters;
                    break;
                }

                status.section_id = ({
                    auto h = (*inst)->folder->Hash();
                    HashUpdate(h, (*lib)->id);
                    h;
                });
                status.visibility =
                    IsBrowserSectionCollapsed(state.common_state, status.section_id, (*inst)->folder->Hash())
                        ? Visibility::InCollapsedSection
                        : Visibility::Shown;
                break;
            }
        }
        status;
    });

    auto const library_collection = [&](sample_lib::Library const& lib) {
        auto const info = libraries.Find(lib.id);
        return LibraryCollection(builder.arena, lib, info ? info->total_available : 0);
    };
    auto const collection_of_library = [&](sample_lib::LibraryId id) -> Optional<BrowserCollection> {
        if (id == sample_lib::k_waveform_library_id) {
            return BrowserCollection {
                .filter = BrowserFilter::Library,
                .key = id,
                .name = waveform_collection.common.text,
                .library_id = id,
                .num_items = waveform_info.total_available,
                .subtext = "Basic waveforms built into Floe"_s,
            };
        }
        for (auto const l : libs)
            if (l->id == id) return library_collection(*l);
        return k_nullopt;
    };
    // The library whose root folder this is.
    auto const library_collection_of_root = [&](FolderNode const& root) -> Optional<BrowserCollection> {
        for (auto const l : libs)
            if (&l->root_folders[ToInt(sample_lib::ResourceType::Instrument)] == &root)
                return library_collection(*l);
        return k_nullopt;
    };

    // IMPORTANT: we create the options struct inside the call so that lambdas and values from
    // statement-expressions live long enough.
    DoBrowserModal(
        builder,
        {
            .browser_id = state.id,
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
                auto const& button_rect = state.common_state.absolute_button_rect;
                auto const space_below = window_height - button_rect.Bottom() - WwToPixels(20.0f);
                auto const space_above = button_rect.y - WwToPixels(20.0f);
                PixelsToWw(Max(space_below, space_above));
            }),
            .results_width = 300,
            .filters_col_width = 250,
            .store_id = HashFnv1a("instrument-browser"),
            .flush_with_opener = true,
            .item_type_name = "instrument",
            .plural_item_type_name = "instruments",
            .do_items = [&](GuiBuilder& builder) { InstBrowserItems(builder, context, state); },
            .show_search = true,
            .filter_search_placeholder_text = "Search libraries/tags",
            .item_search_placeholder_text = "Search instruments",
            .current_item = current_item,
            .browse_scope = CurrentBrowseScope(state.common_state,
                                               {
                                                   .collection_noun = "library"_s,
                                                   .folders = folders,
                                                   .collection_of_root = library_collection_of_root,
                                                   .collection_of_library = collection_of_library,
                                               }),
            .library_filters = ({
                Optional<LibraryFilters> f = LibraryFilters {
                    .libraries_table = context.frame_context.lib_table,
                    .library_images = context.library_images,
                    .instance_index = context.engine.instance_index,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .collection_view = true,
                    .resource_type = sample_lib::ResourceType::Instrument,
                    .folders = folders,
                    .additional_pseudo_collection = &waveform_collection,
                    .additional_pseudo_collection_info = &waveform_info,
                    .error_notifications = context.engine.error_notifications,
                    .notifications = context.notifications,
                    .confirmation_dialog_state = context.confirmation_dialog_state,
                    .collection_name_prefix = is_browser_screenshot ? "browser.library."_s : ""_s,
                };
                f;
            }),
            .tags_filters = ({
                Optional<TagsFilters> f = tags_filters;
                f;
            }),
            .favourites_filter_info = favourites_info,
            .num_results = num_results,
        });
}
