// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/cloneable.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/maths.hpp"

#include "contiguous.hpp"

struct Allocator;

template <typename Type>
struct Span {
    using ValueType = Type;

    DEFINE_BASIC_ARRAY_METHODS(Type, data, size, const)

    constexpr Span() = default;

    constexpr Span(Type* data, usize size) : data(data), size(size) {}

    constexpr Span(ContiguousContainerSimilarTo<Span> auto&& other) : data(other.data), size(other.size) {}

    constexpr Span<RemoveConst<Type>> Clone(Allocator& a, CloneType clone_type = CloneType::Shallow) const;

    template <usize N>
    constexpr Span(Type (&array_literal)[N]) : data(array_literal) {
        static_assert(N != 0);
        if constexpr (CharacterType<Type>) {
            ASSERT(array_literal[N - 1] == 0); // expecting null terminated
            size = N - 1;
        } else {
            size = N;
        }
    }

    // NOTE: be careful here, the padding bytes in a struct are not necessarily zeroed and therefore you may
    // get inconsistent results if you are reading it as a block of memory
    constexpr Span<u8> ToByteSpan() const { return {(u8*)data, SizeInBytes()}; }
    constexpr Span<u8 const> ToConstByteSpan() const { return {(u8 const*)data, SizeInBytes()}; }
    constexpr usize SizeInBytes() const { return size * sizeof(Type); }

    constexpr void RemovePrefix(usize n) {
        ASSERT(n <= size);
        data += n;
        size -= n;
    }

    constexpr void RemoveSuffix(usize n) {
        ASSERT(n <= size);
        size -= n;
    }

    constexpr Span SubSpan(usize offset, usize sub_size = LargestRepresentableValue<usize>()) const {
        if (sub_size == 0) return {data, 0};
        ASSERT(offset <= size);
        return {data + offset, Min(sub_size, size - offset)};
    }

    constexpr Span Suffix(usize suffix_size) const {
        ASSERT(suffix_size <= size);
        return {data + size - suffix_size, suffix_size};
    }

    Type* data {};
    usize size {};
};

template <typename Type, usize k_size>
struct StaticSpan {
    constexpr StaticSpan(Type* data) : data(data) {}

    DEFINE_BASIC_ARRAY_METHODS(Type, data, k_size, const)

    constexpr Span<Type> Items() { return {data, k_size}; }
    constexpr operator Span<Type>() { return Items(); }

    static constexpr usize size = k_size; // NOLINT(readability-identifier-naming)
    Type* data;
};

using String = Span<char const>;
using WString = Span<wchar_t const>;
using MutableString = Span<char>;
using MutableWString = Span<wchar_t>;

constexpr String operator""_s(char const* str, usize size) noexcept { return {str, size}; }
constexpr WString operator""_s(wchar_t const* str, usize size) noexcept { return {str, size}; }
