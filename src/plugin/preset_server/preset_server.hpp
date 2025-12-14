// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/error_notifications.hpp"

#include "common_infrastructure/preset_bank_info.hpp"
#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

// Preset folders are designed to be unconnected to other folders. They are the granular unit of scanning and
// updating. The hierarchy of folders is represented separately in a FolderNode tree - this has to be built
// for a specific point in time, whereas PresetFolders can be created and destroyed as per our epoch-based
// reclamation scheme.
struct PresetFolder {
    struct Preset {
        String name {};
        StateMetadataRef metadata {};
        OrderedSet<sample_lib::LibraryIdRef> used_libraries {};
        u64 file_hash {};
        u64 full_path_hash {};
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

    Optional<PresetBank> preset_bank_info {}; // From metadata file (primary importance)

    u64 all_presets_hash {}; // Hash of all presets in this folder.

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

    // Multiple readers support: track all active reader versions and maintain the oldest
    MutexProtected<DynamicArray<u64>> active_reader_versions {Malloc::Instance()};
    Atomic<u64> oldest_version_in_use = k_no_version;

    // The next fields are versioned and mutex protected
    DynamicArray<PresetFolder*> folders {arena};
    DynamicSet<String> used_tags {arena};
    DynamicSet<sample_lib::LibraryIdRef> used_libraries {arena};
    DynamicSet<String> authors {arena};
    ArenaAllocator folder_node_arena {(Allocator&)arena};
    Span<FolderNode> folder_nodes {};
    Span<usize> folder_node_order_indices {};
    Span<usize> folder_node_preset_bank_indices {};

    DynamicSet<u64, NoHash> preset_file_hashes {arena};
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};

    DynamicArray<ScanFolder> scan_folders {arena};

    Thread thread;
    WorkSignaller work_signaller;
    u64 server_thread_id {};
    Atomic<bool> end_thread {false};

    Atomic<bool> enable_scanning {};
    Atomic<u32> is_scanning {};
};

void InitPresetServer(PresetServer& server, String always_scanned_folder);
void ShutdownPresetServer(PresetServer& server);

void SetExtraScanFolders(PresetServer& server, Span<String const> folders);

// A 'listing' augments a FolderNode with more preset-specific information such as the PresetFolder
// (immutable), and preset bank info. For convenience, the node's user_data is also points to this listing so
// that given only a node you can get the listing.
struct PresetFolderListing {
    // May be null if the folder does not contain any presets, in which case it's just a node in the folder
    // tree.
    PresetFolder const* folder;

    // This is of secondary importance if preset_bank_info is specified in PresetFolder.
    PresetBank const* fallback_preset_bank_info;

    // The node's user_data is this listing.
    FolderNode const& node;
};

Optional<sample_lib::LibraryIdRef> AllPresetsSingleLibrary(FolderNode const& node);
PresetBank const* PresetBankForNode(FolderNode const& node);
PresetBank const* ContainingPresetBank(FolderNode const* node);
bool IsInsideFolder(PresetFolderListing const* node, usize folder_node_hash);

struct PresetsSnapshot {
    // Folders that contain presets, sorted. e.i. these will have PresetFolderListing::folder != null.
    Span<PresetFolderListing const*> folders;

    // Root nodes of all preset banks. All presets are guaranteed to be inside one of these nodes. Presets
    // that aren't explicitly put into packs will be smartly grouped into "Miscellaneous Presets" packs.
    Span<FolderNode const*> preset_banks;

    // Additional convenience data
    Set<String> used_tags;
    Set<sample_lib::LibraryIdRef> used_libraries;
    Set<String> authors;
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};
};

// Opaque handle that must be returned to EndReadFolders when done reading the snapshot.
enum class PresetServerReadHandle : u64 {};

struct BeginReadFoldersResult {
    PresetsSnapshot snapshot;
    PresetServerReadHandle handle;
};

// Trigger the server to start the scanning process if its not already doing so.
void StartScanningIfNeeded(PresetServer& server);

BeginReadFoldersResult BeginReadFolders(PresetServer& server, ArenaAllocator& arena);
void EndReadFolders(PresetServer& server, PresetServerReadHandle handle);

// Waits until all folders have finished scanning.
// Returns true if loading completed, false if timeout was reached. If timeout is nullopt, waits indefinitely.
// If timeout is 0, just returns 'is scanning' status immediately.
// Must have called StartScanningIfNeeded before this.
// [threadsafe]
bool WaitIfFoldersAreScanning(PresetServer& server, Optional<u32> timeout);
bool AreFoldersScanning(PresetServer& server);
