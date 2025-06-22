// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

// Goals of this class:
// - Storage for errors that are designed to be displayed to the user.
// - Reader thread (UI thread) can iterate over them with minimal locking.
// - Can be passed to any system in the codebase where they can use it on background threads.
// - Errors have IDs allowing for systems to remove errors if they are no longer relevant.
// - Filling in an error is not done under a lock. Writers can take their time to construct comprehensive
//   error information.
// - Errors can be updated - also not under a lock.
//
// Example:
//
// Writer:
//   auto const my_error_id = HashMultiple(Array {"my-system", filepath});
//
//   if (success) {
//       error_notifications.RemoveError(my_error_id);
//   } else if (auto err = error_notifications.BeginWriteError(id)) {
//       fmt::Assign(err->title, "{}", blah);
//       //...
//   }
//
// Reader:
//   error_notifications.ForEach([&](ThreadsafeErrorNotifications2::Item const& item) {
//      // .. display item on GUI
//   })

struct ThreadsafeErrorNotifications {
    static constexpr u64 k_empty_id = 0;

    // We use the top bit of the ID to signal that the item is being modified.
    static constexpr u64 k_being_modified_bit = 1ull << 63;

    struct Item {
        Atomic<u64> id; // private
        DynamicArrayBounded<char, 64> title;
        DynamicArrayBounded<char, 512> message;
        Optional<ErrorCode> error_code;
    };

    static u64 ClearSpecialBits(u64 id) {
        // IDs mustn't have the special bits.
        id &= ~k_being_modified_bit;
        return id;
    }

    // Writer thread.
    // Finds or creates an item with the given id.
    // Must NOT be called with the same id whilst already 'begun'. If it doesn't return null you must call
    // EndWriteError.
    Item* BeginWriteError(u64 id) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        ASSERT(id);
        id = ClearSpecialBits(id);
        auto const id_with_modified_bit = id | k_being_modified_bit;

        // Check for existing.
        for (auto& item : items) {
            auto const item_id = item.id.Load(LoadMemoryOrder::Acquire);
            if (item_id == k_empty_id) continue;

            u64 expected = id;
            if (item.id.CompareExchangeStrong(expected,
                                              id_with_modified_bit,
                                              RmwMemoryOrder::AcquireRelease,
                                              LoadMemoryOrder::Acquire)) {
                return &item;
            }

            if (ClearSpecialBits(expected) == id)
                ASSERT(!(expected & k_being_modified_bit)); // 'begin' already called on this item.
        }

        // Claim new slot.
        for (auto& item : items) {
            u64 expected = k_empty_id;
            if (item.id.CompareExchangeStrong(expected,
                                              id_with_modified_bit,
                                              RmwMemoryOrder::AcquireRelease,
                                              LoadMemoryOrder::Acquire)) {
                dyn::Clear(item.title);
                dyn::Clear(item.message);
                item.error_code = k_nullopt;
                return &item;
            }
        }

        // No free slots.
        return nullptr;
    }

    // Writer thread.
    // Commits the error item.
    static void EndWriteError(Item& item) {
        // Clear the being modified bit.
        item.id.FetchAnd(~k_being_modified_bit, RmwMemoryOrder::AcquireRelease);
    }

    // Writer thread.
    bool RemoveError(u64 id) {
        ASSERT(id);
        id = ClearSpecialBits(id);

        for (auto& item : items) {
            usize count = 0;
            u64 item_id = item.id.Load(LoadMemoryOrder::Acquire);
            while (ClearSpecialBits(item_id) == id) {
                // Clear the id, but keep the being-modified bit if it was set.
                if (item.id.CompareExchangeWeak(item_id,
                                                k_empty_id | (item_id & k_being_modified_bit),
                                                RmwMemoryOrder::AcquireRelease,
                                                LoadMemoryOrder::Acquire)) {
                    return true;
                }
                ++count;
                ASSERT(count < 10000);
            }
        }

        return false;
    }

    static bool ItemReadable(u64 id) {
        if (id == k_empty_id) return false;
        if (id & k_being_modified_bit) return false;
        return true;
    }

    enum class ItemIterationResult {
        Continue,
        Stop,
        Remove,
    };

    // Reader thread.
    void ForEach(FunctionRef<ItemIterationResult(Item const&)> func) {
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        for (auto& item : items) {
            if (!ItemReadable(item.id.Load(LoadMemoryOrder::Acquire))) continue;
            auto result = func(item);
            if (result == ItemIterationResult::Stop) break;
            if (result == ItemIterationResult::Remove) {
                // Clear the id, but keep the being-modified bit if it was set.
                item.id.FetchAnd(k_being_modified_bit, RmwMemoryOrder::AcquireRelease);
            }
        }
    }

    bool HasErrors() {
        mutex.Lock();
        DEFER { mutex.Unlock(); };
        for (auto& item : items) {
            if (!ItemReadable(item.id.Load(LoadMemoryOrder::Acquire))) continue;
            return true;
        }
        return false;
    }

    Mutex mutex {};
    Array<Item, 20> items {};
};
