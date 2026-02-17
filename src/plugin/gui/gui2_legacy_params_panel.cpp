// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_legacy_params_panel.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui_state.hpp"

static void DrawDarkModeModalBackground(imgui::Context const& imgui) {
    imgui.draw_list->PushClipRectFullScreen();
    imgui.draw_list->AddRectFilled(0, GuiIo().in.window_size.ToFloat2(), 0x6c0f0d0d);
    imgui.draw_list->PopClipRect();

    auto const rounding = GuiIo().WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background1, .dark_mode = true}), rounding);
}

static void LegacyParamsPanel(GuiBuilder& builder, GuiState& g) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Header
    {
        auto const title_container = DoBox(builder,
                                           {
                                               .parent = root,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding = {.lrtb = k_default_spacing},
                                                   .contents_gap = k_default_spacing * 1.2f,
                                                   .contents_direction = layout::Direction::Row,
                                                   .contents_align = layout::Alignment::Justify,
                                               },
                                           });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = "Legacy Parameters",
                  .font = FontType::Heading1,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, k_font_heading1_size},
                  },
              });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = ICON_FA_XMARK,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .button_behaviour =
                      imgui::ButtonConfig {
                          .closes_popup_or_modal = true,
                      },
                  .extra_margin_for_mouse_events = 8,
              });
    }

    // Divider
    {
        auto const one_pixel = GuiIo().PixelsToWw(1.0f);
        DoBox(builder,
              {
                  .parent = root,
                  .background_fill_colours = Col {.c = Col::Surface0, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, one_pixel},
                  },
              });
    }

    // Parameter grid
    auto const grid = DoBox(builder,
                            {
                                .parent = root,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = {8, 0},
                                    .contents_direction = layout::Direction::Row,
                                    .contents_multiline = true,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    for (auto const& desc : k_param_descriptors) {
        if (!desc.flags.hidden) continue;

        builder.imgui.PushId(ToInt(desc.index));
        DEFER { builder.imgui.PopId(); };

        auto const container = DoBox(builder,
                                     {
                                         .parent = grid,
                                         .border_colours = Col {.c = Col::Overlay0, .dark_mode = true},
                                         .round_background_corners = 0b1111,
                                         .layout {
                                             .size = layout::k_hug_contents,
                                             .contents_padding = {.lrtb = 5},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                         },
                                     });

        switch (desc.value_type) {
            case ParamValueType::Float: {
                DoKnobParameter(g,
                                container,
                                g.engine.processor.main_params.DescribedValue(desc.index),
                                {
                                    .knob_highlight_col = {.c = Col::Highlight},
                                    .knob_line_col = {.c = Col::Background0, .dark_mode = true},
                                });
                break;
            }
            case ParamValueType::Menu: {
                DoMenuParameter(g,
                                container,
                                g.engine.processor.main_params.DescribedValue(desc.index),
                                {.width = 120});
                break;
            }
            case ParamValueType::Bool: {
                DoButtonParameter(g, container, g.engine.processor.main_params.DescribedValue(desc.index));
                break;
            }
            case ParamValueType::Int: {
                // TODO: add int parameter component
                break;
            }
        }

        // Module label
        DoBox(builder,
              {
                  .parent = container,
                  .text = desc.ModuleString(),
                  .size_from_text = true,
                  .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
                  .text_justification = TextJustification::Centred,
              });
    }
}

void DoLegacyParamsPanel(GuiBuilder& builder, GuiState& g) {
    if (!builder.imgui.IsModalOpen(k_legacy_params_panel_id)) return;

    auto viewport_config = k_default_modal_viewport;
    viewport_config.draw_background = DrawDarkModeModalBackground;

    auto const window_size = GuiIo().in.window_size.ToFloat2();
    auto const panel_size = GuiIo().WwToPixels(f32x2 {500, 400});
    auto const bounds = Rect {.pos = 0, .size = window_size}.CentredRect(panel_size);

    DoBoxViewport(builder,
                  {
                      .run = [&g](GuiBuilder& b) { LegacyParamsPanel(b, g); },
                      .bounds = bounds,
                      .imgui_id = k_legacy_params_panel_id,
                      .viewport_config = viewport_config,
                  });
}
