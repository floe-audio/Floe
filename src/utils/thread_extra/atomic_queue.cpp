// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_queue.hpp"

#include "tests/framework.hpp"
#include "utils/thread_extra/starting_gun.hpp"

template <usize k_size>
void DoAtomicQueueTest(tests::Tester& tester, String name) {
    SUBCASE(name) {
        SUBCASE("Basic operations") {
            AtomicQueue<int, k_size> q;

            REQUIRE(q.Push(Array<int, 1> {99}));

            Array<int, 1> buf;
            REQUIRE(q.Pop(buf) == 1);
            REQUIRE(buf[0] == 99);
        }

        SUBCASE("Move operations") {
            SUBCASE("int") {
                AtomicQueue<int, k_size> q;

                REQUIRE(q.Push(Array<int, 1> {99}));
                Array<int, 1> buf;
                REQUIRE(q.Pop(buf) == 1);
                REQUIRE(buf[0] == 99);
            }
        }

        SUBCASE("Push single elements until full") {
            AtomicQueue<int, k_size> q;

            constexpr int k_val = 99;
            for (auto _ : Range(k_size))
                REQUIRE(q.Push(k_val));
            REQUIRE(!q.Push(k_val));

            for (auto _ : Range(k_size)) {
                int v;
                REQUIRE(q.Pop(v));
                REQUIRE(v == k_val);
            }
        }

        SUBCASE("Push large elements") {
            AtomicQueue<usize, k_size> q;

            Array<usize, k_size / 2> items {};
            for (auto [index, i] : Enumerate(items))
                i = index;

            REQUIRE(q.Push(items));

            Array<usize, k_size / 2> out_items {};
            REQUIRE(q.Pop(out_items) == k_size / 2);

            for (auto [index, i] : Enumerate(out_items))
                REQUIRE(i == index);
        }

        SUBCASE("Push too many elements") {
            AtomicQueue<int, k_size> q;
            Array<int, k_size * 2> items {};
            REQUIRE(!q.Push(items));
        }

        SUBCASE("Pop is clamped to number of elements") {
            AtomicQueue<int, k_size> q;
            Array<int, k_size * 2> items {};
            int const val = 99;
            REQUIRE(q.Pop(items) == 0);
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Pop(items) == 1);
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Push({&val, 1}));
            REQUIRE(q.Pop(items) == 2);
        }

        auto const do_random_spamming =
            [](AtomicQueue<int, k_size>& q, StartingGun& starting_gun, bool push) {
                starting_gun.WaitUntilFired();
                Array<int, 1> small_item {};
                Array<int, 4> big_item {};
                auto seed = RandomSeed();
                for (auto _ : Range(10000)) {
                    if (RandomIntInRange<int>(seed, 0, 1) == 0)
                        if (push)
                            q.Push(small_item);
                        else
                            q.Pop(small_item);
                    else if (push)
                        q.Push(big_item);
                    else
                        q.Pop(big_item);
                }
            };

        SUBCASE("2 threads spamming mindlessly") {
            AtomicQueue<int, k_size> q;
            Thread producer;
            Thread consumer;
            StartingGun starting_gun;
            producer.Start([&]() { do_random_spamming(q, starting_gun, true); }, "producer");
            consumer.Start([&]() { do_random_spamming(q, starting_gun, false); }, "consumer");
            starting_gun.Fire();
            producer.Join();
            consumer.Join();
        }

        SUBCASE("2 threads: all push/pops are accounted for and in order") {
            constexpr int k_num_values = 10000;
            AtomicQueue<int, k_size> q;

            // NOTE(Sam): Yieiding the thread is necessary here when running with Valgrind. It doesn't seem to
            // be nececssary normally though.

            Thread producer;
            StartingGun starting_gun;
            Atomic<bool> producer_ready {false};
            producer.Start(
                [&]() {
                    producer_ready.Store(true, StoreMemoryOrder::Relaxed);
                    starting_gun.WaitUntilFired();
                    for (auto const index : Range(k_num_values))
                        while (!q.Push(index))
                            YieldThisThread();
                },
                "producer");

            while (!producer_ready.Load(LoadMemoryOrder::Relaxed))
                YieldThisThread();

            tester.log.Debug("Producer ready");
            starting_gun.Fire();

            int index = 0;
            do {
                Array<int, 1> buf;
                if (auto num_popped = q.Pop(Span<int> {buf})) {
                    CHECK_EQ(num_popped, 1u);
                    CHECK_EQ(buf[0], index);
                    index++;
                } else {
                    YieldThisThread();
                }
            } while (index != k_num_values);

            producer.Join();
        }
    }
}

TEST_CASE(TestAtomicQueue) {
    DoAtomicQueueTest<64>(tester, "1");
    DoAtomicQueueTest<8>(tester, "2");
    return k_success;
}

TEST_REGISTRATION(RegisterAtomicQueueTests) { REGISTER_TEST(TestAtomicQueue); }
