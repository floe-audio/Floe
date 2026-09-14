// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"
#include "foundation/utils/maths.hpp"

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
PUBLIC constexpr T Copysign(T x, T y) {
    return __builtin_elementwise_copysign(x, y);
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
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Tanh, tanh)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Atan, atan)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Floor, floor)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log, log)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log2, log2)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Log10, log10)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Exp, exp)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Exp2, exp2)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Round, round)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Trunc, trunc)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Sqrt, sqrt)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Pow, pow)
DEFINE_BUILTIN_SIMD_MATHS_FUNC(Fabs, abs)

// Fast 2^x for f32 vectors: a polynomial for the fractional part, the integer part built straight into the
// float's exponent bits. Max relative error ~7.5e-8 (below f32 epsilon). x is clamped to the representable
// exponent range so the result is always finite. Polynomial from:
//   lolremez --float -d 5 -r "0:1" "2^x" "2^x"
PUBLIC ALWAYS_INLINE f32x2 Exp2Fast(f32x2 x) {
    x = Min(Max(x, f32x2(-126)), f32x2(127));
    auto const integer_part = __builtin_elementwise_floor(x);
    auto const f = x - integer_part; // [0, 1)

    f32x2 u = 0.0018775767f;
    u = (u * f) + 0.0089893397f;
    u = (u * f) + 0.055826318f;
    u = (u * f) + 0.24015361f;
    u = (u * f) + 0.69315308f;
    u = (u * f) + 0.99999994f;

    auto const scale = __builtin_bit_cast(f32x2, (ConvertVector(integer_part, s32x2) + 127) << 23);
    return u * scale;
}

// Fast e^x for f32 vectors, via Exp2Fast (e^x = 2^(x * log2(e))).
PUBLIC ALWAYS_INLINE f32x2 ExpFast(f32x2 x) { return Exp2Fast(x * 1.4426950408889634f); }

// Fast natural log for f32 vectors, valid for x > 0. Decomposes x = m * 2^e and approximates log2(m) over
// [1, 2]; e comes straight from the exponent bits. Max relative error ~1.3e-5. Polynomial from:
//   lolremez --float -d 5 -r "1:2" "log(x)/log(2)"
PUBLIC ALWAYS_INLINE f32x2 LogFast(f32x2 x) {
    auto const bits = __builtin_bit_cast(s32x2, x);
    auto const exponent = ConvertVector((bits >> 23) - 127, f32x2);
    auto const mantissa = __builtin_bit_cast(f32x2, (bits & 0x007fffff) | 0x3f800000); // [1, 2)

    f32x2 p = 0.04487361f;
    p = (p * mantissa) - 0.41656369f;
    p = (p * mantissa) + 1.6311488f;
    p = (p * mantissa) - 3.5507929f;
    p = (p * mantissa) + 5.091711f;
    p = (p * mantissa) - 2.800364f;

    return (exponent + p) * 0.6931471805599453f; // * ln(2)
}

// Fast sine for f32 vectors (radians). Range-reduces to [-pi/2, pi/2] via a three-part Cody-Waite
// subtraction of pi (so the reduction stays accurate for arguments of a few tens), then an odd degree-9
// minimax polynomial. Max error ~5e-9 over one period. Accuracy degrades for very large |x| as the
// reduction loses the low bits; fine for the few-radian inputs here. Polynomial from:
//   lolremez --float -d 4 -r "0:2.4674011" "sin(sqrt(x))/sqrt(x)"   (deg-4 in x^2 = deg-9 in x)
PUBLIC ALWAYS_INLINE f32x2 SinFast(f32x2 x) {
    auto const k = __builtin_elementwise_round(x * 0.31830988618379067f); // round(x / pi)

    // pi split into three parts so k*pi keeps its low bits.
    auto r = x - (k * 3.140625f);
    r = r - (k * 0.0009670257568359375f);
    r = r - (k * 6.2771141e-7f);

    auto const r2 = r * r;
    f32x2 p = 2.6052248e-6f;
    p = (p * r2) - 0.00019809075f;
    p = (p * r2) + 0.0083330506f;
    p = (p * r2) - 0.16666658f;
    p = (p * r2) + 1.0f;
    auto const sin = p * r;

    // sin(x) = (-1)^k sin(r): flip the sign bit when k is odd.
    auto const sign_flip = (ConvertVector(k, s32x2) & 1) << 31;
    return __builtin_bit_cast(f32x2, __builtin_bit_cast(s32x2, sin) ^ sign_flip);
}

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
