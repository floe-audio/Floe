// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_distortion_display.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/distortion.hpp"
#include "processor/processor.hpp"

constexpr f32 k_preview_sample_rate = 48000;
constexpr u32 k_samples_per_cycle = DistortionDisplayState::k_samples_per_cycle;
constexpr u32 k_drawn_samples = DistortionDisplayState::k_drawn_samples;
// Octave's DC removal and Punish's coupling filters sit at 10 Hz and up, so an asymmetric shape takes tens
// of milliseconds to settle. These cycles are rendered and discarded before the drawn ones.
constexpr u32 k_settle_cycles = 14;

// Amplitude that reaches the top and bottom of the display. A little above the test tone's peak so a clean
// tone fills most of the height and Gain has room to grow it before the trace flattens at the edges.
constexpr f32 k_full_scale_amplitude = 0.2f;

static f32 TestToneSample(u32 sample_index) {
    return DistortionDsp::k_reference_peak_amplitude *
           Sin(k_tau<> * ((f32)sample_index / (f32)k_samples_per_cycle));
}

static Span<f32 const> RenderPreview(DistortionDisplayState& state,
                                     DistortionDisplayState::Inputs const& inputs) {
    if (state.valid && state.inputs == inputs) return state.wet;

    // The DSP's 10 Hz DC blockers droop every flat part of the wave by a visible amount within one cycle.
    // They are bypassed here and replaced by subtracting the mean of the drawn cycles: an exact whole number
    // of cycles, so this is what an ideal DC blocker would remove.
    auto& dsp = state.dsp;
    dsp.SetSettings({.type = inputs.type, .tilt = inputs.tilt, .compensate = true, .dc_block = false});
    dsp.SetSampleRate(k_preview_sample_rate); // Also resets, snapping the tilt shelves to their targets.

    DistortionDsp::Controls const controls {
        .drive01 = inputs.drive01,
        .punish01 = inputs.punish01,
        .auto_gain01 = inputs.auto_gain01,
    };
    auto const settle_samples = k_samples_per_cycle * k_settle_cycles;
    for (auto const sample_index : Range(settle_samples + k_drawn_samples)) {
        auto const output = dsp.Process(f32x2(TestToneSample((u32)sample_index)), controls);
        if (sample_index >= settle_samples) state.wet[sample_index - settle_samples] = output[0];
    }

    auto const mean = ({
        f32 sum = 0;
        for (auto const sample : state.wet)
            sum += sample;
        sum / (f32)k_drawn_samples;
    });
    for (auto& sample : state.wet)
        sample -= mean;

    // Steps in the shape (Bitcrush) make the downsampler ring at the top of the spectrum, which draws as
    // one-sample spikes. A zero-phase 3-tap smoothing nulls Nyquist and rounds corners by less than a pixel.
    // The buffer is a whole number of cycles, so it wraps around cleanly.
    {
        auto const unsmoothed = state.wet;
        for (auto const sample_index : Range(k_drawn_samples)) {
            auto const prev = unsmoothed[(sample_index + k_drawn_samples - 1) % k_drawn_samples];
            auto const next = unsmoothed[(sample_index + 1) % k_drawn_samples];
            state.wet[sample_index] = (prev + (2 * unsmoothed[sample_index]) + next) * 0.25f;
        }
    }

    state.inputs = inputs;
    state.valid = true;
    return state.wet;
}

void DoDistortionDisplay(GuiState& g, Rect viewport_r, bool greyed_out) {
    auto& imgui = g.imgui;
    auto& params = g.engine.processor.main_params;
    auto const& macro_dests = g.engine.processor.main_macro_destinations;

    auto const audible_projected_value = [&](ParamIndex index) {
        auto const param = params.DescribedValue(index);
        return param.info.ProjectValue(
            AdjustedLinearValue(params.values, macro_dests, param.LinearValue(), index));
    };

    auto const type = ParamToInt<DistortionType>(params.LinearValueLegacyAware(ParamIndex::DistortionType));
    DistortionDisplayState::Inputs const inputs {
        .type = type,
        .drive01 = audible_projected_value(ParamIndex::DistortionDrive),
        .punish01 = audible_projected_value(ParamIndex::DistortionPunish),
        .tilt = audible_projected_value(ParamIndex::DistortionTilt),
        .auto_gain01 =
            (IsLegacyDistortionType(type) && !params.BoolValue(ParamIndex::DistortionAutoGain)) ? 0.0f : 1.0f,
    };
    auto const wet = RenderPreview(g.distortion_display_state, inputs);
    auto const output_gain = DbToAmp(audible_projected_value(ParamIndex::DistortionGain));

    auto const window_r = imgui.ViewportRectToWindowRect(viewport_r);
    {
        auto const id = imgui.MakeId("distortion_display");
        imgui.RegisterRectForMouseTracking(window_r, false);
        imgui.SetHot(window_r, id);
        Tooltip(
            g,
            id,
            window_r,
            {
                .tooltip =
                    "A preview of the distortion's shaping: a 200hz sine wave rendered through the current settings."_s,
            });
    }

    imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::EqBack), WwToPixels(k_corner_rounding));

    auto const centre_y = viewport_r.y + (viewport_r.h * 0.5f);
    {
        auto const p0 = imgui.ViewportPosToWindowPos({viewport_r.x, centre_y});
        auto const p1 = imgui.ViewportPosToWindowPos({viewport_r.Right(), centre_y});
        imgui.draw_list->AddLine(p0, p1, LiveCol(UiColMap::EqGridZero));
    }

    auto const half_h = viewport_r.h * 0.5f;
    auto const trace_points = [&](auto sample_value) {
        DynamicArrayBounded<f32x2, k_drawn_samples> points;
        for (auto const sample_index : Range(k_drawn_samples)) {
            auto const t = (f32)sample_index / (f32)(k_drawn_samples - 1);
            auto const value = Clamp(sample_value(sample_index) / k_full_scale_amplitude, -1.0f, 1.0f);
            auto const x = viewport_r.x + (t * viewport_r.w);
            auto const y = centre_y - (value * half_h);
            dyn::Append(points, imgui.ViewportPosToWindowPos({x, y}));
        }
        return points;
    };

    auto const line_col = LiveCol(greyed_out ? UiColMap::EqLineGreyedOut : UiColMap::EqLine);

    auto const wet_points = trace_points([&](u32 sample_index) { return wet[sample_index] * output_gain; });

    auto const zero_y_window = imgui.ViewportPosToWindowPos({viewport_r.x, centre_y}).y;
    auto const area_col = LiveCol(UiColMap::EqArea);
    for (auto const i : Range(wet_points.size - 1)) {
        auto const p0 = wet_points[i];
        auto const p1 = wet_points[i + 1];
        f32x2 const verts[] = {
            f32x2 {p0.x, zero_y_window},
            p0,
            p1,
            f32x2 {p1.x, zero_y_window},
        };
        imgui.draw_list->AddConvexPolyFilled(verts, area_col, false);
    }
    imgui.draw_list->AddPolyline(wet_points, line_col, false, 1.5f, true);
}
