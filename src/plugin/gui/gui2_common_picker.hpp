// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/tags.hpp"

#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "sample_lib_server/sample_library_server.hpp"

constexpr auto k_picker_item_height = 20.0f;
constexpr auto k_picker_spacing = 8.0f;

enum class SearchDirection { Forward, Backward };

enum class FilterMode : u8 {
    ProgressiveNarrowing, // AKA "match all", AND
    AdditiveSelection, // AKA "match any", OR
    Count,
};

struct CommonPickerState {
    bool HasFilters() const {
        if (selected_library_hashes.size || selected_library_author_hashes.size || selected_tags_hashes.size)
            return true;
        for (auto const& hashes : other_selected_hashes)
            if (hashes->size) return true;
        return false;
    }

    void ClearAll() {
        dyn::Clear(selected_library_hashes);
        dyn::Clear(selected_library_author_hashes);
        dyn::Clear(selected_tags_hashes);
        for (auto other_hashes : other_selected_hashes)
            dyn::Clear(*other_hashes);
    }

    DynamicArray<u64> selected_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_library_author_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_tags_hashes {Malloc::Instance()};
    DynamicArrayBounded<char, 100> search {};
    DynamicArrayBounded<DynamicArray<u64>*, 2> other_selected_hashes {};
    FilterMode filter_mode = FilterMode::ProgressiveNarrowing;
};

// Ephemeral
struct PickerPopupContext {
    sample_lib_server::Server& sample_library_server;
    sample_lib::LibraryIdRef const* hovering_lib {};
    CommonPickerState& state;
};

struct PickerItemOptions {
    Box parent;
    String text;
    bool is_current;
    Array<graphics::TextureHandle, k_num_layers + 1> icons;
};

struct FilterItemInfo {
    u32 num_used_in_items_lists {};
    u32 total_available {};
};

struct TagsFilters {
    HashTable<String, FilterItemInfo> tags;
};

struct LibraryFilters {
    LibraryImagesArray& library_images;
    OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo> libraries;
    OrderedHashTable<String, FilterItemInfo> library_authors;
};

// IMPORTANT: we use FunctionRefs here, you need to make sure the lifetime of the functions outlives the
// options.
struct PickerPopupOptions {
    struct Button {
        String text {};
        String tooltip {};
        f32 icon_scaling {};
        TrivialFunctionRef<void()> on_fired {};
    };

    struct Column {
        String title {};
        f32 width {};
    };

    String title {};
    f32 height {}; // VW
    f32 rhs_width {}; // VW
    f32 filters_col_width {}; // VW

    String item_type_name {}; // "instrument", "preset", etc.
    String items_section_heading {}; // "Instruments", "Presets", etc.

    Span<ModalTabConfig const> tab_config {};
    u32* current_tab_index;

    Optional<Button> rhs_top_button {};
    TrivialFunctionRef<void(GuiBoxSystem&)> rhs_do_items {};
    bool show_search {true};

    TrivialFunctionRef<void()> on_load_previous {};
    TrivialFunctionRef<void()> on_load_next {};
    TrivialFunctionRef<void()> on_load_random {};
    TrivialFunctionRef<void()> on_scroll_to_show_selected {};

    Optional<LibraryFilters> library_filters {};
    Optional<TagsFilters> tags_filters {};
    TrivialFunctionRef<void(GuiBoxSystem&, Box const& parent, u8& num_sections)> do_extra_filters {};
    bool has_extra_filters {};

    f32 status_bar_height {};
    TrivialFunctionRef<Optional<String>()> status {}; // Set if something is hovering
};

PUBLIC Box DoPickerItem(GuiBoxSystem& box_system, PickerItemOptions const& options) {
    auto const item =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .background_fill = options.is_current ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, k_picker_item_height},
                          .contents_direction = layout::Direction::Row,
                      },
              });

    for (auto tex : options.icons) {
        if (!tex) continue;
        DoBox(box_system,
              {
                  .parent = item,
                  .background_tex = tex,
                  .layout {
                      .size = style::k_library_icon_standard_size,
                      .margins = {.r = k_picker_spacing / 2},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = item,
              .text = options.text,
              .font = FontType::Body,
              .layout =
                  {
                      .size = layout::k_fill_parent,
                  },
          });

    return item;
}

PUBLIC Box DoPickerItemsRoot(GuiBoxSystem& box_system) {
    return DoBox(box_system,
                 {
                     .layout {
                         .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                         .contents_gap = k_picker_spacing,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

struct FilterButtonOptions {
    Box parent;
    bool is_selected;
    Optional<graphics::TextureHandle> icon;
    String text;
    DynamicArray<u64>& hashes;
    u64 clicked_hash;
    FilterMode filter_mode;
};

PUBLIC Box DoFilterButton(GuiBoxSystem& box_system,
                          CommonPickerState& state,
                          FilterItemInfo const& info,
                          FilterButtonOptions const& options) {
    auto const num_used = ({
        u32 n = 0;
        switch (options.filter_mode) {
            case FilterMode::ProgressiveNarrowing: n = info.num_used_in_items_lists; break;
            case FilterMode::AdditiveSelection: n = info.total_available; break;
            case FilterMode::Count: PanicIfReached();
        }
        n;
    });

    auto const button =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .background_fill = options.is_selected ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_active = style::Colour::Highlight,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {layout::k_hug_contents, k_picker_item_height},
                      .contents_padding = {.r = k_picker_spacing / 2},
                      .contents_gap = {k_picker_spacing / 2, 0},
                  },
              });

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = button,
                  .background_tex = options.icon,
                  .layout {
                      .size = style::k_library_icon_standard_size,
                      .margins = {.r = 3},
                  },
              });
    }

    bool grey_out = false;
    if (options.filter_mode == FilterMode::ProgressiveNarrowing) grey_out = num_used == 0;

    DoBox(box_system,
          {
              .parent = button,
              .text = fmt::FormatInline<70>("{} ({})", options.text, num_used),
              .font = FontType::Body,
              .text_fill = grey_out ? style::Colour::Surface1 : style::Colour::Text,
              .text_fill_hot = style::Colour::Text,
              .text_fill_active = style::Colour::Text,
              .size_from_text = true,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .margins = {.l = options.icon ? 0 : k_picker_spacing / 2},
                  },
          });

    if (button.button_fired) {
        dyn::Append(box_system.state->deferred_actions,
                    [&hashes = options.hashes,
                     &state = state,
                     clicked_hash = options.clicked_hash,
                     num_used = num_used,
                     is_selected = options.is_selected,
                     filter_mode = options.filter_mode]() {
                        switch (filter_mode) {
                            case FilterMode::ProgressiveNarrowing: {
                                if (is_selected) {
                                    dyn::RemoveValue(hashes, clicked_hash);
                                } else {
                                    if (num_used == 0) state.ClearAll();
                                    dyn::Append(hashes, clicked_hash);
                                }
                                break;
                            }
                            case FilterMode::AdditiveSelection: {
                                if (is_selected)
                                    dyn::RemoveValue(hashes, clicked_hash);
                                else
                                    dyn::Append(hashes, clicked_hash);
                                break;
                            }
                            case FilterMode::Count: PanicIfReached();
                        }
                    });
    }

    return button;
}

struct PickerItemsSectionOptions {
    Box parent;
    Optional<String> heading;
    Optional<String> icon;
    bool heading_is_folder;
    bool multiline_contents;
    bool subsection;
};

static Box DoPickerItemsSectionContainer(GuiBoxSystem& box_system, PickerItemsSectionOptions const& options) {
    auto const container =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .margins = {.b = options.subsection ? k_picker_spacing / 2 : 0},
                          .contents_padding = {.l = options.subsection ? k_picker_spacing / 2 : 0},
                          .contents_direction = layout::Direction::Column,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
              });

    auto const heading_container = DoBox(box_system,
                                         {
                                             .parent = container,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = k_picker_spacing / 2,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });
    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = heading_container,
                  .text = *options.icon,
                  .font_size = style::k_font_icons_size * 0.7f,
                  .font = FontType::Icons,
                  .size_from_text = true,
              });
    }

    if (options.heading) {
        DynamicArrayBounded<char, 200> buf;

        String text = *options.heading;

        if (options.heading_is_folder) {
            buf = *options.heading;
            for (auto& c : buf)
                c = ToUppercaseAscii(c);
            dyn::Replace(buf, "/"_s, ": "_s);

            text = buf;
        }

        DoBox(box_system,
              {
                  .parent = heading_container,
                  .text = text,
                  .font = FontType::Heading3,
                  .size_from_text = true,
                  .text_overflow = TextOverflowType::ShowDotsOnLeft,
                  .layout {
                      .margins = {.b = k_picker_spacing / 2},
                  },
              });
    }

    if (!options.multiline_contents) return container;

    return DoBox(box_system,
                 {
                     .parent = container,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

PUBLIC void DoPickerLibraryFilters(GuiBoxSystem& box_system,
                                   PickerPopupContext& context,
                                   Box const& parent,
                                   LibraryFilters const& library_filters,
                                   u8& sections) {
    if (library_filters.libraries.size) {
        if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++sections;

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "LIBRARIES"_s,
                                                               .multiline_contents = true,
                                                           });

        for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
            auto const is_selected = Contains(context.state.selected_library_hashes, lib_hash);
            ASSERT(lib_id.name.size);
            ASSERT(lib_id.author.size);

            auto const button = DoFilterButton(
                box_system,
                context.state,
                lib_info,
                {
                    .parent = section,
                    .is_selected = is_selected,
                    .icon = LibraryImagesFromLibraryId(library_filters.library_images,
                                                       box_system.imgui,
                                                       lib_id,
                                                       context.sample_library_server,
                                                       box_system.arena,
                                                       true)
                                .AndThen([&](LibraryImages const& imgs) {
                                    return box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(
                                        imgs.icon);
                                }),
                    .text = lib_id.name,
                    .hashes = context.state.selected_library_hashes,
                    .clicked_hash = lib_hash,
                    .filter_mode = context.state.filter_mode,
                });
            if (button.is_hot) context.hovering_lib = &lib_id;
        }
    }

    if (library_filters.library_authors.size) {
        if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++sections;

        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "LIBRARY AUTHORS"_s,
                                                               .multiline_contents = true,
                                                           });
        for (auto const [author, author_info, author_hash] : library_filters.library_authors) {
            auto const is_selected = Contains(context.state.selected_library_author_hashes, author_hash);
            DoFilterButton(box_system,
                           context.state,
                           author_info,
                           {
                               .parent = section,
                               .is_selected = is_selected,
                               .text = author,
                               .hashes = context.state.selected_library_author_hashes,
                               .clicked_hash = author_hash,
                               .filter_mode = context.state.filter_mode,
                           });
        }
    }
}

static constexpr u64 HashTagCategory(TagCategory const& category) {
    auto const i = ToInt(category);
    return Hash(Span {&i, 1});
}

PUBLIC void DoPickerTagsFilters(GuiBoxSystem& box_system,
                                PickerPopupContext& context,
                                Box const& parent,
                                TagsFilters const& tags_filters,
                                u8& sections) {
    if (!tags_filters.tags.size) return;

    if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
    ++sections;

    OrderedHashTable<TagCategory, OrderedHashTable<TagType, FilterItemInfo>> standard_tags {};
    OrderedHashTable<String, FilterItemInfo> non_standard_tags {};

    for (auto const [name, info, _] : tags_filters.tags) {
        if (auto const t = LookupTagName(name)) {
            auto& tags_for_category =
                standard_tags.FindOrInsertGrowIfNeeded(box_system.arena, t->category, {}).element.data;
            tags_for_category.InsertGrowIfNeeded(box_system.arena, t->tag, info);
        } else {
            non_standard_tags.InsertGrowIfNeeded(box_system.arena, name, info);
        }
    }

    auto const tags_container = DoPickerItemsSectionContainer(box_system,
                                                              {
                                                                  .parent = parent,
                                                                  .heading = "TAGS",
                                                                  .heading_is_folder = true,
                                                                  .multiline_contents = false,
                                                              });

    for (auto [category, tags_for_category, _] : standard_tags) {
        auto const category_info = Tags(category);
        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = tags_container,
                                                               .heading = category_info.name,
                                                               .icon = category_info.font_awesome_icon,
                                                               .heading_is_folder = true,
                                                               .multiline_contents = true,
                                                               .subsection = true,
                                                           });

        for (auto const [tag, filter_item_info, _] : tags_for_category) {
            auto const tag_info = GetTagInfo(tag);
            auto const tag_hash = Hash(tag_info.name);
            auto const is_selected = Contains(context.state.selected_tags_hashes, tag_hash);
            DoFilterButton(box_system,
                           context.state,
                           filter_item_info,
                           {
                               .parent = section,
                               .is_selected = is_selected,
                               .text = tag_info.name,
                               .hashes = context.state.selected_tags_hashes,
                               .clicked_hash = tag_hash,
                               .filter_mode = context.state.filter_mode,
                           });
        }
    }
}

PUBLIC void DoPickerStatusBar(GuiBoxSystem& box_system,
                              PickerPopupContext& context,
                              FunctionRef<Optional<String>()> custom_status) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = k_picker_spacing},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    String text {};

    if (custom_status) {
        auto const status = custom_status();
        if (status) text = *status;
    }

    if (auto const lib_id = context.hovering_lib) {
        auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, *lib_id);
        DEFER { lib.Release(); };

        DynamicArray<char> buf {box_system.arena};
        fmt::Append(buf, "{} by {}.", lib_id->name, lib_id->author);
        if (lib) {
            if (lib->description) fmt::Append(buf, " {}", lib->description);
        }
        text = buf.ToOwnedSpan();
    }

    DoBox(box_system,
          {
              .parent = root,
              .text = text,
              .wrap_width = k_wrap_to_parent,
              .font = FontType::Body,
              .size_from_text = true,
          });
}

static String FilterModeText(FilterMode mode) {
    switch (mode) {
        case FilterMode::ProgressiveNarrowing: return "Match all";
        case FilterMode::AdditiveSelection: return "Match any";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static String FilterModeDescription(FilterMode mode) {
    switch (mode) {
        case FilterMode::ProgressiveNarrowing: return "Progressively narrow down the items. AND logic.";
        case FilterMode::AdditiveSelection:
            return "Select any items that match any of the filters. OR logic.";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static void DoFilterModeMenu(GuiBoxSystem& box_system, PickerPopupContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const filter_mode : EnumIterator<FilterMode>()) {
        if (MenuItem(box_system,
                     root,
                     {
                         .text = FilterModeText(filter_mode),
                         .subtext = FilterModeDescription(filter_mode),
                         .is_selected = context.state.filter_mode == filter_mode,
                     })) {
            dyn::Append(box_system.state->deferred_actions,
                        [&mode = context.state.filter_mode, new_mode = filter_mode]() { mode = new_mode; });
        }
    }
}

static void
DoPickerPopup(GuiBoxSystem& box_system, PickerPopupContext& context, PickerPopupOptions const& options) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = {layout::k_hug_contents, options.height},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoBox(box_system,
          {
              .parent = root,
              .text = options.title,
              .font = FontType::Heading2,
              .size_from_text = true,
              .layout {
                  .margins = {.lrtb = k_picker_spacing},
              },
          });

    if (options.current_tab_index) {
        ASSERT(options.tab_config.size > 0);
        DoModalTabBar(box_system,
                      {
                          .parent = root,
                          .tabs = options.tab_config,
                          .current_tab_index = *options.current_tab_index,
                      });
    }

    {
        auto const headings_row = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

        {
            auto const lhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.filters_col_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_gap = k_picker_spacing / 2,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = lhs_top,
                      .text = "Filters",
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            if (options.library_filters || options.has_extra_filters) {
                auto const popup_id = box_system.imgui.GetID("filtermode");
                auto const popup_btn = MenuButton(box_system,
                                                  lhs_top,
                                                  {
                                                      .text = FilterModeText(context.state.filter_mode),
                                                      .tooltip = "Select filtering mode",
                                                  });
                if (popup_btn.button_fired) box_system.imgui.OpenPopup(popup_id, popup_btn.imgui_id);

                AddPanel(box_system,
                         Panel {
                             .run = [&context](
                                        GuiBoxSystem& box_system) { DoFilterModeMenu(box_system, context); },
                             .data =
                                 PopupPanel {
                                     .debug_name = "filtermode",
                                     .creator_layout_id = popup_btn.layout_id,
                                     .popup_imgui_id = popup_id,
                                     .additional_imgui_window_flags =
                                         imgui::WindowFlags_PositionOnTopOfParentPopup,
                                 },
                         });
            }

            if (context.state.HasFilters()) {
                if (IconButton(box_system,
                               lhs_top,
                               ICON_FA_XMARK,
                               "Clear all filters",
                               style::k_font_heading2_size * 0.9f,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&context]() { context.state.ClearAll(); });
                }
            }
        }

        DoModalDivider(box_system, headings_row, DividerType::Vertical);

        {
            auto const rhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.rhs_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = rhs_top,
                      .text = options.items_section_heading,
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            for (auto const& btn : ArrayT<PickerPopupOptions::Button>({
                     {
                         .text = ICON_FA_CARET_LEFT,
                         .tooltip = fmt::Format(box_system.arena, "Load previous {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_previous,
                     },
                     {
                         .text = ICON_FA_CARET_RIGHT,
                         .tooltip = fmt::Format(box_system.arena, "Load next {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_next,
                     },
                     {
                         .text = ICON_FA_SHUFFLE,
                         .tooltip = fmt::Format(box_system.arena, "Load random {}", options.item_type_name),
                         .icon_scaling = 0.8f,
                         .on_fired = options.on_load_random,
                     },
                     {
                         .text = ICON_FA_LOCATION_ARROW,
                         .tooltip =
                             fmt::Format(box_system.arena, "Scroll to current {}", options.item_type_name),
                         .icon_scaling = 0.7f,
                         .on_fired = options.on_scroll_to_show_selected,
                     },
                 })) {
                if (!btn.on_fired) continue;
                if (IconButton(box_system,
                               rhs_top,
                               btn.text,
                               btn.tooltip,
                               style::k_font_heading2_size * btn.icon_scaling,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions, [fired = btn.on_fired]() { fired(); });
                }
            }
        }
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

    auto const main_section = DoBox(box_system,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    {
        auto const lhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {options.filters_col_width, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        AddPanel(box_system,
                 {
                     .run =
                         [&](GuiBoxSystem& box_system) {
                             if (!options.library_filters && !options.tags_filters) return;

                             auto const root = DoPickerItemsRoot(box_system);

                             u8 num_lhs_sections = 0;

                             if (options.library_filters)
                                 DoPickerLibraryFilters(box_system,
                                                        context,
                                                        root,
                                                        *options.library_filters,
                                                        num_lhs_sections);
                             if (options.do_extra_filters)
                                 options.do_extra_filters(box_system, root, num_lhs_sections);

                             if (options.tags_filters)
                                 DoPickerTagsFilters(box_system,
                                                     context,
                                                     root,
                                                     *options.tags_filters,
                                                     num_lhs_sections);
                         },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = lhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "filters",
                         },
                 });
    }

    DoModalDivider(box_system, main_section, DividerType::Vertical);

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {options.rhs_width, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_gap = k_picker_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            if (auto const& btn = options.rhs_top_button) {
                if (TextButton(box_system, rhs, btn->text, btn->tooltip, true))
                    dyn::Append(box_system.state->deferred_actions, [&]() { btn->on_fired(); });
            }

            if (options.show_search) {
                auto const search_box =
                    DoBox(box_system,
                          {
                              .parent = rhs,
                              .background_fill = style::Colour::Background2,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_padding = {.lr = k_picker_spacing / 2},
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                DoBox(box_system,
                      {
                          .parent = search_box,
                          .text = ICON_FA_MAGNIFYING_GLASS,
                          .font_size = k_picker_item_height * 0.9f,
                          .font = FontType::Icons,
                          .text_fill = style::Colour::Subtext0,
                          .size_from_text = true,
                      });

                if (auto const text_input =
                        DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = context.state.search,
                                  .text_input_box = TextInputBox::SingleLine,
                                  .text_input_cursor = style::Colour::Text,
                                  .text_input_selection = style::Colour::Highlight,
                                  .layout {
                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                  },
                              });
                    text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&s = context.state.search, new_text = text_input.text_input_result->text]() {
                                    dyn::AssignFitInCapacity(s, new_text);
                                });
                }

                if (context.state.search.size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_XMARK,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .font = FontType::Icons,
                                  .text_fill = style::Colour::Subtext0,
                                  .size_from_text = true,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .activate_on_click_button = MouseButton::Left,
                                  .activation_click_event = ActivationClickEvent::Up,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&s = context.state.search]() { dyn::Clear(s); });
                    }
                }
            }
        }

        AddPanel(box_system,
                 {
                     .run = [&](GuiBoxSystem& box_system) { options.rhs_do_items(box_system); },
                     .data =
                         Subpanel {
                             .id = DoBox(box_system,
                                         {
                                             .parent = rhs,
                                             .layout {
                                                 .size = layout::k_fill_parent,
                                             },
                                         })
                                       .layout_id,
                             .imgui_id = (imgui::Id)SourceLocationHash(),
                             .debug_name = "rhs",
                         },
                 });
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

    AddPanel(
        box_system,
        {
            .run = [&](GuiBoxSystem& box_system) { DoPickerStatusBar(box_system, context, options.status); },
            .data =
                Subpanel {
                    .id = DoBox(box_system,
                                {
                                    .parent = root,
                                    .layout {
                                        .size = {layout::k_fill_parent, options.status_bar_height},
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                    },
                                })
                              .layout_id,
                    .imgui_id = (imgui::Id)SourceLocationHash(),
                    .debug_name = "status bar",
                },
        });
}

PUBLIC void DoPickerPopup(GuiBoxSystem& box_system,
                          PickerPopupContext context,
                          imgui::Id popup_id,
                          Rect absolute_button_rect,
                          PickerPopupOptions const& options) {
    RunPanel(box_system,
             Panel {
                 .run = [&](GuiBoxSystem& box_system) { DoPickerPopup(box_system, context, options); },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
