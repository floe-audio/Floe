// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

thread_local DynamicArrayBounded<char, k_max_thread_name_size> g_thread_name {};

thread_local u8 g_is_logical_main_thread = 0;
static Atomic<u8> g_inside_main_thread {};

[[nodiscard]] bool EnterLogicalMainThread() {
    auto expected = g_is_logical_main_thread;
    if (!g_inside_main_thread.CompareExchangeStrong(expected,
                                                    expected + 1,
                                                    RmwMemoryOrder::AcquireRelease,
                                                    LoadMemoryOrder::Relaxed)) [[unlikely]] {
        // The thread_local and the atomic variable are not in sync meaning there's already a thread that's
        // the logical main thread.
        return false;
    }

    ++g_is_logical_main_thread;
    return true;
}

void LeaveLogicalMainThread() {
    g_inside_main_thread.FetchSub(1, RmwMemoryOrder::Release);
    --g_is_logical_main_thread;
}

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
