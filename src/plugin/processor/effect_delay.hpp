// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <vitfx/wrapper.hpp>

#include "utils/debug/tracy_wrapped.hpp"

#include "effect.hpp"
#include "processing_utils/synced_timings.hpp"

class Delay final : public Effect {
  public:
    Delay() : Effect(EffectType::Delay), delay(vitfx::delay::Create()) {}
    ~Delay() override { vitfx::delay::Destroy(delay); }

    void ResetInternal() override {
        ZoneNamedN(hard_reset, "Delay HardReset", true);
        vitfx::delay::HardReset(*delay);
        silent_seconds = 0;
    }

    void PrepareToPlay(AudioProcessingContext const& context) override {
        vitfx::delay::SetSampleRate(*delay, (int)context.sample_rate);
    }

    void ProcessChangesInternal(ProcessBlockChanges const& changes,
                                AudioProcessingContext const& context) override {
        using namespace vitfx::delay;
        bool update_time_l = false;
        bool update_time_r = false;

        if (auto p = changes.changed_params.Param(ParamIndex::DelayTimeSyncSwitch))
            is_synced = p->ValueAsBool();

        if (auto p = changes.changed_params.Param(ParamIndex::DelayFeedback))
            args.params[ToInt(Params::Feedback)] = p->ProjectedValue();

        if (auto p = changes.changed_params.Param(ParamIndex::DelayTimeSyncedL)) {
            synced_time_l = ToSyncedTime(p->ValueAsInt<param_values::DelaySyncedTime>());
            update_time_l = true;
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DelayTimeSyncedR)) {
            synced_time_r = ToSyncedTime(p->ValueAsInt<param_values::DelaySyncedTime>());
            update_time_r = true;
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DelayTimeLMs)) {
            free_time_hz_l = MsToHz(p->ProjectedValue());
            update_time_l = true;
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DelayTimeRMs)) {
            free_time_hz_r = MsToHz(p->ProjectedValue());
            update_time_r = true;
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DelayMode)) {
            auto mode = p->ValueAsInt<param_values::DelayMode>();
            args.params[ToInt(Params::Mode)] = (f32)mode;
        }

        if (auto p = changes.changed_params.Param(ParamIndex::DelayFilterCutoffSemitones))
            args.params[ToInt(Params::FilterCutoffSemitones)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::DelayFilterSpread))
            args.params[ToInt(Params::FilterSpread)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::DelayMix))
            args.params[ToInt(Params::Mix)] = p->ProjectedValue();
        if (auto p = changes.changed_params.Param(ParamIndex::DelayFeedback))
            args.params[ToInt(Params::Feedback)] = p->ProjectedValue();

        if (is_synced && changes.tempo_changed) {
            update_time_l = true;
            update_time_r = true;
        }

        if (update_time_l) {
            args.params[ToInt(Params::TimeLeftHz)] =
                is_synced ? SyncedTimeToHz(context.tempo, synced_time_l) : free_time_hz_l;
        }
        if (update_time_r) {
            args.params[ToInt(Params::TimeRightHz)] =
                is_synced ? SyncedTimeToHz(context.tempo, synced_time_r) : free_time_hz_r;
        }
    }

    EffectProcessResult ProcessBlock(Span<StereoAudioFrame> frames,
                                     AudioProcessingContext const& context,
                                     ExtraProcessingContext extra_context) override {
        ZoneNamedN(process_block, "Delay ProcessBlock", true);

        if (!ShouldProcessBlock()) return EffectProcessResult::Done;

        auto wet = extra_context.scratch_buffers.buf1.Interleaved();
        wet.size = frames.size;
        CopyMemory(wet.data, frames.data, frames.size * sizeof(StereoAudioFrame));

        auto num_frames = (u32)frames.size;
        u32 pos = 0;
        while (num_frames) {
            u32 const chunk_size = Min(num_frames, 64u);

            args.num_frames = (int)chunk_size;
            args.in_interleaved = (f32*)(frames.data + pos);
            args.out_interleaved = (f32*)(wet.data + pos);

            vitfx::delay::Process(*delay, args);

            num_frames -= chunk_size;
            pos += chunk_size;
        }

        for (auto const frame_index : Range((u32)frames.size))
            frames[frame_index] = MixOnOffSmoothing(context, wet[frame_index], frames[frame_index]);

        // check for silence on the output
        UpdateSilentSeconds(silent_seconds, frames, context.sample_rate);

        return IsSilent() ? EffectProcessResult::Done : EffectProcessResult::ProcessingTail;
    }

    static SyncedTimes ToSyncedTime(param_values::DelaySyncedTime t) {
        // Remapping enum values like this allows us to separate values that cannot change (the
        // parameter value), with values that we have more control over (DSP code)
        switch (t) {
            case param_values::DelaySyncedTime::_1_64T: return SyncedTimes::_1_64T;
            case param_values::DelaySyncedTime::_1_64: return SyncedTimes::_1_64;
            case param_values::DelaySyncedTime::_1_64D: return SyncedTimes::_1_64D;
            case param_values::DelaySyncedTime::_1_32T: return SyncedTimes::_1_32T;
            case param_values::DelaySyncedTime::_1_32: return SyncedTimes::_1_32;
            case param_values::DelaySyncedTime::_1_32D: return SyncedTimes::_1_32D;
            case param_values::DelaySyncedTime::_1_16T: return SyncedTimes::_1_16T;
            case param_values::DelaySyncedTime::_1_16: return SyncedTimes::_1_16;
            case param_values::DelaySyncedTime::_1_16D: return SyncedTimes::_1_16D;
            case param_values::DelaySyncedTime::_1_8T: return SyncedTimes::_1_8T;
            case param_values::DelaySyncedTime::_1_8: return SyncedTimes::_1_8;
            case param_values::DelaySyncedTime::_1_8D: return SyncedTimes::_1_8D;
            case param_values::DelaySyncedTime::_1_4T: return SyncedTimes::_1_4T;
            case param_values::DelaySyncedTime::_1_4: return SyncedTimes::_1_4;
            case param_values::DelaySyncedTime::_1_4D: return SyncedTimes::_1_4D;
            case param_values::DelaySyncedTime::_1_2T: return SyncedTimes::_1_2T;
            case param_values::DelaySyncedTime::_1_2: return SyncedTimes::_1_2;
            case param_values::DelaySyncedTime::_1_2D: return SyncedTimes::_1_2D;
            case param_values::DelaySyncedTime::_1_1T: return SyncedTimes::_1_1T;
            case param_values::DelaySyncedTime::_1_1: return SyncedTimes::_1_1;
            case param_values::DelaySyncedTime::_1_1D: return SyncedTimes::_1_1D;
            case param_values::DelaySyncedTime::Count: break;
        }
        PanicIfReached();
        return {};
    }

    bool IsSilent() const {
        constexpr f32 k_extra_seconds = 0.1f; // ensure that we detect echos in the buffer
        return silent_seconds > ((1.0f / Max(args.params[ToInt(vitfx::delay::Params::TimeLeftHz)],
                                             args.params[ToInt(vitfx::delay::Params::TimeRightHz)])) +
                                 k_extra_seconds);
    }

    f32 silent_seconds = 0;
    vitfx::delay::Delay* delay {};
    SyncedTimes synced_time_l {};
    SyncedTimes synced_time_r {};
    f32 free_time_hz_l {};
    f32 free_time_hz_r {};
    bool is_synced = false;
    vitfx::delay::ProcessDelayArgs args {};
};
