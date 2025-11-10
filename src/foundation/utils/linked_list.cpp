// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

TEST_CASE(TestLinkedList) {
    LeakDetectingAllocator a;

    struct Node {
        int val;
        Node* next;
    };

    IntrusiveSinglyLinkedList<Node> list {};

    auto prepend = [&](int v) {
        auto new_node = a.New<Node>();
        new_node->val = v;
        SinglyLinkedListPrepend(list.first, new_node);
    };

    CHECK(list.Empty());

    prepend(1);
    prepend(2);

    CHECK(!list.Empty());

    usize count = 0;
    for (auto it : list) {
        if (count == 0) CHECK(it.val == 2);
        if (count == 1) CHECK(it.val == 1);
        ++count;
    }
    CHECK(count == 2);

    auto remove_if = [&](auto pred) {
        SinglyLinkedListRemoveIf(
            list.first,
            [&](Node const& node) { return pred(node.val); },
            [&](Node* node) { a.Delete(node); });
    };

    remove_if([](int) { return true; });
    CHECK(list.Empty());

    prepend(1);
    prepend(2);
    prepend(3);
    prepend(2);

    auto count_list = [&]() {
        usize count = 0;
        for ([[maybe_unused]] auto i : list)
            ++count;
        return count;
    };

    CHECK(count_list() == 4);

    remove_if([](int i) { return i == 1; });
    CHECK(count_list() == 3);
    for (auto i : list)
        CHECK(i.val != 1);

    remove_if([](int i) { return i == 2; });
    CHECK(count_list() == 1);
    CHECK(list.first->val == 3);

    remove_if([](int i) { return i == 3; });
    CHECK(count_list() == 0);
    CHECK(list.first == nullptr);

    prepend(3);
    prepend(2);
    prepend(2);
    prepend(1);
    CHECK(count_list() == 4);

    // remove first
    remove_if([](int i) { return i == 1; });
    CHECK(count_list() == 3);
    CHECK(list.first->val == 2);
    CHECK(list.first->next->val == 2);
    CHECK(list.first->next->next->val == 3);
    CHECK(list.first->next->next->next == nullptr);

    // remove last
    remove_if([](int i) { return i == 3; });
    CHECK(count_list() == 2);
    CHECK(list.first->val == 2);
    CHECK(list.first->next->val == 2);
    CHECK(list.first->next->next == nullptr);

    remove_if([](int i) { return i == 2; });
    CHECK(count_list() == 0);

    return k_success;
}

TEST_REGISTRATION(RegisterLinkedListTests) { REGISTER_TEST(TestLinkedList); }
