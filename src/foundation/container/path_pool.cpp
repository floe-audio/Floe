// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestPathPool) {
    auto& a = tester.scratch_arena;

    PathPool pool;

    SUBCASE("all allocations are freed") {
        DynamicArrayBounded<String, 10> paths;
        dyn::Append(paths, pool.Clone("abcde", a));
        dyn::Append(paths, pool.Clone("a", a));
        dyn::Append(paths, pool.Clone("b", a));
        dyn::Append(paths, pool.Clone("c", a));
        dyn::Append(paths, pool.Clone("abc", a));
        dyn::Append(paths, pool.Clone("ab", a));
        dyn::Append(paths, pool.Clone("a", a));

        for (auto p : paths)
            pool.Free(p);

        CHECK(pool.used_list == nullptr);
        CHECK(pool.free_list != nullptr);
    }

    SUBCASE("very long string") {
        auto const long_string = a.AllocateExactSizeUninitialised<char>(1000);
        for (auto& c : long_string)
            c = 'a';
        auto const p = pool.Clone((String)long_string, a);
        CHECK_EQ(p, long_string);

        pool.Free(p);
    }
    return k_success;
}

TEST_CASE(TestPathPoolFreeListReuseRefCounting) {
    ArenaAllocator arena {PageAllocator::Instance()};
    PathPool pool;

    auto const s1 = pool.Clone("hello_world"_s, arena);
    pool.Free(s1);

    // Reuse from free_list, then clone again via StartsWithSpan match.
    auto const s2 = pool.Clone("hello_world"_s, arena);
    auto const s3 = pool.Clone("hello_world"_s, arena);
    pool.Free(s3);

    // s2 should still be valid; the entry must not have been moved back to the free_list.
    pool.Clone("goodbye_wor"_s, arena);
    CHECK_EQ(s2, "hello_world"_s);

    return k_success;
}

TEST_REGISTRATION(RegisterPathPoolTests) {
    REGISTER_TEST(TestPathPool);
    REGISTER_TEST(TestPathPoolFreeListReuseRefCounting);
}
