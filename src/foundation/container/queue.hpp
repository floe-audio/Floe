// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/array.hpp"
#include "foundation/container/bounded_list.hpp"
#include "foundation/memory/allocators.hpp"
#include "foundation/utils/dummy_mutex.hpp"
#include "foundation/utils/linked_list.hpp"

#include "optional.hpp"

template <typename Type, typename MutexType = DummyMutex>
struct Queue {
    struct Node {
        Node* prev {};
        Node* next {};
        Type data {};
    };

    template <typename... Args>
    void Push(Args&&... args) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        auto node = arena.NewUninitialised<Node>();
        PLACEMENT_NEW(&node->data) Type(Forward<Args>(args)...);

        DoublyLinkedListAppend(*this, node);
    }

    Optional<Type> TryPop() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        if (!first) return k_nullopt;
        auto result = Move(first->data);
        first->data.~Type();

        DoublyLinkedListRemoveFirst(*this);
        if (!first) arena.ResetCursorAndConsolidateRegions();

        return Move(result);
    }

    bool Empty() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        return first == nullptr;
    }

    ArenaAllocator arena;
    Node* first {};
    Node* last {};
    [[no_unique_address]] MutexType mutex {};
};

template <typename Type, usize k_size, typename MutexType = DummyMutex>
struct BoundedQueue {
    template <typename... Args>
    bool TryPush(Args&&... args) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        auto node = list.AppendUninitialised();
        if (!node) return false;
        PLACEMENT_NEW(node) Type(Forward<Args>(args)...);

        return true;
    }

    Optional<Type> TryPop() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        if (list.Empty()) return k_nullopt;

        auto result = Move(list.First());
        list.RemoveFirst();
        return Move(result);
    }

    // Only useful as a hint, not a guarantee
    bool Full() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        return list.Full();
    }

    BoundedList<Type, k_size> list {};
    [[no_unique_address]] MutexType mutex {};
};
