// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/contiguous.hpp"
#include "foundation/container/span.hpp"
#include "foundation/universal_defs.hpp"

PUBLIC constexpr usize Kb(usize kb) { return kb * 1024; }
PUBLIC constexpr usize Mb(usize mb) { return mb * 1024 * 1024; }

PUBLIC inline void ZeroMemory(Span<u8> bytes) {
    for (auto& b : bytes)
        b = 0;
}
PUBLIC inline void ZeroMemory(void* ptr, usize num_bytes) { ZeroMemory({(u8*)ptr, num_bytes}); }

PUBLIC inline void FillMemory(Span<u8> bytes, u8 value) {
    for (auto& b : bytes)
        b = value;
}
PUBLIC inline void FillMemory(void* ptr, u8 value, usize num_bytes) {
    FillMemory({(u8*)ptr, num_bytes}, value);
}

PUBLIC inline void CopyMemory(void* destination, void const* source, usize num_bytes) {
    for (auto const i : Range(num_bytes))
        ((u8*)destination)[i] = ((u8 const*)source)[i];
}

PUBLIC inline void CopyMemory(Span<u8> destination, Span<u8 const> source) {
    ASSERT(destination.size >= source.size);
    for (auto const i : Range(source.size))
        destination[i] = source[i];
}

// aka memmove
PUBLIC inline void MoveMemory(void* destination, void const* source, usize num_bytes) {
    if (destination == source) return;
    if (destination < source)
        for (auto const i : Range(num_bytes))
            ((u8*)destination)[i] = ((u8 const*)source)[i];
    else
        for (usize i = num_bytes - 1; i != usize(-1); --i)
            ((u8*)destination)[i] = ((u8 const*)source)[i];
}

PUBLIC inline bool MemoryIsEqual(void const* a, void const* b, usize num_bytes) {
    auto bytes_a = (u8 const*)a;
    auto bytes_b = (u8 const*)b;
    for (auto const i : Range(num_bytes))
        if (bytes_a[i] != bytes_b[i]) return false;
    return true;
}

constexpr usize NumBitsNeededToStore(unsigned long long val) {
    if (val == 0) return 1;
    return (sizeof(val) * 8) - (usize)__builtin_clzll(val);
}

static constexpr usize k_max_alignment = sizeof(void*) * 2;

// Minimum offset between two objects to avoid false sharing
// https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
// https://en.wikipedia.org/wiki/False_sharing
// The cppreference page suggests that, in certain cases, separating data accessed by multiple threads
// by this value can speed up things by 6x. FreeBSD's buf_ring.h uses this technique.
static constexpr usize k_destructive_interference_size = __GCC_DESTRUCTIVE_SIZE;

PUBLIC constexpr bool IsPowerOfTwo(usize v) { return !(v & (v - 1)); }
PUBLIC constexpr usize Power2Modulo(usize x, usize y) { return x & (y - 1); }
// https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2
PUBLIC constexpr u32 NextPowerOf2(u32 x) {
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
}

// Finds the next value that is aligned to 'alignment'
PUBLIC constexpr usize AlignForward(usize value, usize alignment) {
    return __builtin_align_up(value, alignment);
}

// returns true if the pointer is aligned to a multiple of 'alignment'
PUBLIC ALWAYS_INLINE constexpr bool IsAligned(void const* pointer, usize alignment) {
    return __builtin_is_aligned(pointer, alignment);
}

PUBLIC constexpr usize BytesToAddForAlignment(uintptr ptr, usize alignment) {
    ASSERT(IsPowerOfTwo(alignment));
    auto const m1 = alignment - 1;
    auto const aligned = ((ptr + m1) & ~m1);
    return aligned - ptr;
}

template <typename Type>
PUBLIC constexpr Span<u8 const> AsBytes(Type const& obj) {
    return {(u8 const*)&obj, sizeof(Type)};
}

template <typename DestType, typename Type>
PUBLIC constexpr void WriteAndIncrement(UnsignedInt auto& pos, DestType* dest, Type const& src) {
    if constexpr (requires(Type t) {
                      typename Type::ValueType;
                      requires sizeof(typename Type::ValueType) == sizeof(DestType);
                      { t.size } -> Convertible<usize>;
                      { t.data } -> Convertible<typename Type::ValueType const*>;
                  }) {
        __builtin_memcpy(&dest[pos], src.data, src.size * sizeof(DestType));
        pos += src.size;
    } else if constexpr (Fundamental<Type> && sizeof(Type) == sizeof(DestType)) {
        dest[pos] = (DestType)src;
        pos += 1;
    } else {
        static_assert(false);
    }
}

template <typename DestType, typename Type>
PUBLIC constexpr void WriteAndIncrement(UnsignedInt auto& pos, Span<DestType> dest, Type const& src) {
    WriteAndIncrement(pos, dest.data, src);
}

template <ContiguousContainer DestType, typename Type>
PUBLIC constexpr void WriteAndIncrement(UnsignedInt auto& pos, DestType& dest, Type const& src) {
    WriteAndIncrement(pos, dest.data, src);
}
