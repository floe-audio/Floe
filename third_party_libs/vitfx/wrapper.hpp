// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <math.h>

namespace vitfx {

namespace reverb {

enum class Params {
    DecayTimeSeconds,
    PreLowPassCutoffSemitones, // 0 to 128
    PreHighPassCutoffSemitones, // 0 to 128
    LowShelfCutoffSemitones, // 0 to 128
    LowShelfGainDb, // -24 to 0
    HighShelfCutoffSemitones, // 0 to 128
    HighShelfGainDb, // -24 to 0
    ChorusAmount, // 0 to 1
    ChorusFrequency, // Hz
    Size, // 0 to 1
    DelaySeconds,
    Mix, // 0 to 1

    Count,
};

struct ProcessReverbArgs {
    int num_frames; // MUST be <= 128

    float const* in_interleaved;
    float* out_interleaved;

    float params[(unsigned)Params::Count];
};

struct Reverb;

Reverb* Create();
void Destroy(Reverb* reverb);
void Process(Reverb& reverb, ProcessReverbArgs args);
void HardReset(Reverb& reverb);
void SetSampleRate(Reverb& reverb, int sample_rate);

} // namespace reverb

namespace phaser {

enum class Params {
    FeedbackAmount, // 0 to 1
    FrequencyHz, // 0.001 to 20
    CenterSemitones, // 8 to 136. center of the phaser filters
    Blend, // 0 to 2. controls the shape of the filter peaks
    ModDepthSemitones, // 0 to 48. size of the range the phaser oscilates
    PhaseOffset, // 0 to 1. offsets the left and right filters. cyclical: 0 == 1 == no-change. could be
                 // displayed in degrees 0 to 360
    Mix, // 0 to 1

    Count,
};

struct ProcessPhaserArgs {
    int num_frames; // MUST be <= 128

    float const* in_interleaved;
    float* out_interleaved;

    float params[(unsigned)Params::Count];
    float const* center_semitones; // Params::CenterSemitones can be a buffer of values rather than a single
                                   // value. If it's nullptr then it will use the single value from 'params'
                                   // for the whole buffer
};

struct Phaser;

Phaser* Create();
void Destroy(Phaser* phaser);
void Process(Phaser& phaser, ProcessPhaserArgs args);
void HardReset(Phaser& phaser);
void SetSampleRate(Phaser& phaser, int sample_rate);

} // namespace phaser

namespace delay {

enum class Mode {
    Mono,
    Stereo,
    PingPong,
    MidPingPong,
};

enum class Params {
    TimeLeftHz,
    TimeRightHz,
    Feedback, // 0 to 1
    Mode, // enum Mode
    FilterCutoffSemitones, // 8 to 136
    FilterSpread, // 0 to 1
    Mix, // 0 to 1

    Count,
};

struct ProcessDelayArgs {
    int num_frames; // MUST be <= 128

    float const* in_interleaved;
    float* out_interleaved;

    float params[(unsigned)Params::Count];
};

struct Delay;

Delay* Create();
void Destroy(Delay* delay);
void Process(Delay& delay, ProcessDelayArgs args);
void HardReset(Delay& delay);
void SetSampleRate(Delay& delay, int sample_rate);

} // namespace delay

namespace compressor {

enum class Params {
    UpperThresholdDb, // -100 to 12
    LowerThresholdDb, // -100 to 12
    UpperRatio, // 0 to 1. 0 = no compression above upper threshold, 1 = limiter
    LowerRatio, // -1 to 1. positive = upward compression below lower threshold, negative = downward expansion
    OutputGainDb, // -30 to 30
    Attack, // 0 to 1. exponential mapping around a base attack time
    Release, // 0 to 1. exponential mapping around a base release time
    Mix, // 0 to 1

    Count,
};

struct ProcessCompressorArgs {
    int num_frames; // MUST be <= 128

    float const* in_interleaved;
    float* out_interleaved;

    float params[(unsigned)Params::Count];
};

struct Compressor;

Compressor* Create();
void Destroy(Compressor* compressor);
void Process(Compressor& compressor, ProcessCompressorArgs args);
void HardReset(Compressor& compressor);
void SetSampleRate(Compressor& compressor, int sample_rate);

constexpr float kBaseAttackMs = 1.4f;
constexpr float kBaseReleaseMs = 28.0f;

inline float EnvelopeParamToMs(float param_01, float base_ms) {
    if (param_01 < 0.0f) param_01 = 0.0f;
    if (param_01 > 1.0f) param_01 = 1.0f;
    return expf(param_01 * 8.0f - 4.0f) * base_ms;
}

inline float EnvelopeMsToParam(float ms, float base_ms) {
    if (ms <= 0.0f) return 0.0f;
    float p = (logf(ms / base_ms) + 4.0f) / 8.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

inline float AttackParamToMs(float param_01) { return EnvelopeParamToMs(param_01, kBaseAttackMs); }
inline float ReleaseParamToMs(float param_01) { return EnvelopeParamToMs(param_01, kBaseReleaseMs); }
inline float AttackMsToParam(float ms) { return EnvelopeMsToParam(ms, kBaseAttackMs); }
inline float ReleaseMsToParam(float ms) { return EnvelopeMsToParam(ms, kBaseReleaseMs); }

} // namespace compressor

} // namespace vitfx
