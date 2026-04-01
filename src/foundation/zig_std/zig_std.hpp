// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// C++ interface to functions backed by Zig's standard library.

#pragma once

#include "foundation/container/contiguous.hpp"
#include "foundation/universal_defs.hpp"

extern "C" u64 RapidHash64(u64 seed, void const* data, usize size);

inline u64 RapidHash64(void const* data, usize size) { return RapidHash64(0, data, size); }

inline u64 RapidHash64(ContiguousContainer auto const& data) {
    return RapidHash64(0, data.data, data.size * sizeof(data.data[0]));
}

inline u64 RapidHash64Multiple(ContiguousContainerOfContiguousContainers auto const& c_of_c) {
    if (!c_of_c.size) return 0x5a6ef77074ebc84b;

    auto hash = RapidHash64(c_of_c[0]);
    for (auto const i : Range<usize>(1, c_of_c.size)) {
        auto const bytes = ToBytes(c_of_c[i]);
        hash = RapidHash64(hash, bytes.data, bytes.size);
    }
    return hash;
}
