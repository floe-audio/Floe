// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

template <usize k_bits>
requires(k_bits != 0)
class AtomicBitset {
  public:
    static constexpr usize k_bits_per_element = sizeof(u64) * 8;
    static constexpr ptrdiff_t k_num_elements =
        (k_bits / k_bits_per_element) + ((k_bits % k_bits_per_element == 0) ? 0 : 1);
    using Bool64 = u64;

    AtomicBitset() {}

    void SetToValue(usize bit, bool value) {
        if (value)
            Set(bit);
        else
            Clear(bit);
    }

    Bool64 Clear(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchAnd(~mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Set(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchOr(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Flip(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchXor(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Get(usize bit) const {
        ASSERT(bit < k_bits);
        return m_elements[bit / k_bits_per_element].Load(LoadMemoryOrder::Relaxed) &
               (u64(1) << bit % k_bits_per_element);
    }

    // NOTE: these Blockwise methods are not atomic in terms of the _whole_ bitset, but they will be atomic in
    // regard to each 64-bit block - and that might be good enough for some needs

    void AssignBlockwise(Bitset<k_bits> other) {
        auto const other_raw = other.elements;
        for (auto const element_index : Range(m_elements.size))
            m_elements[element_index].Store(other_raw[element_index], StoreMemoryOrder::Relaxed);
    }

    Bitset<k_bits> GetBlockwise() const {
        Bitset<k_bits> result;
        for (auto const element_index : Range(m_elements.size))
            result.elements[element_index] = m_elements[element_index].Load(LoadMemoryOrder::Relaxed);
        return result;
    }

    void SetAllBlockwise() {
        for (auto& block : m_elements)
            block.store(~(u64)0);
    }

    void ClearAllBlockwise() {
        for (auto& block : m_elements)
            block.store(0);
    }

  private:
    Array<Atomic<u64>, k_num_elements> m_elements {};
};
