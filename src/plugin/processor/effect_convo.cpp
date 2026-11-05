// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "effect_convo.hpp"

#include "tests/framework.hpp"

TEST_CASE(TestStereoConvolverMonoIr) {
    // The value after the IR must never be read.
    f32 const ir_with_guard[] = {1, 0.5f, 0.25f, 0.125f, 100};
    constexpr int k_ir_frames = 4;

    auto convolver = CreateStereoConvolver();
    DEFER { DestroyStereoConvolver(convolver); };
    Init(*convolver, ir_with_guard, 0, k_ir_frames, 1);

    constexpr int k_num_frames = 8;
    f32 impulse[k_num_frames] = {1};
    f32 out_l[k_num_frames] {};
    f32 out_r[k_num_frames] {};
    Process(*convolver, impulse, impulse, out_l, out_r, k_num_frames);

    for (auto const frame : Range(k_num_frames)) {
        auto const expected_l = frame < k_ir_frames ? ir_with_guard[frame] : 0.0f;
        // The right channel of a mono IR is one frame ahead of the left.
        auto const expected_r = frame < k_ir_frames - 1 ? ir_with_guard[frame + 1] : 0.0f;
        CHECK_APPROX_EQ(out_l[frame], expected_l, 0.0001f);
        CHECK_APPROX_EQ(out_r[frame], expected_r, 0.0001f);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterConvolutionTests) { REGISTER_TEST(TestStereoConvolverMonoIr); }
