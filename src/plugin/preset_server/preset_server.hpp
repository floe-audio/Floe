// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/error_notifications.hpp"

#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

struct PresetFolder {
    struct Preset {
        String name {};
        StateMetadataRef metadata {};
        Set<sample_lib::LibraryIdRef> used_libraries {};
        Set<String> used_library_authors {};
        u64 file_hash {};
        String file_extension {}; // Only if file_format is Mirage. Mirage had variable extensions.
        PresetFormat file_format {};
    };

    Optional<usize> MatchFullPresetPath(String path) const;
    String FullPathForPreset(Preset const& preset, Allocator& a) const;

    ArenaAllocator arena {Malloc::Instance(), 0, 512};

    String scan_folder {};
    String abbreviated_scan_folder {}; // For display purposes
    String folder {}; // subpath of scan_folder, if any
    Span<Preset> presets {};
    Set<sample_lib::LibraryIdRef> used_libraries {};
    Set<String> used_tags {};
    Set<String> used_library_authors {};

    // private
    usize preset_array_capacity {};
    Optional<u64> delete_after_version {};
};

struct PresetServer {
    struct ScanFolder {
        bool always_scanned_folder {};
        String path {};
        bool scanned {};
    };

    static constexpr u64 k_no_version = (u64)-1;

    ThreadsafeErrorNotifications& error_notifications;

    // The reader thread can send the server an array of folder that it should scan.
    Mutex scan_folders_request_mutex;
    ArenaAllocator scan_folders_request_arena {Malloc::Instance(), 0, 128};
    Optional<Span<String>> scan_folders_request {};

    ArenaAllocator arena {PageAllocator::Instance()}; // Preset thread

    ArenaList<PresetFolder> folder_pool {}; // Allocation for folders

    Mutex mutex;

    // We're using a sort of basic "epoch-based reclamation" to delete folders that are no longer in use
    // without the reader having to do much locking.
    Atomic<u64> published_version {};
    Atomic<u64> version_in_use = k_no_version;

    // The next fields are versioned and mutex protected
    DynamicArray<PresetFolder*> folders {arena};
    DynamicSet<String> used_tags {arena};
    DynamicSet<sample_lib::LibraryIdRef> used_libraries {arena};
    DynamicSet<String> authors {arena};
    ArenaAllocator folder_node_arena {(Allocator&)arena};
    Span<FolderNode> folder_nodes {};
    Span<usize> folder_node_order_indices {};

    DynamicSet<u64, NoHash> preset_file_hashes {arena};
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};

    DynamicArray<ScanFolder> scan_folders {arena};

    Thread thread;
    WorkSignaller work_signaller;
    u64 server_thread_id {};
    Atomic<bool> end_thread {false};

    Atomic<bool> enable_scanning {};
};

void InitPresetServer(PresetServer& server, String always_scanned_folder);
void ShutdownPresetServer(PresetServer& server);

void SetExtraScanFolders(PresetServer& server, Span<String const> folders);

struct PresetsSnapshot {
    Span<PresetFolder const*> folders; // Sorted
    Span<FolderNode const*> folder_nodes; // Parallel to folders field.

    // Additional convenience data
    Set<String> used_tags;
    Set<sample_lib::LibraryIdRef> used_libraries;
    Set<String> authors;
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};
};

// Trigger the server to start the scanning process if its not already doing so.
void StartScanningIfNeeded(PresetServer& server);

PresetsSnapshot BeginReadFolders(PresetServer& server, ArenaAllocator& arena);
void EndReadFolders(PresetServer& server);
