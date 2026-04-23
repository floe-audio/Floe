// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

struct Allocator;

enum class CloneType : u8 {
    // NOTE: if the object is a Span of a fundamental type then Shallow or Deep will do the same thing.

    // Only clones the top-level object, not its children.
    Shallow,

    // Recursively clones the object and all its children. Only use this in the following cases:
    // 1. You are using an Allocator that frees all memory at once (e.g. an arena allocator).
    // 2. The children objects have destructors that Free their own memory.
    Deep,
};

template <typename Type>
concept Cloneable = requires(Type const t, Allocator& a) {
    { t.Clone(a, CloneType {}) } -> Same<Type>;
};
