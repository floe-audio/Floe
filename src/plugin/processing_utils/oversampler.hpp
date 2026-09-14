// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// Modified Bessel function of the first kind, order 0.
PUBLIC f64 BesselI0(f64 x) {
    f64 sum = 1;
    f64 term = 1;
    auto const quarter_x_sq = (x * x) / 4.0;
    for (auto const k : Range(1, 40)) {
        term *= quarter_x_sq / ((f64)k * (f64)k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

// Linear-phase half-band FIR for 2x up/down-sampling. Only the centre tap and the taps at an odd distance
// from it are non-zero, so each output needs half the multiplies of a general FIR. The centre tap
// contributes a pure delay, so the filter is stored as the (k_half_order + 1) non-zero side taps.
//
// k_half_order is the group delay in samples at the higher rate and must be odd.
template <u32 k_half_order, u32 k_kaiser_beta_x10>
struct HalfBandFir2x {
    static_assert(k_half_order % 2 == 1);
    static constexpr u32 k_group_delay = k_half_order;
    static constexpr u32 k_num_side_taps = k_half_order + 1;
    static constexpr u32 k_history_size = NextPowerOf2(k_num_side_taps);
    static constexpr u32 k_history_mask = k_history_size - 1;
    static constexpr u32 k_centre_delay = (k_half_order - 1) / 2;
    static constexpr u32 k_delay_size = NextPowerOf2(k_centre_delay + 2);
    static constexpr u32 k_delay_mask = k_delay_size - 1;

    HalfBandFir2x() {
        // Kaiser-windowed ideal half-band. Side tap p sits at distance |2p - k_half_order| from the centre.
        auto const beta = (f64)k_kaiser_beta_x10 / 10.0;
        auto const i0_beta = BesselI0(beta);
        auto const half_length = (f64)k_half_order;
        for (auto const tap_index : Range(k_num_side_taps)) {
            auto const distance = (f64)((2 * (s32)tap_index) - (s32)k_half_order);
            auto const ideal = Sin(k_pi<f64> * distance / 2.0) / (k_pi<f64> * distance);
            auto const window_arg = distance / half_length;
            auto const window = BesselI0(beta * Sqrt(1.0 - (window_arg * window_arg))) / i0_beta;
            side_taps[tap_index] = (f32)(ideal * window);
        }
    }

    // Two output samples per input sample. Gain-compensated for the zero-stuffing.
    void Upsample(f32x2 input, f32x2 out[2]) {
        history[history_pos & k_history_mask] = input;
        out[0] = 2.0f * SideTapSum();
        out[1] = history[(history_pos - k_centre_delay) & k_history_mask];
        ++history_pos;
    }

    // One output sample per two input samples.
    f32x2 Downsample(f32x2 const in[2]) {
        history[history_pos & k_history_mask] = in[0];
        centre_delay[history_pos & k_delay_mask] = in[1];
        auto const result =
            SideTapSum() + (0.5f * centre_delay[(history_pos - k_centre_delay - 1) & k_delay_mask]);
        ++history_pos;
        return result;
    }

    void Reset() {
        for (auto& h : history)
            h = 0;
        for (auto& d : centre_delay)
            d = 0;
        history_pos = 0;
    }

    // Symmetric taps: each coefficient is applied to the pair of samples equidistant from the centre.
    f32x2 SideTapSum() const {
        f32x2 sum = 0;
        for (auto const pair_index : Range(k_num_side_taps / 2)) {
            auto const a = history[(history_pos - pair_index) & k_history_mask];
            auto const b = history[(history_pos - (k_num_side_taps - 1 - pair_index)) & k_history_mask];
            sum += side_taps[pair_index] * (a + b);
        }
        return sum;
    }

    Array<f32, k_num_side_taps> side_taps;
    Array<f32x2, k_history_size> history {};
    Array<f32x2, k_delay_size> centre_delay {};
    u32 history_pos = 0;
};

// 4x oversampling as two cascaded 2x half-band stages. The round trip through two odd-order stages is
// always a half-integer number of base-rate samples, so the downsampling path pads by 2 samples at the
// oversampled rate to land on a whole base-rate latency, letting a dry path be aligned exactly.
struct Oversampler4x {
    static constexpr u32 k_factor = 4;
    using Stage1 = HalfBandFir2x<23, 65>; // base <-> 2x: ~18 kHz passband at 44.1k, >60 dB stop
    using Stage2 = HalfBandFir2x<7, 55>; // 2x <-> 4x: wide transition, short
    static constexpr u32 k_pad_samples = 2;
    static constexpr u32 k_round_trip_4x_samples =
        (4 * Stage1::k_group_delay) + (2 * Stage2::k_group_delay) + k_pad_samples;
    static_assert(k_round_trip_4x_samples % k_factor == 0);
    static constexpr u32 k_latency_base_samples = k_round_trip_4x_samples / k_factor;

    void Upsample(f32x2 input, f32x2 out[k_factor]) {
        f32x2 mid[2];
        up1.Upsample(input, mid);
        up2.Upsample(mid[0], out);
        up2.Upsample(mid[1], out + 2);
    }

    f32x2 Downsample(f32x2 const in[k_factor]) {
        f32x2 padded[k_factor];
        for (auto const i : Range(k_factor)) {
            padded[i] = pad[pad_pos];
            pad[pad_pos] = in[i];
            pad_pos = (pad_pos + 1) % k_pad_samples;
        }
        f32x2 const mid[2] = {down2.Downsample(padded), down2.Downsample(padded + 2)};
        return down1.Downsample(mid);
    }

    void Reset() {
        up1.Reset();
        up2.Reset();
        down1.Reset();
        down2.Reset();
        for (auto& p : pad)
            p = 0;
        pad_pos = 0;
    }

    Stage1 up1 {};
    Stage2 up2 {};
    Stage2 down2 {};
    Stage1 down1 {};
    Array<f32x2, k_pad_samples> pad {};
    u32 pad_pos = 0;
};
