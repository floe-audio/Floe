// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "scoped_string.hpp"

namespace sample_lib_server {

// This struct maintains a list of folders that should be scanned for sample libraries. It also manages async
// scanning of these folders, and reads all libraries, putting the results in lists ready to be consumed by
// the caller.
struct ScanFolders {
    struct LibraryReadResult {
        ArenaAllocator arena {PageAllocator::Instance()};
        String path {};
        Optional<sample_lib::LibraryPtrOrError> library {};
    };

    struct ScanFolderResult {
        MallocedString path {};
        ErrorCodeOr<void> outcome {};
    };

    struct ScanFolder {
        String path {};
        bool scan_issued {};
    };

    using ShouldReadLibrary = TrivialFixedSizeFunction<8, bool(u64 hash, String path)>;

    ~ScanFolders();

    ShouldReadLibrary should_read_library;
    ThreadPool& thread_pool;
    Semaphore& work_signaller;
    mutable Mutex mutex {};
    ArenaAllocator arena {PageAllocator::Instance()};
    PathPool path_pool {};
    DynamicArrayBounded<ScanFolder, k_max_extra_scan_folders + 1> folders {};
    ArenaList<Future<ScanFolderResult>> folder_scan_results {};
    ArenaList<Future<LibraryReadResult>> library_read_results {};
    Atomic<u32> is_scanning {false};
    bool has_always_scanned_folder {};
};

// Set the folders. Does not initiate scanning - that is done via ScanUnscannedFolders. The always-scanned
// folder is just another scan-folder, but it typically has different error handling and so it's tracked
// separately.
void SetAlwaysScannedFolder(ScanFolders& sf, Optional<String> always_scanned_folder);
void SetFolders(ScanFolders& sf, Span<String const> new_folders);

// Begin async scanning any unscanned-folders.
void ScanUnscannedFolders(ScanFolders& sf);

Span<String> GetFolders(ScanFolders const& sf, ArenaAllocator& a);
bool IsAlwaysScannedFolder(ScanFolders const& sf, String path);

// Regularly pop these to consume all results.
Optional<ScanFolders::ScanFolderResult> PopCompletedScanFolderResult(ScanFolders& sf);
Optional<ScanFolders::LibraryReadResult> PopCompletedLibraryReadResult(ScanFolders& sf);

// Issue scans of specific folders.
void AsyncReadLibrary(ScanFolders& sf, String path, bool lock);
void AsyncScanFolder(ScanFolders& sf, String path, bool lock);

// Once all results have been popped, call this to attempt to fire the scanning-complete signal.
bool TryMarkScanningComplete(ScanFolders& sf);

bool WaitIfLibrariesAreScanning(ScanFolders& sf, Optional<u32> timeout_ms);
bool IsScanning(ScanFolders const& sf);

} // namespace sample_lib_server
