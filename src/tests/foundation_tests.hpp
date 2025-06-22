// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

TEST_CASE(TestTaggedUnion) {
    enum class E {
        A,
        B,
        C,
        D,
    };
    using TU = TaggedUnion<E, TypeAndTag<int, E::A>, TypeAndTag<float, E::B>, TypeAndTag<String, E::C>>;

    TU u {int {}};

    SUBCASE("visit") {
        u = 999;
        u.Visit([&](auto const& arg) { tester.log.Debug("Tagged union value is: {}", arg); });

        u = 3.14f;
        u.Visit([&](auto const& arg) { tester.log.Debug("Tagged union value is: {}", arg); });

        u = E::D;
        u.Visit(
            [&](auto const&) { tester.log.Debug("ERROR not expected a tag without a type to be called"); });

        u = "hello"_s;
        u.Visit([&](auto const& arg) { tester.log.Debug("Tagged union value is: {}", arg); });

        tester.log.Debug("Formatting a tagged union: {}", u);
    }

    SUBCASE("format") {
        u = "hello"_s;
        tester.log.Debug("Formatting a tagged union: {}", u);
    }

    SUBCASE("comparison") {
        u = "hello"_s;
        CHECK(u == TU {"hello"_s});
        CHECK(u != TU {3.14f});
        CHECK(u != TU {E::D});

        u = E::D;
        CHECK(u == TU {E::D});
        CHECK(u != TU {3.14f});
    }

    return k_success;
}

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

TEST_CASE(TestBitset) {
    {
        Bitset<65> b;
        REQUIRE(!b.AnyValuesSet());
        b.Set(0);
        REQUIRE(b.Get(0));
        REQUIRE(b.FirstUnsetBit() == 1);

        b <<= 1;
        REQUIRE(b.Get(1));
        REQUIRE(!b.Get(0));
        REQUIRE(b.FirstUnsetBit() == 0);

        b >>= 1;
        REQUIRE(b.Get(0));
        REQUIRE(b.AnyValuesSet());
        b.ClearAll();
        REQUIRE(!b.AnyValuesSet());

        b.SetToValue(5, true);
        auto smaller_bitset = b.Subsection<10>(0);
        REQUIRE(smaller_bitset.Get(5));

        b.ClearAll();

        Bitset<65> other;
        other.SetAll();
        b = other;
        REQUIRE(b.AnyValuesSet());
        b = ~b;
        REQUIRE(!b.AnyValuesSet());

        other.ClearAll();
        other.Set(64);
        b |= other;
        REQUIRE(b.Get(64));
        REQUIRE(other.Get(64));

        other.ClearAll();
        b &= other;
        REQUIRE(!b.AnyValuesSet());

        b.ClearAll();
        REQUIRE(b.NumSet() == 0);
        b.Set(0);
        b.Set(64);
        REQUIRE(b.NumSet() == 2);
    }

    {
        Bitset<8> const b(0b00101010);
        REQUIRE(b.Subsection<3>(2).elements[0] == 0b010);
    }

    {
        Bitset<8> const b(0b11110000);
        REQUIRE(!b.Get(0));
        REQUIRE(b.Get(7));
        REQUIRE(b.Subsection<4>(4).elements[0] == 0b1111);
    }

    {
        Bitset<8> const b(0b00100100);
        REQUIRE(b.Subsection<4>(2).elements[0] == 0b1001);
    }

    {
        Bitset<8> b(0b00000000);
        REQUIRE(b.FirstUnsetBit() == 0);
        b.Set(0);
        REQUIRE(b.FirstUnsetBit() == 1);
        b.Set(1);
        REQUIRE(b.FirstUnsetBit() == 2);
    }

    {
        // test FirstUnsetBit across element boundary
        Bitset<128> b {};
        for (usize i = 0; i < 128; ++i)
            b.Set(i);
        REQUIRE(b.FirstUnsetBit() == 128);
        b.Clear(127);
        REQUIRE(b.FirstUnsetBit() == 127);
        b.Clear(64);
        REQUIRE(b.FirstUnsetBit() == 64);
    }

    {
        Bitset<128> b {};
        for (usize i = 64; i < 128; ++i)
            b.Set(i);
        REQUIRE(b.NumSet() == 64);

        auto const sub = b.Subsection<10>(60);
        REQUIRE(sub.Get(0) == 0);
        REQUIRE(sub.Get(1) == 0);
        REQUIRE(sub.Get(2) == 0);
        REQUIRE(sub.Get(3) == 0);
        REQUIRE(sub.Get(4) != 0);

        auto const sub2 = b.Subsection<64>(64);
        REQUIRE(sub2.NumSet() == 64);
    }
    return k_success;
}

TEST_CASE(TestCircularBuffer) {
    LeakDetectingAllocator allocator;
    CircularBuffer<int> buf {allocator};

    SUBCASE("basics") {
        CHECK(buf.Empty());
        CHECK(buf.Full());
        CHECK(buf.Size() == 0);

        for (auto _ : Range(2)) {
            buf.Push(1);
            CHECK(!buf.Empty());
            CHECK(!buf.Full());
            CHECK(buf.Size() == 1);

            CHECK_EQ(buf.Pop(), 1);
            CHECK(buf.Empty());
            CHECK(!buf.Full());
            CHECK(buf.Size() == 0);
        }

        CHECK(IsPowerOfTwo(buf.buffer.size));
    }

    SUBCASE("push elements") {
        for (auto pre_pushes : Array {10, 11, 13, 50, 100, 9}) {
            CAPTURE(pre_pushes);
            for (auto const i : Range(pre_pushes))
                buf.Push(i);
            for (auto _ : Range(pre_pushes))
                buf.Pop();

            for (auto const i : Range(100))
                buf.Push(i);
            for (auto const i : Range(100))
                CHECK_EQ(buf.Pop(), i);
        }

        for (auto const i : Range(10000))
            buf.Push(i);
        for (auto const i : Range(10000))
            CHECK_EQ(buf.Pop(), i);
    }

    SUBCASE("clear") {
        for (auto const i : Range(32))
            buf.Push(i);
        buf.Clear();
        CHECK(buf.Empty());
        CHECK(!buf.TryPop().HasValue());
    }

    SUBCASE("move assign") {
        SUBCASE("both empty") {
            CircularBuffer<int> buf2 {allocator};
            buf = Move(buf2);
        }
        SUBCASE("new is full") {
            CircularBuffer<int> buf2 {allocator};
            for (auto const i : Range(32))
                buf2.Push(i);
            SUBCASE("old is full") {
                for (auto const i : Range(32))
                    buf.Push(i);
            }
            buf = Move(buf2);
            CHECK(buf.Size() == 32);
            for (auto const i : Range(32))
                CHECK_EQ(buf.Pop(), i);
        }
    }

    SUBCASE("move construct") {
        SUBCASE("empty") { CircularBuffer<int> const buf2 = Move(buf); }
        SUBCASE("full") {
            for (auto const i : Range(32))
                buf.Push(i);
            CircularBuffer<int> const buf2 = Move(buf);
        }
    }

    return k_success;
}

TEST_CASE(TestCircularBufferRefType) {
    LeakDetectingAllocator allocator;
    {
        struct Foo {
            int& i;
        };

        CircularBuffer<Foo> buf {allocator};

        int i = 66;
        Foo const foo {i};
        buf.Push(foo);
        auto result = buf.Pop();
        CHECK(&result.i == &i);
    }

    {
        Array<u16, 5000> bytes;
        for (auto [i, b] : Enumerate<u16>(bytes))
            b = i;

        struct Foo {
            u16& i;
        };
        CircularBuffer<Foo> buf {allocator};

        u16 warmup {};
        for (auto _ : Range(51))
            buf.Push({warmup});
        for (auto _ : Range(51))
            CHECK(&buf.Pop().i == &warmup);

        for (auto& b : bytes)
            buf.Push({b});

        for (auto& b : bytes)
            CHECK(&buf.Pop().i == &b);
    }

    {
        CircularBuffer<int> buf {PageAllocator::Instance()};

        int push_counter = 0;
        int pop_counter = 0;
        for (auto _ : Range(10000)) {
            auto update = RandomIntInRange<int>(tester.random_seed, -8, 8);
            if (update < 0) {
                while (update != 0) {
                    if (auto v = buf.TryPop()) REQUIRE_EQ(v, pop_counter++);
                    ++update;
                }
            } else {
                while (update != 0) {
                    buf.Push(push_counter++);
                    --update;
                }
            }
        }
    }

    return k_success;
}

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

TEST_CASE(TestWriter) {
    SUBCASE("alloced") {
        LeakDetectingAllocator a;
        DynamicArray<char> buf {a};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }

    SUBCASE("inline") {
        DynamicArrayBounded<char, 128> buf {};
        auto writer = dyn::WriterFor(buf);
        TRY(writer.WriteBytes(Array {(u8)'a'}));
        CHECK_EQ(buf.Items(), "a"_s);
    }

    SUBCASE("BufferedWriter") {
        LeakDetectingAllocator a;
        DynamicArray<char> buf {a};

        BufferedWriter<16> buffered_writer {dyn::WriterFor(buf)};
        auto writer = buffered_writer.Writer();

        TRY(writer.WriteChars("hello"_s));
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello"_s);

        TRY(writer.WriteChars(" world"_s));
        CHECK_EQ(buf.Items(), "hello"_s);
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello world"_s);

        TRY(writer.WriteChars("01234567890123456789"_s));
        TRY(buffered_writer.Flush());
        CHECK_EQ(buf.Items(), "hello world01234567890123456789"_s);

        dyn::Clear(buf);

        // ensure that when it reaches the end of the buffer it correctly flushs and continues without missing
        // any characters
        for (auto ch : Range<char>('a', 'z' + 1))
            TRY(writer.WriteChar(ch));
        TRY(buffered_writer.Flush());

        CHECK_EQ(buf.Items(), "abcdefghijklmnopqrstuvwxyz"_s);
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

void SimpleFunction() {}

template <typename FunctionType>
static ErrorCodeOr<void> TestTrivialFunctionBasics(tests::Tester& tester, FunctionType& f) {
    f();
    int captured = 24;
    f = [captured, &tester]() { REQUIRE(captured == 24); };
    f();
    f = []() {};
    f();

    auto const lambda = [&tester]() { REQUIRE(true); };
    f = lambda;
    f();

    Array<char, 16> bloat;
    auto const lambda_large = [&tester, bloat]() {
        REQUIRE(true);
        (void)bloat;
    };
    f = lambda_large;
    f();

    f = Move(lambda);
    f();

    {
        f = [captured, &tester]() { REQUIRE(captured == 24); };
    }
    f();

    if constexpr (CopyConstructible<FunctionType>) {
        auto other_f = f;
        other_f();

        auto other_f2 = Move(f);
        other_f2();
    }
    return k_success;
}

TEST_CASE(TestFunction) {
    SUBCASE("Fixed size") {
        SUBCASE("basics") {
            TrivialFixedSizeFunction<24, void()> f {SimpleFunction};
            static_assert(TriviallyCopyable<decltype(f)>);
            static_assert(TriviallyDestructible<decltype(f)>);
            TRY(TestTrivialFunctionBasics(tester, f));
        }

        SUBCASE("captures are copied 1") {
            int value = 0;
            TrivialFixedSizeFunction<8, void()> a = [&value]() { value = 1; };
            TrivialFixedSizeFunction<8, void()> b = [&value]() { value = 2; };

            value = 0;
            a();
            CHECK_EQ(value, 1);

            value = 0;
            b();
            CHECK_EQ(value, 2);

            value = 0;
            b = a;
            a = []() {};
            b();
            CHECK_EQ(value, 1);
        }

        SUBCASE("captures are copied 2") {
            bool a_value = false;
            bool b_value = false;
            TrivialFixedSizeFunction<8, void()> a = [&a_value]() { a_value = true; };
            TrivialFixedSizeFunction<8, void()> b = [&b_value]() { b_value = true; };

            b = a;
            a = []() {};
            b();
            CHECK(a_value);
            CHECK(!b_value);
        }
    }

    SUBCASE("Allocated") {
        LeakDetectingAllocator allocator;
        TrivialAllocatedFunction<void()> f {SimpleFunction, allocator};
        TRY(TestTrivialFunctionBasics(tester, f));

        SUBCASE("captures are copied") {
            int value = 0;
            TrivialAllocatedFunction<void()> a {[&value]() { value = 1; }, allocator};
            TrivialAllocatedFunction<void()> b {[&value]() { value = 2; }, allocator};

            value = 0;
            a();
            CHECK_EQ(value, 1);

            value = 0;
            b();
            CHECK_EQ(value, 2);
        }
    }

    SUBCASE("Ref") {
        TrivialFunctionRef<void()> f {};
        static_assert(TriviallyCopyable<decltype(f)>);
        static_assert(TriviallyDestructible<decltype(f)>);

        f = SimpleFunction;
        f();
        auto const lambda = [&tester]() { REQUIRE(true); };
        f = lambda;
        f();

        LeakDetectingAllocator allocator;
        {
            TrivialAllocatedFunction<void()> const allocated_f {f, allocator};
            allocated_f();
        }

        f = SimpleFunction;
        {
            TrivialAllocatedFunction<void()> const allocated_f {f, allocator};
            allocated_f();
        }

        int value = 100;
        auto const other_lambda = [&tester, value]() { REQUIRE(value == 100); };

        TrivialFunctionRef<void()> other;
        {
            f = other_lambda;
            other = f.CloneObject(tester.scratch_arena);
        }
        [[maybe_unused]] char push_stack[32];
        other();
    }

    return k_success;
}

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

template <HashTableOrdering k_ordering>
TEST_CASE(TestHashTable) {
    auto& a = tester.scratch_arena;

    SUBCASE("table") {
        DynamicHashTable<String, usize, nullptr, k_ordering> tab {a, 16u};

        CHECK(tab.table.size == 0);
        CHECK(tab.table.Elements().size >= 16);

        {
            usize count = 0;
            for (auto item : tab) {
                (void)item;
                ++count;
            }
            CHECK(count == 0);
        }

        CHECK(tab.Insert("foo", 42));
        CHECK(tab.Insert("bar", 31337));
        CHECK(tab.Insert("qux", 64));
        CHECK(tab.Insert("900", 900));
        CHECK(tab.Insert("112", 112));

        CHECK(tab.Find("foo"));
        CHECK(tab.Find("bar"));
        CHECK(!tab.Find("baz"));

        CHECK(tab.table.Elements().size > 5);
        CHECK(tab.table.size == 5);

        {
            auto v = tab.Find("bar");
            REQUIRE(v);
            tester.log.Debug("{}", *v);
        }

        {
            usize count = 0;
            for (auto item : tab) {
                CHECK(item.key.size);
                tester.log.Debug("{} -> {}", item.key, item.value);
                if (item.key == "112") item.value++;
                ++count;
            }
            CHECK(count == 5);
            auto v = tab.Find("112");
            CHECK(v && *v == 113);
        }

        for (auto const i : Range(10000uz))
            CHECK(tab.Insert(fmt::Format(a, "key{}", i), i));

        // test Assign()
        DynamicHashTable<String, usize, nullptr, k_ordering> other {a, 16u};
        CHECK(other.table.size == 0);
        CHECK(other.Insert("foo", 42));

        tab.Assign(other);
        CHECK(tab.table.size == 1);
    }

    SUBCASE("grow and delete") {
        for (auto insertions : Range(4uz, 32uz)) {
            HashTable<usize, usize, nullptr, k_ordering> tab {};
            for (auto i : Range(insertions)) {
                auto const result = tab.FindOrInsertGrowIfNeeded(a, i, i * 2);
                CHECK(result.inserted);
                tester.log.Debug("Inserted '{}', capacity: {}", i, tab.Capacity());
            }
            CHECK_EQ(tab.size, insertions);
            for (auto i : Range(insertions))
                tab.Delete(i);
            CHECK_EQ(tab.size, 0uz);
            for (auto i : Range(insertions * 4)) {
                auto const result = tab.FindOrInsertGrowIfNeeded(a, i, i * 2);
                CHECK(result.inserted);
            }
            CHECK_EQ(tab.size, insertions * 4);
        }
    }

    SUBCASE("reserve") {
        for (auto count : Range(4uz, 32uz)) {
            HashTable<usize, usize, nullptr, k_ordering> tab {};
            tab.Reserve(a, count);
            CHECK_EQ(tab.size, 0uz);
            for (auto i : Range(count)) {
                auto const result = tab.FindOrInsertWithoutGrowing(i, i * 2);
                CHECK(result.inserted);
            }
            CHECK_EQ(tab.size, count);
        }
    }

    SUBCASE("no initial size") {
        DynamicHashTable<String, int, nullptr, k_ordering> tab {a};
        CHECK(tab.Insert("foo", 100));
        for (auto item : tab)
            CHECK_EQ(item.value, 100);
        auto v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 100);
        *v = 200;
        v = tab.Find("foo");
        REQUIRE(v);
        CHECK_EQ(*v, 200);

        CHECK(tab.table.size == 1);

        CHECK(tab.Delete("foo"));

        CHECK(tab.table.size == 0);
    }

    SUBCASE("move") {
        LeakDetectingAllocator a2;

        SUBCASE("construct") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> const tab2 {Move(tab1)};
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign same allocator") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> tab2 {a2};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
        SUBCASE("assign different allocator") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a2};
            CHECK(tab1.Insert("foo", 100));
            DynamicHashTable<String, int, nullptr, k_ordering> tab2 {Malloc::Instance()};
            tab2 = Move(tab1);
            auto v = tab2.Find("foo");
            REQUIRE(v);
        }
    }

    SUBCASE("Intersect") {
        DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a};
        CHECK(tab1.Insert("foo", 100));
        CHECK(tab1.Insert("bar", 200));

        DynamicHashTable<String, int, nullptr, k_ordering> tab2 {a};
        CHECK(tab2.Insert("bar", 200));
        CHECK(tab2.Insert("baz", 400));

        tab1.table.IntersectWith(tab2);
        CHECK(tab1.table.size == 1);
        auto v = tab1.Find("bar");
        REQUIRE(v);
    }

    if constexpr (k_ordering == HashTableOrdering::Ordered) {
        SUBCASE("Ordered") {
            DynamicHashTable<String, int, nullptr, k_ordering> tab1 {a};
            CHECK(tab1.Insert("b", 0));
            CHECK(tab1.Insert("c", 0));
            CHECK(tab1.Insert("a", 0));
            CHECK(tab1.Insert("d", 0));

            CHECK(tab1.table.size == 4);

            {
                auto it = tab1.begin();
                CHECK_EQ((*it).key, "a"_s);
                ++it;
                CHECK_EQ((*it).key, "b"_s);
                ++it;
                CHECK_EQ((*it).key, "c"_s);
                ++it;
                CHECK_EQ((*it).key, "d"_s);
                ++it;
                CHECK(it == tab1.end());
            }

            // Remove "b" and re-check
            {
                CHECK(tab1.Delete("b"));
                CHECK(tab1.table.size == 3);
                auto it = tab1.begin();
                CHECK_EQ((*it).key, "a"_s);
                ++it;
                CHECK_EQ((*it).key, "c"_s);
                ++it;
                CHECK_EQ((*it).key, "d"_s);
                ++it;
                CHECK(it == tab1.end());
            }

            // Delete all, then add a couple items back
            {
                tab1.DeleteAll();
                CHECK(tab1.table.size == 0);

                CHECK(tab1.Insert("x", 100));
                CHECK(tab1.Insert("y", 200));
                CHECK(tab1.table.size == 2);

                auto it = tab1.begin();
                CHECK_EQ((*it).key, "x"_s);
                ++it;
                CHECK_EQ((*it).key, "y"_s);
                ++it;
                CHECK(it == tab1.end());
            }
        }
    }

    SUBCASE("correct retrieval") {
        HashTable<int, int, nullptr, k_ordering> table {};
        for (int i = 0; i < 1000; ++i) {
            auto const result = table.FindOrInsertGrowIfNeeded(a, i, i * 2);
            CHECK(result.inserted);
        }

        CHECK(table.size == 1000);
        for (auto const& item : table)
            CHECK(item.value == item.key * 2);
    }

    SUBCASE("find or insert") {
        // Test when a table is grown - is the correct element still returned?
        HashTable<String, usize, nullptr, k_ordering> table {};

        usize index = 0;
        for (auto const str : Array {
                 "Vocal Ohh"_s,
                 "Vocal Eee"_s,
                 "Air - Restless Canopy"_s,
                 "Low - Alien Kerogen"_s,
                 "Low - Bass Arena"_s,
                 "Mid - Tickseed Ambience"_s,
                 "Noise - Electric Hiss"_s,
                 "Noise - Static"_s,
                 "Vocal Ooo"_s,
                 "New value"_s,
                 "Other"_s,
                 "New"_s,
                 "String"_s,
                 "Link"_s,
                 "List"_s,
                 "Text"_s,
                 "aaaa"_s,
                 "bbbb"_s,
                 "cccc"_s,
                 "dddd"_s,
                 "e"_s,
                 "1"_s,
                 "2"_s,
             }) {
            auto const result = table.FindOrInsertGrowIfNeeded(a, str, index);
            CHECK(result.inserted);
            CHECK(result.element.data == index);
            ++index;
            CHECK(table.size == index);
        };
    }

    SUBCASE("iteration") {
        auto tags = HashTable<String, Set<String, nullptr, k_ordering>, nullptr, k_ordering>::Create(a, 16);

        auto check = [&]() {
            for (auto const [name, set, name_hash] : tags) {
                CHECK(name.size);
                CHECK(IsValidUtf8(name));
                CHECK(name_hash != 0);
                CHECK(set.size);

                for (auto const [tag, tag_hash] : set) {
                    REQUIRE(tag.size);
                    REQUIRE(tag.size < 64);
                    CHECK(IsValidUtf8(tag));
                }
            }
        };

        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Air - Tephra", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "bittersweet"_s,
                     "bright",
                     "chillout",
                     "dreamy",
                     "fuzzy",
                     "nostalgic",
                     "smooth",
                     "strings-like",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Scattered World", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "full-spectrum",
                     "lush",
                     "multi-pitched",
                     "nostalgic",
                     "sci-fi",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Rumble Hiss", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "noise",
                     "non-pitched",
                     "resonant",
                     "rumbly",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Division", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "choir-like",
                     "ethereal",
                     "pad",
                     "peaceful",
                     "pure",
                     "smooth",
                     "synthesized",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Tickseed Ambience", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "glassy",
                     "pad",
                     "resonant",
                     "saturated",
                     "strings-like",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Drifter", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "cinematic",
                     "dark",
                     "disturbing",
                     "dreamy",
                     "eerie",
                     "menacing",
                     "muddy",
                     "mysterious",
                     "resonant",
                     "rumbly",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Alien Kerogen", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "dreamy",
                     "eerie",
                     "ethereal",
                     "hopeful",
                     "nostalgic",
                     "pad",
                     "smooth",
                     "synthesized",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Bass Arena", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "cinematic",
                     "cold",
                     "eerie",
                     "hypnotic",
                     "muddy",
                     "mysterious",
                     "rumbly",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Boreal", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bright",
                     "glassy",
                     "hopeful",
                     "pad",
                     "sci-fi",
                     "strings-like",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Heavenly Rumble", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dark",
                     "dystopian",
                     "eerie",
                     "ethereal",
                     "muddy",
                     "mysterious",
                     "nostalgic",
                     "rumbly",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Static", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "cold",
                     "noise",
                     "non-pitched",
                     "reedy",
                     "resonant",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Warmth Cycles", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dreamy",
                     "dystopian",
                     "eerie",
                     "metallic",
                     "muffled",
                     "nostalgic",
                     "pulsing",
                     "pure",
                     "sci-fi",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Electric Hiss", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "hissing",
                     "noise",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Hollow Noise", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "eerie",
                     "mysterious",
                     "noise",
                     "non-pitched",
                     "resonant",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Air - Restless Canopy", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "breathy",
                     "dreamy",
                     "ethereal",
                     "nostalgic",
                     "resonant",
                     "smooth",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Misty Nightfall", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "ethereal",
                     "full-spectrum",
                     "glassy",
                     "lush",
                     "metallic",
                     "multi-pitched",
                     "mysterious",
                     "organ-like",
                     "synthesized",
                     "texture",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Vocal Ahh", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "pad"_s,
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Dark Aurora", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "breathy"_s,
                     "cinematic",
                     "dark",
                     "disturbing",
                     "ethereal",
                     "muddy",
                     "resonant",
                     "rumbly",
                     "synthesized",
                     "tense",
                     "texture",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Noise - Atonal Void", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "breathy",
                     "eerie",
                     "ethereal",
                     "mysterious",
                     "noise",
                     "synthesized",
                     "thin",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Nectareous", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "choir-like",
                     "ethereal",
                     "muffled",
                     "pad",
                     "resonant",
                     "smooth",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Goldenrods", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "dreamy",
                     "muddy",
                     "multi-pitched",
                     "mysterious",
                     "resonant",
                     "synthesized",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - First Twilight", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "dreamy",
                     "ethereal",
                     "hopeful",
                     "organ-like",
                     "pad",
                     "pure",
                     "resonant",
                     "smooth",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Greek Moon", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient",
                     "choir-like",
                     "dreamy",
                     "ethereal",
                     "hopeful",
                     "pad",
                     "peaceful",
                     "pure",
                     "smooth",
                     "strings-like",
                     "synthesized",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Mid - Earthly Effigies", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "choir-like",
                     "cinematic",
                     "dystopian",
                     "eerie",
                     "ethereal",
                     "hypnotic",
                     "muddy",
                     "muffled",
                     "resonant",
                     "sci-fi",
                     "smooth",
                     "synthesized",
                     "tense",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - The Actuator", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "ambient"_s,
                     "bittersweet",
                     "cinematic",
                     "dark",
                     "dreamy",
                     "nostalgic",
                     "rumbly",
                     "smooth",
                     "synthesized",
                     "texture",
                     "warm",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }
        {
            auto& set = tags.FindOrInsertGrowIfNeeded(a, "Low - Ether Wraith", {}).element.data;
            CHECK(set.size == 0);
            for (auto const str : Array {
                     "airy"_s,
                     "ambient"_s,
                     "cinematic",
                     "cold",
                     "dark",
                     "disturbing",
                     "dystopian",
                     "eerie",
                     "melancholic",
                     "menacing",
                     "muddy",
                     "noisy",
                     "rumbly",
                     "synthesized",
                     "tense",
                     "texture",
                 }) {
                set.InsertGrowIfNeeded(a, str);
                check();
            }
        }

        if (auto i = tags.Find("Vocal Ahh"))
            for (auto const [tag, _] : *i)
                CHECK(tag == "pad"_s || tag == "synthesized"_s);
    }

    return k_success;
}

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

int TestValue(int) { return 10; }
AllocedString TestValue(AllocedString) { return "abc"_s; }

template <typename Type>
TEST_CASE(TestOptional) {

    SUBCASE("Empty") {
        Optional<Type> const o {};
        REQUIRE(!o.HasValue());
        REQUIRE(!o);
    }

    SUBCASE("Value") {
        Optional<Type> o {TestValue(Type())};
        REQUIRE(o.HasValue());
        REQUIRE(o);
        REQUIRE(o.Value() == TestValue(Type()));
        REQUIRE(*o == TestValue(Type()));

        SUBCASE("copy construct") {
            Optional<Type> other {o};
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("copy assign") {
            Optional<Type> other {};
            other = o;
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("move construct") {
            Optional<Type> other {Move(o)};
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("move assign") {
            Optional<Type> other {};
            other = Move(o);
            REQUIRE(other.HasValue());
            REQUIRE(other.Value() == TestValue(Type()));
        }

        SUBCASE("arrow operator") {
            if constexpr (Same<Type, String>) REQUIRE(o->Size() != 0);
        }
    }
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

TEST_CASE(TestFormatStringReplace) {
    auto& a = tester.scratch_arena;
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA__ bar __BBB__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                          {"__BBB__", "bar"},
                                      })),
             "test foo bar bar"_s);
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA____AAA__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                      })),
             "test foofoo"_s);
    CHECK_EQ(fmt::FormatStringReplace(a, "abc", {}), "abc"_s);
    return k_success;
}

TEST_CASE(TestIntToString) {
    auto to_string = [](int value, fmt::IntToStringOptions options) {
        DynamicArrayBounded<char, 32> result;
        auto size = IntToString(value, result.data, options);
        result.ResizeWithoutCtorDtor(size);
        return result;
    };

    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Decimal}) == "10"_s);
    CHECK(to_string(-99, {.base = fmt::IntToStringOptions::Base::Decimal}) == "-99"_s);
    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "a");
    CHECK(to_string(255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "ff");
    CHECK(to_string(0xfedcba, {.base = fmt::IntToStringOptions::Base::Hexadecimal, .capitalize = true}) ==
          "FEDCBA");
    CHECK(to_string(-255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "-ff");
    return k_success;
}

TEST_CASE(TestFormat) {
    auto& a = tester.scratch_arena;

    SUBCASE("basics") {
        DynamicArrayBounded<char, 256> buf;
        fmt::Assign(buf, "text {}, end", 100);
        CHECK_EQ(buf, "text 100, end"_s);
    }

    SUBCASE("basics") {
        CHECK_EQ(fmt::Format(a, "foo {} bar", 1), "foo 1 bar"_s);
        CHECK_EQ(fmt::Format(a, "{} {} {} {}", 1, 2, 3, 99999), "1 2 3 99999"_s);
        CHECK_EQ(fmt::Format(a, "{} :: {}", "key"_s, 100), "key :: 100"_s);
        CHECK_EQ(fmt::Format(a, "{}", "yeehar"), "yeehar"_s);
        CHECK_EQ(fmt::Format(a, "empty format"), "empty format"_s);
        CHECK_NEQ(fmt::Format(a, "ptr: {}", (void const*)""), ""_s);
    }

    SUBCASE("formats") {
        CHECK_NEQ(fmt::Format(a, "auto f32: {g}", 2.0), ""_s);
        CHECK_EQ(fmt::Format(a, "{x}", 255), "ff"_s);
        CHECK_EQ(fmt::Format(a, "{.2}", 0.2), "0.20"_s);
        CHECK_EQ(fmt::Format(a, "{.1}", 0.8187f), "0.8"_s);
    }

    SUBCASE("width") {
        SUBCASE("pad with spaces") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{1}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{2}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{3}", 10), " 10"_s);
            CHECK_EQ(fmt::Format(a, "{4}", 10), "  10"_s);
            CHECK_EQ(fmt::Format(a, "{4x}", 255), "  ff"_s);
        }

        SUBCASE("pad with zeros") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{01}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{02}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{03}", 10), "010"_s);
            CHECK_EQ(fmt::Format(a, "{04}", 10), "0010"_s);
            CHECK_EQ(fmt::Format(a, "{04x}", 255), "00ff"_s);
            CHECK_EQ(fmt::Format(a, "{07.2}", 3.1111), "0003.11"_s);
        }
    }

    SUBCASE("errors") {
        CHECK_PANICS(fmt::Format(a, "{} {} {} {}", 1));
        CHECK_PANICS(fmt::Format(a, "{}", 1, 1, 1, 1));
        CHECK_PANICS(fmt::Format(a, "{sefsefsef}", 1));
        CHECK_PANICS(fmt::Format(a, "{{}", 1));
        CHECK_PANICS(fmt::Format(a, " {{} ", 1));
        CHECK_PANICS(fmt::Format(a, "{}}", 1));
        CHECK_PANICS(fmt::Format(a, " {}} ", 1));
    }

    SUBCASE("brace literals") {
        CHECK_EQ(fmt::Format(a, "{{}}"), "{}"_s);
        CHECK_EQ(fmt::Format(a, "{{}} {}", 10), "{} 10"_s);
        CHECK_EQ(fmt::Format(a, "{} {{}}", 10), "10 {}"_s);
        CHECK_EQ(fmt::Format(a, "{} {{fff}}", 10), "10 {fff}"_s);
    }

    SUBCASE("strings") {
        CHECK_EQ(fmt::Format(a, "{}", ""), ""_s);
        CHECK_EQ(fmt::Format(a, "{}", "string literal"), "string literal"_s);
        CHECK_EQ(fmt::Format(a, "{}", (char const*)"const char pointer"), "const char pointer"_s);
    }

    SUBCASE("Error") {
        ErrorCodeCategory const category {
            .category_id = "test",
            .message = [](Writer const& writer, ErrorCode error) -> ErrorCodeOr<void> {
                TRY(writer.WriteChars("error code: "));
                TRY(writer.WriteChars(
                    fmt::IntToString(error.code, {.base = fmt::IntToStringOptions::Base::Decimal})));
                return k_success;
            },
        };
        ErrorCode const err {category, 100};
        CHECK_NEQ(fmt::Format(a, "{}", err), ""_s);
        CHECK_NEQ(fmt::Format(a, "{u}", err), ""_s);
    }

    SUBCASE("Dump struct") {
        struct TestStruct {
            int a;
            int b;
            char const* c;
        };
        TestStruct const test {1, 2, "three"};
        tester.log.Debug("struct1 is: {}", fmt::DumpStruct(test));

        auto const arr = Array {
            TestStruct {1, 2, "three"},
            TestStruct {4, 5, "six"},
        };
        tester.log.Debug("struct2 is: {}", fmt::DumpStruct(arr));

        struct OtherStruct {
            int a;
            int b;
            char const* c;
            TestStruct d;
            TestStruct e;
        };
        OtherStruct const other {1, 2, "three", {4, 5, "six"}, {7, 8, "nine"}};
        tester.log.Debug("struct3 is: {}", fmt::DumpStruct(other));

        tester.log.Debug("struct4 is: {}", fmt::DumpStruct(tester));
    }

    SUBCASE("DateAndTime") {
        DateAndTime date {
            .year = 2021,
            .months_since_jan = 1,
            .day_of_month = 1,
            .hour = 12,
            .minute = 30,
            .second = 45,
            .millisecond = 123,
        };
        CHECK_EQ(fmt::Format(a, "{}", date), "2021-02-01 12:30:45.123"_s);
        CHECK_EQ(fmt::Format(a, "{t}", date), "2021-02-01T12:30:45.123Z"_s);
    }

    SUBCASE("Join") {
        CHECK_EQ(fmt::Join(a, {}, ""), ""_s);
        CHECK_EQ(fmt::Join(a, {}, ","), ""_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s}, ""), "a"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s}, ""), "ab"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s, "c"_s}, ""), "abc"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s, "c"_s}, ","), "a,b,c"_s);

        CHECK_EQ(fmt::JoinInline<8>({}, ""), ""_s);
        CHECK_EQ(fmt::JoinInline<8>({}, ","), ""_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s}, ""), "a"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s}, ""), "ab"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s, "c"_s}, ""), "abc"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s, "c"_s}, ","), "a,b,c"_s);
    }

    SUBCASE("PrettyFileSize") {
        CHECK_EQ(fmt::PrettyFileSize(0), "0 B"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024), "1 kB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1524), "1 kB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024 * 1024), "1 MB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024 * 1024 * 1024), "1.00 GB"_s);
        CHECK_EQ(fmt::PrettyFileSize((1024 * 1024 * 1024) + (1024 * 1024 * 100)), "1.10 GB"_s);
    }

    return k_success;
}

TEST_CASE(TestRect) {
    SUBCASE("MakeRectThatEnclosesRects") {
        auto const r1 = Rect {.xywh {0, 5, 50, 50}};
        auto const r2 = Rect {.xywh {5, 0, 100, 25}};
        auto const enclosing = Rect::MakeRectThatEnclosesRects(r1, r2);
        REQUIRE(enclosing.x == 0);
        REQUIRE(enclosing.y == 0);
        REQUIRE(enclosing.w == 105);
        REQUIRE(enclosing.h == 55);
    }
    return k_success;
}

TEST_CASE(TestTrigLookupTable) {
    REQUIRE(trig_table_lookup::Sin(-k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Sin(-k_pi<> / 2) == -1);
    REQUIRE(trig_table_lookup::Sin(0) == 0);
    REQUIRE(trig_table_lookup::Sin(k_pi<> / 2) == 1);
    REQUIRE(trig_table_lookup::Sin(k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Sin(k_pi<> * (3.0f / 2.0f)) == -1);
    REQUIRE(trig_table_lookup::Sin(k_pi<> * 2) == 0);

    REQUIRE(trig_table_lookup::Cos(-k_pi<>) == -1);
    REQUIRE(trig_table_lookup::Cos(-k_pi<> / 2) == 0);
    REQUIRE(trig_table_lookup::Cos(0) == 1);
    REQUIRE(trig_table_lookup::Cos(k_pi<> / 2) == 0);
    REQUIRE(trig_table_lookup::Cos(k_pi<>) == -1);
    REQUIRE(trig_table_lookup::Cos(k_pi<> * (3.0f / 2.0f)) == 0);
    REQUIRE(trig_table_lookup::Cos(k_pi<> * 2) == 1);

    REQUIRE(trig_table_lookup::Tan(0) == 0);
    REQUIRE(trig_table_lookup::Tan(k_pi<>) == 0);
    REQUIRE(trig_table_lookup::Tan(-k_pi<>) == 0);

    f32 phase = -600;
    for (auto _ : Range(100)) {
        constexpr f32 k_arbitrary_value = 42.3432798f;
        REQUIRE(ApproxEqual(trig_table_lookup::Sin(phase), Sin(phase), 0.01f));
        REQUIRE(ApproxEqual(trig_table_lookup::Cos(phase), Cos(phase), 0.01f));
        REQUIRE(ApproxEqual(trig_table_lookup::Tan(phase), Tan(phase), 0.01f));
        phase += k_arbitrary_value;
    }
    return k_success;
}

TEST_CASE(TestMathsTrigTurns) {
    REQUIRE(trig_table_lookup::SinTurnsPositive(0) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(0.75f) == -1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(1) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(2) == 0);
    REQUIRE(trig_table_lookup::SinTurnsPositive(1.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurnsPositive(100.25f) == 1);

    REQUIRE(trig_table_lookup::SinTurns(0) == 0);
    REQUIRE(trig_table_lookup::SinTurns(0.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurns(0.75f) == -1);
    REQUIRE(trig_table_lookup::SinTurns(1) == 0);
    REQUIRE(trig_table_lookup::SinTurns(2) == 0);
    REQUIRE(trig_table_lookup::SinTurns(1.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(100.25f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(-0.25f) == -1);
    REQUIRE(trig_table_lookup::SinTurns(-0.5f) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-0.75f) == 1);
    REQUIRE(trig_table_lookup::SinTurns(-1) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-2) == 0);
    REQUIRE(trig_table_lookup::SinTurns(-200.25) == -1);

    REQUIRE(trig_table_lookup::CosTurns(-0.5f) == -1);
    REQUIRE(trig_table_lookup::CosTurns(-0.5f / 2) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0) == 1);
    REQUIRE(trig_table_lookup::CosTurns(0.5f / 2) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0.5f) == -1);
    REQUIRE(trig_table_lookup::CosTurns(0.5f * (3.0f / 2.0f)) == 0);
    REQUIRE(trig_table_lookup::CosTurns(0.5f * 2) == 1);

    REQUIRE(trig_table_lookup::TanTurns(0) == 0);
    REQUIRE(trig_table_lookup::TanTurns(0.5f) == 0);
    REQUIRE(trig_table_lookup::TanTurns(-0.5f) == 0);
    return k_success;
}

TEST_CASE(TestPath) {
    auto& scratch_arena = tester.scratch_arena;

    using namespace path;
    SUBCASE("Trim") {
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo/"_s, Format::Posix), "foo"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo////\\\\"_s, Format::Windows), "foo"_s);

        SUBCASE("windows") {
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo////"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\unc\\share\\foo\\bar\\", Format::Windows),
                     "\\\\unc\\share\\foo\\bar"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\unc\\share\\", Format::Windows), "\\\\unc\\share"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo/"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:////"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:////"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\C:\\"_s, Format::Windows), "\\\\?\\C:\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Windows), ""_s);
        }

        SUBCASE("posix") {
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo////"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo/"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("////"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        }
    }

    SUBCASE("Join") {
        DynamicArrayBounded<char, 128> s;
        s = "foo"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = ""_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "bar"_s);

        s = "foo"_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = "foo"_s;
        JoinAppend(s, "/"_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = ""_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, ""_s);

        s = "C:/"_s;
        JoinAppend(s, "foo"_s, Format::Windows);
        CHECK_EQ(s, "C:/foo"_s);

        s = "/"_s;
        JoinAppend(s, "foo"_s, Format::Posix);
        CHECK_EQ(s, "/foo"_s);

        {
            auto result = Join(scratch_arena, Array {"foo"_s, "bar"_s, "baz"_s}, Format::Posix);
            CHECK_EQ(result, "foo/bar/baz"_s);
        }
    }

    SUBCASE("Utils") {
        CHECK_EQ(Filename("foo"), "foo"_s);
        CHECK_EQ(Extension("/file.txt"_s), ".txt"_s);
        CHECK(IsAbsolute("/file.txt"_s, Format::Posix));
        CHECK(IsAbsolute("C:/file.txt"_s, Format::Windows));
        CHECK(IsAbsolute("C:\\file.txt"_s, Format::Windows));
        CHECK(IsAbsolute("\\\\server\\share"_s, Format::Windows));
        CHECK(!IsAbsolute("C:"_s, Format::Windows));
        CHECK(!IsAbsolute(""_s, Format::Windows));
    }

    // This SUBCASE is based on Zig's code
    // https://github.com/ziglang/zig
    // Copyright (c) Zig contributors
    // SPDX-License-Identifier: MIT
    SUBCASE("Directory") {
        CHECK_EQ(Directory("/a/b/c", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a/b/c///", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a", Format::Posix), "/"_s);
        CHECK(!Directory("/", Format::Posix).HasValue());
        CHECK(!Directory("//", Format::Posix).HasValue());
        CHECK(!Directory("///", Format::Posix).HasValue());
        CHECK(!Directory("////", Format::Posix).HasValue());
        CHECK(!Directory("", Format::Posix).HasValue());
        CHECK(!Directory("a", Format::Posix).HasValue());
        CHECK(!Directory("a/", Format::Posix).HasValue());
        CHECK(!Directory("a//", Format::Posix).HasValue());

        CHECK(!Directory("c:\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:\\foo", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\bar", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\baz", Format::Windows), "c:\\foo\\bar"_s);
        CHECK(!Directory("\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\foo", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\bar", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\baz", Format::Windows), "\\foo\\bar"_s);
        CHECK(!Directory("c:", Format::Windows).HasValue());
        CHECK(!Directory("c:foo", Format::Windows).HasValue());
        CHECK(!Directory("c:foo\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:foo\\bar", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\baz", Format::Windows), "c:foo\\bar"_s);
        CHECK(!Directory("file:stream", Format::Windows).HasValue());
        CHECK_EQ(Directory("dir\\file:stream", Format::Windows), "dir"_s);
        CHECK(!Directory("\\\\unc\\share", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\\\unc\\share\\foo", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\baz", Format::Windows), "\\\\unc\\share\\foo\\bar"_s);
        CHECK_EQ(Directory("/a/b/", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a/b", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a", Format::Windows), "/"_s);
        CHECK(!Directory("", Format::Windows).HasValue());
        CHECK(!Directory("/", Format::Windows).HasValue());
        CHECK(!Directory("////", Format::Windows).HasValue());
        CHECK(!Directory("foo", Format::Windows).HasValue());
    }

    SUBCASE("IsWithinDirectory") {
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo"));
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo/bar"));
        CHECK(IsWithinDirectory("foo/bar/baz", "foo"));
        CHECK(!IsWithinDirectory("/foo", "/foo"));
        CHECK(!IsWithinDirectory("/foo/bar/baz", "/bar"));
        CHECK(!IsWithinDirectory("/foobar/baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/o"));
    }

    SUBCASE("Windows Parse") {
        {
            auto const p = ParseWindowsPath("C:/foo/bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "C:"_s);
        }
        {
            auto const p = ParseWindowsPath("//a/b");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "//a/b"_s);
        }
        {
            auto const p = ParseWindowsPath("c:../");
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, "c:"_s);
        }
        {
            auto const p = ParseWindowsPath({});
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, ""_s);
        }
        {
            auto const p = ParseWindowsPath("D:\\foo\\bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "D:"_s);
        }
        {
            auto const p = ParseWindowsPath("\\\\LOCALHOST\\c$\\temp\\test-file.txt");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "\\\\LOCALHOST\\c$"_s);
        }
    }

    SUBCASE("MakeSafeForFilename") {
        CHECK_EQ(MakeSafeForFilename("foo", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo/bar", scratch_arena), "foo bar"_s);
        CHECK_EQ(MakeSafeForFilename("foo/bar/baz", scratch_arena), "foo bar baz"_s);
        CHECK_EQ(MakeSafeForFilename("", scratch_arena), ""_s);
        CHECK_EQ(MakeSafeForFilename("\"\"\"", scratch_arena), ""_s);
        CHECK_EQ(MakeSafeForFilename("foo  ", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo  \"", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo: <bar>|<baz>", scratch_arena), "foo bar baz"_s);
    }

    SUBCASE("CompactPath") {
        SUBCASE("compact only") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = false,
                .compact_middle_sections = true,
            };
            SUBCASE("Linux style") {
                CHECK_EQ(MakeDisplayPath("/a/b/c", options, scratch_arena, Format::Posix), "/a/b/c"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d", options, scratch_arena, Format::Posix), "/a/b/c/d"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                         "/a/b/…/d/e"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f", options, scratch_arena, Format::Posix),
                         "/a/b/…/e/f"_s);
                CHECK_EQ(MakeDisplayPath("/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "/home/user/…/src/main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f/g/h/i", options, scratch_arena, Format::Posix),
                         "/a/b/…/h/i"_s);
                CHECK_EQ(MakeDisplayPath("/Volumes/My Drive", options, scratch_arena, Format::Posix),
                         "/Volumes/My Drive"_s);
                CHECK_EQ(MakeDisplayPath("/Volumes/My Drive/Folder/Subfolder/Final",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "/Volumes/My Drive/…/Subfolder/Final"_s);
            }
            SUBCASE("Windows style") {
                CHECK_EQ(MakeDisplayPath("C:/a/b/c", options, scratch_arena, Format::Windows), "C:/a/b/c"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d", options, scratch_arena, Format::Windows),
                         "C:/a/b/c/d"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/d/e"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e/f", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/e/f"_s);
                CHECK_EQ(MakeDisplayPath("C:/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "C:/home/user/…/src/main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e/f/g/h/i", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/h/i"_s);
                CHECK_EQ(MakeDisplayPath("D:\\My Documents\\Projects\\App\\src\\main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "D:\\My Documents\\Projects\\…\\src\\main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("\\\\unc\\share\\foo\\bar\\baz\\blah\\foo",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "\\\\unc\\share\\foo\\bar\\…\\blah\\foo"_s);
            }
        }
        SUBCASE("compact and stylize") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = true,
                .compact_middle_sections = true,
            };
            CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                     "a › b › … › d › e"_s);
            CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f", options, scratch_arena, Format::Posix),
                     "a › b › … › e › f"_s);
            CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                     "C: › a › b › … › d › e"_s);
            CHECK_EQ(MakeDisplayPath("\\\\unc\\share\\foo\\bar\\baz\\blah\\foo",
                                     options,
                                     scratch_arena,
                                     Format::Windows),
                     "\\\\unc\\share › foo › bar › … › blah › foo"_s);
        }
        SUBCASE("stylize only") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = true,
                .compact_middle_sections = false,
            };
            SUBCASE("Linux style") {
                CHECK_EQ(MakeDisplayPath("/a/b/c", options, scratch_arena, Format::Posix), "a › b › c"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d", options, scratch_arena, Format::Posix),
                         "a › b › c › d"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                         "a › b › c › d › e"_s);
                CHECK_EQ(MakeDisplayPath("/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "home › user › docs › projects › app › src › main.cpp"_s);
            }
            SUBCASE("Windows style") {
                CHECK_EQ(MakeDisplayPath("C:/a/b/c", options, scratch_arena, Format::Windows),
                         "C: › a › b › c"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d", options, scratch_arena, Format::Windows),
                         "C: › a › b › c › d"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                         "C: › a › b › c › d › e"_s);
            }
        }
    }

    return k_success;
}

constexpr int k_num_rand_test_repititions = 200;

TEST_CASE(TestRandomIntGeneratorUnsigned) {
    SUBCASE("unsigned") {
        RandomIntGenerator<unsigned int> generator;
        auto seed = (u64)NanosecondsSinceEpoch();

        SUBCASE("Correct generation in range 0 to 3 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("signed") {
        RandomIntGenerator<int> generator;
        auto seed = (u64)NanosecondsSinceEpoch();

        SUBCASE("Correct generation in range -10 to 10 with repeating last value allowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range -10 to 10 with repeating last value disallowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, true);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("move object") {
        RandomIntGenerator<int> generator;
        auto seed = (u64)NanosecondsSinceEpoch();

        constexpr int k_max_val = 10;
        {
            auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator2 = generator;
        {
            auto const random_num = generator2.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator3 = Move(generator);
        {
            auto const random_num = generator3.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }
    }
    return k_success;
}

template <typename T>
TEST_CASE(TestRandomFloatGenerator) {
    RandomFloatGenerator<T> generator;
    auto seed = (u64)NanosecondsSinceEpoch();

    SUBCASE("random values are in a correct range") {
        auto test = [&](bool allow_repititions) {
            constexpr T k_max_val = 100;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num =
                    generator.GetRandomInRange(seed, -k_max_val, k_max_val, allow_repititions);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        };
        test(true);
        test(false);
    }
    return k_success;
}

TEST_CASE(TestVersion) {
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {1, 0, 0}) == "1.0.0"_s);
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {10, 99, 98}) == "10.99.98"_s);

    CHECK(Version {1, 0, 0} == Version {1, 0, 0});
    CHECK(Version {1, 1, 0} > Version {1, 0, 0});
    CHECK(Version {1, 0, 0} < Version {1, 1, 0});
    CHECK(Version {0, 0, 0} < Version {1, 0, 0});
    CHECK(Version {1, 0, 100} < Version {2, 4, 10});
    CHECK(Version {0, 0, 100} < Version {0, 0, 101});

    auto const check_string_parsing = [&](String str, Version ver) {
        CAPTURE(str);
        auto const parsed_ver = ParseVersionString(str);
        REQUIRE(parsed_ver.HasValue());
        CHECK(ver == *parsed_ver);
    };

    CHECK(!ParseVersionString("1"));
    CHECK(!ParseVersionString("1.2"));
    CHECK(!ParseVersionString("hello"));
    CHECK(!ParseVersionString(",,what"));
    CHECK(!ParseVersionString("1,1,2"));
    CHECK(!ParseVersionString("1a,1,2bv"));
    CHECK(!ParseVersionString("200a.200.400a"));
    CHECK(!ParseVersionString("."));
    CHECK(!ParseVersionString(".."));
    CHECK(!ParseVersionString("..."));
    CHECK(!ParseVersionString("...."));
    CHECK(!ParseVersionString(".1.2"));
    CHECK(!ParseVersionString("12.."));
    CHECK(!ParseVersionString(".1."));
    CHECK(!ParseVersionString(""));
    CHECK(!ParseVersionString(" 200   .  4.99 "));

    check_string_parsing("1.1.1", {1, 1, 1});
    check_string_parsing("0.0.0", {0, 0, 0});
    check_string_parsing("1.0.99", {1, 0, 99});
    check_string_parsing("1.0.0-alpha.1", {1, 0, 0});
    check_string_parsing("1.0.0-alpha+abcdef", {1, 0, 0});
    check_string_parsing("1.0.0-alpha+2.2.0", {1, 0, 0});

    {
        u32 prev_version = 0;
        u16 maj = 0;
        u8 min = 0;
        u8 pat = 0;
        for (auto _ : Range(256)) {
            ++pat;
            if (pat > 20) {
                pat = 0;
                ++min;
                if (min > 20) ++maj;
            }

            auto const version = PackVersionIntoU32(maj, min, pat);
            CHECK(version > prev_version);
            prev_version = version;
        }
    }

    CHECK(PackVersionIntoU32(1, 1, 2) < PackVersionIntoU32(1, 2, 0));
    return k_success;
}

TEST_CASE(TestMemoryUtils) {
    CHECK(BytesToAddForAlignment(10, 1) == 0);
    CHECK(BytesToAddForAlignment(9, 1) == 0);
    CHECK(BytesToAddForAlignment(3333333, 1) == 0);
    CHECK(BytesToAddForAlignment(0, 2) == 0);
    CHECK(BytesToAddForAlignment(1, 2) == 1);
    CHECK(BytesToAddForAlignment(2, 2) == 0);
    CHECK(BytesToAddForAlignment(1, 4) == 3);
    CHECK(BytesToAddForAlignment(2, 4) == 2);
    CHECK(BytesToAddForAlignment(3, 4) == 1);
    CHECK(BytesToAddForAlignment(4, 4) == 0);
    CHECK(BytesToAddForAlignment(31, 32) == 1);

    // test NumBitsNeededToStore
    CHECK_EQ(NumBitsNeededToStore(0), 1uz);
    CHECK_EQ(NumBitsNeededToStore(1), 1uz);
    CHECK_EQ(NumBitsNeededToStore(2), 2uz);
    CHECK_EQ(NumBitsNeededToStore(3), 2uz);
    CHECK_EQ(NumBitsNeededToStore(4), 3uz);
    CHECK_EQ(NumBitsNeededToStore(5), 3uz);
    CHECK_EQ(NumBitsNeededToStore(6), 3uz);
    CHECK_EQ(NumBitsNeededToStore(7), 3uz);
    CHECK_EQ(NumBitsNeededToStore(8), 4uz);

    return k_success;
}

TEST_CASE(TestAsciiToUppercase) {
    CHECK(ToUppercaseAscii('a') == 'A');
    CHECK(ToUppercaseAscii('z') == 'Z');
    CHECK(ToUppercaseAscii('A') == 'A');
    CHECK(ToUppercaseAscii('M') == 'M');
    CHECK(ToUppercaseAscii('0') == '0');
    CHECK(ToUppercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToUppercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestAsciiToLowercase) {
    CHECK(ToLowercaseAscii('A') == 'a');
    CHECK(ToLowercaseAscii('Z') == 'z');
    CHECK(ToLowercaseAscii('a') == 'a');
    CHECK(ToLowercaseAscii('m') == 'm');
    CHECK(ToLowercaseAscii('0') == '0');
    CHECK(ToLowercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToLowercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestNullTermStringsEqual) {
    CHECK(NullTermStringsEqual("", ""));
    CHECK(!NullTermStringsEqual("a", ""));
    CHECK(!NullTermStringsEqual("", "a"));
    CHECK(!NullTermStringsEqual("aaa", "a"));
    CHECK(!NullTermStringsEqual("a", "aaa"));
    CHECK(NullTermStringsEqual("aaa", "aaa"));
    return k_success;
}

TEST_CASE(TestSplitWithIterator) {
    auto check = [&](String whole, char token, Span<String> expected_parts, bool skip_consecutive) {
        CAPTURE(whole);
        CAPTURE(expected_parts);
        CAPTURE(skip_consecutive);

        {
            usize cursor {0uz};
            usize index = 0;
            while (auto part = SplitWithIterator(whole, cursor, token, skip_consecutive))
                CHECK_EQ(*part, expected_parts[index++]);
            CHECK_EQ(index, expected_parts.size);
        }

        {
            usize index = 0;
            for (auto const part :
                 SplitIterator {.whole = whole, .token = token, .skip_consecutive = skip_consecutive}) {
                CHECK_EQ(part, expected_parts[index++]);
            }
            CHECK_EQ(index, expected_parts.size);
        }
    };

    check("aa\nbb", '\n', Array {"aa"_s, "bb"}, false);
    check("aa", '\n', Array {"aa"_s}, false);
    check("aa\n\nbb", '\n', Array {"aa"_s, "", "bb"}, false);
    check("\n\nbb", '\n', Array {""_s, "", "bb"}, false);
    check("aa\n\n", '\n', Array {"aa"_s, ""}, false);
    check("\n\n", '\n', Array {""_s, ""}, false);

    check("aa\nbb", '\n', Array {"aa"_s, "bb"}, true);
    check("aa", '\n', Array {"aa"_s}, true);
    check("aa\n\nbb", '\n', Array {"aa"_s, "bb"}, true);
    check("\n\nbb", '\n', Array {"bb"_s}, true);
    check("aa\n\n", '\n', Array {"aa"_s}, true);
    check("\n\n", '\n', {}, true);

    return k_success;
}

TEST_CASE(TestSplit) {
    auto check = [&](String whole, char token, Span<String> expected_parts) {
        CAPTURE(whole);
        CAPTURE(expected_parts);

        auto split = Split(whole, token, tester.scratch_arena);
        REQUIRE(split.size == expected_parts.size);
        for (auto const i : Range(expected_parts.size))
            CHECK(split[i] == expected_parts[i]);
    };
    check("aa\nbb", '\n', Array<String, 2> {"aa", "bb"});
    check("aa", '\n', Array<String, 1> {"aa"});
    return k_success;
}

TEST_CASE(TestParseFloat) {
    CHECK(!ParseFloat(""));
    CHECK(!ParseFloat("string"));

    usize num_chars_read = 0;
    CHECK_APPROX_EQ(ParseFloat("0", &num_chars_read).Value(), 0.0, 0.0001);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_APPROX_EQ(ParseFloat("10", &num_chars_read).Value(), 10.0, 0.0001);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_APPROX_EQ(ParseFloat("-10", &num_chars_read).Value(), -10.0, 0.0001);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_APPROX_EQ(ParseFloat("238942349.230", &num_chars_read).Value(), 238942349.230, 0.0001);
    CHECK_EQ(num_chars_read, 13u);
    return k_success;
}

TEST_CASE(TestParseInt) {
    CHECK(!ParseInt("", ParseIntBase::Decimal));
    CHECK(!ParseInt("string", ParseIntBase::Decimal));
    CHECK(!ParseInt("  ", ParseIntBase::Decimal));

    usize num_chars_read = 0;
    CHECK_EQ(ParseInt("0", ParseIntBase::Decimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Decimal, &num_chars_read).Value(), 10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("-10", ParseIntBase::Decimal, &num_chars_read).Value(), -10);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_EQ(ParseInt("238942349", ParseIntBase::Decimal, &num_chars_read).Value(), 238942349);
    CHECK_EQ(num_chars_read, 9u);

    CHECK_EQ(ParseInt("0", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0x10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("deadc0de", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0xdeadc0de);
    CHECK_EQ(num_chars_read, 8u);

    return k_success;
}

TEST_CASE(TestNarrowWiden) {
    auto& a = tester.scratch_arena;
    // IMPROVE: check against Windows MultiByteToWideChar
    auto const utf8_str = FromNullTerminated((char const*)u8"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt");
    auto const wstr = L"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt"_s;

    SUBCASE("standard functions") {
        auto const converted_wstr = Widen(a, utf8_str);
        CHECK(converted_wstr.HasValue());
        CHECK(converted_wstr.Value() == wstr);
        auto const original_str = Narrow(a, converted_wstr.Value());
        CHECK(original_str.HasValue());
        CHECK(original_str.Value() == utf8_str);
    }

    SUBCASE("widen append") {
        DynamicArray<wchar_t> str {a};
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size);
        CHECK(str == wstr);
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size * 2);
    }

    SUBCASE("narrow append") {
        DynamicArray<char> str {a};
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size);
        CHECK(str == utf8_str);
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size * 2);
    }
    return k_success;
}

TEST_CASE(TestCopyStringIntoBuffer) {
    SUBCASE("char[N] overload") {
        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Size 1 buffer") {
            char buf[1];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Empty source") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }

    SUBCASE("Span<char> overload") {
        SUBCASE("Dest empty") { CopyStringIntoBufferWithNullTerm(nullptr, 0, "abc"); }

        SUBCASE("Source empty") {
            char buffer[6];
            CopyStringIntoBufferWithNullTerm(buffer, 6, "");
            CHECK(buffer[0] == 0);
        }

        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, 2, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }
    return k_success;
}

TEST_CASE(TestMatchWildcard) {
    CHECK(MatchWildcard("*foo*", "foobar"));
    CHECK(MatchWildcard(".*-file", ".text-file"));
    CHECK(MatchWildcard("floe_*.cpp", "floe_functions.cpp"));
    CHECK(MatchWildcard("mirtestãingããage_*.cpp", "mirtestãingããage_functions.cpp"));
    CHECK(MatchWildcard("*.floe*", "1.floe"));
    CHECK(MatchWildcard("*.floe*", "1.floe-wraith"));
    CHECK(MatchWildcard("*.floe*", "1.floe-none"));
    CHECK(!MatchWildcard("*.floe*", "foo.py"));
    return k_success;
}

TEST_CASE(TestStringAlgorithms) {
    SUBCASE("ContainsCaseInsensitiveAscii") {
        String const str = "abcde";
        CHECK(ContainsCaseInsensitiveAscii(str, "abcde"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "abcd"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "abcdef"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "bc"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "BC"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "cb"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "c"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "C"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, ""_s));
    }

    SUBCASE("Compare") {
        CHECK(CompareAscii("aaa"_s, "aaa"_s) == 0);
        CHECK_OP(CompareAscii("aaa"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii("za"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii(""_s, ""_s), ==, 0);
        CHECK_OP(CompareAscii("a"_s, ""_s), >, 0);
        CHECK_OP(CompareAscii(""_s, "a"_s), <, 0);

        CHECK(CompareCaseInsensitiveAscii("Aaa"_s, "aaa"_s) == 0);
        CHECK(CompareCaseInsensitiveAscii(""_s, ""_s) == 0);
    }

    SUBCASE("IsEqualToCaseInsensitveAscii") {
        CHECK(IsEqualToCaseInsensitiveAscii("aa"_s, "AA"_s));
        CHECK(IsEqualToCaseInsensitiveAscii(""_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aa"_s, "AAA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aaa"_s, "AA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("a"_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii(""_s, "1"_s));
    }

    SUBCASE("whitespace") {
        CHECK(CountWhitespaceAtStart("  a"_s) == 2);
        CHECK(CountWhitespaceAtStart("\t\n\r a"_s) == 4);
        CHECK(CountWhitespaceAtStart(" "_s) == 1);
        CHECK(CountWhitespaceAtStart("a "_s) == 0);
        CHECK(CountWhitespaceAtStart(""_s) == 0);

        CHECK(CountWhitespaceAtEnd("a  "_s) == 2);
        CHECK(CountWhitespaceAtEnd("a \t\n\r"_s) == 4);
        CHECK(CountWhitespaceAtEnd(" "_s) == 1);
        CHECK(CountWhitespaceAtEnd(" a"_s) == 0);
        CHECK(CountWhitespaceAtEnd(""_s) == 0);

        CHECK(WhitespaceStripped(" aa  "_s) == "aa");
        CHECK(WhitespaceStrippedStart(" aa  "_s) == "aa  ");
    }

    SUBCASE("FindUtf8TruncationPoint") {
        auto check = [&](String str, usize max_len, usize expected) {
            CAPTURE(str);
            CAPTURE(max_len);
            CAPTURE(expected);
            auto result = FindUtf8TruncationPoint(str, max_len);
            CHECK_EQ(result, expected);
            CHECK(IsValidUtf8(str.SubSpan(0, result)));
        };

        SUBCASE("ascii") {
            auto const str = "Hello World"_s;
            check(str, 5, 5);
            check(str, 10, 10);
        }

        SUBCASE("2-byte UTF-8 character") {
            auto const str = "café"_s;
            check(str, 4, 3);
            check(str, 3, 3);
        }

        SUBCASE("3-byte UTF-8 character") {
            // 0xE2 0x82 0xAC
            auto const str = "Cost: €"_s;

            check(str, 8, 6);
            check(str, 7, 6);
            check(str, 6, 6);
            check(str, 5, 5);
        }

        SUBCASE("4-byte UTF-8 character") {
            // "𐍈" (Gothic letter aiha) is 0xF0 0x90 0x8D 0x88 in UTF-8
            auto const str = "Symbol: \xF0\x90\x8D\x88"_s;

            check(str, 11, 8);
            check(str, 10, 8);
            check(str, 9, 8);
            check(str, 8, 8);
        }

        SUBCASE("Edge cases") {
            auto const str = "€"_s;
            CHECK_EQ(FindUtf8TruncationPoint(str, 1), 0u);
            CHECK_EQ(FindUtf8TruncationPoint(str, 2), 0u);
        }
    }

    return k_success;
}

struct ArenaAllocatorMalloc : ArenaAllocator {
    ArenaAllocatorMalloc() : ArenaAllocator(Malloc::Instance()) {}
};

struct ArenaAllocatorPage : ArenaAllocator {
    ArenaAllocatorPage() : ArenaAllocator(PageAllocator::Instance()) {}
};

struct ArenaAllocatorBigBuf : ArenaAllocator {
    ArenaAllocatorBigBuf() : ArenaAllocator(big_buf) {}
    FixedSizeAllocator<1000> big_buf {&Malloc::Instance()};
};

struct FixedSizeAllocatorTiny : FixedSizeAllocator<1> {
    FixedSizeAllocatorTiny() : FixedSizeAllocator(&Malloc::Instance()) {}
};
struct FixedSizeAllocatorSmall : FixedSizeAllocator<16> {
    FixedSizeAllocatorSmall() : FixedSizeAllocator(&Malloc::Instance()) {}
};
struct FixedSizeAllocatorLarge : FixedSizeAllocator<1000> {
    FixedSizeAllocatorLarge() : FixedSizeAllocator(&Malloc::Instance()) {}
};

template <typename AllocatorType>
TEST_CASE(TestAllocatorTypes) {
    AllocatorType a;

    SUBCASE("Pointers are unique when no existing data is passed in") {
        constexpr auto k_iterations = 1000;
        DynamicArrayBounded<Span<u8>, k_iterations> allocs;
        DynamicArrayBounded<void*, k_iterations> set;
        for (auto _ : Range(k_iterations)) {
            dyn::Append(allocs, a.Allocate({1, 1, true}));
            REQUIRE(Last(allocs).data != nullptr);
            dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
        }
        REQUIRE(set.size == k_iterations);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("all sizes and alignments are handled") {
        usize const sizes[] = {1, 2, 3, 99, 7000};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        auto const total_size = ArraySize(sizes) * ArraySize(alignments);
        DynamicArrayBounded<Span<u8>, total_size> allocs;
        DynamicArrayBounded<void*, total_size> set;
        for (auto s : sizes) {
            for (auto align : alignments) {
                dyn::Append(allocs, a.Allocate({s, align, true}));
                REQUIRE(Last(allocs).data != nullptr);
                dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
            }
        }
        REQUIRE(set.size == total_size);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("reallocating an existing block still contains the same data") {
        auto data = a.template AllocateBytesForTypeOversizeAllowed<int>();
        DEFER { a.Free(data); };
        int const test_value = 1234567;
        *(CheckedPointerCast<int*>(data.data)) = test_value;

        data = a.template Reallocate<int>(100, data, 1, false);
        REQUIRE(*(CheckedPointerCast<int*>(data.data)) == test_value);
    }

    SUBCASE("shrink") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        REQUIRE(data.size >= k_original_size);

        constexpr usize k_new_size = 10;
        auto shrunk_data = a.Resize({data, k_new_size});
        data = shrunk_data;
        REQUIRE(data.size == k_new_size);

        // do another allocation for good measure
        auto data2 = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data2); };
        REQUIRE(data2.size >= k_original_size);
        data2 = a.Resize({data2, k_new_size});
        REQUIRE(data2.size == k_new_size);
    }

    SUBCASE("clone") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        FillMemory(data, 'a');

        auto cloned_data = a.Clone(data);
        DEFER { a.Free(cloned_data); };
        REQUIRE(cloned_data.data != data.data);
        REQUIRE(cloned_data.size == data.size);
        for (auto const i : Range(k_original_size))
            REQUIRE(cloned_data[i] == 'a');
    }

    SUBCASE("a complex mix of allocations, reallocations and frees work") {
        usize const sizes[] = {1,  1, 1, 1, 1,   1,   1,   1,  1,    3,   40034,
                               64, 2, 2, 2, 500, 500, 500, 99, 1000, 100, 20};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        struct Allocation {
            usize size;
            usize align;
            Span<u8> data {};
        };
        Allocation allocs[ArraySize(sizes)];
        usize align_index = 0;
        for (auto const i : Range(ArraySize(sizes))) {
            auto& alloc = allocs[i];
            alloc.size = sizes[i];
            alloc.align = alignments[align_index];
            align_index++;
            if (align_index == ArraySize(alignments)) align_index = 0;
        }

        auto seed = (u64)NanosecondsSinceEpoch();
        RandomIntGenerator<usize> rand_gen;
        usize index = 0;
        for (auto _ : Range(ArraySize(sizes) * 5)) {
            switch (rand_gen.GetRandomInRange(seed, 0, 5)) {
                case 0:
                case 1:
                case 2: {
                    auto const new_size = allocs[index].size;
                    auto const new_align = allocs[index].align;
                    auto const existing_data = allocs[index].data;
                    if (existing_data.size && new_size > existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                            .allow_oversize_result = true,
                        });
                    } else if (new_size < existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                        });
                    } else if (!existing_data.size) {
                        allocs[index].data = a.Allocate({
                            .size = new_size,
                            .alignment = new_align,
                            .allow_oversized_result = true,
                        });
                    }
                    break;
                }
                case 3:
                case 4: {
                    if (allocs[index].data.data) {
                        a.Free(allocs[index].data);
                        allocs[index].data = {};
                    }
                    break;
                }
                case 5: {
                    if (allocs[index].data.data) {
                        auto const new_size = allocs[index].data.size / 2;
                        if (new_size) {
                            allocs[index].data = a.Resize({
                                .allocation = allocs[index].data,
                                .new_size = new_size,
                            });
                        }
                    }
                }
            }
            index++;
            if (index == ArraySize(allocs)) index = 0;
        }

        for (auto& alloc : allocs)
            if (alloc.data.data) a.Free(alloc.data);
    }

    SUBCASE("speed benchmark") {
        constexpr usize k_alignment = 8;
        usize const sizes[] = {1,   16,  16,  16, 16,   32,  32, 32, 32, 32, 40034, 64, 128, 50, 239,
                               500, 500, 500, 99, 1000, 100, 20, 16, 16, 16, 64,    64, 64,  64, 64,
                               64,  64,  64,  64, 64,   64,  64, 64, 64, 64, 64,    64, 64};

        constexpr usize k_num_cycles = 10;
        Span<u8> allocations[ArraySize(sizes) * k_num_cycles];

        Stopwatch const stopwatch;

        for (auto const cycle : Range(k_num_cycles))
            for (auto const i : Range(ArraySize(sizes)))
                allocations[(cycle * ArraySize(sizes)) + i] = a.Allocate({sizes[i], k_alignment, true});

        if constexpr (!Same<ArenaAllocator, AllocatorType>)
            for (auto& alloc : allocations)
                a.Free(alloc);

        String type_name {};
        if constexpr (Same<AllocatorType, FixedSizeAllocatorTiny>)
            type_name = "FixedSizeAllocatorTiny";
        else if constexpr (Same<AllocatorType, FixedSizeAllocatorSmall>)
            type_name = "FixedSizeAllocatorSmall";
        else if constexpr (Same<AllocatorType, FixedSizeAllocatorLarge>)
            type_name = "FixedSizeAllocatorLarge";
        else if constexpr (Same<AllocatorType, Malloc>)
            type_name = "Malloc";
        else if constexpr (Same<AllocatorType, PageAllocator>)
            type_name = "PageAllocator";
        else if constexpr (Same<AllocatorType, ArenaAllocatorMalloc>)
            type_name = "ArenaAllocatorMalloc";
        else if constexpr (Same<AllocatorType, ArenaAllocatorPage>)
            type_name = "ArenaAllocatorPage";
        else if constexpr (Same<AllocatorType, ArenaAllocatorBigBuf>)
            type_name = "ArenaAllocatorBigBuf";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else
            PanicIfReached();

        tester.log.Debug("Speed benchmark: {} for {}", stopwatch, type_name);
    }
    return k_success;
}

TEST_CASE(TestArenaAllocatorCursor) {
    LeakDetectingAllocator leak_detecting_allocator;
    constexpr usize k_first_region_size = 64;
    ArenaAllocator arena {leak_detecting_allocator, k_first_region_size};
    CHECK(arena.first == arena.last);
    CHECK_OP(arena.first->BufferSize(), ==, k_first_region_size);

    auto const cursor1 = arena.TotalUsed();
    REQUIRE(cursor1 == 0);

    arena.NewMultiple<u8>(10);
    auto const cursor2 = arena.TotalUsed();
    CHECK_EQ(cursor2, (usize)10);
    CHECK(arena.first == arena.last);

    CHECK_EQ(arena.TryShrinkTotalUsed(cursor1), (usize)0);

    arena.NewMultiple<u8>(10);
    CHECK_EQ(arena.TotalUsed(), (usize)10);
    CHECK(arena.first == arena.last);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    CHECK(arena.first == arena.last);

    arena.AllocateExactSizeUninitialised<u8>(4000);
    CHECK(arena.first != arena.last);
    CHECK(arena.first->next == arena.last);
    CHECK(arena.last->prev == arena.first);
    CHECK_EQ(arena.TryShrinkTotalUsed(100), (usize)100);
    CHECK_EQ(arena.TotalUsed(), (usize)100);

    CHECK_EQ(arena.TryShrinkTotalUsed(4), k_first_region_size);
    CHECK_LTE(arena.TotalUsed(), k_first_region_size);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    return k_success;
}

TEST_CASE(TestBoundedList) {
    static_assert(Same<BoundedList<int, 255>::UnderlyingIndexType, u8>);
    static_assert(Same<BoundedList<int, 256>::UnderlyingIndexType, u16>);
    static_assert(Same<BoundedList<int, 65535>::UnderlyingIndexType, u32>);

    // used a malloced int to test that the bounded list correctly frees memory
    struct MallocedInt {
        NON_COPYABLE_AND_MOVEABLE(MallocedInt);
        MallocedInt(int i) {
            data = (int*)GpaAlloc(sizeof(int));
            *data = i;
        }
        ~MallocedInt() { GpaFree(data); }
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

enum class TestErrors {
    Error1,
    Error2,
};

constexpr ErrorCodeCategory k_test_error_code_category = {
    .category_id = "CM",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((TestErrors)code.code) {
            case TestErrors::Error1: str = "error 1"; break;
            case TestErrors::Error2: str = "error 2"; break;
        }
        return writer.WriteChars(str);
    }};

inline ErrorCodeCategory const& ErrorCategoryForEnum(TestErrors) { return k_test_error_code_category; }

TEST_CASE(TestErrorCode) {
    auto const e1 = ErrorCode {TestErrors::Error1};
    CHECK(e1.category == &k_test_error_code_category);
    CHECK(e1.code == (s64)TestErrors::Error1);
    CHECK(e1 == TestErrors::Error1);
    CHECK(e1 != TestErrors::Error2);
    CHECK(e1 == ErrorCode {TestErrors::Error1});

    auto const e2 = ErrorCode {TestErrors::Error2};
    CHECK(e1 != e2);

    return k_success;
}

static_assert(NextPowerOf2(0u) == 1u);
static_assert(NextPowerOf2(1u) == 1u);
static_assert(NextPowerOf2(2u) == 2u);
static_assert(NextPowerOf2(3u) == 4u);
static_assert(NextPowerOf2(4u) == 4u);
static_assert(NextPowerOf2(5u) == 8u);
static_assert(NextPowerOf2(6u) == 8u);
static_assert(NextPowerOf2(7u) == 8u);
static_assert(NextPowerOf2(8u) == 8u);
static_assert(NextPowerOf2(9u) == 16u);
static_assert(NextPowerOf2(15u) == 16u);
static_assert(NextPowerOf2(16u) == 16u);
static_assert(NextPowerOf2(17u) == 32u);

TEST_REGISTRATION(RegisterFoundationTests) {
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorBigBuf>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorMalloc>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorPage>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorLarge>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorSmall>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorTiny>);
    REGISTER_TEST(TestAllocatorTypes<LeakDetectingAllocator>);
    REGISTER_TEST(TestAllocatorTypes<Malloc>);
    REGISTER_TEST(TestAllocatorTypes<PageAllocator>);
    REGISTER_TEST(TestArenaAllocatorCursor);
    REGISTER_TEST(TestAsciiToLowercase);
    REGISTER_TEST(TestAsciiToUppercase);
    REGISTER_TEST(TestBinarySearch);
    REGISTER_TEST(TestBitset);
    REGISTER_TEST(TestBoundedList);
    REGISTER_TEST(TestCircularBuffer);
    REGISTER_TEST(TestCircularBufferRefType);
    REGISTER_TEST(TestCopyStringIntoBuffer);
    REGISTER_TEST(TestDynamicArrayBasics<AllocedString>);
    REGISTER_TEST(TestDynamicArrayBasics<Optional<AllocedString>>);
    REGISTER_TEST(TestDynamicArrayBasics<int>);
    REGISTER_TEST(TestDynamicArrayBoundedBasics);
    REGISTER_TEST(TestDynamicArrayChar);
    REGISTER_TEST(TestDynamicArrayClone);
    REGISTER_TEST(TestDynamicArrayString);
    REGISTER_TEST(TestErrorCode);
    REGISTER_TEST(TestFormat);
    REGISTER_TEST(TestFormatStringReplace);
    REGISTER_TEST(TestFunction);
    REGISTER_TEST(TestFunctionQueue);
    REGISTER_TEST(TestHashTable<HashTableOrdering::Unordered>);
    REGISTER_TEST(TestHashTable<HashTableOrdering::Ordered>);
    REGISTER_TEST(TestIntToString);
    REGISTER_TEST(TestLinkedList);
    REGISTER_TEST(TestMatchWildcard);
    REGISTER_TEST(TestMathsTrigTurns);
    REGISTER_TEST(TestMemoryUtils);
    REGISTER_TEST(TestNarrowWiden);
    REGISTER_TEST(TestNullTermStringsEqual);
    REGISTER_TEST(TestOptional<AllocedString>);
    REGISTER_TEST(TestOptional<int>);
    REGISTER_TEST(TestParseFloat);
    REGISTER_TEST(TestParseInt);
    REGISTER_TEST(TestPath);
    REGISTER_TEST(TestPathPool);
    REGISTER_TEST(TestRandomFloatGenerator<f32>);
    REGISTER_TEST(TestRandomFloatGenerator<f64>);
    REGISTER_TEST(TestRandomIntGeneratorUnsigned);
    REGISTER_TEST(TestRect);
    REGISTER_TEST(TestSort);
    REGISTER_TEST(TestSplit);
    REGISTER_TEST(TestSplitWithIterator);
    REGISTER_TEST(TestStringAlgorithms);
    REGISTER_TEST(TestStringSearching);
    REGISTER_TEST(TestTaggedUnion);
    REGISTER_TEST(TestTrigLookupTable);
    REGISTER_TEST(TestVersion);
    REGISTER_TEST(TestWriter);
}
