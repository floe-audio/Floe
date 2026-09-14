// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "effect.hpp"
#include "processing_utils/limiter.hpp"

// The limiter's output lags its input by a short lookahead delay, so this doesn't use the base class's
// ProcessBlockByFrame helper:
// - Mix is blended inside LimiterDsp against the delayed dry signal (blending against the undelayed
//   input would comb-filter), so the base class's mix_param is left at 1 and bypass_mix tracks only
//   the on/off state.
// - The on/off transition is crossfaded in both directions against the undelayed input. There's an
//   unavoidable time-shift at the moment the delay line enters or leaves the signal path; spreading it
//   over the 10ms crossfade is far less audible than the hard cut you'd get from switching instantly.
// - Once fully bypassed the DSP is reset so the delay line and meters don't hold stale audio that would
//   otherwise replay when the effect is next enabled.
class Limiter final : public Effect {
  public:
    Limiter() : Effect(EffectType::Limiter) {}

    void PrepareToPlay(AudioProcessingContext const& context) override {
        limiter_dsp.PrepareToPlay(context.sample_rate);
    }

    void ResetInternal() override { limiter_dsp.Reset(); }

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        limiter_dsp.OnParamChange({
            .gain_db = changes.changed_params.ProjectedValue(ParamIndex::LimiterGain),
            .ceiling_db = changes.changed_params.ProjectedValue(ParamIndex::LimiterCeiling),
            .mix_01 = changes.changed_params.ProjectedValue(ParamIndex::LimiterMix),
        });
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> io_frames, AudioProcessingContext const& context, void*) override {
        auto const starting_from_reset = is_reset;
        if (!ShouldProcessBlock()) {
            Reset();
            return EffectProcessResult::Done;
        }
        if (starting_from_reset) bypass_smoother.prev_output = 0;

        limiter_dsp.input_peak_meter.AddBuffer(io_frames);

        f32x2 wet[k_block_size_max];
        for (auto const frame_index : Range((u32)io_frames.size)) {
            auto const processed = limiter_dsp.Process(io_frames[frame_index]);
            wet[frame_index] = limiter_dsp.CurrentWet();
            io_frames[frame_index] = ApplyBypassCrossfade(context, processed, io_frames[frame_index]);
        }
        // Meter the fully-limited signal rather than io_frames: io_frames is diluted by the Mix knob,
        // which would otherwise make the output meter (and its ceiling marker) misleading below 100% mix.
        limiter_dsp.output_peak_meter.AddBuffer({wet, io_frames.size});
        limiter_dsp.PublishGuiStats((u32)io_frames.size);

        // Keep being called while the delay line still holds audio, so it flushes through rather than
        // sitting there until the next note starts.
        return limiter_dsp.HasPendingAudio() ? EffectProcessResult::ProcessingTail
                                             : EffectProcessResult::Done;
    }

    LimiterDsp limiter_dsp {};
};
