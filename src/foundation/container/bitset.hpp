// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Similar idea to std::bitset

#pragma once

#include "foundation/container/array.hpp"
#include "foundation/universal_defs.hpp"

template <usize k_bits>
requires(k_bits != 0)
struct Bitset {
    using ElementType =
        Conditional<k_bits <= 8, u8, Conditional<k_bits <= 16, u16, Conditional<k_bits <= 32, u32, u64>>>;

    static constexpr usize k_bits_per_element = sizeof(ElementType) * 8;
    static constexpr usize k_num_elements =
        (k_bits / k_bits_per_element) + ((k_bits % k_bits_per_element == 0) ? 0 : 1);
    static constexpr usize k_max_element_index = k_num_elements - 1;
    using Bool = ElementType;

    constexpr Bitset() = default;
    constexpr Bitset(ElementType v) : elements() { elements[0] = v; }
    constexpr Bitset(ContiguousContainer auto const& bits) : elements() { SetBits(bits); }

    template <usize k_result_bits>
    Bitset<k_result_bits> Subsection(usize offset) const {
        auto temp = *this >> offset;
        Bitset<k_result_bits> result;
        for (auto const element_index : Range(Min(elements.size, result.elements.size)))
            result.elements[element_index] =
                (typename Bitset<k_result_bits>::ElementType)temp.elements[element_index];
        result.ClearTrailingBits();
        return result;
    }

    template <typename Function>
    void ForEachSetBit(Function&& function) const {
        for (auto const bit : Range(k_bits))
            if (Get(bit)) function(bit);
    }

    constexpr void SetToValue(usize bit, bool value) {
        if (value)
            Set(bit);
        else
            Clear(bit);
    }

    constexpr void SetBits(ContiguousContainer auto const& bits) {
        for (auto const bit : bits)
            Set((usize)bit);
    }

    constexpr void ClearBits(ContiguousContainer auto const& bits) {
        for (auto const bit : bits)
            Clear((usize)bit);
    }

    constexpr void Clear(usize bit) {
        ASSERT(bit < k_bits);
        elements[bit / k_bits_per_element] &= ~(ElementType(1) << bit % k_bits_per_element);
    }

    constexpr void Set(usize bit) {
        ASSERT(bit < k_bits);
        elements[bit / k_bits_per_element] |= ElementType(1) << bit % k_bits_per_element;
    }

    constexpr void Flip(usize bit) {
        elements[bit / k_bits_per_element] ^= ElementType(1) << bit % k_bits_per_element;
    }

    constexpr Bool Get(usize bit) const {
        ASSERT(bit < k_bits);
        return elements[bit / k_bits_per_element] & (ElementType(1) << bit % k_bits_per_element);
    }

    constexpr void ClearAll() { elements = {}; }
    constexpr void SetAll() {
        for (auto& element : elements)
            element = LargestRepresentableValue<ElementType>();
        ClearTrailingBits();
    }

    constexpr bool AnyValuesSet() const {
        for (auto& element : elements)
            if (element) return true;
        return false;
    }

    constexpr bool AllValuesSet() const { return NumSet() == k_bits; }

    constexpr bool AnySetInSpan(ContiguousContainer auto const& bits) const {
        for (auto const bit : bits)
            if (Get((usize)bit)) return true;
        return false;
    }

    constexpr usize NumSet() const {
        usize result = 0;
        for (auto& element : elements)
            result += Popcount(element);
        return result;
    }

    constexpr usize FirstUnsetBit() const {
        for (auto const element_index : Range(k_num_elements))
            if (elements[element_index] != ElementType(-1))
                return (element_index * k_bits_per_element) +
                       (usize)__builtin_ctzg((ElementType)~elements[element_index]);
        [[unlikely]] return k_bits;
    }

    constexpr Bitset operator~() const {
        Bitset result = *this;
        for (auto& element : result.elements)
            element = ~element;
        result.ClearTrailingBits();
        return result;
    }

    constexpr Bitset& operator&=(Bitset const& other) {
        for (auto const element_index : Range(k_num_elements))
            elements[element_index] &= other.elements[element_index];

        return *this;
    }

    constexpr Bitset& operator|=(Bitset const& other) {
        for (auto const element_index : Range(k_num_elements))
            elements[element_index] |= other.elements[element_index];

        return *this;
    }

    constexpr Bitset& operator^=(Bitset const& other) {
        for (auto const element_index : Range(k_num_elements))
            elements[element_index] ^= other.elements[element_index];

        return *this;
    }

    constexpr Bitset& operator<<=(usize shift) {
        auto const k_num_elementshift = shift / k_bits_per_element;
        if (k_num_elementshift != 0)
            for (auto i = k_max_element_index; i != usize(-1); --i)
                elements[i] = k_num_elementshift <= i ? elements[(i - k_num_elementshift)] : 0;

        if ((shift %= k_bits_per_element) != 0) { // 0 < shift < k_bits_per_element, shift by bits
            for (auto i = k_max_element_index; i != 0; --i)
                elements[i] = (elements[i] << shift) | (elements[i - 1] >> (k_bits_per_element - shift));

            elements[0] <<= shift;
        }
        ClearTrailingBits();
        return *this;
    }

    constexpr Bitset& operator>>=(usize shift) {
        auto const k_num_elementshift = shift / k_bits_per_element;
        if (k_num_elementshift != 0)
            for (usize i = 0; i <= k_max_element_index; ++i)
                elements[i] =
                    k_num_elementshift <= k_max_element_index - i ? elements[i + k_num_elementshift] : 0;

        if ((shift %= k_bits_per_element) != 0) { // 0 < shift < k_bits_per_element, shift by bits
            for (auto const element_index : Range(k_max_element_index))
                elements[element_index] =
                    (ElementType)(elements[element_index] >> shift) |
                    (ElementType)(elements[element_index + 1] << (k_bits_per_element - shift));

            elements[k_max_element_index] >>= shift;
        }
        return *this;
    }

    constexpr Bitset operator<<(usize shift) const {
        Bitset result = *this;
        result <<= shift;
        return result;
    }

    constexpr Bitset operator>>(usize shift) const {
        Bitset result = *this;
        result >>= shift;
        return result;
    }

    constexpr usize Size() const { return k_bits; }

    constexpr bool operator==(Bitset const& other) const { return elements == other.elements; }

    constexpr void ClearTrailingBits() {
        if constexpr ((k_bits % k_bits_per_element) == 0) return;
        elements[k_max_element_index] &= (ElementType(1) << (k_bits % k_bits_per_element)) - 1;
    }

    Array<ElementType, k_num_elements> elements = {};
};

template <usize k_bits>
constexpr Bitset<k_bits> operator&(Bitset<k_bits> const& lhs, Bitset<k_bits> const& rhs) {
    Bitset<k_bits> result = lhs;
    result &= rhs;
    return result;
}

template <usize k_bits>
constexpr Bitset<k_bits> operator|(Bitset<k_bits> const& lhs, Bitset<k_bits> const& rhs) {
    Bitset<k_bits> result = lhs;
    result |= rhs;
    return result;
}

template <usize k_bits>
constexpr Bitset<k_bits> operator^(Bitset<k_bits> const& lhs, Bitset<k_bits> const& rhs) {
    Bitset<k_bits> result = lhs;
    result ^= rhs;
    return result;
}
