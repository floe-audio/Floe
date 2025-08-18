// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

static constexpr f32 k_silence_amp_80 = 0.0001f; // -80 dB
static constexpr f32 k_silence_amp_90 = 0.000031622776601683795f; // -90 dB
static constexpr f32 k_silence_amp_70 = 0.00031622776601683794f; // -70 dB
static constexpr f32 k_silence_db_80 = -80.0f;

static inline f32 AmpToDb(f32 a) {
    if (a < k_silence_amp_80) return k_silence_db_80;
    return 20.0f * Log10(a);
}

static inline f32 DbToAmp(f32 d) {
    if (d <= k_silence_db_80) return 0;
    return Pow(10.0f, d / 20.0f);
}

static inline f32 FrequencyToMidiNote(f32 frequency) {
    constexpr f32 k_notes_per_octave = 12;
    constexpr f32 k_midi_0_frequency = 8.1757989156f;
    constexpr f32 k_inv_log_of_2 = 1.44269504089f;
    return k_notes_per_octave * Log(frequency / k_midi_0_frequency) * k_inv_log_of_2;
}

static inline f32 MsToHz(f32 ms) {
    ASSERT(ms > 0.0f);
    return 1.0f / (ms / 1000.0f);
}

static constexpr String k_note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// There is no standard for what to call middle C, we just know it's MIDI note 60. We choose to call it C3.
constexpr u8 k_middle_c_octave = 3;
constexpr s8 k_octave_offset = k_middle_c_octave - (60 / 12);

PUBLIC DynamicArrayBounded<char, 4> NoteName(u7 midi_note) {
    u8 const note_in_octave = midi_note % 12;
    u8 const octave = midi_note / 12;
    return fmt::FormatInline<4>("{}{}", k_note_names[note_in_octave], octave + k_octave_offset);
}

PUBLIC Optional<u7> MidiNoteFromName(String name) {
    for (auto const [note_index, note_name] : Enumerate<u8>(k_note_names)) {
        if (StartsWithCaseInsensitiveAscii(name, note_name)) {
            auto const octave_str = name.SubSpan(note_name.size);
            if (auto const octave = ParseInt(octave_str, ParseIntBase::Decimal)) {
                if (*octave >= (0 + k_octave_offset) && *octave <= (9 + k_octave_offset)) {
                    auto const midi_octave = *octave - k_octave_offset;
                    auto const midi_note = (midi_octave * 12) + note_index;
                    return (u7)Clamp<s64>(midi_note, 0, 127);
                }
            }
        }
    }
    return k_nullopt;
}

// Does seem to be slightly faster than the std::pow version
// Degree 10 approximation of f(x) = 10^(x/20)
// on interval [ -80, 30 ]
// p(x)=(((((((((1.6355469298094385e-17*x+5.5282461566279986e-15)*x+7.8428333214544011e-13)*x+6.305427623813544e-11)*x+3.484653893205508e-9)*x+1.6333727146349808e-7)*x+7.0959264062566253e-6)*x+2.5499434891803803e-4)*x+6.6832945699735961e-3)*x+1.1512732505952211e-1)*x+9.9783786294442659e-1
// Estimated max error: 2.1621536973691397e-3
constexpr f64 DbToAmpApprox(f64 x) {
    f64 u = 1.6355469298094383e-17;
    u = u * x + 5.5282461566279988e-15;
    u = u * x + 7.8428333214544015e-13;
    u = u * x + 6.3054276238135441e-11;
    u = u * x + 3.4846538932055078e-09;
    u = u * x + 1.6333727146349808e-07;
    u = u * x + 7.0959264062566251e-06;
    u = u * x + 0.00025499434891803805;
    u = u * x + 0.0066832945699735963;
    u = u * x + 0.11512732505952211;
    return (u * x) + 0.99783786294442656;
}

// res in range (0, 1) outputs to a curve in range (0.5, infinity)
static inline f32 ResonanceToQ(f32 res) { return 1.0f / (2.0f * (1.0f - res)); }

inline void CopyInterleavedToSeparateChannels(f32* __restrict dest_l,
                                              f32* __restrict dest_r,
                                              f32 const* __restrict interleaved_source,
                                              usize num_frames) {
    usize pos = 0;
    for (auto const i : Range(num_frames)) {
        dest_l[i] = interleaved_source[pos];
        pos += 2;
    }
    pos = 1;
    for (auto const i : Range(num_frames)) {
        dest_r[i] = interleaved_source[pos];
        pos += 2;
    }
}

inline void CopySeparateChannelsToInterleaved(f32* __restrict interleaved_dest,
                                              f32 const* __restrict src_l,
                                              f32 const* __restrict src_r,
                                              usize num_frames) {
    for (auto const i : Range(num_frames))
        interleaved_dest[i * 2] = src_l[i];
    for (auto const i : Range(num_frames))
        interleaved_dest[1 + (i * 2)] = src_r[i];
}
