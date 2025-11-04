// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

struct ArenaAllocatorMalloc : ArenaAllocator {
    ArenaAllocatorMalloc() : ArenaAllocator(Malloc::Instance()) {}
};

struct ArenaAllocatorPage : ArenaAllocator {
    ArenaAllocatorPage() : ArenaAllocator(PageAllocator::Instance()) {}
};

struct ArenaAllocatorWithInlineStorage100 : ArenaAllocatorWithInlineStorage<100> {
    ArenaAllocatorWithInlineStorage100() : ArenaAllocatorWithInlineStorage<100>(Malloc::Instance()) {}
};

struct ArenaAllocatorBigBuf : ArenaAllocator {
    ArenaAllocatorBigBuf() : ArenaAllocator(big_buf) {}
    FixedSizeAllocator<1000> big_buf {&Malloc::Instance()};
};

struct FixedSizeAllocatorTiny : FixedSizeAllocator<1> {
    FixedSizeAllocatorTiny() : FixedSizeAllocator(&Malloc::Instance()) {}
};
struct FixedSizeAllocatorSmall : FixedSizeAllocator<16> {
    FixedSizeAllocatorSmall() : FixedSizeAllocator(&Malloc::Instance()) {}
};
struct FixedSizeAllocatorLarge : FixedSizeAllocator<1000> {
    FixedSizeAllocatorLarge() : FixedSizeAllocator(&Malloc::Instance()) {}
};

template <typename AllocatorType>
TEST_CASE(TestAllocatorTypes) {
    AllocatorType a;

    SUBCASE("Pointers are unique when no existing data is passed in") {
        constexpr auto k_iterations = 1000;
        DynamicArrayBounded<Span<u8>, k_iterations> allocs;
        DynamicArrayBounded<void*, k_iterations> set;
        for (auto _ : Range(k_iterations)) {
            dyn::Append(allocs, a.Allocate({1, 1, true}));
            REQUIRE(Last(allocs).data != nullptr);
            dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
        }
        REQUIRE(set.size == k_iterations);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("all sizes and alignments are handled") {
        usize const sizes[] = {1, 2, 3, 99, 7000};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        auto const total_size = ArraySize(sizes) * ArraySize(alignments);
        DynamicArrayBounded<Span<u8>, total_size> allocs;
        DynamicArrayBounded<void*, total_size> set;
        for (auto s : sizes) {
            for (auto align : alignments) {
                dyn::Append(allocs, a.Allocate({s, align, true}));
                REQUIRE(Last(allocs).data != nullptr);
                dyn::AppendIfNotAlreadyThere(set, Last(allocs).data);
            }
        }
        REQUIRE(set.size == total_size);
        for (auto alloc : allocs)
            a.Free(alloc);
    }

    SUBCASE("reallocating an existing block still contains the same data") {
        auto data = a.template AllocateBytesForTypeOversizeAllowed<int>();
        DEFER { a.Free(data); };
        int const test_value = 1234567;
        *(CheckedPointerCast<int*>(data.data)) = test_value;

        data = a.template Reallocate<int>(100, data, 1, false);
        REQUIRE(*(CheckedPointerCast<int*>(data.data)) == test_value);
    }

    SUBCASE("shrink") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        REQUIRE(data.size >= k_original_size);

        constexpr usize k_new_size = 10;
        auto shrunk_data = a.Resize({data, k_new_size});
        data = shrunk_data;
        REQUIRE(data.size == k_new_size);

        // do another allocation for good measure
        auto data2 = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data2); };
        REQUIRE(data2.size >= k_original_size);
        data2 = a.Resize({data2, k_new_size});
        REQUIRE(data2.size == k_new_size);
    }

    SUBCASE("clone") {
        constexpr usize k_alignment = 8;
        constexpr usize k_original_size = 20;
        auto data = a.Allocate({k_original_size, k_alignment, true});
        DEFER { a.Free(data); };
        FillMemory(data, 'a');

        auto cloned_data = a.Clone(data);
        DEFER { a.Free(cloned_data); };
        REQUIRE(cloned_data.data != data.data);
        REQUIRE(cloned_data.size == data.size);
        for (auto const i : Range(k_original_size))
            REQUIRE(cloned_data[i] == 'a');
    }

    SUBCASE("a complex mix of allocations, reallocations and frees work") {
        usize const sizes[] = {1,  1, 1, 1, 1,   1,   1,   1,  1,    3,   40034,
                               64, 2, 2, 2, 500, 500, 500, 99, 1000, 100, 20};
        usize const alignments[] = {1, 2, 4, 8, 16, 32};
        struct Allocation {
            usize size;
            usize align;
            Span<u8> data {};
        };
        Allocation allocs[ArraySize(sizes)];
        usize align_index = 0;
        for (auto const i : Range(ArraySize(sizes))) {
            auto& alloc = allocs[i];
            alloc.size = sizes[i];
            alloc.align = alignments[align_index];
            align_index++;
            if (align_index == ArraySize(alignments)) align_index = 0;
        }

        auto seed = RandomSeed();
        RandomIntGenerator<usize> rand_gen;
        usize index = 0;
        for (auto _ : Range(ArraySize(sizes) * 5)) {
            switch (rand_gen.GetRandomInRange(seed, 0, 5)) {
                case 0:
                case 1:
                case 2: {
                    auto const new_size = allocs[index].size;
                    auto const new_align = allocs[index].align;
                    auto const existing_data = allocs[index].data;
                    if (existing_data.size && new_size > existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                            .allow_oversize_result = true,
                        });
                    } else if (new_size < existing_data.size) {
                        allocs[index].data = a.Resize({
                            .allocation = existing_data,
                            .new_size = new_size,
                        });
                    } else if (!existing_data.size) {
                        allocs[index].data = a.Allocate({
                            .size = new_size,
                            .alignment = new_align,
                            .allow_oversized_result = true,
                        });
                    }
                    break;
                }
                case 3:
                case 4: {
                    if (allocs[index].data.data) {
                        a.Free(allocs[index].data);
                        allocs[index].data = {};
                    }
                    break;
                }
                case 5: {
                    if (allocs[index].data.data) {
                        auto const new_size = allocs[index].data.size / 2;
                        if (new_size) {
                            allocs[index].data = a.Resize({
                                .allocation = allocs[index].data,
                                .new_size = new_size,
                            });
                        }
                    }
                }
            }
            index++;
            if (index == ArraySize(allocs)) index = 0;
        }

        for (auto& alloc : allocs)
            if (alloc.data.data) a.Free(alloc.data);
    }

    SUBCASE("speed benchmark") {
        constexpr usize k_alignment = 8;
        usize const sizes[] = {1,   16,  16,  16, 16,   32,  32, 32, 32, 32, 40034, 64, 128, 50, 239,
                               500, 500, 500, 99, 1000, 100, 20, 16, 16, 16, 64,    64, 64,  64, 64,
                               64,  64,  64,  64, 64,   64,  64, 64, 64, 64, 64,    64, 64};

        constexpr usize k_num_cycles = 10;
        Span<u8> allocations[ArraySize(sizes) * k_num_cycles];

        Stopwatch const stopwatch;

        for (auto const cycle : Range(k_num_cycles))
            for (auto const i : Range(ArraySize(sizes)))
                allocations[(cycle * ArraySize(sizes)) + i] = a.Allocate({sizes[i], k_alignment, true});

        if constexpr (!Same<ArenaAllocator, AllocatorType>)
            for (auto& alloc : allocations)
                a.Free(alloc);

        String type_name {};
        if constexpr (Same<AllocatorType, FixedSizeAllocatorTiny>)
            type_name = "FixedSizeAllocatorTiny";
        else if constexpr (Same<AllocatorType, FixedSizeAllocatorSmall>)
            type_name = "FixedSizeAllocatorSmall";
        else if constexpr (Same<AllocatorType, FixedSizeAllocatorLarge>)
            type_name = "FixedSizeAllocatorLarge";
        else if constexpr (Same<AllocatorType, Malloc>)
            type_name = "Malloc";
        else if constexpr (Same<AllocatorType, PageAllocator>)
            type_name = "PageAllocator";
        else if constexpr (Same<AllocatorType, ArenaAllocatorMalloc>)
            type_name = "ArenaAllocatorMalloc";
        else if constexpr (Same<AllocatorType, ArenaAllocatorPage>)
            type_name = "ArenaAllocatorPage";
        else if constexpr (Same<AllocatorType, ArenaAllocatorBigBuf>)
            type_name = "ArenaAllocatorBigBuf";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else if constexpr (Same<AllocatorType, LeakDetectingAllocator>)
            type_name = "LeakDetectingAllocator";
        else if constexpr (Same<AllocatorType, ArenaAllocatorWithInlineStorage100>)
            type_name = "ArenaAllocatorWithInlineStorage100";
        else
            PanicIfReached();

        tester.log.Debug("Speed benchmark: {} for {}", stopwatch, type_name);
    }
    return k_success;
}

TEST_CASE(TestArenaAllocatorCursor) {
    LeakDetectingAllocator leak_detecting_allocator;
    constexpr usize k_first_region_size = 64;
    ArenaAllocator arena {leak_detecting_allocator, k_first_region_size};
    CHECK(arena.first == arena.last);
    CHECK_OP(arena.first->BufferSize(), ==, k_first_region_size);

    auto const cursor1 = arena.TotalUsed();
    REQUIRE(cursor1 == 0);

    arena.NewMultiple<u8>(10);
    auto const cursor2 = arena.TotalUsed();
    CHECK_EQ(cursor2, (usize)10);
    CHECK(arena.first == arena.last);

    CHECK_EQ(arena.TryShrinkTotalUsed(cursor1), (usize)0);

    arena.NewMultiple<u8>(10);
    CHECK_EQ(arena.TotalUsed(), (usize)10);
    CHECK(arena.first == arena.last);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    CHECK(arena.first == arena.last);

    arena.AllocateExactSizeUninitialised<u8>(4000);
    CHECK(arena.first != arena.last);
    CHECK(arena.first->next == arena.last);
    CHECK(arena.last->prev == arena.first);
    CHECK_EQ(arena.TryShrinkTotalUsed(100), (usize)100);
    CHECK_EQ(arena.TotalUsed(), (usize)100);

    CHECK_EQ(arena.TryShrinkTotalUsed(4), k_first_region_size);
    CHECK_LTE(arena.TotalUsed(), k_first_region_size);

    arena.ResetCursorAndConsolidateRegions();
    CHECK_EQ(arena.TotalUsed(), (usize)0);
    return k_success;
}

TEST_CASE(TestArenaAllocatorInlineStorage) {
    LeakDetectingAllocator leak_detecting_allocator;

    SUBCASE("inline storage used for first region") {
        constexpr usize k_size = 1024;
        alignas(k_max_alignment) u8 inline_storage[k_size];
        ArenaAllocator arena(leak_detecting_allocator, {inline_storage, k_size});

        // First allocation should come from inline storage
        auto ptr1 = arena.AllocateExactSizeUninitialised<u64>(10);
        CHECK((u8*)ptr1.data >= inline_storage && (u8*)ptr1.data < inline_storage + k_size);
        CHECK(arena.TotalUsed() == ptr1.size * sizeof(u64));

        // Fill most of inline storage
        auto remaining_space = k_size - ArenaAllocator::Region::HeaderAllocSize() - arena.TotalUsed();
        auto ptr2 = arena.AllocateExactSizeUninitialised<u8>(remaining_space - 64);
        CHECK(ptr2.data >= inline_storage && ptr2.data < inline_storage + k_size);
    }

    SUBCASE("fallback to child allocator when inline storage full") {
        constexpr usize k_size = 256;
        alignas(k_max_alignment) u8 inline_storage[k_size];
        ArenaAllocator arena(leak_detecting_allocator, {inline_storage, k_size});

        // Fill inline storage
        auto inline_capacity = k_size - ArenaAllocator::Region::HeaderAllocSize() - 32;
        auto ptr1 = arena.AllocateExactSizeUninitialised<u8>(inline_capacity);
        CHECK(ptr1.data >= inline_storage && ptr1.data < inline_storage + k_size);

        // Next allocation should trigger child allocator
        auto ptr2 = arena.AllocateExactSizeUninitialised<u64>(64);
        CHECK((u8*)ptr2.data < inline_storage || (u8*)ptr2.data >= inline_storage + k_size);
    }

    SUBCASE("inline storage not freed in destructor") {
        constexpr usize k_size = 512;
        alignas(k_max_alignment) u8 inline_storage[k_size];

        {
            ArenaAllocator arena(leak_detecting_allocator, {inline_storage, k_size});
            auto ptr = arena.AllocateExactSizeUninitialised<u32>(32);
            CHECK((u8*)ptr.data >= inline_storage && (u8*)ptr.data < inline_storage + k_size);

            // Force allocation from child allocator too
            auto large_ptr = arena.AllocateExactSizeUninitialised<u8>(1024);
            CHECK(large_ptr.data < inline_storage || large_ptr.data >= inline_storage + k_size);
        }
        // Arena destructor should only free child allocator regions, not inline storage
        // leak_detecting_allocator will catch any issues
    }

    SUBCASE("empty inline storage handled gracefully") {
        ArenaAllocator arena(leak_detecting_allocator, Span<u8> {});

        auto ptr = arena.AllocateExactSizeUninitialised<u64>(8);
        CHECK(ptr.size == 8);
    }

    SUBCASE("tiny inline storage too small for region header") {
        constexpr usize k_size = 16;
        alignas(k_max_alignment) u8 tiny_storage[k_size];
        ArenaAllocator arena(leak_detecting_allocator, {tiny_storage, ArraySize(tiny_storage)});

        // Should fallback to child allocator since storage too small for header
        auto ptr = arena.AllocateExactSizeUninitialised<u32>(4);
        CHECK((u8*)ptr.data < tiny_storage || (u8*)ptr.data >= tiny_storage + ArraySize(tiny_storage));
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAllocatorTests) {
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorBigBuf>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorMalloc>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorPage>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorLarge>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorSmall>);
    REGISTER_TEST(TestAllocatorTypes<FixedSizeAllocatorTiny>);
    REGISTER_TEST(TestAllocatorTypes<LeakDetectingAllocator>);
    REGISTER_TEST(TestAllocatorTypes<Malloc>);
    REGISTER_TEST(TestAllocatorTypes<PageAllocator>);
    REGISTER_TEST(TestAllocatorTypes<ArenaAllocatorWithInlineStorage100>);
    REGISTER_TEST(TestArenaAllocatorCursor);
    REGISTER_TEST(TestArenaAllocatorInlineStorage);
}
