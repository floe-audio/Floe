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
// - Store GUI state (e.g. window positions, sizes, browser filters, etc.)

namespace persistent_store {

using Id = u64;

struct Value {
    template <Arithmetic T>
    Optional<T> TryGetValueAs() const {
        if (data.size != sizeof(T)) return k_nullopt;
        // It might not be aligned, so we need to copy it.
        T result;
        __builtin_memcpy_inline(&result, data.data, sizeof(T));
        return result;
    }

    template <Arithmetic T>
    bool Contains(T value) const {
        for (auto v = this; v; v = v->next)
            if (v->data.size == sizeof(T) && MemoryIsEqual(v->data.data, &value, sizeof(T))) return true;
        return false;
    }

    Span<u8 const> data; // Probably not aligned.
    Value* next;
};

using StoreTable = HashTable<Id, Value*>;

// Hash table API
// =================================================================================

StoreTable Read(ArenaAllocator& arena, String data);
ErrorCodeOr<void> Write(StoreTable const& store, Writer writer);

// Data is cloned.
void AddValue(StoreTable& store, ArenaAllocator& arena, Id id, Span<u8 const> data);

template <Arithmetic T>
PUBLIC void AddValue(StoreTable& store, ArenaAllocator& arena, Id id, T value) {
    AddValue(store, arena, id, Span<u8 const> {(u8 const*)&value, sizeof(T)});
}

template <Enum E>
PUBLIC void AddValue(StoreTable& store, ArenaAllocator& arena, Id id, E value) {
    UnderlyingType<E> const raw = ToInt(value);
    AddValue(store, arena, id, raw);
}

template <Arithmetic T>
PUBLIC void ReadValue(StoreTable const& store, Id id, T& out) {
    auto const value_or_null = store.Find(id);
    if (!value_or_null || !*value_or_null) return;
    if (auto const v = (*value_or_null)->TryGetValueAs<T>()) out = *v;
}

// Bounds-checked against E::Count so a stale value outside the current enum range is rejected.
template <Enum E>
PUBLIC void ReadEnum(StoreTable const& store, Id id, E& out) {
    UnderlyingType<E> raw = ToInt(out);
    ReadValue(store, id, raw);
    if (raw >= ToInt(E::Count)) return;
    out = (E)raw;
}

// If value is nullopt, it will remove all values for the given id, else it will only remove the specified
// value (identified by comparing bytes).
void RemoveValue(StoreTable& store, Id id, Optional<Span<u8 const>> value);

// Higher-level API.
// =================================================================================

struct Store : StoreTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    String const filepath;
    Atomic<u64> actual_file_last_modified_microsec {};
    TimePoint time_last_checked {}; // background thread
    u64 file_last_modified_microsec {};
    bool init = false;
    bool store_valid = false;
};

enum class GetResult : u8 {
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
template <Arithmetic T>
PUBLIC Optional<T> GetValueAs(Store& store, Id id) {
    auto const r = Get(store, id);
    if (r.tag != GetResult::Found) return k_nullopt;
    return r.Get<Value const*>()->TryGetValueAs<T>();
}

// Main-thread.
void AddValue(Store& store, Id id, Span<u8 const> data);

// Main-thread.
template <Arithmetic T>
PUBLIC void AddValue(Store& store, Id id, T value) {
    AddValue(store, id, {(u8 const*)&value, sizeof(T)});
}

// Main-thread.
void RemoveValue(Store& store, Id id, Optional<Span<u8 const>> value);

// Main-thread.
PUBLIC bool GetFlag(Store& store, Id id) { return Get(store, id).tag == GetResult::Found; }
PUBLIC void AddFlag(Store& store, Id id) { AddValue(store, id, {}); }
PUBLIC void RemoveFlag(Store& store, Id id) { RemoveValue(store, id, k_nullopt); }
PUBLIC void SetFlag(Store& store, Id id, bool state) {
    if (state)
        AddFlag(store, id);
    else
        RemoveFlag(store, id);
}

// Background thread.
void StoreActualFileModifiedTime(Store& store);

} // namespace persistent_store
