// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

// Binary key-value store for persistent application data.
//
// Keys are always u64 ids, values are arbitrary byte arrays.
// The underlying file uses locks to ensure that multiple processes can read/write to the store. It makes a
// reasonable effort to stay in sync with the file on disk, but it is not guaranteed to be up-to-date at all
// times. Overwriting the file after another process has just written is possible in extreme cases - even if
// this occurs the file will not be corrupted.
//
// We can use this for things like:
// - Default preset for new instances
// - Store GUI state (e.g. window positions, sizes, picker filters, etc.)

namespace persistent_store {

using Id = u64;

struct Value {
    template <Arithmetic T>
    T ValueAs() const {
        ASSERT_EQ(data.size, sizeof(T));
        return *(T const*)data.data;
    }

    template <Arithmetic T>
    bool Contains(T value) const {
        for (auto v = this; v; v = v->next)
            if (v->data.size == sizeof(T) && MemoryIsEqual(v->data.data, &value, sizeof(T))) return true;
        return false;
    }

    Span<u8 const> data;
    Value* next;
};

using StoreTable = HashTable<Id, Value*>;

// Hash table API
// =================================================================================

StoreTable Read(ArenaAllocator& arena, String data);
ErrorCodeOr<void> Write(StoreTable const& store, Writer writer);

// Data is cloned.
void AddValue(StoreTable& store, ArenaAllocator& arena, Id id, Span<u8 const> data);

// If value is nullopt, it will remove all values for the given id, else it will only remove the specified
// value (identified by comparing bytes).
void RemoveValue(StoreTable& store, Id id, Optional<Span<u8 const>> value);

// Higher-level API.
// =================================================================================

struct Store : StoreTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    String const& filepath;
    Atomic<u64> actual_file_last_modified_microsec {};
    TimePoint time_last_checked {}; // background thread
    u64 file_last_modified_microsec {};
    bool init = false;
    bool store_valid = false;
};

enum class GetResult {
    Found,
    NotFound,
    StoreInaccessible,
};

using Result = TaggedUnion<GetResult, TypeAndTag<Value const*, GetResult::Found>>;

// These functions will automatically read/write to the file as needed.
// The data is cloned.

// Main-thread.
Result Get(Store& store, Id id);

// Main-thread.
void AddValue(Store& store, Id id, Span<u8 const> data);

// Main-thread.
template <Arithmetic T>
PUBLIC void AddValue(Store& store, Id id, T value) {
    AddValue(store, id, {(u8 const*)&value, sizeof(T)});
}

// Main-thread.
void RemoveValue(Store& store, Id id, Optional<Span<u8 const>> value);

// Background thread.
void StoreActualFileModifiedTime(Store& store);

} // namespace persistent_store
