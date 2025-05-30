// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_server.hpp"

#include <xxhash.h>

#include "state/state_coding.hpp"

static String ExtensionForPreset(PresetFolder::Preset const& preset) {
    switch (preset.file_format) {
        case PresetFormat::Mirage: return preset.file_extension;
        case PresetFormat::Floe: return FLOE_PRESET_FILE_EXTENSION;
        case PresetFormat::Count: PanicIfReached();
    }
}

Optional<usize> PresetFolder::MatchFullPresetPath(String p) const {
    if (!path::IsWithinDirectory(p, scan_folder)) return k_nullopt;

    PathArena scratch_arena {PageAllocator::Instance()};

    DynamicArray<char> path {scan_folder, scratch_arena};
    path::JoinAppend(path, folder);
    auto const path_len = path.size;

    for (auto const [i, preset] : Enumerate(presets)) {
        path::JoinAppend(path, preset.name);
        dyn::AppendSpan(path, ExtensionForPreset(preset));

        if (path == p) return i;

        dyn::Resize(path, path_len);
    }

    return k_nullopt;
}

String PresetFolder::FullPathForPreset(PresetFolder::Preset const& preset, Allocator& a) const {
    auto path = path::Join(a, Array {scan_folder, folder, preset.name});
    path = fmt::JoinAppendResizeAllocation(a, path, Array {ExtensionForPreset(preset)});
    return path;
}

// Reader thread
PresetsSnapshot BeginReadFolders(PresetServer& server, ArenaAllocator& arena) {
    // Trigger the server to start the scanning process if its not already doing so.
    server.enable_scanning.Store(true, StoreMemoryOrder::Relaxed);

    // We tell the server that we're reading the current version so that it knows not to delete any folders
    // that we might be using.
    {
        auto const current_version = server.published_version.Load(LoadMemoryOrder::Acquire);

        auto expected = PresetServer::k_no_version;
        if (!server.version_in_use.CompareExchangeStrong(expected,
                                                         current_version,
                                                         RmwMemoryOrder::AcquireRelease,
                                                         LoadMemoryOrder::Relaxed)) {
            PanicF(
                SourceLocation::Current(),
                "we only allow one reader, and it must not re-enter this function while it's already reading");
        }
    }

    // We take a snapshot the the folders list so that the server can continue to modify it while we're
    // reading and we don't have to do locking or reference counting.
    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };
    auto const folders = arena.Clone(server.folders);

    return {
        .folders = {(PresetFolder const**)folders.data, folders.size},
        .used_tags = {server.used_tags.table.Clone(arena, CloneType::Deep)},
        .used_libraries = {server.used_libraries.table.Clone(arena, CloneType::Deep)},
        .authors = {server.authors.table.Clone(arena, CloneType::Deep)},
        .has_preset_type = server.has_preset_type,
    };
}

void EndReadFolders(PresetServer& server) {
    server.version_in_use.Store(PresetServer::k_no_version, StoreMemoryOrder::Release);
}

static bool FolderIsSafeForDeletion(PresetFolder const& folder, u64 current_version, u64 in_use_version) {
    if (!folder.delete_after_version) return false;

    // If the folder was removed in a previous version, we would like to delete it if we can.
    if (*folder.delete_after_version < current_version) {

        // If there was no reader at the time we checked we can delete it because if a new
        // reader were to have started, it would have seen the current version and used that.
        if (in_use_version == PresetServer::k_no_version) return true;

        // If there were readers at the time we checked, we just need to make sure that the readers
        // are using a version after the folder was removed.
        if (in_use_version > *folder.delete_after_version) return true;
    }

    return false;
}

static void DeleteUnusedFolders(PresetServer& server) {
    ASSERT(CurrentThreadId() == server.server_thread_id);

    auto const current_version = server.published_version.Load(LoadMemoryOrder::Relaxed);
    auto const in_use_version = server.version_in_use.Load(LoadMemoryOrder::Acquire);

    server.folder_pool.RemoveIf([&](PresetFolder const& folder) {
        if (FolderIsSafeForDeletion(folder, current_version, in_use_version)) {
            LogDebug(
                ModuleName::PresetServer,
                "Deleting folder: {}, current_version: {}, in_use_version: {}, folder_deleted_version: {}",
                folder.folder,
                current_version,
                in_use_version,
                *folder.delete_after_version);
            return true;
        }
        return false;
    });
}

static void AddPresetToFolder(PresetFolder& folder,
                              dir_iterator::Entry const& entry,
                              StateSnapshot const& state,
                              u64 file_hash,
                              PresetFormat file_format) {
    auto presets = DynamicArray<PresetFolder::Preset>::FromOwnedSpan(folder.presets,
                                                                     folder.preset_array_capacity,
                                                                     folder.arena);

    dyn::Append(presets,
                PresetFolder::Preset {
                    .name = folder.arena.Clone(path::FilenameWithoutExtension(entry.subpath)),
                    .metadata {
                        .tags = ({
                            auto tags = Set<String>::Create(folder.arena, state.metadata.tags.size);
                            for (auto const tag : state.metadata.tags) {
                                bool const inserted =
                                    tags.InsertWithoutGrowing(folder.arena.Clone(String(tag)));
                                ASSERT(inserted);
                            }
                            tags;
                        }),
                        .author = folder.arena.Clone(state.metadata.author),
                        .description = folder.arena.Clone(state.metadata.description),
                    },
                    .used_libraries = ({
                        decltype(PresetFolder::Preset::used_libraries) used_libraries {};

                        for (auto const& inst_id : state.inst_ids) {
                            if (auto const& sampled_inst = inst_id.TryGet<sample_lib::InstrumentId>()) {
                                auto const lib_id = (sample_lib::LibraryIdRef)sampled_inst->library;
                                if (!Contains(used_libraries, lib_id))
                                    dyn::Append(used_libraries, lib_id.Clone(folder.arena));
                            }
                        }

                        if (state.ir_id) {
                            auto const lib_id = (sample_lib::LibraryIdRef)state.ir_id->library;
                            if (!Contains(used_libraries, lib_id))
                                dyn::Append(used_libraries, lib_id.Clone(folder.arena));
                        }

                        used_libraries;
                    }),
                    .file_hash = file_hash,
                    .file_extension = file_format == PresetFormat::Mirage
                                          ? (String)folder.arena.Clone(path::Extension(entry.subpath))
                                          : ""_s,
                    .file_format = file_format,
                });

    auto const [items, cap] = presets.ToOwnedSpanUnchangedCapacity();
    folder.presets = items;
    folder.preset_array_capacity = cap;
}

struct FoldersAggregateInfo {
    FoldersAggregateInfo(ArenaAllocator& arena)
        : used_tags {arena}
        , used_libraries {arena}
        , authors {arena} {}

    void AddPreset(PresetFolder::Preset const& preset) {
        for (auto const [tag, tag_hash] : preset.metadata.tags)
            used_tags.Insert(tag, tag_hash);

        for (auto const& library_id : preset.used_libraries)
            used_libraries.Insert(library_id);

        if (preset.metadata.author.size) authors.Insert(preset.metadata.author);

        has_preset_type.Set(ToInt(preset.file_format));
    }

    void CopyToServer(PresetServer& server) const {
        server.used_tags.Assign(used_tags);
        server.used_libraries.Assign(used_libraries);
        server.authors.Assign(authors);

        server.used_tags_arena.ResetCursorAndConsolidateRegions();

        server.has_preset_type = has_preset_type;
    }

    DynamicSet<String> used_tags;
    DynamicSet<sample_lib::LibraryIdRef> used_libraries;
    DynamicSet<String> authors;
    Bitset<ToInt(PresetFormat::Count)> has_preset_type {};
};

static void
AppendFolderAndPublish(PresetServer& server, PresetFolder* new_preset_folder, ArenaAllocator& scratch_arena) {
    // Tags and libraries point to memory within each folder, so they share the same versioning as the
    // folders.

    FoldersAggregateInfo info {scratch_arena};
    for (auto const& folder_set :
         Array {server.folders.Items(), Span<PresetFolder*> {&new_preset_folder, 1}}) {
        for (auto const folder : folder_set)
            for (auto const& preset : folder->presets)
                info.AddPreset(preset);
    }

    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };

    dyn::Append(server.folders, new_preset_folder);
    Sort(server.folders, [](PresetFolder const* a, PresetFolder const* b) { return a->folder < b->folder; });

    info.CopyToServer(server);

    server.published_version.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
}

static void RemoveFolderAndPublish(PresetServer& server, usize index, ArenaAllocator& scratch_arena) {
    auto& folder = *server.folders[index];
    folder.delete_after_version = server.published_version.Load(LoadMemoryOrder::Relaxed);
    for (auto const& preset : folder.presets)
        server.preset_file_hashes.Delete(preset.file_hash);

    FoldersAggregateInfo info {scratch_arena};
    for (auto const& existing_folder : server.folders) {
        if (existing_folder == &folder) continue;

        for (auto const& preset : existing_folder->presets)
            info.AddPreset(preset);
    }

    server.mutex.Lock();
    DEFER { server.mutex.Unlock(); };

    dyn::Remove(server.folders, index);

    info.CopyToServer(server);

    server.published_version.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
}

static bool EntryIsPreset(dir_iterator::Entry const& entry) {
    auto const ext = path::Extension(entry.subpath);
    return ext == FLOE_PRESET_FILE_EXTENSION || StartsWithSpan(ext, ".mirage"_s);
}

ErrorCodeOr<void> ScanFolder(PresetServer& server,
                             String subfolder_of_scan_folder,
                             ArenaAllocator& scratch_arena,
                             PresetServer::ScanFolder const& scan_folder,
                             bool recursive) {
    ASSERT(CurrentThreadId() == server.server_thread_id);
    auto const absolute_folder =
        (String)path::Join(scratch_arena, Array {(String)scan_folder.path, subfolder_of_scan_folder});

    auto const entries = TRY(FindEntriesInFolder(scratch_arena,
                                                 absolute_folder,
                                                 {
                                                     .options =
                                                         {
                                                             .wildcard = "*",
                                                             .get_file_size = false,
                                                             .skip_dot_files = true,
                                                         },
                                                     .recursive = false,
                                                     .only_file_type = k_nullopt,
                                                 }));

    PresetFolder* preset_folder {};

    for (auto const& entry : entries) {
        if (!EntryIsPreset(entry)) continue;

        if constexpr (IS_WINDOWS) Replace(entry.subpath, '\\', '/');

        auto const file_data = TRY_OR(
            ReadEntireFile(path::Join(scratch_arena, Array {absolute_folder, entry.subpath}), scratch_arena),
            continue);
        DEFER { scratch_arena.Free(file_data.ToByteSpan()); };

        auto const file_hash = XXH3_64bits(file_data.data, file_data.size) + Hash((String)entry.subpath);

        if (server.preset_file_hashes.Contains(file_hash)) continue;
        server.preset_file_hashes.Insert(file_hash);

        auto const preset_format = PresetFormatFromPath(entry.subpath);

        auto reader = Reader::FromMemory(file_data);
        auto const snapshot = TRY_OR(LoadPresetFile(preset_format, reader, scratch_arena, true), continue);

        if (!preset_folder) {
            preset_folder = server.folder_pool.PrependUninitialised();
            PLACEMENT_NEW(preset_folder) PresetFolder();
            preset_folder->scan_folder = preset_folder->arena.Clone(scan_folder.path);
            preset_folder->folder = preset_folder->arena.Clone(subfolder_of_scan_folder);
        }

        AddPresetToFolder(*preset_folder, entry, snapshot, file_hash, preset_format);
    }

    if (preset_folder) {
        Sort(preset_folder->presets,
             [](PresetFolder::Preset const& a, PresetFolder::Preset const& b) { return a.name < b.name; });

        AppendFolderAndPublish(server, preset_folder, scratch_arena);
    }

    if (recursive) {
        for (auto const& entry : entries) {
            if (entry.type == FileType::Directory) {
                TRY(ScanFolder(server,
                               path::Join(scratch_arena, Array {subfolder_of_scan_folder, entry.subpath}),
                               scratch_arena,
                               scan_folder,
                               recursive));
            }
        }
    }

    return k_success;
}

static ErrorCodeOr<void>
ScanFolder(PresetServer& server, ArenaAllocator& scratch_arena, PresetServer::ScanFolder& scan_folder) {
    if (scan_folder.scanned) return k_success;
    scan_folder.scanned = true;
    TRY(ScanFolder(server, "", scratch_arena, scan_folder, true));
    return k_success;
}

static void ServerThread(PresetServer& server) {
    server.server_thread_id = CurrentThreadId();

    auto watcher = ({
        Optional<DirectoryWatcher> w {};
        auto o = CreateDirectoryWatcher(PageAllocator::Instance());
        if (o.HasValue()) w = o.ReleaseValue();
        Move(w);
    });
    DEFER {
        if (PanicOccurred()) return;
        if (watcher) DestoryDirectoryWatcher(*watcher);
    };

    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    while (!server.end_thread.Load(LoadMemoryOrder::Relaxed)) {
        scratch_arena.ResetCursorAndConsolidateRegions();

        server.work_signaller.WaitUntilSignalledOrSpurious(250u);

        if (!server.enable_scanning.Load(LoadMemoryOrder::Relaxed)) continue;

        // Consume scan folder request
        {
            server.scan_folders_request_mutex.Lock();
            DEFER { server.scan_folders_request_mutex.Unlock(); };

            if (server.scan_folders_request) {
                dyn::RemoveValueIfSwapLast(
                    server.scan_folders,
                    [&](PresetServer::ScanFolder const& scan_folder) {
                        // Never remove the always scanned folder.
                        if (scan_folder.always_scanned_folder) return false;

                        // We don't remove the folder if it's in the new set of folders.
                        if (path::Contains(*server.scan_folders_request, scan_folder.path)) return false;

                        // The folder is not in the new set of folders. We should remove the preset folders
                        // that relate to it so they disappear from the listing.
                        for (usize i = 0; i < server.folders.size;) {
                            auto& preset_folder = *server.folders[i];
                            if (preset_folder.scan_folder == scan_folder.path)
                                RemoveFolderAndPublish(server, i, scratch_arena);
                            else
                                ++i;
                        }

                        return true;
                    });

                for (auto const& path : *server.scan_folders_request) {
                    bool already_exists = false;
                    for (auto& f : server.scan_folders) {
                        if (path::Equal(f.path, path)) {
                            already_exists = true;
                            break;
                        }
                    }

                    if (already_exists) continue;

                    dyn::Append(server.scan_folders,
                                PresetServer::ScanFolder {
                                    .always_scanned_folder = false,
                                    .path = {server.arena.Clone(path)},
                                    .scanned = false,
                                });
                }

                server.scan_folders_request = k_nullopt;
            }

            server.scan_folders_request_arena.FreeAll();
        }

        if (watcher) {
            auto const dirs_to_watch = ({
                DynamicArray<DirectoryToWatch> dirs {scratch_arena};
                for (auto& f : server.scan_folders) {
                    dyn::Append(dirs,
                                {
                                    .path = f.path,
                                    .recursive = true,
                                    .user_data = &f,
                                });
                }
                dirs.ToOwnedSpan();
            });

            // Batch up changes.
            DynamicArray<PresetServer::ScanFolder*> rescan_folders {scratch_arena};

            if (auto const outcome = PollDirectoryChanges(*watcher,
                                                          {
                                                              .dirs_to_watch = dirs_to_watch,
                                                              .retry_failed_directories = false,
                                                              .result_arena = scratch_arena,
                                                              .scratch_arena = scratch_arena,
                                                          });
                outcome.HasError()) {
                // IMPROVE: handle error
                LogDebug(ModuleName::SampleLibraryServer,
                         "Reading directory changes failed: {}",
                         outcome.Error());
            } else {
                auto const dir_changes_span = outcome.Value();
                for (auto const& dir_changes : dir_changes_span) {
                    bool found = false;
                    for (auto& f : server.scan_folders) {
                        if ((void*)&f == dir_changes.linked_dir_to_watch->user_data) {
                            found = true;
                            break;
                        }
                    }
                    ASSERT(found);

                    auto& scan_folder =
                        *(PresetServer::ScanFolder*)dir_changes.linked_dir_to_watch->user_data;

                    if (dir_changes.error) {
                        // IMPROVE: handle this
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "Reading directory changes failed for {}: {}",
                                 scan_folder.path,
                                 dir_changes.error);
                        continue;
                    }

                    for (auto const& subpath_changeset : dir_changes.subpath_changesets) {
                        // Changes to the watched directory itself.
                        if (subpath_changeset.subpath.size == 0) continue;

                        // For now, we ignore the granularity of the changes and just rescan the whole folder.
                        // IMPROVE: handle changes more granularly
                        dyn::AppendIfNotAlreadyThere(rescan_folders, &scan_folder);
                    }
                }
            }

            for (auto scan_folder : rescan_folders) {
                for (usize i = 0; i < server.folders.size;) {
                    auto& preset_folder = *server.folders[i];
                    if (preset_folder.scan_folder == scan_folder->path)
                        RemoveFolderAndPublish(server, i, scratch_arena);
                    else
                        ++i;
                }

                scan_folder->scanned = false; // force a rescan
            }
        }

        for (auto& scan_folder : server.scan_folders) {
            auto const o = ScanFolder(server, scratch_arena, scan_folder);
            if (o.HasError()) {
                if (!scan_folder.always_scanned_folder) {
                    auto const err = server.error_notifications.NewError();
                    err->value = {
                        .title = "Failed to scan presets folder"_s,
                        .message = scan_folder.path,
                        .error_code = o.Error(),
                        .id = ThreadsafeErrorNotifications::Id("prss", scan_folder.path),
                    };
                    server.error_notifications.AddOrUpdateError(err);
                }
            }
        }

        DeleteUnusedFolders(server);
    }

    ASSERT(server.version_in_use.Load(LoadMemoryOrder::Relaxed) == PresetServer::k_no_version);
    server.folder_pool.Clear();
}

void SetExtraScanFolders(PresetServer& server, Span<String const> folders) {
    server.scan_folders_request_mutex.Lock();
    DEFER { server.scan_folders_request_mutex.Unlock(); };

    server.scan_folders_request = server.scan_folders_request_arena.Clone(folders, CloneType::Deep);
}

void InitPresetServer(PresetServer& server, String always_scanned_folder) {
    dyn::Append(server.scan_folders,
                {
                    .always_scanned_folder = true,
                    // We can use the server arena directly because the server thread isn't running yet.
                    .path = {server.arena.Clone(always_scanned_folder)},
                });

    server.thread.Start([&server]() { ServerThread(server); }, "presets");
}

void ShutdownPresetServer(PresetServer& server) {
    server.end_thread.Store(true, StoreMemoryOrder::Release);
    server.work_signaller.Signal();
    server.thread.Join();
}
