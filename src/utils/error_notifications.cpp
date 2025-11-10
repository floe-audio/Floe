// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_notifications.hpp"

#include "tests/framework.hpp"
#include "utils/thread_extra/starting_gun.hpp"

TEST_CASE(TestErrorNotifications) {
    ThreadsafeErrorNotifications n;
    u64 const id1 = 54301239845687;
    u64 const id2 = 61398210056122;

    SUBCASE("basic operations") {
        // Add an item.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Error"_s;
        }

        // Check we can read it.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                CHECK_EQ(item.title, "Error"_s);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 1uz);
        }

        // Remove it.
        CHECK(n.RemoveError(id1));

        // Removing a non-existing item does't work.
        CHECK(!n.RemoveError(100));

        // Check it is gone.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const&) {
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 0uz);
        }
    }

    SUBCASE("update error") {
        // Add an item.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Error"_s;
        }

        // Update it.
        {
            auto item = n.BeginWriteError(id1);
            REQUIRE(item);
            DEFER { n.EndWriteError(*item); };
            item->title = "Updated Error"_s;
        }

        // Check we can read it.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                CHECK_EQ(item.title, "Updated Error"_s);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 1uz);
        }
    }

    SUBCASE("remove an error while it's in begin/end section") {
        // Begin.
        auto item = n.BeginWriteError(id1);
        REQUIRE(item);
        item->title = "Error"_s;

        // Remove it.
        CHECK(n.RemoveError(id1));

        // End.
        n.EndWriteError(*item);

        // This is allowed behaviour. It should be empty now.
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const&) {
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 0uz);
        }
    }

    SUBCASE("multiple begin/end sections simultaneously") {
        auto item1 = n.BeginWriteError(id1);
        REQUIRE(item1);
        item1->title = "Error 1"_s;

        auto item2 = n.BeginWriteError(id2);
        REQUIRE(item2);
        item2->title = "Error 2"_s;

        n.EndWriteError(*item1);
        n.EndWriteError(*item2);

        // Check both are present
        {
            usize count = 0;
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                auto const id = item.id.Load(LoadMemoryOrder::Acquire);
                if (id == id1)
                    CHECK_EQ(item.title, "Error 1"_s);
                else if (id == id2)
                    CHECK_EQ(item.title, "Error 2"_s);
                else
                    TEST_FAILED("Unexpected item ID: {}", id);
                ++count;
                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            CHECK_EQ(count, 2uz);
        }
    }

    SUBCASE("multiple threads") {
        Atomic<u32> iterations {0};
        constexpr u32 k_num_iterations = 10000;
        Array<Thread, 4> producers;
        Atomic<bool> thread_ready {false};
        StartingGun starting_gun;
        Atomic<u64> next_id {1};

        for (auto& p : producers) {
            p.Start(
                [&]() {
                    auto seed = RandomSeed();

                    thread_ready.Store(true, StoreMemoryOrder::Release);
                    starting_gun.WaitUntilFired();

                    while (iterations.Load(LoadMemoryOrder::Acquire) < k_num_iterations) {
                        auto const id = next_id.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                        if (RandomIntInRange<u32>(seed, 0, 5) == 0) {
                            n.RemoveError(Max(id - 2, (u64)1));
                        } else if (auto item = n.BeginWriteError(id)) {
                            DEFER { n.EndWriteError(*item); };
                            item->title = "title"_s;

                            // Simulate an amount of work
                            auto const volatile work_size = RandomIntInRange<usize>(seed, 0, 500);
                            u32 volatile work = 0;
                            for (; work < work_size; work += 1)
                                (void)work;

                            item->message = "message"_s;
                            item->error_code = {};
                        }

                        iterations.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
                        YieldThisThread();
                    }
                },
                "producer");
        }

        auto seed = RandomSeed();

        while (!thread_ready.Load(LoadMemoryOrder::Acquire))
            YieldThisThread();

        starting_gun.Fire();
        while (iterations.Load(LoadMemoryOrder::Acquire) < k_num_iterations) {
            n.ForEach([&](ThreadsafeErrorNotifications::Item const& item) {
                // Let's occasionally remove an item.
                if (RandomIntInRange<u32>(seed, 0, 3) == 0)
                    return ThreadsafeErrorNotifications::ItemIterationResult::Remove;

                CHECK_EQ(item.title, "title"_s);

                // Simulate an amount of work
                auto const volatile work_size = RandomIntInRange<usize>(seed, 0, 500);
                u32 volatile work = 0;
                for (; work < work_size; work += 1)
                    (void)work;

                CHECK_EQ(item.message, "message"_s);

                return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
            });
            YieldThisThread();
        }

        for (auto& p : producers)
            p.Join();
    }

    return k_success;
}

TEST_REGISTRATION(RegisterErrorNotificationsTests) { REGISTER_TEST(TestErrorNotifications); }
