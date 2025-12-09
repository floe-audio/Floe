// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/span.hpp"
#include "foundation/universal_defs.hpp"

#include "contiguous.hpp"

// Wrapper around a C-style array.
// These arrays can be passed around in parameters. And they implicitly convert to a Span of the matching
// type. There's a couple of handy ways that you can initialise an array:
// 1. function(Array {"abc"_s, "def"}) here, the type of the array is determined by the first item - a String
// 2. function(ArrayT<String>({"abc", "def"})) here, we explicitly give the type

template <typename Type, usize k_size>
struct Array {
    using ValueType = Type;

    DEFINE_CONTIGUOUS_CONTAINER_METHODS(Type, data, k_size)

    constexpr StaticSpan<Type, k_size> StaticItems() const { return data; }
    constexpr operator StaticSpan<Type, k_size>() { return data; }
    constexpr operator StaticSpan<Type const, k_size>() const { return data; }

    static constexpr usize size = k_size; // NOLINT(readability-identifier-naming)
    Type data[k_size]; // Needs to be public in order for implicit aggregate initialisation to work
};

// you'll probably need to call placement-new on the items if they have constructors
template <TriviallyCopyable Type, usize k_size>
union UninitialisedArray {
    using ValueType = Type;

    UninitialisedArray() : raw_storage() {}

    // Assign/construct from an Array<Type, k_size>.
    UninitialisedArray(Array<Type, k_size> const& other) {
        for (usize i = 0; i < k_size; ++i)
            PLACEMENT_NEW(&data[i]) Type(other.data[i]);
    }
    UninitialisedArray(Array<Type, k_size>&& other) {
        for (usize i = 0; i < k_size; ++i)
            PLACEMENT_NEW(&data[i]) Type(Move(other.data[i]));
    }

    DEFINE_CONTIGUOUS_CONTAINER_METHODS(Type, data, k_size)

    constexpr StaticSpan<Type, k_size> StaticItems() const { return data; }
    constexpr operator StaticSpan<Type, k_size>() { return data; }
    constexpr operator StaticSpan<Type const, k_size>() { return data; }

    // Copy assignment to Array<Type, k_size>.
    constexpr operator Array<Type, k_size>() const {
        Array<Type, k_size> result;
        for (usize i = 0; i < k_size; ++i)
            PLACEMENT_NEW(&result.data[i]) Type(Move(data[i]));
        return result;
    }

    static constexpr usize size = k_size; // NOLINT(readability-identifier-naming)
    Type data[k_size]; // for easier debugging
    alignas(Type) u8 raw_storage[k_size * sizeof(Type)] = {};
};

namespace detail {

template <class T, usize N, usize... I>
static constexpr Array<RemoveCV<T>, N> MakeArrayHelper(T (&a)[N], IndexSequence<I...>) {
    return {{a[I]...}};
}
template <class T, usize N, usize... I>
static constexpr Array<RemoveCV<T>, N> MakeArrayHelper(T (&&a)[N], IndexSequence<I...>) {
    return {{Move(a[I])...}};
}

template <typename T, usize... Ix, typename... Args>
static constexpr auto MakeInitialisedArrayHelper(IndexSequence<Ix...>, Args&&... args) {
    return Array<T, sizeof...(Ix)> {{((void)Ix, T(Forward<Args>(args)...))...}};
}

} // namespace detail

template <typename T, usize N, typename... Args>
PUBLIC constexpr auto MakeInitialisedArray(Args&&... args) {
    return detail::MakeInitialisedArrayHelper<T>(MakeIndexSequence<N>(), Forward<Args>(args)...);
}

template <typename T, usize N>
class InitialisedArray : public Array<T, N> {
  public:
    template <typename... Args>
    InitialisedArray(Args&&... args)
        : Array<T, N>(detail::MakeInitialisedArrayHelper<T>(MakeIndexSequence<N>(), Forward<Args>(args)...)) {
    }
};

template <class InputIt, class Size, class OutputIt>
PUBLIC constexpr OutputIt CopyN(InputIt first, Size count, OutputIt result) {
    if (count > 0) {
        *result = *first;
        ++result;
        for (Size i = 1; i != count; ++i, ++result)
            *result = *++first;
    }

    return result;
}

template <typename Type, usize... sizes>
PUBLIC constexpr auto ConcatArrays(Array<Type, sizes> const&... arrays) {
    Array<Type, (sizes + ...)> result;
    usize index {};

    ((CopyN(arrays.begin(), sizes, result.begin() + index), index += sizes), ...);

    return result;
}

template <usize k_size>
struct CharArrayHelper {
    Array<char, k_size - 1> array;
    constexpr CharArrayHelper(char const (&literal)[k_size]) { CopyN(literal, k_size - 1, array.begin()); }
};

// creates a Array<char> from a string literal, these can be ConcatArray() into new strings
template <CharArrayHelper str>
PUBLIC constexpr auto operator""_ca() {
    return str.array;
}

template <class T, usize N>
PUBLIC constexpr Array<RemoveCV<T>, N> ArrayT(T (&a)[N]) {
    return detail::MakeArrayHelper(a, MakeIndexSequence<N> {});
}
template <class T, usize N>
PUBLIC constexpr Array<RemoveCV<T>, N> ArrayT(T (&&a)[N]) {
    return detail::MakeArrayHelper(Move(a), MakeIndexSequence<N> {});
}

// Allows for: Array array = {0, 1};
// or: function(Array {"str"_s, ""}) // where the first item of the {} determines the type
// This C++ feature is called a template deduction guide
template <typename T, typename... Args>
Array(T, Args...) -> Array<T, 1 + sizeof...(Args)>;
