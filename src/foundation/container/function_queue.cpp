// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestFunctionQueue) {
    auto& a = tester.scratch_arena;

    FunctionQueue<> q {.arena = PageAllocator::Instance()};
    CHECK(q.Empty());

    int val = 0;

    {
        q.Push([&val]() { val = 1; });
        CHECK(!q.Empty());

        auto f = q.TryPop(a);
        REQUIRE(f.HasValue());
        (*f)();
        CHECK_EQ(val, 1);
        CHECK(q.Empty());
        CHECK(q.first == nullptr);
        CHECK(q.last == nullptr);
    }

    q.Push([&val]() { val = 2; });
    q.Push([&val]() { val = 3; });

    auto f2 = q.TryPop(a);
    auto f3 = q.TryPop(a);

    CHECK(f2);
    CHECK(f3);

    (*f2)();
    CHECK_EQ(val, 2);

    (*f3)();
    CHECK_EQ(val, 3);

    for (auto const i : Range(100))
        q.Push([i, &val] { val = i; });

    for (auto const i : Range(100)) {
        auto f = q.TryPop(a);
        CHECK(f);
        (*f)();
        CHECK_EQ(val, i);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFunctionQueueTests) { REGISTER_TEST(TestFunctionQueue); }
