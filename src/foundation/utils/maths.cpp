// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

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

TEST_REGISTRATION(RegisterMathsTests) {
    REGISTER_TEST(TestTrigLookupTable);
    REGISTER_TEST(TestMathsTrigTurns);
}
