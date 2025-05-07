// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

// NOTE(Sam, May 2024): I had some strange problems related to including the x86 intrinsic headers. In some
// parts of the code there would be no problems, but in other parts there would be 'missing type __m128', or
// similar errors. I solved it by reducing the number of places that the various headers were included. I
// believe the issue was related to #ifdefs in the headers that are fussy about the order in which the headers
// are included. It's a messy problem because there are so many different headers, one for each version of the
// instruction sets: <mmintrin.h> = MMX, <xmmintrin.h> = SSE, <emmintrin.h> = SSE2, <pmmintrin.h> = SSE3, etc.
// It probably warrants keeping an eye on what headers are included in parts of the codebase.

#if defined(__x86_64__)
#if !defined(__SSE2__)
#error "SSE2 is our baseline requirement"
#endif
#include <emmintrin.h> // SSE2
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

template <typename VecType>
PUBLIC ALWAYS_INLINE VecType LoadAlignedToType(UnderlyingTypeOfVec<VecType> const* p) {
    return *CheckedPointerCast<VecType*>(p);
}

template <typename VecType>
PUBLIC ALWAYS_INLINE VecType LoadUnalignedToType(UnderlyingTypeOfVec<VecType> const* p) {
    // Even though it looks like a slow operation, the assembly generated for this is perfect. E.g. on
    // optimised x86_64 it's a single MOVUPS instruction, on non-optimised builds it's not bad too. Aarch64
    // assembly is perfect too.
    VecType result;
    __builtin_memcpy_inline(&result, p, sizeof(VecType));
    return result;
}

template <typename VecType>
PUBLIC ALWAYS_INLINE void StoreToAligned(UnderlyingTypeOfVec<VecType>* dest, VecType v) {
    *CheckedPointerCast<VecType*>(dest) = v;
}

template <typename VecType>
PUBLIC ALWAYS_INLINE void StoreToUnaligned(UnderlyingTypeOfVec<VecType>* dest, VecType v) {
    // Same as above - the assembly generated is perfect on optimised builds: single MOVUPS instruction.
    __builtin_memcpy_inline(dest, &v, sizeof(VecType));
}

template <F32Vector T>
PUBLIC constexpr T Sqrt(T a) {
    // IMPROVE: when we have support for __builtin_elementwise_sqrt, use that instead
#if defined(__x86_64__)
    return _mm_sqrt_ps((f32x4)a);
#elif defined(__aarch64__)
    return vsqrtq_f32((f32x4)a);
#endif
}

// fused multiply add, (x * y) + z.
template <F32Vector T>
PUBLIC constexpr T Fma(T x, T y, T z) {
    return __builtin_elementwise_fma(x, y, z);
}

template <F32Vector T>
PUBLIC constexpr T Pow(T x, T y) {
    return __builtin_elementwise_pow(x, y);
}

template <F32Vector T>
PUBLIC constexpr T Map(T value, T in_min, T in_max, T out_min, T out_max) {
    auto const denominator = in_max - in_min;
    auto const factor = (denominator != 0) ? (value - in_min) * (out_max - out_min) / denominator : 0;
    return out_min + factor;
}

// Doesn't check for divide by zero.
template <F32Vector T>
PUBLIC constexpr T MapUnchecked(T value, T in_min, T in_max, T out_min, T out_max) {
    return out_min + ((value - in_min) * (out_max - out_min) / (in_max - in_min));
}

template <F32Vector T>
PUBLIC constexpr T MapTo01(T value, T in_min, T in_max) {
    return Map(value, in_min, in_max, T(0), T(1));
}

template <F32Vector T>
PUBLIC constexpr T MapTo01Unchecked(T value, T in_min, T in_max) {
    return MapUnchecked(value, in_min, in_max, T(0), T(1));
}

template <F32Vector T>
PUBLIC constexpr T MapFrom01(T value, T out_min, T out_max) {
    return MapUnchecked(value, T(0), T(1), out_min, out_max);
}

#define DEFINE_BUILTIN_SIMD_MATHS_FUNC(name, func)                                                           \
    template <F32Vector T>                                                                                   \
    PUBLIC ALWAYS_INLINE constexpr T name(T x) {                                                             \
        return __builtin_elementwise_##func(x);                                                              \
    }

DEFINE_BUILTIN_SIMD_MATHS_FUNC(Ceil, ceil)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Sin, sin)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Cos, cos)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Tan, tan)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Floor, floor)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log, log)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log2, log2)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log10, log10)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Exp, exp)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Exp2, exp2)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Round, round)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Trunc, trunc)

// IMPROVE: we could do wider SIMD with these. e.g. AVX2
PUBLIC inline void SimdAddAlignedBuffer(f32* d, f32 const* s, usize num) {
    if (num == 0) return;
    ASSERT_HOT((usize)&d[0] % 16 == 0);
    ASSERT_HOT((usize)&s[0] % 16 == 0);
    ASSERT_HOT(!(d >= s && d < (s + num)));

    auto* dest = CheckedPointerCast<f32x4*>(d);
    auto const* source = CheckedPointerCast<f32x4 const*>(s);
    for (unsigned i = 0; i < num; i += NumVectorElements<f32x4>())
        *dest++ += *source++;
}

PUBLIC inline void SimdZeroAlignedBuffer(f32* d, usize num) {
    if (num == 0) return;
    ASSERT_HOT((usize)&d[0] % 16 == 0);

    for (usize i = 0; i < num; i += NumVectorElements<f32x4>()) {
        auto& dest = *CheckedPointerCast<f32x4*>(d + i);
        dest = f32x4 {0};
    }
}
