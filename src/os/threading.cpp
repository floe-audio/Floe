// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

thread_local DynamicArrayBounded<char, k_max_thread_name_size> g_thread_name {};

thread_local u8 g_is_logical_main_thread = 0;

namespace detail {

void AssertThreadNameIsValid(String name) {
    ASSERT(name.size < k_max_thread_name_size, "Thread name is too long");
    for (auto c : name)
        ASSERT(c != ' ' && c != '_' && !IsUppercaseAscii(c),
               "Thread names must be lowercase and not contain spaces");
}

void SetThreadLocalThreadName(String name) {
    AssertThreadNameIsValid(name);
    dyn::Assign(g_thread_name, name);
    tracy::SetThreadName(dyn::NullTerminated(g_thread_name));
}

Optional<String> GetThreadLocalThreadName() {
    if (!g_thread_name.size) return k_nullopt;
    return g_thread_name.Items();
}

} // namespace detail
