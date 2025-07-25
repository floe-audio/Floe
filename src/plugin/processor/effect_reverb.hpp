// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "effect.hpp"

class Reverb final : public Effect {
  public:
    Reverb() : Effect(EffectType::Reverb), reverb(vitfx::reverb::Create()) {}
    ~Reverb() override { vitfx::reverb::Destroy(reverb); }

    void ResetInternal() override {
        ZoneNamedN(hard_reset, "Reverb HardReset", true);
        vitfx::reverb::HardReset(*reverb);
        silent_seconds = 0;
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        vitfx::reverb::SetSampleRate(*reverb, (int)context.sample_rate);
    }

    EffectProcessResult ProcessBlock(Span<StereoAudioFrame> io_frames,
                                     AudioProcessingContext const& context,
                                     ExtraProcessingContext extra_context) override {
        ZoneNamedN(process_block, "Reverb ProcessBlock", true);
        if (!ShouldProcessBlock()) return EffectProcessResult::Done;

        UpdateSilentSeconds(silent_seconds, io_frames, context.sample_rate);

        auto wet = extra_context.scratch_buffers.buf1.Interleaved();
        wet.size = io_frames.size;
        CopyMemory(wet.data, io_frames.data, io_frames.size * sizeof(StereoAudioFrame));

        auto num_frames = (u32)io_frames.size;
        u32 pos = 0;
        while (num_frames) {
            ZoneNamedN(in_chunk, "Reverb chunk", true);
            u32 const chunk_size = Min(num_frames, 64u);
            ZoneValueV(in_chunk, chunk_size);

            args.num_frames = (int)chunk_size;
            args.in_interleaved = (f32*)(io_frames.data + pos);
            args.out_interleaved = (f32*)(wet.data + pos);

            vitfx::reverb::Process(*reverb, args);

            num_frames -= chunk_size;
            pos += chunk_size;
        }

        for (auto const frame_index : Range((u32)io_frames.size))
            io_frames[frame_index] = MixOnOffSmoothing(context, wet[frame_index], io_frames[frame_index]);

        return IsSilent() ? EffectProcessResult::Done : EffectProcessResult::ProcessingTail;
    }

    void OnParamChangeInternal(ChangedParams changed_params, AudioProcessingContext const&) override {
        using namespace vitfx::reverb;
        if (auto p = changed_params.Param(ParamIndex::ReverbDecayTimeMs))
            args.params[ToInt(Params::DecayTimeSeconds)] = p->ProjectedValue() / 1000.0f;
        if (auto p = changed_params.Param(ParamIndex::ReverbPreLowPassCutoff))
            args.params[ToInt(Params::PreLowPassCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbPreHighPassCutoff))
            args.params[ToInt(Params::PreHighPassCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbLowShelfCutoff))
            args.params[ToInt(Params::LowShelfCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbLowShelfGain))
            args.params[ToInt(Params::LowShelfGainDb)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbHighShelfCutoff))
            args.params[ToInt(Params::HighShelfCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbHighShelfGain))
            args.params[ToInt(Params::HighShelfGainDb)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbChorusAmount))
            args.params[ToInt(Params::ChorusAmount)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbChorusFrequency))
            args.params[ToInt(Params::ChorusFrequency)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbSize))
            args.params[ToInt(Params::Size)] = p->ProjectedValue();
        if (auto p = changed_params.Param(ParamIndex::ReverbDelay))
            args.params[ToInt(Params::DelaySeconds)] = p->ProjectedValue() / 1000.0f;
        if (auto p = changed_params.Param(ParamIndex::ReverbMix))
            args.params[ToInt(Params::Mix)] = p->ProjectedValue();
    }

    bool IsSilent() const {
        return silent_seconds > args.params[ToInt(vitfx::reverb::Params::DecayTimeSeconds)];
    }

    f32 silent_seconds = 0;
    vitfx::reverb::Reverb* reverb {};
    vitfx::reverb::ProcessReverbArgs args {};
};
