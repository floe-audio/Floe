// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

namespace sample_lib_server {

template <TriviallyCopyable T, void (*Destructor)(T&), void (*Invalidate)(T&) = [](T& v) { v = {}; }>
struct Scoped : T {
    template <typename... Args>
    Scoped(Args&&... args) : T {Forward<Args>(args)...} {}

    ~Scoped() { Destructor(*this); }

    NON_COPYABLE(Scoped);

    // Allow moves
    Scoped(Scoped&& other) : T {Move(other)} { Invalidate(other); }
    Scoped& operator=(Scoped&& other) {
        if (this != &other) {
            Destructor(*this);
            T::operator=(Move(other));
            Invalidate(other);
        }
        return *this;
    }
};

using MallocedString = Scoped<String, [](String& s) { GlobalFree({(void*)s.data, s.size}); }>;

inline MallocedString CreateMallocedString(String other) {
    auto const mem = GlobalAllocOversizeAllowed({.size = other.size});
    CopyMemory(mem.data, other.data, other.size);
    return String {(char const*)mem.data, other.size};
}

} // namespace sample_lib_server
