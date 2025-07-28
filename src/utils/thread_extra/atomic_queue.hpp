// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <usize val>
constexpr bool k_power_of_two = IsPowerOfTwo(val);

// An atomic lock-free fixed-size ring buffer.
//
// The size must be a power of 2. A consumer is a thread that calls Pop and a producer is a thread that calls
// Push.
//
// Some tricks used here:
// - Instead of doing a modulo to clamp indexes to the size, we use the bitwise AND operator and a mask of
//   size - 1. This is a cheaper operation and is a nice property of having a power-of-2 size.
// - The head/tail indexes are not clamped to the size of the buffer, instead they just keep increasing in
//   size. This allows us to distinguish between full and empty without wasting a slot. This works because
//   of the power-of-2 requirement and properties of unsigned integer overflow. See the snellman.net link.
//
// https://doc.dpdk.org/guides-19.05/prog_guide/ring_lib.html
// https://svnweb.freebsd.org/base/release/12.2.0/sys/sys/buf_ring.h?revision=367086&view=markup
// https://github.com/eldipa/loki
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/

template <TriviallyCopyable Type, usize k_size>
requires(k_power_of_two<k_size>)
struct AtomicQueue {
    static constexpr u32 k_mask = k_size - 1;

    bool Push(Type const& item) { return Push({&item, 1}); }

    bool Pop(Type& item) {
        if (Pop({&item, 1})) return true;
        return false;
    }

    DynamicArrayBounded<Type, k_size> PopAll() {
        DynamicArrayBounded<Type, k_size> result;
        result.ResizeWithoutCtorDtor(k_size);
        auto num = Pop(result.Items());
        result.ResizeWithoutCtorDtor(num);
        return result;
    }

    bool Push(Span<Type const> data) {
        // Step 1: copy into local variables and check for size
        auto const initial_producer_head = producer.head;
        auto const consumer_tail = consumer.tail.Load(LoadMemoryOrder::Acquire);

        // Step 2: check for entries and return if we can't do the push
        auto const entries_to_add = (u32)data.size;
        auto const free_entries = k_size - (initial_producer_head - consumer_tail);
        ASSERT(free_entries <= k_size);
        if (free_entries < entries_to_add) [[unlikely]]
            return false;

        // Step 3: calculate the new producer head
        auto new_producer_head = initial_producer_head + entries_to_add;
        producer.head = new_producer_head;

        // Step 4: perform the copy
        for (auto const i : Range(entries_to_add)) {
            auto const ring_index = (initial_producer_head + i) & k_mask;
            m_data[ring_index] = data[i];
        }

        // Step 5: we've done the copy, we can now move the tail so that any consumer can access the objects
        // we've added
        producer.tail.Store(new_producer_head, StoreMemoryOrder::Release);
        return true;
    }

    // Returns the number of elements that were actually popped
    u32 Pop(Span<Type> out_buffer) {
        // Step 1: copy into local variables
        auto const initial_consumer_head = consumer.head;
        auto const producer_tail = producer.tail.Load(LoadMemoryOrder::Acquire);

        // Step 2: check for entries and ensure we only pop as many as are ready
        auto const ready_entries = producer_tail - initial_consumer_head;
        if (!ready_entries) return 0;
        auto entries_to_remove = (u32)out_buffer.size;
        if (ready_entries < entries_to_remove) entries_to_remove = ready_entries;

        // Step 3: calculate the new consumer head
        auto new_consumer_head = initial_consumer_head + entries_to_remove;
        consumer.head = new_consumer_head;

        // Step 4: perform the copy
        for (auto const i : Range(entries_to_remove)) {
            auto const ring_index = (initial_consumer_head + i) & k_mask;
            out_buffer[i] = m_data[ring_index];
        }

        // Step 5: we've done the copy, we can now move the tail so that any producer can use the slots again
        consumer.tail.Store(new_consumer_head, StoreMemoryOrder::Release);
        return entries_to_remove;
    }

    struct alignas(k_destructive_interference_size) {
        u32 volatile head {0};
        Atomic<u32> tail {0};
    } producer;

    struct alignas(k_destructive_interference_size) {
        u32 volatile head {0};
        Atomic<u32> tail {0};
    } consumer;

    UninitialisedArray<Type, k_size> m_data {};
};
