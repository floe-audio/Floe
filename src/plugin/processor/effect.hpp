// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/effect_descriptors.hpp"

#include "param.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/filters.hpp"
#include "processing_utils/stereo_audio_frame.hpp"

inline void UpdateSilentSeconds(f32& silent_seconds, Span<StereoAudioFrame const> frames, f32 sample_rate) {
    bool all_silent = true;
    for (auto const& f : frames)
        if (!f.IsSilent()) {
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

    StereoAudioFrame
    MixStereo(AudioProcessingContext const& context, StereoAudioFrame w, StereoAudioFrame d) {
        return w * wet_smoother.LowPass(wet, context.one_pole_smoothing_cutoff_10ms) +
               d * dry_smoother.LowPass(dry, context.one_pole_smoothing_cutoff_10ms);
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

        Span<StereoAudioFrame> Interleaved() { return ToStereoFramesSpan(m_buffer, m_block_size); }

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

// Base class for effects.
// The subclass can either override ProcessFrame or ProcessBlock
class Effect {
  public:
    Effect(EffectType type) : type(type) {}

    virtual ~Effect() {}

    // audio-thread
    void OnParamChange(ChangedParams changed_params, AudioProcessingContext const& context) {
        if (auto p = changed_params.Param(k_effect_info[(u32)type].on_param_index))
            mix = p->ValueAsBool() ? 1.0f : 0.0f;
        OnParamChangeInternal(changed_params, context);
    }

    // main-thread but never while any audio-thread function is being called
    virtual void PrepareToPlay(AudioProcessingContext const&) {}

    // audio-thread
    virtual void SetTempo(AudioProcessingContext const&) {}

    // audio-thread
    virtual EffectProcessResult ProcessBlock(Span<StereoAudioFrame> frames,
                                             [[maybe_unused]] ScratchBuffers scratch_buffers,
                                             AudioProcessingContext const& context) {
        if (!ShouldProcessBlock()) return EffectProcessResult::Done;
        for (auto [i, frame] : Enumerate<u32>(frames))
            frame = MixOnOffSmoothing(context, ProcessFrame(context, frame, i), frame);
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
    StereoAudioFrame
    MixOnOffSmoothing(AudioProcessingContext const& context, StereoAudioFrame wet, StereoAudioFrame dry) {
        return LinearInterpolate(mix_smoother.LowPass(mix, context.one_pole_smoothing_cutoff_10ms), dry, wet);
    }

    // Internals
    virtual StereoAudioFrame
    ProcessFrame(AudioProcessingContext const&, StereoAudioFrame in, u32 frame_index) {
        PanicIfReached();
        (void)frame_index;
        return in;
    }

    virtual void OnParamChangeInternal(ChangedParams changed_params,
                                       AudioProcessingContext const& context) = 0;

    virtual void ResetInternal() {}

    EffectType const type;
    f32 mix = 0;
    OnePoleLowPassFilter<f32> mix_smoother {};
    bool is_reset = true;
};
