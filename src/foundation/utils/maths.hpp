// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

PUBLIC constexpr f64 SecondsToMilliseconds(f64 s) { return s * 1e3; }
PUBLIC constexpr f64 SecondsToMicroseconds(f64 s) { return s * 1e6; }
PUBLIC constexpr f64 SecondsToNanoseconds(f64 s) { return s * 1e9; }

template <typename Type = f32>
constexpr Type k_pi = (Type)3.14159265358979323846;
template <typename Type = f32>
constexpr Type k_half_pi = k_pi<Type> / 2;
template <typename Type = f32>
constexpr Type k_tau = k_pi<Type> * 2;
template <typename Type = f32>
constexpr Type k_two_pi = k_tau<Type>;
template <typename Type = f32>
constexpr Type k_sqrt_two = (Type)1.41421356237309504880;
template <typename Type = f32>
constexpr Type k_e32 = (Type)2.71828182845904523536;
template <typename Type = f32>
constexpr Type k_ln2 = (Type)0.69314718055994530942;

constexpr f32 k_nan32 = __builtin_nanf("");
constexpr f64 k_nan64 = __builtin_nan("");

template <typename T>
PUBLIC ALWAYS_INLINE constexpr T Min(T a, T b) {
    if constexpr (Vector<T>) return __builtin_elementwise_min(a, b);
    return (b < a) ? b : a;
}
template <typename T>
PUBLIC ALWAYS_INLINE constexpr T Max(T a, T b) {
    if constexpr (Vector<T>) return __builtin_elementwise_max(a, b);
    return (a < b) ? b : a;
}

// clang-format off
template <typename T> PUBLIC ALWAYS_INLINE constexpr T Min(T a, T b, T c) { return Min(Min(a, b), c); }
template <typename T> PUBLIC ALWAYS_INLINE constexpr T Min(T a, T b, T c, T d) { return Min(Min(Min(a, b), c), d); }
template <typename T> PUBLIC ALWAYS_INLINE constexpr T Max(T a, T b, T c) { return Max(Max(a, b), c); }
template <typename T> PUBLIC ALWAYS_INLINE constexpr T Max(T a, T b, T c, T d) { return Max(Max(Max(a, b), c), d); }
// clang-format on

template <typename T>
PUBLIC ALWAYS_INLINE constexpr T Clamp(T v, T lo, T hi) {
    if constexpr (!Vector<T>) ASSERT(lo <= hi);
    return Min(Max(v, lo), hi);
}

template <typename T>
PUBLIC ALWAYS_INLINE constexpr T Clamp01(T v) {
    return Min(Max(v, (T)0), (T)1);
}

template <typename Type>
requires(Signed<Type> || Vector<Type>)
PUBLIC constexpr Type Abs(Type x) {
    if constexpr (Vector<Type>) return __builtin_elementwise_abs(x);
    return x < Type(0) ? -x : x;
}

// Number of bits set
template <UnsignedInt T>
PUBLIC ALWAYS_INLINE constexpr u8 Popcount(T x) {
    if constexpr (sizeof(T) <= sizeof(unsigned int))
        return (u8)__builtin_popcount(x);
    else if constexpr (sizeof(T) <= sizeof(unsigned long))
        return (u8)__builtin_popcountl(x);
    else if constexpr (sizeof(T) <= sizeof(unsigned long long))
        return (u8)__builtin_popcountll(x);
    else
        static_assert(false, "Unsupported type");
}

#define DEFINE_BUILTIN_MATHS_FUNC(name, func)                                                                \
    template <FloatingPoint T>                                                                               \
    PUBLIC ALWAYS_INLINE constexpr T name(T x) {                                                             \
        if constexpr (Same<T, f64>)                                                                          \
            return __builtin_##func(x);                                                                      \
        else if constexpr (Same<T, f32>)                                                                     \
            return __builtin_##func##f(x);                                                                   \
        else                                                                                                 \
            static_assert(false, "Unsupported type");                                                        \
    }

#define DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(name, func)                                                         \
    template <FloatingPoint T>                                                                               \
    PUBLIC ALWAYS_INLINE constexpr T name(T x, T y) {                                                        \
        if constexpr (Same<T, f64>)                                                                          \
            return __builtin_##func(x, y);                                                                   \
        else if constexpr (Same<T, f32>)                                                                     \
            return __builtin_##func##f(x, y);                                                                \
        else                                                                                                 \
            static_assert(false, "Unsupported type");                                                        \
    }

DEFINE_BUILTIN_MATHS_FUNC(Fabs, fabs)
DEFINE_BUILTIN_MATHS_FUNC(Ceil, ceil)
DEFINE_BUILTIN_MATHS_FUNC(Floor, floor)
DEFINE_BUILTIN_MATHS_FUNC(Trunc, trunc)
DEFINE_BUILTIN_MATHS_FUNC(Round, round)
DEFINE_BUILTIN_MATHS_FUNC(Sin, sin)
DEFINE_BUILTIN_MATHS_FUNC(Cos, cos)
DEFINE_BUILTIN_MATHS_FUNC(Tan, tan)
DEFINE_BUILTIN_MATHS_FUNC(Asin, asin)
DEFINE_BUILTIN_MATHS_FUNC(Acos, acos)
DEFINE_BUILTIN_MATHS_FUNC(Atan, atan)
DEFINE_BUILTIN_MATHS_FUNC(Sinh, sinh)
DEFINE_BUILTIN_MATHS_FUNC(Cosh, cosh)
DEFINE_BUILTIN_MATHS_FUNC(Tanh, tanh)
DEFINE_BUILTIN_MATHS_FUNC(Asinh, asinh)
DEFINE_BUILTIN_MATHS_FUNC(Acosh, acosh)
DEFINE_BUILTIN_MATHS_FUNC(Atanh, atanh)
DEFINE_BUILTIN_MATHS_FUNC(Exp, exp)
DEFINE_BUILTIN_MATHS_FUNC(Exp2, exp2)
DEFINE_BUILTIN_MATHS_FUNC(Log, log)
DEFINE_BUILTIN_MATHS_FUNC(Log2, log2)
DEFINE_BUILTIN_MATHS_FUNC(Log10, log10)
DEFINE_BUILTIN_MATHS_FUNC(Sqrt, sqrt)

DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(Copysign, copysign)
DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(Pow, pow)
DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(Fmod, fmod)
// fmin/fmax handle NaNs and signed zeros
DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(Fmin, fmin)
DEFINE_BUILTIN_MATHS_FUNC_2_ARGS(Fmax, fmax)

template <FloatingPoint FloatType>
PUBLIC constexpr int RoundPositiveFloat(FloatType v) {
    ASSERT(v >= 0);
    ASSERT(v < (FloatType)LargestRepresentableValue<int>());
    return (int)(v + (FloatType)0.5);
}

template <FloatingPoint FloatType>
PUBLIC constexpr FloatType FloorPositiveFloat(FloatType v) {
    ASSERT(v >= 0);
    ASSERT(v < (FloatType)LargestRepresentableValue<int>());
    return (FloatType)(s64)v;
}

template <typename T1, typename T2, typename T3>
PUBLIC constexpr auto LinearInterpolate(T1 t, T2 a, T3 b) {
    return a + (t * (b - a));
}

PUBLIC constexpr f32 LinearInterpolate(f32 t, f32 a, f32 b) { return a + (t * (b - a)); }

PUBLIC constexpr f32 Map(f32 value, f32 in_min, f32 in_max, f32 out_min, f32 out_max) {
    if (in_max - in_min == 0.0f) return 0.0f;
    return out_min + ((value - in_min) * (out_max - out_min) / (in_max - in_min));
}

PUBLIC constexpr f32 MapTo01(f32 value, f32 in_min, f32 in_max) { return Map(value, in_min, in_max, 0, 1); }

PUBLIC constexpr f32 MapFrom01(f32 value, f32 out_min, f32 out_max) {
    ASSERT(value >= 0 && value <= 1);
    return Map(value, 0, 1, out_min, out_max);
}

PUBLIC constexpr f32 MapTo01Skew(f32 non_norm_val, f32 min, f32 max, f32 skew) {
    ASSERT(non_norm_val >= min && non_norm_val <= max);
    if (skew == 1) return MapTo01(non_norm_val, min, max);
    auto const normalised_val = (non_norm_val - min) / (max - min);
    return Pow(normalised_val, 1.0f / skew);
}

PUBLIC constexpr f32 MapFrom01Skew(f32 normalised_val, f32 min, f32 max, f32 skew) {
    ASSERT(normalised_val >= 0 && normalised_val <= 1);
    if (skew == 1) return MapFrom01(normalised_val, min, max);
    return (Pow(normalised_val, skew) * (max - min)) + min;
}

namespace trig_table_lookup {
constexpr f32 k_quadrant1_of_sine[34] = {0.f,
                                         0.04906767433f,
                                         0.09801714033f,
                                         0.1467304745f,
                                         0.195090322f,
                                         0.2429801799f,
                                         0.2902846773f,
                                         0.3368898534f,
                                         0.3826834324f,
                                         0.4275550934f,
                                         0.4713967368f,
                                         0.5141027442f,
                                         0.555570233f,
                                         0.5956993045f,
                                         0.6343932842f,
                                         0.6715589548f,
                                         0.7071067812f,
                                         0.7409511254f,
                                         0.7730104534f,
                                         0.8032075315f,
                                         0.8314696123f,
                                         0.85772861f,
                                         0.8819212643f,
                                         0.9039892931f,
                                         0.9238795325f,
                                         0.9415440652f,
                                         0.9569403357f,
                                         0.9700312532f,
                                         0.9807852804f,
                                         0.98917651f,
                                         0.9951847267f,
                                         0.9987954562f,
                                         1.f,
                                         1.f};

constexpr f32 k_three_over_two_pi = k_pi<> + k_half_pi<>;
constexpr f32 k_max_index = (f32)ArraySize(k_quadrant1_of_sine) - 2;

struct SineQuadrantInfo {
    f32 start_value;
    f32 sign;
    bool inverted;
};

constexpr SineQuadrantInfo k_quadrant_infos[4] = {
    {0, 1, false},
    {k_half_pi<>, 1, true},
    {k_pi<>, -1, false},
    {k_three_over_two_pi, -1, true},
};

static constexpr f32 GetQuadrant1ValueFrom01(f32 val01) {
    ASSERT(val01 >= 0 && val01 <= 1);
    auto const table_pos = val01 * k_max_index;
    auto const table_index = (unsigned)table_pos;
    auto const t = table_pos - (f32)table_index;
    return LinearInterpolate(t, k_quadrant1_of_sine[table_index], k_quadrant1_of_sine[table_index + 1]);
}

static constexpr f32 MapToRange0ToTwoPi(f32 rad) {
    rad -= (f32)(int)(rad / k_two_pi<>)*k_two_pi<>;
    if (rad < 0) rad = k_two_pi<> + rad;
    if (rad == k_two_pi<>) rad = 0;
    ASSERT(rad >= 0 && rad < k_two_pi<>);
    return rad;
}

PUBLIC constexpr f32 Sin(f32 rad) {
    rad = MapToRange0ToTwoPi(rad);

    auto const quadrant = k_quadrant_infos[(unsigned)(rad / k_half_pi<>)];
    rad -= quadrant.start_value;
    f32 pos = rad / k_half_pi<>;
    if (quadrant.inverted) pos = 1 - pos;
    return GetQuadrant1ValueFrom01(pos) * quadrant.sign;
}

PUBLIC constexpr f32 Cos(f32 rad) { return Sin(rad + k_half_pi<>); }

PUBLIC constexpr f32 Tan(f32 rad) { return Sin(rad) / Cos(rad); }

// Turns API
// In turns, 0 is 0 degrees, 0.5 is 180 degrees, 1 is 360 degrees, 2 is 720 degrees, etc
PUBLIC constexpr f32 SinTurnsPositive(f32 turns) {
    ASSERT(turns >= 0);
    auto const x = turns * 4;
    auto const index_unbounded = (u32)x;
    auto const index = index_unbounded % 4;
    auto const quadrant = k_quadrant_infos[index];

    auto pos = x - (f32)index_unbounded;

    if (quadrant.inverted) pos = 1 - pos;
    return GetQuadrant1ValueFrom01(pos) * quadrant.sign;
}

PUBLIC constexpr f32 SinTurns(f32 turns) {
    return (turns >= 0) ? SinTurnsPositive(turns) : (-SinTurnsPositive(-turns));
}

PUBLIC constexpr f32 CosTurnsPositive(f32 turns) { return SinTurnsPositive(turns + 0.25f); }
PUBLIC constexpr f32 CosTurns(f32 turns) { return SinTurns(turns + 0.25f); }
PUBLIC constexpr f32 TanTurns(f32 turns) { return SinTurns(turns) / CosTurns(turns); }

} // namespace trig_table_lookup
