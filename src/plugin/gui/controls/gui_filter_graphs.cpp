// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_filter_graphs.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_filter_graph_draw.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "processing_utils/filters.hpp"
#include "processor/effect_filter_iir.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr f32 k_handle_radius_ww = 5.0f;
constexpr f32 k_grabber_radius_ww = 12.0f;

// =============================================================================
// Shared building blocks. Each visualiser assembles itself from these.

static Rect MakeGrabberWindowRect(imgui::Context& imgui, f32x2 node_pos_viewport, f32 grabber_radius) {
    Rect const viewport_r {.xywh {node_pos_viewport.x - grabber_radius,
                                  node_pos_viewport.y - grabber_radius,
                                  grabber_radius * 2,
                                  grabber_radius * 2}};
    return imgui.RegisterAndConvertRect(viewport_r);
}

struct GrabberDragOptions {
    Rect viewport_r;
    Rect grabber_window_r;
    imgui::Id interaction_id;
    f32x2 node_pos_viewport;
    Span<ParamIndex const> moving_params;
    ParamIndex double_click_target;
    bool track_y_axis;
    TrivialFunctionRef<void(f32 x_t, f32 y_t)> on_drag;
    String drag_name;
};

// One global suffices: only one grabber can be dragged at a time.
static f32x2 g_grabber_rel_click_pos;

static void RunGrabberDrag(GuiState& g, GrabberDragOptions const& opt) {
    auto& imgui = g.imgui;
    auto& processor = g.engine.processor;

    if (imgui.ButtonBehaviour(opt.grabber_window_r,
                              opt.interaction_id,
                              imgui::SliderConfig::k_activation_cfg))
        g_grabber_rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(opt.node_pos_viewport);

    if (imgui.ButtonBehaviour(opt.grabber_window_r,
                              opt.interaction_id,
                              {.mouse_button = MouseButton::Left, .event = MouseButtonEvent::DoubleClick}))
        g.param_text_editor_to_open = opt.double_click_target;

    if (imgui.WasJustActivated(opt.interaction_id, MouseButton::Left)) {
        BeginUndoableStep(g.engine, opt.drag_name);
        for (auto const p : opt.moving_params)
            ParameterJustStartedMoving(processor, p);
    }

    if (imgui.IsActive(opt.interaction_id, MouseButton::Left)) {
        auto const min_x = imgui.ViewportPosToWindowPos({opt.viewport_r.x, 0}).x;
        auto const max_x = imgui.ViewportPosToWindowPos({opt.viewport_r.Right(), 0}).x;
        auto const cursor = GuiIo().in.cursor_pos - g_grabber_rel_click_pos;
        auto const x_t = MapTo01(Clamp(cursor.x, min_x, max_x), min_x, max_x);

        f32 y_t = 0;
        if (opt.track_y_axis) {
            auto const min_y = imgui.ViewportPosToWindowPos({0, opt.viewport_r.y}).y;
            auto const max_y = imgui.ViewportPosToWindowPos({0, opt.viewport_r.Bottom()}).y;
            y_t = 1.0f - MapTo01(Clamp(cursor.y, min_y, max_y), min_y, max_y);
        }
        opt.on_drag(x_t, y_t);
    }

    if (imgui.WasJustDeactivated(opt.interaction_id, MouseButton::Left)) {
        for (auto const p : opt.moving_params)
            ParameterJustStoppedMoving(processor, p);
        EndUndoableStep(g.engine);
    }
}

static void DoScrollResonance(GuiState& g,
                              imgui::Id interaction_id,
                              Rect grabber_window_r,
                              ParamIndex reso_index,
                              f32 current_linear) {
    g.imgui.ConsumeScrollAtRect(grabber_window_r);
    if (!g.imgui.IsHot(interaction_id)) return;
    auto const scroll = GuiIo().in.mouse_scroll_delta_in_lines;
    if (scroll == 0) return;
    auto const new_reso = Clamp01(current_linear + (scroll * 0.03f));
    SetParameterValue(g.engine.processor, reso_index, new_reso, {});
}

struct GrabberDrawOptions {
    f32x2 node_pos_viewport;
    f32 handle_radius;
    imgui::Id interaction_id;
    Rect grabber_window_r;
    Span<DescribedParamValue const*> popup_params;
    bool greyed_out;
    CursorType active_cursor;
};

static void DrawGrabberHandleAndPopup(GuiState& g, GrabberDrawOptions const& opt) {
    ParameterValuePopup(g, opt.popup_params, opt.interaction_id, opt.grabber_window_r);
    filter_graph_draw::DrawHandle(g.imgui,
                                  g.imgui.ViewportPosToWindowPos(opt.node_pos_viewport),
                                  opt.handle_radius,
                                  opt.interaction_id,
                                  opt.greyed_out);
    if (g.imgui.IsHotOrActive(opt.interaction_id, MouseButton::Left))
        GuiIo().out.wants.cursor_type = opt.active_cursor;
}

static void DoParamTextEditorOverlay(GuiState& g, Rect viewport_r, Span<ParamIndex const> indices) {
    if (!g.param_text_editor_to_open) return;
    auto const cut = viewport_r.w / 3;
    Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
    HandleShowingTextEditorForParams(g, edit_r, indices);
}

static f32 SemitonesToHz(f32 semitones) { return 440.0f * Exp2((semitones - 69.0f) / 12.0f); }

// X-axis position for a Hz cutoff stored as MIDI semitones, mapped through the standard FilterCutoff
// descriptor.
static f32 SemitoneCutoffXViewport(Rect viewport_r, ParamDescriptor const& freq_info, f32 semitones) {
    auto const x_lin = freq_info.LineariseValue(SemitonesToHz(semitones), true).ValueOr(0.0f);
    return viewport_r.x + (x_lin * viewport_r.w);
}

// Inverse: take an x_t in [0,1] from drag, project to Hz via freq_info, convert to semitones, clamp.
static f32 SemitonesFromXt(f32 x_t, ParamDescriptor const& freq_info, ParamDescriptor const& target_info) {
    auto const target_hz = freq_info.ProjectValue(x_t);
    return Clamp(FrequencyToMidiNote(target_hz), target_info.linear_range.min, target_info.linear_range.max);
}

// =============================================================================
// Layer filter visualiser.

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

void DoFilterGraph(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out) {
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

    filter_graph_draw::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const node_pos = [&] {
        return f32x2 {viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
                      filter_graph_draw::DbToY(0.0f, viewport_r)};
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());
    auto const grabber_window_r = MakeGrabberWindowRect(imgui, node_pos(), grabber_radius);

    ParamIndex const moving[] = {cutoff_index};
    RunGrabberDrag(
        g,
        {
            .viewport_r = viewport_r,
            .grabber_window_r = grabber_window_r,
            .interaction_id = interaction_id,
            .node_pos_viewport = node_pos(),
            .moving_params = moving,
            .double_click_target = cutoff_index,
            .track_y_axis = false,
            .on_drag = [&](f32 x_t, f32) { SetParameterValue(engine.processor, cutoff_index, x_t, {}); },
            .drag_name = "Layer filter node"_s,
        });

    DoScrollResonance(g, interaction_id, grabber_window_r, reso_index, reso_param.LinearValue());

    DoFilterTypeRightClickMenu(g, grabber_window_r, interaction_id, layer_index);

    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const reso_adj_linear =
        AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index);

    auto const sv_type = MapLayerFilterType(type_param.IntValue<param_values::LayerFilterType>());

    auto const cutoff_adj_hz = Clamp(cutoff_param.info.ProjectValue(cutoff_adj_linear),
                                     15.0f,
                                     filter_graph_draw::k_sample_rate * 0.49f);
    // Clamp to just below 1 to avoid a divide-by-zero in ResonanceToQ at exactly 1.
    auto const res_adj = Clamp(reso_adj_linear, 0.0f, 0.9999f);

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return sv_filter::MagnitudeDb(sv_type,
                                          cutoff_adj_hz,
                                          filter_graph_draw::k_sample_rate,
                                          res_adj,
                                          hz);
        },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* popup_params[] = {&cutoff_param, &reso_param};
    DrawGrabberHandleAndPopup(g,
                              {
                                  .node_pos_viewport = node_pos(),
                                  .handle_radius = handle_radius,
                                  .interaction_id = interaction_id,
                                  .grabber_window_r = grabber_window_r,
                                  .popup_params = popup_params,
                                  .greyed_out = greyed_out,
                                  .active_cursor = CursorType::HorizontalArrows,
                              });

    ParamIndex const editor_indices[] = {
        cutoff_index,
        reso_index,
        ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterType),
    };
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

// =============================================================================
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

void DoEffectFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out) {
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

    filter_graph_draw::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const uses_gain =
        param_values::EffectFilterTypeUsesGain(type_param.IntValue<param_values::EffectFilterType>());
    auto const stages = EffectFilterStageCount(type_param.IntValue<param_values::EffectFilterType>());

    auto const node_pos = [&] {
        return f32x2 {
            viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
            uses_gain ? filter_graph_draw::DbToY(Clamp(gain_param.ProjectedValue(),
                                                       filter_graph_draw::k_min_db,
                                                       filter_graph_draw::k_max_db),
                                                 viewport_r)
                      : filter_graph_draw::DbToY(0.0f, viewport_r),
        };
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());
    auto const grabber_window_r = MakeGrabberWindowRect(imgui, node_pos(), grabber_radius);

    ParamIndex const moving_with_gain[] = {ParamIndex::FilterCutoff, ParamIndex::FilterGain};
    ParamIndex const moving_no_gain[] = {ParamIndex::FilterCutoff};
    RunGrabberDrag(
        g,
        {
            .viewport_r = viewport_r,
            .grabber_window_r = grabber_window_r,
            .interaction_id = interaction_id,
            .node_pos_viewport = node_pos(),
            .moving_params = uses_gain ? Span<ParamIndex const> {moving_with_gain}
                                       : Span<ParamIndex const> {moving_no_gain},
            .double_click_target = ParamIndex::FilterCutoff,
            .track_y_axis = uses_gain,
            .on_drag =
                [&](f32 x_t, f32 y_t) {
                    SetParameterValue(engine.processor, ParamIndex::FilterCutoff, x_t, {});
                    if (uses_gain) {
                        auto const new_gain_db =
                            MapFrom01(y_t, filter_graph_draw::k_min_db, filter_graph_draw::k_max_db);
                        auto const gain_linear =
                            gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
                        SetParameterValue(engine.processor, ParamIndex::FilterGain, gain_linear, {});
                    }
                },
            .drag_name = "Filter node"_s,
        });

    DoScrollResonance(g,
                      interaction_id,
                      grabber_window_r,
                      ParamIndex::FilterResonance,
                      reso_param.LinearValue());

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
    auto const gain_adj_db = uses_gain ? gain_param.info.ProjectValue(gain_adj_linear) / 2 : 0.0f;

    auto const coeffs = rbj_filter::Coefficients({
        .type = EffectFilterTypeToRbjType(type_param.IntValue<param_values::EffectFilterType>()),
        .fs = filter_graph_draw::k_sample_rate,
        .fc = Clamp(cutoff_adj_hz, 15.0f, filter_graph_draw::k_sample_rate * 0.49f),
        .q = q_adj,
        .peak_gain = gain_adj_db,
    });

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return (f32)stages * rbj_filter::MagnitudeDb(coeffs, hz, filter_graph_draw::k_sample_rate);
        },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* popup_params[] = {&cutoff_param, &reso_param, &gain_param};
    DrawGrabberHandleAndPopup(
        g,
        {
            .node_pos_viewport = node_pos(),
            .handle_radius = handle_radius,
            .interaction_id = interaction_id,
            .grabber_window_r = grabber_window_r,
            .popup_params = popup_params,
            .greyed_out = greyed_out,
            .active_cursor = uses_gain ? CursorType::AllArrows : CursorType::HorizontalArrows,
        });

    ParamIndex const editor_indices[] = {
        ParamIndex::FilterCutoff,
        ParamIndex::FilterResonance,
        ParamIndex::FilterGain,
        ParamIndex::FilterType,
    };
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

// =============================================================================
// Reverb pre-filter and post-shelf visualisers.
//
// We draw idealised symmetric curves rather than the exact one-pole difference topology used by
// the DSP — that real shape is gentle and asymmetric and reads poorly. The user-facing model is
// "LP cascaded with HP" for the pre-filter and "low-shelf cascaded with high-shelf" for the post,
// each with a 24 dB/octave (4th order) rolloff so handle movement is clearly visible.
//
// X-axis uses ParamIndex::FilterCutoff's descriptor (Hz, log-ish) because the reverb cutoffs are
// stored in MIDI semitones and don't carry an Hz projection.

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

void DoReverbPreFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_graph_draw::DrawBackground(imgui, viewport_r, freq_info);

    auto const lp_param = params.DescribedValue(ParamIndex::ReverbPreLowPassCutoff);
    auto const hp_param = params.DescribedValue(ParamIndex::ReverbPreHighPassCutoff);

    auto const handle_y = filter_graph_draw::DbToY(0.0f, viewport_r);

    struct Grabber {
        ParamIndex index;
        DescribedParamValue const& param;
        u64 seed;
        imgui::Id interaction_id;
        Rect window_r;
    };
    auto const node_pos_for = [&](Grabber const& gr) {
        return f32x2 {SemitoneCutoffXViewport(viewport_r, freq_info, gr.param.LinearValue()), handle_y};
    };
    Grabber grabbers[] = {
        {.index = ParamIndex::ReverbPreLowPassCutoff, .param = lp_param, .seed = 0},
        {.index = ParamIndex::ReverbPreHighPassCutoff, .param = hp_param, .seed = 1},
    };
    for (auto& gr : grabbers) {
        gr.interaction_id = imgui.MakeId(SourceLocationHash() ^ gr.seed);
        gr.window_r = MakeGrabberWindowRect(imgui, node_pos_for(gr), grabber_radius);
    }

    for (auto const& gr : grabbers) {
        ParamIndex const moving[] = {gr.index};
        RunGrabberDrag(g,
                       {
                           .viewport_r = viewport_r,
                           .grabber_window_r = gr.window_r,
                           .interaction_id = gr.interaction_id,
                           .node_pos_viewport = node_pos_for(gr),
                           .moving_params = moving,
                           .double_click_target = gr.index,
                           .track_y_axis = false,
                           .on_drag =
                               [&](f32 x_t, f32) {
                                   SetParameterValue(engine.processor,
                                                     gr.index,
                                                     SemitonesFromXt(x_t, freq_info, gr.param.info),
                                                     {});
                               },
                           .drag_name = "Filter node"_s,
                       });
    }

    auto const lp_fc_hz = SemitonesToHz(lp_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, lp_param.LinearValue(), lp_param.info.index)));
    auto const hp_fc_hz = SemitonesToHz(hp_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, hp_param.LinearValue(), hp_param.info.index)));

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return LpMagDb(hz, lp_fc_hz) + HpMagDb(hz, hp_fc_hz); },
        freq_info,
        greyed_out);

    for (auto const& gr : grabbers) {
        DescribedParamValue const* popup_params[] = {&gr.param};
        DrawGrabberHandleAndPopup(g,
                                  {
                                      .node_pos_viewport = node_pos_for(gr),
                                      .handle_radius = handle_radius,
                                      .interaction_id = gr.interaction_id,
                                      .grabber_window_r = gr.window_r,
                                      .popup_params = popup_params,
                                      .greyed_out = greyed_out,
                                      .active_cursor = CursorType::HorizontalArrows,
                                  });
    }

    ParamIndex const editor_indices[] = {
        ParamIndex::ReverbPreLowPassCutoff,
        ParamIndex::ReverbPreHighPassCutoff,
    };
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

void DoReverbPostShelfGraph(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_graph_draw::DrawBackground(imgui, viewport_r, freq_info);

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
        imgui::Id interaction_id;
        Rect window_r;
    };
    auto const node_pos_for = [&](Shelf const& sh) {
        return f32x2 {
            SemitoneCutoffXViewport(viewport_r, freq_info, sh.cutoff_param.LinearValue()),
            filter_graph_draw::DbToY(Clamp(sh.gain_param.ProjectedValue(),
                                           filter_graph_draw::k_min_db,
                                           filter_graph_draw::k_max_db),
                                     viewport_r),
        };
    };
    Shelf shelves[] = {
        {.cutoff_idx = ParamIndex::ReverbLowShelfCutoff,
         .gain_idx = ParamIndex::ReverbLowShelfGain,
         .cutoff_param = lo_cut_param,
         .gain_param = lo_gain_param,
         .seed = 0},
        {.cutoff_idx = ParamIndex::ReverbHighShelfCutoff,
         .gain_idx = ParamIndex::ReverbHighShelfGain,
         .cutoff_param = hi_cut_param,
         .gain_param = hi_gain_param,
         .seed = 1},
    };
    for (auto& sh : shelves) {
        sh.interaction_id = imgui.MakeId(SourceLocationHash() ^ sh.seed);
        sh.window_r = MakeGrabberWindowRect(imgui, node_pos_for(sh), grabber_radius);
    }

    for (auto const& sh : shelves) {
        ParamIndex const moving[] = {sh.cutoff_idx, sh.gain_idx};
        RunGrabberDrag(
            g,
            {
                .viewport_r = viewport_r,
                .grabber_window_r = sh.window_r,
                .interaction_id = sh.interaction_id,
                .node_pos_viewport = node_pos_for(sh),
                .moving_params = moving,
                .double_click_target = sh.cutoff_idx,
                .track_y_axis = true,
                .on_drag =
                    [&](f32 x_t, f32 y_t) {
                        SetParameterValue(engine.processor,
                                          sh.cutoff_idx,
                                          SemitonesFromXt(x_t, freq_info, sh.cutoff_param.info),
                                          {});
                        auto const new_gain_db =
                            Clamp(MapFrom01(y_t, filter_graph_draw::k_min_db, filter_graph_draw::k_max_db),
                                  -24.0f,
                                  0.0f);
                        auto const gain_lin =
                            sh.gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
                        SetParameterValue(engine.processor, sh.gain_idx, gain_lin, {});
                    },
                .drag_name = "Filter node"_s,
            });
    }

    auto const projected_adj = [&](DescribedParamValue const& p) {
        return p.info.ProjectValue(AdjustedLinearValue(params, macro_dests, p.LinearValue(), p.info.index));
    };
    auto const lo_fc_hz = SemitonesToHz(projected_adj(lo_cut_param));
    auto const hi_fc_hz = SemitonesToHz(projected_adj(hi_cut_param));
    auto const lo_gain_db = projected_adj(lo_gain_param);
    auto const hi_gain_db = projected_adj(hi_gain_param);

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            return LowShelfMagDb(hz, lo_fc_hz, lo_gain_db) + HighShelfMagDb(hz, hi_fc_hz, hi_gain_db);
        },
        freq_info,
        greyed_out);

    for (auto const& sh : shelves) {
        DescribedParamValue const* popup_params[] = {&sh.cutoff_param, &sh.gain_param};
        DrawGrabberHandleAndPopup(g,
                                  {
                                      .node_pos_viewport = node_pos_for(sh),
                                      .handle_radius = handle_radius,
                                      .interaction_id = sh.interaction_id,
                                      .grabber_window_r = sh.window_r,
                                      .popup_params = popup_params,
                                      .greyed_out = greyed_out,
                                      .active_cursor = CursorType::AllArrows,
                                  });
    }

    ParamIndex const editor_indices[] = {
        ParamIndex::ReverbLowShelfCutoff,
        ParamIndex::ReverbLowShelfGain,
        ParamIndex::ReverbHighShelfCutoff,
        ParamIndex::ReverbHighShelfGain,
    };
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

// =============================================================================
// Convolution reverb high-pass visualiser.

void DoConvolutionReverbHighpassGraph(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const cutoff_param = params.DescribedValue(ParamIndex::ConvolutionReverbHighpass);

    filter_graph_draw::DrawBackground(imgui, viewport_r, cutoff_param.info);

    auto const node_pos = [&] {
        return f32x2 {viewport_r.x + (cutoff_param.LinearValue() * viewport_r.w),
                      filter_graph_draw::DbToY(0.0f, viewport_r)};
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());
    auto const grabber_window_r = MakeGrabberWindowRect(imgui, node_pos(), grabber_radius);

    ParamIndex const moving[] = {ParamIndex::ConvolutionReverbHighpass};
    RunGrabberDrag(
        g,
        {
            .viewport_r = viewport_r,
            .grabber_window_r = grabber_window_r,
            .interaction_id = interaction_id,
            .node_pos_viewport = node_pos(),
            .moving_params = moving,
            .double_click_target = ParamIndex::ConvolutionReverbHighpass,
            .track_y_axis = false,
            .on_drag =
                [&](f32 x_t, f32) {
                    SetParameterValue(engine.processor, ParamIndex::ConvolutionReverbHighpass, x_t, {});
                },
            .drag_name = "Reverb highpass node"_s,
        });

    auto const cutoff_adj_linear =
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index);
    auto const cutoff_adj_hz = Clamp(cutoff_param.info.ProjectValue(cutoff_adj_linear),
                                     15.0f,
                                     filter_graph_draw::k_sample_rate * 0.49f);

    auto const coeffs = rbj_filter::Coefficients({
        .type = rbj_filter::Type::HighPass,
        .fs = filter_graph_draw::k_sample_rate,
        .fc = cutoff_adj_hz,
        .q = 1.0f,
        .peak_gain = 0.0f,
    });

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return rbj_filter::MagnitudeDb(coeffs, hz, filter_graph_draw::k_sample_rate); },
        cutoff_param.info,
        greyed_out);

    DescribedParamValue const* popup_params[] = {&cutoff_param};
    DrawGrabberHandleAndPopup(g,
                              {
                                  .node_pos_viewport = node_pos(),
                                  .handle_radius = handle_radius,
                                  .interaction_id = interaction_id,
                                  .grabber_window_r = grabber_window_r,
                                  .popup_params = popup_params,
                                  .greyed_out = greyed_out,
                                  .active_cursor = CursorType::HorizontalArrows,
                              });

    ParamIndex const editor_indices[] = {ParamIndex::ConvolutionReverbHighpass};
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

// =============================================================================
// Delay filter visualiser.
//
// The delay's filter is a bandpass: LP at midiToHz(cutoff + radius), HP at midiToHz(cutoff -
// radius), where radius = spread * 8 octaves. We draw the same idealised 4th-order shape used by
// the reverb pre-filter and let the user drag a single centre handle for cutoff plus scroll for
// spread.

void DoDelayFilterGraph(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    imgui.PushId(SourceLocationHash());
    DEFER { imgui.PopId(); };

    auto const& freq_info = k_param_descriptors[ToInt(ParamIndex::FilterCutoff)];
    filter_graph_draw::DrawBackground(imgui, viewport_r, freq_info);

    auto const cutoff_param = params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones);
    auto const spread_param = params.DescribedValue(ParamIndex::DelayFilterSpread);

    auto const node_pos = [&] {
        return f32x2 {
            SemitoneCutoffXViewport(viewport_r, freq_info, cutoff_param.LinearValue()),
            viewport_r.Bottom() - (spread_param.LinearValue() * viewport_r.h),
        };
    };

    auto const interaction_id = imgui.MakeId(SourceLocationHash());
    auto const grabber_window_r = MakeGrabberWindowRect(imgui, node_pos(), grabber_radius);

    ParamIndex const moving[] = {ParamIndex::DelayFilterCutoffSemitones, ParamIndex::DelayFilterSpread};
    RunGrabberDrag(g,
                   {
                       .viewport_r = viewport_r,
                       .grabber_window_r = grabber_window_r,
                       .interaction_id = interaction_id,
                       .node_pos_viewport = node_pos(),
                       .moving_params = moving,
                       .double_click_target = ParamIndex::DelayFilterCutoffSemitones,
                       .track_y_axis = true,
                       .on_drag =
                           [&](f32 x_t, f32 y_t) {
                               SetParameterValue(engine.processor,
                                                 ParamIndex::DelayFilterCutoffSemitones,
                                                 SemitonesFromXt(x_t, freq_info, cutoff_param.info),
                                                 {});
                               SetParameterValue(engine.processor, ParamIndex::DelayFilterSpread, y_t, {});
                           },
                       .drag_name = "Delay filter node"_s,
                   });

    auto const cutoff_adj_sem = cutoff_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, cutoff_param.LinearValue(), cutoff_param.info.index));
    auto const spread_adj = spread_param.info.ProjectValue(
        AdjustedLinearValue(params, macro_dests, spread_param.LinearValue(), spread_param.info.index));

    constexpr f32 k_spread_octaves = 8;
    auto const radius_sem = spread_adj * k_spread_octaves * 12.0f;
    auto const lp_fc_hz = SemitonesToHz(cutoff_adj_sem + radius_sem);
    auto const hp_fc_hz = SemitonesToHz(cutoff_adj_sem - radius_sem);

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) { return LpMagDb(hz, lp_fc_hz) + HpMagDb(hz, hp_fc_hz); },
        freq_info,
        greyed_out);

    DescribedParamValue const* popup_params[] = {&cutoff_param, &spread_param};
    DrawGrabberHandleAndPopup(g,
                              {
                                  .node_pos_viewport = node_pos(),
                                  .handle_radius = handle_radius,
                                  .interaction_id = interaction_id,
                                  .grabber_window_r = grabber_window_r,
                                  .popup_params = popup_params,
                                  .greyed_out = greyed_out,
                                  .active_cursor = CursorType::AllArrows,
                              });

    ParamIndex const editor_indices[] = {
        ParamIndex::DelayFilterCutoffSemitones,
        ParamIndex::DelayFilterSpread,
    };
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}

// =============================================================================
// Per-layer EQ graph: 3 bands, one draggable handle per band, summed response.

struct EqBandParams {
    LayerParamIndex type;
    LayerParamIndex freq;
    LayerParamIndex reso;
    LayerParamIndex gain;
};

static constexpr Array<EqBandParams, k_num_eq_bands> k_eq_band_params = {{
    {LayerParamIndex::EqType1,
     LayerParamIndex::EqFreq1,
     LayerParamIndex::EqResonance1,
     LayerParamIndex::EqGain1},
    {LayerParamIndex::EqType2,
     LayerParamIndex::EqFreq2,
     LayerParamIndex::EqResonance2,
     LayerParamIndex::EqGain2},
    {LayerParamIndex::EqType3,
     LayerParamIndex::EqFreq3,
     LayerParamIndex::EqResonance3,
     LayerParamIndex::EqGain3},
}};

static void DoEqBandRightClickMenu(GuiState& g,
                                   Rect window_r,
                                   imgui::Id interaction_id,
                                   u8 layer_index,
                                   EqBandParams const& bp,
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

void DoEqGraph(GuiState& g, u8 layer_index, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const& macro_dests = engine.processor.main_macro_destinations;

    auto const handle_radius = WwToPixels(k_handle_radius_ww);
    auto const grabber_radius = WwToPixels(k_grabber_radius_ww);

    auto push_id = HashInit();
    HashUpdate(push_id, SourceLocationHash());
    HashUpdate(push_id, layer_index);
    imgui.PushId(push_id);
    DEFER { imgui.PopId(); };

    auto const& freq_info =
        k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::EqFreq1))];

    filter_graph_draw::DrawBackground(imgui, viewport_r, freq_info);

    struct Band {
        EqBandParams params;
        rbj_filter::Coeffs coeffs;
        u32 num_stages;
        bool uses_gain;
        imgui::Id interaction_id;
        Rect window_r;
    };
    Array<Band, k_num_eq_bands> bands {};

    // Handle Y reflects the *base* (user-set) gain so it doesn't drift while macros modulate the curve.
    auto const node_pos_for = [&](Band const& b) {
        auto const freq_param = params.DescribedValue(layer_index, b.params.freq);
        auto const gain_param = params.DescribedValue(layer_index, b.params.gain);
        return f32x2 {
            viewport_r.x + (freq_param.LinearValue() * viewport_r.w),
            b.uses_gain ? filter_graph_draw::DbToY(gain_param.ProjectedValue(), viewport_r)
                        : filter_graph_draw::DbToY(0.0f, viewport_r),
        };
    };

    for (auto const band_idx : Range(k_num_eq_bands)) {
        auto& b = bands[band_idx];
        b.params = k_eq_band_params[band_idx];

        auto const freq_param = params.DescribedValue(layer_index, b.params.freq);
        auto const reso_param = params.DescribedValue(layer_index, b.params.reso);
        auto const gain_param = params.DescribedValue(layer_index, b.params.gain);
        auto const type_param = params.DescribedValue(layer_index, b.params.type);

        auto const eq_type = type_param.IntValue<param_values::EqType>();
        b.uses_gain = param_values::EqTypeUsesGain(eq_type);
        b.num_stages = EqTypeStageCount(eq_type);

        auto const freq_adj_hz = freq_param.info.ProjectValue(
            AdjustedLinearValue(params, macro_dests, freq_param.LinearValue(), freq_param.info.index));
        auto const gain_adj_db = gain_param.info.ProjectValue(
            AdjustedLinearValue(params, macro_dests, gain_param.LinearValue(), gain_param.info.index));
        auto const q_adj = rbj_filter::EqResonanceToQ(
            AdjustedLinearValue(params, macro_dests, reso_param.LinearValue(), reso_param.info.index));

        b.coeffs = rbj_filter::Coefficients({
            .type = EqTypeToRbjType(eq_type),
            .fs = filter_graph_draw::k_sample_rate,
            .fc = Clamp(freq_adj_hz, 15.0f, filter_graph_draw::k_sample_rate * 0.49f),
            .q = q_adj,
            .peak_gain = gain_adj_db,
        });

        b.interaction_id = imgui.MakeId(SourceLocationHash() + band_idx);
        b.window_r = MakeGrabberWindowRect(imgui, node_pos_for(b), grabber_radius);
    }

    for (auto const band_idx : Range(k_num_eq_bands)) {
        auto const& b = bands[band_idx];
        auto const freq_index = ParamIndexFromLayerParamIndex(layer_index, b.params.freq);
        auto const gain_index = ParamIndexFromLayerParamIndex(layer_index, b.params.gain);
        auto const reso_index = ParamIndexFromLayerParamIndex(layer_index, b.params.reso);
        auto const reso_param = params.DescribedValue(layer_index, b.params.reso);
        auto const gain_param = params.DescribedValue(layer_index, b.params.gain);

        ParamIndex const moving_with_gain[] = {freq_index, gain_index};
        ParamIndex const moving_no_gain[] = {freq_index};
        RunGrabberDrag(g,
                       {
                           .viewport_r = viewport_r,
                           .grabber_window_r = b.window_r,
                           .interaction_id = b.interaction_id,
                           .node_pos_viewport = node_pos_for(b),
                           .moving_params = b.uses_gain ? Span<ParamIndex const> {moving_with_gain}
                                                        : Span<ParamIndex const> {moving_no_gain},
                           .double_click_target = freq_index,
                           .track_y_axis = b.uses_gain,
                           .on_drag =
                               [&](f32 x_t, f32 y_t) {
                                   SetParameterValue(engine.processor, freq_index, x_t, {});
                                   if (b.uses_gain) {
                                       auto const new_gain_db = MapFrom01(y_t,
                                                                          filter_graph_draw::k_min_db,
                                                                          filter_graph_draw::k_max_db);
                                       auto const gain_linear =
                                           gain_param.info.LineariseValue(new_gain_db, true).ValueOr(0.0f);
                                       SetParameterValue(engine.processor, gain_index, gain_linear, {});
                                   }
                               },
                           .drag_name = "EQ band node"_s,
                       });

        DoScrollResonance(g, b.interaction_id, b.window_r, reso_index, reso_param.LinearValue());

        DoEqBandRightClickMenu(g, b.window_r, b.interaction_id, layer_index, b.params, (u8)(band_idx + 1));
    }

    filter_graph_draw::DrawResponseCurve(
        imgui,
        viewport_r,
        [&](f32 hz) {
            f32 db = 0;
            for (auto const& b : bands)
                db += (f32)b.num_stages *
                      rbj_filter::MagnitudeDb(b.coeffs, hz, filter_graph_draw::k_sample_rate);
            return db;
        },
        freq_info,
        greyed_out);

    for (auto const& b : bands) {
        auto const freq_param = params.DescribedValue(layer_index, b.params.freq);
        auto const gain_param = params.DescribedValue(layer_index, b.params.gain);
        auto const reso_param = params.DescribedValue(layer_index, b.params.reso);
        DescribedParamValue const* popup_params[] = {&freq_param, &gain_param, &reso_param};
        DrawGrabberHandleAndPopup(
            g,
            {
                .node_pos_viewport = node_pos_for(b),
                .handle_radius = handle_radius,
                .interaction_id = b.interaction_id,
                .grabber_window_r = b.window_r,
                .popup_params = popup_params,
                .greyed_out = greyed_out,
                .active_cursor = b.uses_gain ? CursorType::AllArrows : CursorType::HorizontalArrows,
            });
        OverlayMacroDestinationRegion(g,
                                      b.window_r,
                                      ParamIndexFromLayerParamIndex(layer_index, b.params.freq));
    }

    Array<ParamIndex, k_num_eq_bands * 4> editor_indices;
    {
        usize idx = 0;
        for (auto const& b : bands) {
            editor_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, b.params.freq);
            editor_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, b.params.reso);
            editor_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, b.params.gain);
            editor_indices[idx++] = ParamIndexFromLayerParamIndex(layer_index, b.params.type);
        }
    }
    DoParamTextEditorOverlay(g, viewport_r, editor_indices);
}
