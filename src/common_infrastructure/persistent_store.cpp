// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "persistent_store.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

#include "common_errors.hpp"

namespace persistent_store {

struct ChunkHeader {
    u64 id;
    u32 size;
};

StoreTable Read(ArenaAllocator& arena, String data) {
    StoreTable store {};

    auto const* data_ptr = (u8 const*)data.data;
    auto const* data_end = data_ptr + data.size;

    while (data_ptr < data_end) {
        if ((usize)(data_end - data_ptr) < sizeof(ChunkHeader))
            break; // Invalid data. We just stop reading, we might have some valid data before this point.

        ChunkHeader header {};
        CopyMemory(&header, data_ptr, sizeof(ChunkHeader));
        data_ptr += sizeof(ChunkHeader);

        auto value = arena.New<Value>(Value {
            .data = {data_ptr, header.size},
            .next = nullptr,
        });

        auto result = store.FindOrInsertGrowIfNeeded(arena, header.id, value);
        if (!result.inserted) SinglyLinkedListPrepend(result.element.data, value);

        data_ptr += header.size;
    }

    return store;
}

ErrorCodeOr<void> Write(StoreTable const& store, Writer writer) {
    for (auto const& [id, value, _] : store) {
        for (auto v = value; v; v = v->next) {
            ChunkHeader const header {
                .id = id,
                .size = (u32)v->data.size,
            };
            TRY(writer.WriteBytes({(u8 const*)&header, sizeof(header)}));
            TRY(writer.WriteBytes({v->data.data, v->data.size}));
        }
    }
    return k_success;
}

// Data is cloned.
void AddValue(StoreTable& store, ArenaAllocator& arena, Id id, Span<u8 const> data) {
    auto value = arena.New<Value>(Value {
        .data = arena.Clone(data),
        .next = nullptr,
    });

    auto result = store.FindOrInsertGrowIfNeeded(arena, id, value);
    if (!result.inserted) SinglyLinkedListPrepend(result.element.data, value);
}

// If value is nullopt, it will remove all values for the given id, else it will only remove the specified
// value (identified using memcmp).
void RemoveValue(StoreTable& store, Id id, Optional<Span<u8 const>> value) {
    if (value) {
        auto existing_element = store.FindElement(id);
        if (!existing_element) return;
        SinglyLinkedListRemoveIf(
            existing_element->data,
            [&](Value const& node) {
                return node.data.size == value->size &&
                       MemoryIsEqual(node.data.data, value->data, node.data.size);
            },
            [&](Value*) {});
        if (existing_element->data == nullptr) store.DeleteElement(existing_element);
    } else {
        store.Delete(id);
    }
}

struct ReadResult {
    String file_data;
    s128 file_last_modified {};
};

ErrorCodeOr<ReadResult> ReadEntireStoreFile(String path, ArenaAllocator& arena) {
    LogDebug(ModuleName::PersistentStore, "Reading persistent_store file: {}", path);

    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Read,
                                 .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                                 .creation = FileMode::Creation::OpenExisting,
                             }));
    TRY(file.Lock({.type = FileLockOptions::Type::Shared}));
    DEFER { auto _ = file.Unlock(); };

    if (TRY(file.FileSize()) > Mb(100)) return ErrorCode {CommonError::InvalidFileFormat};

    auto const file_last_modified = TRY(file.LastModifiedTimeNsSinceEpoch());
    auto const file_data = TRY(file.ReadWholeFile(arena));

    return ReadResult {
        .file_data = file_data,
        .file_last_modified = file_last_modified,
    };
}

static bool InitIfNeeded(Store& store) {
    // If the file is newer than the last time we read/wrote it, we need to re-read it; another process may
    // have updated it.
    if (store.actual_file_last_modified_microsec.Load(LoadMemoryOrder::Acquire) >
        store.file_last_modified_microsec)
        store.init = false;

    if (store.init) return store.store_valid;
    store.init = true;
    store.store_valid = false;
    store.arena.ResetCursorAndConsolidateRegions();

    auto const outcome = ReadEntireStoreFile(store.filepath, store.arena);

    String data {};
    if (outcome.HasValue()) {
        data = outcome.Value().file_data;
        store.file_last_modified_microsec = (u64)(outcome.Value().file_last_modified / 1000);
    } else if (outcome.Error() != FilesystemError::PathDoesNotExist)
        return false;

    (StoreTable&)store = Read(store.arena, data);
    store.store_valid = true;
    return true;
}

static void WriteFile(Store& store) {
    auto const file_last_modified = NanosecondsSinceEpoch();
    store.file_last_modified_microsec = (u64)(file_last_modified / 1000);

    auto file = TRY_OR(OpenFile(store.filepath,
                                {
                                    .capability = FileMode::Capability::Write,
                                    .win32_share = FileMode::Share::ReadWrite,
                                    .creation = FileMode::Creation::CreateAlways,
                                    .everyone_read_write = true,
                                }),
                       return;);

    TRY_OR(file.Lock({.type = FileLockOptions::Type::Exclusive}), return);
    DEFER { auto _ = file.Unlock(); };

    BufferedWriter<Kb(4)> buffered_writer {
        .unbuffered_writer = file.Writer(),
    };
    DEFER {
        auto _ = buffered_writer.Flush();
        auto _ = file.Flush();
    };
    TRY_OR(Write(store, buffered_writer.Writer()), {
        LogError(ModuleName::PersistentStore, "Failed to write data to persistent store: {}", error);
        return;
    });
    auto _ = buffered_writer.Flush();
    auto _ = file.Flush();
    auto _ = file.SetLastModifiedTimeNsSinceEpoch(file_last_modified);
}

ErrorCodeOr<s128> ReadStoreFileModifiedTime(String path) {
    auto file = TRY(OpenFile(path,
                             {
                                 .capability = FileMode::Capability::Read,
                                 .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                                 .creation = FileMode::Creation::OpenExisting,
                             }));
    TRY(file.Lock({.type = FileLockOptions::Type::Shared}));
    DEFER { auto _ = file.Unlock(); };

    return TRY(file.LastModifiedTimeNsSinceEpoch());
}

// Background thread.
void StoreActualFileModifiedTime(Store& store) {
    // We don't need to do this too often, let's save resources.
    {
        constexpr f64 k_seconds_between_checks = 3.0;
        auto now = TimePoint::Now();
        if ((now - store.time_last_checked) < k_seconds_between_checks) return;
        store.time_last_checked = now;
    }

    auto const outcome = ReadStoreFileModifiedTime(store.filepath);
    if (outcome.HasValue()) {
        store.actual_file_last_modified_microsec.Store((u64)(outcome.Value() / 1000),
                                                       StoreMemoryOrder::Release);
    }
}

Result Get(Store& store, Id id) {
    if (!InitIfNeeded(store)) return Result {GetResult::StoreInaccessible};

    auto const value_or_null = store.Find(id);
    if (!value_or_null) return Result {GetResult::NotFound};
    Value const* value = *value_or_null;
    return Result {value};
}

void AddValue(Store& store, Id id, Span<u8 const> data) {
    if (!InitIfNeeded(store)) return;
    AddValue(store, store.arena, id, data);
    WriteFile(store);
}

void RemoveValue(Store& store, Id id, Optional<Span<u8 const>> value) {
    if (!InitIfNeeded(store)) return;
    RemoveValue((StoreTable&)store, id, value);
    WriteFile(store);
}

TEST_CASE(TestPersistentStore) {
    SUBCASE("write and read") {
        StoreTable store;
        AddValue(store, tester.scratch_arena, 1, "hello"_s.ToConstByteSpan());
        AddValue(store, tester.scratch_arena, 1, "hello2"_s.ToConstByteSpan());
        AddValue(store, tester.scratch_arena, 2, "world"_s.ToConstByteSpan());

        DynamicArray<u8> data {tester.scratch_arena};
        auto writer = dyn::WriterFor(data);
        TRY(Write(store, writer));

        CHECK(data.size > 0);

        StoreTable read_store = Read(tester.scratch_arena, String {(char const*)data.data, data.size});
        CHECK_EQ(read_store.size, 2u);
        {
            auto const* values = read_store.Find(1);
            REQUIRE(values);
            CHECK_EQ(SinglyLinkedListSize(*values), 2u);
            DynamicArrayBounded<String, 2> values_array {};
            for (auto const* value = *values; value; value = value->next) {
                dyn::AppendIfNotAlreadyThere(values_array,
                                             String {(char const*)value->data.data, value->data.size});
            }
            CHECK(Contains(values_array, "hello"_s));
            CHECK(Contains(values_array, "hello2"_s));
        }
        {
            auto const* values = read_store.Find(2);
            REQUIRE(values);
            CHECK_EQ(SinglyLinkedListSize(*values), 1u);
            CHECK_EQ((*values)->data, "world"_s.ToConstByteSpan());
        }
    }

    SUBCASE("add and remove values") {
        StoreTable store;
        AddValue(store, tester.scratch_arena, 1, "value1"_s.ToConstByteSpan());
        AddValue(store, tester.scratch_arena, 1, "value2"_s.ToConstByteSpan());
        CHECK_EQ(store.size, 1u);
        CHECK_EQ(SinglyLinkedListSize(*store.Find(1)), 2u);

        RemoveValue(store, 1, "value1"_s.ToConstByteSpan());
        CHECK_EQ(SinglyLinkedListSize(*store.Find(1)), 1u);
        RemoveValue(store, 1, "value2"_s.ToConstByteSpan());
        CHECK_EQ(store.size, 0u);
    }

    SUBCASE("remove all values for an id") {
        StoreTable store;
        AddValue(store, tester.scratch_arena, 1, "value1"_s.ToConstByteSpan());
        AddValue(store, tester.scratch_arena, 1, "value2"_s.ToConstByteSpan());
        CHECK_EQ(store.size, 1u);
        CHECK_EQ(SinglyLinkedListSize(*store.Find(1)), 2u);

        RemoveValue(store, 1, k_nullopt);
        CHECK_EQ(store.size, 0u);
    }

    return k_success;
}

} // namespace persistent_store

TEST_REGISTRATION(RegisterPersistentStoreTests) { REGISTER_TEST(persistent_store::TestPersistentStore); }
