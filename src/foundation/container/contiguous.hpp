// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/universal_defs.hpp"

#define DEFINE_BASIC_ARRAY_METHODS(type, data, size, const_word)                                             \
    constexpr type* begin() const_word { return data; }                                                      \
    constexpr type* end() const_word { return data + size; }                                                 \
    ALWAYS_INLINE constexpr type& operator[](usize index) const_word {                                       \
        ASSERT_HOT(index < size);                                                                            \
        return data[index];                                                                                  \
    }

#define DEFINE_SPAN_INTERFACE_METHODS(type, data, size, const_word)                                          \
    constexpr Span<type> Items() const_word { return {data, size}; }                                         \
    constexpr operator Span<type>() const_word { return {data, size}; }

#define DEFINE_CONTIGUOUS_CONTAINER_METHODS(type_name, data, size)                                           \
    DEFINE_BASIC_ARRAY_METHODS(const type_name, data, size, const)                                           \
    DEFINE_BASIC_ARRAY_METHODS(type_name, data, size, )                                                      \
    DEFINE_SPAN_INTERFACE_METHODS(const type_name, data, size, const)                                        \
    DEFINE_SPAN_INTERFACE_METHODS(type_name, data, size, )

template <class ContainerType>
concept ContiguousContainer = requires(ContainerType a) {
    { a.size } -> Convertible<usize>;
    { a.data } -> Convertible<typename RemoveReference<ContainerType>::ValueType*>;
    a.begin();
    a.end();
};

template <typename Container1, typename Container2>
concept ContiguousContainerSimilarTo = ContiguousContainer<Container1> && ContiguousContainer<Container2> &&
                                       Same<RemoveCV<typename RemoveReference<Container1>::ValueType>,
                                            RemoveCV<typename RemoveReference<Container2>::ValueType>>;

template <typename Container>
concept ContiguousContainerOfContiguousContainers =
    ContiguousContainer<Container> && ContiguousContainer<typename Container::ValueType>;
