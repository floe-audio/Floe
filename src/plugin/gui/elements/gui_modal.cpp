// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_modal.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"

Box DoModalRootBox(GuiBuilder& builder) {
    return DoBox(builder,
                 {
                     .layout {
                         .size = layout::k_fill_parent,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

// Creates a standard panel header with title and close button
Box DoModalHeader(GuiBuilder& builder, ModalHeaderConfig const& config) {
    ASSERT(config.title.size);
    auto const title_container = DoBox(builder,
                                       {
                                           .parent = config.parent,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lrtb = k_default_spacing},
                                               .contents_gap = k_default_spacing * 1.2f,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Justify,
                                           },
                                       });

    DoBox(builder,
          {.parent = title_container,
           .text = config.title,
           .font = FontType::Heading1,
           .layout {
               .size = {layout::k_fill_parent, k_font_heading1_size},
           }});

    if (config.modeless) {
        if (DoBox(builder,
                  {
                      .parent = title_container,
                      .text = *config.modeless ? ICON_FA_UNLOCK : ICON_FA_LOCK,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1111,
                      .button_behaviour = imgui::ButtonConfig {},
                      .extra_margin_for_mouse_events = 8,
                  })
                .button_fired) {
            if (!config.modeless) builder.imgui.CloseTopModal();
            *config.modeless = !*config.modeless;
        }
    }

    DoBox(builder,
          {
              .parent = title_container,
              .text = ICON_FA_XMARK,
              .size_from_text = true,
              .font = FontType::Icons,
              .background_fill_auto_hot_active_overlay = true,
              .round_background_corners = 0b1111,
              .button_behaviour =
                  imgui::ButtonConfig {
                      .closes_popup_or_modal = true,
                  },
              .extra_margin_for_mouse_events = 8,
          });

    return title_container;
}

Box DoModalDivider(GuiBuilder& builder, Box parent, DividerOptions options, u64 id_extra) {
    auto const one_pixel = PixelsToWw(1.0f);
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
                     .background_fill_colours = Col {.c = !options.subtle ? Col::Surface2 : Col::Surface0},
                     .layout {
                         .size = options.horizontal ? f32x2 {layout::k_fill_parent, one_pixel}
                                                    : f32x2 {one_pixel, layout::k_fill_parent},
                         .margins = {.lr = options.vertical ? options.margin : 0,
                                     .tb = options.horizontal ? options.margin : 0},
                     },
                 });
}

// Creates a tab bar with configurable tabs
Box DoModalTabBar(GuiBuilder& builder, ModalTabBarConfig const& config) {
    constexpr auto k_tab_border = 4;
    auto const tab_container = DoBox(builder,
                                     {
                                         .parent = config.parent,
                                         .background_fill_colours = Col {.c = Col::Background1},
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_padding = {.lr = k_tab_border, .t = k_tab_border},
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });

    for (auto const tab : config.tabs) {
        bool const is_current = tab.index == config.current_tab_index;

        auto const tab_box =
            DoBox(builder,
                  {
                      .parent = tab_container,
                      .id_extra = tab.index,
                      .background_fill_colours = Col {.c = is_current ? Col::Background0 : Col::None},
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1100,
                      .layout {
                          .size = layout::k_hug_contents,
                          .contents_padding = {.lr = k_default_spacing, .tb = 4},
                          .contents_gap = 5,
                          .contents_direction = layout::Direction::Row,
                      },
                      .button_behaviour =
                          is_current ? k_nullopt : Optional<imgui::ButtonConfig>(imgui::ButtonConfig {}),
                  });

        if (tab_box.button_fired) config.current_tab_index = tab.index;

        if (tab.icon) {
            DoBox(builder,
                  BoxConfig {
                      .parent = tab_box,
                      .text = *tab.icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .text_colours = Col {.c = is_current ? Col::Subtext0 : Col::Surface2},
                  });
        }

        DoBox(builder,
              {
                  .parent = tab_box,
                  .text = tab.text,
                  .size_from_text = true,
                  .text_colours = Col {.c = is_current ? Col::Text : Col::Subtext0},
              });
    }

    return tab_container;
}

// High-level function that creates a complete modal layout within an already open modal window.
Box DoModal(GuiBuilder& builder, ModalConfig const& config) {
    auto const root = DoModalRootBox(builder);

    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = config.title,
                      .modeless = config.modeless,
                  });

    DoModalTabBar(builder,
                  {
                      .parent = root,
                      .tabs = config.tabs,
                      .current_tab_index = config.current_tab_index,
                  });

    return root;
}

bool CheckboxButton(GuiBuilder& builder,
                    Box parent,
                    String text,
                    bool state,
                    TooltipString tooltip,
                    u64 id_extra) {
    auto const button = DoBox(builder,
                              {
                                  .parent = parent,
                                  .id_extra = id_extra,
                                  .layout {
                                      .size = {layout::k_hug_contents, layout::k_hug_contents},
                                      .contents_gap = k_medium_gap,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::Start,
                                  },
                                  .tooltip = tooltip,
                                  .button_behaviour = imgui::ButtonConfig {},
                              });

    DoBox(builder,
          {
              .parent = button,
              .text = state ? ICON_FA_CHECK : ""_s,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.7f,
              .text_colours = Col {Col::Text},
              .text_justification = TextJustification::Centred,
              .background_fill_colours = Col {Col::Background2},
              .background_fill_auto_hot_active_overlay = true,
              .border_colours = Col {Col::Overlay0},
              .border_auto_hot_active_overlay = true,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
              .layout {
                  .size = k_icon_button_size,
              },
          });
    DoBox(builder,
          {
              .parent = button,
              .text = text,
              .size_from_text = true,
          });

    return button.button_fired;
}

bool TextButton(GuiBuilder& builder, Box parent, TextButtonOptions const& options, u64 id_extra) {
    auto const button =
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .background_fill_colours = Col {.c = Col::Background2},
                  .background_fill_auto_hot_active_overlay = !options.disabled,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {options.fill_x ? layout::k_fill_parent : layout::k_hug_contents,
                               layout::k_hug_contents},
                      .contents_padding = {.lr = k_button_padding_x, .tb = k_button_padding_y},
                  },
                  .tooltip = options.disabled ? k_nullopt : options.tooltip,
                  .button_behaviour =
                      options.disabled ? k_nullopt : Optional<imgui::ButtonConfig>(imgui::ButtonConfig {}),
              });

    DoBox(builder,
          {
              .parent = button,
              .text = options.text,
              .size_from_text = !options.fill_x,
              .font = FontType::Body,
              .text_colours = Col {.c = options.disabled ? Col::Surface1 : Col::Text},
              .text_justification = TextJustification::Centred,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {layout::k_fill_parent, k_font_body_size},
              },
          });

    return button.button_fired;
}

Box IconButton(GuiBuilder& builder,
               Box parent,
               String icon,
               String tooltip,
               f32 font_size,
               f32x2 size,
               u64 id_extra) {
    auto const button = DoBox(builder,
                              {
                                  .parent = parent,
                                  .id_extra = id_extra,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .round_background_corners = 0b1111,
                                  .layout {
                                      .size = size,
                                      .contents_align = layout::Alignment::Middle,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                                  .tooltip = tooltip,
                                  .button_behaviour = imgui::ButtonConfig {},
                              });

    DoBox(builder,
          {
              .parent = button,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = font_size,
              .text_colours = Col {.c = Col::Subtext0},
          });

    return button;
}

TextInputResult TextInput(GuiBuilder& builder, Box parent, TextInputOptions const& options, u64 id_extra) {
    auto const box =
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .background_fill_colours = Col {.c = options.background ? Col::Background2 : Col::None},
                  .border_colours =
                      ColSet {
                          .base = {.c = options.border ? Col::Overlay0 : Col::None},
                          .hot = {.c = options.border ? Col::Overlay1 : Col::None},
                          .active = {.c = options.border ? Col::Blue : Col::None},
                      },
                  .round_background_corners = 0b1111,
                  .layout {.size = options.size},
                  .tooltip = options.tooltip,
              });

    Optional<imgui::TextInputResult> result {};

    if (auto const r = BoxRect(builder, box)) {
        auto const window_r = builder.imgui.RegisterAndConvertRect(*r);
        result = builder.imgui.TextInputBehaviour({
            .rect_in_window_coords = window_r,
            .id = box.imgui_id,
            .text = options.text,
            .input_cfg =
                {
                    .x_padding = WwToPixels(4.0f),
                    .centre_align = false,
                    .escape_unfocuses = true,
                    .select_all_when_opening = false,
                    .multiline = options.multiline,
                    .multiline_wordwrap_hack = options.multiline,
                },
            .button_cfg =
                {
                    .mouse_button = MouseButton::Left,
                    .event = MouseButtonEvent::Up,
                },
        });

        DrawTextInput(builder.imgui,
                      *result,
                      {
                          .text_col = {Col::Text},
                          .cursor_col = {Col::Text},
                          .selection_col = {.c = Col::Highlight, .alpha = 128},
                      });
    }

    return {.box = box, .result = result};
}

Optional<s64> IntField(GuiBuilder& builder, Box parent, IntFieldOptions const& options, u64 id_extra) {
    auto value = options.value;
    auto const initial_value = value;
    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .id_extra = id_extra,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = k_medium_gap,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    auto const item_container = DoBox(builder,
                                      {
                                          .parent = container,
                                          .background_fill_colours = Col {.c = Col::Background2},
                                          .border_colours = Col {.c = Col::Overlay0},
                                          .round_background_corners = 0b1111,
                                          .layout {
                                              .size = layout::k_hug_contents,
                                          },
                                      });

    {
        auto const text = fmt::IntToString(value);
        auto const text_input = TextInput(builder,
                                          item_container,
                                          {
                                              .text = text,
                                              .tooltip = "Enter a new value"_s,
                                              .size = f32x2 {options.width, 20},
                                              .border = false,
                                              .background = false,
                                          });
        if (text_input.result && text_input.result->buffer_changed) {
            auto const new_value = ParseInt(text_input.result->text, ParseIntBase::Decimal);
            if (new_value.HasValue()) value = options.constrainer(new_value.Value());
        }
    }

    auto const k_button_width = 13.0f;

    if (DoBox(builder,
              {
                  .parent = item_container,
                  .text = ICON_FA_CARET_LEFT,
                  .font = FontType::Icons,
                  .text_justification = TextJustification::Centred,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1001,
                  .layout {
                      .size = {k_button_width, layout::k_fill_parent},
                  },
                  .tooltip = "Decrease value"_s,
                  .button_behaviour = imgui::ButtonConfig {},
              })
            .button_fired) {
        value = options.constrainer(value - 1);
    }

    if (DoBox(builder,
              {
                  .parent = item_container,
                  .text = ICON_FA_CARET_RIGHT,
                  .font = FontType::Icons,
                  .text_justification = TextJustification::Centred,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b0110,
                  .layout {
                      .size = {k_button_width, layout::k_fill_parent},
                  },
                  .tooltip = "Increase value"_s,
                  .button_behaviour = imgui::ButtonConfig {},
              })
            .button_fired) {
        value = options.constrainer(value + 1);
    }

    // label
    DoBox(builder,
          {
              .parent = container,
              .text = options.label,
              .size_from_text = true,
              .tooltip = options.tooltip,
          });

    if (value != initial_value) return value;
    return k_nullopt;
}
