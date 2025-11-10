// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

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

TEST_REGISTRATION(RegisterFunctionTests) { REGISTER_TEST(TestFunction); }
