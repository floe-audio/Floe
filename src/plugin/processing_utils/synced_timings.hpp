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

enum class SyncedTimesType { Straight, Dotted, Triplet };

constexpr SyncedTimesType SyncedTimesType(SyncedTimes t) {
    switch (t) {
        case SyncedTimes::_1_64T:
        case SyncedTimes::_1_32T:
        case SyncedTimes::_1_16T:
        case SyncedTimes::_1_8T:
        case SyncedTimes::_1_4T:
        case SyncedTimes::_1_2T:
        case SyncedTimes::_1_1T:
        case SyncedTimes::_2_1T:
        case SyncedTimes::_4_1T: return SyncedTimesType::Triplet;
        case SyncedTimes::_1_64D:
        case SyncedTimes::_1_32D:
        case SyncedTimes::_1_16D:
        case SyncedTimes::_1_8D:
        case SyncedTimes::_1_4D:
        case SyncedTimes::_1_2D:
        case SyncedTimes::_1_1D:
        case SyncedTimes::_2_1D:
        case SyncedTimes::_4_1D: return SyncedTimesType::Dotted;
        case SyncedTimes::_1_64:
        case SyncedTimes::_1_32:
        case SyncedTimes::_1_16:
        case SyncedTimes::_1_8:
        case SyncedTimes::_1_4:
        case SyncedTimes::_1_2:
        case SyncedTimes::_1_1:
        case SyncedTimes::_2_1:
        case SyncedTimes::_4_1: return SyncedTimesType::Straight;
        case SyncedTimes::Count: break;
    }
    return {};
}

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

constexpr SyncedTimes
LargestSyncedTimeWithinTarget(f64 target_ms,
                              f64 tempo,
                              Optional<enum SyncedTimesType> preferred_type = k_nullopt) {
    SyncedTimes best = SyncedTimes::_1_64T;
    f64 best_ms = 0;
    bool found_fitting = false;
    SyncedTimes fastest = SyncedTimes::_1_64T;
    f64 fastest_ms = 0;
    bool found_any = false;
    for (auto const candidate : EnumIterator<SyncedTimes>()) {
        if (preferred_type && SyncedTimesType(candidate) != *preferred_type) continue;
        auto const ms = SyncedTimeToMs(tempo, candidate);
        if (ms <= 0) continue;
        if (!found_any || ms < fastest_ms) {
            fastest_ms = ms;
            fastest = candidate;
            found_any = true;
        }
        if (ms > target_ms) continue;
        if (!found_fitting || ms > best_ms) {
            best_ms = ms;
            best = candidate;
            found_fitting = true;
        }
    }
    return found_fitting ? best : fastest;
}

// Remapping enum values like this allows us to separate values that cannot ever change (the parameter value),
// with values that we have more control over (DSP code). It helps make things explicit when things change and
// we need to maintain perfect backwards compatibility.
template <typename Type>
PUBLIC SyncedTimes SyncedTimesFromParam(Type param_rate) {
    switch (param_rate) {
        case Type::_1_64T: return SyncedTimes::_1_64T;
        case Type::_1_64: return SyncedTimes::_1_64;
        case Type::_1_64D: return SyncedTimes::_1_64D;
        case Type::_1_32T: return SyncedTimes::_1_32T;
        case Type::_1_32: return SyncedTimes::_1_32;
        case Type::_1_32D: return SyncedTimes::_1_32D;
        case Type::_1_16T: return SyncedTimes::_1_16T;
        case Type::_1_16: return SyncedTimes::_1_16;
        case Type::_1_16D: return SyncedTimes::_1_16D;
        case Type::_1_8T: return SyncedTimes::_1_8T;
        case Type::_1_8: return SyncedTimes::_1_8;
        case Type::_1_8D: return SyncedTimes::_1_8D;
        case Type::_1_4T: return SyncedTimes::_1_4T;
        case Type::_1_4: return SyncedTimes::_1_4;
        case Type::_1_4D: return SyncedTimes::_1_4D;
        case Type::_1_2T: return SyncedTimes::_1_2T;
        case Type::_1_2: return SyncedTimes::_1_2;
        case Type::_1_2D: return SyncedTimes::_1_2D;
        case Type::_1_1T: return SyncedTimes::_1_1T;
        case Type::_1_1: return SyncedTimes::_1_1;
        case Type::_1_1D: return SyncedTimes::_1_1D;
        case Type::_2_1T: return SyncedTimes::_2_1T;
        case Type::_2_1: return SyncedTimes::_2_1;
        case Type::_2_1D: return SyncedTimes::_2_1D;
        case Type::_4_1T: return SyncedTimes::_4_1T;
        case Type::_4_1: return SyncedTimes::_4_1;
        case Type::_4_1D: return SyncedTimes::_4_1D;
        case Type::Count: PanicIfReached(); break;
    }
    return SyncedTimes::_1_8;
}
