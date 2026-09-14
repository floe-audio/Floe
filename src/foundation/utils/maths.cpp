// Copyright 2025-2026 Sam Windell
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

TEST_CASE(TestQuarterSineFade) {
    REQUIRE(QuarterSineFade(0.0f) == 0);
    REQUIRE(ApproxEqual(QuarterSineFade(1.0f), 1.0f, 1e-4f));

    REQUIRE(ApproxEqual(QuarterSineFade(0.5f), 0.7071068f, 1e-4f));

    for (int i = 0; i <= 100; i++) {
        auto const x = (f32)i / 100.0f;
        auto const expected = Sin(x * (k_pi<> / 2.0f));
        REQUIRE(ApproxEqual(QuarterSineFade(x), expected, 1e-3f));
    }
    return k_success;
}

TEST_CASE(TestExp2Fast) {
    REQUIRE(ApproxEqual(Exp2Fast(0.0), 1.0, 1e-6));

    // Far wider than any pitch range we use; stay well under a cent (~6e-4 relative error).
    for (int i = -12000; i <= 12000; ++i) {
        auto const x = (f64)i / 100.0;
        auto const expected = Exp2(x);
        REQUIRE(ApproxEqual(Exp2Fast(x), expected, expected * 1e-6));
    }
    return k_success;
}

TEST_CASE(TestFastSimdExpLog) {
    // Exp2Fast / ExpFast cover the argument range the distortion shapers hit; LogFast only sees x >= 1.
    for (int i = -4000; i <= 4000; ++i) {
        auto const x = (f32)i / 100.0f;
        CAPTURE(x);

        auto const exp2_expected = Exp2(x);
        REQUIRE(ApproxEqual(Exp2Fast(f32x2(x)).x, exp2_expected, exp2_expected * 1e-6f));

        // ExpFast folds in a f32 x*log2(e) multiply, so it's a touch looser than Exp2Fast at large |x|.
        auto const exp_expected = Exp(x);
        REQUIRE(ApproxEqual(ExpFast(f32x2(x)).x, exp_expected, exp_expected * 1e-5f));

        if (x >= 1) {
            auto const log_expected = Log(x);
            REQUIRE(ApproxEqual(LogFast(f32x2(x)).x, log_expected, Max(Abs(log_expected), 1.0f) * 2e-5f));
        }
    }

    // Both lanes are computed independently.
    auto const two_lanes = ExpFast(f32x2 {1.0f, -2.0f});
    REQUIRE(ApproxEqual(two_lanes.x, Exp(1.0f), Exp(1.0f) * 1e-6f));
    REQUIRE(ApproxEqual(two_lanes.y, Exp(-2.0f), Exp(-2.0f) * 1e-6f));

    // Out-of-range arguments stay finite rather than producing NaN/Inf.
    REQUIRE(__builtin_isfinite(Exp2Fast(f32x2(1000.0f)).x));
    REQUIRE(Exp2Fast(f32x2(-1000.0f)).x >= 0);
    return k_success;
}

TEST_CASE(TestSinFast) {
    REQUIRE(SinFast(f32x2(0)).x == 0.0f); // exact at zero

    // Well past the few-radian range the distortion shapers hit, both lanes independent.
    for (int i = -20000; i <= 20000; ++i) {
        auto const x = (f32)i / 400.0f; // covers ~[-50, 50]
        CAPTURE(x);
        auto const v = SinFast(f32x2 {x, -x});
        REQUIRE(ApproxEqual(v.x, Sin(x), 2e-6f));
        REQUIRE(ApproxEqual(v.y, Sin(-x), 2e-6f));
    }
    return k_success;
}

TEST_REGISTRATION(RegisterMathsTests) {
    REGISTER_TEST(TestTrigLookupTable);
    REGISTER_TEST(TestMathsTrigTurns);
    REGISTER_TEST(TestQuarterSineFade);
    REGISTER_TEST(TestExp2Fast);
    REGISTER_TEST(TestFastSimdExpLog);
    REGISTER_TEST(TestSinFast);
}
