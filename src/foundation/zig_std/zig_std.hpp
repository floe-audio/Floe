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
