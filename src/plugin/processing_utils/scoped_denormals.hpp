// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef __ARM_ARCH

#include <xmmintrin.h>

class ScopedNoDenormals {
  public:
    ScopedNoDenormals() {
        mxcsr = _mm_getcsr();
        // set flush-to-zero (FTZ) and denormals-are-zero (DAZ) flags
        _mm_setcsr(mxcsr | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
    }

    ~ScopedNoDenormals() { _mm_setcsr(mxcsr); }

    unsigned mxcsr;
};

#else

#include <fenv.h>

class ScopedNoDenormals {
  public:
    ScopedNoDenormals() {
        auto result = fegetenv(&prev);
        ASSERT_EQ(result, 0);

        fenv_t v = _FE_DFL_DISABLE_DENORMS_ENV;
        result = fesetenv(&v);
        ASSERT_EQ(result, 0);
    }

    ~ScopedNoDenormals() {
        auto result = fesetenv(&prev);
        ASSERT_EQ(result, 0);
    }

    fenv_t prev;
};

#endif
