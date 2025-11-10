// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestBoundedList) {
    static_assert(Same<BoundedList<int, 255>::UnderlyingIndexType, u8>);
    static_assert(Same<BoundedList<int, 256>::UnderlyingIndexType, u16>);
    static_assert(Same<BoundedList<int, 65535>::UnderlyingIndexType, u32>);

    // used a malloced int to test that the bounded list correctly frees memory
    struct MallocedInt {
        NON_COPYABLE_AND_MOVEABLE(MallocedInt);
        MallocedInt(int i) {
            data = (int*)GlobalAlloc({.size = sizeof(int)}).data;
            *data = i;
        }
        ~MallocedInt() { GlobalFreeNoSize(data); }
        bool operator==(int i) const { return *data == i; }
        int* data;
    };

    using List = BoundedList<MallocedInt, 3>;
    List list {};
    static_assert(Same<List::UnderlyingIndexType, u8>);
    CHECK(list.first == List::k_invalid_index);
    CHECK(list.last == List::k_invalid_index);
    CHECK(ToInt(list.free_list) == 0);

    {
        usize num_free = 0;
        for (auto n = list.free_list; n != List::k_invalid_index; n = list.NodeAt(n)->next)
            num_free++;
        CHECK(num_free == 3);
    }

    {
        auto val = list.AppendUninitialised();
        ASSERT(val);
        PLACEMENT_NEW(val) MallocedInt(1);
        CHECK(!list.Empty());
        CHECK(!list.Full());
        CHECK(list.First() == 1);
        CHECK(list.last == list.first);
        CHECK(!list.ContainsMoreThanOne());

        {
            usize num_free = 0;
            for (auto n = list.free_list; n != List::k_invalid_index; n = list.NodeAt(n)->next)
                num_free++;
            CHECK(num_free == 2);
        }

        for (auto& i : list)
            CHECK(i == 1);

        list.Remove(val);

        CHECK(list.first == List::k_invalid_index);
        CHECK(list.last == List::k_invalid_index);

        {
            usize num_free = 0;
            for (auto n = list.free_list; n != List::k_invalid_index; n = list.NodeAt(n)->next)
                num_free++;
            CHECK(num_free == 3);
        }
    }

    {
        auto val1 = list.AppendUninitialised();
        ASSERT(val1);
        auto val2 = list.AppendUninitialised();
        ASSERT(val2);
        auto val3 = list.AppendUninitialised();
        ASSERT(val3);
        auto val4 = list.AppendUninitialised();
        CHECK(val4 == nullptr);

        CHECK(list.free_list == List::k_invalid_index);

        PLACEMENT_NEW(val1) MallocedInt(1);
        PLACEMENT_NEW(val2) MallocedInt(2);
        PLACEMENT_NEW(val3) MallocedInt(3);

        for (auto [index, i] : Enumerate<int>(list))
            CHECK(i == index + 1);

        list.Remove(val2);
        CHECK(list.First() == 1);
        CHECK(list.Last() == 3);
        CHECK(list.NodeAt(list.first)->next == list.last);
        CHECK(list.free_list != List::k_invalid_index);

        list.RemoveFirst();
        CHECK(*list.First().data == 3);

        list.RemoveFirst();
        CHECK(list.first == List::k_invalid_index);
        CHECK(list.last == List::k_invalid_index);
        CHECK(list.free_list != List::k_invalid_index);

        usize free_count = 0;
        for (auto n = list.free_list; n != List::k_invalid_index; n = list.NodeAt(n)->next)
            ++free_count;
        CHECK(free_count == 3);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterBoundedListTests) { REGISTER_TEST(TestBoundedList); }
