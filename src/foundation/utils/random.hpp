// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/contiguous.hpp"
#include "foundation/container/optional.hpp"
#include "foundation/universal_defs.hpp"

// https://prng.di.unimi.it/
// "This is a fixed-increment version of Java 8's SplittableRandom generator See
// http://dx.doi.org/10.1145/2714064.2660195 and
// http://docs.oracle.com/javase/8/docs/api/java/util/SplittableRandom.html"

PUBLIC inline u64 RandomU64(u64& seed) {
    u64 z = (seed += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

constexpr u64 k_rand_max = u64(-1);

template <Integral Type>
PUBLIC inline Type RandomIntInRange(u64& seed, Type inclusive_min, Type inclusive_max) {
    auto const size = (inclusive_max - inclusive_min) + 1;
    if (size == 1) return inclusive_min;
    ASSERT(size > 0);
    auto const r = RandomU64(seed);
    return inclusive_min + Type(r % (u64)size);
}

template <FloatingPoint Type>
PUBLIC inline Type RandomFloatInRange(u64& seed, Type min, Type max) {
    constexpr u64 k_arbitrary_val = 1239671576;
    return min + (((Type)(RandomU64(seed) % k_arbitrary_val) / (Type)k_arbitrary_val) * (max - min));
}

template <FloatingPoint Type>
PUBLIC inline Type RandomFloat01(u64& seed) {
    return RandomFloatInRange<Type>(seed, 0, 1);
}

struct RandomNormalDistribution {
    RandomNormalDistribution(f64 mean, f64 std_dev) : mean(mean), std_dev(std_dev) {}

    // https://en.wikipedia.org/wiki/Marsaglia_polar_method
    f64 Next(u64& seed) {
        if (has_spare) {
            has_spare = false;
            return (spare * std_dev) + mean;
        } else {
            f64 u;
            f64 v;
            f64 s;
            do {
                u = RandomFloat01<f64>(seed) * 2.0 - 1.0;
                v = RandomFloat01<f64>(seed) * 2.0 - 1.0;
                s = u * u + v * v;
            } while (s >= 1.0 || s == 0.0);
            s = __builtin_sqrt(-2.0 * __builtin_log(s) / s);
            spare = v * s;
            has_spare = true;
            return mean + (std_dev * u * s);
        }
    }

    f64 mean, std_dev;
    f64 spare;
    bool has_spare = false;
};

inline int FastRand(unsigned int& seed) {
    seed = (214013 * seed + 2531011);
    return (seed >> 16) & 0x7FFF;
}

template <Integral Type>
struct RandomIntGenerator {
    RandomIntGenerator() {}
    Type GetRandomInRange(u64& seed, Type const min, Type const max, bool disallow_previous_result = true) {
        ASSERT(max >= min);
        auto const size = max - min;
        if (!size) return min;
        Type result;
        int count = 0;
        do {
            result = GetInRange(seed, min, max);
        } while (disallow_previous_result && size &&
                 (m_previous_random_index && result == *m_previous_random_index) && (count++ < 3));
        m_previous_random_index = result;
        ASSERT(result >= min && result <= max);
        return result;
    }

    Optional<Type> m_previous_random_index {};

  private:
    Type GetInRange(u64& seed, Type const min, Type const max) {
        return RandomIntInRange<Type>(seed, min, max);
    }
};

template <typename Type>
struct RandomFloatGenerator {
    Type GetRandomInRange(u64& seed, Type const min, Type const max, bool disallow_previous_result = false) {
        auto const int_max = (u64)1 << (u64)31;
        auto const random_int =
            random_int_generator.GetRandomInRange(seed, (u64)0, int_max, disallow_previous_result);
        auto const val01 = (Type)random_int / (Type)int_max;
        return min + (val01 * (max - min));
    }

    RandomIntGenerator<u32> random_int_generator;
};

PUBLIC auto& Shuffle(ContiguousContainer auto& data, u64& seed) {
    if (data.size <= 1) return data;
    for (usize i = 0; i < data.size - 1; i++) {
        u64 const j = i + (RandomU64(seed) / (k_rand_max / (data.size - i) + 1));
        ASSERT(j < data.size);
        Swap(data[i], data[j]);
    }
    return data;
}

PUBLIC auto& RandomElement(ContiguousContainer auto const& data, u64& seed) {
    ASSERT(data.size);
    return data[RandomIntInRange<usize>(seed, 0, data.size - 1)];
}
