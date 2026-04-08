// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_ir_browser.hpp"

#include "engine/favourite_items.hpp"
#include "gui/panels/gui_common_browser.hpp"

static Optional<IrCursor> CurrentCursor(IrBrowserContext const& context, sample_lib::IrId const& ir_id) {
    for (auto const [lib_index, l] : Enumerate(context.frame_context.libraries)) {
        if (l->id != ir_id.library) continue;
        for (auto const [ir_index, i] : Enumerate(l->sorted_irs))
            if (i->id == ir_id.ir_id) return IrCursor {lib_index, ir_index};
    }

    return k_nullopt;
}

static bool IrMatchesSearch(sample_lib::ImpulseResponse const& ir, String search) {
    if (ContainsCaseInsensitiveAscii(ir.name, search)) return true;
    return false;
}

static bool ShouldSkipIr(IrBrowserContext const& context,
                         IrBrowserState const& state,
                         sample_lib::ImpulseResponse const& ir) {
    if (state.common_state.search.size && !IrMatchesSearch(ir, state.common_state.search)) return true;

    return ShouldSkipByFilters(state.common_state, [&](usize index, FilterSelection const& filter) -> bool {
        auto const fi = (FilterIndex)index;

        if (fi == FilterIndex::Favourites)
            return IsFavourite(context.prefs, k_favourite_ir_key, sample_lib::PersistentIrHash(ir));

        if (fi == FilterIndex::Folder) {
            bool any_match = false;
            filter.ForEachSelected([&](String, u64 key) {
                if (IsInsideFolder(ir.folder, key)) any_match = true;
            });
            return any_match;
        }

        if (fi == FilterIndex::Library) return filter.Contains(Hash(ir.library.id));

        if (fi == FilterIndex::LibraryAuthor) return filter.Contains(ir.library.author_hash);

        if (fi == FilterIndex::Tags) return MatchesTagFilter(filter, ir.tags, state.common_state.filter_mode);

        return false;
    });
}

static Optional<IrCursor> IterateIr(IrBrowserContext const& context,
                                    IrBrowserState const& state,
                                    IrCursor cursor,
                                    SearchDirection direction,
                                    bool first) {
    auto const& libs = context.frame_context.libraries;
    if (libs.size == 0) return k_nullopt;

    if (cursor.lib_index >= libs.size) cursor.lib_index = 0;

    if (!first) {
        switch (direction) {
            case SearchDirection::Forward: ++cursor.ir_index; break;
            case SearchDirection::Backward:
                static_assert(UnsignedInt<decltype(cursor.ir_index)>);
                --cursor.ir_index;
                break;
        }
    }

    for (usize lib_step = 0; lib_step < libs.size + 1; (
             {
                 ++lib_step;
                 switch (direction) {
                     case SearchDirection::Forward:
                         cursor.lib_index = (cursor.lib_index + 1) % libs.size;
                         cursor.ir_index = 0;
                         break;
                     case SearchDirection::Backward:
                         static_assert(UnsignedInt<decltype(cursor.lib_index)>);
                         --cursor.lib_index;
                         if (cursor.lib_index >= libs.size) // check wraparound
                             cursor.lib_index = libs.size - 1;
                         cursor.ir_index = libs[cursor.lib_index]->irs_by_id.size - 1;
                         break;
                 }
             })) {
        auto const& lib = *libs[cursor.lib_index];

        if (lib.irs_by_id.size == 0) continue;

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

static void LoadIr(IrBrowserContext const& context, IrBrowserState& state, IrCursor const& cursor) {
    auto const& lib = *context.frame_context.libraries[cursor.lib_index];
    auto const& ir = *lib.sorted_irs[cursor.ir_index];
    LoadConvolutionIr(context.engine, sample_lib::IrId {lib.id, ir.id});
    state.scroll_to_show_selected = true;
}

void LoadAdjacentIr(IrBrowserContext const& context, IrBrowserState& state, SearchDirection direction) {
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

void LoadRandomIr(IrBrowserContext const& context, IrBrowserState& state) {
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

void IrBrowserItems(GuiBuilder& builder, IrBrowserContext& context, IrBrowserState& state) {
    auto const root = DoBrowserItemsRoot(builder);

    Optional<u64> previous_folder_hash {};
    Optional<BrowserSection> folder_section {};

    auto const first =
        IterateIr(context, state, {.lib_index = 0, .ir_index = 0}, SearchDirection::Forward, true);
    if (!first) return;

    sample_lib::Library const* previous_library {};
    ItemIcon lib_icon {ItemIconType::None};
    auto cursor = *first;
    while (true) {
        auto const& lib = *context.frame_context.libraries[cursor.lib_index];
        auto const& ir = *lib.sorted_irs[cursor.ir_index];
        auto const& folder = ir.folder;
        auto folder_hash = folder->Hash();
        HashUpdate(folder_hash, lib.id);
        auto const new_folder = folder_hash != previous_folder_hash;

        if (new_folder) {
            previous_folder_hash = folder_hash;
            folder_section = BrowserSection {
                .state = state.common_state,
                .id = folder_hash,
                .parent = root,
                .folder = folder,
            };
        }

        auto const ir_id = sample_lib::IrId {lib.id, ir.id};
        auto const ir_hash = sample_lib::PersistentIrHash(ir);
        auto const is_current = context.engine.processor.convo.ir_id == ir_id;
        auto const is_favourite = IsFavourite(context.prefs, k_favourite_ir_key, ir_hash);

        if (folder_section->Do(builder).tag != BrowserSection::State::Collapsed) {
            auto const item =
                DoBrowserItem(builder,
                              state.common_state,
                              {
                                  .parent = folder_section->Do(builder).Get<Box>(),
                                  .id_extra = ir_hash,
                                  .text = ir.name,
                                  .tooltip = FunctionRef<String()>([&]() -> String {
                                      DynamicArray<char> buffer {builder.arena};

                                      fmt::Append(buffer, "{}. Tags: ", ir.name);
                                      if (ir.tags.AnyValuesSet()) {
                                          bool first = true;
                                          ir.tags.ForEachSetBit([&](usize bit) {
                                              if (!first) fmt::Append(buffer, ", ");
                                              first = false;
                                              fmt::Append(buffer, "{}", GetTagInfo((TagType)bit).name);
                                          });
                                      } else {
                                          dyn::AppendSpan(buffer, "none");
                                      }

                                      return buffer.ToOwnedSpan();
                                  }),
                                  .item_id = ir_hash,
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
                                      decltype(BrowserItemOptions::icons) result;
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
                    LoadConvolutionIr(context.engine, k_nullopt);
                else
                    LoadConvolutionIr(context.engine, ir_id);
            }

            if (item.favourite_toggled)
                ToggleFavourite(context.prefs, k_favourite_ir_key, ir_hash, is_favourite);
        }

        if (auto next = IterateIr(context, state, cursor, SearchDirection::Forward, false)) {
            cursor = *next;
            if (cursor == *first) break;
        } else {
            break;
        }
    }
}

void DoIrBrowserPopup(GuiBuilder& builder, IrBrowserContext& context, IrBrowserState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    auto const& libs = context.frame_context.libraries;
    auto& ir_id = context.engine.processor.convo.ir_id;

    TagsFilters tags_filters {};

    auto libraries =
        OrderedHashTable<sample_lib::LibraryId, FilterItemInfo, NoHash, LibraryIdLessThanFilterInfo>::Create(
            builder.arena,
            libs.size);
    auto library_authors = OrderedHashTable<String, FilterItemInfo>::Create(builder.arena, libs.size);

    auto folders = HashTable<FolderNode const*, FilterItemInfo>::Create(builder.arena, 16);
    auto root_folder = FolderRootSet::Create(builder.arena, 8);

    FilterItemInfo favourites_info {};

    for (auto const l : libs) {
        if (l->irs_by_id.size == 0) continue;
        auto& lib_found = libraries.FindOrInsertWithoutGrowing(l->id, {}).element.data;
        auto& author_found =
            library_authors.FindOrInsertWithoutGrowing(l->author, {}, l->author_hash).element.data;

        if (auto& f = l->root_folders[ToInt(sample_lib::ResourceType::Ir)]; f.first_child)
            root_folder.InsertGrowIfNeeded(builder.arena, &f);

        for (auto const& ir : l->sorted_irs) {
            auto const skip = ShouldSkipIr(context, state, *ir);

            if (IsFavourite(context.prefs, k_favourite_ir_key, sample_lib::PersistentIrHash(*ir))) {
                if (!skip) ++favourites_info.num_used_in_items_lists;
                ++favourites_info.total_available;
            }

            ++lib_found.total_available;
            if (!skip) ++lib_found.num_used_in_items_lists;

            ++author_found.total_available;
            if (!skip) ++author_found.num_used_in_items_lists;

            for (auto f = ir->folder; f; f = f->parent) {
                auto& i = folders.FindOrInsertGrowIfNeeded(builder.arena, f, {}).element.data;
                if (!skip) ++i.num_used_in_items_lists;
                ++i.total_available;
            }

            ir->tags.ForEachSetBit([&](usize bit) {
                tags_filters.available_tags.Set(bit);
                auto& i = tags_filters.tags[bit];
                ++i.total_available;
                if (!skip) ++i.num_used_in_items_lists;
            });
            if (!ir->tags.AnyValuesSet()) {
                tags_filters.has_untagged = true;
                auto& i = tags_filters.untagged_info;
                ++i.total_available;
                if (!skip) ++i.num_used_in_items_lists;
            }
        }
    }

    DoBrowserModal(
        builder,
        {
            .browser_id = state.k_panel_id,
            .sample_library_server = context.sample_library_server,
            .preferences = context.prefs,
            .store = context.persistent_store,
            .state = state.common_state,
            .instance_index = context.engine.instance_index,
        },
        BrowserPopupOptions {
            .title = "Impulse Response",
            .height = 600,
            .rhs_width = 230,
            .filters_col_width = 230,
            .item_type_name = "impulse response",
            .rhs_top_button =
                BrowserPopupOptions::Button {
                    .text = fmt::Format(builder.arena,
                                        "Unload {}",
                                        ir_id ? ({
                                            auto n = IrName(context.engine);
                                            usize constexpr k_max_len = 10;
                                            if (n.size > k_max_len)
                                                n = fmt::Format(
                                                    builder.arena,
                                                    "{}…",
                                                    n.SubSpan(0, FindUtf8TruncationPoint(n, k_max_len)));
                                            n;
                                        })
                                              : "IR"_s),
                    .tooltip = "Unload the current impulse response.",
                    .disabled = !ir_id,
                    .on_fired = TrivialFunctionRef<void()>([&]() {
                                    LoadConvolutionIr(context.engine, k_nullopt);
                                    builder.imgui.CloseModal(state.k_panel_id);
                                }).CloneObject(builder.arena),
                },
            .rhs_do_items = [&](GuiBuilder& builder) { IrBrowserItems(builder, context, state); },
            .filter_search_placeholder_text = "Search libraries/tags",
            .item_search_placeholder_text = "Search IRs",
            .on_load_previous = [&]() { LoadAdjacentIr(context, state, SearchDirection::Backward); },
            .on_load_next = [&]() { LoadAdjacentIr(context, state, SearchDirection::Forward); },
            .on_load_random = [&]() { LoadRandomIr(context, state); },
            .on_scroll_to_show_selected = [&]() { state.scroll_to_show_selected = true; },
            .library_filters =
                LibraryFilters {
                    .libraries_table = context.frame_context.lib_table,
                    .library_images = context.library_images,
                    .instance_index = context.engine.instance_index,
                    .libraries = libraries,
                    .library_authors = library_authors,
                    .card_view = true,
                    .resource_type = sample_lib::ResourceType::Ir,
                    .folders = folders,
                    .error_notifications = context.engine.error_notifications,
                    .notifications = context.notifications,
                    .confirmation_dialog_state = context.confirmation_dialog_state,
                },
            .tags_filters = tags_filters,
            .favourites_filter_info = favourites_info,
        });
}
