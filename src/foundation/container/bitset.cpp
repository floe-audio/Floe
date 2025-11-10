// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestBitset) {
    {
        Bitset<65> b;
        REQUIRE(!b.AnyValuesSet());
        b.Set(0);
        REQUIRE(b.Get(0));
        REQUIRE(b.FirstUnsetBit() == 1);

        b <<= 1;
        REQUIRE(b.Get(1));
        REQUIRE(!b.Get(0));
        REQUIRE(b.FirstUnsetBit() == 0);

        b >>= 1;
        REQUIRE(b.Get(0));
        REQUIRE(b.AnyValuesSet());
        b.ClearAll();
        REQUIRE(!b.AnyValuesSet());

        b.SetToValue(5, true);
        auto smaller_bitset = b.Subsection<10>(0);
        REQUIRE(smaller_bitset.Get(5));

        b.ClearAll();

        Bitset<65> other;
        other.SetAll();
        b = other;
        REQUIRE(b.AnyValuesSet());
        b = ~b;
        REQUIRE(!b.AnyValuesSet());

        other.ClearAll();
        other.Set(64);
        b |= other;
        REQUIRE(b.Get(64));
        REQUIRE(other.Get(64));

        other.ClearAll();
        b &= other;
        REQUIRE(!b.AnyValuesSet());

        b.ClearAll();
        REQUIRE(b.NumSet() == 0);
        b.Set(0);
        b.Set(64);
        REQUIRE(b.NumSet() == 2);
    }

    {
        Bitset<8> const b(0b00101010);
        REQUIRE(b.Subsection<3>(2).elements[0] == 0b010);
    }

    {
        Bitset<8> const b(0b11110000);
        REQUIRE(!b.Get(0));
        REQUIRE(b.Get(7));
        REQUIRE(b.Subsection<4>(4).elements[0] == 0b1111);
    }

    {
        Bitset<8> const b(0b00100100);
        REQUIRE(b.Subsection<4>(2).elements[0] == 0b1001);
    }

    {
        Bitset<8> b(0b00000000);
        REQUIRE(b.FirstUnsetBit() == 0);
        b.Set(0);
        REQUIRE(b.FirstUnsetBit() == 1);
        b.Set(1);
        REQUIRE(b.FirstUnsetBit() == 2);
    }

    {
        // test FirstUnsetBit across element boundary
        Bitset<128> b {};
        for (usize i = 0; i < 128; ++i)
            b.Set(i);
        REQUIRE(b.FirstUnsetBit() == 128);
        b.Clear(127);
        REQUIRE(b.FirstUnsetBit() == 127);
        b.Clear(64);
        REQUIRE(b.FirstUnsetBit() == 64);
    }

    {
        Bitset<128> b {};
        for (usize i = 64; i < 128; ++i)
            b.Set(i);
        REQUIRE(b.NumSet() == 64);

        auto const sub = b.Subsection<10>(60);
        REQUIRE(sub.Get(0) == 0);
        REQUIRE(sub.Get(1) == 0);
        REQUIRE(sub.Get(2) == 0);
        REQUIRE(sub.Get(3) == 0);
        REQUIRE(sub.Get(4) != 0);

        auto const sub2 = b.Subsection<64>(64);
        REQUIRE(sub2.NumSet() == 64);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterBitsetTests) { REGISTER_TEST(TestBitset); }
