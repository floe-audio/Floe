// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_eq.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/filters.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr u8 k_num_bands = 2;
constexpr f32 k_display_min_db = -24.0f;
constexpr f32 k_display_max_db = 24.0f;
constexpr f32 k_display_sample_rate = 48000.0f;
constexpr f32 k_handle_radius_ww = 5.0f;
constexpr f32 k_grabber_radius_ww = 12.0f;
constexpr usize k_curve_points = 160;

struct BandParams {
    LayerParamIndex type;
    LayerParamIndex freq;
    LayerParamIndex reso;
    LayerParamIndex gain;
};

static constexpr Array<BandParams, k_num_bands> k_band_params = {{
    {LayerParamIndex::EqType1,
     LayerParamIndex::EqFreq1,
     LayerParamIndex::EqResonance1,
     LayerParamIndex::EqGain1},
    {LayerParamIndex::EqType2,
     LayerParamIndex::EqFreq2,
     LayerParamIndex::EqResonance2,
     LayerParamIndex::EqGain2},
}};

static f32 BandMagnitudeDb(rbj_filter::Coeffs const& c, f32 frequency_hz) {
    auto const w = 2.0 * k_pi<f64> * (f64)frequency_hz / (f64)k_display_sample_rate;
    auto const cos_w = Cos(w);
    auto const cos_2w = Cos(2.0 * w);
    auto const b0 = (f64)c.b0;
    auto const b1 = (f64)c.b1;
    auto const b2 = (f64)c.b2;
    auto const a1 = (f64)c.a1;
    auto const a2 = (f64)c.a2;
    auto const num = (b0 * b0) + (b1 * b1) + (b2 * b2) + (2.0 * ((b0 * b1) + (b1 * b2)) * cos_w) +
                     (2.0 * b0 * b2 * cos_2w);
    auto const den = 1.0 + (a1 * a1) + (a2 * a2) + (2.0 * (a1 + (a1 * a2)) * cos_w) + (2.0 * a2 * cos_2w);
    if (den <= 0) return 0;
    auto const mag2 = num / den;
    if (mag2 <= 0) return k_display_min_db;
    return (f32)(10.0 * Log10(mag2));
}

static rbj_filter::Type EqTypeToRbjType(param_values::EqType t) {
    switch (t) {
        case param_values::EqType::Peak: return rbj_filter::Type::Peaking;
        case param_values::EqType::LowShelf: return rbj_filter::Type::LowShelf;
        case param_values::EqType::HighShelf: return rbj_filter::Type::HighShelf;
        case param_values::EqType::Count: break;
    }
    return rbj_filter::Type::Peaking;
}

static f32 DbToY(f32 db, Rect viewport_r) {
    auto const t = MapTo01(Clamp(db, k_display_min_db, k_display_max_db), k_display_min_db, k_display_max_db);
    return viewport_r.Bottom() - (t * viewport_r.h);
}

static void DoEqBandRightClickMenu(GuiState& g,
                                   Rect window_r,
                                   imgui::Id interaction_id,
                                   u8 layer_index,
                                   LayerParamIndex type_param,
                                   u8 band_number) {
    auto const right_click_id = (imgui::Id)(SourceLocationHash() ^ ((u64)layer_index << 8) ^ band_number);

    if (g.imgui.ButtonBehaviour(window_r,
                                interaction_id,
                                {
                                    .mouse_button = MouseButton::Right,
                                    .event = MouseButtonEvent::Up,
                                })) {
        g.imgui.OpenPopupMenu(right_click_id, interaction_id);
    }

    if (!g.imgui.IsPopupMenuOpen(right_click_id)) return;

    auto const param = g.engine.processor.main_params.DescribedValue(layer_index, type_param);
    auto const current_type = param.IntValue<param_values::EqType>();
    auto const param_index = ParamIndexFromLayerParamIndex(layer_index, type_param);

    DoBoxViewport(g.builder,
                  {
                      .run =
                          [current_type, param_index, &g](GuiBuilder&) {
                              auto const root = DoBox(g.builder,
                                                      {
                                                          .layout {
                                                              .size = layout::k_hug_contents,
                                                              .contents_direction = layout::Direction::Column,
                                                              .contents_align = layout::Alignment::Start,
                                                          },
                                                      });

                              for (auto const i : Range(ToInt(param_values::EqType::Count))) {
                                  auto const type_val = (param_values::EqType)i;
                                  auto const item = MenuItem(g.builder,
                                                             root,
                                                             {
                                                                 .text = param_values::k_eq_type_strings[i],
                                                                 .is_selected = type_val == current_type,
                                                             },
                                                             (u64)i);
                                  if (item.button_fired && type_val != current_type)
                                      SetParameterValue(g.engine.processor, param_index, (f32)i, {});
                              }
                          },
                      .bounds = window_r,
                      .imgui_id = right_click_id,
                      .viewport_config = k_default_popup_menu_viewport,
                  });
}

void DoEqVisualizer(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    auto push_id = HashInit();
    HashUpdate(push_id, SourceLocationHash());
    HashUpdate(push_id, layer_index);
    imgui.PushId(push_id);
    DEFER { imgui.PopId(); };

    // Background.
    auto const window_rect = imgui.ViewportRectToWindowRect(viewport_r);
    imgui.draw_list->AddRectFilled(window_rect, LiveCol(UiColMap::EqBack), WwToPixels(k_corner_rounding));

    // Grid lines (horizontal dB markers and vertical frequency markers).
    {
        auto const grid_col = LiveCol(UiColMap::EqGrid);
        auto const zero_col = LiveCol(UiColMap::EqGridZero);
        for (auto const db : Array {-18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f}) {
            auto const y = DbToY(db, viewport_r);
            auto const p0 = imgui.ViewportPosToWindowPos({viewport_r.x, y});
            auto const p1 = imgui.ViewportPosToWindowPos({viewport_r.Right(), y});
            imgui.draw_list->AddLine(p0, p1, db == 0.0f ? zero_col : grid_col);
        }

        // Frequency gridlines at 100Hz, 1kHz, 10kHz (log-mapped through projection).
        auto const& freq_info_grid =
            k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::EqFreq1))];
        for (auto const f_hz : Array {100.0f, 1000.0f, 10000.0f}) {
            auto const linear = freq_info_grid.LineariseValue(f_hz, true);
            if (!linear) continue;
            auto const x = viewport_r.x + (*linear * viewport_r.w);
            auto const p0 = imgui.ViewportPosToWindowPos({x, viewport_r.y});
            auto const p1 = imgui.ViewportPosToWindowPos({x, viewport_r.Bottom()});
            imgui.draw_list->AddLine(p0, p1, grid_col);
        }
    }

    // Gather band info and compute coefficients.
    // The node handle sits at the base (user-set) parameter value, but the drawn response curve
    // reflects any macro modulation applied to freq / reso / gain.
    struct BandState {
        param_values::EqType type;
        rbj_filter::Coeffs coeffs;
        f32x2 node_pos_viewport;
    };
    Array<BandState, k_num_bands> bands;

    auto const& macro_dests = engine.processor.main_macro_destinations;

    for (auto const band_idx : Range(k_num_bands)) {
        auto& b = bands[band_idx];
        auto const& bp = k_band_params[band_idx];

        auto const freq_param = params.DescribedValue(layer_index, bp.freq);
        auto const reso_param = params.DescribedValue(layer_index, bp.reso);
        auto const gain_param = params.DescribedValue(layer_index, bp.gain);
        auto const type_param = params.DescribedValue(layer_index, bp.type);

        b.type = type_param.IntValue<param_values::EqType>();

        auto const freq_adj_linear =
            AdjustedLinearValue(params, macro_dests, freq_param.LinearValue(), freq_param.info.index);
        auto const reso_adj_linear =
            AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);
        auto const gain_adj_linear =
            AdjustedLinearValue(params, macro_dests, gain_param.LinearValue(), gain_param.info.index);

        auto const freq_adj_hz = freq_param.info.ProjectValue(freq_adj_linear);
        auto const gain_adj_db = gain_param.info.ProjectValue(gain_adj_linear);
        auto const q_adj = MapFrom01Skew(reso_adj_linear, 0.5f, 8.0f, 5.0f);

        b.coeffs = rbj_filter::Coefficients({
            .type = EqTypeToRbjType(b.type),
            .fs = k_display_sample_rate,
            .fc = Clamp(freq_adj_hz, 15.0f, k_display_sample_rate * 0.49f),
            .q = q_adj,
            .peak_gain = gain_adj_db,
        });

        // Node position uses the base value (no macro adjustment).
        auto const node_x = viewport_r.x + (freq_param.LinearValue() * viewport_r.w);
        auto const node_y = DbToY(gain_param.ProjectedValue(), viewport_r);
        b.node_pos_viewport = {node_x, node_y};
    }

    // Draw frequency response curve (combined).
    DynamicArrayBounded<f32x2, k_curve_points> curve_points;

    auto const& freq_info =
        k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::EqFreq1))];

    for (auto const i : Range(k_curve_points)) {
        auto const t = (f32)i / (f32)(k_curve_points - 1);
        auto const x = viewport_r.x + (t * viewport_r.w);
        auto const freq_hz = freq_info.ProjectValue(t);

        f32 total_db = 0;
        for (auto const& b : bands)
            total_db += BandMagnitudeDb(b.coeffs, freq_hz);

        auto const y = DbToY(total_db, viewport_r);
        dyn::Append(curve_points, imgui.ViewportPosToWindowPos({x, y}));
    }

    // Area fill: draw per-segment quads between the zero line and the curve.
    // AddConvexPolyFilled can't handle the non-convex shape the curve makes.
    auto const zero_y_window = imgui.ViewportPosToWindowPos({viewport_r.x, DbToY(0.0f, viewport_r)}).y;
    auto const area_col = LiveCol(UiColMap::EqArea);
    for (auto const i : Range(curve_points.size - 1)) {
        auto const p0 = curve_points[i];
        auto const p1 = curve_points[i + 1];
        f32x2 const verts[] = {
            f32x2 {p0.x, zero_y_window},
            p0,
            p1,
            f32x2 {p1.x, zero_y_window},
        };
        imgui.draw_list->AddConvexPolyFilled(verts, area_col, false);
    }

    auto const line_col = greyed_out ? LiveCol(UiColMap::EqLineGreyedOut) : LiveCol(UiColMap::EqLine);
    imgui.draw_list->AddPolyline(curve_points, line_col, false, 1.5f, true);

    // Interaction + handles per band.
    for (auto const band_idx : Range(k_num_bands)) {
        auto const& b = bands[band_idx];
        auto const& bp = k_band_params[band_idx];

        auto const freq_index = ParamIndexFromLayerParamIndex(layer_index, bp.freq);
        auto const gain_index = ParamIndexFromLayerParamIndex(layer_index, bp.gain);
        auto const reso_index = ParamIndexFromLayerParamIndex(layer_index, bp.reso);

        auto const freq_param = params.DescribedValue(layer_index, bp.freq);
        auto const gain_param = params.DescribedValue(layer_index, bp.gain);
        auto const reso_param = params.DescribedValue(layer_index, bp.reso);
        DescribedParamValue const* params_arr[] = {&freq_param, &gain_param, &reso_param};

        auto const interaction_id = imgui.MakeId(SourceLocationHash() + band_idx);

        auto const grabber_viewport_r = Rect {.xywh {b.node_pos_viewport.x - grabber_radius,
                                                     b.node_pos_viewport.y - grabber_radius,
                                                     grabber_radius * 2,
                                                     grabber_radius * 2}};
        auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

        // Drag: track relative click pos.
        static Array<f32x2, k_num_bands> rel_click_pos;
        if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
            rel_click_pos[band_idx] =
                GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(b.node_pos_viewport);

        if (imgui.ButtonBehaviour(grabber_window_r,
                                  interaction_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = freq_index;

        if (imgui.WasJustActivated(interaction_id, MouseButton::Left)) {
            ParameterJustStartedMoving(engine.processor, freq_index);
            ParameterJustStartedMoving(engine.processor, gain_index);
        }

        if (imgui.IsActive(interaction_id, MouseButton::Left)) {
            auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
            auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;
            auto const min_y = imgui.ViewportPosToWindowPos({0, viewport_r.y}).y;
            auto const max_y = imgui.ViewportPosToWindowPos({0, viewport_r.Bottom()}).y;

            auto const cursor = GuiIo().in.cursor_pos - rel_click_pos[band_idx];

            auto const x_clamped = Clamp(cursor.x, min_x, max_x);
            auto const new_freq_linear = MapTo01(x_clamped, min_x, max_x);

            auto const y_clamped = Clamp(cursor.y, min_y, max_y);
            auto const y_t = 1.0f - MapTo01(y_clamped, min_y, max_y);
            auto const new_gain_db = MapFrom01(y_t, k_display_min_db, k_display_max_db);
            auto const gain_linear = gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);

            SetParameterValue(engine.processor, freq_index, new_freq_linear, {});
            SetParameterValue(engine.processor, gain_index, gain_linear, {});
        }

        if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, freq_index);
            ParameterJustStoppedMoving(engine.processor, gain_index);
        }

        // Mouse wheel over handle -> adjust resonance.
        if (imgui.IsHot(interaction_id)) {
            auto const scroll = GuiIo().in.mouse_scroll_delta_in_lines;
            if (scroll != 0) {
                auto new_reso = Clamp01(reso_param.LinearValue() + (scroll * 0.03f));
                ParameterJustStartedMoving(engine.processor, reso_index);
                SetParameterValue(engine.processor, reso_index, new_reso, {});
                ParameterJustStoppedMoving(engine.processor, reso_index);
            }
        }

        // Right-click menu for band type.
        DoEqBandRightClickMenu(g, grabber_window_r, interaction_id, layer_index, bp.type, (u8)(band_idx + 1));

        ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

        // Draw the handle.
        auto const handle_pos = imgui.ViewportPosToWindowPos(b.node_pos_viewport);
        auto col = greyed_out ? LiveCol(UiColMap::EqHandleGreyedOut) : LiveCol(UiColMap::EqHandle);
        if (imgui.IsHot(interaction_id) || imgui.IsActive(interaction_id, MouseButton::Left)) {
            auto halo = FromU32(col);
            halo.a /= 2;
            imgui.draw_list->AddCircleFilled(handle_pos, handle_radius * 1.8f, ToU32(halo));
            col = LiveCol(UiColMap::EqHandleHover);
        }
        imgui.draw_list->AddCircleFilled(handle_pos, handle_radius, col);

        if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        OverlayMacroDestinationRegion(g, grabber_window_r, freq_index);
    }

    // Text editor.
    if (g.param_text_editor_to_open) {
        Array<ParamIndex, k_num_bands * 4> all_indices;
        usize idx = 0;
        for (auto const band_idx : Range(k_num_bands)) {
            auto const& bp = k_band_params[band_idx];
            all_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, bp.freq);
            all_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, bp.reso);
            all_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, bp.gain);
            all_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, bp.type);
        }
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}
