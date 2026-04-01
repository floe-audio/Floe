// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// This file contains code from "Sane C++"
// https://github.com/Pagghiu/SaneCppLibraries
// Copyright (c) 2022 - present Stefano Cristiano <pagghiu@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/container/optional.hpp"
#include "foundation/universal_defs.hpp"

#include "config.h"

template <typename T, auto tag>
struct TypeAndTag {
    using Type = T;
    static constexpr auto k_tag = tag;
};

template <typename Type, typename TagType>
concept CompatibleTypeAndTag =
    TriviallyCopyable<typename Type::Type> && Same<RemoveConst<decltype(Type::k_tag)>, TagType>;

template <typename Type, typename... Ts>
concept TypeWithinTypeAndTags = IsTypePresentInVariadicArgs<Type, typename Ts::Type...>;

template <typename... TT>
struct TypeList {
    static constexpr int const k_size = sizeof...(TT);
};

template <typename T, int k_index, int k_initial_size = 0>
struct TypeListGet;

template <int k_index, int k_initial_index, typename T, typename... TT>
struct TypeListGet<TypeList<T, TT...>, k_index, k_initial_index> {
    /// The retrieved type at the specified index.
    using Type = Conditional<k_index == k_initial_index,
                             T,
                             typename TypeListGet<TypeList<TT...>, k_index, k_initial_index + 1>::Type>;
};

template <int k_index, int k_initial_index>
struct TypeListGet<TypeList<>, k_index, k_initial_index> {
    /// If the TypeList is empty or the index is out of bounds, the retrieved type is 'void'.
    using Type = void;
};

template <int k_index>
struct TypeListGet<TypeList<>, k_index, 0> {};

template <typename T, int k_index>
using TypeListGetT = typename TypeListGet<T, k_index>::Type;

template <typename TagType>
concept TaggedUnionDefaultConstructible = requires(TagType e) {
    { TaggedUnionDefaultValue(e) };
};

template <typename TagType, CompatibleTypeAndTag<TagType>... Ts>
class TaggedUnion {
    template <int k_index>
    using TypeAtIndex = TypeListGetT<TypeList<Ts...>, k_index>;

    static constexpr auto k_num_types = TypeList<Ts...>::k_size;

    template <TagType k_wanted_tag, int k_start_index = k_num_types>
    struct TagToType {
        static constexpr int k_index = k_wanted_tag == TypeAtIndex<k_start_index - 1>::k_tag
                                           ? k_start_index - 1
                                           : TagToType<k_wanted_tag, k_start_index - 1>::k_index;
        static_assert(k_index >= 0, "Type not found!");
        using Type = Conditional<k_wanted_tag == TypeAtIndex<k_start_index - 1>::k_tag, // Condition
                                 typename TypeAtIndex<k_start_index - 1>::Type, // True
                                 typename TagToType<k_wanted_tag, k_start_index - 1>::Type>; // False
    };

    template <TagType k_wanted_tag>
    struct TagToType<k_wanted_tag, 0> {
        using Type = typename TypeAtIndex<0>::Type;
        static constexpr int k_index = 0;
    };

    template <typename Type>
    static constexpr TagType TypeToTag() {
        TagType t;
        (SetTagIfTypesMatch<Type, Ts>(t), ...);
        return t;
    }

    template <typename Type, typename TypeAndTagType>
    static constexpr void SetTagIfTypesMatch(TagType& tag) {
        if constexpr (Same<Type, typename TypeAndTagType::Type>) tag = TypeAndTagType::k_tag;
    }

    template <typename TypeAndTagType>
    static constexpr void AssertTagIsntAssociatedWithData(TagType tag) {
        ASSERT(
            tag != TypeAndTagType::k_tag,
            "this tag has associated data; setting this object should be done using that data rather than a tag on its own");
    }

    template <int k_index>
    auto& GetFromTypeIndex() {
        using T = typename TypeAtIndex<k_index>::Type;
        return *(T*)storage;
    }

    template <int k_index>
    auto const& GetFromTypeIndex() const {
        using T = typename TypeAtIndex<k_index>::Type;
        return *(T const*)storage;
    }

    static constexpr usize k_data_size = LargestValueInTemplateArgs<sizeof(typename Ts::Type)...>::k_value;
    static constexpr usize k_data_align = LargestValueInTemplateArgs<alignof(typename Ts::Type)...>::k_value;

    template <TagType k_wanted_tag>
    constexpr void CallWithTypeIfTagMatches(auto&& function) const {
        if (k_wanted_tag == tag) return function(GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>());
    }

  public:
    TaggedUnion(TaggedUnion const& other) = default;
    TaggedUnion& operator=(TaggedUnion const& other) = default;

    TaggedUnion() requires TaggedUnionDefaultConstructible<TagType>
    {
        Set(TaggedUnionDefaultValue(TagType {}));
    }

    // Set with a type
    // ===========================================================
    template <TypeWithinTypeAndTags<Ts...> Type>
    TaggedUnion(Type v) {
        Set(v);
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    TaggedUnion& operator=(Type v) {
        Set(v);
        return *this;
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    void Set(Type v) {
        *(Type*)storage = v;
        tag = TypeToTag<Type>();
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    Type& ChangeToType() {
        tag = TypeToTag<Type>();
        return (Type*)storage;
    }

    // Set with a tag
    // ===========================================================
    TaggedUnion(TagType t) { Set(t); }

    TaggedUnion& operator=(TagType t) {
        Set(t);
        return *this;
    }

    void Set(TagType t) {
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) (AssertTagIsntAssociatedWithData<Ts>(t), ...);
        tag = t;
    }

    template <TagType k_wanted_tag>
    typename TypeAtIndex<TagToType<k_wanted_tag>::k_index>::Type& ChangeTo() {
        tag = k_wanted_tag;
        return GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>();
    }

    // Utils
    // ===========================================================
    template <TypeWithinTypeAndTags<Ts...> Type>
    bool Is() const {
        return TypeToTag<Type>() == tag;
    }

    bool operator==(TaggedUnion const& other) const {
        if (tag != other.tag) return false;

        Optional<bool> result;
        Visit([&](auto const& v) { result = v == other.Get<RemoveCVReference<decltype(v)>>(); });

        // If the tag has no associated data, we just compare the tags (which we already did)
        if (!result) return true;
        return *result;
    }

    bool operator!=(TaggedUnion const& other) const {
        if (tag != other.tag) return true;

        Optional<bool> result;
        Visit([&](auto const& v) { result = v != other.Get<RemoveCVReference<decltype(v)>>(); });

        // If the tag has no associated data, we just compare the tags (which we already did)
        if (!result) return false;
        return *result;
    }

    bool operator==(TagType t) const {
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) (AssertTagIsntAssociatedWithData<Ts>(t), ...);
        return t == tag;
    }

    // Get from a type
    // ===========================================================
    template <TypeWithinTypeAndTags<Ts...> Type>
    Type* TryGetMut() const {
        if (Is<Type>()) return (Type*)storage;
        return nullptr;
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    Type* TryGet() {
        if (Is<Type>()) return (Type*)storage;
        return nullptr;
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    Type const* TryGet() const {
        if (Is<Type>()) return (Type const*)storage;
        return nullptr;
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    Optional<Type> TryGetOpt() const {
        if (Is<Type>()) return *(Type*)storage;
        return k_nullopt;
    }

    template <TypeWithinTypeAndTags<Ts...> Type>
    Type& Get() {
        ASSERT(TypeToTag<Type>() == tag);
        return *(Type*)storage;
    }
    template <TypeWithinTypeAndTags<Ts...> Type>
    Type const& Get() const {
        ASSERT(TypeToTag<Type>() == tag);
        return *(Type const*)storage;
    }

    // Get from a tag
    // ===========================================================
    template <TagType k_wanted_tag>
    typename TypeAtIndex<TagToType<k_wanted_tag>::k_index>::Type* TryGetFromTag() {
        if (k_wanted_tag == tag) return &GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>();
        return nullptr;
    }

    template <TagType k_wanted_tag>
    typename TypeAtIndex<TagToType<k_wanted_tag>::k_index>::Type const* TryGetFromTag() const {
        if (k_wanted_tag == tag) return &GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>();
        return nullptr;
    }

    template <TagType k_wanted_tag>
    typename TypeAtIndex<TagToType<k_wanted_tag>::k_index>::Type& GetFromTag() {
        return GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>();
    }

    template <TagType k_wanted_tag>
    typename TypeAtIndex<TagToType<k_wanted_tag>::k_index>::Type const& GetFromTag() const {
        return GetFromTypeIndex<TagToType<k_wanted_tag>::k_index>();
    }

    // ===========================================================
    void Visit(auto&& function) const { (CallWithTypeIfTagMatches<Ts::k_tag>(function), ...); }

    // don't modify these directly, use the methods
    // ===========================================================
    TagType tag;
    alignas(k_data_align) u8 storage[k_data_size];
};
