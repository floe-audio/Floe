// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "filters.hpp"
#include "oversampler.hpp"

using param_values::DistortionType;
using param_values::IsLegacyDistortionType;
using param_values::k_distortion_type_strings;

constexpr usize k_num_distortion_types = ToInt(DistortionType::Count);

// Antiderivative anti-aliasing (first order). Holds the one-sample history and the ill-conditioned-path
// branch; each distortion curve supplies only its own value and antiderivative. A curve exposes static
// Value/IntegralDiff/Limit/MinPath and rides ProcessClamped, or (for a stateful pointwise antiderivative)
// exposes Shape/Integral and rides ProcessPointwise.
struct AdaaFirstOrder {
    static constexpr f32 k_min_path = 0.01f;

    // Pointwise antiderivative: the curve exposes Shape(x) and Integral(x). We store the previous integral
    // so Integral runs once per sample. The difference loses precision as it grows, so min_path scales with
    // level. Shape (the fallback at the midpoint) is only evaluated on a short path.
    f32x2 ProcessPointwise(f32x2 input, auto const& curve) {
        auto const prev = Exchange(prev_input, input);
        auto const integral = curve.Integral(input);
        auto const prev_integral_value = Exchange(prev_integral, integral);
        auto const path = input - prev;
        auto const min_path = k_min_path * Max(f32x2(1), Fabs(input), Fabs(prev));
        auto const short_path = Fabs(path) <= min_path;
        auto const averaged = (integral - prev_integral_value) / path;
        if (short_path[0] || short_path[1]) return short_path ? curve.Shape((input + prev) * 0.5f) : averaged;
        return averaged;
    }

    // Curves flat at +-1 beyond +-Curve::k_limit. The curve exposes Value(x) and IntegralDiff(a, b) over the
    // clamped pair; the flat-region tail is added here so nothing cancels. Value (the midpoint fallback) is
    // only evaluated on a short path.
    template <typename Curve>
    f32x2 ProcessClamped(f32x2 input) {
        auto const prev = Exchange(prev_input, input);
        constexpr f32 k_limit = Curve::k_limit;
        auto const clamped = Clamp(input, f32x2(-k_limit), f32x2(k_limit));
        auto const clamped_prev = Clamp(prev, f32x2(-k_limit), f32x2(k_limit));
        auto const above = Max(input - k_limit, f32x2(0)) - Max(prev - k_limit, f32x2(0));
        auto const below = Min(input + k_limit, f32x2(0)) - Min(prev + k_limit, f32x2(0));
        auto const diff = Curve::IntegralDiff(clamped, clamped_prev) + above - below;
        auto const path = input - prev;
        auto const short_path = Fabs(path) <= f32x2(Curve::MinPath());
        auto const averaged = diff / path;
        if (short_path[0] || short_path[1])
            return short_path ? Curve::Value((input + prev) * 0.5f) : averaged;
        return averaged;
    }

    void Reset() {
        prev_input = 0;
        prev_integral = 0;
    }

    f32x2 prev_input = 0;
    f32x2 prev_integral = 0;
};

// Shunt diode clipper, v_in = v_out + k * exp(v_out / Vt), solved in closed form by the
// Wright omega function. Each side has its own sharpness (1 / Vt); the shared offset makes
// them meet at the origin.
struct DiodeCurve {
    static f32x2 LogWrightOmega(f32x2 u) {
        // Clamp floor at 1 for log
        auto const u_high = Max(u, f32x2(1));

        // Natural log for large inputs
        auto const log_u = LogFast(u_high);

        // Polynomial approximation for middle inputs
        auto const cubic = ((((0.0026677352f * u) - 0.061137704f) * u + 0.61860213f) * u) - 0.58201244f;

        // Select initial guess by range
        auto v = u < -3.5f ? u : (u < 8 ? cubic : log_u - (log_u / u_high));

        // Two iterations of Newton-Raphson
        for (auto _ : Range(2)) {
            auto const exp_v = ExpFast(v);
            v -= (exp_v + v - u) / (exp_v + 1);
        }

        return v;
    }

    static constexpr DiodeCurve ForType(DistortionType type) {
        if (type == DistortionType::Valve)
            return {.sharpness_pos = 8,
                    .sharpness_neg = 1.5f,
                    .offset = -6,
                    .log_omega_at_offset = -6.0024726f};
        return {.sharpness_pos = 12, .sharpness_neg = 6, .offset = -8, .log_omega_at_offset = -8.0003354f};
    }

    // (ln(omega(s * x + c)) - ln(omega(c))) / s
    f32x2 Shape(f32x2 x) const {
        auto const sharpness = x >= 0 ? f32x2(sharpness_pos) : f32x2(sharpness_neg);
        auto const v = LogWrightOmega((sharpness * Fabs(x)) + offset);
        return Copysign((v - log_omega_at_offset) / sharpness, x);
    }

    // Integral of ln(omega(u)) is u * (v - 1) + v - v^2 / 2 with v = ln(omega(u)).
    f32x2 Integral(f32x2 x) const {
        auto const sharpness = x >= 0 ? f32x2(sharpness_pos) : f32x2(sharpness_neg);
        auto const magnitude = Fabs(x);
        auto const u = (sharpness * magnitude) + offset;
        auto const v = LogWrightOmega(u);
        auto const c = offset;
        auto const v_c = log_omega_at_offset;
        auto const log_omega_integral =
            ((u * (v - 1)) + v - (v * v * 0.5f)) - ((c * (v_c - 1)) + v_c - (v_c * v_c * 0.5f));
        return (log_omega_integral / (sharpness * sharpness)) - (v_c * magnitude / sharpness);
    }

    f32 sharpness_pos;
    f32 sharpness_neg;
    f32 offset;
    f32 log_omega_at_offset; // precomputed ln(omega(offset))
};

struct HardClipCurve {
    static constexpr f32 k_limit = 1;

    static constexpr f32 MinPath() { return 0; }

    static f32x2 Value(f32x2 x) { return Clamp(x, f32x2(-1), f32x2(1)); }

    static f32x2 IntegralDiff(f32x2 a, f32x2 b) { return (a - b) * (a + b) * 0.5f; }
};

struct SineFoldCurve {
    // Bounds the number of folds. Sits on a peak of the sine so the curve joins the flat top smoothly.
    static constexpr f32 k_limit = 2.5f * k_pi<>;

    static constexpr f32 MinPath() { return 0; }

    static f32x2 Value(f32x2 x) { return Sin(Clamp(x, f32x2(-k_limit), f32x2(k_limit))); }

    static f32x2 IntegralDiff(f32x2 a, f32x2 b) {
        // cos(b) - cos(a), written as a product of sines.
        // Sin() is actually faster here than SinFast() in our benchmarks.
        return 2 * Sin((a + b) * 0.5f) * Sin((a - b) * 0.5f);
    }
};

// Short paths take the midpoint outright: the curve is straight between its corners.
struct WavefolderCurve {
    static f32x2 WrapToFoldPeriod(f32x2 x) { return x - (4 * Floor(x * 0.25f)); }

    // Triangle wave of x, equal to x inside [-1, 1].
    static f32x2 TriangleFold(f32x2 x) { return Fabs(WrapToFoldPeriod(x - 1) - 2) - 1; }

    // Written as the area of the current segment so nothing cancels.
    static f32x2 TriangleFoldIntegral(f32x2 x) {
        auto const u = WrapToFoldPeriod(x - 1);
        auto const height = Fabs(u - 2);
        auto const area = height * (2 - height) * 0.5f;
        return u <= 2 ? area : -area;
    }

    // Bounds the number of folds. Sits on a peak of the triangle so the curve joins the flat top smoothly.
    static constexpr f32 k_limit = 5;

    static constexpr f32 MinPath() { return AdaaFirstOrder::k_min_path; }

    static f32x2 Value(f32x2 x) { return TriangleFold(Clamp(x, f32x2(-k_limit), f32x2(k_limit))); }

    static f32x2 IntegralDiff(f32x2 a, f32x2 b) { return TriangleFoldIntegral(a) - TriangleFoldIntegral(b); }
};

static inline f32x2 Sinc(f32x2 x) {
    auto const initial_x = x;
    x = x == 0.0f ? 1.0f : x;
    x *= k_pi<>;
    return initial_x == 0.0f ? f32x2(1) : SinFast(x) / x;
}

struct DriveRangeDb {
    static constexpr DriveRangeDb ForType(DistortionType type) {
        switch (type) {
            case DistortionType::Tape: return {.min = -12, .max = 48, .skew = 0.6f};
            case DistortionType::Valve: return {.min = -8, .max = 50, .skew = 0.5f};
            case DistortionType::HardClip: return {.min = 0, .max = 50, .skew = 0.8f};
            case DistortionType::Overdrive: return {.min = 0, .max = 40, .skew = 0.6f};
            case DistortionType::SineFold: return {.min = 0, .max = 40, .skew = 0.5f};
            case DistortionType::Warp: return {.min = 0, .max = 36, .skew = 0.5f};
            case DistortionType::Wavefolder: return {.min = 0, .max = 40, .skew = 0.5f};
            case DistortionType::Octave:
            case DistortionType::Bitcrush:
            case DistortionType::RingMod: return {.min = -12, .max = 36, .skew = 1};
            case DistortionType::LegacyTubeLog:
            case DistortionType::LegacyTubeAsym3:
            case DistortionType::LegacySine:
            case DistortionType::LegacyRaph1:
            case DistortionType::LegacyDecimate:
            case DistortionType::LegacyAtan:
            case DistortionType::LegacyClip:
            case DistortionType::LegacyFoldback:
            case DistortionType::LegacyRectifier:
            case DistortionType::LegacyRingMod:
            case DistortionType::Count: PanicIfReached();
        }
    }

    f32 min;
    f32 max;
    f32 skew;
};

constexpr f32 k_legacy_drive_max_gain = 60;

inline f32 DistortionInputGain(DistortionType type, f32 drive01) {
    ASSERT_HOT(drive01 >= 0 && drive01 <= 1);
    if (IsLegacyDistortionType(type)) return (drive01 * (k_legacy_drive_max_gain - 1)) + 1;
    auto const range = DriveRangeDb::ForType(type);
    return DbToAmp(range.min + ((range.max - range.min) * Pow(drive01, range.skew)));
}

inline f32 DistortionDriveFromInputGain(DistortionType type, f32 input_gain) {
    if (IsLegacyDistortionType(type))
        return Clamp((input_gain - 1) / (k_legacy_drive_max_gain - 1), 0.0f, 1.0f);
    auto const range = DriveRangeDb::ForType(type);
    auto const position = (AmpToDb(input_gain) - range.min) / (range.max - range.min);
    return Pow(Clamp(position, 0.0f, 1.0f), 1 / range.skew);
}

// Shaper: one stage's curve
struct DistortionShaper {
    DistortionShaper() { Reset(); }

    f32x2 Shape(f32x2 input, DistortionType type, f32 drive01, f32 input_gain, f32 bias) {
        ASSERT_HOT(drive01 >= 0 && drive01 <= 1);
        f32x2 output = 0;
        input = (input * input_gain) + bias;

        switch (type) {
            case DistortionType::Tape: {
                output = Tanh(input);
                break;
            }
            case DistortionType::Valve:
            case DistortionType::Overdrive: {
                output = adaa.ProcessPointwise(input, DiodeCurve::ForType(type));
                break;
            }
            case DistortionType::HardClip: {
                output = adaa.ProcessClamped<HardClipCurve>(input);
                break;
            }
            case DistortionType::SineFold: {
                output = adaa.ProcessClamped<SineFoldCurve>(input);
                break;
            }
            case DistortionType::Wavefolder: {
                output = adaa.ProcessClamped<WavefolderCurve>(input);
                break;
            }
            case DistortionType::Warp:
            case DistortionType::LegacyRaph1: {
                output = (input < 0) ? (ExpFast(input) - 1.0f - Sinc(3.0f + input))
                                     : (1.0f - ExpFast(-input) + Sinc(input - 3.0f));
                break;
            }
            case DistortionType::Octave: {
                // DC is removed before saturating, as an octave fuzz's coupling cap does.
                auto const mix = drive01;
                auto const rectified = Fabs(input);
                output = input * (1 - mix) + rectified * mix;
                octave_dc += (output - octave_dc) * octave_dc_cutoff;
                output -= octave_dc;
                output = Tanh(output * (1 + drive01 * 2));
                break;
            }
            case DistortionType::Bitcrush: {
                constexpr int k_decimate_bits = 16;
                constexpr f32 k_m = 1 << (k_decimate_bits - 1);

                // Bowed: the first octave below the base rate is mostly above hearing.
                auto const hold_length = (f32)Oversampler4x::k_factor * Pow(50.0f, Sqrt(drive01));
                decimate_cnt += 1.0f / hold_length;

                if (decimate_cnt >= 1) {
                    decimate_cnt -= 1;
                    decimate_y = Trunc(input * k_m) / k_m;
                }
                output = Tanh(decimate_y);
                break;
            }
            case DistortionType::RingMod: {
                auto const freq = 50 + (drive01 * 200);
                ring_phase += freq * ring_phase_inc_per_hz;
                if (ring_phase > k_tau<>) ring_phase -= k_tau<>;

                auto const modulator = Sin(ring_phase);
                auto const ring_amount = drive01;
                output = input * (1 - ring_amount + ring_amount * modulator);
                output = Tanh(output * (1 + drive01));
                break;
            }

            // Legacy types, kept as they were.
            case DistortionType::LegacyTubeLog: {
                output = Copysign(Log(1 + Fabs(input)), input);
                // Log is unbounded, so a hot input grows without limit.
                output = Clamp(output, f32x2(-20), f32x2(20));
                break;
            }
            case DistortionType::LegacyTubeAsym3: {
                // Without the clamp Exp overflows to NaN on hot input.
                input = Clamp(input, f32x2(-30), f32x2(30));
                auto const a = Exp(input - 1);
                auto const b = Exp(-input);
                auto const num = a - b - (1 / Exp(1.0f)) + 1;
                auto const denom = a + b;

                output = (num / denom);
                break;
            }
            case DistortionType::LegacySine: {
                output = Sin(input);
                break;
            }
            case DistortionType::LegacyDecimate: {
                // Buggy legacy version - no actual sample-and-hold ever happens.
                constexpr int k_decimate_bits = 16;
                constexpr f32 k_m = 1 << (k_decimate_bits - 1);

                auto const amount = (drive01 * 199) + 1;
                decimate_cnt += amount + ((1.0f - amount) * 0.165f);

                if (decimate_cnt >= 1) {
                    decimate_cnt -= 1;
                    decimate_y = Trunc(input * k_m) / k_m;
                }
                output = Tanh(decimate_y);
                break;
            }
            case DistortionType::LegacyAtan: {
                auto const amount = (drive01 * (k_legacy_drive_max_gain - 1) + 1) / 4;
                output = (1.0f / Atan(amount)) * Atan(input * amount);
                break;
            }
            case DistortionType::LegacyClip: {
                output = input >= 0 ? Min(input, f32x2(1.0f)) : Max(input, f32x2(-1.0f));
                break;
            }
            case DistortionType::LegacyFoldback: {
                auto const threshold = 0.5f + (drive01 * 0.4f);
                auto abs_input = Fabs(input);
                auto sign = Copysign(f32x2(1), input);

                output =
                    abs_input > threshold ? sign * Max(threshold - (abs_input - threshold), f32x2(0)) : input;
                output = Tanh(output * (1 + drive01));
                break;
            }
            case DistortionType::LegacyRectifier: {
                // Buggy legacy version - saturates with the DC still in, so a hot signal flattens out.
                auto const mix = drive01;
                auto const rectified = Fabs(input);
                output = input * (1 - mix) + rectified * mix;
                output = Tanh(output * (1 + drive01 * 2));
                break;
            }
            case DistortionType::LegacyRingMod: {
                // Buggy legacy version - assumes a 44.1k sample rate.
                auto const freq = 50 + (drive01 * 200);
                ring_phase += freq * k_tau<> / 44100.0f;
                if (ring_phase > k_tau<>) ring_phase -= k_tau<>;

                auto const modulator = Sin(ring_phase);
                auto const ring_amount = drive01;
                output = input * (1 - ring_amount + ring_amount * modulator);
                output = Tanh(output * (1 + drive01));
                break;
            }
            case DistortionType::Count: PanicIfReached(); break;
        }

        return output;
    }

    void SetSampleRate(f32 sample_rate) {
        ring_phase_inc_per_hz = k_tau<> / sample_rate;
        octave_dc_cutoff = OnePoleLowPassFilter<f32x2>::HzToCutoff(10, sample_rate);
    }

    void Reset() {
        decimate_y = 0;
        decimate_cnt = 0;
        ring_phase = 0;
        adaa.Reset();
        octave_dc = 0;
    }

    f32x2 decimate_y;
    f32 decimate_cnt;
    f32 ring_phase;
    AdaaFirstOrder adaa;
    f32x2 octave_dc;
    f32 ring_phase_inc_per_hz = k_tau<> / 44100.0f;
    f32 octave_dc_cutoff = OnePoleLowPassFilter<f32x2>::HzToCutoff(10, 44100.0f * Oversampler4x::k_factor);
};

// Measured makeup gains: the gain that brings the loudest K-weighted output of probes at and below the
// reference level back to the reference loudness. Generated by "zig build script:gen-distortion-table".
struct DistortionNormTable {
    static constexpr u32 k_num_buckets = 65;
    static constexpr u32 k_num_punish_buckets = 13; // multiples of 1/12 so each stage's fade-in is on-grid
    static constexpr u32 k_num_punish_drive_buckets = 33;
    static constexpr u32 k_max_stages = 4;
    using DriveCurve = Array<f32, k_num_buckets>;
    using PunishSurface = Array<Array<f32, k_num_punish_drive_buckets>, k_num_punish_buckets>;

    // Square-root spaced: the gains move fastest over the first few percent of drive.
    static constexpr f32 DriveFromBucketPosition(f32 pos01) { return pos01 * pos01; }

    template <usize k_size>
    static f32 LookupAtPosition(Array<f32, k_size> const& curve, f32 pos01) {
        auto const pos = Clamp(pos01, 0.0f, 1.0f) * (k_size - 1);
        auto const lower = (usize)pos;
        auto const upper = Min(lower + 1, k_size - 1);
        return LinearInterpolate(pos - (f32)lower, curve[lower], curve[upper]);
    }

    static f32 Lookup(DriveCurve const& curve, f32 drive01) {
        return LookupAtPosition(curve, Sqrt(Clamp(drive01, 0.0f, 1.0f)));
    }

    static f32 Lookup(PunishSurface const& surface, f32 punish01, f32 drive01) {
        auto const pos = Clamp(punish01, 0.0f, 1.0f) * (k_num_punish_buckets - 1);
        auto const lower = (usize)pos;
        auto const upper = Min(lower + 1, (usize)(k_num_punish_buckets - 1));
        auto const drive_pos = Sqrt(Clamp(drive01, 0.0f, 1.0f));
        return LinearInterpolate(pos - (f32)lower,
                                 LookupAtPosition(surface[lower], drive_pos),
                                 LookupAtPosition(surface[upper], drive_pos));
    }

    Array<DriveCurve, k_num_distortion_types> stage_gain;
    Array<PunishSurface, k_num_distortion_types> punish_gain; // residual left after the per-stage gains
};

#include "distortion_norm_table.hpp"

// The complete wet path:
//   tilt (base rate) -> 4x upsample -> emphasis -> shape -> DC block -> emphasis inverse -> makeup
//   -> [inter-stage LP -> coupling HP -> shape (hotter, biased) -> DC block -> makeup -> blend in] x punish
//   -> residual makeup -> 4x downsample -> DC block
struct DistortionDsp {
    static constexpr u32 k_max_stages = DistortionNormTable::k_max_stages;
    static constexpr u32 k_num_punish_stages = k_max_stages - 1;
    static constexpr u32 k_latency_base_samples = Oversampler4x::k_latency_base_samples;
    // The makeup tables' reference peak. A survey of factory presets playing a 4-note chord at velocity 100
    // sits a few dB hotter than this; pinning the "balanced" point a little below typical material pulls back
    // the boost that quiet inputs otherwise get from the shaper's compression.
    static constexpr f32 k_reference_peak_amplitude = 0.1413f; // -17 dBFS
    static constexpr f32 k_tilt_max_db = 12;
    static constexpr f32 k_tilt_pivot_hz = 1000;
    static constexpr f32 k_punish_max_bias = 0.8f; // in shaper-input units, where the knee sits near 1
    static constexpr f32 k_punish_coupling_min_hz = 10;
    static constexpr f32 k_punish_coupling_max_hz = 50;

    struct Settings {
        DistortionType type = DistortionType::Tape;
        f32 tilt = 0; // -1 (push lows) to 1 (push highs)
        bool compensate = true; // false: raw shaper output
    };

    // Cheap enough to derive every sample, so the caller can smooth these.
    struct Controls {
        f32 drive01;
        f32 punish01 = 0; // 0 (single stage) to 1 (all extra stages fully in)
        f32 auto_gain01 = 1; // 0: the Legacy makeup, 1: the measured table
    };

    void SetSampleRate(f32 base_sample_rate) {
        sample_rate = base_sample_rate;
        auto const oversampled_rate = base_sample_rate * Oversampler4x::k_factor;
        for (auto& shaper : shapers)
            shaper.SetSampleRate(oversampled_rate);
        dc_block_cutoff_4x = OnePoleLowPassFilter<f32x2>::HzToCutoff(10, oversampled_rate);
        dc_block_cutoff_base = OnePoleLowPassFilter<f32x2>::HzToCutoff(10, base_sample_rate);
        interstage_lp_cutoff_4x = OnePoleLowPassFilter<f32x2>::HzToCutoff(10000, oversampled_rate);
        SetSettings(settings);
        Reset();
    }

    void SetSettings(Settings const& new_settings) {
        // The shapers' ADAA state belongs to the old curve.
        if (new_settings.type != settings.type)
            for (auto& shaper : shapers)
                shaper.Reset();
        settings = new_settings;

        auto const tilt_db =
            Clamp(Copysign(settings.tilt * settings.tilt, settings.tilt), -1.0f, 1.0f) * k_tilt_max_db;
        low_shelf_coeffs.Set(rbj_filter::Type::LowShelf, sample_rate, k_tilt_pivot_hz, 0.7071f, -tilt_db);
        high_shelf_coeffs.Set(rbj_filter::Type::HighShelf, sample_rate, k_tilt_pivot_hz, 0.7071f, tilt_db);

        struct EmphasisFilter {
            f32 db;
            f32 hz;
            rbj_filter::Type type;
        };

        auto const emphasis = ({
            EmphasisFilter f {};
            if (settings.type == DistortionType::Tape)
                f = {.db = 12, .hz = 2000, .type = rbj_filter::Type::HighShelf};
            else if (settings.type == DistortionType::Overdrive)
                f = {.db = 8, .hz = 750, .type = rbj_filter::Type::Peaking};
            f;
        });

        emphasis_filter_active = emphasis.db != 0;
        if (emphasis_filter_active) {
            auto const oversampled_rate = sample_rate * (f32)Oversampler4x::k_factor;
            emphasis_pre_coeffs = rbj_filter::Coefficients({.type = emphasis.type,
                                                            .fs = oversampled_rate,
                                                            .fc = emphasis.hz,
                                                            .q = 0.7071f,
                                                            .peak_gain = emphasis.db});
            emphasis_post_coeffs = rbj_filter::Coefficients({.type = emphasis.type,
                                                             .fs = oversampled_rate,
                                                             .fc = emphasis.hz,
                                                             .q = 0.7071f,
                                                             .peak_gain = -emphasis.db});
        }
    }

    void Reset() {
        for (auto& shaper : shapers)
            shaper.Reset();
        for (auto& f : stage_dc_blockers)
            f.Reset();
        for (auto& f : interstage_lps)
            f.Reset();
        for (auto& f : coupling_hps)
            f.Reset();
        output_dc_blocker.Reset();
        oversampler.Reset();
        emphasis_pre_data = {};
        emphasis_post_data = {};
        low_shelf_data = {};
        high_shelf_data = {};
        low_shelf_coeffs.ResetSmoothing();
        high_shelf_coeffs.ResetSmoothing();
    }

    struct PunishStages {
        u32 num_active = 0;
        Array<f32, k_num_punish_stages> weights {};
        f32 bias = 0;
        f32 coupling_hp_cutoff_4x = 0;
    };

    // Punish stages need a monotonic saturating curve; types that fold, rectify or modulate cascade into
    // Tape.
    static constexpr DistortionType PunishStageFunction(DistortionType type) {
        switch (type) {
            case DistortionType::Tape:
            case DistortionType::Valve:
            case DistortionType::Overdrive:
            case DistortionType::LegacyClip:
            case DistortionType::LegacyTubeLog:
            case DistortionType::LegacyTubeAsym3:
            case DistortionType::LegacyAtan:
            case DistortionType::HardClip: return type;
            case DistortionType::Warp:
            case DistortionType::LegacySine:
            case DistortionType::SineFold:
            case DistortionType::Wavefolder:
            case DistortionType::LegacyDecimate:
            case DistortionType::LegacyRaph1:
            case DistortionType::LegacyFoldback:
            case DistortionType::Octave:
            case DistortionType::LegacyRectifier:
            case DistortionType::LegacyRingMod:
            case DistortionType::Bitcrush:
            case DistortionType::RingMod: return DistortionType::Tape;
            case DistortionType::Count: break;
        }
        return DistortionType::Tape;
    }

    PunishStages ComputePunishStages(f32 punish01) const {
        ASSERT_HOT(punish01 >= 0 && punish01 <= 1);
        PunishStages stages;
        for (auto const stage_index : Range(k_num_punish_stages)) {
            auto const weight = Clamp((punish01 * k_num_punish_stages) - (f32)stage_index, 0.0f, 1.0f);
            stages.weights[stage_index] = weight;
            if (weight > 0) stages.num_active = stage_index + 1;
        }
        stages.bias = punish01 * k_punish_max_bias;
        auto const coupling_hz = MapFrom01(punish01, k_punish_coupling_min_hz, k_punish_coupling_max_hz);
        stages.coupling_hp_cutoff_4x =
            OnePoleLowPassFilter<f32x2>::HzToCutoff(coupling_hz, sample_rate * Oversampler4x::k_factor);
        return stages;
    }

    struct PreGains {
        f32 first_stage = 1;
        Array<f32, k_num_punish_stages> punish_stages {};
    };

    PreGains ComputePreGains(Controls const& controls, PunishStages const& punish) const {
        PreGains gains;
        gains.first_stage = DistortionInputGain(settings.type, controls.drive01);
        for (auto const stage_index : Range(punish.num_active))
            gains.punish_stages[stage_index] =
                gains.first_stage * (1 + (controls.punish01 * (f32)(stage_index + 1)));
        return gains;
    }

    struct Makeup {
        f32 first_stage = 1;
        Array<f32, k_num_punish_stages> punish_stages {};
        f32 output_residual = 1;
    };

    Makeup
    TableMakeup(PreGains const& pre_gains, Controls const& controls, PunishStages const& punish) const {
        auto const punish_type = PunishStageFunction(settings.type);
        Makeup makeup;
        makeup.first_stage =
            DistortionNormTable::Lookup(norm_table->stage_gain[ToInt(settings.type)], controls.drive01);
        for (auto const stage_index : Range(punish.num_active))
            makeup.punish_stages[stage_index] = DistortionNormTable::Lookup(
                norm_table->stage_gain[ToInt(punish_type)],
                DistortionDriveFromInputGain(punish_type, pre_gains.punish_stages[stage_index]));
        if (punish.num_active != 0)
            makeup.output_residual =
                DistortionNormTable::Lookup(norm_table->punish_gain[ToInt(settings.type)],
                                            controls.punish01,
                                            controls.drive01);
        return makeup;
    }

    // Undo the pre-gain and apply a fixed curve. Punish stages have no table: swing a unity-clamped level
    // through the stage's curve and restore the output swing to the input swing.
    Makeup
    LegacyMakeup(PreGains const& pre_gains, Controls const& controls, PunishStages const& punish) const {
        auto const punish_type = PunishStageFunction(settings.type);
        Makeup makeup;
        makeup.first_stage = MapFrom01(controls.drive01, 1, 2) / pre_gains.first_stage;
        auto const first_stage_level =
            Min(1.0f, k_reference_peak_amplitude * pre_gains.first_stage) * makeup.first_stage;
        for (auto const stage_index : Range(punish.num_active)) {
            DistortionShaper curve;
            auto const extremes = curve.Shape(f32x2 {first_stage_level, -first_stage_level},
                                              punish_type,
                                              controls.drive01,
                                              pre_gains.punish_stages[stage_index],
                                              punish.bias);
            makeup.punish_stages[stage_index] =
                (2 * first_stage_level) / Max(extremes[0] - extremes[1], 1e-6f);
        }
        return makeup;
    }

    // The table is measured against raw shaper output, so it replaces the Legacy makeup rather than stacking.
    Makeup
    ComputeMakeup(PreGains const& pre_gains, Controls const& controls, PunishStages const& punish) const {
        if (!settings.compensate) return {};
        auto const table = TableMakeup(pre_gains, controls, punish);
        if (controls.auto_gain01 == 1) return table;
        auto const legacy = LegacyMakeup(pre_gains, controls, punish);
        auto const mix = controls.auto_gain01;
        Makeup blended;
        blended.first_stage = LinearInterpolate(mix, legacy.first_stage, table.first_stage);
        for (auto const stage_index : Range(punish.num_active))
            blended.punish_stages[stage_index] =
                LinearInterpolate(mix, legacy.punish_stages[stage_index], table.punish_stages[stage_index]);
        blended.output_residual = LinearInterpolate(mix, legacy.output_residual, table.output_residual);
        return blended;
    }

    f32x2 Process(f32x2 input, Controls controls) {
        controls.drive01 = Clamp(controls.drive01, 0.0f, 1.0f);
        controls.punish01 = Clamp(controls.punish01, 0.0f, 1.0f);
        controls.auto_gain01 = Clamp(controls.auto_gain01, 0.0f, 1.0f);
        auto const punish = ComputePunishStages(controls.punish01);
        auto const pre_gains = ComputePreGains(controls, punish);
        auto const makeup = ComputeMakeup(pre_gains, controls, punish);
        auto const punish_type = PunishStageFunction(settings.type);

        auto const tilted =
            rbj_filter::Process(high_shelf_data,
                                high_shelf_coeffs.Value().coeffs,
                                rbj_filter::Process(low_shelf_data, low_shelf_coeffs.Value().coeffs, input));

        f32x2 oversampled[Oversampler4x::k_factor];
        oversampler.Upsample(tilted, oversampled);

        for (auto& x : oversampled) {
            // Apply filter
            if (emphasis_filter_active) x = rbj_filter::Process(emphasis_pre_data, emphasis_pre_coeffs, x);

            // Saturate
            x = shapers[0].Shape(x, settings.type, controls.drive01, pre_gains.first_stage, 0);
            x = stage_dc_blockers[0].HighPass(x, dc_block_cutoff_4x);

            // Invert filter
            if (emphasis_filter_active) x = rbj_filter::Process(emphasis_post_data, emphasis_post_coeffs, x);

            x *= makeup.first_stage;

            for (auto const stage_index : Range(punish.num_active)) {
                auto const smoothed = interstage_lps[stage_index].LowPass(x, interstage_lp_cutoff_4x);
                auto const coupled =
                    coupling_hps[stage_index].HighPass(smoothed, punish.coupling_hp_cutoff_4x);
                auto shaped = shapers[stage_index + 1].Shape(coupled,
                                                             punish_type,
                                                             controls.drive01,
                                                             pre_gains.punish_stages[stage_index],
                                                             punish.bias);
                shaped = stage_dc_blockers[stage_index + 1].HighPass(shaped, dc_block_cutoff_4x);
                shaped *= makeup.punish_stages[stage_index];
                x = LinearInterpolate(punish.weights[stage_index], x, shaped);
            }

            x *= makeup.output_residual;
        }

        auto const wet = oversampler.Downsample(oversampled);
        return output_dc_blocker.HighPass(wet, dc_block_cutoff_base);
    }

    Settings settings {};
    DistortionNormTable const* norm_table = &k_distortion_norm_table;
    f32 sample_rate = 44100;
    f32 dc_block_cutoff_4x = 0;
    f32 dc_block_cutoff_base = 0;
    f32 interstage_lp_cutoff_4x = 1;

    // We sometimes apply a shelf/peak before shaping (and invert it after).
    bool emphasis_filter_active = false;
    rbj_filter::Coeffs emphasis_pre_coeffs {};
    rbj_filter::Coeffs emphasis_post_coeffs {};
    rbj_filter::StereoData emphasis_pre_data {};
    rbj_filter::StereoData emphasis_post_data {};

    Array<DistortionShaper, k_max_stages> shapers {};
    Array<OnePoleLowPassFilter<f32x2>, k_max_stages> stage_dc_blockers {};
    Array<OnePoleLowPassFilter<f32x2>, k_num_punish_stages> interstage_lps {};
    Array<OnePoleLowPassFilter<f32x2>, k_num_punish_stages> coupling_hps {};
    OnePoleLowPassFilter<f32x2> output_dc_blocker {};
    Oversampler4x oversampler {};

    rbj_filter::SmoothedCoefficients low_shelf_coeffs {};
    rbj_filter::SmoothedCoefficients high_shelf_coeffs {};
    rbj_filter::StereoData low_shelf_data {};
    rbj_filter::StereoData high_shelf_data {};
};
