// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

// Waveform
// ================================================================================================

enum class WaveformType : u32 {
    Sine,
    WhiteNoiseMono,
    WhiteNoiseStereo,
    Count,
};

constexpr auto k_waveform_type_names = Array {
    "Sine"_s,
    "White Noise Mono",
    "White Noise Stereo",
};
static_assert(k_waveform_type_names.size == ToInt(WaveformType::Count));

// Instrument
// ================================================================================================

enum class InstrumentType : u32 {
    None,
    WaveformSynth,
    Sampler,
};

// ID
using InstrumentId = TaggedUnion<InstrumentType,
                                 TypeAndTag<WaveformType, InstrumentType::WaveformSynth>,
                                 TypeAndTag<sample_lib::InstrumentId, InstrumentType::Sampler>>;

// For efficiency and simplicity, we sometimes want to just store a raw pointer for the instrument, not the
// RefCounted wrapper. Therefore we unwrap it and used this tagged union instead.
using InstrumentUnwrapped =
    TaggedUnion<InstrumentType,
                TypeAndTag<sample_lib::LoadedInstrument const*, InstrumentType::Sampler>,
                TypeAndTag<WaveformType, InstrumentType::WaveformSynth>>;
