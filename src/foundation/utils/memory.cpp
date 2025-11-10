// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestMemoryUtils) {
    CHECK(BytesToAddForAlignment(10, 1) == 0);
    CHECK(BytesToAddForAlignment(9, 1) == 0);
    CHECK(BytesToAddForAlignment(3333333, 1) == 0);
    CHECK(BytesToAddForAlignment(0, 2) == 0);
    CHECK(BytesToAddForAlignment(1, 2) == 1);
    CHECK(BytesToAddForAlignment(2, 2) == 0);
    CHECK(BytesToAddForAlignment(1, 4) == 3);
    CHECK(BytesToAddForAlignment(2, 4) == 2);
    CHECK(BytesToAddForAlignment(3, 4) == 1);
    CHECK(BytesToAddForAlignment(4, 4) == 0);
    CHECK(BytesToAddForAlignment(31, 32) == 1);

    // test NumBitsNeededToStore
    CHECK_EQ(NumBitsNeededToStore(0), 1uz);
    CHECK_EQ(NumBitsNeededToStore(1), 1uz);
    CHECK_EQ(NumBitsNeededToStore(2), 2uz);
    CHECK_EQ(NumBitsNeededToStore(3), 2uz);
    CHECK_EQ(NumBitsNeededToStore(4), 3uz);
    CHECK_EQ(NumBitsNeededToStore(5), 3uz);
    CHECK_EQ(NumBitsNeededToStore(6), 3uz);
    CHECK_EQ(NumBitsNeededToStore(7), 3uz);
    CHECK_EQ(NumBitsNeededToStore(8), 4uz);

    return k_success;
}

static_assert(NextPowerOf2(0u) == 1u);
static_assert(NextPowerOf2(1u) == 1u);
static_assert(NextPowerOf2(2u) == 2u);
static_assert(NextPowerOf2(3u) == 4u);
static_assert(NextPowerOf2(4u) == 4u);
static_assert(NextPowerOf2(5u) == 8u);
static_assert(NextPowerOf2(6u) == 8u);
static_assert(NextPowerOf2(7u) == 8u);
static_assert(NextPowerOf2(8u) == 8u);
static_assert(NextPowerOf2(9u) == 16u);
static_assert(NextPowerOf2(15u) == 16u);
static_assert(NextPowerOf2(16u) == 16u);
static_assert(NextPowerOf2(17u) == 32u);

TEST_REGISTRATION(RegisterMemoryTests) { REGISTER_TEST(TestMemoryUtils); }
