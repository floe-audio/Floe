// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct AudioData {
    usize RamUsageBytes() const { return interleaved_samples.size * sizeof(interleaved_samples[0]); }

    u64 hash {};
    u8 channels {};
    f32 sample_rate {};
    u32 num_frames {};
    Span<f32 const> interleaved_samples {};
};
