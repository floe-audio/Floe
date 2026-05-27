// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library_id_cache.hpp"

#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

#include "common_errors.hpp"

namespace {

constexpr String k_cache_filename = "library_id_cache.bin"_s;

} // namespace

String LibraryIdCachePath(ArenaAllocator& arena, bool create_parent_dir) {
    DynamicArrayBounded<char, Kb(1)> error_log;
    auto writer = dyn::WriterFor(error_log);
    auto const path = KnownDirectoryWithSubdirectories(arena,
                                                       KnownDirectoryType::UserData,
                                                       Array {"Floe"_s},
                                                       k_cache_filename,
                                                       {.create = create_parent_dir, .error_log = &writer});
    return path;
}

void WriteLibraryIdCache(Span<String const> id_strings) {
    ArenaAllocatorWithInlineStorage<2000> scratch {PageAllocator::Instance()};
    auto const path = LibraryIdCachePath(scratch, true);

    auto file = TRY_OR(
        OpenFile(path,
                 {
                     .capability = FileMode::Capability::Write,
                     .win32_share = FileMode::Share::ReadWrite,
                     .creation = FileMode::Creation::CreateAlways,
                     .everyone_read_write = true,
                 }),
        {
            LogError(ModuleName::SampleLibraryServer, "Failed to open library id cache for write: {}", error);
            return;
        });

    TRY_OR(file.Lock({.type = FileLockOptions::Type::Exclusive}), return);
    DEFER { auto _ = file.Unlock(); };

    BufferedWriter<Kb(4)> buffered {.unbuffered_writer = file.Writer()};
    DEFER {
        buffered.FlushReset();
        auto _ = file.Flush();
    };

    for (auto const& id_string : id_strings) {
        auto const size = (u32)id_string.size;
        TRY_OR(buffered.Writer().WriteBytes({(u8 const*)&size, sizeof(size)}), return);
        if (size) TRY_OR(buffered.Writer().WriteBytes(id_string.ToConstByteSpan()), return);
    }
}

void LoadLibraryIdCache(ArenaAllocator& arena) {
    auto const path = LibraryIdCachePath(arena, false);

    auto file = TRY_OR(OpenFile(path,
                                {
                                    .capability = FileMode::Capability::Read,
                                    .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                                    .creation = FileMode::Creation::OpenExisting,
                                }),
                       return;);

    TRY_OR(file.Lock({.type = FileLockOptions::Type::Shared}), return);
    DEFER { auto _ = file.Unlock(); };

    if (TRY_OR(file.FileSize(), return;) > Mb(10)) return;
    auto const data = TRY_OR(file.ReadWholeFile(arena), return;);

    auto const* p = (u8 const*)data.data;
    auto const* end = p + data.size;
    while (p < end) {
        if ((usize)(end - p) < sizeof(u32)) break;
        u32 size;
        __builtin_memcpy_inline(&size, p, sizeof(size));
        p += sizeof(size);
        if ((usize)(end - p) < size) break;
        if (size) sample_lib::HashLibraryIdString(String {(char const*)p, size});
        p += size;
    }
}
