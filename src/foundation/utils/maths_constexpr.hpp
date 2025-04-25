// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Based on https://github.com/pkeir/ctfft/tree/master by Paul Keir but with modifications:
// - Uses clang/gcc builtins instead of std::
// - adds pow with floating point exponent
// - adds some extra functions based on code from GCEM https://github.com/kthohr/gcem (apache 2 licence)
// - replaces sin with a version based on Remez polynomial approximation
//
// Copyright Paul Keir 2012-2023
// SPDX-License-Identifier: BSL-1.0
//
// It's untested how these compare to the non-constexpr math.h functions in terms of speed. Likely these
// constexpr ones would be much slower.

#pragma once
#include <float.h>

namespace constexpr_math {

constexpr double k_pi = 3.141592653589793;
constexpr double k_pi_2 = 1.570796326794897;
constexpr double k_e = 2.718281828459045;
constexpr double k_tau = k_pi * 2;

constexpr double k_tol = 1e-8; // 0.00000001;

constexpr double Abs(double const x) {
    if (x == 0) return 0; // deal with signed zeros
    return x < 0.0 ? -x : x;
}

constexpr double Square(double const x) { return x * x; }

constexpr double SqrtHelper(double const x, double const g) {
    return Abs(g - (x / g)) < k_tol ? g : SqrtHelper(x, (g + x / g) / 2.0);
}

constexpr double Sqrt(double const x) { return SqrtHelper(x, 1.0); }

constexpr double Cube(double const x) { return x * x * x; }

constexpr double Pow(double base, int exponent) {
    return exponent < 0    ? 1.0 / Pow(base, -exponent)
           : exponent == 0 ? 1.
           : exponent == 1 ? base
                           : base * Pow(base, exponent - 1);
}

// atan formulae from http://mathonweb.com/algebra_e-book.htm
// x - x^3/3 + x^5/5 - x^7/7+x^9/9  etc.
constexpr double AtanPolyHelper(double const res, double const num1, double const den1, double const delta) {
    return res < k_tol ? res
                       : res + AtanPolyHelper(((num1 * delta) / (den1 + 2.)) - (num1 / den1),
                                              num1 * delta * delta,
                                              den1 + 4.,
                                              delta);
}

constexpr double AtanPoly(double const x) {
    return x + AtanPolyHelper((Pow(x, 5) / 5.) - (Pow(x, 3) / 3.), Pow(x, 7), 7., x * x);
}

// Define an M_PI_6? Define a root 3?
constexpr double AtanIdentity(double const x) {
    return x <= (2. - Sqrt(3.)) ? AtanPoly(x) : (k_pi_2 / 3.) + AtanPoly((Sqrt(3.) * x - 1) / (Sqrt(3.) + x));
}

constexpr double AtanCmplmntry(double const x) {
    return (x < 1) ? AtanIdentity(x) : k_pi_2 - AtanIdentity(1 / x);
}

constexpr double Atan(double const x) { return (x >= 0) ? AtanCmplmntry(x) : -AtanCmplmntry(-x); }

constexpr double Atan2(double const y, double const x) {
    return x > 0             ? Atan(y / x)
           : y >= 0 && x < 0 ? Atan(y / x) + k_pi
           : y < 0 && x < 0  ? Atan(y / x) - k_pi
           : y > 0 && x == 0 ? k_pi_2
           : y < 0 && x == 0 ? -k_pi_2
                             : 0; // 0 == undefined
}

// based on code from GCEM
// Copyright (C) 2016-2023 Keith O'Hara
// SPDX-License-Identifier: Apache-2.0
constexpr double Trunc(double const x) {
    constexpr double k_quiet_nan = __builtin_nan("");
    if (__builtin_isnan(x)) return k_quiet_nan;
    if (__builtin_isinf(x)) return x;
    auto const abs_x = Abs(x);
    if (DBL_MIN > abs_x) return x;
    if (abs_x >= 4503599627370496.) return x;
    return (double)(long long)x;
}

constexpr double Nearest(double x) { return (x - 0.5) > Trunc(x) ? Trunc(x + 0.5) : Trunc(x); }

constexpr double Fraction(double x) {
    return (x - 0.5) > (int)x ? -(((double)(int)(x + 0.5)) - x) : x - ((double)(int)(x));
}

constexpr double ExpHelper(double const r) {
    return 1.0 + r + (Pow(r, 2) / 2.0) + (Pow(r, 3) / 6.0) + (Pow(r, 4) / 24.0) + (Pow(r, 5) / 120.0) +
           (Pow(r, 6) / 720.0) + (Pow(r, 7) / 5040.0);
}

// exp(x) = e^n . e^r (where n is an integer, and -0.5 > r < 0.5
// exp(r) = e^r = 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120
constexpr double Exp(double const x) {
    constexpr double k_quiet_nan = __builtin_nan("");
    if (__builtin_isnan(x)) return k_quiet_nan;
    if (x == -__builtin_huge_val()) return 0;
    if (DBL_MIN > Abs(x)) return 1;
    if (x == __builtin_huge_val()) return __builtin_huge_val();

    auto const exp = Nearest(x);
    if (__builtin_isinf(exp)) return k_quiet_nan;
    return Pow(k_e, (int)exp) * ExpHelper(Fraction(x));
}

constexpr double Mantissa(double const x) {
    return x >= 10.0 ? Mantissa(x * 0.1) : x < 1.0 ? Mantissa(x * 10.0) : x;
}

// log(m) = log(sqrt(m)^2) = 2 x log( sqrt(m) )
// log(x) = log(m x 10^p) = 0.86858896 ln( sqrt(m) ) + p
constexpr int ExponentHelper(double const x, int const exp) {
    return x >= 10.0 ? ExponentHelper(x * 0.1, exp + 1) : x < 1.0 ? ExponentHelper(x * 10.0, exp - 1) : exp;
}

constexpr int Exponent(double const x) { return ExponentHelper(x, 0); }

constexpr double LogHelper2(double const y) {
    return 2.0 *
           (y + Pow(y, 3) / 3.0 + Pow(y, 5) / 5.0 + Pow(y, 7) / 7.0 + Pow(y, 9) / 9.0 + Pow(y, 11) / 11.0);
}

// log in the range 1-sqrt(10)
constexpr double LogHelper(double const x) { return LogHelper2((x - 1.0) / (x + 1.0)); }

// n.b. log 10 is 2.3025851
// n.b. log m = log (sqrt(m)^2) = 2 * log sqrt(m)
constexpr double Log(double const x) {
    constexpr double k_inf = __builtin_huge_val();
    constexpr double k_quiet_nan = __builtin_nan("");
    return x == 0  ? -k_inf
           : x < 0 ? k_quiet_nan
                   : (2.0 * LogHelper(Sqrt(Mantissa(x)))) + (2.3025851 * Exponent(x));
}

constexpr double Pow(double base, double exponent) {
    constexpr double k_quiet_nan = __builtin_nan("");
    if (base < 0) return k_quiet_nan;
    return Exp(exponent * Log(base));
}

constexpr float Powf(float base, float exponent) { return (float)Pow((double)base, (double)exponent); }

// based on code from GCEM,
// Copyright (C) 2016-2023 Keith O'Hara
// SPDX-License-Identifier: Apache-2.0
constexpr double Floor(double const x) noexcept {
    constexpr double k_quiet_nan = __builtin_nan("");
    if (__builtin_isnan(x)) return k_quiet_nan;
    if (__builtin_isinf(x)) return x;

    auto const ab = Abs(x);
    if (DBL_MIN > ab) return x;

    if (ab >= 4503599627370496.) return x;
    auto const x_whole = double(static_cast<long long>(x));
    return x_whole - static_cast<double>((x < 0) && (x < x_whole));
}

// based on code from GCEM
// Copyright (C) 2016-2023 Keith O'Hara
// SPDX-License-Identifier: Apache-2.0
constexpr double Fmod(double const x, double const y) {
    constexpr double k_quiet_nan = __builtin_nan("");
    if (__builtin_isnan(x) || __builtin_isnan(y)) return k_quiet_nan;
    if (__builtin_isinf(x) || __builtin_isinf(y)) return k_quiet_nan;
    return x - (Trunc(x / y) * y);
}

constexpr double Sin(double x) {
    x = Fmod(x, k_tau);

    // Generated using lolremez: https://github.com/samhocevar/lolremez
    // Approximation of f(x) = sin(x)
    // on interval [ 0, tau ]
    // with a polynomial of degree 6.
    // p(x)=(((((1.9780807228056624e-23*x-5.4653984455204592e-3)*x+8.5850278026940752e-2)*x-3.8595063473619359e-1)*x+2.4826590880887231e-1)*x+8.9753139917518325e-1)*x+6.8497712357808586e-3
    double u = 1.9780807228056624e-23;
    u = u * x + -5.4653984455204592e-3;
    u = u * x + 8.5850278026940752e-2;
    u = u * x + -3.8595063473619359e-1;
    u = u * x + 2.4826590880887231e-1;
    u = u * x + 8.9753139917518325e-1;
    return (u * x) + 6.8497712357808586e-3;
}

constexpr double Cos(double const x) { return Sin(k_pi_2 - x); }

} // namespace constexpr_math
