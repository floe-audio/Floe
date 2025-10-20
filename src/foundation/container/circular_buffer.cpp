// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

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

TEST_REGISTRATION(RegisterCircularBufferTests) {
    REGISTER_TEST(TestCircularBuffer);
    REGISTER_TEST(TestCircularBufferRefType);
}
