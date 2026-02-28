// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_popup_menu.hpp"

#include "gui/elements/gui_constants.hpp"

Box MenuOpenButton(GuiBuilder& builder, Box parent, MenuOpenButtonOptions const& options, u64 id_extra) {
    auto const button =
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .background_fill_colours = Col {.c = Col::Background2},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {options.width, layout::k_hug_contents},
                      .contents_padding = {.lr = k_button_padding_x, .tb = k_button_padding_y},
                      .contents_gap = k_menu_item_padding_x,
                      .contents_align = layout::Alignment::Justify,
                  },
                  .tooltip = options.tooltip,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    DoBox(builder,
          {
              .parent = button,
              .text = options.text,
              .size_from_text = true,
              .font = FontType::Body,
          });

    DoBox(builder,
          {
              .parent = button,
              .text = ICON_FA_CARET_DOWN,
              .size_from_text = true,
              .font = FontType::Icons,
          });

    return button;
}

Box MenuItem(GuiBuilder& builder, Box parent, MenuItemOptions const& options, u64 id_extra) {
    bool const disabled = options.mode == MenuItemOptions::Mode::Disabled;
    auto const item =
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .background_fill_auto_hot_active_overlay = !disabled,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_direction = layout::Direction::Row,
                  },
                  .tooltip = options.tooltip,
                  .button_behaviour = disabled ? Optional<imgui::ButtonConfig> {}
                                               : Optional<imgui::ButtonConfig> {imgui::ButtonConfig {}},
              });

    if (item.button_fired && options.close_on_click) builder.imgui.CloseTopPopupOnly();

    if (!options.no_icon_gap)
        DoBox(builder,
              {
                  .parent = item,
                  .text = options.is_selected ? String(ICON_FA_CHECK) : "",
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0},
                  .layout {
                      .size = k_icon_button_size,
                      .margins {.l = k_menu_item_padding_x},
                  },
              });

    auto const text_container =
        DoBox(builder,
              {
                  .parent = item,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = k_menu_item_padding_x, .tb = k_menu_item_padding_y},
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                  },
              });
    DoBox(builder,
          {
              .parent = text_container,
              .text = options.text,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours =
                  Col {.c = options.mode != MenuItemOptions::Mode::Active ? Col::Overlay1 : Col::Text},
          });
    if (options.subtext && options.subtext->size) {
        DoBox(builder,
              {
                  .parent = text_container,
                  .text = *options.subtext,
                  .size_from_text = true,
                  .text_colours = Col {.c = Col::Subtext0},
              });
    }

    return item;
}
