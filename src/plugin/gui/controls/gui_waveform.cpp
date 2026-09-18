// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_waveform.hpp"

#include <IconsFontAwesome6.h>

#include "foundation/utils/maths_constexpr.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/loop_modes.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/core/gui_waveform_images.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"
#include "processor/sample_processing.hpp"

// Prevents the waveform image flickering during fast melodic passages. The first change always goes through
// instantly; if a second arrives shortly after, the display locks until things calm down.
static u64 DebouncedWaveformHash(WaveformHashDebounce& state, u64 raw_hash) {
    constexpr f64 k_detection_window_secs = 0.10;
    constexpr f64 k_calm_threshold_secs = 1.3;

    // A zero hash means the audio thread cleared it (instrument change) — reset immediately so we
    // don't keep showing a waveform from the previous instrument.
    if (raw_hash == 0) {
        state = {};
        return 0;
    }

    auto const now = TimePoint::Now();
    auto const secs_since_last_change =
        state.last_change_time ? (now - state.last_change_time) : k_calm_threshold_secs;

    if (raw_hash != state.last_raw_hash) {
        if (!state.locked && secs_since_last_change < k_detection_window_secs)
            state.locked = true;
        else if (!state.locked)
            state.displayed_hash = raw_hash;
        state.last_raw_hash = raw_hash;
        state.last_change_time = now;
    } else if (state.locked && secs_since_last_change >= k_calm_threshold_secs) {
        state.locked = false;
        state.displayed_hash = raw_hash;
    }

    return state.displayed_hash;
}

static bool IsMultisampledInstrument(LayerProcessor const& layer) {
    if (auto i = layer.instrument.TryGetFromTag<InstrumentType::Sampler>())
        return (*i)->instrument.category == sample_lib::SamplerCategory::Multisample;
    return false;
}

enum class MultisampleDisplay : u8 { Representative, LastPlayed, Paused };

static f32 AdjustedLayerParamLinear(GuiState& g, u8 layer_index, LayerParamIndex layer_param) {
    auto const index = ParamIndexFromLayerParamIndex(layer_index, layer_param);
    auto const& params = g.engine.processor.main_params;
    return AdjustedLinearValue(params.values,
                               g.engine.processor.main_macro_destinations,
                               params.LinearValue(index),
                               index);
}

static Optional<String> WaveformTooltipText(ArenaAllocator& arena,
                                            LayerProcessor const& layer,
                                            Optional<param_values::PlayMode> play_mode,
                                            MultisampleDisplay multisample_display) {
#define WAVEFORM_INTRO "The waveform display. "
#define MULTISAMPLE_INTRO                                                                                    \
    WAVEFORM_INTRO                                                                                           \
    "This Instrument contains many samples, and the one you hear depends on which note you play and how "    \
    "hard. "

    auto const markers = ({
        String m =
            "When playing, the red markers are voices, each one tracking through the sample as it plays."_s;
        if (play_mode) switch (*play_mode) {
                case param_values::PlayMode::Standard: break;
                case param_values::PlayMode::GranularPlayback:
                    m = "When playing, the red markers are voices. Each one is the point grains are being drawn from, tracking through the sample as it plays. The lilac lines are the individual grains."_s;
                    break;
                case param_values::PlayMode::GranularFixed:
                    m = "When playing, the lilac lines are individual grains. The highlighted region is where they can be drawn from, set by the Position and Spread controls."_s;
                    break;
                case param_values::PlayMode::Count: PanicIfReached();
            }
        m;
    });
    auto const with_markers = [&](String body) -> Optional<String> {
        return fmt::Format(arena, "{}{}", body, markers);
    };

    switch (layer.instrument.tag) {
        case InstrumentType::None: return k_nullopt;
        case InstrumentType::WaveformSynth:
            return WAVEFORM_INTRO
                "This layer's Instrument is a built-in waveform rather than a sampled sound, so this shows its shape. When playing, the red lines are individual voices, placed left to right by pitch."_s;
        case InstrumentType::Sampler: {
            auto const& inst = *layer.instrument.GetFromTag<InstrumentType::Sampler>();
            switch (inst.instrument.category) {
                case sample_lib::SamplerCategory::Empty: return k_nullopt;
                case sample_lib::SamplerCategory::SingleSample:
                    return with_markers(WAVEFORM_INTRO
                                        "This is the sample that this layer's Instrument plays. "_s);
                case sample_lib::SamplerCategory::Sliced:
                    return with_markers(
                        WAVEFORM_INTRO
                        "This is the sample that this layer's Instrument plays, with vertical lines marking where it's divided into slices for tempo-synced playback. "_s);
                case sample_lib::SamplerCategory::Multisample:
                    switch (multisample_display) {
                        case MultisampleDisplay::Representative:
                            return with_markers(
                                MULTISAMPLE_INTRO
                                "Until you play a note, this shows a representative sample chosen by the library. "_s);
                        case MultisampleDisplay::LastPlayed:
                            return with_markers(
                                MULTISAMPLE_INTRO
                                "This shows the sample from the most recently played note. "_s);
                        case MultisampleDisplay::Paused:
                            return with_markers(
                                MULTISAMPLE_INTRO
                                "Notes are changing too quickly to follow, so the display is paused on a recent sample until things settle. "_s);
                    }
            }
        }
    }
    return k_nullopt;

#undef WAVEFORM_INTRO
#undef MULTISAMPLE_INTRO
}

// Sweep diagonal lines across the rect.
static void DrawHatchPattern(DrawList& draw_list, Rect r, u32 col, f32 spacing, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    draw_list.PushClipRect(r.Min(), r.Max(), true);

    constexpr auto k_angle_degrees = 55.0;
    constexpr auto k_angle_tan = (f32)constexpr_math::Tan(k_angle_degrees * constexpr_math::k_pi / 180.0);
    auto const step = spacing;
    auto const diagonal_extent = r.h / k_angle_tan;

    // Start far enough left that lines entering from the top-right still cover the rect.
    auto const start = -diagonal_extent;
    auto const end = r.w + (r.h / k_angle_tan);

    for (f32 offset = start; offset < end; offset += step) {
        f32x2 const a {r.x + offset, r.y};
        f32x2 const b {r.x + offset - diagonal_extent, r.Bottom()};
        draw_list.AddLine(a, b, col, thickness);
    }

    draw_list.PopClipRect();
}

struct PlayModeFeatures {
    bool has_play_mode;
    bool show_sample_offset;
    bool show_loop_controls;
    bool show_crossfade;
    bool show_grain_position_indicator;
    bool show_voice_cursors;
};

static PlayModeFeatures GetPlayModeFeatures(param_values::PlayMode play_mode) {
    switch (play_mode) {
        case param_values::PlayMode::Standard:
            return {
                .has_play_mode = true,
                .show_sample_offset = true,
                .show_loop_controls = true,
                .show_crossfade = true,
                .show_grain_position_indicator = false,
                .show_voice_cursors = true,
            };
        case param_values::PlayMode::GranularPlayback:
            return {
                .has_play_mode = true,
                .show_sample_offset = true,
                .show_loop_controls = true,
                .show_crossfade = true,
                .show_grain_position_indicator = false,
                .show_voice_cursors = true,
            };
        case param_values::PlayMode::GranularFixed:
            return {
                .has_play_mode = true,
                .show_sample_offset = false,
                .show_loop_controls = false,
                .show_crossfade = false,
                .show_grain_position_indicator = true,
                .show_voice_cursors = false,
            };
        case param_values::PlayMode::Count: PanicIfReached();
    }
    PanicIfReached();
}

static void DrawSpreadRegionRect(DrawList& draw_list,
                                 Rect window_r,
                                 f32 viewport_w,
                                 f32 start_01, // audio-data 0-1 space
                                 f32 end_01, // audio-data 0-1 space
                                 bool reverse,
                                 u32 col) {
    if (start_01 >= end_01) return;

    // Convert from audio-data space to visual space.
    f32 const visual_start = reverse ? (1.0f - end_01) : start_01;
    f32 const visual_end = reverse ? (1.0f - start_01) : end_01;

    f32 const left = Max(window_r.x + (visual_start * viewport_w), window_r.x);
    f32 const right = Min(window_r.x + (visual_end * viewport_w), window_r.Right());
    if (left >= right) return;

    draw_list.AddRectFilled(f32x2 {left, window_r.y}, f32x2 {right, window_r.Bottom()}, col);
}

static void DoWaveformControls(GuiState& g, LayerProcessor& layer, Rect r, PlayModeFeatures const& features) {
    if (layer.instrument_id.tag == InstrumentType::WaveformSynth) return;

    auto const handle_height = WwToPixels(12.8f);
    auto const handle_width = WwToPixels(14.0f);
    constexpr auto k_epsilon = 0.001f;
    constexpr auto k_slider_sensitivity = 320.0f;

    auto const& params = g.engine.processor.main_params;

    auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
    auto const desired_loop_mode =
        params.IntValue<param_values::LoopMode>(layer.index, LayerParamIndex::LoopMode);
    auto const mode =
        ActualLoopBehaviour(layer.instrument, desired_loop_mode, layer.VolumeEnvelopeIsOn(params));

    struct SingleBuiltinLoop {
        f32 start;
        f32 end;
        f32 crossfade;
        bool custom_loops_allowed;
    };

    auto const single_builtin_loop = ({
        Optional<SingleBuiltinLoop> l = {};
        if (IsBuiltinLoop(mode.value.id)) {
            // If it's a single sample with a builtin loop, we can use that.
            if (auto i = layer.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
                if ((*i)->instrument.regions.size == 1) {
                    if (auto const loop = (*i)->instrument.regions[0].loop.builtin_loop) {
                        auto const num_frames = (*i)->audio_datas[0]->num_frames;
                        auto const checked_loop = CreateBoundsCheckedLoop(*loop, num_frames);
                        l = SingleBuiltinLoop {
                            .start = (f32)checked_loop.start / (f32)num_frames,
                            .end = (f32)checked_loop.end / (f32)num_frames,
                            .crossfade = (f32)checked_loop.crossfade / (f32)num_frames,
                            .custom_loops_allowed =
                                (bool)(*i)->instrument.loop_overview.user_defined_loops_allowed,
                        };
                    }
                }
            }
        }
        l;
    });

    auto const extra_grabbing_room_x = handle_width;
    auto const extra_grabbing_room_towards_centre = r.h / 3;
    auto const extra_grabbing_room_away_from_centre = r.h / 6;

    enum class HandleType : u8 { LoopStart, LoopEnd, Offset, Xfade };
    enum class HandleDirection : u8 { Left, Right };

    // Loop points and crossfade.
    Rect start_line;
    Rect start_handle;
    Rect end_line;
    Rect end_handle;
    Rect xfade_line;
    Rect xfade_handle;
    Rect loop_region_r;

    // Macros move the audible loop while the handles stay at the parameter values. The region fill and
    // crossfade lines use these; the handles use the lines above.
    Rect adj_start_line;
    Rect adj_end_line;
    Rect adj_xfade_line;
    Rect adj_loop_region_r;
    f32 adj_loop_xfade_size {};

    Rect const& adj_left_line = reverse ? adj_end_line : adj_start_line;
    Rect const& adj_right_line = reverse ? adj_start_line : adj_end_line;

    bool draw_xfade = false;
    bool draw_xfade_as_inactive = false;
    f32 loop_xfade_size {};

    auto const start_id = g.imgui.MakeId("loop start");
    auto const end_id = g.imgui.MakeId("loop end");
    auto const xfade_id = g.imgui.MakeId("loop xfade");
    auto const loop_region_id = g.imgui.MakeId("region");

    auto draw_handle = [&](Rect r, imgui::Id id, HandleType type, bool inactive) {
        auto back_col = LiveCol(!single_builtin_loop ? UiColMap::WaveformLoopHandle
                                                     : UiColMap::WaveformLoopHandleInactive);
        auto back_hover_col = LiveCol(UiColMap::WaveformLoopHandleHover);
        auto text_col = LiveCol(UiColMap::WaveformLoopHandleText);

        String text {};
        HandleDirection handle_direction {HandleDirection::Left};
        switch (type) {
            case HandleType::LoopEnd: {
                handle_direction = reverse ? HandleDirection::Left : HandleDirection::Right;
                text = reverse ? ICON_FA_ROTATE_RIGHT : ICON_FA_ROTATE_LEFT;
                break;
            }
            case HandleType::LoopStart: {
                text = reverse ? ICON_FA_ROTATE_LEFT : ICON_FA_ROTATE_RIGHT;
                handle_direction = reverse ? HandleDirection::Right : HandleDirection::Left;
                break;
            }
            case HandleType::Offset: {
                text = ICON_FA_CARET_RIGHT;
                handle_direction = HandleDirection::Left;
                back_col = LiveCol(UiColMap::WaveformOffsetHandle);
                back_hover_col = LiveCol(UiColMap::WaveformOffsetHandleHover);
                text_col = LiveCol(UiColMap::WaveformOffsetHandleText);
                break;
            }
            case HandleType::Xfade: {
                text = ICON_FA_FIRE;
                handle_direction = mode.value.mode == sample_lib::LoopMode::Standard
                                       ? (reverse ? HandleDirection::Left : HandleDirection::Right)
                                       : HandleDirection::Right;
                back_col = inactive ? LiveCol(UiColMap::WaveformXfadeHandleInactive)
                                    : LiveCol(UiColMap::WaveformXfadeHandle);
                back_hover_col = LiveCol(UiColMap::WaveformXfadeHandleHover);
                text_col = LiveCol(UiColMap::WaveformXfadeHandleText);
                break;
            }
        }

        g.imgui.draw_list->AddRectFilled(r,
                                         g.imgui.IsHotOrActive(id, MouseButton::Left) ? back_hover_col
                                                                                      : back_col,
                                         WwToPixels(4.0f),
                                         ({
                                             u4 rc = 0;
                                             switch (handle_direction) {
                                                 case HandleDirection::Left: rc = 0b1001; break;
                                                 case HandleDirection::Right: rc = 0b0110; break;
                                             }
                                             rc;
                                         }));
        g.fonts.Push(g.fonts.atlas[ToInt(FontType::Icons)]);
        DEFER { g.fonts.Pop(); };
        g.imgui.draw_list->AddTextInRect(r,
                                         text_col,
                                         text,
                                         {
                                             .justification = TextJustification::Centred,
                                             .overflow_type = TextOverflowType::AllowOverflow,
                                             .font_scaling = 0.5f,
                                         });
    };

    // tooltip_text: if empty, the parameter's own description is used.
    auto do_handle_slider = [&](imgui::Id id,
                                Span<ParamIndex const> params,
                                Optional<ParamIndex> tooltip_param,
                                String tooltip_text,
                                Rect grabber_unregistered,
                                f32 value,
                                f32 default_val,
                                bool invert_slider,
                                FunctionRef<void(f32)> callback) {
        if (grabber_unregistered.w == 0) return;
        auto const grabber_r = g.imgui.RegisterAndConvertRect(grabber_unregistered);
        if (tooltip_param)
            AddParamContextMenuBehaviour(g,
                                         grabber_r,
                                         id,
                                         g.engine.processor.main_params.DescribedValue(*tooltip_param));

        auto const changed = g.imgui.SliderBehaviourRange({
            .rect_in_window_coords = grabber_r,
            .id = id,
            .min = invert_slider ? 1.0f : 0.0f,
            .max = invert_slider ? 0.0f : 1.0f,
            .value = value,
            .default_value = default_val,
            .cfg =
                {
                    .sensitivity = k_slider_sensitivity,
                    .slower_with_shift = true,
                    .shift_sensitivity_multiplier = 16,
                    .default_on_modifer = true,
                },
        });

        if (g.imgui.ButtonBehaviour(grabber_r,
                                    id,
                                    {
                                        .mouse_button = MouseButton::Left,
                                        .event = MouseButtonEvent::DoubleClick,
                                    })) {
            g.param_text_editor_to_open = GuiState::ParamTextEditorRequest {
                .param = params[0],
                .widget_id = ParamTextEditorOverlayId(g.imgui),
            };
        }

        if (g.imgui.IsHotOrActive(id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (g.imgui.WasJustActivated(id, MouseButton::Left))
            for (auto p : params)
                ParameterJustStartedMoving(g.engine.processor, p);
        if (changed) callback(value);
        if (g.imgui.WasJustDeactivated(id, MouseButton::Left))
            for (auto p : params)
                ParameterJustStoppedMoving(g.engine.processor, p);

        if (tooltip_param) {
            auto const param_obj = g.engine.processor.main_params.DescribedValue(*tooltip_param);
            Tooltip(g,
                    id,
                    grabber_r,
                    {
                        .value_popup = FunctionRef<String()> {[&]() -> String {
                            return ParamValuePopupText(g, param_obj, g.scratch_arena);
                        }},
                        .tooltip = FunctionRef<String()> {[&]() -> String {
                            return tooltip_text.size ? tooltip_text
                                                     : ParamTooltipText(param_obj, g.scratch_arena);
                        }},
                        .tooltip_footer = k_dragger_tooltip_footer,
                        // Place tooltips clear of the whole waveform rather than just the grabber, so
                        // they never cover the waveform you're editing.
                        .avoid_r = g.imgui.ViewportRectToWindowRect(r),
                    });
        }
    };

    // For handles that are drawn but can't be dragged. Uses its own id so the handle doesn't take on
    // the hover colour, which would suggest it's draggable.
    auto do_fixed_handle_tooltip =
        [&](imgui::Id id, Rect grabber_unregistered, String value_popup, String tooltip_text) {
            if (grabber_unregistered.w == 0) return;
            auto const grabber_r = g.imgui.RegisterAndConvertRect(grabber_unregistered);
            g.imgui.RegisterRectForMouseTracking(grabber_r, false);
            g.imgui.SetHot(grabber_r, id);
            Tooltip(g,
                    id,
                    grabber_r,
                    {
                        .value_popup = value_popup,
                        .tooltip = tooltip_text,
                        .avoid_r = g.imgui.ViewportRectToWindowRect(r),
                    });
        };

    if (mode.value.editable || single_builtin_loop) {
        auto const loop_start = !single_builtin_loop
                                    ? params.LinearValue(layer.index, LayerParamIndex::LoopStart)
                                    : single_builtin_loop->start;
        auto const loop_end = !single_builtin_loop
                                  ? Max(params.LinearValue(layer.index, LayerParamIndex::LoopEnd), loop_start)
                                  : single_builtin_loop->end;
        auto const raw_crossfade_size = !single_builtin_loop
                                            ? params.LinearValue(layer.index, LayerParamIndex::LoopCrossfade)
                                            : single_builtin_loop->crossfade;
        struct LoopGeometry {
            f32 start_pos;
            f32 end_pos;
            f32 xfade_line_pos;
            f32 xfade_size;
        };
        auto const geometry_for = [&](f32 start, f32 end, f32 raw_crossfade) {
            LoopGeometry geo {
                .start_pos = start * r.w,
                .end_pos = end * r.w,
                .xfade_size =
                    ClampCrossfadeSize<f32>(raw_crossfade, start, end, 1.0f, *mode.value.mode) * r.w,
            };
            geo.xfade_line_pos = geo.end_pos - geo.xfade_size;
            if (mode.value.mode == sample_lib::LoopMode::Standard) {
                if (reverse) {
                    geo.start_pos = r.w - geo.start_pos;
                    geo.end_pos = r.w - geo.end_pos;
                    geo.xfade_line_pos = geo.end_pos + geo.xfade_size;
                }
            } else if (!reverse) {
                geo.xfade_line_pos = geo.end_pos + geo.xfade_size;
            } else {
                geo.start_pos = r.w - geo.start_pos;
                geo.end_pos = r.w - geo.end_pos;
                geo.xfade_line_pos = geo.start_pos + geo.xfade_size;
            }
            return geo;
        };

        auto const geometry = geometry_for(loop_start, loop_end, raw_crossfade_size);
        loop_xfade_size = geometry.xfade_size;
        auto const loop_start_pos = geometry.start_pos;
        auto const loop_end_pos = geometry.end_pos;
        auto const loop_xfade_line_pos = geometry.xfade_line_pos;

        auto const xfade_active = loop_start != 0 && (loop_end - loop_start) != 0;
        draw_xfade_as_inactive = !xfade_active;

        auto const xfade_param_id =
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopCrossfade);
        auto const start_param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopStart);
        auto const end_param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopEnd);

        {
            auto const adj_geometry = ({
                auto geo = geometry;
                if (!single_builtin_loop) {
                    auto const adj_start =
                        AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::LoopStart);
                    auto const adj_end =
                        Max(AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::LoopEnd), adj_start);
                    geo = geometry_for(
                        adj_start,
                        adj_end,
                        AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::LoopCrossfade));
                }
                geo;
            });
            adj_loop_xfade_size = adj_geometry.xfade_size;
            adj_start_line = g.imgui.ViewportRectToWindowRect(r.WithXW(r.x + adj_geometry.start_pos, 1));
            adj_end_line = g.imgui.ViewportRectToWindowRect(r.WithXW(r.x + adj_geometry.end_pos, 1));
            adj_xfade_line = g.imgui.ViewportRectToWindowRect(r.WithXW(r.x + adj_geometry.xfade_line_pos, 1));
            adj_loop_region_r = g.imgui.ViewportRectToWindowRect(
                Rect::FromMinMax({r.x + Min(adj_geometry.start_pos, adj_geometry.end_pos), r.y},
                                 {r.x + Max(adj_geometry.start_pos, adj_geometry.end_pos), r.Bottom()}));
        }

        auto const fixed_loop_hint = ({
            String h {};
            if (single_builtin_loop)
                h = single_builtin_loop->custom_loops_allowed
                        ? "To set your own, choose one of the Custom Loop modes from the Loop menu."_s
                        : "This Instrument doesn't allow custom loop points."_s;
            h;
        });
        auto const fixed_value_popup = [&](DescribedParamValue const& param, f32 value) -> String {
            return g.scratch_arena.Clone(*param.info.LinearValueToString(value));
        };

        // Reads the loop points fresh from the params rather than the values captured at the start of the
        // frame, since the caller has just changed them.
        auto set_xfade_size_if_needed = [&]() {
            auto const new_loop_start = params.LinearValue(layer.index, LayerParamIndex::LoopStart);
            auto const new_loop_end =
                Max(params.LinearValue(layer.index, LayerParamIndex::LoopEnd), new_loop_start);
            auto xfade = g.engine.processor.main_params.LinearValue(xfade_param_id);
            auto clamped_xfade =
                ClampCrossfadeSize(xfade, new_loop_start, new_loop_end, 1.0f, *mode.value.mode);
            if (xfade > clamped_xfade) {
                SetParameterValue(g.engine.processor,
                                  xfade_param_id,
                                  clamped_xfade,
                                  {.host_should_not_record = true});
            }
        };

        // Start.
        {
            auto const param = g.engine.processor.main_params.DescribedValue(start_param_id);

            start_line = r.WithXW(r.x + loop_start_pos, 1);
            start_handle = {.xywh {start_line.Right() - handle_width, r.y, handle_width, handle_height}};
            if (reverse) start_handle.x += handle_width - start_line.w;

            auto grabber = start_handle;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            grabber.w += extra_grabbing_room_x;
            if (!reverse) grabber.x -= extra_grabbing_room_x;

            if (!single_builtin_loop)
                do_handle_slider(start_id,
                                 Array {start_param_id, xfade_param_id},
                                 start_param_id,
                                 reverse
                                     ? (String)fmt::Format(g.scratch_arena,
                                                           "{}\n\nReverse is on, so the display is mirrored: "
                                                           "the loop start sits on the right.",
                                                           param.info.tooltip)
                                     : String {},
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 reverse,
                                 [&](f32 val) {
                                     val = Max(0.0f, Min(loop_end - k_epsilon, val));
                                     SetParameterValue(g.engine.processor, start_param_id, val, {});
                                     set_xfade_size_if_needed();
                                 });
            else
                do_fixed_handle_tooltip(
                    g.imgui.MakeId("loop start fixed"),
                    grabber,
                    fixed_value_popup(param, single_builtin_loop->start),
                    fmt::Format(
                        g.scratch_arena,
                        "This loop start point is built into the Instrument, so it can't be moved. {}",
                        fixed_loop_hint));

            start_line = g.imgui.RegisterAndConvertRect(start_line);
            start_handle = g.imgui.RegisterAndConvertRect(start_handle);

            if (!single_builtin_loop)
                OverlayMacroDestinationRegion(g, g.imgui.ViewportRectToWindowRect(grabber), start_param_id);
        };

        // End.
        {
            auto const param = g.engine.processor.main_params.DescribedValue(end_param_id);

            end_line = r.WithXW(r.x + loop_end_pos, 1);
            end_handle = {.xywh {end_line.x, r.y, handle_width, handle_height}};
            if (reverse) end_handle.x -= handle_width - end_line.w;

            auto grabber = end_handle;
            grabber.w += extra_grabbing_room_x;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            if (reverse) grabber.x -= extra_grabbing_room_x;

            if (!single_builtin_loop)
                do_handle_slider(end_id,
                                 Array {end_param_id, xfade_param_id},
                                 end_param_id,
                                 reverse
                                     ? (String)fmt::Format(g.scratch_arena,
                                                           "{}\n\nReverse is on, so the display is mirrored: "
                                                           "the loop end sits on the left.",
                                                           param.info.tooltip)
                                     : String {},
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 reverse,
                                 [&](f32 value) {
                                     value = Min(1.0f, Max(loop_start + k_epsilon, value));
                                     SetParameterValue(g.engine.processor, end_param_id, value, {});
                                     set_xfade_size_if_needed();
                                 });
            else
                do_fixed_handle_tooltip(
                    g.imgui.MakeId("loop end fixed"),
                    grabber,
                    fixed_value_popup(param, single_builtin_loop->end),
                    fmt::Format(g.scratch_arena,
                                "This loop end point is built into the Instrument, so it can't be moved. {}",
                                fixed_loop_hint));

            end_line = g.imgui.RegisterAndConvertRect(end_line);
            end_handle = g.imgui.RegisterAndConvertRect(end_handle);

            if (!single_builtin_loop)
                OverlayMacroDestinationRegion(g, g.imgui.ViewportRectToWindowRect(grabber), end_param_id);
        };

        // Region.
        {
            loop_region_r = Rect::FromMinMax({r.x + Min(loop_start_pos, loop_end_pos), r.y},
                                             {r.x + Max(loop_start_pos, loop_end_pos), r.Bottom()});

            auto const region_draggable = !single_builtin_loop && !(loop_start == 0 && loop_end == 1);

            if (region_draggable) {
                do_handle_slider(loop_region_id,
                                 Array {start_param_id, end_param_id, xfade_param_id},
                                 {},
                                 {},
                                 loop_region_r,
                                 loop_start,
                                 loop_start,
                                 reverse,
                                 [&](f32 value) {
                                     f32 delta = value - loop_start;
                                     if (loop_end + delta > 1.0f) delta = 1.0f - loop_end;

                                     auto new_start = loop_start + delta;
                                     auto new_end = loop_end + delta;

                                     if (new_start != loop_start || new_end != loop_end) {
                                         SetParameterValue(g.engine.processor, start_param_id, new_start, {});
                                         SetParameterValue(g.engine.processor, end_param_id, new_end, {});
                                         set_xfade_size_if_needed();
                                     }
                                 });
            }
            loop_region_r = g.imgui.RegisterAndConvertRect(loop_region_r);

            if (region_draggable) {
                auto const start_param = g.engine.processor.main_params.DescribedValue(start_param_id);
                auto const end_param = g.engine.processor.main_params.DescribedValue(end_param_id);
                auto const xfade_param = g.engine.processor.main_params.DescribedValue(xfade_param_id);
                DescribedParamValue const* param_ptrs[] = {&start_param, &end_param, &xfade_param};

                Tooltip(g,
                        loop_region_id,
                        loop_region_r,
                        {
                            .value_popup = FunctionRef<String()> {[&]() -> String {
                                return ParamValuePopupText(g, param_ptrs, g.scratch_arena);
                            }},
                            .tooltip = "Drag to move the loop, keeping its length and crossfade"_s,
                            .avoid_r = g.imgui.ViewportRectToWindowRect(r),
                        });
            }
        }

        // Crossfade control.
        if (features.show_crossfade) {
            auto const& param = g.engine.processor.main_params.DescribedValue(xfade_param_id);

            xfade_line = r.WithXW(r.x + loop_xfade_line_pos, 1);
            xfade_handle = {.xywh {xfade_line.x, r.y + handle_height, handle_width, handle_height}};
            if (reverse && mode.value.mode == sample_lib::LoopMode::Standard)
                xfade_handle.x -= handle_width - xfade_line.w;

            auto grabber = xfade_handle;
            grabber.w += extra_grabbing_room_x;
            if (reverse && mode.value.mode == sample_lib::LoopMode::Standard)
                grabber.x -= extra_grabbing_room_x;

            if (single_builtin_loop) {
                do_fixed_handle_tooltip(
                    g.imgui.MakeId("loop xfade fixed"),
                    grabber,
                    fixed_value_popup(param, single_builtin_loop->crossfade),
                    fmt::Format(
                        g.scratch_arena,
                        "This loop crossfade is built into the Instrument, so it can't be changed. {}",
                        fixed_loop_hint));
            } else if (!xfade_active) {
                do_fixed_handle_tooltip(
                    g.imgui.MakeId("loop xfade inactive"),
                    grabber,
                    ParamValuePopupText(g, param, g.scratch_arena),
                    loop_start == 0
                        ? "The loop crossfade can't be used while the loop starts at the very beginning of the sample, because it needs audio before the loop start to blend in. Move the loop start later to enable it."_s
                        : "The loop crossfade can't be used because the loop has no length."_s);
            } else {
                bool const invert = mode.value.mode == sample_lib::LoopMode::Standard ? !reverse : false;

                auto const tooltip_text = ({
                    String mode_note {};
                    switch (*mode.value.mode) {
                        case sample_lib::LoopMode::Standard:
                            mode_note =
                                "This is a 'standard' wrap-around loop, so the crossfade happens as playback nears the loop end, blending into the audio just before the loop start."_s;
                            break;
                        case sample_lib::LoopMode::PingPong:
                            mode_note =
                                "This is a 'ping-pong' loop, so the crossfade smooths each turnaround, blending in the audio just beyond the loop point."_s;
                            break;
                        case sample_lib::LoopMode::Count: PanicIfReached();
                    }
                    fmt::Format(g.scratch_arena, "{}\n\n{}", param.info.tooltip, mode_note);
                });

                do_handle_slider(xfade_id,
                                 {&xfade_param_id, 1},
                                 xfade_param_id,
                                 tooltip_text,
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 invert,
                                 [&](f32 value) {
                                     value = ClampCrossfadeSize<f32>(value,
                                                                     loop_start - k_epsilon,
                                                                     loop_end + k_epsilon,
                                                                     1.0f,
                                                                     *mode.value.mode);
                                     SetParameterValue(g.engine.processor, xfade_param_id, value, {});
                                 });
            }

            xfade_line = g.imgui.RegisterAndConvertRect(xfade_line);
            xfade_handle = g.imgui.RegisterAndConvertRect(xfade_handle);
            draw_xfade = true;

            if (!single_builtin_loop)
                OverlayMacroDestinationRegion(g, g.imgui.ViewportRectToWindowRect(grabber), xfade_param_id);
        }
    }

    // Offset.
    Rect offs_handle {};
    auto const offs_imgui_id = g.imgui.MakeId("offset");
    if (features.show_sample_offset) {
        auto const sample_offset = params.LinearValue(layer.index, LayerParamIndex::SampleOffset);
        auto const param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::SampleOffset);
        auto const& param = g.engine.processor.main_params.DescribedValue(param_id);

        auto sample_offset_r = r.WithW(r.w * sample_offset);
        offs_handle = {.xywh {sample_offset_r.Right() - handle_width,
                              r.Bottom() - handle_height,
                              handle_width,
                              handle_height}};
        auto grabber = offs_handle;
        grabber.y -= extra_grabbing_room_towards_centre;
        grabber.h += extra_grabbing_room_towards_centre + extra_grabbing_room_away_from_centre;
        grabber.w += extra_grabbing_room_x;
        grabber.x -= extra_grabbing_room_x;

        do_handle_slider(offs_imgui_id,
                         {&param_id, 1},
                         param_id,
                         {},
                         grabber,
                         param.LinearValue(),
                         param.DefaultLinearValue(),
                         false,
                         [&](f32 value) { SetParameterValue(g.engine.processor, param_id, value, {}); });

        offs_handle = g.imgui.RegisterAndConvertRect(offs_handle);
        sample_offset_r = g.imgui.RegisterAndConvertRect(sample_offset_r);

        auto const adj_sample_offset =
            AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::SampleOffset);
        g.imgui.draw_list->AddRectFilled(g.imgui.ViewportRectToWindowRect(r.WithW(r.w * adj_sample_offset)),
                                         LiveCol(UiColMap::WaveformSampleOffset));
        g.imgui.draw_list->AddRectFilled(f32x2 {sample_offset_r.Right() - 1, sample_offset_r.y},
                                         sample_offset_r.Max(),
                                         g.imgui.IsHotOrActive(offs_imgui_id, MouseButton::Left)
                                             ? LiveCol(UiColMap::WaveformOffsetHandleHover)
                                             : LiveCol(UiColMap::WaveformOffsetHandle));

        OverlayMacroDestinationRegion(g, g.imgui.ViewportRectToWindowRect(grabber), param_id);
    }

    // Drawing.
    if (mode.value.editable || single_builtin_loop) {
        auto other_xfade_line = adj_start_line.WithPos(
            adj_start_line.TopRight() + f32x2 {reverse ? adj_loop_xfade_size : -adj_loop_xfade_size, 0});
        if (mode.value.mode == sample_lib::LoopMode::PingPong)
            other_xfade_line =
                adj_left_line.WithPos(adj_left_line.TopRight() - f32x2 {adj_loop_xfade_size, 0});

        if (draw_xfade && adj_loop_xfade_size > 0.01f) {
            if (mode.value.mode == sample_lib::LoopMode::Standard) {
                g.imgui.draw_list->AddLine(adj_xfade_line.Min(),
                                           adj_end_line.BottomLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
                g.imgui.draw_list->AddLine(other_xfade_line.BottomLeft(),
                                           adj_start_line.TopLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
            } else {
                g.imgui.draw_list->AddLine(other_xfade_line.BottomLeft(),
                                           adj_left_line.TopLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
                g.imgui.draw_list->AddLine(adj_right_line.TopRight(),
                                           adj_xfade_line.BottomLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
            }
        }

        auto const region_active =
            g.imgui.IsHot(loop_region_id) || g.imgui.IsActive(loop_region_id, MouseButton::Left);
        if (!region_active && adj_loop_xfade_size > 0.01f && draw_xfade) {
            if (mode.value.mode == sample_lib::LoopMode::Standard) {
                auto const points = Array {adj_start_line.TopLeft(),
                                           adj_xfade_line.TopLeft(),
                                           adj_end_line.BottomRight(),
                                           adj_start_line.BottomLeft()};
                g.imgui.draw_list->AddConvexPolyFilled(points,
                                                       LiveCol(UiColMap::WaveformRegionOverlay),
                                                       true);
            } else {
                auto const points = Array {other_xfade_line.BottomLeft(),
                                           adj_left_line.TopLeft(),
                                           adj_right_line.TopLeft(),
                                           adj_xfade_line.BottomRight()};
                g.imgui.draw_list->AddConvexPolyFilled(points,
                                                       LiveCol(UiColMap::WaveformRegionOverlay),
                                                       true);
            }
        } else {
            g.imgui.draw_list->AddRectFilled(adj_loop_region_r,
                                             region_active ? LiveCol(UiColMap::WaveformRegionOverlayHover)
                                                           : LiveCol(UiColMap::WaveformRegionOverlay));
        }

        struct LineAndId {
            Rect line;
            imgui::Id id;
        };
        for (auto const [line, id] : Array {
                 LineAndId {start_line, start_id},
                 LineAndId {end_line, end_id},
             }) {
            g.imgui.draw_list->AddRectFilled(
                line,
                LiveCol(g.imgui.IsHotOrActive(id, MouseButton::Left) ? UiColMap::WaveformLoopHandleHover
                        : !single_builtin_loop                       ? UiColMap::WaveformLoopHandle
                                                                     : UiColMap::WaveformLoopHandleInactive));
        }

        if (draw_xfade && loop_xfade_size > 0.01f) {
            g.imgui.draw_list->AddRectFilled(xfade_line,
                                             g.imgui.IsHotOrActive(xfade_id, MouseButton::Left)
                                                 ? LiveCol(UiColMap::WaveformXfadeHandleHover)
                                                 : LiveCol(UiColMap::WaveformXfadeHandle));
        }

        draw_handle(start_handle, start_id, HandleType::LoopStart, false);
        draw_handle(end_handle, end_id, HandleType::LoopEnd, false);
        if (draw_xfade) draw_handle(xfade_handle, xfade_id, HandleType::Xfade, draw_xfade_as_inactive);
    }
    if (features.show_sample_offset) draw_handle(offs_handle, offs_imgui_id, HandleType::Offset, false);

    // Text editor.
    if (g.param_text_editor_to_open) {
        auto const waveform_params = Array {
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopStart),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopEnd),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopCrossfade),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::SampleOffset),
        };
        auto const cut = r.w / 3;
        HandleShowingTextEditorForParams(g, r.CutLeft(cut).CutRight(cut), waveform_params);
    }
}

void DoWaveformElement(GuiState& g,
                       LayerProcessor& layer,
                       Rect viewport_r,
                       WaveformGuiOptions const& options) {
    g.imgui.PushId(SourceLocationHash() + layer.index);
    DEFER { g.imgui.PopId(); };

    // Fix issue where texture subtly begins to tile when we don't want it
    viewport_r.xywh = Round(viewport_r.xywh);

    auto const window_r = ({
        auto r = g.imgui.RegisterAndConvertRect(viewport_r);
        r.xywh = Round(r.xywh); // As above, fix tiling issue.
        r;
    });

    g.imgui.draw_list->AddRectFilled(window_r,
                                     LiveCol(UiColMap::WaveformLoopBack),
                                     WwToPixels(k_corner_rounding));

    if (g.engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer.index].Load(
            LoadMemoryOrder::Relaxed) != -1) {
        g.imgui.draw_list->AddTextInRect(window_r,
                                         LiveCol(UiColMap::WaveformLoadingText),
                                         "Loading…",
                                         {
                                             .justification = TextJustification::Centred,
                                             .overflow_type = TextOverflowType::AllowOverflow,
                                             .font_scaling = 1,
                                         });
    } else {
        auto const& params = g.engine.processor.main_params;
        auto const features = ({
            auto f = options.play_mode.HasValue() ? GetPlayModeFeatures(*options.play_mode)
                                                  : PlayModeFeatures {.show_voice_cursors = true};
            if (layer.IsSliced()) f.show_sample_offset = false;
            f;
        });

        auto const is_multisample = IsMultisampledInstrument(layer);

        // Waveform image.
        if (layer.instrument_id.tag != InstrumentType::None) {
            // The image cuts follow the macro-adjusted values so the shading matches what's audible; the
            // handles in DoWaveformControls stay at the parameter values.
            auto const offset =
                (features.show_sample_offset && layer.instrument_id.tag == InstrumentType::Sampler)
                    ? AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::SampleOffset)
                    : 0;
            auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);

            auto const raw_hash =
                g.engine.processor.voice_pool.last_activated_audio_data_hash[layer.index].Load(
                    LoadMemoryOrder::Relaxed);

            auto& debounce = g.waveform_hash_debounce[layer.index];
            auto const last_activated_hash =
                is_multisample ? DebouncedWaveformHash(debounce, raw_hash) : raw_hash;

            struct Range {
                f32x2 lo;
                f32x2 hi;
            };

            Range const whole_section_uv {
                .lo = {reverse ? 1.0f - offset : offset, 0},
                .hi = {reverse ? 0.0f : 1.0f, 1},
            };

            if (auto const tex = GuiIo().in.renderer->GetTextureFromImage(
                    GetWaveformImage(g.waveform_images,
                                     layer.instrument,
                                     *GuiIo().in.renderer,
                                     g.shared_engine_systems.thread_pool,
                                     viewport_r.size,
                                     g.engine.instance_index,
                                     last_activated_hash))) {
                if (features.has_play_mode && features.show_loop_controls) {
                    auto const loop_start =
                        AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::LoopStart);
                    auto const loop_end =
                        Max(AdjustedLayerParamLinear(g, layer.index, LayerParamIndex::LoopEnd), loop_start);
                    auto const loop_mode =
                        params.IntValue<param_values::LoopMode>(layer.index, LayerParamIndex::LoopMode);
                    bool const loop_points_editable =
                        ActualLoopBehaviour(layer.instrument, loop_mode, layer.VolumeEnvelopeIsOn(params))
                            .value.editable;

                    g.imgui.draw_list->AddImage(tex.Value(),
                                                window_r.Min() + f32x2 {offset * viewport_r.w, 0},
                                                window_r.Max(),
                                                whole_section_uv.lo,
                                                whole_section_uv.hi,
                                                (!loop_points_editable)
                                                    ? LiveCol(UiColMap::WaveformLoopWaveformLoop)
                                                    : LiveCol(UiColMap::WaveformLoopWaveform));

                    // Loop region highlight (shown in standard and granular speed, but only
                    // editable/draggable in standard).
                    if ((loop_end - loop_start) != 0 && loop_points_editable) {
                        Range const loop_section_uv {
                            .lo = {loop_start, 0},
                            .hi = {loop_start + (loop_end - loop_start), 1},
                        };
                        g.imgui.draw_list->AddImage(
                            tex.Value(),
                            window_r.Min() +
                                f32x2 {viewport_r.w * (reverse ? (1.0f - loop_start) : loop_start), 0},
                            window_r.Max() - f32x2 {window_r.w * (reverse ? loop_end : (1.0f - loop_end)), 0},
                            loop_section_uv.lo,
                            loop_section_uv.hi,
                            LiveCol(UiColMap::WaveformLoopWaveformLoop));
                    }

                    if (offset != 0) {
                        Range const offset_section_uv {
                            .lo = {reverse ? 1.0f : 0.0f, 0},
                            .hi = {reverse ? 1.0f - offset : offset, 1},
                        };
                        g.imgui.draw_list->AddImage(tex.Value(),
                                                    window_r.Min(),
                                                    window_r.Max() -
                                                        f32x2 {viewport_r.w * (1.0f - offset), 0},
                                                    offset_section_uv.lo,
                                                    offset_section_uv.hi,
                                                    LiveCol(UiColMap::WaveformLoopWaveformOffset));
                    }

                } else if (features.has_play_mode) {
                    // Play mode without loop controls (e.g. GranularFixed): draw
                    // the waveform plainly, with the sample offset overlay if applicable.
                    g.imgui.draw_list->AddImage(tex.Value(),
                                                window_r.Min() + f32x2 {offset * viewport_r.w, 0},
                                                window_r.Max(),
                                                whole_section_uv.lo,
                                                whole_section_uv.hi,
                                                LiveCol(UiColMap::WaveformLoopWaveformLoop));

                    if (offset != 0) {
                        Range const offset_section_uv {
                            .lo = {reverse ? 1.0f : 0.0f, 0},
                            .hi = {reverse ? 1.0f - offset : offset, 1},
                        };
                        g.imgui.draw_list->AddImage(tex.Value(),
                                                    window_r.Min(),
                                                    window_r.Max() -
                                                        f32x2 {viewport_r.w * (1.0f - offset), 0},
                                                    offset_section_uv.lo,
                                                    offset_section_uv.hi,
                                                    LiveCol(UiColMap::WaveformLoopWaveformOffset));
                    }

                } else {
                    // No play mode: plain waveform with no overlays or offset colouring,
                    // but still respecting the reverse flag.
                    g.imgui.draw_list->AddImage(tex.Value(),
                                                window_r.Min(),
                                                window_r.Max(),
                                                {reverse ? 1.0f : 0.0f, 0},
                                                {reverse ? 0.0f : 1.0f, 1},
                                                LiveCol(UiColMap::WaveformLoopWaveformLoop));
                }
            }

            if (is_multisample && debounce.locked) {
                DrawHatchPattern(*g.imgui.draw_list,
                                 window_r,
                                 LiveCol(UiColMap::WaveformMultisampleHatch),
                                 WwToPixels(5.5f),
                                 WwToPixels(1.0f));
            }

            // Whole-waveform tooltip. Registered before the controls so that hovering a handle takes
            // priority.
            {
                auto const multisample_display = ({
                    auto d = MultisampleDisplay::Representative;
                    if (debounce.locked)
                        d = MultisampleDisplay::Paused;
                    else if (last_activated_hash)
                        d = MultisampleDisplay::LastPlayed;
                    d;
                });
                if (auto const tooltip_text =
                        WaveformTooltipText(g.scratch_arena, layer, options.play_mode, multisample_display)) {
                    auto const id = g.imgui.MakeId("waveform");
                    g.imgui.RegisterRectForMouseTracking(window_r, false);
                    g.imgui.SetHot(window_r, id);
                    Tooltip(g, id, window_r, {.tooltip = *tooltip_text});
                }
            }

            // Multisample indicator: small eye icon in the top-right corner.
            if (is_multisample) {
                auto const icon_pad = WwToPixels(4.0f);
                auto const icon_size = WwToPixels(11.0f);
                Rect const icon_r = {.xywh {window_r.Right() - icon_size - icon_pad,
                                            window_r.y + icon_pad,
                                            icon_size,
                                            icon_size}};

                g.fonts.Push(g.fonts.atlas[ToInt(FontType::Icons)]);
                DEFER { g.fonts.Pop(); };
                auto icon_col = FromU32(LiveCol(UiColMap::WaveformMultisampleBadgeText));
                icon_col.a = (u8)(icon_col.a * 0.6f);
                g.imgui.draw_list->AddTextInRect(icon_r,
                                                 ToU32(icon_col),
                                                 ICON_FA_EYE,
                                                 {
                                                     .justification = TextJustification::Centred,
                                                     .overflow_type = TextOverflowType::AllowOverflow,
                                                     .font_scaling = 0.7f,
                                                 });
            }

            // Slice markers: thin vertical lines on the waveform at slice boundaries.
            if (auto inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
                auto const& regions = (*inst)->instrument.regions;
                if ((*inst)->instrument.category == sample_lib::SamplerCategory::Sliced &&
                    (*inst)->audio_datas.size) {
                    auto const num_frames = (*inst)->audio_datas[0]->num_frames;
                    if (num_frames > 0) {
                        auto const slice_col = LiveCol(UiColMap::WaveformSliceMarker);
                        for (auto const& slice : regions[0].slices) {
                            if (slice.start_frame == 0 || slice.start_frame >= num_frames) continue;
                            f32 pos = (f32)slice.start_frame / (f32)num_frames;
                            if (reverse) pos = 1.0f - pos;
                            auto const x_vp = Round(viewport_r.x + (pos * viewport_r.w));
                            auto const top = g.imgui.ViewportPosToWindowPos({x_vp, viewport_r.y});
                            auto const bottom =
                                g.imgui.ViewportPosToWindowPos({x_vp, viewport_r.y + viewport_r.h});
                            g.imgui.draw_list->AddLine(top, bottom, slice_col);
                        }
                    }
                }
            }
        }

        // Consume voice waveform markers once for use by both spread regions and voice cursors.
        auto const has_active_voices =
            g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed) > 0;
        auto const muted_opacity = LayerIsSilent(g.engine.processor, layer.index) ? 0.25f : 1.0f;
        auto& voice_waveform_markers =
            g.engine.processor.voice_pool.voice_waveform_markers_for_gui.Consume().data;

        // Grain markers (drawn below controls and voice cursors).
        if (has_active_voices) {
            auto& grain_markers_arr = g.engine.processor.voice_pool.grain_markers_for_gui.Consume().data;
            bool const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
            for (auto const voice_index : Range(k_num_voices)) {
                auto const& vm = grain_markers_arr[voice_index];
                if (!vm.num_active || vm.layer_index != layer.index) continue;

                f32 const voice_intensity = (f32)vm.intensity / (f32)UINT16_MAX;

                for (auto const i : Range(vm.num_active)) {
                    f32 pos = (f32)vm.grains[i].position / (f32)UINT16_MAX;
                    if (reverse) pos = 1.0f - pos;

                    f32x2 marker_pos {Round(viewport_r.x + (pos * viewport_r.w)), viewport_r.y};
                    marker_pos = g.imgui.ViewportPosToWindowPos(marker_pos);

                    f32 const grain_envelope = (f32)vm.grains[i].envelope / 255.0f;
                    DrawVoiceMarkerLine(g.imgui,
                                        marker_pos,
                                        viewport_r.h,
                                        g.imgui.ViewportPosToWindowPos(viewport_r.pos).x,
                                        {},
                                        {
                                            .opacity = voice_intensity * muted_opacity * grain_envelope,
                                            .col = LiveCol(UiColMap::WaveformLoopGrainMarkers),
                                        });
                }
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
            }
        }

        // Waveform controls: loop handles, offset handle, crossfade handle.
        if (features.show_loop_controls) DoWaveformControls(g, layer, viewport_r, features);

        // GranularFixed spread indicator from params (visible even with no notes playing).
        if (features.show_grain_position_indicator) {
            auto const adj_projected = [&](LayerParamIndex layer_param) {
                auto const index = ParamIndexFromLayerParamIndex(layer.index, layer_param);
                return k_param_descriptors[ToInt(index)].ProjectValue(
                    AdjustedLayerParamLinear(g, layer.index, layer_param));
            };
            auto const grain_pos = adj_projected(LayerParamIndex::GranularPosition);
            auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
            auto const grain_spread = adj_projected(LayerParamIndex::GranularSpread);
            auto const col = LiveCol(UiColMap::WaveformRegionOverlay);

            f32 const fp_start = grain_pos;
            f32 const fp_end = Min(grain_pos + grain_spread, 1.0f);
            f32 const audio_start = reverse ? (1.0f - fp_end) : fp_start;
            f32 const audio_end = reverse ? (1.0f - fp_start) : fp_end;
            DrawSpreadRegionRect(*g.imgui.draw_list,
                                 window_r,
                                 viewport_r.w,
                                 audio_start,
                                 audio_end,
                                 reverse,
                                 col);
        }

        // Voice cursors. Hidden in GranularFixed: the playhead there is just the Position param, which
        // the spread region already shows.
        if (has_active_voices && features.show_voice_cursors) {
            for (auto const voice_index : Range(k_num_voices)) {
                auto const marker = voice_waveform_markers[voice_index];
                if (!marker.intensity || marker.layer_index != layer.index) continue;

                f32 position = (f32)marker.position / (f32)UINT16_MAX;
                f32 const intensity = (f32)marker.intensity / (f32)UINT16_MAX;
                if (layer.instrument_id.tag == InstrumentType::Sampler &&
                    params.BoolValue(layer.index, LayerParamIndex::Reverse))
                    position = 1 - position;

                f32x2 cursor_pos {Round(viewport_r.x + (position * viewport_r.w)), viewport_r.y};
                cursor_pos = g.imgui.ViewportPosToWindowPos(cursor_pos);
                DrawVoiceMarkerLine(g.imgui,
                                    cursor_pos,
                                    viewport_r.h,
                                    g.imgui.ViewportPosToWindowPos(viewport_r.pos).x,
                                    {},
                                    {.opacity = intensity * muted_opacity});
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
            }
        }
    }
}
