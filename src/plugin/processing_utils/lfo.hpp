// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

struct LFO {
    enum class Waveform { None, Sine, Triangle, Sawtooth, Square, RandomSteps, RandomGlide };

    // Scalar xorshift32 (Marsaglia 2003) on random_state. Returns a value in [-1, 1].
    f32 NextRandomBipolar() {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        // Float trick: build a float in [1, 2) then subtract 1 → [0, 1).
        u32 const bits = (random_state >> 9) | __builtin_bit_cast(u32, 1.0f);
        f32 unipolar;
        __builtin_memcpy_inline(&unipolar, &bits, sizeof(unipolar));
        return ((unipolar - 1.0f) * 2.0f) - 1.0f;
    }

    // returns [-1, 1]
    f32 Tick() {
        // We track the phase of the LFO using the method described by Remy Muller:
        // https://www.musicdsp.org/en/latest/Synthesis/152-another-lfo-class.html
        auto const old_phase = phase;
        phase += phase_increment_per_tick;

        if (waveform == Waveform::RandomSteps || waveform == Waveform::RandomGlide) {
            if (phase < old_phase) {
                prev_random = next_random;
                next_random = NextRandomBipolar();
            }

            // RandomSteps
            if (waveform == Waveform::RandomSteps) return prev_random;

            // RandomGlide
            f32 const t = (f32)phase / (f32)(1ull << 32);
            f32 const s = t * t * (3.0f - (2.0f * t)); // Smoothstep
            return prev_random + ((next_random - prev_random) * s);
        }

        auto const index = old_phase >> 24; // top 8 bits is the table index
        auto const frac =
            (old_phase & 0x00FFFFFF) * (1.0f / (f32)(1 << 24)); // bottom 24 bits is the fractional part

        auto const output = LinearInterpolate(frac, table[index], table[index + 1]);
        return (output + 1.0f) - 1.0f;
    }

    void SetRate(f32 sample_rate, f32 new_rate_hz) {
        phase_increment_per_tick = (u32)((256.0f * new_rate_hz / sample_rate) * (f32)(1 << 24));
    }

    void SetWaveform(Waveform w) {
        switch (w) {
            case Waveform::RandomSteps:
            case Waveform::RandomGlide: break; // no lookup table needed
            case Waveform::Sine: {
                for (u32 i = 0; i <= 256; i++)
                    table[i] = trig_table_lookup::SinTurnsPositive((f32)i / 256.0f);

                break;
            }
            case Waveform::Triangle: {
                for (u32 i = 0; i < 64; i++) {
                    table[i] = (f32)i / 64.0f;
                    table[i + 64] = (64 - (f32)i) / 64.0f;
                    table[i + 128] = -(f32)i / 64.0f;
                    table[i + 192] = -(64 - (f32)i) / 64.0f;
                }
                table[256] = 0.0f;
                break;
            }
            case Waveform::Sawtooth: {
                for (u32 i = 0; i < 256; i++)
                    table[i] = 2.0f * ((f32)i / 255.0f) - 1.0f;
                table[256] = -1.0f;
                break;
            }
            case Waveform::Square: {
                for (u32 i = 0; i < 128; i++) {
                    table[i] = 1.0f;
                    table[i + 128] = -1.0f;
                }
                table[256] = 1.0f;
                break;
            }
            case Waveform::None: break;
        }
        waveform = w;
    }

    Waveform waveform {Waveform::None};
    u32 phase = 0;
    u32 phase_increment_per_tick = 0;
    u32 random_state = 1; // xorshift32 state for random waveforms; must be non-zero
    f32 prev_random = 0; // value at start of current cycle, for random waveforms
    f32 next_random = 0; // value at start of next cycle, for random waveforms
    f32 table[257] = {}; // table[0] == table[256] to avoid edge case
};
