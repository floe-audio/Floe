// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

constexpr int k_num_rand_test_repititions = 200;

TEST_CASE(TestRandomIntGeneratorUnsigned) {
    SUBCASE("unsigned") {
        RandomIntGenerator<unsigned int> generator;
        auto seed = RandomSeed();

        SUBCASE("Correct generation in range 0 to 3 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value allowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, false);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range 0 to 3000000000 with repeating last value disallowed") {
            constexpr unsigned int k_max_val = 3000000000;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto random_num = generator.GetRandomInRange(seed, 0, k_max_val, true);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("signed") {
        RandomIntGenerator<int> generator;
        auto seed = RandomSeed();

        SUBCASE("Correct generation in range -10 to 10 with repeating last value allowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }

        SUBCASE("Correct generation in range -10 to 10 with repeating last value disallowed") {
            constexpr int k_max_val = 10;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, true);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        }
    }
    SUBCASE("move object") {
        RandomIntGenerator<int> generator;
        auto seed = RandomSeed();

        constexpr int k_max_val = 10;
        {
            auto const random_num = generator.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator2 = generator;
        {
            auto const random_num = generator2.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }

        auto generator3 = Move(generator);
        {
            auto const random_num = generator3.GetRandomInRange(seed, -k_max_val, k_max_val, false);
            REQUIRE(random_num >= -k_max_val);
            REQUIRE(random_num <= k_max_val);
        }
    }
    return k_success;
}

template <typename T>
TEST_CASE(TestRandomFloatGenerator) {
    RandomFloatGenerator<T> generator;
    auto seed = RandomSeed();

    SUBCASE("random values are in a correct range") {
        auto test = [&](bool allow_repititions) {
            constexpr T k_max_val = 100;
            for (auto _ : Range(k_num_rand_test_repititions)) {
                auto const random_num =
                    generator.GetRandomInRange(seed, -k_max_val, k_max_val, allow_repititions);
                REQUIRE(random_num >= -k_max_val);
                REQUIRE(random_num <= k_max_val);
            }
        };
        test(true);
        test(false);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterRandomTests) {
    REGISTER_TEST(TestRandomFloatGenerator<f32>);
    REGISTER_TEST(TestRandomFloatGenerator<f64>);
    REGISTER_TEST(TestRandomIntGeneratorUnsigned);
}
