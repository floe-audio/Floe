// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"

#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"

inline void UpdateSilentSeconds(f32& silent_seconds, Span<f32x2 const> frames, f32 sample_rate) {
    bool all_silent = true;
    for (auto const& f : frames)
        if (!IsSilent(f)) {
            all_silent = false;
            break;
        }
    if (all_silent)
        silent_seconds += (f32)frames.size / sample_rate;
    else
        silent_seconds = 0;
}

struct EffectWetDryHelper {
    void SetWet(f32 amp) { wet = amp; }
    void SetDry(f32 amp) { dry = amp; }

    f32 Mix(AudioProcessingContext const& context, f32 w, f32 d) {
        return (w * wet_smoother.LowPass(wet, context.one_pole_smoothing_cutoff_10ms)) +
               (d * dry_smoother.LowPass(dry, context.one_pole_smoothing_cutoff_10ms));
    }

    f32x2 MixStereo(AudioProcessingContext const& context, f32x2 w, f32x2 d) {
        return (w * wet_smoother.LowPass(wet, context.one_pole_smoothing_cutoff_10ms)) +
               (d * dry_smoother.LowPass(dry, context.one_pole_smoothing_cutoff_10ms));
    }

    void Reset() {
        wet_smoother.Reset();
        dry_smoother.Reset();
    }

    f32 wet;
    OnePoleLowPassFilter<f32> wet_smoother {};
    f32 dry;
    OnePoleLowPassFilter<f32> dry_smoother {};
};

struct ScratchBuffers {
    class Buffer {
      public:
        Buffer(f32* b, u32 size) : m_buffer(b), m_block_size(size) { ASSERT_EQ((usize)b % 16, 0u); }

        Span<f32x2> Interleaved() { return ToStereoFramesSpan(m_buffer, m_block_size); }

        Array<f32*, 2> Channels() { return {m_buffer, m_buffer + m_block_size}; }

      private:
        f32* m_buffer;
        u32 m_block_size;
    };

    ScratchBuffers(u32 block_size, f32* b1, f32* b2) : buf1(b1, block_size), buf2(b2, block_size) {}
    Buffer buf1, buf2;
};

enum class EffectProcessResult {
    Done, // no more processing needed
    ProcessingTail, // processing needed
};

struct Effect {
    Effect(EffectType type) : type(type) {}

    virtual ~Effect() {}

    // main-thread but never while any audio-thread function is being called
    virtual void PrepareToPlay(AudioProcessingContext const&) {}

    void ProcessChanges(ProcessBlockChanges const& changes, AudioProcessingContext const& context) {
        if (auto p = changes.changed_params.Param(k_effect_info[(u32)type].on_param_index))
            mix = p->ValueAsBool() ? 1.0f : 0.0f;
        ProcessChangesInternal(changes, context);
    }

    struct ExtraProcessingContext {
        // The effect may use these buffers for temporary storage.
        ScratchBuffers scratch_buffers;

        // Effect-specific context.
        void* effect_context = nullptr;
    };

    // audio-thread
    virtual EffectProcessResult
    ProcessBlock(Span<f32x2>, AudioProcessingContext const&, ExtraProcessingContext) = 0;

    // Helper function for simple effects that only need to process one frame at a time. Wraps the individual
    // frame processing in the necessary block processing machinery.
    ALWAYS_INLINE EffectProcessResult ProcessBlockByFrame(Span<f32x2> frames,
                                                          auto process_frame_function,
                                                          AudioProcessingContext const& context) {
        if (!ShouldProcessBlock()) return EffectProcessResult::Done;
        for (auto& frame : frames)
            frame = MixOnOffSmoothing(context, process_frame_function(frame), frame);
        return EffectProcessResult::Done;
    }

    // audio-thread
    void Reset() {
        if (is_reset) return;
        ResetInternal();
        is_reset = true;
        mix_smoother.Reset();
    }

    // audio-thread
    bool ShouldProcessBlock() {
        if (mix == 0 && mix_smoother.IsStable(mix, 0.001f)) return false;
        is_reset = false;
        return true;
    }

    // audio-thread
    f32x2 MixOnOffSmoothing(AudioProcessingContext const& context, f32x2 wet, f32x2 dry) {
        return LinearInterpolate(mix_smoother.LowPass(mix, context.one_pole_smoothing_cutoff_10ms), dry, wet);
    }

    virtual void ResetInternal() {}
    virtual void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                        AudioProcessingContext const& context) = 0;

    EffectType const type;
    f32 mix = 0;
    OnePoleLowPassFilter<f32> mix_smoother {};
    bool is_reset = true;
};
