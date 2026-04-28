// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_filter_visualiser.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_filter_display.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "processing_utils/filters.hpp"
#include "processor/effect_filter_iir.hpp"
#include "processor/processor.hpp"

constexpr f32 k_handle_radius_ww = 5.0f;
constexpr f32 k_grabber_radius_ww = 12.0f;

static sv_filter::Type MapLayerFilterType(param_values::LayerFilterType t) {
    switch (t) {
        case param_values::LayerFilterType::Lowpass: return sv_filter::Type::Lowpass;
        case param_values::LayerFilterType::Highpass: return sv_filter::Type::Highpass;
        case param_values::LayerFilterType::Bandpass: return sv_filter::Type::UnitGainBandpass;
        case param_values::LayerFilterType::BandpassResonant: return sv_filter::Type::Bandpass;
        case param_values::LayerFilterType::BandShelving: return sv_filter::Type::BandShelving;
        case param_values::LayerFilterType::Notch: return sv_filter::Type::Notch;
        case param_values::LayerFilterType::Peak: return sv_filter::Type::Peak;
        case param_values::LayerFilterType::Count: break;
    }
    return sv_filter::Type::Lowpass;
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
                [current_type, param_index, layer_index, &g](GuiBuilder&) {
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

                    DoModalDivider(g.builder, root, {.horizontal = true});

                    auto const cutoff_idx =
                        ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterCutoff);
                    auto const reso_idx =
                        ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterResonance);

                    if (MenuItem(g.builder, root, {.text = "Reset Value"}).button_fired) {
                        SetParameterValue(g.engine.processor,
                                          cutoff_idx,
                                          k_param_descriptors[ToInt(cutoff_idx)].default_linear_value,
                                          {});
                        SetParameterValue(g.engine.processor,
                                          reso_idx,
                                          k_param_descriptors[ToInt(reso_idx)].default_linear_value,
                                          {});
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

    auto const cutoff_param = params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff);
    auto const reso_param = params.DescribedValue(layer_index, LayerParamIndex::FilterResonance);
    auto const type_param = params.DescribedValue(layer_index, LayerParamIndex::FilterType);

    auto const cutoff_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterCutoff);
    auto const reso_index = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterResonance);

    filter_display::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const node_pos_viewport = [&] {
        return f32x2 {viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
                      filter_display::DbToY(0.0f, viewport_r)};
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    auto const grabber_viewport_r = Rect {.xywh {node_pos_viewport().x - grabber_radius,
                                                 node_pos_viewport().y - grabber_radius,
                                                 grabber_radius * 2,
                                                 grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    // Process input before drawing the curve/handle so they reflect this frame's changes.
    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport());

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

    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const reso_adj_linear =
        AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);

    auto const sv_type = MapLayerFilterType(type_param.IntValue<param_values::LayerFilterType>());

    auto const cutoff_adj_hz = Clamp(cutoff_param.info.ProjectValue(cutoff_adj_linear),
                                     15.0f,
                                     filter_display::k_sample_rate * 0.49f);
    // Clamp to just below 1 to avoid a divide-by-zero in ResonanceToQ at exactly 1.
    auto const res_adj = Clamp(reso_adj_linear, 0.0f, 0.9999f);

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return sv_filter::MagnitudeDb(sv_type, cutoff_adj_hz, filter_display::k_sample_rate, res_adj, hz);
        },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* params_arr[] = {&cutoff_param, &reso_param};
    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    filter_display::DrawHandle(imgui,
                               imgui.ViewportPosToWindowPos(node_pos_viewport()),
                               handle_radius,
                               interaction_id,
                               greyed_out);

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

                    DoModalDivider(g.builder, root, {.horizontal = true});

                    if (MenuItem(g.builder, root, {.text = "Reset Value"}).button_fired) {
                        SetParameterValue(
                            g.engine.processor,
                            ParamIndex::FilterCutoff,
                            k_param_descriptors[ToInt(ParamIndex::FilterCutoff)].default_linear_value,
                            {});
                        SetParameterValue(
                            g.engine.processor,
                            ParamIndex::FilterResonance,
                            k_param_descriptors[ToInt(ParamIndex::FilterResonance)].default_linear_value,
                            {});
                        SetParameterValue(
                            g.engine.processor,
                            ParamIndex::FilterGain,
                            k_param_descriptors[ToInt(ParamIndex::FilterGain)].default_linear_value,
                            {});
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

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const cutoff_param = params.DescribedValue(ParamIndex::FilterCutoff);
    auto const reso_param = params.DescribedValue(ParamIndex::FilterResonance);
    auto const gain_param = params.DescribedValue(ParamIndex::FilterGain);
    auto const type_param = params.DescribedValue(ParamIndex::FilterType);

    filter_display::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const uses_gain = [&] {
        return param_values::EffectFilterTypeUsesGain(type_param.IntValue<param_values::EffectFilterType>());
    };

    auto const num_stages = [&] {
        return EffectFilterStageCount(type_param.IntValue<param_values::EffectFilterType>());
    };

    auto const node_pos_viewport = [&] {
        return f32x2 {
            viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
            uses_gain()
                ? filter_display::DbToY(
                      Clamp(gain_param.ProjectedValue(), filter_display::k_min_db, filter_display::k_max_db),
                      viewport_r)
                : filter_display::DbToY(0.0f, viewport_r),
        };
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    auto const grabber_viewport_r = Rect {.xywh {node_pos_viewport().x - grabber_radius,
                                                 node_pos_viewport().y - grabber_radius,
                                                 grabber_radius * 2,
                                                 grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    // Process input before drawing the curve/handle so they reflect this frame's changes.
    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport());

    if (imgui.ButtonBehaviour(grabber_window_r,
                              interaction_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::DoubleClick,
                              }))
        g.param_text_editor_to_open = ParamIndex::FilterCutoff;

    if (imgui.WasJustActivated(interaction_id, MouseButton::Left)) {
        ParameterJustStartedMoving(engine.processor, ParamIndex::FilterCutoff);
        if (uses_gain()) ParameterJustStartedMoving(engine.processor, ParamIndex::FilterGain);
    }

    if (imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;

        auto const cursor = GuiIo().in.cursor_pos - rel_click_pos;
        auto const x_clamped = Clamp(cursor.x, min_x, max_x);
        auto const new_cutoff_linear = MapTo01(x_clamped, min_x, max_x);
        SetParameterValue(engine.processor, ParamIndex::FilterCutoff, new_cutoff_linear, {});

        if (uses_gain()) {
            auto const min_y = imgui.ViewportPosToWindowPos({0, viewport_r.y}).y;
            auto const max_y = imgui.ViewportPosToWindowPos({0, viewport_r.Bottom()}).y;
            auto const y_clamped = Clamp(cursor.y, min_y, max_y);
            auto const y_t = 1.0f - MapTo01(y_clamped, min_y, max_y);
            auto const new_gain_db = MapFrom01(y_t, filter_display::k_min_db, filter_display::k_max_db);
            auto const gain_linear = gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
            SetParameterValue(engine.processor, ParamIndex::FilterGain, gain_linear, {});
        }
    }

    if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
        ParameterJustStoppedMoving(engine.processor, ParamIndex::FilterCutoff);
        if (uses_gain()) ParameterJustStoppedMoving(engine.processor, ParamIndex::FilterGain);
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

    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const reso_adj_linear =
        AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);
    auto const gain_adj_linear =
        AdjustedLinearValue(params, macro_dests, gain_param.LinearValue(), gain_param.info.index);

    auto const cutoff_adj_hz = cutoff_param.info.ProjectValue(cutoff_adj_linear);
    auto const q_adj = rbj_filter::EffectFilterResonanceToQ(Clamp01(reso_adj_linear));
    // FilterGain stores the audible gain; DSP uses half per pass so two passes → audible = FilterGain.
    auto const gain_adj_db = uses_gain() ? gain_param.info.ProjectValue(gain_adj_linear) / 2 : 0.0f;

    auto const stages = num_stages();
    auto const coeffs = rbj_filter::Coefficients({
        .type = EffectFilterTypeToRbjType(type_param.IntValue<param_values::EffectFilterType>()),
        .fs = filter_display::k_sample_rate,
        .fc = Clamp(cutoff_adj_hz, 15.0f, filter_display::k_sample_rate * 0.49f),
        .q = q_adj,
        .peak_gain = gain_adj_db,
    });

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return (f32)stages * rbj_filter::MagnitudeDb(coeffs, hz, filter_display::k_sample_rate);
        },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* params_arr[] = {&cutoff_param, &reso_param, &gain_param};
    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    filter_display::DrawHandle(imgui,
                               imgui.ViewportPosToWindowPos(node_pos_viewport()),
                               handle_radius,
                               interaction_id,
                               greyed_out);

    if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = uses_gain() ? CursorType::AllArrows : CursorType::HorizontalArrows;

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

// ===============================================================================
// Reverb pre-filter and post-shelf visualisers.
//
// We draw idealised symmetric curves rather than the exact one-pole difference topology used by
// the DSP — that real shape is gentle and asymmetric and reads poorly. The user-facing model is
// "LP cascaded with HP" for the pre-filter and "low-shelf cascaded with high-shelf" for the post,
// each with a 24 dB/octave (4th order) rolloff so handle movement is clearly visible.
//
// X-axis uses ParamIndex::FilterCutoff's descriptor (Hz, log-ish) because the reverb cutoffs are
// stored in MIDI semitones and don't carry an Hz projection.

static f32 SemitonesToHz(f32 semitones) { return 440.0f * Exp2((semitones - 69.0f) / 12.0f); }

constexpr s32 k_reverb_filter_order = 2;

static f32 ButterworthMagDb(f32 ratio) {
    auto const r2 = ratio * ratio;
    auto const r2n = Pow(r2, (f32)k_reverb_filter_order);
    return -10.0f * Log10(1.0f + r2n);
}

static f32 LpMagDb(f32 freq_hz, f32 fc_hz) { return ButterworthMagDb(freq_hz / fc_hz); }
static f32 HpMagDb(f32 freq_hz, f32 fc_hz) { return ButterworthMagDb(fc_hz / freq_hz); }

// Shelf with attenuation only: at the shelf-side limit it sits at gain_db, at the other limit at 0 dB.
static f32 LowShelfMagDb(f32 freq_hz, f32 fc_hz, f32 gain_db) {
    auto const r = freq_hz / fc_hz;
    auto const r2 = r * r;
    auto const r2n = Pow(r2, (f32)k_reverb_filter_order);
    return gain_db / (1.0f + r2n);
}

static f32 HighShelfMagDb(f32 freq_hz, f32 fc_hz, f32 gain_db) {
    auto const r = fc_hz / freq_hz;
    auto const r2 = r * r;
    auto const r2n = Pow(r2, (f32)k_reverb_filter_order);
    return gain_db / (1.0f + r2n);
}

void DoReverbPreFilterVisualizer(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_display::DrawBackground(imgui, viewport_r, freq_info);

    auto const lp_param = params.DescribedValue(ParamIndex::ReverbPreLowPassCutoff);
    auto const hp_param = params.DescribedValue(ParamIndex::ReverbPreHighPassCutoff);

    auto const handle_y = filter_display::DbToY(0.0f, viewport_r);

    struct Grabber {
        ParamIndex index;
        DescribedParamValue const& param;
        u64 seed;
    };
    Grabber const grabbers[] = {
        {ParamIndex::ReverbPreLowPassCutoff, lp_param, 0},
        {ParamIndex::ReverbPreHighPassCutoff, hp_param, 1},
    };

    auto const node_pos_viewport = [&](Grabber const& gr) {
        auto const cutoff_hz = SemitonesToHz(gr.param.LinearValue());
        auto const x_lin = freq_info.LineariseValue(cutoff_hz, true).ValueOr(0.0f);
        return f32x2 {viewport_r.x + (x_lin * viewport_r.w), handle_y};
    };

    struct GrabberFrame {
        imgui::Id interaction_id;
        Rect window_r;
    };
    GrabberFrame frames[ArraySize(grabbers)];
    for (auto const i : Range(ArraySize(grabbers))) {
        auto const& gr = grabbers[i];
        auto const pos = node_pos_viewport(gr);
        Rect const grabber_viewport_r {
            .xywh {pos.x - grabber_radius, pos.y - grabber_radius, grabber_radius * 2, grabber_radius * 2}};
        frames[i] = {
            .interaction_id = imgui.MakeId(SourceLocationHash() ^ gr.seed),
            .window_r = imgui.RegisterAndConvertRect(grabber_viewport_r),
        };
    }

    // Process input before drawing the curve/handles so they reflect this frame's changes.
    static f32x2 rel_click_pos;
    for (auto const i : Range(ArraySize(grabbers))) {
        auto const& gr = grabbers[i];
        auto const interaction_id = frames[i].interaction_id;
        auto const grabber_window_r = frames[i].window_r;

        if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
            rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport(gr));

        if (imgui.ButtonBehaviour(grabber_window_r,
                                  interaction_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = gr.index;

        if (imgui.WasJustActivated(interaction_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, gr.index);

        if (imgui.IsActive(interaction_id, MouseButton::Left)) {
            auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
            auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;
            auto const cursor_x = GuiIo().in.cursor_pos.x - rel_click_pos.x;
            auto const x_clamped = Clamp(cursor_x, min_x, max_x);
            auto const x_t = MapTo01(x_clamped, min_x, max_x);
            auto const target_hz = freq_info.ProjectValue(x_t);
            auto const semitones = Clamp(FrequencyToMidiNote(target_hz),
                                         gr.param.info.linear_range.min,
                                         gr.param.info.linear_range.max);
            SetParameterValue(engine.processor, gr.index, semitones, {});
        }

        if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, gr.index);
    }

    auto const lp_fc_hz = SemitonesToHz(lp_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, lp_param.LinearValue(), lp_param.info.index)));
    auto const hp_fc_hz = SemitonesToHz(hp_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, hp_param.LinearValue(), hp_param.info.index)));

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return LpMagDb(hz, lp_fc_hz) + HpMagDb(hz, hp_fc_hz); },
        freq_info,
        greyed_out);

    for (auto const i : Range(ArraySize(grabbers))) {
        auto const& gr = grabbers[i];
        auto const interaction_id = frames[i].interaction_id;
        auto const grabber_window_r = frames[i].window_r;

        DescribedParamValue const* params_arr[] = {&gr.param};
        ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

        filter_display::DrawHandle(imgui,
                                   imgui.ViewportPosToWindowPos(node_pos_viewport(gr)),
                                   handle_radius,
                                   interaction_id,
                                   greyed_out);

        if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        OverlayMacroDestinationRegion(g, grabber_window_r, gr.index);
    }

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 2> const all_indices {
            ParamIndex::ReverbPreLowPassCutoff,
            ParamIndex::ReverbPreHighPassCutoff,
        };
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}

void DoReverbPostShelfVisualizer(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_display::DrawBackground(imgui, viewport_r, freq_info);

    auto const lo_cut_param = params.DescribedValue(ParamIndex::ReverbLowShelfCutoff);
    auto const lo_gain_param = params.DescribedValue(ParamIndex::ReverbLowShelfGain);
    auto const hi_cut_param = params.DescribedValue(ParamIndex::ReverbHighShelfCutoff);
    auto const hi_gain_param = params.DescribedValue(ParamIndex::ReverbHighShelfGain);

    struct Shelf {
        ParamIndex cutoff_idx;
        ParamIndex gain_idx;
        DescribedParamValue const& cutoff_param;
        DescribedParamValue const& gain_param;
        u64 seed;
    };
    Shelf const shelves[] = {
        {ParamIndex::ReverbLowShelfCutoff, ParamIndex::ReverbLowShelfGain, lo_cut_param, lo_gain_param, 0},
        {ParamIndex::ReverbHighShelfCutoff, ParamIndex::ReverbHighShelfGain, hi_cut_param, hi_gain_param, 1},
    };

    auto const node_pos_viewport = [&](Shelf const& sh) {
        auto const cutoff_hz = SemitonesToHz(sh.cutoff_param.LinearValue());
        auto const x_lin = freq_info.LineariseValue(cutoff_hz, true).ValueOr(0.0f);
        return f32x2 {
            viewport_r.x + (x_lin * viewport_r.w),
            filter_display::DbToY(
                Clamp(sh.gain_param.ProjectedValue(), filter_display::k_min_db, filter_display::k_max_db),
                viewport_r),
        };
    };

    struct ShelfFrame {
        imgui::Id interaction_id;
        Rect window_r;
    };
    ShelfFrame frames[ArraySize(shelves)];
    for (auto const i : Range(ArraySize(shelves))) {
        auto const& sh = shelves[i];
        auto const pos = node_pos_viewport(sh);
        Rect const grabber_viewport_r {
            .xywh {pos.x - grabber_radius, pos.y - grabber_radius, grabber_radius * 2, grabber_radius * 2}};
        frames[i] = {
            .interaction_id = imgui.MakeId(SourceLocationHash() ^ sh.seed),
            .window_r = imgui.RegisterAndConvertRect(grabber_viewport_r),
        };
    }

    // Process input before drawing the curve/handles so they reflect this frame's changes.
    static f32x2 rel_click_pos;
    for (auto const i : Range(ArraySize(shelves))) {
        auto const& sh = shelves[i];
        auto const interaction_id = frames[i].interaction_id;
        auto const grabber_window_r = frames[i].window_r;

        if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
            rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport(sh));

        if (imgui.ButtonBehaviour(grabber_window_r,
                                  interaction_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = sh.cutoff_idx;

        if (imgui.WasJustActivated(interaction_id, MouseButton::Left)) {
            ParameterJustStartedMoving(engine.processor, sh.cutoff_idx);
            ParameterJustStartedMoving(engine.processor, sh.gain_idx);
        }

        if (imgui.IsActive(interaction_id, MouseButton::Left)) {
            auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
            auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;
            auto const min_y = imgui.ViewportPosToWindowPos({0, viewport_r.y}).y;
            auto const max_y = imgui.ViewportPosToWindowPos({0, viewport_r.Bottom()}).y;
            auto const cursor = GuiIo().in.cursor_pos - rel_click_pos;

            auto const x_clamped = Clamp(cursor.x, min_x, max_x);
            auto const x_t = MapTo01(x_clamped, min_x, max_x);
            auto const target_hz = freq_info.ProjectValue(x_t);
            auto const semitones = Clamp(FrequencyToMidiNote(target_hz),
                                         sh.cutoff_param.info.linear_range.min,
                                         sh.cutoff_param.info.linear_range.max);
            SetParameterValue(engine.processor, sh.cutoff_idx, semitones, {});

            auto const y_clamped = Clamp(cursor.y, min_y, max_y);
            auto const y_t = 1.0f - MapTo01(y_clamped, min_y, max_y);
            auto const new_gain_db =
                Clamp(MapFrom01(y_t, filter_display::k_min_db, filter_display::k_max_db), -24.0f, 0.0f);
            auto const gain_lin = sh.gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
            SetParameterValue(engine.processor, sh.gain_idx, gain_lin, {});
        }

        if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, sh.cutoff_idx);
            ParameterJustStoppedMoving(engine.processor, sh.gain_idx);
        }
    }

    auto const projected_adj = [&](DescribedParamValue const& p) {
        return p.info.ProjectValue(AdjustedLinearValue(params, macro_dests, p.LinearValue(), p.info.index));
    };
    auto const lo_fc_hz = SemitonesToHz(projected_adj(lo_cut_param));
    auto const hi_fc_hz = SemitonesToHz(projected_adj(hi_cut_param));
    auto const lo_gain_db = projected_adj(lo_gain_param);
    auto const hi_gain_db = projected_adj(hi_gain_param);

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return LowShelfMagDb(hz, lo_fc_hz, lo_gain_db) + HighShelfMagDb(hz, hi_fc_hz, hi_gain_db);
        },
        freq_info,
        greyed_out);

    for (auto const i : Range(ArraySize(shelves))) {
        auto const& sh = shelves[i];
        auto const interaction_id = frames[i].interaction_id;
        auto const grabber_window_r = frames[i].window_r;

        DescribedParamValue const* params_arr[] = {&sh.cutoff_param, &sh.gain_param};
        ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

        filter_display::DrawHandle(imgui,
                                   imgui.ViewportPosToWindowPos(node_pos_viewport(sh)),
                                   handle_radius,
                                   interaction_id,
                                   greyed_out);

        if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        OverlayMacroDestinationRegion(g, grabber_window_r, sh.cutoff_idx);
    }

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 4> const all_indices {
            ParamIndex::ReverbLowShelfCutoff,
            ParamIndex::ReverbLowShelfGain,
            ParamIndex::ReverbHighShelfCutoff,
            ParamIndex::ReverbHighShelfGain,
        };
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}

// ===============================================================================
// Convolution reverb high-pass visualiser.

void DoConvolutionReverbHighpassVisualizer(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const cutoff_param = params.DescribedValue(ParamIndex::ConvolutionReverbHighpass);

    filter_display::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const node_pos_viewport = [&] {
        return f32x2 {viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
                      filter_display::DbToY(0.0f, viewport_r)};
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    Rect const grabber_viewport_r {.xywh {node_pos_viewport().x - grabber_radius,
                                          node_pos_viewport().y - grabber_radius,
                                          grabber_radius * 2,
                                          grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport());

    if (imgui.ButtonBehaviour(grabber_window_r,
                              interaction_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::DoubleClick,
                              }))
        g.param_text_editor_to_open = ParamIndex::ConvolutionReverbHighpass;

    if (imgui.WasJustActivated(interaction_id, MouseButton::Left))
        ParameterJustStartedMoving(engine.processor, ParamIndex::ConvolutionReverbHighpass);

    if (imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;
        auto const cursor_x = GuiIo().in.cursor_pos.x - rel_click_pos.x;
        auto const x_clamped = Clamp(cursor_x, min_x, max_x);
        auto const new_cutoff_linear = MapTo01(x_clamped, min_x, max_x);
        SetParameterValue(engine.processor, ParamIndex::ConvolutionReverbHighpass, new_cutoff_linear, {});
    }

    if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left))
        ParameterJustStoppedMoving(engine.processor, ParamIndex::ConvolutionReverbHighpass);

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const cutoff_adj_hz = Clamp(cutoff_param.info.ProjectValue(cutoff_adj_linear),
                                     15.0f,
                                     filter_display::k_sample_rate * 0.49f);

    auto const coeffs = rbj_filter::Coefficients({
        .type = rbj_filter::Type::HighPass,
        .fs = filter_display::k_sample_rate,
        .fc = cutoff_adj_hz,
        .q = 1.0f,
        .peak_gain = 0.0f,
    });

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return rbj_filter::MagnitudeDb(coeffs, hz, filter_display::k_sample_rate); },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* params_arr[] = {&cutoff_param};
    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    filter_display::DrawHandle(imgui,
                               imgui.ViewportPosToWindowPos(node_pos_viewport()),
                               handle_radius,
                               interaction_id,
                               greyed_out);

    if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

    OverlayMacroDestinationRegion(g, grabber_window_r, ParamIndex::ConvolutionReverbHighpass);

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 1> const all_indices {ParamIndex::ConvolutionReverbHighpass};
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}

// ===============================================================================
// Delay filter visualiser.
//
// The delay's filter is a bandpass: LP at midiToHz(cutoff + radius), HP at midiToHz(cutoff -
// radius), where radius = spread * 8 octaves. We draw the same idealised 4th-order shape used by
// the reverb pre-filter and let the user drag a single centre handle for cutoff plus scroll for
// spread.

void DoDelayFilterVisualizer(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_display::DrawBackground(imgui, viewport_r, freq_info);

    auto const cutoff_param = params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones);
    auto const spread_param = params.DescribedValue(ParamIndex::DelayFilterSpread);

    auto const node_pos_viewport = [&] {
        auto const cutoff_hz = SemitonesToHz(cutoff_param.LinearValue());
        auto const x_lin = freq_info.LineariseValue(cutoff_hz, true).ValueOr(0.0f);
        return f32x2 {viewport_r.x + (x_lin * viewport_r.w),
                      viewport_r.Bottom() - (spread_param.LinearValue() * viewport_r.h)};
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());

    Rect const grabber_viewport_r {.xywh {node_pos_viewport().x - grabber_radius,
                                          node_pos_viewport().y - grabber_radius,
                                          grabber_radius * 2,
                                          grabber_radius * 2}};
    auto const grabber_window_r = imgui.RegisterAndConvertRect(grabber_viewport_r);

    // Process input before drawing the curve/handle so they reflect this frame's changes.
    static f32x2 rel_click_pos;
    if (imgui.ButtonBehaviour(grabber_window_r, interaction_id, imgui::SliderConfig::k_activation_cfg))
        rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(node_pos_viewport());

    if (imgui.ButtonBehaviour(grabber_window_r,
                              interaction_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::DoubleClick,
                              }))
        g.param_text_editor_to_open = ParamIndex::DelayFilterCutoffSemitones;

    if (imgui.WasJustActivated(interaction_id, MouseButton::Left)) {
        ParameterJustStartedMoving(engine.processor, ParamIndex::DelayFilterCutoffSemitones);
        ParameterJustStartedMoving(engine.processor, ParamIndex::DelayFilterSpread);
    }

    if (imgui.IsActive(interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({viewport_r.Right(), 0}).x;
        auto const min_y = imgui.ViewportPosToWindowPos({0, viewport_r.y}).y;
        auto const max_y = imgui.ViewportPosToWindowPos({0, viewport_r.Bottom()}).y;
        auto const cursor = GuiIo().in.cursor_pos - rel_click_pos;

        auto const x_clamped = Clamp(cursor.x, min_x, max_x);
        auto const x_t = MapTo01(x_clamped, min_x, max_x);
        auto const target_hz = freq_info.ProjectValue(x_t);
        auto const semitones = Clamp(FrequencyToMidiNote(target_hz),
                                     cutoff_param.info.linear_range.min,
                                     cutoff_param.info.linear_range.max);
        SetParameterValue(engine.processor, ParamIndex::DelayFilterCutoffSemitones, semitones, {});

        auto const y_clamped = Clamp(cursor.y, min_y, max_y);
        auto const new_spread = 1.0f - MapTo01(y_clamped, min_y, max_y);
        SetParameterValue(engine.processor, ParamIndex::DelayFilterSpread, new_spread, {});
    }

    if (imgui.WasJustDeactivated(interaction_id, MouseButton::Left)) {
        ParameterJustStoppedMoving(engine.processor, ParamIndex::DelayFilterCutoffSemitones);
        ParameterJustStoppedMoving(engine.processor, ParamIndex::DelayFilterSpread);
    }

    auto const cutoff_adj_sem = cutoff_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index));
    auto const spread_adj = spread_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, spread_param.LinearValue(), spread_param.info.index));

    constexpr f32 k_spread_octaves = 8;
    auto const radius_sem = spread_adj * k_spread_octaves * 12.0f;
    auto const lp_fc_hz = SemitonesToHz(cutoff_adj_sem + radius_sem);
    auto const hp_fc_hz = SemitonesToHz(cutoff_adj_sem - radius_sem);

    filter_display::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return LpMagDb(hz, lp_fc_hz) + HpMagDb(hz, hp_fc_hz); },
        freq_info,
        greyed_out);

    DescribedParamValue const* params_arr[] = {&cutoff_param, &spread_param};
    ParameterValuePopup(g, params_arr, interaction_id, grabber_window_r);

    filter_display::DrawHandle(imgui,
                               imgui.ViewportPosToWindowPos(node_pos_viewport()),
                               handle_radius,
                               interaction_id,
                               greyed_out);

    if (imgui.IsHotOrActive(interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = CursorType::AllArrows;

    OverlayMacroDestinationRegion(g, grabber_window_r, ParamIndex::DelayFilterCutoffSemitones);

    if (g.param_text_editor_to_open) {
        Array<ParamIndex, 2> const all_indices {
            ParamIndex::DelayFilterCutoffSemitones,
            ParamIndex::DelayFilterSpread,
        };
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, all_indices);
    }
}
