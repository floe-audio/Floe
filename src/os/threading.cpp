// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

thread_local DynamicArrayBounded<char, k_max_thread_name_size> g_thread_name {};

// We have 2 possible modes:
// - No mutex - just check for concurrent access and return false if there is.
// - Mutex - protect the main thread with a mutex so there is no possible way for concurrent access.
//
// As a plugin, we can't trust the host. Host's SHOULD not have multiple 'main threads' at the same time. This
// actually seems to be strict requirement of both CLAP and VST3 spec. But some hosts do not follow this rule.
//
// For example, pluginval and JUCE's hosting code "My VST3 HostApplication". This is not a hypothetical - it's
// been found in production.
//
// It actually quite simple to protect from this error by using a mutex. The performance cost should be
// incredibly low; correctly behaving hosts will have no contention at all, and incorrectly behaving hosts
// will just have a mutex lock/unlock around the main thread code.
#define PROTECT_MAIN_THREAD_WITH_MUTEX 1

thread_local u8 g_is_logical_main_thread = 0;

#if PROTECT_MAIN_THREAD_WITH_MUTEX
// We use a thin mutex so that we don't have an object that needs a constructor and destructor.
static MutexThinRecursive g_logical_main_thread_mutex {};
#else
static Atomic<u8> g_inside_main_thread {};
#endif

[[nodiscard]] bool EnterLogicalMainThread() {
#if PROTECT_MAIN_THREAD_WITH_MUTEX
    g_logical_main_thread_mutex.Lock();
#else
    // We check for concurrent access. If there is, we return false.
    auto expected = g_is_logical_main_thread;
    if (!g_inside_main_thread.CompareExchangeStrong(expected,
                                                    expected + 1,
                                                    RmwMemoryOrder::AcquireRelease,
                                                    LoadMemoryOrder::Relaxed)) [[unlikely]] {
        // The thread_local and the atomic variable are not in sync meaning there's already a thread
        // that's the logical main thread.
        return false;
    }

#endif
    ++g_is_logical_main_thread;
    return true;
}

// Only called if EnterLogicalMainThread() returned true.
void LeaveLogicalMainThread() {
#if PROTECT_MAIN_THREAD_WITH_MUTEX
    g_logical_main_thread_mutex.Unlock();
#else
    g_inside_main_thread.FetchSub(1, RmwMemoryOrder::Release);
#endif
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
