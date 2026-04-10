// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer_common.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_builder.hpp"

void DoInstSelectorRightClickMenu(GuiState& g, Box selector_button, u8 layer_index) {
    auto const& layer_obj = g.engine.Layer(layer_index);
    auto const right_click_id = SourceLocationHash() + layer_index;

    if (auto const r = BoxRect(g.builder, selector_button)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        if (g.imgui.ButtonBehaviour(window_r,
                                    selector_button.imgui_id,
                                    {
                                        .mouse_button = MouseButton::Right,
                                        .event = MouseButtonEvent::Up,
                                    })) {
            g.imgui.OpenPopupMenu(right_click_id, selector_button.imgui_id);
        }

        if (g.imgui.IsPopupMenuOpen(right_click_id))
            DoBoxViewport(
                g.builder,
                {
                    .run =
                        [&](GuiBuilder&) {
                            auto const root = DoBox(g.builder,
                                                    {
                                                        .layout {
                                                            .size = layout::k_hug_contents,
                                                            .contents_direction = layout::Direction::Column,
                                                            .contents_align = layout::Alignment::Start,
                                                        },
                                                    });
                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Unload instrument"_s,
                                             .mode = layer_obj.instrument_id.tag == InstrumentType::None
                                                         ? MenuItemOptions::Mode::Disabled
                                                         : MenuItemOptions::Mode::Active,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                LoadInstrument(g.engine, layer_index, InstrumentType::None);
                            }
                        },
                    .bounds = window_r,
                    .imgui_id = right_click_id,
                    .viewport_config = k_default_popup_menu_viewport,
                });
    }
}
