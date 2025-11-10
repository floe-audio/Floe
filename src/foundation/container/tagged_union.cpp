// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

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

TEST_REGISTRATION(RegisterTaggedUnionTests) { REGISTER_TEST(TestTaggedUnion); }
