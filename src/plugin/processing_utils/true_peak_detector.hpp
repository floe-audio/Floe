// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// Estimates the "true peak" (inter-sample peak) of a signal using a lightweight 4x-oversampling
// polyphase FIR interpolator. A raw sample-peak reader can miss peaks that occur between samples (for
// example a full-scale sine can reconstruct to above 1.0 between two in-range samples). ITU-R BS.1770
// Annex 2 specifies a similar oversampling FIR for compliance-grade true-peak metering; this is a
// cheaper approximation of that, sized for real-time peak limiting rather than offline measurement.
//
// 6 taps per phase (a Hamming-windowed sinc, normalised to unity DC gain) is the minimum that gives a
// usable estimate -- a shorter, more aggressively windowed filter is too far from a sinc to find
// inter-sample peaks reliably.
//
// The 6 taps span history offsets -2, -1, 0, 1, 2, 3 relative to the sample being estimated, so the
// estimate for a given input sample is only available once 3 further samples have arrived
// (k_group_delay_samples). Callers that need sample-accurate alignment must account for this.
class TruePeakDetector {
  public:
    static constexpr u32 k_taps_per_phase = 6;
    static constexpr u32 k_centre_tap = k_taps_per_phase / 2 - 1; // index of the offset-0 tap
    static constexpr u32 k_group_delay_samples = k_taps_per_phase - 1 - k_centre_tap;
    static constexpr u32 k_num_interpolated_phases = 3; // phases 1, 2, 3 of 4; phase 0 is the sample itself

    TruePeakDetector() {
        // Windowed-sinc polyphase interpolation coefficients. Phase 0 (the original sample) needs no
        // filtering: sinc(0) = 1 and sinc(non-zero integer) = 0 is an exact identity for a normalised
        // sinc interpolator, so only phases 1-3 (fractional offsets 0.25, 0.5, 0.75) are computed here.
        for (auto const phase_index : Range(k_num_interpolated_phases)) {
            auto const phase = (f32)(phase_index + 1) / (f32)(k_num_interpolated_phases + 1);
            f32 sum = 0;
            for (auto const tap_index : Range(k_taps_per_phase)) {
                auto const offset = (f32)tap_index - (f32)k_centre_tap;
                auto const x = offset - phase;
                auto const sinc = (x == 0.0f) ? 1.0f : (Sin(k_pi<f32> * x) / (k_pi<f32> * x));
                auto const window =
                    0.54f - 0.46f * Cos(k_tau<f32> * (f32)tap_index / (f32)(k_taps_per_phase - 1));
                m_coeffs[phase_index][tap_index] = sinc * window;
                sum += sinc * window;
            }
            // Normalise to unity DC gain so a sustained signal isn't systematically over- or
            // under-estimated (the short window leaves the raw coefficients summing to slightly off 1).
            for (auto& coeff : m_coeffs[phase_index])
                coeff /= sum;
        }
    }

    // Push a new sample and return the true-peak estimate (per channel) for the sample k_centre_tap
    // positions ago (see the class comment for why). During the first few calls after
    // construction/Reset, the missing history is zero, which just makes the estimate ramp in rather
    // than being wrong.
    f32x2 EstimatePeak(f32x2 new_sample) {
        for (auto const i : Range(k_taps_per_phase - 1))
            m_history[i] = m_history[i + 1];
        m_history[k_taps_per_phase - 1] = new_sample;

        f32x2 peak = Abs(m_history[k_centre_tap]); // phase 0: the sample at offset 0, unfiltered

        for (auto const phase_index : Range(k_num_interpolated_phases)) {
            f32x2 interpolated {0, 0};
            for (auto const tap_index : Range(k_taps_per_phase))
                interpolated += m_history[tap_index] * m_coeffs[phase_index][tap_index];
            peak = Max(peak, Abs(interpolated));
        }

        return peak;
    }

    void Reset() {
        for (auto& h : m_history)
            h = {};
    }

  private:
    f32x2 m_history[k_taps_per_phase] {};
    f32 m_coeffs[k_num_interpolated_phases][k_taps_per_phase] {};
};
