// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wrapper.hpp"

#include <assert.h>

#include "src/synthesis/effects/compressor.h"
#include "src/synthesis/effects/delay.h"
#include "src/synthesis/effects/phaser.h"
#include "src/synthesis/effects/reverb.h"
#include "tracy/Tracy.hpp"

namespace vitfx {

namespace reverb {

struct Reverb {
    vital::Reverb reverb;
    vital::Output in_params[(int)Params::Count];
    vital::poly_float in_buffer[vital::kMaxBufferSize];
};

Reverb* Create() {
    auto verb = new Reverb();
    for (int i = 0; i < (int)Params::Count; ++i) {
        unsigned index = -1;
        switch ((Params)i) {
            case Params::DecayTimeSeconds: index = vital::Reverb::kDecayTime; break;
            case Params::PreLowPassCutoffSemitones: index = vital::Reverb::kPreLowCutoff; break;
            case Params::PreHighPassCutoffSemitones: index = vital::Reverb::kPreHighCutoff; break;
            case Params::LowShelfCutoffSemitones: index = vital::Reverb::kLowCutoff; break;
            case Params::LowShelfGainDb: index = vital::Reverb::kLowGain; break;
            case Params::HighShelfCutoffSemitones: index = vital::Reverb::kHighCutoff; break;
            case Params::HighShelfGainDb: index = vital::Reverb::kHighGain; break;
            case Params::ChorusAmount: index = vital::Reverb::kChorusAmount; break;
            case Params::ChorusFrequency: index = vital::Reverb::kChorusFrequency; break;
            case Params::Size: index = vital::Reverb::kSize; break;
            case Params::DelaySeconds: index = vital::Reverb::kDelay; break;
            case Params::Mix: index = vital::Reverb::kWet; break;
            case Params::Count: break;
        }
        verb->reverb.plug(&verb->in_params[i], index);
    }
    return verb;
}

void Destroy(Reverb* reverb) { delete reverb; }

void Process(Reverb& reverb, ProcessReverbArgs args) {
    assert(args.num_frames <= vital::kMaxBufferSize);

    {
        ZoneNamedN(setup, "Reverb setup", true);
        for (int i = 0; i < (int)Params::Count; ++i) {
            // The chorus amount parameter behaves more pleasingly with a strong exponential curve
            if (i == (int)Params::ChorusAmount) args.params[i] = powf(args.params[i], 5.0f);

            // The reverb module only ever looks at the first value in the buffer
            reverb.in_params[i].buffer[0] = vital::poly_float::init(args.params[i]);
        }

        for (int i = 0; i < args.num_frames; ++i)
            reverb.in_buffer[i] = vital::utils::toPolyFloatFromUnaligned(&args.in_interleaved[i * 2]);
    }

    {
        ZoneNamedN(process, "Reverb processWithInput", true);
        reverb.reverb.processWithInput(reverb.in_buffer, args.num_frames);
    }

    {
        ZoneNamedN(copy, "Reverb copy output", true);
        for (int i = 0; i < args.num_frames; ++i) {
            auto const& o = reverb.reverb.output()->buffer[i];
            args.out_interleaved[i * 2 + 0] = o[0];
            args.out_interleaved[i * 2 + 1] = o[1];
        }
    }
}

void HardReset(Reverb& reverb) { reverb.reverb.hardReset(); }

void SetSampleRate(Reverb& reverb, int sample_rate) { reverb.reverb.setSampleRate(sample_rate); }

} // namespace reverb

namespace phaser {

struct Phaser {
    vital::Phaser phaser;
    vital::Output in_params[(int)Params::Count];
    vital::poly_float in_buffer[vital::kMaxBufferSize];
};

Phaser* Create() {
    auto phaser = new Phaser();
    for (int i = 0; i < (int)Params::Count; ++i) {
        unsigned index = -1;
        switch ((Params)i) {
            case Params::FeedbackAmount: index = vital::Phaser::kFeedbackGain; break;
            case Params::FrequencyHz: index = vital::Phaser::kRate; break;
            case Params::CenterSemitones: index = vital::Phaser::kCenter; break;
            case Params::Blend: index = vital::Phaser::kBlend; break;
            case Params::ModDepthSemitones: index = vital::Phaser::kModDepth; break;
            case Params::PhaseOffset: index = vital::Phaser::kPhaseOffset; break;
            case Params::Mix: index = vital::Phaser::kMix; break;
            case Params::Count: break;
        }
        phaser->phaser.plug(&phaser->in_params[i], index);
    }
    phaser->phaser.init();
    return phaser;
}

void Destroy(Phaser* phaser) { delete phaser; }

void Process(Phaser& phaser, ProcessPhaserArgs args) {
    assert(args.num_frames <= vital::kMaxBufferSize);

    for (int i = 0; i < (int)Params::Count; ++i) {
        if (i == (int)Params::CenterSemitones) {
            if (args.center_semitones != nullptr)
                for (int j = 0; j < args.num_frames; ++j)
                    phaser.in_params[i].buffer[j] = vital::poly_float::init(args.center_semitones[j]);
            else
                for (int j = 0; j < args.num_frames; ++j)
                    phaser.in_params[i].buffer[j] = vital::poly_float::init(args.params[i]);
        } else {
            // The phaser module looks at the first value in the buffer
            phaser.in_params[i].buffer[0] = vital::poly_float::init(args.params[i]);
        }
    }

    for (int i = 0; i < args.num_frames; ++i)
        phaser.in_buffer[i] = vital::utils::toPolyFloatFromUnaligned(&args.in_interleaved[i * 2]);

    phaser.phaser.processWithInput(phaser.in_buffer, args.num_frames);

    for (int i = 0; i < args.num_frames; ++i) {
        auto const& o = phaser.phaser.output()->buffer[i];
        args.out_interleaved[i * 2 + 0] = o[0];
        args.out_interleaved[i * 2 + 1] = o[1];
    }
}

void HardReset(Phaser& phaser) { phaser.phaser.hardReset(); }

void SetSampleRate(Phaser& phaser, int sample_rate) { phaser.phaser.setSampleRate(sample_rate); }

} // namespace phaser

namespace delay {

constexpr float kMaxDelayTime = 4.0f;

struct Delay {
    vital::StereoDelay delay {0};
    vital::Output in_params[(int)Params::Count];
    vital::poly_float in_buffer[vital::kMaxBufferSize];
};

Delay* Create() {
    auto delay = new Delay();
    for (int i = 0; i < (int)Params::Count; ++i) {
        unsigned index = -1;
        switch ((Params)i) {
            case Params::TimeLeftHz: index = vital::StereoDelay::kFrequency; break;
            case Params::TimeRightHz: index = vital::StereoDelay::kFrequencyAux; break;
            case Params::Feedback: index = vital::StereoDelay::kFeedback; break;
            case Params::Mode: index = vital::StereoDelay::kStyle; break;
            case Params::FilterCutoffSemitones: index = vital::StereoDelay::kFilterCutoff; break;
            case Params::FilterSpread: index = vital::StereoDelay::kFilterSpread; break;
            case Params::Mix: index = vital::StereoDelay::kWet; break;
            case Params::Count: break;
        }
        delay->delay.plug(&delay->in_params[i], index);
    }
    return delay;
}

void Destroy(Delay* delay) { delete delay; }

void Process(Delay& delay, ProcessDelayArgs args) {
    assert(args.num_frames <= vital::kMaxBufferSize);

    for (int i = 0; i < (int)Params::Count; ++i) {
        // The delay module only ever looks at the first value in the buffer
        delay.in_params[i].buffer[0] = vital::poly_float::init(args.params[i]);
    }

    for (int i = 0; i < args.num_frames; ++i)
        delay.in_buffer[i] = vital::utils::toPolyFloatFromUnaligned(&args.in_interleaved[i * 2]);

    delay.delay.processWithInput(delay.in_buffer, args.num_frames);

    for (int i = 0; i < args.num_frames; ++i) {
        auto const& o = delay.delay.output()->buffer[i];
        args.out_interleaved[i * 2 + 0] = o[0];
        args.out_interleaved[i * 2 + 1] = o[1];
    }
}

void HardReset(Delay& delay) { delay.delay.hardReset(); }

void SetSampleRate(Delay& delay, int sample_rate) {
    delay.delay.setSampleRate(sample_rate);
    delay.delay.setMaxSamples(kMaxDelayTime * sample_rate);
}

} // namespace delay

namespace compressor {

// Match the "band" (mid) defaults from vital::MultibandCompressor so a single-band wrapper
// behaves like a sensible general-purpose compressor.
constexpr float kBaseAttackMs = 1.4f;
constexpr float kBaseReleaseMs = 28.0f;

struct Compressor {
    vital::Compressor compressor {kBaseAttackMs, kBaseReleaseMs, kBaseAttackMs, kBaseReleaseMs};
    vital::Output in_params[(int)Params::Count];
    vital::poly_float in_buffer[vital::kMaxBufferSize];
};

Compressor* Create() {
    auto compressor = new Compressor();
    for (int i = 0; i < (int)Params::Count; ++i) {
        unsigned index = -1;
        switch ((Params)i) {
            case Params::UpperThresholdDb: index = vital::Compressor::kUpperThreshold; break;
            case Params::LowerThresholdDb: index = vital::Compressor::kLowerThreshold; break;
            case Params::UpperRatio: index = vital::Compressor::kUpperRatio; break;
            case Params::LowerRatio: index = vital::Compressor::kLowerRatio; break;
            case Params::OutputGainDb: index = vital::Compressor::kOutputGain; break;
            case Params::Attack: index = vital::Compressor::kAttack; break;
            case Params::Release: index = vital::Compressor::kRelease; break;
            case Params::Mix: index = vital::Compressor::kMix; break;
            case Params::Count: break;
        }
        compressor->compressor.plug(&compressor->in_params[i], index);
    }
    return compressor;
}

void Destroy(Compressor* compressor) { delete compressor; }

void Process(Compressor& compressor, ProcessCompressorArgs args) {
    assert(args.num_frames <= vital::kMaxBufferSize);

    for (int i = 0; i < (int)Params::Count; ++i) {
        // The compressor module only ever looks at the first value in the buffer
        compressor.in_params[i].buffer[0] = vital::poly_float::init(args.params[i]);
    }

    for (int i = 0; i < args.num_frames; ++i)
        compressor.in_buffer[i] = vital::utils::toPolyFloatFromUnaligned(&args.in_interleaved[i * 2]);

    compressor.compressor.processWithInput(compressor.in_buffer, args.num_frames);

    for (int i = 0; i < args.num_frames; ++i) {
        auto const& o = compressor.compressor.output()->buffer[i];
        args.out_interleaved[i * 2 + 0] = o[0];
        args.out_interleaved[i * 2 + 1] = o[1];
    }
}

void HardReset(Compressor& compressor) { compressor.compressor.reset(vital::constants::kFullMask); }

void SetSampleRate(Compressor& compressor, int sample_rate) { compressor.compressor.setSampleRate(sample_rate); }

} // namespace compressor

} // namespace vitfx
