// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_filter_visualizer.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_biquad_display.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/filters.hpp"
#include "processor/processor.hpp"

constexpr f32 k_handle_radius_ww = 5.0f;
constexpr f32 k_grabber_radius_ww = 12.0f;

// Peak / band-shelving have no user gain parameter, so we use a fixed boost for visualisation
// so the user can see the shape.
constexpr f32 k_display_peak_gain_db = 12.0f;

struct FilterVisState {
    rbj_filter::Type rbj_type;
    bool valid; // false for types we can't nicely visualise (e.g. Allpass)
    f32 peak_gain_db;
};

static FilterVisState MapLayerFilterType(param_values::LayerFilterType t) {
    switch (t) {
        case param_values::LayerFilterType::Lowpass: return {rbj_filter::Type::LowPass, true, 0};
        case param_values::LayerFilterType::Bandpass: return {rbj_filter::Type::BandPassCzpg, true, 0};
        case param_values::LayerFilterType::Highpass: return {rbj_filter::Type::HighPass, true, 0};
        case param_values::LayerFilterType::UnitGainBandpass:
            return {rbj_filter::Type::BandPassCzpg, true, 0};
        case param_values::LayerFilterType::BandShelving:
            return {rbj_filter::Type::Peaking, true, k_display_peak_gain_db};
        case param_values::LayerFilterType::Notch: return {rbj_filter::Type::Notch, true, 0};
        case param_values::LayerFilterType::Allpass: return {rbj_filter::Type::AllPass, false, 0};
        case param_values::LayerFilterType::Peak:
            return {rbj_filter::Type::Peaking, true, k_display_peak_gain_db};
        case param_values::LayerFilterType::Count: break;
    }
    return {rbj_filter::Type::LowPass, true, 0};
}

static void DoFilterTypeRightClickMenu(GuiState& g, Rect window_r, imgui::Id interaction_id, u8 layer_index) {
    auto const right_click_id = (imgui::Id)(SourceLocationHash() ^ ((u64)layer_index << 8));

    if (g.imgui.ButtonBehaviour(window_r,
                                interaction_id,
                                {
                                    .mouse_button = MouseButton::Right,
                                    .event = MouseButtonEvent::Up,
                                })) {
        g.imgui.OpenPopupMenu(right_click_id, interaction_id);
    }

    if (!g.imgui.IsPopupMenuOpen(right_click_id)) return;

    auto const param =
        g.engine.processor.main_params.DescribedValue(layer_index, LayerParamIndex::FilterType);
    auto const current_type = param.IntValue<param_values::LayerFilterType>();
    auto const param_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterType);

    DoBoxViewport(
        g.builder,
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

                    for (auto const i : Range(ToInt(param_values::LayerFilterType::Count))) {
                        auto const type_val = (param_values::LayerFilterType)i;
                        // Hide the legacy allpass from the visualiser picker.
                        if (type_val == param_values::LayerFilterType::Allpass && type_val != current_type)
                            continue;
                        auto const item = MenuItem(g.builder,
                                                   root,
                                                   {
                                                       .text = param_values::k_layer_filter_type_strings[i],
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

void DoFilterVisualizer(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out) {
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
        k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterCutoff))];

    biquad_display::DrawBackground(imgui, viewport_r, freq_info);

    auto const cutoff_param = params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff);
    auto const reso_param = params.DescribedValue(layer_index, LayerParamIndex::FilterResonance);
    auto const type_param = params.DescribedValue(layer_index, LayerParamIndex::FilterType);

    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const reso_adj_linear =
        AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);

    auto const vis_state = MapLayerFilterType(type_param.IntValue<param_values::LayerFilterType>());

    auto const cutoff_adj_hz = cutoff_param.info.ProjectValue(cutoff_adj_linear);
    // Clamp to just below 1 to avoid a divide-by-zero in ResonanceToQ at exactly 1.
    auto const q_adj =
        sv_filter::ResonanceToQ(sv_filter::SkewResonance(Clamp(reso_adj_linear, 0.0f, 0.9999f)));

    auto const coeffs = rbj_filter::Coefficients({
        .type = vis_state.rbj_type,
        .fs = biquad_display::k_sample_rate,
        .fc = Clamp(cutoff_adj_hz, 15.0f, biquad_display::k_sample_rate * 0.49f),
        .q = q_adj,
        .peak_gain = vis_state.peak_gain_db,
    });

    if (vis_state.valid) {
        rbj_filter::Coeffs const coeffs_list[] = {coeffs};
        biquad_display::DrawResponseCurve(imgui, viewport_r, coeffs_list, freq_info, greyed_out);
    }

    // Node position: X = cutoff (base value). Y stays pinned to the 0dB line regardless of the
    // curve shape — the curve itself reflects resonance.
    auto const node_x = viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w);
    auto const node_y = biquad_display::DbToY(0.0f, viewport_r);
    f32x2 const node_pos_viewport {node_x, node_y};

    auto const cutoff_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterCutoff);
    auto const reso_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterResonance);
    DescribedParamValue const* params_arr[] = {&cutoff_param, &reso_param};

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    auto const grabber_viewport_r = Rect {.xywh {node_pos_viewport.x - grabber_radius,
                                                 node_pos_viewport.y - grabber_radius,
                                                 grabber_radius * 2,
                                                 grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport);

    if (imgui.ButtonBehaviour(grabber_window_r,
                              interaction_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::DoubleClick,
                              }))
        g.param_text_editor_to_open = cutoff_index;

    if (imgui.WasJustActivated(interaction_id, MouseButton::Left))
        ParameterJustStartedMoving(engine.processor, cutoff_index);

    if (imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;

        auto const cursor_x = GuiIo().in.cursor_pos.x - rel_click_pos.x;
        auto const x_clamped = Clamp(cursor_x, min_x, max_x);
        auto const new_cutoff_linear = MapTo01(x_clamped, min_x, max_x);

        SetParameterValue(engine.processor, cutoff_index, new_cutoff_linear, {});
    }

    if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left))
        ParameterJustStoppedMoving(engine.processor, cutoff_index);

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

    DoFilterTypeRightClickMenu(g, grabber_window_r, interaction_id, layer_index);

    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    auto const handle_pos = imgui.ViewportPosToWindowPos(node_pos_viewport);
    biquad_display::DrawHandle(imgui, handle_pos, handle_radius, interaction_id, greyed_out);

    if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

    OverlayMacroDestinationRegion(g, grabber_window_r, cutoff_index);

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 3> const all_indices {
            cutoff_index,
            reso_index,
            ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterType),
        };
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}

// ===============================================================================
// Effect filter visualiser.

static rbj_filter::Type EffectFilterTypeToRbjType(param_values::EffectFilterType t) {
    switch (t) {
        case param_values::EffectFilterType::LowPass: return rbj_filter::Type::LowPass;
        case param_values::EffectFilterType::HighPass: return rbj_filter::Type::HighPass;
        case param_values::EffectFilterType::BandPass: return rbj_filter::Type::BandPassCzpg;
        case param_values::EffectFilterType::Notch: return rbj_filter::Type::Notch;
        case param_values::EffectFilterType::Peak: return rbj_filter::Type::Peaking;
        case param_values::EffectFilterType::LowShelf: return rbj_filter::Type::LowShelf;
        case param_values::EffectFilterType::HighShelf: return rbj_filter::Type::HighShelf;
        case param_values::EffectFilterType::Count: break;
    }
    return rbj_filter::Type::LowPass;
}

static bool EffectFilterTypeUsesGain(param_values::EffectFilterType t) {
    return t == param_values::EffectFilterType::Peak || t == param_values::EffectFilterType::LowShelf ||
           t == param_values::EffectFilterType::HighShelf;
}

static void DoEffectFilterTypeRightClickMenu(GuiState& g, Rect window_r, imgui::Id interaction_id) {
    auto const right_click_id = (imgui::Id)SourceLocationHash();

    if (g.imgui.ButtonBehaviour(window_r,
                                interaction_id,
                                {
                                    .mouse_button = MouseButton::Right,
                                    .event = MouseButtonEvent::Up,
                                })) {
        g.imgui.OpenPopupMenu(right_click_id, interaction_id);
    }

    if (!g.imgui.IsPopupMenuOpen(right_click_id)) return;

    auto const param = g.engine.processor.main_params.DescribedValue(ParamIndex::FilterType);
    auto const current_type = param.IntValue<param_values::EffectFilterType>();

    DoBoxViewport(
        g.builder,
        {
            .run =
                [current_type, &g](GuiBuilder&) {
                    auto const root = DoBox(g.builder,
                                            {
                                                .layout {
                                                    .size = layout::k_hug_contents,
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

                    for (auto const i : Range(ToInt(param_values::EffectFilterType::Count))) {
                        auto const type_val = (param_values::EffectFilterType)i;
                        auto const item = MenuItem(g.builder,
                                                   root,
                                                   {
                                                       .text = param_values::k_effect_filter_type_strings[i],
                                                       .is_selected = type_val == current_type,
                                                   },
                                                   (u64)i);
                        if (item.button_fired && type_val != current_type)
                            SetParameterValue(g.engine.processor, ParamIndex::FilterType, (f32)i, {});
                    }
                },
            .bounds = window_r,
            .imgui_id = right_click_id,
            .viewport_config = k_default_popup_menu_viewport,
        });
}

void DoEffectFilterVisualizer(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId((u64)SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];

    biquad_display::DrawBackground(imgui, viewport_r, freq_info);

    auto const cutoff_param = params.DescribedValue(ParamIndex::FilterCutoff);
    auto const reso_param = params.DescribedValue(ParamIndex::FilterResonance);
    auto const gain_param = params.DescribedValue(ParamIndex::FilterGain);
    auto const type_param = params.DescribedValue(ParamIndex::FilterType);

    auto const filter_type = type_param.IntValue<param_values::EffectFilterType>();
    auto const uses_gain = EffectFilterTypeUsesGain(filter_type);

    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const reso_adj_linear =
        AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);
    auto const gain_adj_linear =
        AdjustedLinearValue(params, macro_dests, gain_param.LinearValue(), gain_param.info.index);

    auto const cutoff_adj_hz = cutoff_param.info.ProjectValue(cutoff_adj_linear);
    auto const q_adj = MapFrom01Skew(Clamp01(reso_adj_linear), 0.5f, 2.0f, 5.0f);
    auto const gain_adj_db = uses_gain ? gain_param.info.ProjectValue(gain_adj_linear) : 0.0f;

    auto const coeffs = rbj_filter::Coefficients({
        .type = EffectFilterTypeToRbjType(filter_type),
        .fs = biquad_display::k_sample_rate,
        .fc = Clamp(cutoff_adj_hz, 15.0f, biquad_display::k_sample_rate * 0.49f),
        .q = q_adj,
        .peak_gain = gain_adj_db,
    });

    // The DSP runs two biquad passes in series, so the audible response is |H|². We model this
    // in the display by feeding the same coefficients twice (dB values add).
    rbj_filter::Coeffs const coeffs_list[] = {coeffs, coeffs};
    biquad_display::DrawResponseCurve(imgui, viewport_r, coeffs_list, freq_info, greyed_out);

    // Node position: X = cutoff (base). Y = gain (base) for gain-using types, else pinned to 0dB.
    auto const node_x = viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w);
    auto const node_y = uses_gain ? biquad_display::DbToY(gain_param.ProjectedValue(), viewport_r)
                                  : biquad_display::DbToY(0.0f, viewport_r);
    f32x2 const node_pos_viewport {node_x, node_y};

    DescribedParamValue const* params_arr[] = {&cutoff_param, &reso_param, &gain_param};

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    auto const grabber_viewport_r = Rect {.xywh {node_pos_viewport.x - grabber_radius,
                                                 node_pos_viewport.y - grabber_radius,
                                                 grabber_radius * 2,
                                                 grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport);

    if (imgui.ButtonBehaviour(grabber_window_r,
                              interaction_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::DoubleClick,
                              }))
        g.param_text_editor_to_open = ParamIndex::FilterCutoff;

    if (imgui.WasJustActivated(interaction_id, MouseButton::Left)) {
        ParameterJustStartedMoving(engine.processor, ParamIndex::FilterCutoff);
        if (uses_gain) ParameterJustStartedMoving(engine.processor, ParamIndex::FilterGain);
    }

    if (imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;

        auto const cursor = GuiIo().in.cursor_pos - rel_click_pos;
        auto const x_clamped = Clamp(cursor.x, min_x, max_x);
        auto const new_cutoff_linear = MapTo01(x_clamped, min_x, max_x);
        SetParameterValue(engine.processor, ParamIndex::FilterCutoff, new_cutoff_linear, {});

        if (uses_gain) {
            auto const min_y = imgui.ViewportPosToWindowPos({0, viewport_r.y}).y;
            auto const max_y = imgui.ViewportPosToWindowPos({0, viewport_r.Bottom()}).y;
            auto const y_clamped = Clamp(cursor.y, min_y, max_y);
            auto const y_t = 1.0f - MapTo01(y_clamped, min_y, max_y);
            auto const new_gain_db = MapFrom01(y_t, biquad_display::k_min_db, biquad_display::k_max_db);
            auto const gain_linear = gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
            SetParameterValue(engine.processor, ParamIndex::FilterGain, gain_linear, {});
        }
    }

    if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
        ParameterJustStoppedMoving(engine.processor, ParamIndex::FilterCutoff);
        if (uses_gain) ParameterJustStoppedMoving(engine.processor, ParamIndex::FilterGain);
    }

    imgui.ConsumeScrollAtRect(grabber_window_r);
    if (imgui.IsHot(interaction_id)) {
        auto const scroll = GuiIo().in.mouse_scroll_delta_in_lines;
        if (scroll != 0) {
            auto new_reso = Clamp01(reso_param.LinearValue() + (scroll * 0.03f));
            ParameterJustStartedMoving(engine.processor, ParamIndex::FilterResonance);
            SetParameterValue(engine.processor, ParamIndex::FilterResonance, new_reso, {});
            ParameterJustStoppedMoving(engine.processor, ParamIndex::FilterResonance);
        }
    }

    DoEffectFilterTypeRightClickMenu(g, grabber_window_r, interaction_id);

    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    auto const handle_pos = imgui.ViewportPosToWindowPos(node_pos_viewport);
    biquad_display::DrawHandle(imgui, handle_pos, handle_radius, interaction_id, greyed_out);

    if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = uses_gain ? CursorType::AllArrows : CursorType::HorizontalArrows;

    OverlayMacroDestinationRegion(g, grabber_window_r, ParamIndex::FilterCutoff);

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 4> const all_indices {
            ParamIndex::FilterCutoff,
            ParamIndex::FilterResonance,
            ParamIndex::FilterGain,
            ParamIndex::FilterType,
        };
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}
