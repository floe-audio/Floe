// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_ref_list.hpp"

#include "tests/framework.hpp"
#include "utils/thread_extra/starting_gun.hpp"

struct MallocedObj {
    MallocedObj(char c) : obj((char*)GlobalAlloc({.size = 10}).data) { FillMemory({(u8*)obj, 10}, (u8)c); }
    ~MallocedObj() {
        GlobalFreeNoSize(obj);
        obj = nullptr;
    }
    bool operator==(char c) const { return obj[0] == c; }
    char* obj;
};

TEST_CASE(TestAtomicRefList) {
    AtomicRefList<MallocedObj> map;

    SUBCASE("basics") {
        // Initially empty
        {
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
        }

        // Allocate and insert
        {
            auto node = map.AllocateUninitialised();
            REQUIRE(node);
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
            PLACEMENT_NEW(&node->value) MallocedObj('a');
            map.Insert(node);
            CHECK(map.dead_list == nullptr);
            CHECK(map.free_list == nullptr);
            CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == node);
        }

        // Retained iterator
        {
            auto it = map.begin();
            CHECK(it->TryRetain());
            CHECK(it.node);
            it->Release();

            ++it;
            REQUIRE(!it.node);
        }

        // Remove
        {
            auto it = map.begin();
            REQUIRE(it.node);
            map.Remove(it);
            CHECK(map.begin().node == nullptr);
            CHECK(map.dead_list);
            CHECK(!map.free_list);
        }

        // Delete unreferenced
        {
            map.DeleteRemovedAndUnreferenced();
            CHECK(map.free_list);
            CHECK(!map.dead_list);
        }

        // Check multiple objects
        {
            auto const keys = Array {'a', 'b', 'c', 'd', 'e', 'f'};
            auto const count = [&]() {
                usize count = 0;
                for (auto& i : map) {
                    auto val = i.TryRetain();
                    REQUIRE(val);
                    DEFER { i.Release(); };
                    ++count;
                }
                return count;
            };

            // Insert and iterate
            {
                for (auto c : keys) {
                    auto n = map.AllocateUninitialised();
                    PLACEMENT_NEW(&n->value) MallocedObj(c);
                    map.Insert(n);
                }

                auto it = map.begin();
                REQUIRE(it.node);
                CHECK(Find(keys, it->value.obj[0]));
                usize num = 0;
                while (it != map.end()) {
                    ++num;
                    ++it;
                }
                CHECK_EQ(num, keys.size);
            }

            // Remove first and writer-iterate
            {
                auto writer_it = map.begin();
                map.Remove(writer_it);

                CHECK_EQ(count(), keys.size - 1);
            }

            // Remove while in a loop
            {
                usize pos = 0;
                for (auto it = map.begin(); it != map.end();) {
                    if (pos == 2)
                        it = map.Remove(it);
                    else
                        ++it;
                    ++pos;
                }
                CHECK_EQ(count(), keys.size - 2);
            }

            // Remove unref
            {
                map.DeleteRemovedAndUnreferenced();
                CHECK_EQ(count(), keys.size - 2);
                CHECK(map.free_list != nullptr);
            }

            // Remove all
            {
                map.RemoveAll();
                map.DeleteRemovedAndUnreferenced();
                CHECK(map.live_list.Load(LoadMemoryOrder::Relaxed) == nullptr);
                CHECK(map.dead_list == nullptr);
            }
        }
    }

    SUBCASE("multithreading") {
        Thread writer_thread;
        Atomic<bool> done {false};

        StartingGun starting_gun;
        Atomic<bool> thread_ready {false};

        usize inserted = 0;
        usize removed = 0;
        usize garbage_collections = 0;

        writer_thread.Start(
            [&]() {
                thread_ready.Store(true, StoreMemoryOrder::Relaxed);
                starting_gun.WaitUntilFired();
                auto seed = RandomSeed();
                for (auto _ : Range(500000)) {
                    for (char c = 'a'; c <= 'z'; ++c) {
                        auto const r = RandomIntInRange(seed, 0, 2);
                        if (r == 0) {
                            for (auto it = map.begin(); it != map.end();)
                                if (it->value == c) {
                                    it = map.Remove(it);
                                    ++removed;
                                    break;
                                } else {
                                    ++it;
                                }
                        } else if (r == 1) {
                            bool found = false;
                            for (auto& it : map) {
                                if (it.value == c) {
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                auto node = map.AllocateUninitialised();
                                PLACEMENT_NEW(&node->value) MallocedObj(c);
                                map.Insert(node);
                                ++inserted;
                            }
                        } else if (r == 2) {
                            map.DeleteRemovedAndUnreferenced();
                            ++garbage_collections;
                        }
                    }
                    YieldThisThread();
                }
                done.Store(true, StoreMemoryOrder::Release);
            },
            "test-thread");

        while (!thread_ready.Load(LoadMemoryOrder::Relaxed))
            YieldThisThread();

        starting_gun.Fire();
        while (!done.Load(LoadMemoryOrder::Relaxed)) {
            for (auto& i : map)
                if (auto val = i.TryRetain()) {
                    CHECK(val->obj[0] >= 'a' && val->obj[0] <= 'z');
                    i.Release();
                }
            YieldThisThread();
        }

        writer_thread.Join();

        tester.log.Debug("Inserted: {}, removed: {}, garbage collections: {}",
                         inserted,
                         removed,
                         garbage_collections);

        for (auto n = map.live_list.Load(LoadMemoryOrder::Relaxed); n != nullptr;
             n = n->next.Load(LoadMemoryOrder::Relaxed))
            CHECK_EQ(n->reader_uses.Load(LoadMemoryOrder::Relaxed), 0u);

        map.RemoveAll();
        map.DeleteRemovedAndUnreferenced();
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAtomicRefListTests) { REGISTER_TEST(TestAtomicRefList); }
