// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestStringSearching) {
    CHECK(Contains("abc"_s, 'a'));
    CHECK(!Contains("abc"_s, 'd'));
    CHECK(!Contains(""_s, 'a'));

    CHECK(ContainsSpan("abc"_s, "a"_s));
    CHECK(ContainsSpan("abc"_s, "b"_s));
    CHECK(ContainsSpan("abc"_s, "abc"_s));
    CHECK(ContainsSpan("aaaabbb"_s, "aaaa"_s));
    CHECK(ContainsSpan("abcdefg"_s, "abc"_s));
    CHECK(ContainsSpan("abcdefg"_s, "bcd"_s));
    CHECK(ContainsSpan("abcdefg"_s, "cde"_s));
    CHECK(ContainsSpan("abcdefg"_s, "def"_s));
    CHECK(ContainsSpan("abcdefg"_s, "efg"_s));
    CHECK(!ContainsSpan("abcdefg"_s, "fgh"_s));
    CHECK(!ContainsSpan("aaabbb"_s, "aaaa"_s));
    CHECK(!ContainsSpan(""_s, ""_s));

    CHECK(FindSpan("abc"_s, "a"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abc"_s, "b"_s).ValueOr(999) == 1);
    CHECK(FindSpan("abc"_s, "c"_s).ValueOr(999) == 2);
    CHECK(FindSpan("abc"_s, "abc"_s).ValueOr(999) == 0);
    CHECK(FindSpan("aaaabbb"_s, "aaaa"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abcdefg"_s, "abc"_s).ValueOr(999) == 0);
    CHECK(FindSpan("abcdefg"_s, "bcd"_s).ValueOr(999) == 1);
    CHECK(FindSpan("abcdefg"_s, "cde"_s).ValueOr(999) == 2);
    CHECK(FindSpan("abcdefg"_s, "def"_s).ValueOr(999) == 3);
    CHECK(FindSpan("abcdefg"_s, "efg"_s).ValueOr(999) == 4);
    CHECK(!FindSpan("abcdefg"_s, "fgh"_s));
    CHECK(!FindSpan("aaabbb"_s, "aaaa"_s));
    CHECK(!FindSpan(""_s, ""_s));

    CHECK(StartsWith("aa"_s, 'a'));
    CHECK(!StartsWith("aa"_s, 'b'));
    CHECK(!StartsWith(""_s, 'b'));
    CHECK(StartsWithSpan("aaa"_s, "aa"_s));
    CHECK(!StartsWithSpan("baa"_s, "aa"_s));
    CHECK(!StartsWithSpan(""_s, "aa"_s));
    CHECK(!StartsWithSpan("aa"_s, ""_s));

    CHECK(NullTermStringStartsWith("aa", "a"));
    CHECK(!NullTermStringStartsWith("aa", "b"));
    CHECK(!NullTermStringStartsWith("", "b"));
    CHECK(NullTermStringStartsWith("", ""));
    CHECK(NullTermStringStartsWith("b", ""));

    CHECK(EndsWith("aa"_s, 'a'));
    CHECK(!EndsWith("aa"_s, 'b'));
    CHECK(EndsWithSpan("aaa"_s, "aa"_s));
    CHECK(!EndsWithSpan("aab"_s, "aa"_s));
    CHECK(!EndsWithSpan(""_s, "aa"_s));
    CHECK(!EndsWithSpan("aa"_s, ""_s));

    CHECK(ContainsOnly("aa"_s, 'a'));
    CHECK(!ContainsOnly("aab"_s, 'a'));
    CHECK(!ContainsOnly(""_s, 'a'));
    CHECK(!ContainsOnly("bb"_s, 'a'));

    CHECK(FindLast("aaa"_s, 'a').ValueOr(999) == 2);
    CHECK(FindLast("aab"_s, 'a').ValueOr(999) == 1);
    CHECK(FindLast("file/path"_s, '/').ValueOr(999) == 4);
    CHECK(FindLast("abb"_s, 'a').ValueOr(999) == 0);
    CHECK(!FindLast("aaa"_s, 'b'));
    CHECK(!FindLast(""_s, 'b'));

    CHECK(Find("aaa"_s, 'a').ValueOr(999) == 0);
    CHECK(Find("baa"_s, 'a').ValueOr(999) == 1);
    CHECK(Find("bba"_s, 'a').ValueOr(999) == 2);
    CHECK(!Find("aaa"_s, 'b'));
    CHECK(!Find(""_s, 'b'));

    CHECK(FindIf("abc"_s, [](char c) { return c == 'b'; }).ValueOr(999) == 1);
    CHECK(!FindIf("abc"_s, [](char c) { return c == 'd'; }));
    CHECK(!FindIf(""_s, [](char c) { return c == 'd'; }));

    Array<u8, 32> buffer;
    CHECK(ContainsPointer(buffer, buffer.data + 1));
    CHECK(ContainsPointer(buffer, buffer.data + 4));
    CHECK(!ContainsPointer(buffer, (u8 const*)((uintptr_t)buffer.data + 100)));
    CHECK(!ContainsPointer(buffer, (u8 const*)((uintptr_t)buffer.data - 1)));

    return k_success;
}

TEST_CASE(TestSort) {
    SUBCASE("Sort") {
        SUBCASE("normal size") {
            int array[] = {7, 4, 6};
            Sort(array);
            REQUIRE(array[0] == 4);
            REQUIRE(array[1] == 6);
            REQUIRE(array[2] == 7);
        }
        SUBCASE("empty") {
            Span<int> span;
            Sort(span);
        }
        SUBCASE("one element") {
            int v = 10;
            Span<int> span {&v, 1};
            Sort(span);
        }
    }
    return k_success;
}

TEST_CASE(TestBinarySearch) {
    SUBCASE("BinarySearch") {
        REQUIRE(!FindBinarySearch(Span<int> {}, [](auto&) { return 0; }).HasValue());

        {
            int array[] = {1, 4, 6};
            Span<int> const span {array, ArraySize(array)};
            REQUIRE(FindBinarySearch(span, [](int i) {
                        if (i == 4) return 0;
                        if (i < 4) return -1;
                        return 1;
                    }).Value() == 1);
        }

        {
            int v = 1;
            Span<int> const span {&v, 1};
            REQUIRE(FindBinarySearch(span, [](int i) {
                        if (i == 1) return 0;
                        if (i < 1) return -1;
                        return 1;
                    }).Value() == 0);
        }
    }

    SUBCASE("BinarySearchForSlotToInsert") {
        Array<int, 5> arr = {0, 2, 4, 6, 8};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        auto const r5 = BinarySearchForSlotToInsert(span, [](int i) { return i - 9000; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
        REQUIRE(r5 == 5);

        span = {};
        auto const empty = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        REQUIRE(empty == 0);
    }

    SUBCASE("BinarySearchForSlotToInsert 2") {
        Array<int, 4> arr = {0, 2, 4, 6};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
    }

    SUBCASE("BinarySearchForSlotToInsert 2") {
        Array<int, 11> arr = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20};
        auto span = arr.Items();

        auto const r0 = BinarySearchForSlotToInsert(span, [](int i) { return i - 0; });
        auto const r1 = BinarySearchForSlotToInsert(span, [](int i) { return i - 1; });
        auto const r2 = BinarySearchForSlotToInsert(span, [](int i) { return i - 3; });
        auto const r3 = BinarySearchForSlotToInsert(span, [](int i) { return i - 5; });
        auto const r4 = BinarySearchForSlotToInsert(span, [](int i) { return i - 7; });
        auto const r10 = BinarySearchForSlotToInsert(span, [](int i) { return i - 19; });
        REQUIRE(r0 == 0);
        REQUIRE(r1 == 1);
        REQUIRE(r2 == 2);
        REQUIRE(r3 == 3);
        REQUIRE(r4 == 4);
        REQUIRE(r10 == 10);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAlgorithmTests) {
    REGISTER_TEST(TestStringSearching);
    REGISTER_TEST(TestSort);
    REGISTER_TEST(TestBinarySearch);
}
