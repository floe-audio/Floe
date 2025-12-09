// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/error/error_code.hpp"
#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/writer.hpp"

#include "contiguous.hpp"

namespace dyn {

template <typename ContainerType>
concept DynArray = requires(ContainerType a, ContainerType const b) {
    { a.size } -> Convertible<usize>;
    { a.data } -> Convertible<typename ContainerType::ValueType*>;
    { a.Items() } -> Same<Span<typename ContainerType::ValueType>>;
    { a.Reserve((usize)0) } -> Same<bool>;
    a.ResizeWithoutCtorDtor((usize)0);
};

template <DynArray DynType>
using SpanFor = Span<typename DynType::ValueType const>;

template <DynArray DynType>
using MutableSpanFor = Span<typename DynType::ValueType>;

template <typename Type>
PUBLIC constexpr void CallDestructors(Span<Type> items) {
    if constexpr (!Fundamental<Type>)
        for (auto& item : items)
            item.~Type();
    if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
        constexpr u8 k_garbage = 0xd0;
        FillMemory(items.ToByteSpan(), k_garbage);
    }
}

template <typename Type>
PUBLIC constexpr void CallConstructors(Span<Type> items) {
    if constexpr (!Fundamental<Type>)
        for (auto& item : items)
            PLACEMENT_NEW(&item) Type();
}

// You must initialise the elements in the gap that this creates (using placement-new)
PUBLIC constexpr bool MakeUninitialisedGap(DynArray auto& array, usize pos, usize count) {
    using ValueType = typename RemoveReference<decltype(array)>::ValueType;

    if (pos > array.size) return false;
    auto const initial_size = array.size;
    auto const desired_size = initial_size + count;
    if (!array.Reserve(desired_size)) return false;
    array.ResizeWithoutCtorDtor(desired_size);

    for (usize i = initial_size - 1; i != (pos - 1); --i) {
        auto& old_item = array.data[i];
        PLACEMENT_NEW(array.data + (i + count)) ValueType(Move(old_item));
        old_item.~ValueType(); // Destroy moved-from object
    }

    return true;
}

PUBLIC constexpr bool MakeUninitialisedGapAtEnd(DynArray auto& array, usize count) {
    auto const desired_size = array.size + count;
    if (!array.Reserve(desired_size)) return false;
    array.ResizeWithoutCtorDtor(desired_size);
    return true;
}

PUBLIC constexpr bool Resize(DynArray auto& array, usize new_size) {
    auto const delta = (s64)new_size - (s64)array.size;
    if (delta == 0) return true;

    if (!array.Reserve(new_size)) return false;
    if (delta < 0) {
        CallDestructors(array.Items().SubSpan(new_size));
        array.ResizeWithoutCtorDtor(new_size);
    } else {
        array.ResizeWithoutCtorDtor(new_size);
        CallConstructors(array.Items().SubSpan(new_size - (usize)delta));
    }
    return true;
}

PUBLIC constexpr void Pop(DynArray auto& array, usize num_to_pop_from_end = 1) {
    ASSERT(array.size >= num_to_pop_from_end);
    if (num_to_pop_from_end == 0) return;
    auto const new_size = array.size - num_to_pop_from_end;
    CallDestructors(array.Items().SubSpan(new_size));
    array.ResizeWithoutCtorDtor(new_size);
}

PUBLIC constexpr void Clear(DynArray auto& array) {
    if (!array.size) return;
    CallDestructors(array.Items());
    array.ResizeWithoutCtorDtor(0);
}

template <DynArray DynType>
PUBLIC constexpr bool AssignAssumingAlreadyEmpty(DynType& array, SpanFor<DynType> new_items) {
    if (!array.Reserve(new_items.size)) return false;
    array.ResizeWithoutCtorDtor(new_items.size);
    for (auto const item_index : Range(new_items.size))
        PLACEMENT_NEW(array.data + item_index) DynType::ValueType(new_items[item_index]);
    return true;
}

template <DynArray DynType>
PUBLIC constexpr bool Assign(DynType& array, SpanFor<DynType> new_items) {
    if (array.size) CallDestructors(array.Items());
    return dyn::AssignAssumingAlreadyEmpty(array, new_items);
}

template <DynArray DynType>
PUBLIC constexpr void AssignFitInCapacity(DynType& array, SpanFor<DynType> new_items) {
    Assign(array, new_items.SubSpan(0, array.Capacity()));
}

template <class... Args>
PUBLIC constexpr bool AssignRepeated(DynArray auto& array, usize count, Args&&... args) {
    using ValueType = typename RemoveReference<decltype(array)>::ValueType;
    CallDestructors(array.Items());
    if (!array.Reserve(count)) return false;
    array.ResizeWithoutCtorDtor(count);
    for (auto const item_index : Range(count))
        PLACEMENT_NEW(array.data + item_index) ValueType(Forward<Args>(args)...);
    return true;
}

template <DynArray DynType>
PUBLIC constexpr bool MoveAssignAssumingAlreadyEmpty(DynType& array, MutableSpanFor<DynType> data) {
    if (!array.Reserve(data.size)) return false;
    array.ResizeWithoutCtorDtor(data.size);
    for (auto const item_index : Range(data.size))
        PLACEMENT_NEW(array.data + item_index) DynType::ValueType(Move(data[item_index]));
    return true;
}

template <DynArray DynType>
PUBLIC constexpr bool MoveAssign(DynType& array, MutableSpanFor<DynType> new_items) {
    if (array.size) CallDestructors(array.Items());
    return ArrayMoveAssignAssumingAlreadyEmpty(array, new_items);
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr bool Append(DynType& array, ArgumentType&& value) {
    if (!MakeUninitialisedGapAtEnd(array, 1)) return false;
    PLACEMENT_NEW(array.data + (array.size - 1)) DynType::ValueType(Forward<ArgumentType>(value));
    return true;
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr void AppendAssumeCapacity(DynType& array, ArgumentType&& value) {
    PLACEMENT_NEW(array.data + array.size) DynType::ValueType(Forward<ArgumentType>(value));
    array.ResizeWithoutCtorDtor(array.size + 1);
}

template <DynArray DynType>
PUBLIC constexpr bool AppendSpan(DynType& array, SpanFor<DynType> new_items) {
    if (!MakeUninitialisedGapAtEnd(array, new_items.size)) return false;
    auto write_ptr = array.data + (array.size - new_items.size);
    for (auto const new_items_index : Range(new_items.size))
        PLACEMENT_NEW(write_ptr + new_items_index) DynType::ValueType(new_items[new_items_index]);
    return true;
}

template <DynArray DynType>
PUBLIC constexpr bool AppendSpanAssumeCapacity(DynType& array, SpanFor<DynType> new_items) {
    auto write_ptr = array.data + array.size;
    for (auto const new_items_index : Range(new_items.size))
        PLACEMENT_NEW(write_ptr + new_items_index) DynType::ValueType(new_items[new_items_index]);
    array.ResizeWithoutCtorDtor(array.size + new_items.size);
    return true;
}

// Returns true if item was added
template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr bool AppendIfNotAlreadyThere(DynType& array, ArgumentType&& v) {
    if (!Find(array, v)) return dyn::Append(array, Forward<ArgumentType>(v));
    return false;
}

template <class... Args>
PUBLIC constexpr bool Emplace(DynArray auto& array, Args&&... args) {
    using Type = typename RemoveReference<decltype(array)>::ValueType;
    if (!MakeUninitialisedGapAtEnd(array, 1)) return false;
    PLACEMENT_NEW(array.data + (array.size - 1)) Type(Forward<Args>(args)...);
    return true;
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr bool Insert(DynType& array, usize pos, ArgumentType&& value) {
    using Type = typename RemoveReference<decltype(array)>::ValueType;
    if (!MakeUninitialisedGap(array, pos, 1)) return false;
    PLACEMENT_NEW(array.data + pos) Type(Forward<ArgumentType>(value));
    return true;
}

template <DynArray DynType>
PUBLIC constexpr bool InsertSpan(DynType& array, usize pos, SpanFor<DynType> data) {
    if (!MakeUninitialisedGap(array, pos, data.size)) return false;
    for (auto const data_index : Range(data.size))
        PLACEMENT_NEW(array.data + pos + data_index) DynType::ValueType(data[data_index]);
    return true;
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr bool InsertRepeated(DynType& array, usize pos, usize count, ArgumentType const& v) {
    if (!MakeUninitialisedGap(array, pos, count)) return false;
    for (auto const index : Range(count))
        PLACEMENT_NEW(array.data + pos + index) DynType::ValueType(v);
    return true;
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC bool Prepend(DynType& array, ArgumentType&& value) {
    return dyn::Insert(array, 0, Forward<ArgumentType>(value));
}

template <DynArray DynType>
PUBLIC bool PrependSpan(DynType& array, SpanFor<DynType> items) {
    return dyn::InsertSpan(array, 0, items);
}

PUBLIC constexpr void Remove(DynArray auto& array, usize index, usize count = 1) {
    using Type = typename RemoveReference<decltype(array)>::ValueType;

    if (count == 0) return;
    if (index >= array.size) return;
    if (index + count > array.size) count = array.size - index;

    auto& data = array.data;

    for (usize i = index; i < (index + count); ++i)
        data[i].~Type();

    for (usize i = index + count; i < array.size; ++i) {
        PLACEMENT_NEW(data + (i - count)) Type(Move(data[i]));
        data[i].~Type();
    }
    array.ResizeWithoutCtorDtor(array.size - count);
}

PUBLIC constexpr void RemoveSwapLast(DynArray auto& array, usize index) {
    using Type = typename RemoveReference<decltype(array)>::ValueType;

    if (index >= array.size) return;

    ASSERT(array.size);
    if (index != (array.size - 1)) Swap(array.data[index], array.data[array.size - 1]);

    array.data[array.size - 1].~Type();
    array.ResizeWithoutCtorDtor(array.size - 1);
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr usize RemoveValue(DynType& array, ArgumentType const& value) {
    usize num_removed = 0;
    for (usize i = 0; i < array.size;)
        if (array.data[i] == value) {
            ++num_removed;
            dyn::Remove(array, i);
        } else {
            ++i;
        }
    return num_removed;
}

PUBLIC constexpr usize RemoveValueIf(DynArray auto& array, auto&& should_remove_element) {
    usize num_removed = 0;
    for (usize i = 0; i < array.size;) {
        if (should_remove_element(array.data[i])) {
            dyn::Remove(array, i);
            ++num_removed;
        } else {
            ++i;
        }
    }
    return num_removed;
}

template <DynArray DynType, typename ArgumentType = DynType::ValueType>
PUBLIC constexpr usize RemoveValueSwapLast(DynType& array, ArgumentType const& value) {
    usize num_removed = 0;
    for (usize i = 0; i < array.size;)
        if (array.data[i] == value) {
            ++num_removed;
            dyn::RemoveSwapLast(array, i);
        } else {
            ++i;
        }
    return num_removed;
}

PUBLIC constexpr usize RemoveValueIfSwapLast(DynArray auto& array, auto&& should_remove_element) {
    usize num_removed = 0;
    for (usize i = 0; i < array.size;) {
        if (should_remove_element(array.data[i])) {
            ++num_removed;
            dyn::RemoveSwapLast(array, i);
        } else {
            ++i;
        }
    }
    return num_removed;
}

template <DynArray DynType>
PUBLIC constexpr usize
Replace(DynType& array,
        ContiguousContainerSimilarTo<Span<typename DynType::ValueType>> auto const& search_span,
        ContiguousContainerSimilarTo<Span<typename DynType::ValueType>> auto const& replacement_span) {
    if (search_span.size > array.size) return 0;
    if (search_span.size == 0) return 0;

    usize num_replaced = 0;
    for (auto const array_index : Range(array.size - (search_span.size - 1))) {
        bool match = true;
        for (auto const search_index : Range(search_span.size)) {
            if (array.data[array_index + search_index] != search_span[search_index]) {
                match = false;
                break;
            }
        }
        if (match) {
            dyn::Remove(array, array_index, search_span.size);
            dyn::InsertSpan(array, array_index, replacement_span);
            ++num_replaced;
        }
    }
    return num_replaced;
}

template <DynArray DynType>
requires(Integral<typename DynType::ValueType>)
PUBLIC DynType::ValueType* NullTerminated(DynType& array) {
    if (array.Capacity() < (array.size + 1)) {
        if (!array.Reserve(array.size + 1)) {
            if (array.Capacity()) {
                // NOTE: we overwrite the last element with a null terminator
                array.data[array.Capacity() - 1] = 0;
                return array.data;
            } else {
                static typename DynType::ValueType term = {};
                return &term;
            }
        }
    }
    array.data[array.size] = 0;
    return array.data;
}

template <DynArray DynType>
requires(CharacterType<typename DynType::ValueType>)
PUBLIC constexpr void TrimWhitespace(DynType& array) {
    auto trimmed = WhitespaceStripped(array.Items());
    dyn::Remove(array, 0, (usize)(trimmed.data - array.data));
    ASSERT(array.size >= trimmed.size);
    dyn::Pop(array, array.size - trimmed.size);
}

template <DynArray DynType>
requires(sizeof(typename DynType::ValueType) == 1)
PUBLIC constexpr Writer WriterFor(DynType& array) {
    Writer result;
    result.Set<DynType>(array, [](DynType& arr, Span<u8 const> bytes) -> ErrorCodeOr<void> {
        AppendSpan(arr, {(typename DynType::ValueType const*)bytes.data, bytes.size});
        return k_success;
    });
    return result;
}

} // namespace dyn

// move-only allocated resizeable array
template <typename Type>
requires(!Const<Type>)
struct DynamicArray {
    NON_COPYABLE(DynamicArray);
    DEFINE_CONTIGUOUS_CONTAINER_METHODS(Type, data, size)

    using ValueType = Type;

    constexpr DynamicArray(Allocator& allocator) : allocator(allocator) {}

    constexpr DynamicArray(Span<Type const> span, Allocator& allocator) : allocator(allocator) {
        auto const added = dyn::AssignAssumingAlreadyEmpty(*this, span);
        ASSERT(added);
    }

    constexpr DynamicArray(DynamicArray&& other) : allocator(other.allocator) {
        items = other.items;
        capacity_bytes = other.capacity_bytes;
        other.items = {};
        other.capacity_bytes = {};
    }

    constexpr ~DynamicArray() {
        dyn::CallDestructors(items);
        if (capacity_bytes) allocator.Free(AllocatedSpan());
    }

    constexpr DynamicArray& operator=(DynamicArray&& other) {
        if (!other.size) {
            dyn::Clear(*this);
            return *this;
        }

        if (&allocator == &other.allocator) {
            dyn::Clear(*this);
            if (capacity_bytes) allocator.Free(AllocatedSpan());
            items = other.items;
            capacity_bytes = other.capacity_bytes;
            other.items = {};
            other.capacity_bytes = {};
        } else {
            dyn::CallDestructors(Items());
            Reserve(other.size);
            for (auto const item_index : Range(other.size))
                PLACEMENT_NEW(items.data + item_index) Type(Move(other.data[item_index]));
            ResizeWithoutCtorDtor(other.size);
            dyn::Clear(other);
        }

        return *this;
    }

    constexpr DynamicArray Clone(Allocator& a, CloneType clone_type) const {
        auto cloned_items = const_cast<DynamicArray&>(*this).Items().Clone(a, clone_type);
        return DynamicArray::FromOwnedSpan(cloned_items, a);
    }

    // Returns a span that you must free with the Allocator() of this class. You must also call the
    // destructors on the elements if needed.
    constexpr Span<Type> ToOwnedSpan() {
        ShrinkToFit();
        auto const result = items;
        items = {};
        capacity_bytes = 0;
        return result;
    }

    struct OwnedSpan {
        Span<Type> items;
        usize capacity;
    };

    constexpr OwnedSpan ToOwnedSpanUnchangedCapacity() {
        auto const result = items;
        auto const capacity = Capacity();
        items = {};
        capacity_bytes = 0;
        return {result, capacity};
    }

    constexpr Span<Type const> ToConstOwnedSpan() { return ToOwnedSpan().ToConst(); }

    constexpr void ShrinkToFit() {
        auto const initial_size = size;
        if (Capacity() > initial_size) {
            if (initial_size) {
                auto shrunk_data = allocator.Resize({
                    .allocation = AllocatedSpan(),
                    .new_size = initial_size * sizeof(Type),
                    .move_memory_handler = Allocator::MoveMemoryHandlerForType<Type>(&initial_size),
                });
                items = {CheckedPointerCast<Type*>(shrunk_data.data), initial_size};
                capacity_bytes = shrunk_data.size;
            } else {
                allocator.Free(AllocatedSpan());
                items = {};
                capacity_bytes = 0;
            }
        }
    }

    constexpr void ClearAndFree() {
        dyn::Clear(*this);
        if (capacity_bytes) allocator.Free(AllocatedSpan());
        items = {};
        capacity_bytes = {};
    }

    constexpr bool Reserve(usize new_capacity) {
        auto const current_capacity = Capacity();
        if (new_capacity <= current_capacity) return true;
        new_capacity = Max<usize>(4, current_capacity + (current_capacity / 2), new_capacity);

        auto mem = allocator.Reallocate<Type>(new_capacity, AllocatedSpan(), size, true);
        ASSERT(mem.data != nullptr);
        if (mem.data == nullptr) return false;

        items = {CheckedPointerCast<Type*>(mem.data), items.size};
        capacity_bytes = mem.size;
        return true;
    }

    constexpr void ResizeWithoutCtorDtor(usize new_size) {
        ASSERT(new_size <= Capacity());
        items = {items.data, new_size};
    }

    // span must have been created with allocator
    static constexpr DynamicArray FromOwnedSpan(Span<Type> span, ::Allocator& allocator) {
        return FromOwnedSpan(span, span.size, allocator);
    }
    static constexpr DynamicArray FromOwnedSpan(Span<Type> span, usize capacity, ::Allocator& allocator) {
        DynamicArray result {allocator};
        result.items = span;
        result.capacity_bytes = capacity * sizeof(Type);
        return result;
    }

    constexpr void TakeOwnership(Span<Type> allocated_data) {
        ClearAndFree();
        items = allocated_data;
        capacity_bytes = allocated_data.Size() * sizeof(Type);
    }

    constexpr usize Capacity() const { return capacity_bytes / sizeof(Type); }
    constexpr Span<u8> AllocatedSpan() const { return {(u8*)items.data, capacity_bytes}; }

    union {
        Span<Type> items {};
        struct {
            Type* data;
            usize size;
        };
    };
    ::Allocator& allocator;
    usize capacity_bytes {};
};

template <typename Type, usize k_capacity>
requires(!Const<Type>)
struct DynamicArrayBounded {
    using ValueType = Type;

    PROPAGATE_TRIVIALLY_COPYABLE(DynamicArrayBounded, Type);
    DEFINE_CONTIGUOUS_CONTAINER_METHODS(Type, data, size)

    constexpr DynamicArrayBounded() = default;

    constexpr DynamicArrayBounded(Span<Type const> data) {
        ASSERT(dyn::AssignAssumingAlreadyEmpty(*this, data));
    }

    template <usize k_array_capacity>
    requires(k_array_capacity <= k_capacity)
    constexpr DynamicArrayBounded(Array<Type, k_array_capacity> const& array) {
        ASSERT(dyn::AssignAssumingAlreadyEmpty(*this, array.Items()));
    }

    constexpr DynamicArrayBounded(DynamicArrayBounded const& other) {
        dyn::AssignAssumingAlreadyEmpty(*this, other.Items());
    }

    constexpr DynamicArrayBounded(DynamicArrayBounded&& other) {
        dyn::MoveAssignAssumingAlreadyEmpty(*this, other.Items());
        dyn::Clear(other);
    }

    constexpr DynamicArrayBounded& operator=(DynamicArrayBounded const& other) {
        ASSERT(dyn::Assign(*this, other.Items()));
        return *this;
    }

    constexpr DynamicArrayBounded& operator=(DynamicArrayBounded&& other) {
        ASSERT(dyn::MoveAssign(*this, other.Items()));
        dyn::Clear(other);
        return *this;
    }

    constexpr ~DynamicArrayBounded() { dyn::CallDestructors(Items()); }

    constexpr bool Reserve(usize capacity) { return capacity <= k_capacity; }

    constexpr void ResizeWithoutCtorDtor(usize new_size) {
        ASSERT(new_size <= k_capacity);
        size = new_size;
    }

    static constexpr usize Capacity() { return k_capacity; }

    struct Storage {
        PROPAGATE_TRIVIALLY_COPYABLE(Storage, Type);

        constexpr Storage() : storage() {}
        constexpr ~Storage() {}

        constexpr operator Type*() { return (Type*)storage; }
        constexpr operator Type const*() const { return (Type const*)storage; }

        alignas(Type) u8 storage[k_capacity * sizeof(Type)];
    };

    // This allows for use in constexpr contexts.
    struct TrivialStorage {
        PROPAGATE_TRIVIALLY_COPYABLE(TrivialStorage, Type);

        constexpr TrivialStorage() = default;
        constexpr ~TrivialStorage() {}

        constexpr operator Type*() { return storage; }
        constexpr operator Type const*() const { return storage; }

        Type storage[k_capacity] {};
    };

    Conditional<TriviallyCopyable<Type> && DefaultConstructible<Type>, TrivialStorage, Storage> data {};
    usize size {};
};
