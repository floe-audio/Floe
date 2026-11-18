// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scoped_fp_environment.hpp"

#include <valgrind/valgrind.h>

#include "tests/framework.hpp"

TEST_CASE(TestScopedFloatingPointEnvironment) {
#ifndef __ARM_ARCH
    // Valgrind doesn't support unmasked SSE exceptions: the mask bits always read back as masked.
    if (RUNNING_ON_VALGRIND) return k_success;

    auto const original_mxcsr = _mm_getcsr();
    DEFER { _mm_setcsr(original_mxcsr); };

    // Only the invalid-operation exception: unmasking inexact would trap on ordinary arithmetic.
    auto const host_mxcsr = original_mxcsr & ~(unsigned)_MM_MASK_INVALID;
    _mm_setcsr(host_mxcsr);

    bool result_is_nan = false;
    unsigned mxcsr_when_masking = 0;
    {
        ScopedFloatingPointEnvironment const fp_environment {{.mask_exceptions = true}};
        mxcsr_when_masking = _mm_getcsr();
        f32 volatile zero = 0;
        f32 volatile result = zero / zero;
        result_is_nan = result != result;
    }
    auto const mxcsr_after_masking = _mm_getcsr();

    unsigned mxcsr_when_not_masking = 0;
    {
        ScopedFloatingPointEnvironment const fp_environment {{.mask_exceptions = false}};
        mxcsr_when_not_masking = _mm_getcsr();
    }
    auto const mxcsr_after_not_masking = _mm_getcsr();
    _mm_setcsr(original_mxcsr);

    CHECK(result_is_nan);
    CHECK_EQ(mxcsr_when_masking & (unsigned)_MM_MASK_MASK, (unsigned)_MM_MASK_MASK);
    CHECK_EQ(mxcsr_after_masking, host_mxcsr);
    CHECK_EQ(mxcsr_when_not_masking & (unsigned)_MM_MASK_MASK, host_mxcsr & (unsigned)_MM_MASK_MASK);
    CHECK_EQ(mxcsr_after_not_masking, host_mxcsr);
#endif
    return k_success;
}

TEST_REGISTRATION(RegisterScopedFloatingPointEnvironmentTests) {
    REGISTER_TEST(TestScopedFloatingPointEnvironment);
}
