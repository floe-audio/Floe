// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "utils/debug/tracy_wrapped.hpp"

#include "effect.hpp"

class Phaser final : public Effect {
  public:
    Phaser() : Effect(EffectType::Phaser), phaser(vitfx::phaser::Create()) {}
    ~Phaser() override { vitfx::phaser::Destroy(phaser); }

    void ResetInternal() override {
        ZoneNamedN(hard_reset, "Phaser HardReset", true);
        vitfx::phaser::HardReset(*phaser);
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        vitfx::phaser::SetSampleRate(*phaser, (int)context.sample_rate);
    }

    void ProcessChangesInternal(ProcessBlockChanges const& changes, AudioProcessingContext const&) override {
        using namespace vitfx::phaser;

        if (auto p = changes.changed_params.Param(ParamIndex::PhaserFeedback))
            args.params[ToInt(Params::FeedbackAmount)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserModFreqHz))
            args.params[ToInt(Params::FrequencyHz)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserCenterSemitones))
            args.params[ToInt(Params::CenterSemitones)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserShape))
            args.params[ToInt(Params::Blend)] = p->ProjectedValue() * 2;
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserModDepth))
            args.params[ToInt(Params::ModDepthSemitones)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserStereoAmount))
            args.params[ToInt(Params::PhaseOffset)] = p->ProjectedValue() / 2;
        if (auto p = changes.changed_params.Param(ParamIndex::PhaserMix))
            args.params[ToInt(Params::Mix)] = p->ProjectedValue();
    }

    EffectProcessResult
    ProcessBlock(Span<f32x2> io_frames, AudioProcessingContext const& context, void*) override {
        ZoneNamedN(process_block, "Phaser ProcessBlock", true);

        if (!ShouldProcessBlock()) return EffectProcessResult::Done;

        f32x2 wet[k_block_size_max];

        CopyMemory(wet, io_frames.data, io_frames.size * sizeof(f32x2));

        auto num_frames = (u32)io_frames.size;
        u32 pos = 0;
        while (num_frames) {
            u32 const chunk_size = Min(num_frames, 64u);

            args.num_frames = (int)chunk_size;
            args.in_interleaved = (f32*)(io_frames.data + pos);
            args.out_interleaved = (f32*)(wet + pos);
            // TODO(1.0): use center_semitones_buffered

            vitfx::phaser::Process(*phaser, args);

            num_frames -= chunk_size;
            pos += chunk_size;
        }

        for (auto const frame_index : Range((u32)io_frames.size))
            io_frames[frame_index] = MixOnOffSmoothing(context, wet[frame_index], io_frames[frame_index]);

        return EffectProcessResult::Done;
    }

    vitfx::phaser::Phaser* phaser {};
    vitfx::phaser::ProcessPhaserArgs args {};
};
