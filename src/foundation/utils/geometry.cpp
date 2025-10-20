// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

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

TEST_REGISTRATION(RegisterGeometryTests) { REGISTER_TEST(TestRect); }
