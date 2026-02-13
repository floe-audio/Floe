// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_modal_viewports.hpp"

#include <IconsFontAwesome6.h>

#include "foundation/foundation.hpp"

#include "engine/engine.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/renderer.hpp"
#include "gui_state.hpp"
#include "gui_viewport_utils.hpp"
#include "old/gui_button_widgets.hpp"
#include "old/gui_label_widgets.hpp"
#include "old/gui_widget_helpers.hpp"

static Rect ModalRect(f32 width, f32 height) {
    auto const size = f32x2 {width, height};
    Rect r;
    r.pos = GuiIo().in.window_size.ToFloat2() / 2 - size / 2; // centre
    r.size = size;
    return r;
}

static Rect ModalRect(imgui::Context const&, UiSizeId width_id, UiSizeId height_id) {
    return ModalRect(LiveSize(width_id), LiveSize(height_id));
}

static void DoHeading(GuiState& g,
                      f32& y_pos,
                      String str,
                      TextJustification justification = TextJustification::CentredLeft,
                      UiColMap col = UiColMap::PopupItemText) {
    auto& imgui = g.imgui;
    auto const viewport_title_h = LiveSize(UiSizeId::ModalViewportTitleH);
    auto const viewport_title_gap_y = LiveSize(UiSizeId::ModalViewportTitleGapY);

    g.fonts.Push(ToInt(FontType::Heading1));
    DEFER { g.fonts.Pop(); };
    auto const r = imgui.RegisterAndConvertRect({.xywh {0, y_pos, imgui.CurrentVpWidth(), viewport_title_h}});
    g.imgui.draw_list->AddTextInRect(r, LiveCol(col), str, {.justification = justification});

    y_pos += viewport_title_h + viewport_title_gap_y;
}

bool DoCloseButtonForCurrentViewport(GuiState& g, String tooltip_text, buttons::Style const& style) {
    auto& imgui = g.imgui;
    f32 const pad = LiveSize(UiSizeId::SidePanelCloseButtonPad);
    f32 const size = LiveSize(UiSizeId::SidePanelCloseButtonSize);

    auto const x = imgui.CurrentVpWidth() - (pad + size);
    Rect const btn_r = {.xywh {x, pad, size, size}};

    auto const btn_id = imgui.MakeId("close");
    bool const button_clicked = buttons::Button(g, btn_id, btn_r, ICON_FA_XMARK, style);

    Tooltip(g, btn_id, imgui.ViewportRectToWindowRect(btn_r), tooltip_text, {});
    return button_clicked;
}

static void DoLegacyParamsModal(GuiState& g) {
    if (!g.imgui.IsModalOpen(k_legacy_params_panel_id)) return;

    auto body_font = g.fonts.atlas[ToInt(FontType::Body)];
    g.fonts.Push(body_font);
    DEFER { g.fonts.Pop(); };
    auto& imgui = g.imgui;

    imgui.BeginViewport(
        ({
            auto cfg = FloeStandardConfig(g.imgui, [](imgui::Context const& imgui) {
                imgui.draw_list->PushClipRectFullScreen();
                imgui.draw_list->AddRectFilled(Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()},
                                               LiveCol(UiColMap::SidePanelOverlay));
                imgui.draw_list->PopClipRect();
                auto r = imgui.curr_viewport->unpadded_bounds;
                auto const rounding = LiveSize(UiSizeId::CornerRounding);
                imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::TopPanelBackTop), rounding);
            });
            cfg.padding = {
                .l = LiveSize(UiSizeId::ModalViewportPadL),
                .t = LiveSize(UiSizeId::ModalViewportPadT),
                .r = LiveSize(UiSizeId::ModalViewportPadR),
                .b = LiveSize(UiSizeId::ModalViewportPadB),
            };
            cfg.mode = imgui::ViewportMode::Modal;
            cfg.positioning = imgui::ViewportPositioning::WindowAbsolute;
            cfg.exclusive_focus = true;
            cfg.close_on_click_outside = true;
            cfg.close_on_escape = true;
            cfg;
        }),
        k_legacy_params_panel_id,
        ModalRect(imgui, UiSizeId::LegacyParamsViewportWidth, UiSizeId::LegacyParamsViewportHeight));
    DEFER { imgui.EndViewport(); };

    f32 y_pos = 0;
    DoHeading(g, y_pos, "Legacy Parameters", TextJustification::CentredLeft, UiColMap::TopPanelTitleText);
    if (DoCloseButtonForCurrentViewport(g,
                                        "Close this window",
                                        buttons::BrowserIconButton(g.imgui).WithLargeIcon())) {
        g.imgui.CloseModal(k_legacy_params_panel_id);
    }

    // Sub-viewport
    imgui.BeginViewport(FloeStandardConfig(imgui, {}),
                        {
                            .x = 0,
                            .y = y_pos,
                            .w = imgui.CurrentVpWidth(),
                            .h = imgui.CurrentVpHeight() - y_pos,
                        },
                        "legacy-params-sub-viewport");
    DEFER { imgui.EndViewport(); };

    auto root = layout::CreateItem(g.layout,
                                   g.scratch_arena,
                                   {
                                       .size = g.imgui.CurrentVpSize(),
                                       .contents_gap = {20, 10},
                                       .contents_direction = layout::Direction::Row,
                                       .contents_multiline = true,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   });

    struct ParamData {
        ParamIndex index;
        LayIDPair pair;
        layout::Id extra_label;
    };

    DynamicArray<ParamData> hidden_params {g.scratch_arena};
    for (auto const& desc : k_param_descriptors)
        if (desc.flags.hidden) dyn::Append(hidden_params, {.index = desc.index});

    for (auto& p : hidden_params) {
        auto const container = layout::CreateItem(g.layout,
                                                  g.scratch_arena,
                                                  {
                                                      .parent = root,
                                                      .size = layout::k_hug_contents,
                                                      .margins = {.b = 20},
                                                      .contents_direction = layout::Direction::Column,
                                                      .contents_align = layout::Alignment::Start,
                                                  });

        auto const& desc = k_param_descriptors[ToInt(p.index)];
        if (desc.value_type != ParamValueType::Bool) {
            LayoutParameterComponent(g,
                                     container,
                                     p.pair,
                                     g.engine.processor.main_params.DescribedValue(p.index),
                                     UiSizeId::Top2KnobsGapX);
        } else {
            auto const text_width = g.fonts.CalcTextSize(desc.name, {}).x;
            auto const toggle_width = text_width + (LiveSize(UiSizeId::MenuButtonTextMarginL) * 2);
            auto const btn_h = LiveSize(UiSizeId::ParamPopupButtonHeight);
            p.pair.control = layout::CreateItem(g.layout,
                                                g.scratch_arena,
                                                {
                                                    .parent = container,
                                                    .size = {toggle_width, btn_h},
                                                });
            p.pair.label = layout::k_invalid_id;
        }

        p.extra_label = layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = container,
                                               .size = {layout::k_fill_parent, body_font->font_size},
                                           });
    }

    layout::RunContext(g.layout);
    DEFER { layout::ResetContext(g.layout); };

    for (auto& p : hidden_params) {
        auto const& desc = k_param_descriptors[ToInt(p.index)];
        auto const& param = g.engine.processor.main_params.DescribedValue(p.index);
        switch (desc.value_type) {
            case ParamValueType::Float: KnobAndLabel(g, param, p.pair, knobs::DefaultKnob(g.imgui)); break;
            case ParamValueType::Menu:
                buttons::PopupWithItems(g,
                                        param,
                                        p.pair.control,
                                        buttons::ParameterPopupButton(g.imgui, false));
                if (p.pair.label != layout::k_invalid_id)
                    labels::Label(g, param, p.pair.label, labels::ParameterCentred(g.imgui, false));
                break;
            case ParamValueType::Bool:
                buttons::Toggle(g, param, p.pair.control, buttons::ParameterToggleButton(g.imgui, false));
                break;
            case ParamValueType::Int: PanicIfReached(); break;
        }

        auto const label_r = imgui.RegisterAndConvertRect(layout::GetRect(g.layout, p.extra_label));
        imgui.draw_list->AddTextInRect(label_r,
                                       LiveCol(UiColMap::TopPanelTitleText),
                                       desc.ModuleString(),
                                       {.justification = TextJustification::Centred});
    }
}

void DoModalViewports(GuiState& g) { DoLegacyParamsModal(g); }
