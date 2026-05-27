// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

// A quick and simple growable array that uses the 'global allocator' (malloc).
// Ideally this would somehow be just part of DynamicArray, but currently DynamicArray does not depend any
// 'malloc' includes, and it requires using the Allocator interface.
//
// Based on code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

// Elements stored in BasicDynamicArray must be safe to memcpy/realloc because the array uses
// GlobalReallocOversizeAllowed for growth. Types that are TriviallyCopyable satisfy this automatically.
// Non-trivially-copyable types (such as BasicDynamicArray itself) can opt in by defining a MemcpySafe tag
// type.
template <typename T>
concept MemcpySafe = TriviallyCopyable<T> || requires { typename T::MemcpySafe; };

template <MemcpySafe T>
struct BasicDynamicArray {
    using MemcpySafe = void;

    using ValueType = T;
    using Iterator = ValueType*;
    using ConstIterator = ValueType const*;

    DEFINE_SPAN_INTERFACE_METHODS(BasicDynamicArray, data, size, )
    DEFINE_SPAN_INTERFACE_METHODS(BasicDynamicArray, data, size, const)

    NON_COPYABLE(BasicDynamicArray);

    BasicDynamicArray() {
        size = capacity = 0;
        data = nullptr;
    }
    ~BasicDynamicArray() {
        if (data) GlobalFreeNoSize(data);
    }

    bool Empty() const { return size == 0; }
    int Size() const { return size; }
    int Capacity() const { return capacity; }

    ALWAYS_INLINE ValueType& operator[](u32 i) {
        ASSERT_HOT(i < size);
        return data[i];
    }
    ALWAYS_INLINE ValueType const& operator[](u32 i) const {
        ASSERT_HOT(i < size);
        return data[i];
    }

    void Clear() {
        if (data) {
            size = capacity = 0;
            GlobalFreeNoSize(data);
            data = nullptr;
        }
    }
    Iterator begin() { return data; }
    ConstIterator begin() const { return data; }
    Iterator end() { return data + size; }
    ConstIterator end() const { return data + size; }
    ValueType& Front() {
        ASSERT(size > 0);
        return data[0];
    }
    ValueType const& Front() const {
        ASSERT(size > 0);
        return data[0];
    }
    ValueType& Back() {
        ASSERT(size > 0);
        return data[size - 1];
    }
    ValueType const& Back() const {
        ASSERT(size > 0);
        return data[size - 1];
    }

    u32 GrowCapacity(u32 new_size) const {
        auto const new_capacity = capacity ? (capacity + (capacity / 2)) : 8;
        return new_capacity > new_size ? new_capacity : new_size;
    }

    void Resize(u32 new_size) {
        if (new_size > capacity) Reserve(GrowCapacity(new_size));
        size = new_size;
    }
    void Reserve(u32 new_capacity) {
        if (new_capacity <= capacity) return;
        auto mem = GlobalReallocOversizeAllowed({(void*)data, capacity * sizeof(ValueType)},
                                                {.size = new_capacity * sizeof(ValueType)});
        data = (ValueType*)mem.data;
        capacity = (u32)(mem.size / sizeof(ValueType));
    }

    void PushBack(ValueType const& v) {
        if (size == capacity) Reserve(GrowCapacity(size + 1));
        data[size++] = v;
    }
    void PopBack() {
        ASSERT(size > 0);
        size--;
    }

    u32 size;
    u32 capacity;
    T* data;
};
