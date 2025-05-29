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

struct PickerItemOptions {
    Box parent;
    String text;
    bool is_current;
    Optional<graphics::TextureHandle> icon;
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

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = item,
                  .background_tex = *options.icon,
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

PUBLIC Box DoFilterButton(GuiBoxSystem& box_system,
                          Box const& parent,
                          bool is_selected,
                          Optional<graphics::TextureHandle> icon,
                          String text) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill = is_selected ? style::Colour::Highlight : style::Colour::None,
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

    if (icon) {
        DoBox(box_system,
              {
                  .parent = button,
                  .background_tex = icon,
                  .layout {
                      .size = style::k_library_icon_standard_size,
                      .margins = {.r = 3},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .font = FontType::Body,
              .text_fill = style::Colour::Text,
              .text_fill_hot = style::Colour::Text,
              .text_fill_active = style::Colour::Text,
              .size_from_text = true,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .margins = {.l = icon ? 0 : k_picker_spacing / 2},
                  },
          });

    return button;
}

struct PickerItemsSectionOptions {
    Box parent;
    Optional<String> heading;
    Optional<String> icon;
    bool heading_is_folder;
    bool multiline_contents;
};

static Box DoPickerItemsSectionContainer(GuiBoxSystem& box_system, PickerItemsSectionOptions const& options) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = options.parent,
                                     .layout =
                                         {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
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

struct TagsFilters {
    DynamicArray<u64>& selected_tags_hashes;
    Set<String> tags;
};

struct LibraryFilters {
    DynamicArray<u64>& selected_library_hashes;
    DynamicArray<u64>& selected_library_author_hashes;
    LibraryImagesArray& library_images;
    sample_lib_server::Server& sample_library_server;
};

PUBLIC void DoPickerLibraryFilters(GuiBoxSystem& box_system,
                                   Box const& parent,
                                   Span<sample_lib::LibraryIdRef const> libraries,
                                   LibraryFilters const& library_filters,
                                   sample_lib::LibraryIdRef const*& hovering_library) {
    {
        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = "LIBRARIES"_s,
                                                               .multiline_contents = true,
                                                           });

        for (auto const& lib : libraries) {
            auto const lib_id_hash = lib.Hash();
            if (library_filters.selected_library_author_hashes.size) {
                if (!Contains(library_filters.selected_library_author_hashes, Hash(lib.author))) continue;
            }
            auto const is_selected = Contains(library_filters.selected_library_hashes, lib_id_hash);

            auto const button = DoFilterButton(
                box_system,
                section,
                is_selected,
                LibraryImagesFromLibraryId(library_filters.library_images,
                                           box_system.imgui,
                                           lib,
                                           library_filters.sample_library_server,
                                           box_system.arena,
                                           true)
                    .AndThen([&](LibraryImages const& imgs) {
                        return box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(imgs.icon);
                    }),
                lib.name);
            if (button.is_hot) hovering_library = &lib;
            if (button.button_fired) {
                dyn::Append(box_system.state->deferred_actions,
                            [&hashes = library_filters.selected_library_hashes, lib_id_hash, is_selected]() {
                                if (is_selected)
                                    dyn::RemoveValue(hashes, lib_id_hash);
                                else
                                    dyn::Append(hashes, lib_id_hash);
                            });
            }
        }
    }

    {
        DynamicSet<String> library_authors {box_system.arena};
        for (auto const& lib : libraries) {
            if (library_filters.selected_library_hashes.size) {
                if (!Contains(library_filters.selected_library_hashes, lib.Hash())) continue;
            }
            library_authors.Insert(lib.author);
        }

        // Every library has an author, so if there is only one author it makes no sense to show it -
        // selecting it will do nothing.
        if (library_authors.table.size != 1) {
            auto const section = DoPickerItemsSectionContainer(box_system,
                                                               {
                                                                   .parent = parent,
                                                                   .heading = "LIBRARY AUTHORS"_s,
                                                                   .multiline_contents = true,
                                                               });
            for (auto const& author : library_authors.Elements()) {
                if (!author.active) continue;
                auto const is_selected =
                    Contains(library_filters.selected_library_author_hashes, author.hash);
                if (DoFilterButton(box_system, section, is_selected, {}, author.key).button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&hashes = library_filters.selected_library_author_hashes,
                                 is_selected,
                                 author_hash = author.hash]() {
                                    if (is_selected)
                                        dyn::RemoveValue(hashes, author_hash);
                                    else
                                        dyn::Append(hashes, author_hash);
                                });
                }
            }
        }
    }
}

static constexpr u64 HashTagCategory(TagCategory const& category) {
    auto const i = ToInt(category);
    return Hash(Span {&i, 1});
}

PUBLIC void
DoPickerTagsFilters(GuiBoxSystem& box_system, Box const& parent, TagsFilters const& tags_filters) {
    if (!tags_filters.tags.size) return;

    // TODO: use a hash table or something perhaps. We want to look up a tag+category by name.

    struct Category {
        TagCategory category;
        DynamicArray<TagType> tags;
    };

    DynamicArray<Category> standard_tags {box_system.arena};
    for (auto const category : EnumIterator<TagCategory>()) {
        auto const category_info = Tags(category);
        for (auto const tag : category_info.tags) {
            auto const tag_info = GetTagInfo(tag);
            if (tags_filters.tags.Contains(tag_info.name)) {
                if (auto const i =
                        FindIf(standard_tags, [&](Category const& c) { return c.category == category; })) {
                    auto& cat = standard_tags[*i];
                    dyn::Append(cat.tags, tag);
                } else {
                    dyn::Append(standard_tags,
                                Category {
                                    .category = category,
                                    .tags = {Span {&tag, 1}, box_system.arena},
                                });
                }
            }
        }
    }

    Sort(standard_tags,
         [](Category const& a, Category const& b) { return ToInt(a.category) < ToInt(b.category); });

    // IMPROVE: add a heading around all of this for TAGS, then these sections are sub-sections

    for (auto& category : standard_tags) {
        Sort(category.tags, [](TagType const& a, TagType const& b) { return ToInt(a) < ToInt(b); });
        auto const category_info = Tags(category.category);
        auto const section = DoPickerItemsSectionContainer(box_system,
                                                           {
                                                               .parent = parent,
                                                               .heading = category_info.name,
                                                               .icon = category_info.font_awesome_icon,
                                                               .heading_is_folder = true,
                                                               .multiline_contents = true,
                                                           });

        for (auto const tag : category.tags) {
            auto const tag_info = GetTagInfo(tag);
            auto const tag_hash = Hash(tag_info.name);
            auto const is_selected = Contains(tags_filters.selected_tags_hashes, tag_hash);
            if (DoFilterButton(box_system, section, is_selected, {}, tag_info.name).button_fired) {
                if (is_selected)
                    dyn::RemoveValue(tags_filters.selected_tags_hashes, tag_hash);
                else
                    dyn::Append(tags_filters.selected_tags_hashes, tag_hash);
            }
        }
    }
}

PUBLIC void DoPickerStatusBar(GuiBoxSystem& box_system,
                              FunctionRef<Optional<String>()> custom_status,
                              sample_lib_server::Server& server,
                              sample_lib::LibraryIdRef const* hovering_lib) {
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

    if (auto const lib_id = hovering_lib) {
        auto lib = sample_lib_server::FindLibraryRetained(server, *lib_id);
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

    sample_lib_server::Server& sample_library_server;

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
    DynamicArrayBounded<char, 100>* search {};

    TrivialFunctionRef<void()> on_load_previous {};
    TrivialFunctionRef<void()> on_load_next {};
    TrivialFunctionRef<void()> on_load_random {};
    TrivialFunctionRef<void()> on_scroll_to_show_selected {};

    Span<sample_lib::LibraryIdRef const> libraries;
    Optional<LibraryFilters> library_filters {};
    Optional<TagsFilters> tags_filters {};
    TrivialFunctionRef<void(GuiBoxSystem&, Box const& parent)> do_extra_filters {};
    TrivialFunctionRef<void()> on_clear_all_filters {};

    f32 status_bar_height {};
    TrivialFunctionRef<Optional<String>()> status {}; // Set if something is hovering
};

// Ephemeral
struct PickerPopupContext {
    sample_lib::LibraryIdRef const* hovering_lib {};
};

static void
DoPickerPopup(GuiBoxSystem& box_system, PickerPopupOptions const& options, PickerPopupContext& context) {
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

            if (options.on_clear_all_filters && (options.library_filters.AndThen([](LibraryFilters const& f) {
                    return f.selected_library_hashes.size;
                }) || options.tags_filters.AndThen([](TagsFilters const& f) {
                    return f.selected_tags_hashes.size;
                }))) {
                if (IconButton(box_system,
                               lhs_top,
                               ICON_FA_TIMES,
                               "Clear all filters",
                               style::k_font_heading2_size * 0.9f,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [clear = options.on_clear_all_filters]() { clear(); });
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
                         .text = ICON_FA_RANDOM,
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

                             if (options.library_filters)
                                 DoPickerLibraryFilters(box_system,
                                                        root,
                                                        options.libraries,
                                                        *options.library_filters,
                                                        context.hovering_lib);
                             if (options.tags_filters)
                                 DoPickerTagsFilters(box_system, root, *options.tags_filters);

                             if (options.do_extra_filters) options.do_extra_filters(box_system, root);
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

            if (auto const& search = options.search) {
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
                          .text = ICON_FA_SEARCH,
                          .font_size = k_picker_item_height * 0.9f,
                          .font = FontType::Icons,
                          .text_fill = style::Colour::Subtext0,
                          .size_from_text = true,
                      });

                if (auto const text_input =
                        DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = *search,
                                  .text_input_box = TextInputBox::SingleLine,
                                  .text_input_cursor = style::Colour::Text,
                                  .text_input_selection = style::Colour::Highlight,
                                  .layout {
                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                  },
                              });
                    text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&s = *search, new_text = text_input.text_input_result->text]() {
                                    dyn::AssignFitInCapacity(s, new_text);
                                });
                }

                if (search->size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_TIMES,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .font = FontType::Icons,
                                  .text_fill = style::Colour::Subtext0,
                                  .size_from_text = true,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .activate_on_click_button = MouseButton::Left,
                                  .activation_click_event = ActivationClickEvent::Up,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions, [&s = *search]() { dyn::Clear(s); });
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

    AddPanel(box_system,
             {
                 .run =
                     [&](GuiBoxSystem& box_system) {
                         DoPickerStatusBar(box_system,
                                           options.status,
                                           options.sample_library_server,
                                           context.hovering_lib);
                     },
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
                          imgui::Id popup_id,
                          Rect absolute_button_rect,
                          PickerPopupOptions const& options) {
    PickerPopupContext context {};
    RunPanel(box_system,
             Panel {
                 .run = [&](GuiBoxSystem& box_system) { DoPickerPopup(box_system, options, context); },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = absolute_button_rect,
                         .popup_imgui_id = popup_id,
                     },
             });
}
