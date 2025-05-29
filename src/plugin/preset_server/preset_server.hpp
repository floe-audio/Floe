// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "state/state_coding.hpp"
#include "state/state_snapshot.hpp"

struct PresetFolder {
    struct Preset {
        String name {};
        StateMetadataRef metadata {};
        DynamicArrayBounded<sample_lib::LibraryIdRef, k_num_layers + 1> used_libraries {};
        u64 file_hash {};
        String file_extension {}; // Only if file_format is Mirage. Mirage had variable extensions.
        PresetFormat file_format {};
    };

    Optional<usize> MatchFullPresetPath(String path) const;
    String FullPathForPreset(Preset const& preset, Allocator& a) const;

    ArenaAllocator arena {Malloc::Instance(), 0, 512};

    String scan_folder {};
    String folder {}; // subpath of scan_folder, if any
    Span<Preset> presets {};

    // private
    usize preset_array_capacity {};
    Optional<u64> delete_after_version {};
};

u64 NoHash(u64 const&);

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

    ArenaList<PresetFolder, false> folder_pool {arena}; // Allocation for folders

    Mutex mutex;

    // We're using a sort of basic "epoch-based reclamation" to delete folders that are no longer in use
    // without the reader having to do much locking.
    Atomic<u64> published_version {};
    Atomic<u64> version_in_use = k_no_version;

    // The next fields are versioned and mutex protected
    DynamicArray<PresetFolder*> folders {arena};
    DynamicSet<String> used_tags {arena};
    DynamicSet<sample_lib::LibraryIdRef, sample_lib::Hash> used_libraries {arena};
    DynamicSet<String> authors {arena};
    ArenaAllocator used_tags_arena {(Allocator&)arena};
    DynamicHashTable<u64, DynamicSet<String>*, NoHash> used_tags_by_library {arena};
    DynamicHashTable<u64, DynamicSet<String>*, NoHash> used_tags_by_library_author {arena};
    DynamicHashTable<u64, DynamicSet<String>*, NoHash> used_tags_by_preset_author {arena};

    DynamicSet<u64, NoHash> preset_file_hashes {arena};
    Array<bool, ToInt(PresetFormat::Count)> has_preset_type {};

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

    // Additional convenience data
    Set<String> used_tags;
    Set<sample_lib::LibraryIdRef, sample_lib::Hash> used_libraries;
    Set<String> authors;
    HashTable<u64, Set<String>, NoHash> used_tags_by_library_hash;
    HashTable<u64, Set<String>, NoHash> used_tags_by_library_author_hash;
    HashTable<u64, Set<String>, NoHash> used_tags_by_preset_author_hash;
    Array<bool, ToInt(PresetFormat::Count)> has_preset_type {};
};

PresetsSnapshot BeginReadFolders(PresetServer& server, ArenaAllocator& arena);
void EndReadFolders(PresetServer& server);
