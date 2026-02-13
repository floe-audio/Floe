#pragma once

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

// A quick and simple growable array that uses the 'global allocator' (malloc).
// Ideally this would somehow be just part of DynamicArray, but currently DynamicArray does not depend any
// 'malloc' includes, and it requires using the Allocator interface.

template <TriviallyCopyable T>
struct BasicDynamicArray {
    using ValueType = T;
    using Iterator = ValueType*;
    using ConstIterator = ValueType const*;

    DEFINE_SPAN_INTERFACE_METHODS(BasicDynamicArray, data, size, )
    DEFINE_SPAN_INTERFACE_METHODS(BasicDynamicArray, data, size, const)
    PROPAGATE_TRIVIALLY_COPYABLE(BasicDynamicArray, T);

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

    ValueType& operator[](u32 i) {
        ASSERT(i < size);
        return data[i];
    }
    ValueType const& operator[](u32 i) const {
        ASSERT(i < size);
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
        data = (ValueType*)GlobalRealloc({(void*)data, capacity * sizeof(ValueType)},
                                         {.size = new_capacity * sizeof(ValueType)})
                   .data;
        capacity = new_capacity;
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
