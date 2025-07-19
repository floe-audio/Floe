// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/span.hpp"
#include "foundation/container/tagged_union.hpp"
#include "foundation/memory/cloneable.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/algorithm.hpp"
#include "foundation/utils/linked_list.hpp"
#include "foundation/utils/memory.hpp"

enum class AllocatorCommand {
    Allocate,
    Free,
    Resize,
};

struct AllocateCommand {
    usize size {};
    usize alignment {};
    bool allow_oversized_result {};
};

struct FreeCommand {
    Span<u8> allocation {};
};

struct ResizeCommand {
    struct MoveMemoryHandler {
        struct Args {
            void* context;
            void* destination;
            void* source;
            usize num_bytes;
        };
        void* context {};
        void (*function)(Args args) = nullptr;
    };

    Span<u8> allocation {};
    usize new_size {};
    bool allow_oversize_result {};
    MoveMemoryHandler move_memory_handler {};
};

using AllocatorCommandUnion = TaggedUnion<AllocatorCommand,
                                          TypeAndTag<AllocateCommand, AllocatorCommand::Allocate>,
                                          TypeAndTag<FreeCommand, AllocatorCommand::Free>,
                                          TypeAndTag<ResizeCommand, AllocatorCommand::Resize>>;

struct Allocator {
    virtual ~Allocator() = default;
    virtual Span<u8> DoCommand(AllocatorCommandUnion const& command) = 0;

    Span<u8> Allocate(AllocateCommand const& command) { return DoCommand(command); }
    void Free(Span<u8> data) { DoCommand(FreeCommand {.allocation = data}); }
    [[nodiscard]] Span<u8> Resize(ResizeCommand const& command) { return DoCommand(command); }

    template <typename Type>
    [[nodiscard]] Span<Type> ResizeType(Span<Type> data, usize num_used, usize new_size) {
        auto result = Resize({
            .allocation = data.ToByteSpan(),
            .new_size = new_size * sizeof(Type),
            .allow_oversize_result = false,
            .move_memory_handler = MoveMemoryHandlerForType<Type>(&num_used),
        });
        return {CheckedPointerCast<Type*>(result.data), result.size / sizeof(Type)};
    }

    // Allocate uninitialised memory for the given type. It may return more than num_to_allocate. It might
    // return a block that is large and not divisible by sizeof(Type).
    template <typename Type>
    Span<u8> AllocateBytesForTypeOversizeAllowed(usize num_to_allocate = 1) {
        if (!num_to_allocate) return {};
        return Allocate({
            .size = num_to_allocate * sizeof(Type),
            .alignment = alignof(Type),
            .allow_oversized_result = true,
        });
    }

    // Allocate uninitialised memory for the given type. It will return exactly the number that you request.
    // If it's a type that has a constructor, you need to call placement-new on each item of the result.
    template <typename Type>
    Span<Type> AllocateExactSizeUninitialised(usize num_to_allocate = 1) {
        if (!num_to_allocate) return {};
        auto result = Allocate({
            .size = num_to_allocate * sizeof(Type),
            .alignment = alignof(Type),
            .allow_oversized_result = false,
        });
        return {CheckedPointerCast<Type*>(result.data), num_to_allocate};
    }

    template <typename Type>
    Type* NewUninitialised() {
        auto result = (Type*)(void*)Allocate({
                                                 .size = sizeof(Type),
                                                 .alignment = alignof(Type),
                                                 .allow_oversized_result = false,
                                             })
                          .data;
        return result;
    }

    template <typename Type, typename... Args>
    Type* New(Args&&... args) {
        auto result = (Type*)(void*)Allocate({
                                                 .size = sizeof(Type),
                                                 .alignment = alignof(Type),
                                                 .allow_oversized_result = false,
                                             })
                          .data;
        PLACEMENT_NEW(result) Type(Forward<Args>(args)...);
        return result;
    }

    // Allocates default-initialised objects of exactly num_to_allocate.
    template <typename Type>
    Span<Type> NewMultiple(usize num_to_allocate) {
        if (!num_to_allocate) return {};

        auto result = AllocateExactSizeUninitialised<Type>(num_to_allocate);
        for (auto& i : result)
            PLACEMENT_NEW(&i) Type();

        return result;
    }

    template <typename Type>
    auto Clone(Type const& container, CloneType clone_type = CloneType::Shallow) {
        using ValueType = RemoveConst<typename Type::ValueType>;
        static_assert(TriviallyCopyable<ValueType> || Cloneable<ValueType>);

        if (!container.size) return Span<ValueType> {};
        auto result = AllocateExactSizeUninitialised<ValueType>(container.size);

        switch (clone_type) {
            case CloneType::Shallow: {
                for (auto const i : Range(container.size))
                    PLACEMENT_NEW(&result[i]) ValueType(container[i]);
                break;
            }
            case CloneType::Deep: {
                for (auto const i : Range(container.size))
                    if constexpr (Cloneable<ValueType>)
                        PLACEMENT_NEW(&result[i]) ValueType(container[i].Clone(*this, CloneType::Deep));
                    else if constexpr (TriviallyCopyable<ValueType>)
                        PLACEMENT_NEW(&result[i]) ValueType(container[i]);
                    else
                        PanicIfReached();

                break;
            }
        }
        return result;
    }

    template <typename Type>
    static ResizeCommand::MoveMemoryHandler
    MoveMemoryHandlerForType(usize const* num_used_in_existing_allocation) {
        return {
            .context = (void*)num_used_in_existing_allocation,
            .function =
                [](ResizeCommand::MoveMemoryHandler::Args args) {
                    auto const num_objects_used = (decltype(num_used_in_existing_allocation))args.context;
                    if constexpr (TriviallyCopyable<Type>) {
                        CopyMemory(args.destination, args.source, *num_objects_used * sizeof(Type));
                    } else {
                        static_assert(
                            MoveConstructible<Type>,
                            "When reallocating memory, the Allocator is allowed to create a new block of memory and move the old data into the new block. Therefore your type needs to be move-constructible.");

                        auto dst = (Type*)args.destination;
                        auto src = (Type*)args.source;
                        for (auto const i : Range(*num_objects_used)) {
                            PLACEMENT_NEW(dst + i) Type(Move(src[i]));
                            src[i].~Type();
                        }
                    }
                },
        };
    }

    // Reallocates an existing allocation. The existing allocation can be empty, in which case it will just do
    // a new allocation. You can specify how many objects of the existing memory that you have initialised, if
    // the allocator cannot reallocate in-place, it will MOVE the objects from the old allocation to the new,
    // and free the old allocation.
    template <typename Type>
    Span<u8> Reallocate(usize num_to_allocate,
                        Span<u8> existing_allocation,
                        usize num_used_in_existing_allocation,
                        bool allow_oversize_result) {
        if (existing_allocation.size) {
            return Resize({
                .allocation = existing_allocation,
                .new_size = num_to_allocate * sizeof(Type),
                .allow_oversize_result = allow_oversize_result,
                .move_memory_handler = MoveMemoryHandlerForType<Type>(&num_used_in_existing_allocation),
            });
        }
        return Allocate({
            .size = num_to_allocate * sizeof(Type),
            .alignment = alignof(Type),
            .allow_oversized_result = false,
        });
    }

    // Only use on a pointer allocated with New
    template <typename Type>
    void Delete(Type* ptr) {
        ptr->~Type();
        Free({(u8*)ptr, sizeof(Type)});
    }

    // Only use on a pointer allocated with NewMultiple
    template <typename Type>
    void Delete(Span<Type> data) {
        for (auto& d : data)
            d.~Type();
        Free(data.ToByteSpan());
    }

    Span<u8> ResizeUsingNewAllocation(ResizeCommand const& cmd, usize alignment) {
        auto new_allocation = Allocate({
            .size = cmd.new_size,
            .alignment = alignment,
            .allow_oversized_result = cmd.allow_oversize_result,
        });
        if (!new_allocation.size) return {};
        if (cmd.move_memory_handler.function)
            cmd.move_memory_handler.function({.context = cmd.move_memory_handler.context,
                                              .destination = new_allocation.data,
                                              .source = cmd.allocation.data,
                                              .num_bytes = cmd.allocation.size});
        Free(cmd.allocation);
        return new_allocation;
    }
};

template <typename Type>
constexpr Span<RemoveConst<Type>> Span<Type>::Clone(Allocator& a, CloneType clone_type) const {
    return a.Clone(*this, clone_type);
}

template <TriviallyCopyable Type>
constexpr Optional<Type> Optional<Type>::Clone(Allocator& a, CloneType clone_type) const {
    if (HasValue()) return Value().Clone(a, clone_type);
    return k_nullopt;
}

template <typename Type>
requires(!TriviallyCopyable<Type>)
constexpr Optional<Type> Optional<Type>::Clone(Allocator& a, CloneType clone_type) const {
    if (HasValue()) return Value().Clone(a, clone_type);
    return k_nullopt;
}

static void CheckAllocatorCommandIsValid(AllocatorCommandUnion const& command_union) {
    if constexpr (!RUNTIME_SAFETY_CHECKS_ON) return;
    switch (command_union.tag) {
        case AllocatorCommand::Allocate: {
            auto const& cmd = command_union.Get<AllocateCommand>();
            ASSERT(cmd.size != 0);
            ASSERT(cmd.size < Gb(10));
            ASSERT(cmd.alignment != 0);
            ASSERT(IsPowerOfTwo(cmd.alignment));
            break;
        }

        case AllocatorCommand::Free: {
            auto const& cmd = command_union.Get<FreeCommand>();
            ASSERT(cmd.allocation.size != 0);
            break;
        }

        case AllocatorCommand::Resize: {
            auto const& cmd = command_union.Get<ResizeCommand>();
            ASSERT(cmd.allocation.size);
            ASSERT(cmd.new_size != 0);
            break;
        }
    }
}

static constexpr Optional<Span<u8>>
HandleBumpAllocation(Span<u8> stack, usize& cursor, AllocateCommand const& cmd) {
    if (!stack.size) return k_nullopt;

    auto const align_to_add = BytesToAddForAlignment(cursor, cmd.alignment);
    auto const aligned_cursor = cursor + align_to_add;
    auto const cursor_after_allocation = aligned_cursor + cmd.size;
    if (cursor_after_allocation <= stack.size) {
        Span<u8> result {stack.data + aligned_cursor, cmd.size};
        cursor = cursor_after_allocation;
        return result;
    }

    return k_nullopt;
}

static constexpr Optional<Span<u8>>
TryGrowingInPlace(Span<u8> stack, usize& cursor, ResizeCommand const& cmd) {
    auto const current_unused_stack = stack.data + cursor;

    if (End(cmd.allocation) == current_unused_stack) {
        auto const aligned_existing_stack_data = cmd.allocation.data;
        auto const resized_existing_stack_data_end = aligned_existing_stack_data + cmd.new_size;
        if (resized_existing_stack_data_end <= End(stack)) {
            ASSERT(resized_existing_stack_data_end >= stack.data);
            cursor = (usize)(resized_existing_stack_data_end - stack.data);
            auto result = Span<u8> {aligned_existing_stack_data, cmd.new_size};
            return result;
        }
    }
    return k_nullopt;
}

static constexpr void HandleBumpFree(Span<u8> data_to_free, u8* stack_data, usize& cursor) {
    if (data_to_free.data && End(data_to_free) == (stack_data + cursor)) {
        if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
            // fill the memory with a pattern to help catch use-after-free bugs
            FillMemory(data_to_free.data, 0xCD, data_to_free.size);
        }
        cursor -= data_to_free.size;
    }
}

static constexpr Span<u8>
HandleBumpShrink(Span<u8> allocation, usize required_bytes, u8* stack_data, usize& cursor) {
    ASSERT(allocation.size >= required_bytes);

    // lets just pretend that the bit that would be leftover from shrinking is an allocation that we
    // should free
    auto allocation_ending = allocation.Suffix(allocation.size - required_bytes);
    HandleBumpFree(allocation_ending, stack_data, cursor);

    return {allocation.data, required_bytes};
}

// A basic arena allocator that allows for a convenient way to free all of the data at once in the destructor.
// Essentially fixed size buffers are allocated using the given child Allocator when needed. If
// the allocation fits inside the buffer then the cursor is incremented. Or if not, a new region is allocated
// and the cursor is set there. Deallocation happens all at once in the destructor, however, calling Free on
// memory that was just allocated will move the cursor back; allowing you to used that memory again.
//
// https://en.wikipedia.org/wiki/Region-based_memory_management
struct ArenaAllocator : public Allocator {
    struct Region {
        Region* next;
        Region* prev;
        usize size;
        bool from_child_allocator;

        Span<u8> AllocedMemory() {
            ASSERT(size != 0);
            return {(u8*)this, size};
        }
        u8* BufferData() { return (u8*)this + HeaderAllocSize(); }
        usize BufferSize() const {
            ASSERT(size > HeaderAllocSize());
            return size - HeaderAllocSize();
        }
        Span<u8> BufferView() { return {BufferData(), BufferSize()}; }
        static constexpr usize HeaderAllocSize() {
            static_assert(k_max_alignment * 2 >= sizeof(Region));
            return k_max_alignment * 2;
        }
    };

    ArenaAllocator(Allocator& child_allocator,
                   usize reserve_first_region_bytes = 0,
                   usize minimum_bytes_per_region = 64)
        : minimum_bytes_per_region(minimum_bytes_per_region)
        , child_allocator(child_allocator) {
        if (reserve_first_region_bytes) CreateAndPrependRegionToList(reserve_first_region_bytes, 0);
    }

    ArenaAllocator(Allocator& child_allocator, Span<u8> inline_storage, usize minimum_bytes_per_region = 64)
        : minimum_bytes_per_region(minimum_bytes_per_region)
        , child_allocator(child_allocator) {
        if (inline_storage.size > Region::HeaderAllocSize()) {
            ASSERT(IsAligned(inline_storage.data, alignof(Region)), "larger alignment required");

            auto new_region = CheckedPointerCast<Region*>(inline_storage.data);
            new_region->size = inline_storage.size;
            new_region->from_child_allocator = false;

            DoublyLinkedListPrepend(*this, new_region);
            current_region_cursor = 0;
        }
    }
    ~ArenaAllocator() { FreeAll(); }

    ArenaAllocator(ArenaAllocator&& other) : child_allocator(other.child_allocator) {
        first = other.first;
        last = other.last;
        current_region_cursor = other.current_region_cursor;
        minimum_bytes_per_region = other.minimum_bytes_per_region;

        other.first = {};
        other.last = {};
        other.current_region_cursor = {};
    }

    ArenaAllocator& operator=(ArenaAllocator&& other) {
        FreeAll();

        first = other.first;
        last = other.last;
        current_region_cursor = other.current_region_cursor;
        minimum_bytes_per_region = other.minimum_bytes_per_region;
        child_allocator = other.child_allocator;

        other.first = {};
        other.last = {};
        other.current_region_cursor = {};

        return *this;
    }

    NON_COPYABLE(ArenaAllocator);

    // Don't free the result
    Span<char> CloneNullTerminated(String s) {
        auto result = AllocateExactSizeUninitialised<char>(s.size + 1);
        CopyMemory(result.data, s.data, s.size);
        result[result.size - 1] = '\0';
        result.size -= 1;
        return result;
    }

    Span<u8> DoCommand(AllocatorCommandUnion const& command_union) {
        CheckAllocatorCommandIsValid(command_union);

        switch (command_union.tag) {
            case AllocatorCommand::Allocate: {
                auto const& cmd = command_union.Get<AllocateCommand>();

                auto current_region_header = first ? first : CreateAndPrependRegionToList(cmd.size, 0);
                while (true) {
                    if (auto allocation = HandleBumpAllocation(current_region_header->BufferView(),
                                                               current_region_cursor,
                                                               cmd)) {
                        return *allocation;
                    }

                    current_region_header =
                        CreateAndPrependRegionToList(cmd.size, current_region_header->BufferSize());
                }
                PanicIfReached();
                return {};
            }

            case AllocatorCommand::Free: {
                auto const& cmd = command_union.Get<FreeCommand>();
                ASSERT(first);
                HandleBumpFree(cmd.allocation, first->BufferData(), current_region_cursor);
                return {};
            }

            case AllocatorCommand::Resize: {
                auto const& cmd = command_union.Get<ResizeCommand>();
                ASSERT(first);

                if (cmd.new_size > cmd.allocation.size) {
                    if (auto allocation = TryGrowingInPlace(first->BufferView(), current_region_cursor, cmd))
                        return *allocation;

                    return ResizeUsingNewAllocation(cmd, k_max_alignment);
                } else if (cmd.new_size < cmd.allocation.size) {
                    return HandleBumpShrink(cmd.allocation,
                                            cmd.new_size,
                                            first->BufferData(),
                                            current_region_cursor);
                } else {
                    return cmd.allocation;
                }
            }
        }
        return {};
    }

    void FreeAll() {
        for (auto region = first; region != nullptr;) {
            auto region_to_free = region;
            region = region->next; // needs to be done prior to freeing

            if (region_to_free->from_child_allocator) child_allocator.Free(region_to_free->AllocedMemory());
        }
        first = nullptr;
        last = nullptr;
        current_region_cursor = 0;
    }

    void ResetCurrentRegionCursor() { current_region_cursor = 0; }

    void ResetCursorAndConsolidateRegions() {
        if (first == nullptr) return;
        if (first == last) {
            current_region_cursor = 0;
            return;
        }

        // Start at the last region (oldest) and work our way back to the first (newest), freeing regions and
        // summing the sizes.
        usize size_used = current_region_cursor;
        for (auto r = first->next; r != nullptr;) {
            size_used += r->BufferSize();

            auto const region_to_free = r;
            r = r->next;
            if (region_to_free->from_child_allocator) child_allocator.Free(region_to_free->AllocedMemory());
        }

        // The first region is the newest and largest region so we use that for resizing.
        // If there's inline storage, it should be the oldest region (it's created in the constructor). If we
        // got this far, we must have multiple regions and therefore the newest region must have been
        // allocated.
        ASSERT(first->from_child_allocator);
        auto const data = child_allocator.Resize({
            .allocation = first->AllocedMemory(),
            .new_size = size_used + Region::HeaderAllocSize(),
            .allow_oversize_result = true,
        });

        auto new_region = CheckedPointerCast<Region*>(data.data);
        new_region->size = data.size;
        new_region->next = nullptr;
        new_region->prev = nullptr;
        new_region->from_child_allocator = true;

        first = new_region;
        last = new_region;
        current_region_cursor = 0;
    }

    usize TryShrinkTotalUsed(usize size) {
        if (!first) return 0;

        // We need to work out what region the size is in by starting from the last (oldest) region and
        // working towards the first (newest).
        usize pos = 0;
        for (auto r = last; r != nullptr; r = r->prev) {
            if (r == first) {
                auto const new_current_region_cursor = size - pos;
                ASSERT(new_current_region_cursor <= current_region_cursor);

                if constexpr (RUNTIME_SAFETY_CHECKS_ON)
                    FillMemory(first->BufferData() + new_current_region_cursor,
                               0xCD,
                               current_region_cursor - new_current_region_cursor);

                current_region_cursor = new_current_region_cursor;
                return size;
            }

            auto const next_pos = pos + r->BufferSize();
            if (size >= pos && size < next_pos) {
                // The size we were given is not in the first (newest) region. We would need to deallocate
                // regions and would leave the arena using a smaller region as its first. For efficiency
                // reasons, this probably isn't what we want. So instead, we just trim what we can from
                // the newest region.

                if constexpr (RUNTIME_SAFETY_CHECKS_ON)
                    FillMemory(first->BufferData(), 0xCD, current_region_cursor);

                current_region_cursor = 0;

                usize total_used = next_pos;
                for (auto r2 = first->next; r2 != r; r2 = r2->next)
                    total_used += r2->BufferSize();
                return total_used;
            }
            pos = next_pos;
        }

        Panic("size is greater than total arena used");
        return 0;
    }

    usize TotalUsed() const {
        if (!first) return 0;
        usize result = current_region_cursor;
        for (auto r = first->next; r != nullptr; r = r->next)
            result += r->BufferSize();
        return result;
    }

    // Private.
    Region* CreateAndPrependRegionToList(usize size, usize previous_size) {
        auto const memory_region_size = Max(minimum_bytes_per_region, size, previous_size * 2);
        auto region_bytes = child_allocator.Allocate({
            .size = memory_region_size + Region::HeaderAllocSize(),
            .alignment = k_max_alignment,
            .allow_oversized_result = true,
        });

        auto new_region = CheckedPointerCast<Region*>(region_bytes.data);
        new_region->size = region_bytes.size;
        new_region->from_child_allocator = true;

        DoublyLinkedListPrepend(*this, new_region);

        current_region_cursor = 0;
        return first;
    }

    bool ContainsPointer(u8 const* p) const {
        for (auto r = first; r != nullptr; r = r->next)
            if (::ContainsPointer(r->BufferView(), p)) return true;
        return false;
    }

    usize minimum_bytes_per_region {};
    Region* first {}; // A.K.A. current.
    Region* last {};
    usize current_region_cursor {};
    Allocator& child_allocator;
};

// If there is no fallback allocator then there is no need to call Free().
template <usize static_size>
class FixedSizeAllocator : public Allocator {
  public:
    NON_COPYABLE_AND_MOVEABLE(FixedSizeAllocator);

    FixedSizeAllocator(Allocator* fallback_allocator) : m_fallback_allocator(fallback_allocator) {}

    Span<u8> DoCommand(AllocatorCommandUnion const& command) override {
        auto stack = StackView();

        CheckAllocatorCommandIsValid(command);

        switch (command.tag) {
            case AllocatorCommand::Allocate: {
                auto const& cmd = command.Get<AllocateCommand>();
                if (auto allocation = HandleBumpAllocation(stack, m_cursor, cmd)) return *allocation;
                if (m_fallback_allocator) return m_fallback_allocator->Allocate(cmd);
                return {};
            }

            case AllocatorCommand::Free: {
                auto const& cmd = command.Get<FreeCommand>();
                if (ContainsPointer(stack, cmd.allocation.data))
                    HandleBumpFree(cmd.allocation, stack.data, m_cursor);
                else if (m_fallback_allocator)
                    return m_fallback_allocator->DoCommand(command);
                break;
            }

            case AllocatorCommand::Resize: {
                auto const& cmd = command.Get<ResizeCommand>();
                if (cmd.new_size == cmd.allocation.size) return cmd.allocation;

                if (ContainsPointer(stack, cmd.allocation.data)) {
                    if (cmd.new_size > cmd.allocation.size) {
                        if (auto allocation = TryGrowingInPlace(stack, m_cursor, cmd)) return *allocation;
                        return ResizeUsingNewAllocation(cmd, k_max_alignment);
                    } else {
                        return HandleBumpShrink(cmd.allocation, cmd.new_size, stack.data, m_cursor);
                    }
                } else if (m_fallback_allocator) {
                    return m_fallback_allocator->Resize(cmd);
                }

                break;
            }
        }
        return {};
    }

    Span<u8> UsedStackData() { return {m_stack_data, m_cursor}; }

    usize MaxSize() const { return static_size; }

  private:
    Span<u8> StackView() { return {m_stack_data, static_size}; }

    Allocator* m_fallback_allocator;
    usize m_cursor {};
    alignas(k_max_alignment) u8 m_stack_data[static_size];
};

template <usize static_size>
struct ArenaAllocatorWithInlineStorage : public ArenaAllocator {
    ArenaAllocatorWithInlineStorage(Allocator& fallback)
        : ArenaAllocator(fallback, Span<u8>(inline_storage, static_size)) {}
    alignas(k_max_alignment) u8 inline_storage[static_size];
};
