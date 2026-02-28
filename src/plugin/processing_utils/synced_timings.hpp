// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/audio_utils.hpp"

enum class SyncedTimes {
    // NOLINTBEGIN(readability-identifier-naming)
    // clang-format off
    _1_64T, _1_64, _1_64D,
    _1_32T, _1_32, _1_32D,
    _1_16T, _1_16, _1_16D,
    _1_8T, _1_8, _1_8D,
    _1_4T, _1_4, _1_4D,
    _1_2T, _1_2, _1_2D,
    _1_1T, _1_1, _1_1D,
    _2_1T, _2_1, _2_1D,
    _4_1T, _4_1, _4_1D,
    // NOLINTEND(readability-identifier-naming)
    // clang-format on

    Count,
};

constexpr f64 k_synced_times_ms_at_1_bpm[] = {
    // triplets are whole-note * 2/3
    // dotted are whole-note * 1.5

    // clang-format off
    2500,   3750,   5625,     // 1/64
    5000,   7500,   11250,    // 1/32 
    10000,  15000,  22500,    // 1/16 
    20000,  30000,  45000,    // 1/8 
    40000,  60000,  90000,    // 1/4
    80000,  120000, 180000,   // 1/2
    160000, 240000, 360000,   // 1/1
    320000, 480000, 720000,   // 2/1
    640000, 920000, 1440000,  // 4/1
    // clang-format on
};
static_assert(ToInt(SyncedTimes::Count) == ArraySize(k_synced_times_ms_at_1_bpm));

constexpr f64 SyncedTimeToMs(f64 bpm, SyncedTimes t) {
    auto const result = k_synced_times_ms_at_1_bpm[ToInt(t)] / bpm;
    return result;
}

constexpr f32 SyncedTimeToHz(f64 bpm, SyncedTimes t) { return MsToHz((f32)SyncedTimeToMs(bpm, t)); }
