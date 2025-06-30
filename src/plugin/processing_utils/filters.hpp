// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "stereo_audio_frame.hpp"
#include "volume_fade.hpp"

template <ScalarOrVectorFloat T>
struct OnePoleLowPassFilter {
    using ElementType = UnderlyingTypeOfVecOrScalar<T>;

    T LowPass(T const input, ElementType const cutoff01) {
        if (!prev_is_valid) [[unlikely]] {
            prev_output = input;
            prev_is_valid = true;
            return input;
        }
        T const output = prev_output + (cutoff01 * (input - prev_output));
        prev_output = output;
        return output;
    }

    T LowPass(T const input, ElementType const cutoff01, ElementType* change) {
        if (!prev_is_valid) [[unlikely]] {
            prev_output = input;
            prev_is_valid = true;
            *change = __FLT_MAX__;
            return input;
        }
        T const output = prev_output + (cutoff01 * (input - prev_output));
        *change = Abs(output - prev_output);
        prev_output = output;
        return output;
    }

    void Reset() { prev_is_valid = false; }

    T prev_output {};
    bool prev_is_valid {false};
};

// ===============================================================================
// RBJ filter
// Based on: "Cookbook formulae for audio EQ biquad filter coefficients by Robert Bristow-Johnson
// <rbj@audioimagination.com>"
// https://www.musicdsp.org/en/latest/Filters/197-rbj-audio-eq-cookbook.html

namespace rbj_filter {

struct Data {
    f32 out1 = 0, out2 = 0, in1 = 0, in2 = 0;
};

struct StereoData {
    StereoAudioFrame out1 = {}, out2 = {}, in1 = {}, in2 = {};
};

struct Coeffs {
    f32 b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

struct Filter {
    void Reset() { data = {}; }
    Data data;
    Coeffs coeffs;
};

enum class Type {
    LowPass,
    HighPass,
    BandPassCsg,
    BandPassCzpg,
    Notch,
    AllPass,
    Peaking,
    LowShelf,
    HighShelf,
};

struct Params {
    Type type = Type::LowPass;
    f32 fs = 44100;
    f32 fc = 10000;
    f32 q = 1;
    f32 peak_gain = 0;
    bool q_is_bandwidth = false;
};

inline f32 Process(Data& d, Coeffs const& c, f32 in) {
    f32 const out = (c.b0 * in) + (c.b1 * d.in1) + (c.b2 * d.in2) - (c.a1 * d.out1) - (c.a2 * d.out2);

    d.in2 = d.in1;
    d.in1 = in;
    d.out2 = d.out1;
    d.out1 = out;

    return out;
}

inline StereoAudioFrame Process(StereoData& d, Coeffs const& c, StereoAudioFrame in) {
    auto out = c.b0 * in + c.b1 * d.in1 + c.b2 * d.in2 - c.a1 * d.out1 - c.a2 * d.out2;

    d.in2 = d.in1;
    d.in1 = in;
    d.out2 = d.out1;
    d.out1 = out;

    return out;
}

inline f32 Process(Filter& f, f32 in) { return Process(f.data, f.coeffs, in); }

static Coeffs Coefficients(Params const& p) {
    auto const type = p.type;
    auto const sample_rate = (f64)p.fs;
    auto const frequency = Min((f64)p.fc, sample_rate / 2);
    auto const q = (f64)p.q;
    auto const db_gain = (f64)p.peak_gain;
    bool const q_is_bandwidth = p.q_is_bandwidth;

    // Temporary coefficient variables.
    f64 alpha {};
    f64 a0 {};
    f64 a1 {};
    f64 a2 {};
    f64 b0 {};
    f64 b1 {};
    f64 b2 {};

    if (type >= Type::Peaking) { // Peaking, Low-shelf and High-shelf.
        f64 const a = Pow(10.0, db_gain / 40.0);
        f64 const omega = 2.0 * k_pi<f64> * frequency / sample_rate;
        f64 const tsin = Sin(omega);
        f64 const tcos = Cos(omega);

        if (q_is_bandwidth)
            alpha = tsin * Sinh(Log(2.0) / 2.0 * q * omega / tsin);
        else
            alpha = tsin / (2.0 * q);

        f64 const beta = Sqrt(a) / q;

        switch (type) {
            case Type::Peaking: {
                b0 = 1.0 + alpha * a;
                b1 = -2.0 * tcos;
                b2 = 1.0 - alpha * a;
                a0 = 1.0 + alpha / a;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha / a;
                break;
            }
            case Type::LowShelf: {
                b0 = a * ((a + 1.0) - (a - 1.0) * tcos + beta * tsin);
                b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * tcos);
                b2 = a * ((a + 1.0) - (a - 1.0) * tcos - beta * tsin);
                a0 = (a + 1.0) + (a - 1.0) * tcos + beta * tsin;
                a1 = -2.0 * ((a - 1.0) + (a + 1.0) * tcos);
                a2 = (a + 1.0) + (a - 1.0) * tcos - beta * tsin;
                break;
            }
            case Type::HighShelf: {
                b0 = a * ((a + 1.0) + (a - 1.0) * tcos + beta * tsin);
                b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * tcos);
                b2 = a * ((a + 1.0) + (a - 1.0) * tcos - beta * tsin);
                a0 = (a + 1.0) - (a - 1.0) * tcos + beta * tsin;
                a1 = 2.0 * ((a - 1.0) - (a + 1.0) * tcos);
                a2 = (a + 1.0) - (a - 1.0) * tcos - beta * tsin;
                break;
            }
            default: PanicIfReached();
        }
    } else {
        f64 const omega = 2.0 * k_pi<f64> * frequency / sample_rate;
        f64 const tsin = Sin(omega);
        f64 const tcos = Cos(omega);

        if (q_is_bandwidth)
            alpha = tsin * Sinh(Log(2.0) / 2.0 * q * omega / tsin);
        else
            alpha = tsin / (2.0 * q);

        switch (type) {
            case Type::LowPass: {
                b0 = (1.0 - tcos) / 2.0;
                b1 = 1.0 - tcos;
                b2 = (1.0 - tcos) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            case Type::HighPass: {
                b0 = (1.0 + tcos) / 2.0;
                b1 = -(1.0 + tcos);
                b2 = (1.0 + tcos) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            case Type::BandPassCsg: {
                b0 = tsin / 2.0;
                b1 = 0.0;
                b2 = -tsin / 2;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            case Type::BandPassCzpg: {
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            case Type::Notch: {
                b0 = 1.0;
                b1 = -2.0 * tcos;
                b2 = 1.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            case Type::AllPass: {
                b0 = 1.0 - alpha;
                b1 = -2.0 * tcos;
                b2 = 1.0 + alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * tcos;
                a2 = 1.0 - alpha;
                break;
            }
            default: PanicIfReached();
        }
    }

    if (a0 == 0) a0 = 1;

    // Use a0 to normalise.
    return {
        .b0 = f32(b0 / a0),
        .b1 = f32(b1 / a0),
        .b2 = f32(b2 / a0),
        .a1 = f32(a1 / a0),
        .a2 = f32(a2 / a0),
    };
}

class SmoothedCoefficients {
  public:
    struct State {
        rbj_filter::Coeffs coeffs;
        f32 mix;
    };

    void Set(Params& p) { Set(p.type, p.fs, p.fc, p.q, p.peak_gain); }

    void Set(Type type, f32 sample_rate, f32 fc, f32 Q, f32 gain_db) {
        m_pending_fc = fc;
        m_pending_gain = gain_db;
        m_pending_q = Q;
        m_pending_type = type;

        if (sample_rate != m_sample_rate) {
            // Let's not try and do anything fancy if the sample rate changes, just do a hard reset.

            m_fc.SetBoth(fc);
            m_gain.SetBoth(gain_db);
            m_q.SetBoth(Q);
            m_type = type;
            m_sample_rate = sample_rate;

            RecalculateCoefficientsWithCurrentValues();

            m_remaining_samples = 0;
            return;
        }

        f32 transition_ms;
        {
            // If the change is very small we don't need a very long transition so let's do a little
            // calculation in order to get a transition length that is proportional to the size of the change.

            constexpr f32 k_max_transition_ms = 100;
            constexpr f32 k_min_transition_ms = 4;

            auto const delta_fc = Abs(fc - m_fc.target);
            auto const delta_q = Abs(Q - m_q.target);
            auto const delta_gain = Abs(gain_db - m_gain.target);

            constexpr f32 k_max_fc_delta = 5000;
            constexpr f32 k_max_q_delta = 5;
            constexpr f32 k_max_gain_delta = 24;

            auto const transition_ms_fc = Map(Min(delta_fc, k_max_fc_delta),
                                              0,
                                              k_max_fc_delta,
                                              k_min_transition_ms,
                                              k_max_transition_ms);
            auto const transition_ms_q =
                Map(Min(delta_q, k_max_q_delta), 0, k_max_q_delta, k_min_transition_ms, k_max_transition_ms);
            auto const transition_ms_gain = Map(Min(delta_gain, k_max_gain_delta),
                                                0,
                                                k_max_gain_delta,
                                                k_min_transition_ms,
                                                k_max_transition_ms);

            transition_ms = Max(transition_ms_fc, transition_ms_q, transition_ms_gain);
        }

        m_remaining_samples = int(sample_rate * (transition_ms / 1000.0f));
        m_fc.target = fc;
        m_q.target = Q;
        m_gain.target = gain_db;

        if (type != m_type) {
            // We will actually set the filter when the fade out is completed.
            m_fade.SetAsFadeOut(sample_rate, 20);
        }
    }

    void ResetSmoothing() {
        m_fc.SetBoth(m_pending_fc);
        m_q.SetBoth(m_pending_q);
        m_gain.SetBoth(m_pending_gain);
        m_type = m_pending_type;
        m_remaining_samples = 0;
        m_fade.ForceSetFullVolume();

        RecalculateCoefficientsWithCurrentValues();
    }

    State Value() {
        PerformSmoothingStepIfNeeded();
        auto fade = m_fade.GetFade();
        if (m_fade.IsSilent()) {
            m_type = m_pending_type;
            m_fc.SetBoth(m_pending_fc);
            m_q.SetBoth(m_pending_q);
            m_gain.SetBoth(m_pending_gain);
            RecalculateCoefficientsWithCurrentValues();

            m_fade.SetAsFadeIn(m_sample_rate, 20);
        }
        return {m_coeffs, fade};
    }

    rbj_filter::Coeffs Coeffs() const { return m_coeffs; }

    bool NeedsUpdate() const {
        if (m_remaining_samples) return true;
        if (m_fade.GetCurrentState() != VolumeFade::State::FullVolume) return true;
        return false;
    }

  private:
    void PerformSmoothingStepIfNeeded() {
        if (m_remaining_samples) {
            m_coeffs = Coefficients({.type = m_type,
                                     .fs = m_sample_rate,
                                     .fc = m_fc.Get(m_remaining_samples),
                                     .q = m_q.Get(m_remaining_samples),
                                     .peak_gain = m_gain.Get(m_remaining_samples)});

            --m_remaining_samples;
        }
    }

    void RecalculateCoefficientsWithCurrentValues() {
        m_coeffs = Coefficients({.type = m_type,
                                 .fs = m_sample_rate,
                                 .fc = m_fc.current,
                                 .q = m_q.current,
                                 .peak_gain = m_gain.current});
    }

    struct SmoothedParam {
        void SetBoth(f32 v) {
            target = v;
            current = v;
        }

        f32 Get(int remaining_samples) {
            current += (target - current) / (f32)remaining_samples;
            return current;
        }

        f32 target;
        f32 current;
    };

    VolumeFade m_fade {VolumeFade::State::FullVolume};

    SmoothedParam m_fc {};
    SmoothedParam m_q {};
    SmoothedParam m_gain {};
    Type m_type {};
    f32 m_sample_rate {};

    f32 m_pending_fc {};
    f32 m_pending_q {};
    f32 m_pending_gain {};
    Type m_pending_type {};

    int m_remaining_samples = 0;

    rbj_filter::Coeffs m_coeffs = {};
};

} // namespace rbj_filter

// ===============================================================================
// This code is based on https://github.com/JordanTHarris/VAStateVariableFilter
// Copyright (c) 2015 Jordan Harris
// SPDX-License-Identifier: MIT
// Modified by Sam Windell to integrate with the rest of the codebase

// A state-variable filter algorithm as described in The Art of VA Filter Design, by Vadim Zavalishin

namespace sv_filter {

enum class Type {
    Lowpass,
    Bandpass,
    Highpass,
    UnitGainBandpass,
    BandShelving,
    Notch,
    Allpass,
    Peak,
};

template <ScalarOrVector<f32> FloatType>
struct Data {
    FloatType z1_a = {}, z2_a = {}; // State variables (z^-1).
};

inline f32 ResonanceToQ(f32 resonance) {
    ASSERT(resonance >= 0 && resonance <= 1);
    return 1.0f / (2.0f * (1.0f - resonance));
}

inline f32 SkewResonance(f32 percent) {
    constexpr f32 k_multiplier = 0.95f; // Just to make it sound better.
    return Pow(percent, 4.0f) * k_multiplier;
}

struct CachedHelpers {
    void Update(f32 sample_rate, f32 cutoff, f32 res, f32 shelf_gain = 2.0f) {
        auto const q = ResonanceToQ(res);

        auto calculate_g_coeff = [](f32 sample_rate, f32 cutoff) {
            auto t = 1.0f / sample_rate;
            auto wa = (2.0f / t) * trig_table_lookup::TanTurns(cutoff * t / 2.0f);
            auto g = wa * t / 2.0f;
            return g;
        };

        g_coeff = calculate_g_coeff(sample_rate, cutoff);
        r_coeff = 1.0f / (2.0f * q);
        k_coeff = shelf_gain;

        h_2r = 2.0f * r_coeff;
        h_4r = 4.0f * r_coeff;
        h_2rag = h_2r + g_coeff;
        divisor = 1.0f + (h_2r * g_coeff) + g_coeff * g_coeff;
    }

    f32 g_coeff, r_coeff, k_coeff;
    f32 h_2r, h_4r, h_2rag, divisor;
};

template <ScalarOrVector<f32> FloatType>
inline void Process(FloatType in, FloatType& out, Data<FloatType>& d, Type type, CachedHelpers const& c) {
    auto hp = (in - c.h_2rag * d.z1_a - d.z2_a) / c.divisor;
    auto g = c.g_coeff;
    auto bp = hp * g + d.z1_a;
    auto lp = bp * g + d.z2_a;

    d.z1_a = g * hp + bp;
    d.z2_a = g * bp + lp;

    switch (type) {
        case Type::Lowpass: {
            out = lp;
            break;
        }
        case Type::Bandpass: {
            out = bp;
            break;
        }
        case Type::Highpass: {
            out = hp;
            break;
        }
        case Type::UnitGainBandpass: {
            out = c.h_2r * bp;
            break;
        }
        case Type::BandShelving: {
            auto ubp = c.h_2r * bp;
            auto bshelf = in + ubp * c.k_coeff;
            out = bshelf;
            break;
        }
        case Type::Notch: {
            auto ubp = c.h_2r * bp;
            auto notch = in - ubp;
            out = notch;
            break;
        }
        case Type::Allpass: {
            auto ap = in - (c.h_4r * bp);
            out = ap;
            break;
        }
        case Type::Peak: {
            auto peak = lp - hp;
            out = peak;
            break;
        }
    }
}

namespace detail {

constexpr f32 k_projection_exponent = 2.8f;
constexpr f32 k_projection_min = 10;
constexpr f32 k_projection_max = 20000;
constexpr auto k_projection_range = k_projection_max - k_projection_min;

} // namespace detail

static consteval auto CreateLinearSpaceLookupTable() {
    using namespace detail;

    Array<f32, 256> result {};
    constexpr auto k_max_index = result.size - 1;

    for (auto const i : Range(result.size)) {
        auto t = (f64)i / (f64)k_max_index;
        result[i] = (f32)((constexpr_math::Pow(t, (f64)k_projection_exponent) * (f64)k_projection_range) +
                          (f64)k_projection_min);
    }

    return result;
}

PUBLIC f32 HzToLinear(f32 hz) {
    using namespace detail;
    auto const result = Pow((hz - k_projection_min) * (1 / k_projection_range), 1.0f / k_projection_exponent);
    ASSERT(result >= 0 && result <= 1);
    return result;
}

PUBLIC f32 LinearToHz(f32 linear) {
    ASSERT(linear >= 0 && linear <= 1);
    constexpr auto k_table = CreateLinearSpaceLookupTable();
    constexpr auto k_max_index = (u32)k_table.size - 1;

    auto scaled = linear * k_max_index;
    auto index = (u32)scaled;
    auto x = scaled - (f32)index;
    auto index_next = index + 1;
    if (index_next == k_table.size) index_next = k_max_index;
    return k_table[index] + ((k_table[index_next] - k_table[index]) * x);
}

} // namespace sv_filter
