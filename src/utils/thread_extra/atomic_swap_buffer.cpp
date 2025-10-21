// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_swap_buffer.hpp"

#include "tests/framework.hpp"
#include "utils/thread_extra/starting_gun.hpp"

TEST_CASE(TestAtomicSwapBuffer) {
    AtomicSwapBuffer<int, true> buffer;

    Thread producer;
    Thread consumer;
    StartingGun starting_gun;
    Atomic<u32> threads_ready {0};
    producer.Start(
        [&]() {
            threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            starting_gun.WaitUntilFired();
            for (auto const value : Range<int>(10000)) {
                auto& data = buffer.Write();
                data = value;
                buffer.Publish();
            }
        },
        "producer");
    consumer.Start(
        [&]() {
            threads_ready.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
            starting_gun.WaitUntilFired();
            for (auto const _ : Range<int>(10000))
                buffer.Consume();
        },
        "consumer");

    while (threads_ready.Load(LoadMemoryOrder::Relaxed) != 2)
        YieldThisThread();

    starting_gun.Fire();
    producer.Join();
    consumer.Join();

    return k_success;
}

TEST_REGISTRATION(RegisterAtomicSwapBufferTests) { REGISTER_TEST(TestAtomicSwapBuffer); }
