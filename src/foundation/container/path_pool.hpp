// Copyright 2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/linked_list.hpp"

#include "span.hpp"

// Makes string allocations reusable within an arena. The lifetime is managed by the arena, but you can
// (optionally) Clone/Free individual strings within that lifetime to avoid lots of reallocations. It avoids
// having make everything RAII ready (std C++ style).
struct PathPool {
    struct Path {
        MutableString buffer;
        u32 buffer_refs {};
        Path* next {};
    };

    String Clone(String p, Allocator& arena, usize min_size = 64) {
        for (auto i = used_list; i != nullptr; i = i->next) {
            if (StartsWithSpan(i->buffer, p)) {
                ++i->buffer_refs;
                return {i->buffer.data, p.size};
            }
        }

        {
            Path* prev_free = nullptr;
            for (auto free = free_list; free != nullptr; free = free->next) {
                // if we find a free path that is big enough, use it
                if (free->buffer.size >= p.size) {
                    SinglyLinkedListRemove(free_list, free, prev_free);
                    SinglyLinkedListPrepend(used_list, free);

                    // copy and return
                    CopyMemory(free->buffer.data, p.data, p.size);
                    return {free->buffer.data, p.size};
                }
                prev_free = free;
            }
        }

        auto new_path = arena.NewUninitialised<Path>();
        new_path->buffer = arena.AllocateExactSizeUninitialised<char>(Max(p.size, min_size));
        new_path->buffer_refs = 1;
        CopyMemory(new_path->buffer.data, p.data, p.size);
        if (new_path->buffer.size != p.size)
            ZeroMemory(new_path->buffer.data + p.size, new_path->buffer.size - p.size);
        SinglyLinkedListPrepend(used_list, new_path);
        return {new_path->buffer.data, p.size};
    }

    // It is ok if the string is not in the pool.
    void Free(String p) {
        Path* prev = nullptr;
        for (auto i = used_list; i != nullptr; i = i->next) {
            if (i->buffer.data == p.data) {
                if (--i->buffer_refs == 0) {
                    SinglyLinkedListRemove(used_list, i, prev);
                    SinglyLinkedListPrepend(free_list, i);
                }
                return;
            }
            prev = i;
        }
    }

    Path* used_list {};
    Path* free_list {};
};
