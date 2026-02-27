// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scan_folders.hpp"

#include "tests/framework.hpp"

namespace sample_lib_server {

static Optional<sample_lib::LibraryPtrOrError>
DoLibraryReading(String path,
                 ArenaAllocator& lib_arena,
                 ScanFolders::ShouldReadLibrary const& should_read_library) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};
    using H = sample_lib::TryHelpersOutcomeToError;

    LogDebug(ModuleName::SampleLibraryServer, "Reading library {}", path);

    auto const format = TRY_OPT_OR(sample_lib::DetermineFileFormat(path), return k_nullopt);

    PathOrMemory path_or_memory = path;
    if (format == sample_lib::FileFormat::Lua) {
        // It will be more efficient to just load the whole Lua into memory.
        auto const data = TRY_H(ReadEntireFile(path, scratch_arena)).ToConstByteSpan();
        path_or_memory = data;
        LogInfo(ModuleName::SampleLibraryServer, "reading Lua library ({} Kb)", data.size / 1024);
    } else {
        LogInfo(ModuleName::SampleLibraryServer, "reading MDATA library");
    }

    auto reader = TRY_H(Reader::FromPathOrMemory(path_or_memory));
    auto const file_hash = TRY_H(sample_lib::Hash(path, reader, format));

    if (!should_read_library(file_hash, path)) return k_nullopt;

    auto lib = TRY(sample_lib::Read(reader, format, path, lib_arena, scratch_arena));
    lib->file_hash = file_hash;
    return lib;
}

static ErrorCodeOr<void> DoFolderScanning(ScanFolders& sf, String path) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    LogDebug(ModuleName::SampleLibraryServer, "Scanning folder {}", path);

    u32 num_scanned_entries = 0;

    auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                path,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                }));
    DEFER { dir_iterator::Destroy(it); };

    while (auto const entry = TRY(dir_iterator::Next(it, scratch_arena))) {
        if (num_scanned_entries++ == 99999) {
            LogError(ModuleName::SampleLibraryServer, "Scan folder has too many files in it");
            return ErrorCode {FilesystemError::FolderContainsTooManyFiles};
        }
        if (ContainsSpan(entry->subpath, k_temporary_directory_prefix)) continue;
        if (entry->type == FileType::Directory) continue;
        if (auto const full_path = dir_iterator::FullPath(it, *entry, scratch_arena);
            sample_lib::DetermineFileFormat(full_path))
            AsyncReadLibrary(sf, full_path, true);
    }
    return k_success;
}

template <typename ResultType>
static Optional<ResultType> PopCompletedResult(ScanFolders& sf, auto& list) {
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };

    Optional<ResultType> result {};

    list.RemoveIf([&result](Future<ResultType>& f) {
        if (result) return false;
        if (f.HasResult()) {
            result.Emplace(f.ReleaseResult());
            return true;
        }
        return false;
    });

    return result;
}

void SetAlwaysScannedFolder(ScanFolders& sf, Optional<String> always_scanned_folder) {
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };

    if (sf.has_always_scanned_folder) {
        sf.path_pool.Free(sf.folders[0].path);
        dyn::Remove(sf.folders, (usize)0);
        sf.has_always_scanned_folder = false;
    }

    if (always_scanned_folder) {
        dyn::Prepend(sf.folders, {.path = sf.path_pool.Clone(*always_scanned_folder, sf.arena)});
        sf.has_always_scanned_folder = true;
    }
}

ScanFolders::~ScanFolders() {
    constexpr u32 k_timeout_ms = 180 * 1000;
    for (auto& f : folder_scan_results)
        auto _ = f.ShutdownAndRelease(k_timeout_ms);
    for (auto& f : library_read_results)
        auto _ = f.ShutdownAndRelease(k_timeout_ms);

    while (outstanding_async_cleanups.Load(LoadMemoryOrder::Acquire) > 0)
        SpinLoopPause();

    folder_scan_results.Clear();
    library_read_results.Clear();
}

void SetFolders(ScanFolders& sf, Span<String const> new_folders) {
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };

    // Remove folders that are not present in the new list, skipping the first folder if it's the
    // always-scanned folder.
    for (usize i = sf.has_always_scanned_folder ? 1 : 0; i < sf.folders.size;) {
        if (!Find(new_folders, sf.folders[i].path)) {
            sf.path_pool.Free(sf.folders[i].path);
            dyn::Remove(sf.folders, i);
        } else {
            ++i;
        }
    }

    // Add new folders.
    for (auto const f : new_folders) {
        ASSERT(path::IsAbsolute(f));

        if (!FindIf(sf.folders, [f](auto const scan_folder) { return f == scan_folder.path; }))
            dyn::Append(sf.folders, {.path = sf.path_pool.Clone(f, sf.arena), .scan_issued = false});
    }
}

Span<String> GetFolders(ScanFolders const& sf, ArenaAllocator& a) {
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };
    auto const result = a.AllocateExactSizeUninitialised<String>(sf.folders.size);
    for (auto const i : Range(sf.folders.size))
        result[i] = a.Clone(sf.folders[i].path);
    return result;
}

bool IsAlwaysScannedFolder(ScanFolders const& sf, String path) {
    if (!sf.has_always_scanned_folder) return false;
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };
    return path::Equal(path, sf.folders[0].path);
}

void ScanUnscannedFolders(ScanFolders& sf) {
    sf.mutex.Lock();
    DEFER { sf.mutex.Unlock(); };

    for (auto& f : sf.folders)
        if (!f.scan_issued) {
            AsyncScanFolder(sf, f.path, false);
            f.scan_issued = true;
        }
}

void AsyncReadLibrary(ScanFolders& sf, String path, bool lock) {
    String cloned_path {};
    Future<ScanFolders::LibraryReadResult>* future {};
    {
        if (lock) sf.mutex.Lock();
        DEFER {
            if (lock) sf.mutex.Unlock();
        };

        cloned_path = sf.path_pool.Clone(path, sf.arena);
        future = sf.library_read_results.Prepend(sf.arena);
        sf.is_scanning.Store(true, StoreMemoryOrder::Release);
    }

    sf.outstanding_async_cleanups.FetchAdd(1, RmwMemoryOrder::Relaxed);
    sf.thread_pool.Async(
        *future,
        [path = cloned_path, &sf]() {
            ScanFolders::LibraryReadResult result {};
            result.path = result.arena.Clone(path);
            result.library = DoLibraryReading(path, result.arena, sf.should_read_library);
            return Move(result);
        },
        [path = cloned_path, &sf]() {
            sf.mutex.Lock();
            sf.path_pool.Free(path);
            sf.mutex.Unlock();
            sf.work_signaller.Signal();
            sf.outstanding_async_cleanups.FetchSub(1, RmwMemoryOrder::Release);
        });
}

void AsyncScanFolder(ScanFolders& sf, String path, bool lock) {
    String cloned_path {};
    Future<ScanFolders::ScanFolderResult>* future {};
    {
        if (lock) sf.mutex.Lock();
        DEFER {
            if (lock) sf.mutex.Unlock();
        };

        cloned_path = sf.path_pool.Clone(path, sf.arena);
        future = sf.folder_scan_results.Prepend(sf.arena);
        sf.is_scanning.Store(true, StoreMemoryOrder::Release);
    }

    sf.outstanding_async_cleanups.FetchAdd(1, RmwMemoryOrder::Relaxed);
    sf.thread_pool.Async(
        *future,
        [path = cloned_path, &sf]() {
            ScanFolders::ScanFolderResult result {};
            result.path = CreateMallocedString(path);
            result.outcome = DoFolderScanning(sf, path);
            return Move(result);
        },
        [path = cloned_path, &sf]() {
            sf.mutex.Lock();
            sf.path_pool.Free(path);
            sf.mutex.Unlock();
            sf.work_signaller.Signal();
            sf.outstanding_async_cleanups.FetchSub(1, RmwMemoryOrder::Release);
        });
}

Optional<ScanFolders::ScanFolderResult> PopCompletedScanFolderResult(ScanFolders& sf) {
    return PopCompletedResult<ScanFolders::ScanFolderResult>(sf, sf.folder_scan_results);
}

Optional<ScanFolders::LibraryReadResult> PopCompletedLibraryReadResult(ScanFolders& sf) {
    return PopCompletedResult<ScanFolders::LibraryReadResult>(sf, sf.library_read_results);
}

bool TryMarkScanningComplete(ScanFolders& sf) {
    bool scanning_complete = false;
    {
        sf.mutex.Lock();
        DEFER { sf.mutex.Unlock(); };
        if (!sf.folder_scan_results.Empty() || !sf.library_read_results.Empty()) {
            scanning_complete = false;
        } else {
            sf.is_scanning.Store(false, StoreMemoryOrder::Release);
            scanning_complete = true;
        }
    }

    if (scanning_complete) WakeWaitingThreads(sf.is_scanning, NumWaitingThreads::All);
    return scanning_complete;
}

bool WaitIfLibrariesAreScanning(ScanFolders& sf, Optional<u32> timeout_ms) {
    return WaitIfValueIsExpectedStrong(sf.is_scanning, true, timeout_ms);
}

bool IsScanning(ScanFolders const& sf) { return sf.is_scanning.Load(LoadMemoryOrder::Acquire); }

} // namespace sample_lib_server

// Tests
// ============================================================

using namespace sample_lib_server;

TEST_CASE(TestScanFoldersSetAndGet) {
    Semaphore semaphore {};
    ThreadPool thread_pool {};
    thread_pool.Init("test"_s, 1u);
    ScanFolders sf {
        .should_read_library = [](u64, String) { return true; },
        .thread_pool = thread_pool,
        .work_signaller = semaphore,
    };

    {
        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 0uz);
    }

    {
        auto const paths = Array {FAKE_ABSOLUTE_PATH_PREFIX "libs"_s, FAKE_ABSOLUTE_PATH_PREFIX "samples"_s};
        SetFolders(sf, paths);

        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 2uz);
        CHECK(Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "libs"_s));
        CHECK(Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "samples"_s));
    }

    {
        auto const initial = Array {FAKE_ABSOLUTE_PATH_PREFIX "a"_s,
                                    FAKE_ABSOLUTE_PATH_PREFIX "b"_s,
                                    FAKE_ABSOLUTE_PATH_PREFIX "c"_s};
        SetFolders(sf, initial);

        auto const updated = Array {FAKE_ABSOLUTE_PATH_PREFIX "b"_s, FAKE_ABSOLUTE_PATH_PREFIX "d"_s};
        SetFolders(sf, updated);

        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 2uz);
        CHECK(Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "b"_s));
        CHECK(Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "d"_s));
        CHECK(!Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "a"_s));
        CHECK(!Find(folders, FAKE_ABSOLUTE_PATH_PREFIX "c"_s));
    }

    {
        auto const paths = Array {FAKE_ABSOLUTE_PATH_PREFIX "a"_s};
        SetFolders(sf, paths);
        SetFolders(sf, paths);

        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 1uz);
    }

    return k_success;
}

TEST_CASE(TestScanFoldersAlwaysScanned) {
    Semaphore semaphore {};
    ThreadPool thread_pool {};
    thread_pool.Init("test"_s, 1u);
    ScanFolders sf {
        .should_read_library = [](u64, String) { return true; },
        .thread_pool = thread_pool,
        .work_signaller = semaphore,
    };

    CHECK(!IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "any"_s));

    SetAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "always"_s);
    CHECK(IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "always"_s));
    CHECK(!IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "other"_s));
    {
        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 1uz);
    }

    // Always-scanned folder is preserved when setting extra folders.
    {
        auto const extras = Array {FAKE_ABSOLUTE_PATH_PREFIX "extra"_s};
        SetFolders(sf, extras);

        auto folders = GetFolders(sf, tester.scratch_arena);
        REQUIRE_EQ(folders.size, 2uz);
        CHECK(IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "always"_s));
    }

    // Replacing always-scanned folder.
    SetAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "second"_s);
    CHECK(!IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "always"_s));
    CHECK(IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "second"_s));

    // Removing always-scanned folder.
    SetAlwaysScannedFolder(sf, k_nullopt);
    CHECK(!IsAlwaysScannedFolder(sf, FAKE_ABSOLUTE_PATH_PREFIX "second"_s));

    return k_success;
}

TEST_CASE(TestScanFoldersScanningState) {
    Semaphore semaphore {};
    ThreadPool thread_pool {};
    thread_pool.Init("test"_s, 1u);
    ScanFolders sf {
        .should_read_library = [](u64, String) { return true; },
        .thread_pool = thread_pool,
        .work_signaller = semaphore,
    };

    CHECK(!IsScanning(sf));
    CHECK(TryMarkScanningComplete(sf));

    return k_success;
}

TEST_REGISTRATION(RegisterScanFoldersTests) {
    REGISTER_TEST(TestScanFoldersSetAndGet);
    REGISTER_TEST(TestScanFoldersAlwaysScanned);
    REGISTER_TEST(TestScanFoldersScanningState);
}
