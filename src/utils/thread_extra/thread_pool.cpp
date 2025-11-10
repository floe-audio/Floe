// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thread_pool.hpp"

#include "tests/framework.hpp"
#include "utils/thread_extra/starting_gun.hpp"

TEST_CASE(TestAsync) {
    ThreadPool pool;
    pool.Init("test", 2u);

    auto cleanup = []() {};

    SUBCASE("basic async with return value") {
        Future<int> future;
        CHECK(!future.IsFinished());
        pool.Async(future, []() { return 42; }, cleanup);
        CHECK(future.WaitUntilFinished());
        REQUIRE(future.IsFinished());
        CHECK_EQ(future.Result(), 42);
    }

    SUBCASE("type with no default constructor") {
        struct NoDefault {
            NoDefault(int v) : value(v) {}
            int value;
        };
        Future<NoDefault> future;
        pool.Async(future, []() { return NoDefault(99); }, cleanup);
        CHECK(future.WaitUntilFinished());
        REQUIRE(future.IsFinished());
        CHECK_EQ(future.Result().value, 99);
    }

    SUBCASE("try release result") {
        Future<int> future;

        // No result available initially
        CHECK(!future.TryReleaseResult().HasValue());

        pool.Async(future, []() { return 789; }, cleanup);
        CHECK(future.WaitUntilFinished());

        auto result = future.TryReleaseResult();
        REQUIRE(result.HasValue());
        CHECK_EQ(*result, 789);

        // After releasing, should be inactive
        CHECK(future.IsInactive());

        // Second try should return nullopt
        CHECK(!future.TryReleaseResult().HasValue());
    }

    SUBCASE("release result") {
        Future<int> future;
        pool.Async(future, []() { return 321; }, cleanup);
        CHECK(future.WaitUntilFinished());

        int result = future.ReleaseResult();
        CHECK_EQ(result, 321);
        CHECK(future.IsInactive());
    }

    SUBCASE("shutdown with active future") {
        Future<int> future;
        Atomic<bool> work_started {false};
        Atomic<bool> do_work {false};

        pool.Async(
            future,
            [&]() {
                work_started.Store(true, StoreMemoryOrder::Release);
                while (!do_work.Load(LoadMemoryOrder::Acquire))
                    YieldThisThread();
                return 999;
            },
            cleanup);

        // Wait for work to start
        while (!work_started.Load(LoadMemoryOrder::Acquire))
            YieldThisThread();

        // Do the work
        do_work.Store(true, StoreMemoryOrder::Release);

        CHECK_EQ(*future.ShutdownAndRelease(10000u), 999);
        CHECK(future.IsInactive());
    }

    SUBCASE("multiple futures concurrently") {
        Array<Future<int>, 5> futures;
        Atomic<int> counter {0};

        for (auto [i, future] : Enumerate(futures)) {
            pool.Async(
                future,
                [&counter, i]() {
                    return counter.FetchAdd((int)i + 1, RmwMemoryOrder::AcquireRelease) + (int)i + 1;
                },
                cleanup);
        }

        // Wait for all to complete
        for (auto& future : futures) {
            CHECK(future.WaitUntilFinished());
            CHECK(future.IsFinished());
        }

        // Results should be accumulated
        int total = 0;
        for (auto& future : futures)
            total += future.Result();

        CHECK(total > 0);
    }

    SUBCASE("rapid operations with starting gun") {
        constexpr usize k_num_operations = 1000;
        constexpr usize k_num_worker_threads = 4;

        Array<Thread, k_num_worker_threads> worker_threads;
        Array<Future<int>, k_num_operations> futures;
        StartingGun starting_gun;
        Atomic<usize> threads_ready {0};
        Atomic<usize> total_completed {0};
        Atomic<usize> total_cancelled {0};

        // Start worker threads that wait for the starting gun
        for (auto [i, thread] : Enumerate(worker_threads)) {
            thread.Start(
                [&starting_gun, &threads_ready]() {
                    threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                    starting_gun.WaitUntilFired();
                    // Thread is now ready to process async jobs
                },
                fmt::FormatInline<k_max_thread_name_size>("rapid-{}", i));
        }

        // Wait for all threads to be ready
        while (threads_ready.Load(LoadMemoryOrder::Acquire) < k_num_worker_threads)
            YieldThisThread();

        // Fire starting gun and rapidly create many async operations
        starting_gun.Fire();

        for (auto [i, future] : Enumerate(futures)) {
            pool.Async(
                future,
                [i, &total_completed]() {
                    // Simulate some work with random duration
                    auto seed = RandomSeed();
                    auto work_cycles = RandomIntInRange(seed, 1, 100);
                    for (auto _ : Range(work_cycles))
                        SpinLoopPause();

                    total_completed.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                    return (int)i;
                },
                []() {} // empty cleanup
            );
        }

        // Rapidly check status of all futures, randomly cancel some
        auto seed = RandomSeed();
        usize checks_completed = 0;

        while (checks_completed < futures.size) {
            checks_completed = 0;

            for (auto [i, future] : Enumerate(futures)) {
                if (future.IsFinished()) {
                    ++checks_completed;
                    continue;
                }

                if (future.IsInactive()) {
                    ++checks_completed;
                    continue;
                }

                // Randomly cancel some in-progress futures (stress test cancellation)
                if (RandomIntInRange(seed, 0, 1000) < 5) { // ~0.5% chance
                    if (future.Cancel()) total_cancelled.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                }
            }

            // Brief yield to allow worker threads to make progress
            YieldThisThread();
        }

        // Collect all results and verify consistency
        usize results_collected = 0;
        for (auto [i, future] : Enumerate(futures)) {
            if (auto result = future.TryReleaseResult()) {
                CHECK_EQ(*result, (int)i);
                ++results_collected;
            } else {
                // Future was cancelled or never completed
                CHECK(future.IsInactive());
            }
        }

        // Join worker threads
        for (auto& thread : worker_threads)
            if (thread.Joinable()) thread.Join();

        auto completed = total_completed.Load(LoadMemoryOrder::Acquire);
        auto cancelled = total_cancelled.Load(LoadMemoryOrder::Acquire);

        tester.log.Debug("Rapid operations: {} completed, {} cancelled, {} results collected",
                         completed,
                         cancelled,
                         results_collected);

        // Verify that completed operations equals results collected
        CHECK_EQ(completed, results_collected);

        // Verify that we don't have more results than operations
        CHECK(results_collected <= k_num_operations);
        CHECK(completed <= k_num_operations);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterThreadPoolTests) { REGISTER_TEST(TestAsync); }
