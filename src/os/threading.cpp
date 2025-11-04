// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threading.hpp"

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"
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

TEST_CASE(TestFuture) {
    SUBCASE("future lifecycle states") {
        Future<int> future;

        // Initially inactive
        CHECK(future.IsInactive());
        CHECK(!future.IsFinished());
        CHECK(!future.IsInProgress());
        CHECK(!future.IsCancelled());

        // Set to pending
        future.SetPending();
        CHECK(!future.IsInactive());
        CHECK(future.IsInProgress());
        CHECK(!future.IsFinished());

        // Simulate TrySetRunning success
        CHECK(future.TrySetRunning());
        CHECK(future.IsInProgress());
        CHECK(!future.IsFinished());

        // Set result
        future.SetResult(123);
        CHECK(!future.IsInProgress());
        CHECK(future.IsFinished());
        CHECK(future.HasResult());
        CHECK_EQ(future.Result(), 123);

        // Reset back to inactive
        future.Reset();
        CHECK(future.IsInactive());
        CHECK(!future.IsFinished());
    }

    SUBCASE("future cancellation before running") {
        Future<int> future;
        future.SetPending();

        CHECK(future.Cancel());
        CHECK(future.IsCancelled());
        CHECK(future.IsInProgress());

        // TrySetRunning should fail
        CHECK(!future.TrySetRunning());
        CHECK(future.IsCancelled());
        CHECK(future.IsInactive());
    }

    SUBCASE("future cancellation after finishing") {
        Future<int> future;
        future.SetPending();
        CHECK(future.TrySetRunning());
        future.SetResult(456);

        // Cancel after finishing should return false
        CHECK(!future.Cancel());
        CHECK(future.IsFinished());
        CHECK_EQ(future.Result(), 456);
    }

    SUBCASE("multiple cancel calls") {
        Future<int> future;
        future.SetPending();

        CHECK(future.Cancel());
        CHECK(future.IsCancelled());

        // Second cancel should still return true (already cancelled)
        CHECK(future.Cancel());
        CHECK(future.IsCancelled());

        CHECK(!future.TrySetRunning());

        CHECK(!future.ShutdownAndRelease());
    }

    SUBCASE("waiting") {
        Future<int> future;
        Thread thread;

        future.SetPending();

        thread.Start(
            [&future]() {
                ASSERT(future.TrySetRunning());
                SleepThisThread(20);
                future.SetResult(100);
            },
            "future-thread");
        DEFER { thread.Join(); };

        CHECK(future.WaitUntilFinished(2000u));
        CHECK(future.IsFinished());
        CHECK_EQ(future.Result(), 100);
    }

    SUBCASE("stress test") {
        Future<u32> future;
        Thread producer;
        Atomic<u32> consumer_round_ready {(u32)-1};
        Atomic<u32> producer_round_ready {(u32)-1};
        constexpr u32 k_num_rounds = 2000;

        auto const random_pause = [](auto& seed) {
            if (RandomIntInRange(seed, 0, 3) != 0) return;

            switch (RandomIntInRange(seed, 0, 3)) {
                case 0:
                case 1: YieldThisThread(); break;
                case 2: SleepThisThread(RandomIntInRange(seed, 0, 2)); break;
                case 3: SpinLoopPause(); break;
            }
        };

        producer.Start(
            [&]() {
                auto seed = RandomSeed();

                for (auto const round : Range(k_num_rounds)) {
                    // Wait for consumer to be ready for this round
                    while (consumer_round_ready.Load(LoadMemoryOrder::Acquire) != round)
                        WaitIfValueIsExpected(consumer_round_ready,
                                              consumer_round_ready.Load(LoadMemoryOrder::Relaxed),
                                              1000u);

                    // Signal producer ready
                    producer_round_ready.Store(round, StoreMemoryOrder::Release);
                    WakeWaitingThreads(producer_round_ready, NumWaitingThreads::All);

                    random_pause(seed);

                    if (future.TrySetRunning()) {
                        ASSERT(future.IsInProgress());
                        ASSERT(!future.IsFinished());

                        random_pause(seed); // Work simulation

                        future.SetResult(round);
                        // We cannot touch the future at this point.
                    } else {
                        // Was cancelled - future should be inactive with cancel bit.
                        // We cannot touch the future at this point.
                    }
                }
            },
            "producer");

        auto seed = RandomSeed();

        for (auto const round : Range(k_num_rounds)) {
            CHECK(future.IsInactive());
            future.SetPending();

            // Signal consumer ready
            consumer_round_ready.Store(round, StoreMemoryOrder::Release);
            WakeWaitingThreads(consumer_round_ready, NumWaitingThreads::All);

            // Wait for producer to be ready for this round
            while (producer_round_ready.Load(LoadMemoryOrder::Acquire) != round)
                WaitIfValueIsExpected(producer_round_ready,
                                      producer_round_ready.Load(LoadMemoryOrder::Relaxed),
                                      1000u);

            random_pause(seed);

            switch (RandomIntInRange(seed, 0, 2)) {
                case 0: { // Cancel
                    auto const cancelled = future.Cancel();
                    if (cancelled) CHECK(future.IsCancelled());

                    CHECK(future.WaitUntilFinished(5000u));
                    if (cancelled) CHECK(future.IsCancelled());
                    if (future.IsInactive()) {
                        // Fine, the producer was cancelled before starting.
                    } else if (future.IsFinished()) {
                        // Didn't manage to change before producer set result, but still safe.
                        REQUIRE(future.HasResult());
                        CHECK(future.Result() == round);
                        future.Reset();
                    }

                    break;
                }
                case 1: { // Wait for result
                    CHECK(future.WaitUntilFinished(5000u));

                    // Should have correctly completed.
                    REQUIRE(future.HasResult());
                    CHECK(future.Result() == round);
                    future.Reset();

                    break;
                }
                case 2: { // ShutdownAndRelease
                    if (auto const result = future.ShutdownAndRelease()) CHECK(*result == round);

                    CHECK(future.IsInactive());
                    break;
                }
            }
        }

        producer.Join();
    }

    return k_success;
}

TEST_CASE(TestCallOnce) {
    CallOnceFlag flag {};
    int i = 0;
    CHECK(!flag.Called());
    CallOnce(flag, [&]() { i = 1; });
    CHECK(flag.Called());
    CHECK_EQ(i, 1);
    CallOnce(flag, [&]() { i = 2; });
    CHECK_EQ(i, 1);
    return k_success;
}

int g_global_int = 0;

TEST_CASE(TestThread) {
    Thread thread;
    REQUIRE(!thread.Joinable());

    thread.Start(
        []() {
            g_global_int = 1;
            SleepThisThread(1);
        },
        "test-thread");

    REQUIRE(thread.Joinable());
    thread.Join();

    REQUIRE(g_global_int == 1);
    return k_success;
}

TEST_CASE(TestMutex) {
    Mutex m;
    m.Lock();
    m.TryLock();
    m.Unlock();
    return k_success;
}

TEST_CASE(TestFutex) {
    SUBCASE("basic wait and wake") {
        for (auto const wake_mode : Array {NumWaitingThreads::One, NumWaitingThreads::All}) {
            Atomic<u32> atomic {0};

            Thread thread;
            thread.Start(
                [&]() {
                    SleepThisThread(1);
                    atomic.Store(1, StoreMemoryOrder::Release);
                    WakeWaitingThreads(atomic, wake_mode);
                },
                "thread");

            auto const timed_out = !WaitIfValueIsExpectedStrong(atomic, 0, {});
            CHECK(!timed_out);

            thread.Join();
        }
    }

    SUBCASE("timeout when not woken") {
        Atomic<u32> atomic {0};
        CHECK(!WaitIfValueIsExpectedStrong(atomic, 0, 1u));
    }
    return k_success;
}

TEST_REGISTRATION(RegisterThreadingTests) {
    REGISTER_TEST(TestFuture);
    REGISTER_TEST(TestCallOnce);
    REGISTER_TEST(TestThread);
    REGISTER_TEST(TestMutex);
    REGISTER_TEST(TestFutex);
}
