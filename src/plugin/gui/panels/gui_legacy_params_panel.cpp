// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_legacy_params_panel.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "processor/processor.hpp"

static void DrawDarkModeModalBackground(imgui::Context const& imgui) {
    imgui.draw_list->PushClipRectFullScreen();
    imgui.draw_list->AddRectFilled(0, GuiIo().in.window_size.ToFloat2(), 0x6c0f0d0d);
    imgui.draw_list->PopClipRect();

    auto const rounding = WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background1, .dark_mode = true}), rounding);
}

static void LegacyParamRow(GuiBuilder& builder, GuiState& g, ParamDescriptor const& desc, Box parent) {
    builder.imgui.PushId(ToInt(desc.index));
    DEFER { builder.imgui.PopId(); };

    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .border_colours = Col {.c = Col::Overlay0, .dark_mode = true},
                               .round_background_corners = 0b1111,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_padding = {.lr = 8, .tb = 6},
                                   .contents_gap = 8,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    auto const text_column = DoBox(builder,
                                   {
                                       .parent = row,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 1,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

    DoBox(builder,
          {
              .parent = text_column,
              .text = desc.ModuleString(" › "),
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
          });

    DoBox(builder,
          {
              .parent = text_column,
              .text = desc.name,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Text, .dark_mode = true},
          });

    DoBox(builder,
          {
              .parent = row,
              .layout {.size = {layout::k_fill_parent, 0}},
          });

    auto const right_group = DoBox(builder,
                                   {
                                       .parent = row,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 12,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    auto const dpv = g.engine.processor.main_params.DescribedValue(desc.index);
    switch (desc.value_type) {
        case ParamValueType::Float: {
            DoKnobParameter(g,
                            right_group,
                            dpv,
                            {
                                .width = 29,
                                .knob_highlight_col = {.c = Col::Highlight},
                                .knob_line_col = {.c = Col::Background0, .dark_mode = true},
                                .label = false,
                            });
            break;
        }
        case ParamValueType::Menu: {
            DoMenuParameter(g, right_group, dpv, {.width = 140, .label = false});
            break;
        }
        case ParamValueType::Bool: {
            DoButtonParameter(g, right_group, dpv, {.override_label = " "_s});
            break;
        }
        case ParamValueType::Int: {
            // TODO: add int parameter component
            break;
        }
    }

    auto const reset_btn =
        DoBox(builder,
              {
                  .parent = right_group,
                  .text = ICON_FA_ROTATE_LEFT,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .tooltip = "Disable this legacy override and restore the modern parameter"_s,
                  .button_behaviour = imgui::ButtonConfig {},
                  .extra_margin_for_mouse_events = 4,
              });

    if (reset_btn.button_fired)
        SetParameterValue(g.engine.processor, desc.index, desc.default_linear_value, {});
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
        auto const one_pixel = PixelsToWw(1.0f);
        DoBox(builder,
              {
                  .parent = root,
                  .background_fill_colours = Col {.c = Col::Surface0, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, one_pixel},
                  },
              });
    }

    DoBox(
        builder,
        {
            .parent = root,
            .text =
                "Legacy parameters are superseded by the main GUI but kept for backwards compatibility with existing DAW automation. Only parameters that are currently overriding their modern equivalents are shown. Reset a parameter to disable its override. Learn more on the documentation website.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
            .font = FontType::Body,
            .text_colours = Col {.c = Col::White, .alpha = 120},
            .layout =
                {
                    .margins = {.t = 5, .lr = k_default_spacing},
                },
        });

    auto const list = DoBox(builder,
                            {
                                .parent = root,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = 6,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    bool any_overriding = false;
    for (auto const& desc : k_param_descriptors) {
        if (!desc.flags.legacy) continue;
        auto const linear_value = g.engine.processor.main_params.LinearValue(desc.index);
        if (!IsLegacyParamOverridingModern(desc, linear_value)) continue;

        any_overriding = true;
        LegacyParamRow(builder, g, desc, list);
    }

    if (!any_overriding) {
        DoBox(builder,
              {
                  .parent = list,
                  .text = "No legacy parameters are currently overriding modern parameters."_s,
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
              });
    }
}

void DoLegacyParamsPanel(GuiBuilder& builder, GuiState& g) {
    if (!builder.imgui.IsModalOpen(k_legacy_params_panel_id)) return;

    auto viewport_config = k_default_modal_viewport;
    viewport_config.draw_background = DrawDarkModeModalBackground;

    auto const window_size = GuiIo().in.window_size.ToFloat2();
    auto const panel_size = WwToPixels(f32x2 {520, 480});
    auto const bounds = Rect {.pos = 0, .size = window_size}.CentredRect(panel_size);

    DoBoxViewport(builder,
                  {
                      .run = [&g](GuiBuilder& b) { LegacyParamsPanel(b, g); },
                      .bounds = bounds,
                      .imgui_id = k_legacy_params_panel_id,
                      .viewport_config = viewport_config,
                  });
}
