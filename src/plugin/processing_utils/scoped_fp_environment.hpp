// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Flushes denormals, and optionally masks floating-point exceptions, for the object's lifetime, then restores
// the host's environment.

struct FloatingPointEnvironmentOptions {
    // We've seen a few Sentry reports (e.g. FLOE-EP) of EXCEPTION_FLT_INVALID_OPERATION firing in code that
    // is valid. For instance, the RBJ filter coefficient smoothing does 2 scalar divides by non-zero
    // constants; the compiler emitted them as one 4-wide divps with the 2 unused lanes zeroed, so those lanes
    // compute 0/0. We compile with the default FP exception mode, which lets the compiler optimise like this
    // on the assumption that exceptions are masked.
    //
    // The reports are rare, suggesting only some hosts call us with FP exceptions unmasked. In those hosts we
    // crash on code that otherwise works, so we mask the exceptions and let people carry on using Floe. The
    // cost is that a genuinely invalid operation won't trap in those hosts either; it gives NaN as it does
    // everywhere else. Masking is opt-in so that it's only applied to the calls where we've seen these
    // reports.
    //
    // The alternatives seem worse:
    // - Changing the compiler's FP exception mode restricts optimisation, likely making the code slower.
    // - Catching the trap, reporting it and resuming (may be possible on Windows) is fiddly logic in the
    //   crash handler and a potentially never-ending project: what the compiler emits can change, each report
    //   would need disassembling to tell whether it's harmless, and we'd need a way to stop repeat reports of
    //   the known-harmless cases.
    bool mask_exceptions;
};

#ifndef __ARM_ARCH

#include <xmmintrin.h>

class ScopedFloatingPointEnvironment {
  public:
    ScopedFloatingPointEnvironment(FloatingPointEnvironmentOptions options) {
        mxcsr = _mm_getcsr();
        _mm_setcsr(mxcsr | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON |
                   (options.mask_exceptions ? (unsigned)_MM_MASK_MASK : 0u));
    }

    ~ScopedFloatingPointEnvironment() { _mm_setcsr(mxcsr); }

    unsigned mxcsr;
};

#else

#include <fenv.h>

// The environment installed here has exceptions masked whatever the options say.
class ScopedFloatingPointEnvironment {
  public:
    ScopedFloatingPointEnvironment(FloatingPointEnvironmentOptions) {
        auto result = fegetenv(&prev);
        ASSERT_EQ(result, 0);

        fenv_t v = _FE_DFL_DISABLE_DENORMS_ENV;
        result = fesetenv(&v);
        ASSERT_EQ(result, 0);
    }

    ~ScopedFloatingPointEnvironment() {
        auto result = fesetenv(&prev);
        ASSERT_EQ(result, 0);
    }

    fenv_t prev;
};

#endif
