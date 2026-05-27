// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_utils.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestDbToAmpApprox) {
    REQUIRE(ApproxEqual(DbToAmp(-6), (f32)DbToAmpApprox(-6), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(0), (f32)DbToAmpApprox(0), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(-20), (f32)DbToAmpApprox(-20), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(6), (f32)DbToAmpApprox(6), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(3), (f32)DbToAmpApprox(3), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(12), (f32)DbToAmpApprox(12), 0.01f));
    REQUIRE(ApproxEqual(DbToAmp(-60), (f32)DbToAmpApprox(-60), 0.01f));
    return k_success;
}

TEST_REGISTRATION(RegisterAudioUtilsTests) { REGISTER_TEST(TestDbToAmpApprox); }
