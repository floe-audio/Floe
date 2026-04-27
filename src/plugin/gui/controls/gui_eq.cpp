// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_eq.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_biquad_display.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/filters.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr u8 k_num_bands = 2;
constexpr f32 k_handle_radius_ww = 5.0f;
constexpr f32 k_grabber_radius_ww = 12.0f;

struct BandParams {
    LayerParamIndex type;
    LayerParamIndex freq;
    LayerParamIndex legacy_reso;
    LayerParamIndex reso;
    LayerParamIndex gain;
};

static constexpr Array<BandParams, k_num_bands> k_band_params = {{
    {LayerParamIndex::EqType1,
     LayerParamIndex::EqFreq1,
     LayerParamIndex::LegacyEqResonance1,
     LayerParamIndex::EqResonance1,
     LayerParamIndex::EqGain1},
    {LayerParamIndex::EqType2,
     LayerParamIndex::EqFreq2,
     LayerParamIndex::LegacyEqResonance2,
     LayerParamIndex::EqResonance2,
     LayerParamIndex::EqGain2},
}};

static rbj_filter::Type EqTypeToRbjType(param_values::EqType t) {
    switch (t) {
        case param_values::EqType::Peak: return rbj_filter::Type::Peaking;
        case param_values::EqType::LowShelf: return rbj_filter::Type::LowShelf;
        case param_values::EqType::HighShelf: return rbj_filter::Type::HighShelf;
        case param_values::EqType::Count: break;
    }
    return rbj_filter::Type::Peaking;
}

static void DoEqBandRightClickMenu(GuiState& g,
                                   Rect window_r,
                                   imgui::Id interaction_id,
                                   u8 layer_index,
                                   BandParams const& bp,
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

    auto const param = g.engine.processor.main_params.DescribedValue(layer_index, bp.type);
    auto const current_type = param.IntValue<param_values::EqType>();
    auto const type_index = ParamIndexFromLayerParamIndex(layer_index, bp.type);
    auto const freq_index = ParamIndexFromLayerParamIndex(layer_index, bp.freq);
    auto const reso_index = ParamIndexFromLayerParamIndex(layer_index, bp.reso);
    auto const gain_index = ParamIndexFromLayerParamIndex(layer_index, bp.gain);

    DoBoxViewport(
        g.builder,
        {
            .run =
                [current_type, type_index, freq_index, reso_index, gain_index, &g](GuiBuilder&) {
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
                            SetParameterValue(g.engine.processor, type_index, (f32)i, {});
                    }

                    DoModalDivider(g.builder, root, {.horizontal = true});

                    if (MenuItem(g.builder, root, {.text = "Reset Value"}).button_fired) {
                        SetParameterValue(g.engine.processor,
                                          freq_index,
                                          k_param_descriptors[ToInt(freq_index)].default_linear_value,
                                          {});
                        SetParameterValue(g.engine.processor,
                                          reso_index,
                                          k_param_descriptors[ToInt(reso_index)].default_linear_value,
                                          {});
                        SetParameterValue(g.engine.processor,
                                          gain_index,
                                          k_param_descriptors[ToInt(gain_index)].default_linear_value,
                                          {});
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

    auto const& freq_info =
        k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::EqFreq1))];

    biquad_display::DrawBackground(imgui, viewport_r, freq_info);

    // Node handle sits at the base (user-set) value; the drawn curve reflects macro modulation.
    struct BandState {
        rbj_filter::Coeffs coeffs;
        f32x2 node_pos_viewport;
    };
    Array<BandState, k_num_bands> bands;

    auto const& macro_dests = engine.processor.main_macro_destinations;

    for (auto const band_idx : Range(k_num_bands)) {
        auto& b = bands[band_idx];
        auto const& bp = k_band_params[band_idx];

        auto const freq_param = params.DescribedValue(layer_index, bp.freq);
        auto const legacy_reso_param = params.DescribedValue(layer_index, bp.legacy_reso);
        auto const reso_param = params.DescribedValue(layer_index, bp.reso);
        auto const gain_param = params.DescribedValue(layer_index, bp.gain);
        auto const type_param = params.DescribedValue(layer_index, bp.type);

        auto const freq_adj_linear =
            AdjustedLinearValue(params, macro_dests, freq_param.LinearValue(), freq_param.info.index);
        auto const reso_adj_linear =
            AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);
        auto const gain_adj_linear =
            AdjustedLinearValue(params, macro_dests, gain_param.LinearValue(), gain_param.info.index);

        auto const freq_adj_hz = freq_param.info.ProjectValue(freq_adj_linear);
        auto const gain_adj_db = gain_param.info.ProjectValue(gain_adj_linear);
        auto const q_adj = ({
            auto const legacy_linear = legacy_reso_param.LinearValue();
            (legacy_linear != legacy_reso_param.info.default_linear_value)
                ? MapFrom01Skew(legacy_linear, 0.5f, 8.0f, 5.0f)
                : MapFrom01Skew(reso_adj_linear, 0.5f, 8.0f, 2.0f);
        });

        b.coeffs = rbj_filter::Coefficients({
            .type = EqTypeToRbjType(type_param.IntValue<param_values::EqType>()),
            .fs = biquad_display::k_sample_rate,
            .fc = Clamp(freq_adj_hz, 15.0f, biquad_display::k_sample_rate * 0.49f),
            .q = q_adj,
            .peak_gain = gain_adj_db,
        });

        auto const node_x = viewport_r.x + (freq_param.LinearValue() * viewport_r.w);
        auto const node_y = biquad_display::DbToY(gain_param.ProjectedValue(), viewport_r);
        b.node_pos_viewport = {node_x, node_y};
    }

    Array<rbj_filter::Coeffs, k_num_bands> coeffs_list;
    for (auto const i : Range(k_num_bands))
        coeffs_list[i] = bands[i].coeffs;
    biquad_display::DrawResponseCurve(imgui, viewport_r, coeffs_list, freq_info, greyed_out);

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
            auto const new_gain_db = MapFrom01(y_t, biquad_display::k_min_db, biquad_display::k_max_db);
            auto const gain_linear = gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);

            SetParameterValue(engine.processor, freq_index, new_freq_linear, {});
            SetParameterValue(engine.processor, gain_index, gain_linear, {});
        }

        if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, freq_index);
            ParameterJustStoppedMoving(engine.processor, gain_index);
        }

        imgui.ConsumeScrollAtRect(grabber_window_r);
        if (imgui.IsHot(interaction_id)) {
            auto const scroll = GuiIo().in.mouse_scroll_delta_in_lines;
            if (scroll != 0) {
                auto new_reso = Clamp01(reso_param.LinearValue() + (scroll * 0.03f));
                ParameterJustStartedMoving(engine.processor, reso_index);
                SetParameterValue(engine.processor, reso_index, new_reso, {});
                ParameterJustStoppedMoving(engine.processor, reso_index);
            }
        }

        DoEqBandRightClickMenu(g, grabber_window_r, interaction_id, layer_index, bp, (u8)(band_idx + 1));

        ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

        auto const handle_pos = imgui.ViewportPosToWindowPos(b.node_pos_viewport);
        biquad_display::DrawHandle(imgui, handle_pos, handle_radius, interaction_id, greyed_out);

        if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        OverlayMacroDestinationRegion(g, grabber_window_r, freq_index);
    }

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
