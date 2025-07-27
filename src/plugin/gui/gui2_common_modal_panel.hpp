// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_box_system.hpp"

// Creates the root container for a panel
PUBLIC Box DoModalRootBox(GuiBoxSystem& box_system) {
    return DoBox(box_system,
                 {
                     .layout {
                         .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

// Configuration structs for panel components
struct ModalHeaderConfig {
    Box parent;
    String title;
    TrivialFunctionRef<void()> on_close;
    bool* modeless {};
};

// Creates a standard panel header with title and close button
PUBLIC Box DoModalHeader(GuiBoxSystem& box_system, ModalHeaderConfig const& config) {
    ASSERT(config.title.size);
    auto const title_container = DoBox(box_system,
                                       {
                                           .parent = config.parent,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lrtb = style::k_spacing},
                                               .contents_gap = style::k_spacing * 1.2f,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Justify,
                                           },
                                       });

    DoBox(box_system,
          {.parent = title_container,
           .text = config.title,
           .font = FontType::Heading1,
           .layout {
               .size = {layout::k_fill_parent, style::k_font_heading1_size},
           }});

    if (config.modeless) {
        if (DoBox(box_system,
                  {
                      .parent = title_container,
                      .text = *config.modeless ? ICON_FA_UNLOCK : ICON_FA_LOCK,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1111,
                      .behaviour = Behaviour::Button,
                      .extra_margin_for_mouse_events = 8,
                  })
                .button_fired) {
            *config.modeless = !*config.modeless;
        }
    }

    if (auto const close = DoBox(box_system,
                                 {
                                     .parent = title_container,
                                     .text = ICON_FA_XMARK,
                                     .size_from_text = true,
                                     .font = FontType::Icons,
                                     .background_fill_auto_hot_active_overlay = true,
                                     .round_background_corners = 0b1111,
                                     .behaviour = Behaviour::Button,
                                     .extra_margin_for_mouse_events = 8,
                                 });
        close.button_fired) {
        config.on_close();
    }

    return title_container;
}

enum class DividerType { Horizontal, Vertical };
PUBLIC Box DoModalDivider(GuiBoxSystem& box_system, Box parent, DividerType type) {
    auto const one_pixel = box_system.imgui.PixelsToVw(1);
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .background_fill_colours = {style::Colour::Surface2},
                     .layout {
                         .size = type == DividerType::Horizontal ? f32x2 {layout::k_fill_parent, one_pixel}
                                                                 : f32x2 {one_pixel, layout::k_fill_parent},
                     },
                 });
}

struct ModalTabConfig {
    Optional<String> icon;
    String text;
    u32 index;
};

struct ModalTabBarConfig {
    Box parent;
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// Creates a tab bar with configurable tabs
PUBLIC Box DoModalTabBar(GuiBoxSystem& box_system, ModalTabBarConfig const& config) {
    constexpr auto k_tab_border = 4;
    auto const tab_container = DoBox(box_system,
                                     {
                                         .parent = config.parent,
                                         .background_fill_colours = {style::Colour::Background1},
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

        auto const tab_box = DoBox(
            box_system,
            {
                .parent = tab_container,
                .background_fill_colours = {is_current ? style::Colour::Background0 : style::Colour::None},
                .background_fill_auto_hot_active_overlay = true,
                .round_background_corners = 0b1100,
                .layout {
                    .size = layout::k_hug_contents,
                    .contents_padding = {.lr = style::k_spacing, .tb = 4},
                    .contents_gap = 5,
                    .contents_direction = layout::Direction::Row,
                },
                .behaviour = is_current ? Behaviour::None : Behaviour::Button,
            });

        if (tab_box.button_fired)
            dyn::Append(
                box_system.state->deferred_actions,
                [&current_tab = config.current_tab_index, index = tab.index]() { current_tab = index; });

        if (tab.icon) {
            DoBox(box_system,
                  BoxConfig {
                      .parent = tab_box,
                      .text = *tab.icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .text_colours = Splat(is_current ? style::Colour::Subtext0 : style::Colour::Surface2),
                  });
        }

        DoBox(box_system,
              {
                  .parent = tab_box,
                  .text = tab.text,
                  .size_from_text = true,
                  .text_colours = Splat(is_current ? style::Colour::Text : style::Colour::Subtext0),
              });
    }

    return tab_container;
}

struct ModalConfig {
    String title;
    TrivialFunctionRef<void()> on_close;
    bool* modeless {};
    Span<ModalTabConfig const> tabs;
    u32& current_tab_index;
};

// High-level function that creates a complete modal layout within an already open modal window.
PUBLIC Box DoModal(GuiBoxSystem& box_system, ModalConfig const& config) {
    auto const root = DoModalRootBox(box_system);

    DoModalHeader(box_system,
                  {
                      .parent = root,
                      .title = config.title,
                      .on_close = config.on_close,
                      .modeless = config.modeless,
                  });

    DoModalTabBar(box_system,
                  {
                      .parent = root,
                      .tabs = config.tabs,
                      .current_tab_index = config.current_tab_index,
                  });

    return root;
}

PUBLIC bool CheckboxButton(GuiBoxSystem& box_system,
                           Box parent,
                           String text,
                           bool state,
                           TooltipString tooltip = k_nullopt) {
    auto const button = DoBox(box_system,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {layout::k_hug_contents, layout::k_hug_contents},
                                      .contents_gap = style::k_prefs_medium_gap,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::Start,
                                  },
                                  .tooltip = tooltip,
                                  .behaviour = Behaviour::Button,
                              });

    DoBox(box_system,
          {
              .parent = button,
              .text = state ? ICON_FA_CHECK : ""_s,
              .font = FontType::Icons,
              .font_size = style::k_font_icons_size * 0.7f,
              .text_colours = Splat(style::Colour::Text),
              .text_align_x = TextAlignX::Centre,
              .text_align_y = TextAlignY::Centre,
              .background_fill_colours = {style::Colour::Background2},
              .background_fill_auto_hot_active_overlay = true,
              .border_colours = {style::Colour::Overlay0},
              .border_auto_hot_active_overlay = true,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
              .layout {
                  .size = style::k_prefs_icon_button_size,
              },
          });
    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .size_from_text = true,
          });

    return button.button_fired;
}

struct TextButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    bool fill_x = false;
    bool disabled = false;
};

PUBLIC bool TextButton(GuiBoxSystem& builder, Box parent, TextButtonOptions const& options) {
    auto const button =
        DoBox(builder,
              {
                  .parent = parent,
                  .background_fill_colours = Splat(style::Colour::Background2),
                  .background_fill_auto_hot_active_overlay = !options.disabled,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {options.fill_x ? layout::k_fill_parent : layout::k_hug_contents,
                               layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                  },
                  .tooltip = options.disabled ? k_nullopt : options.tooltip,
                  .behaviour = options.disabled ? Behaviour::None : Behaviour::Button,
              });

    auto const text_col = options.disabled ? style::Colour::Surface1 : style::Colour::Text;
    DoBox(builder,
          {
              .parent = button,
              .text = options.text,
              .size_from_text = !options.fill_x,
              .font = FontType::Body,
              .text_colours = Splat(text_col),
              .text_align_x = TextAlignX::Centre,
              .text_align_y = TextAlignY::Centre,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {layout::k_fill_parent, style::k_font_body_size},
              },
          });

    return button.button_fired;
}

PUBLIC Box
IconButton(GuiBoxSystem& builder, Box parent, String icon, String tooltip, f32 font_size, f32x2 size) {
    auto const button = DoBox(builder,
                              {
                                  .parent = parent,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .round_background_corners = 0b1111,
                                  .layout {
                                      .size = size,
                                      .contents_align = layout::Alignment::Middle,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                                  .tooltip = tooltip,
                                  .behaviour = Behaviour::Button,
                              });

    DoBox(builder,
          {
              .parent = button,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = font_size,
              .text_colours = Splat(style::Colour::Subtext0),
          });

    return button;
}

struct TextInputOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32x2 size;
    bool border = true;
    bool background = true;
    bool multiline = false;
};

PUBLIC Box TextInput(GuiBoxSystem& builder, Box parent, TextInputOptions const& options) {
    auto const box = DoBox(builder,
                           {
                               .parent = parent,
                               .text = options.text,
                               .font = FontType::Body,
                               .text_colours = Splat(style::Colour::Text),
                               .background_fill_colours = Splat(
                                   options.background ? style::Colour::Background2 : style::Colour::None),
                               .border_colours {
                                   .base = options.border ? style::Colour::Overlay0 : style::Colour::None,
                                   .hot = options.border ? style::Colour::Overlay1 : style::Colour::None,
                                   .active = options.border ? style::Colour::Blue : style::Colour::None,
                               },
                               .round_background_corners = 0b1111,
                               .layout {.size = options.size},
                               .tooltip = options.tooltip,
                               .behaviour = Behaviour::TextInput,
                               .multiline_text_input = options.multiline,
                           });
    DrawTextInput(builder,
                  box,
                  {
                      .text_col = style::Colour::Text,
                      .cursor_col = style::Colour::Text,
                      .selection_col = style::Colour::Highlight,
                  });

    return box;
}

struct IntFieldOptions {
    String label;
    TooltipString tooltip = k_nullopt;
    f32 width;
    s64 value;
    FunctionRef<s64(s64 value)> constrainer;
};

PUBLIC Optional<s64> IntField(GuiBoxSystem& builder, Box parent, IntFieldOptions const& options) {
    auto value = options.value;
    auto const initial_value = value;
    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = style::k_prefs_medium_gap,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    auto const item_container = DoBox(builder,
                                      {
                                          .parent = container,
                                          .background_fill_colours = {style::Colour::Background2},
                                          .border_colours = {style::Colour::Overlay0},
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
        if (text_input.text_input_result && text_input.text_input_result->buffer_changed) {
            auto const new_value = ParseInt(text_input.text_input_result->text, ParseIntBase::Decimal);
            if (new_value.HasValue()) value = options.constrainer(new_value.Value());
        }
    }

    auto const k_button_width = 13.0f;

    if (DoBox(builder,
              {
                  .parent = item_container,
                  .text = ICON_FA_CARET_LEFT,
                  .font = FontType::Icons,
                  .text_align_x = TextAlignX::Centre,
                  .text_align_y = TextAlignY::Centre,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1001,
                  .layout {
                      .size = {k_button_width, layout::k_fill_parent},
                  },
                  .tooltip = "Decrease value"_s,
                  .behaviour = Behaviour::Button,
              })
            .button_fired) {
        value = options.constrainer(value - 1);
    }

    if (DoBox(builder,
              {
                  .parent = item_container,
                  .text = ICON_FA_CARET_RIGHT,
                  .font = FontType::Icons,
                  .text_align_x = TextAlignX::Centre,
                  .text_align_y = TextAlignY::Centre,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b0110,
                  .layout {
                      .size = {k_button_width, layout::k_fill_parent},
                  },
                  .tooltip = "Increase value"_s,
                  .behaviour = Behaviour::Button,
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

struct MenuButtonOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    f32 width = layout::k_hug_contents;
};

PUBLIC Box MenuButton(GuiBoxSystem& box_system, Box parent, MenuButtonOptions const& options) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill_colours = {style::Colour::Background2},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {options.width, layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                      .contents_gap = style::k_menu_item_padding_x,
                      .contents_align = layout::Alignment::Justify,
                  },
                  .tooltip = options.tooltip,
                  .behaviour = Behaviour::Button,
              });

    DoBox(box_system,
          {
              .parent = button,
              .text = options.text,
              .size_from_text = true,
              .font = FontType::Body,
          });

    DoBox(box_system,
          {
              .parent = button,
              .text = ICON_FA_CARET_DOWN,
              .size_from_text = true,
              .font = FontType::Icons,
          });

    return button;
}

struct MenuItemOptions {
    String text;
    TooltipString tooltip = k_nullopt;
    Optional<String> subtext;
    bool is_selected;
    bool close_on_click = true;
};

PUBLIC bool MenuItem(GuiBoxSystem& box_system, Box parent, MenuItemOptions const& options) {
    auto const item = DoBox(box_system,
                            {
                                .parent = parent,
                                .background_fill_auto_hot_active_overlay = true,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_direction = layout::Direction::Row,
                                },
                                .tooltip = options.tooltip,
                                .behaviour = Behaviour::Button,
                            });

    if (item.button_fired && options.close_on_click) box_system.imgui.CloseTopPopupOnly();

    DoBox(box_system,
          {
              .parent = item,
              .text = options.is_selected ? String(ICON_FA_CHECK) : "",
              .font = FontType::Icons,
              .text_colours = Splat(style::Colour::Subtext0),
              .layout {
                  .size = style::k_prefs_icon_button_size,
                  .margins {.l = style::k_menu_item_padding_x},
              },
          });

    auto const text_container = DoBox(
        box_system,
        {
            .parent = item,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lr = style::k_menu_item_padding_x, .tb = style::k_menu_item_padding_y},
                .contents_direction = layout::Direction::Column,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
            },
        });
    DoBox(box_system,
          {
              .parent = text_container,
              .text = options.text,
              .size_from_text = true,
              .font = FontType::Body,
          });
    if (options.subtext && options.subtext->size) {
        DoBox(box_system,
              {
                  .parent = text_container,
                  .text = *options.subtext,
                  .size_from_text = true,
                  .text_colours = Splat(style::Colour::Subtext0),
              });
    }

    return item.button_fired;
}
