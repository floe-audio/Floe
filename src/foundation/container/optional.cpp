// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

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

static int TestValue(int) { return 10; }
static AllocedString TestValue(AllocedString) { return "abc"_s; }

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

TEST_REGISTRATION(RegisterOptionalTests) {
    REGISTER_TEST(TestOptional<AllocedString>);
    REGISTER_TEST(TestOptional<int>);
}
