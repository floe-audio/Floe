// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <foundation/foundation.hpp>
#include <os/threading.hpp>

// Lock-free triple buffer with atomic swap. Single producer, single consumer. Producer thread writes and
// publishes when it's done. Consumer thread can safely read blocks of data that are published.
// Super fast and simple but uses 3x the memory.
//
// If there is likely to be lots of contention, you might want to enable k_avoid_cacheline_contention, which
// will align the buffers to avoid false sharing. However, this increases the size of this struct even more:
// it will be at least 384 bytes.
//
// https://github.com/brilliantsugar/trio
// https://github.com/HadrienG2/triple-buffer
template <TriviallyCopyable Type, bool k_avoid_cacheline_contention>
struct AtomicSwapBuffer {
    // producer thread
    Type& Write() { return buffers[back_buffer_index].data; }

    // producer thread
    void Publish() {
        // place the back buffer index in the middle buffer state along with the dirty bit
        auto const old_middle_state =
            middle_buffer_state.Exchange(back_buffer_index | k_dirty_bit, RmwMemoryOrder::AcquireRelease);

        // the old middle state is what we will use for writing next
        back_buffer_index = old_middle_state & k_dirty_mask;
    }

    // producer thread
    void WriteAndPublish(Type const& data) {
        Write() = data;
        Publish();
    }

    struct ConsumeResult {
        Type const& data;
        bool changed;
    };

    // consumer thread
    // we could return a bool to indicate if the buffer was dirty, but we don't need it for now
    ConsumeResult Consume() {
        // if it's not dirty, we don't swap anything, just return the front
        if (!(middle_buffer_state.Load(LoadMemoryOrder::Acquire) & k_dirty_bit))
            return {buffers[front_buffer_index].data, false};

        // if it's dirty, we swap the middle with the front (with no dirty bit)
        auto const prev = middle_buffer_state.Exchange(front_buffer_index, RmwMemoryOrder::AcquireRelease);
        front_buffer_index = prev & k_dirty_mask;
        return {buffers[front_buffer_index].data, true};
    }

    static constexpr u32 k_dirty_bit = 1u << 31;
    static constexpr u32 k_dirty_mask = ~k_dirty_bit;

    // C++ spec says alignas(0) is always ignored
    static constexpr usize k_alignment = k_avoid_cacheline_contention ? k_destructive_interference_size : 0;

    struct Buffer {
        alignas(k_alignment) Type data;
    };
    Buffer buffers[3];

    // producer and consumer. contains both index and dirty bit.
    alignas(k_alignment) Atomic<u32> middle_buffer_state {1};

    // producer only
    alignas(k_alignment) u32 back_buffer_index {0};
    // consumer
    alignas(k_alignment) u32 front_buffer_index {2};
};
