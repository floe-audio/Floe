// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

TEST_CASE(TestDynamicArrayChar) {
    LeakDetectingAllocator a1;
    auto& a2 = Malloc::Instance();
    Allocator* allocators[] = {&a1, &a2};

    for (auto a_ptr : allocators) {
        auto& a = *a_ptr;
        SUBCASE("initialisation and assignment") {
            DynamicArray<char> s1(String("hello there"), a);
            DynamicArray<char> s2("hello there", a);
            DynamicArray<char> const s3(a);
            DynamicArray<char> const s4 {Malloc::Instance()};

            DynamicArray<char> const move_constructed(Move(s2));
            REQUIRE(move_constructed == "hello there"_s);

            DynamicArray<char> const move_assigned = Move(s1);
            REQUIRE(move_assigned == "hello there"_s);
        }

        SUBCASE("modify contents") {
            DynamicArray<char> s {a};
            dyn::AppendSpan(s, "aa"_s);
            REQUIRE(s.size == 2);
            REQUIRE(s == "aa"_s);
            dyn::Append(s, 'f');
            REQUIRE(s.size == 3);
            REQUIRE(s == "aaf"_s);
            dyn::PrependSpan(s, "bb"_s);
            REQUIRE(s.size == 5);
            REQUIRE(s == "bbaaf"_s);
            dyn::Prepend(s, 'c');
            REQUIRE(s == "cbbaaf"_s);

            dyn::Clear(s);
            REQUIRE(s.size == 0);

            dyn::Assign(s, "3000000"_s);
            dyn::Assign(s, "3"_s);
            REQUIRE(NullTerminatedSize(dyn::NullTerminated(s)) == s.size);
        }

        SUBCASE("iterators") {
            DynamicArray<char> const s {"hey", a};
            char const chars[] = {'h', 'e', 'y'};
            int index = 0;
            for (auto c : s)
                REQUIRE(c == chars[index++]);
        }
    }
    return k_success;
}

struct AllocedString {
    AllocedString() : data() {}
    AllocedString(String d) : data(d.Clone(Malloc::Instance())) {}
    AllocedString(AllocedString const& other) : data(other.data.Clone(Malloc::Instance())) {}
    AllocedString(AllocedString&& other) : data(other.data) { other.data = {}; }
    AllocedString& operator=(AllocedString const& other) {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
        data = other.data.Clone(Malloc::Instance());
        return *this;
    }
    AllocedString& operator=(AllocedString&& other) {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
        data = other.data;
        other.data = {};
        return *this;
    }
    ~AllocedString() {
        if (data.size) Malloc::Instance().Free(data.ToByteSpan());
    }
    friend bool operator==(AllocedString const& a, AllocedString const& b) { return a.data == b.data; }

    String data {};
};

template <typename Type>
TEST_CASE(TestDynamicArrayBasics) {
    Malloc a1;
    FixedSizeAllocator<50> fixed_size_a {&Malloc::Instance()};
    LeakDetectingAllocator a5;
    ArenaAllocator a2(fixed_size_a);
    ArenaAllocator a3(a5);
    FixedSizeAllocator<512> a4 {&Malloc::Instance()};
    Allocator* allocators[] = {&a1, &a2, &a3, &a4, &a5};

    for (auto a_ptr : allocators) {
        auto& a = *a_ptr;
        DynamicArray<Type> buf(a);
        auto const default_initialised = !Fundamental<Type>;

        auto check_grow_buffer_incrementally = [&]() {
            usize const max = 550;
            for (usize i = 1; i <= max; ++i) {
                dyn::Resize(buf, i);
                REQUIRE(buf.size == i);
                REQUIRE(buf.Items().size == i);
                if (default_initialised) REQUIRE(*buf.data == Type());
            }
            REQUIRE(buf.size == max);
            REQUIRE(buf.Items().size == max);
        };

        SUBCASE("Initial values") {
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);
        }

        SUBCASE("Reserve small") {
            buf.Reserve(10);
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);

            SUBCASE("Resize small") {
                dyn::Resize(buf, 1);
                REQUIRE(buf.size == 1);
                REQUIRE(buf.Items().size == 1);
                if (default_initialised) REQUIRE(*buf.data == Type());
            }

            SUBCASE("Resize incrementally") { check_grow_buffer_incrementally(); }
        }

        SUBCASE("Reserve large") {
            buf.Reserve(1000);
            REQUIRE(buf.size == 0);
            REQUIRE(buf.Items().size == 0);

            SUBCASE("Resize incrementally") { check_grow_buffer_incrementally(); }
        }

        SUBCASE("Grow incrementally") { check_grow_buffer_incrementally(); }

        SUBCASE("iterate") {
            dyn::Resize(buf, 4);
            for (auto& i : buf)
                (void)i;
            for (auto const& i : buf)
                (void)i;
        }

        if constexpr (Same<int, Type>) {
            SUBCASE("Add 10 values then resize to heap data") {
                dyn::Resize(buf, 10);
                REQUIRE(buf.size == 10);
                REQUIRE(buf.Items().size == 10);

                for (auto const i : Range(10))
                    buf.Items()[(usize)i] = i + 1;

                dyn::Resize(buf, 1000);

                for (auto const i : Range(10))
                    REQUIRE(buf.Items()[(usize)i] == i + 1);
            }

            SUBCASE("To owned span") {
                SUBCASE("with span lifetime shorter than array") {
                    dyn::Resize(buf, 10);
                    REQUIRE(buf.size == 10);

                    auto span = buf.ToOwnedSpan();
                    DEFER { a.Free(span.ToByteSpan()); };
                    REQUIRE(buf.size == 0);
                    REQUIRE(buf.Capacity() == 0);

                    REQUIRE(span.size == 10);
                }

                SUBCASE("with span lifetime longer than array") {
                    Span<int> span {};

                    {
                        DynamicArray<int> other {a};
                        dyn::Resize(other, 10);

                        span = other.ToOwnedSpan();
                        REQUIRE(other.size == 0);
                        REQUIRE(other.Capacity() == 0);
                        REQUIRE(span.size == 10);
                    }

                    a.Free(span.ToByteSpan());
                }
            }

            SUBCASE("Modify contents") {
                dyn::Append(buf, 10);
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == 10);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, 20);
                dyn::Prepend(buf, 30);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == 30);
                REQUIRE(Last(buf) == 20);
                REQUIRE(buf[1] == 20);

                DynamicArray<Type> other {a};
                dyn::Append(other, 99);
                dyn::Append(other, 100);
                dyn::Append(other, 101);

                dyn::AppendSpan(buf, other.Items());
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == 30);
                REQUIRE(buf[1] == 20);
                REQUIRE(buf[2] == 99);
                REQUIRE(buf[3] == 100);
                REQUIRE(buf[4] == 101);

                auto null_term_data = dyn::NullTerminated(buf);
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == 30);
                REQUIRE(buf[1] == 20);
                REQUIRE(buf[2] == 99);
                REQUIRE(buf[3] == 100);
                REQUIRE(buf[4] == 101);
                REQUIRE(null_term_data[5] == 0);

                SUBCASE("RemoveValue") {
                    dyn::Assign(buf, ArrayT<int>({1, 3, 5, 1, 2, 1, 1}));
                    dyn::RemoveValue(buf, 1);
                    REQUIRE(buf.size == 3);
                    REQUIRE(buf[0] == 3);
                    REQUIRE(buf[1] == 5);
                    REQUIRE(buf[2] == 2);

                    dyn::Assign(buf, ArrayT<int>({1, 1, 1, 1}));
                    dyn::RemoveValue(buf, 1);
                    REQUIRE(buf.size == 0);
                }

                SUBCASE("RemoveSwapLast") {
                    dyn::Assign(buf, ArrayT<int>({3, 5, 6}));
                    dyn::RemoveSwapLast(buf, 0);
                    for (auto v : buf)
                        REQUIRE(v == 5 || v == 6);
                }

                SUBCASE("AppendIfNotAlreadyThere") {
                    dyn::Assign(buf, Array {3, 5, 6});
                    dyn::AppendIfNotAlreadyThere(buf, 3);
                    REQUIRE(buf.size == 3);
                    dyn::AppendIfNotAlreadyThere(buf, 4);
                    REQUIRE(buf.size == 4);
                    dyn::Clear(buf);
                    dyn::AppendIfNotAlreadyThere(buf, 1);
                    REQUIRE(buf.size);
                }
            }

            SUBCASE("Initialiser list") {
                dyn::Assign(buf, ArrayT<int>({20, 31, 50}));
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == 20);
                REQUIRE(buf[1] == 31);
                REQUIRE(buf[2] == 50);

                DynamicArray<Type> other {a};
                dyn::Assign(other, ArrayT<Type>({999, 999}));
                REQUIRE(other.size == 2);
                REQUIRE(other[0] == 999);
                REQUIRE(other[1] == 999);

                dyn::Append(other, Type {40});
                REQUIRE(other.size == 3);
                dyn::AppendSpan(other, ArrayT<Type>({41, 42}));
                REQUIRE(other.size == 5);
            }

            SUBCASE("move") {
                SUBCASE("no reserve") { buf.Reserve(0); }
                SUBCASE("big reserve") { buf.Reserve(1000); }

                dyn::Append(buf, 10);
                dyn::Append(buf, 11);
                dyn::Append(buf, 12);
                SUBCASE("constructor") {
                    DynamicArray<Type> other(Move(buf));
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                    REQUIRE(other.size == 3);
                }

                SUBCASE("assign operators") {
                    DynamicArray<Type> other {a};
                    SUBCASE("move") {
                        SUBCASE("existing static") {
                            dyn::Append(other, 99);
                            other = Move(buf);
                        }
                        SUBCASE("existing heap") {
                            other.Reserve(1000);
                            dyn::Append(other, 99);
                            other = Move(buf);
                        }
                    }

                    REQUIRE(other.size == 3);
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                }

                SUBCASE("assign operator with different allocator") {
                    FixedSizeAllocator<512> other_a {&Malloc::Instance()};
                    DynamicArray<Type> other(other_a);
                    dyn::Append(other, 99);
                    other = Move(buf);

                    REQUIRE(other.size == 3);
                    REQUIRE(other[0] == 10);
                    REQUIRE(other[1] == 11);
                    REQUIRE(other[2] == 12);
                }
            }
        }

        if constexpr (Same<AllocedString, Type>) {
            SUBCASE("Add 10 values then resize to heap data") {
                dyn::Resize(buf, 10);
                REQUIRE(buf.size == 10);
                REQUIRE(buf.Items().size == 10);

                auto make_long_string = [&tester](int i) {
                    return AllocedString(
                        fmt::Format(tester.scratch_arena, "this is a long string with a number: {}", i + 1));
                };

                for (auto const i : Range(10))
                    buf.Items()[(usize)i] = make_long_string(i);
            }
            SUBCASE("Modify contents with move") {
                AllocedString foo1 {"foo1"};
                AllocedString foo2 {"foo2"};
                AllocedString foo3 {"foo3"};

                dyn::Append(buf, Move(foo1));
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "foo1"_s);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, Move(foo2));
                dyn::Prepend(buf, Move(foo3));
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "foo3"_s);
                REQUIRE(Last(buf) == "foo2"_s);
            }

            SUBCASE("Modify contents") {
                dyn::Append(buf, "a");
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[0] == "a"_s);

                dyn::Clear(buf);
                REQUIRE(buf.size == 0);

                dyn::Append(buf, "b"_s);
                dyn::Prepend(buf, "c"_s);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(Last(buf) == "b"_s);
                REQUIRE(buf[1] == "b"_s);

                String long_string = "long string to ensure that short string optimisations are not involved";

                DynamicArray<Type> other {a};
                dyn::Append(other, "d"_s);
                dyn::Append(other, "e"_s);
                dyn::Append(other, long_string);

                dyn::AppendSpan(buf, other.Items());
                REQUIRE(buf.size == 5);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(buf[1] == "b"_s);
                REQUIRE(buf[2] == "d"_s);
                REQUIRE(buf[3] == "e"_s);
                REQUIRE(buf[4] == long_string);

                dyn::Insert(buf, 0, "yo"_s);
                REQUIRE(buf.size == 6);
                REQUIRE(buf[0] == "yo"_s);
                REQUIRE(buf[1] == "c"_s);

                dyn::Insert(buf, 3, "3"_s);
                REQUIRE(buf.size == 7);
                REQUIRE(buf[3] == "3"_s);
                REQUIRE(buf[4] == "d"_s);
                REQUIRE(buf[5] == "e"_s);
                REQUIRE(buf[6] == long_string);

                dyn::Insert(buf, 6, "6"_s);
                REQUIRE(buf.size == 8);
                REQUIRE(buf[6] == "6"_s);

                dyn::Remove(buf, 0);
                REQUIRE(buf.size == 7);
                REQUIRE(buf[0] == "c"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 3);
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[1] == "b"_s);
                REQUIRE(buf[2] == "c"_s);

                dyn::Remove(buf, 1);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "a"_s);
                REQUIRE(buf[1] == "c"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 1, 10);
                REQUIRE(buf.size == 1);
                REQUIRE(buf[0] == "a"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 0, 2);
                REQUIRE(buf.size == 2);
                REQUIRE(buf[0] == "c"_s);
                REQUIRE(buf[1] == "d"_s);

                dyn::Assign(buf, ArrayT<Type>({"a"_s, "b"_s, "c"_s, "d"_s}));
                dyn::Remove(buf, 10, 2);
                REQUIRE(buf.size == 4);

                dyn::Clear(buf);
                dyn::Insert(buf, 0, "foo"_s);
                dyn::Clear(buf);
                dyn::Insert(buf, 10, "foo"_s);
                REQUIRE(buf.size == 0);

                dyn::Remove(buf, 0);
                dyn::Remove(buf, 10);

                AllocedString strs_data[] = {"1"_s, "2"_s, "3"_s};
                Span<AllocedString> const strs {strs_data, ArraySize(strs_data)};
                dyn::Clear(buf);
                dyn::InsertSpan(buf, 0, strs);
                REQUIRE(buf.size == 3);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "3"_s);

                dyn::InsertSpan(buf, 3, strs);
                REQUIRE(buf.size == 6);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "3"_s);
                REQUIRE(buf[3] == "1"_s);
                REQUIRE(buf[4] == "2"_s);
                REQUIRE(buf[5] == "3"_s);

                dyn::InsertSpan(buf, 2, strs);
                REQUIRE(buf.size == 9);
                REQUIRE(buf[0] == "1"_s);
                REQUIRE(buf[1] == "2"_s);
                REQUIRE(buf[2] == "1"_s);
                REQUIRE(buf[3] == "2"_s);
                REQUIRE(buf[4] == "3"_s);
                REQUIRE(buf[5] == "3"_s);
                REQUIRE(buf[6] == "1"_s);
                REQUIRE(buf[7] == "2"_s);
                REQUIRE(buf[8] == "3"_s);
            }

            SUBCASE("Remove") {
                DynamicArray<char> str {"012345"_s, a};
                dyn::Remove(str, 0, 2);
                REQUIRE(str == "2345"_s);
                dyn::Remove(str, 0, 100);
                REQUIRE(str == ""_s);
            }

            SUBCASE("Insert") {
                DynamicArray<char> str {"012345"_s, a};
                dyn::InsertSpan(str, 0, "aa"_s);
                REQUIRE(str == "aa012345"_s);
                dyn::InsertSpan(str, 4, "777"_s);
                REQUIRE(str == "aa017772345"_s);
            }

            SUBCASE("Replace") {
                DynamicArray<char> str {a};
                dyn::Assign(str, "aa bb cc aa d"_s);
                SUBCASE("with a longer string") {
                    dyn::Replace(str, "aa"_s, "fff"_s);
                    REQUIRE(str == "fff bb cc fff d"_s);
                }
                SUBCASE("with a shorter string") {
                    dyn::Replace(str, "aa"_s, "f"_s);
                    REQUIRE(str == "f bb cc f d"_s);
                }
                SUBCASE("a single character") {
                    dyn::Replace(str, "d"_s, "e"_s);
                    REQUIRE(str == "aa bb cc aa e"_s);
                }
                SUBCASE("empty existing value") {
                    dyn::Replace(str, ""_s, "fff"_s);
                    REQUIRE(str == "aa bb cc aa d"_s);
                }
                SUBCASE("empty replacement") {
                    dyn::Replace(str, "aa"_s, ""_s);
                    REQUIRE(str == " bb cc  d"_s);
                }
            }
        }
    }
    return k_success;
}

TEST_CASE(TestDynamicArrayClone) {
    LeakDetectingAllocator a;

    SUBCASE("deep") {
        auto& arr_alloc = Malloc::Instance();

        DynamicArray<DynamicArray<String>> arr {arr_alloc};
        DynamicArray<String> const strs {arr_alloc};

        dyn::Append(arr, strs.Clone(a, CloneType::Deep));
        dyn::Append(arr, strs.Clone(a, CloneType::Deep));
        dyn::Prepend(arr, strs.Clone(a, CloneType::Deep));
        dyn::Insert(arr, 1, strs.Clone(a, CloneType::Deep));
        dyn::Remove(arr, 0);

        SUBCASE("move assigning does not change the allocator") {
            DynamicArray<DynamicArray<String>> other_arr {a};
            dyn::Append(other_arr, strs.Clone(a, CloneType::Deep));
            arr = Move(other_arr);
            REQUIRE(&arr.allocator == &arr_alloc);
            REQUIRE(&other_arr.allocator == &a);
        }
    }

    SUBCASE("shallow") {
        DynamicArray<Optional<String>> buf {a};
        dyn::Append(buf, "1"_s);
        dyn::Append(buf, "2"_s);
        dyn::Append(buf, k_nullopt);

        auto const duped = buf.Clone(a, CloneType::Shallow);
        REQUIRE(duped.size == 3);
        REQUIRE(duped[0].HasValue());
        REQUIRE(duped[0].Value() == "1"_s);
        REQUIRE(duped[1].HasValue());
        REQUIRE(duped[1].Value() == "2"_s);
        REQUIRE(!duped[2].HasValue());
    }

    return k_success;
}

TEST_CASE(TestDynamicArrayString) {
    DynamicArrayBounded<char, 64> buf;
    dyn::Assign(buf, "a   "_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    dyn::Assign(buf, "   a"_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    dyn::Assign(buf, "   a   "_s);
    dyn::TrimWhitespace(buf);
    REQUIRE(buf == "a"_s);
    return k_success;
}

TEST_CASE(TestDynamicArrayBoundedBasics) {
    SUBCASE("Basics") {
        DynamicArrayBounded<char, 10> arr {"aa"_s};
        REQUIRE(arr == "aa"_s);
        REQUIRE(arr.data);
        REQUIRE(arr.size);
        REQUIRE(*arr.data == 'a');
    }

    SUBCASE("Move") {
        DynamicArrayBounded<char, 10> a {"aa"_s};
        DynamicArrayBounded<char, 10> b {Move(a)};
        REQUIRE(b == "aa"_s);

        DynamicArrayBounded<char, 10> c {"bb"_s};
        b = Move(c);
        REQUIRE(b == "bb"_s);
    }

    SUBCASE("Overflow") {
        LeakDetectingAllocator alloc;
        DynamicArrayBounded<DynamicArray<char>, 4> arr;
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));

        REQUIRE(!dyn::Append(arr, DynamicArray<char>("foo", alloc)));
        REQUIRE(!dyn::Insert(arr, 1, DynamicArray<char>("foo", alloc)));

        dyn::Clear(arr);

        REQUIRE(dyn::Append(arr, DynamicArray<char>("foo", alloc)));
    }
    return k_success;
}

TEST_REGISTRATION(RegisterDynamicArrayTests) {
    REGISTER_TEST(TestDynamicArrayChar);
    REGISTER_TEST(TestDynamicArrayBasics<AllocedString>);
    REGISTER_TEST(TestDynamicArrayBasics<Optional<AllocedString>>);
    REGISTER_TEST(TestDynamicArrayBasics<int>);
    REGISTER_TEST(TestDynamicArrayBoundedBasics);
    REGISTER_TEST(TestDynamicArrayClone);
    REGISTER_TEST(TestDynamicArrayString);
}
