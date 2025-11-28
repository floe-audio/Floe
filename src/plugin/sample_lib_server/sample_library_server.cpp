// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_library_server.hpp"

#include <xxhash.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/sample_library/audio_file.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "build_resources/embedded_files.h"

namespace sample_lib_server {

using namespace detail;
constexpr String k_trace_category = "SLS";
constexpr u32 k_trace_colour = 0xfcba03;

// ==========================================================================================================
// Scan folders

ScanFolders::Folders CopyActiveFolders(ScanFolders& folders) {
    folders.mutex.Lock();
    DEFER { folders.mutex.Unlock(); };
    auto const result = folders.folders;
    return result;
}

// You must not have a copy of any active folders when you call this.
void DeleteUnusedScanFolders(ScanFolders& folders) {
    folders.mutex.Lock();
    DEFER { folders.mutex.Unlock(); };

    folders.folder_allocator.RemoveIf([&](ScanFolder const& f) {
        // Delete folders that are not in the active list.
        return !Contains(folders.folders, &f);
    });
}

// ==========================================================================================================
// Library loading

struct PendingLibraryJobs {
    struct Job {
        enum class Type { ReadLibrary, ScanFolder };

        struct ReadLibrary {
            struct Args {
                PathOrMemory path_or_memory;
                sample_lib::FileFormat format;
                LibrariesAtomicList& libraries;
            };
            struct Result {
                ArenaAllocator arena {PageAllocator::Instance()};
                Optional<sample_lib::LibraryPtrOrError> result {};
            };

            Args args;
            Result result {};
        };

        struct ScanFolder {
            struct Args {
                detail::ScanFolder* folder;
                LibrariesAtomicList& libraries;
            };
            struct Result {
                ErrorCodeOr<void> outcome {};
            };

            Args args;
            Result result {};
        };

        using DataUnion = TaggedUnion<Type,
                                      TypeAndTag<ReadLibrary*, Type::ReadLibrary>,
                                      TypeAndTag<ScanFolder*, Type::ScanFolder>>;

        DataUnion data;
        Job* next {nullptr};
        Atomic<bool> completed {false};
        bool result_handled {};
    };

    u64 server_thread_id;
    ThreadPool& thread_pool;
    WorkSignaller& work_signaller;
    Atomic<u32>& num_uncompleted_jobs;
    Span<ScanFolder*> const folders;

    Mutex job_mutex;
    ArenaAllocator job_arena {PageAllocator::Instance()};
    Atomic<Job*> jobs {};
};

static void ReadLibraryAsync(PendingLibraryJobs& pending_library_jobs,
                             LibrariesAtomicList& lib_list,
                             PathOrMemory path_or_memory,
                             sample_lib::FileFormat format);

static void DoReadLibraryJob(PendingLibraryJobs::Job::ReadLibrary& job, ArenaAllocator& scratch_arena) {
    ZoneScopedN("read library");

    auto const& args = job.args;
    String const path = args.path_or_memory.Is<String>() ? args.path_or_memory.Get<String>() : ":memory:";
    ZoneText(path.data, path.size);

    auto const try_read = [&]() -> Optional<sample_lib::LibraryPtrOrError> {
        using H = sample_lib::TryHelpersOutcomeToError;
        auto path_or_memory = args.path_or_memory;
        if (args.format == sample_lib::FileFormat::Lua && args.path_or_memory.Is<String>()) {
            // it will be more efficient to just load the whole lua into memory
            path_or_memory =
                TRY_H(ReadEntireFile(args.path_or_memory.Get<String>(), scratch_arena)).ToConstByteSpan();
        }

        auto reader = TRY_H(Reader::FromPathOrMemory(path_or_memory));
        auto const file_hash = TRY_H(sample_lib::Hash(path, reader, args.format));

        for (auto& node : args.libraries) {
            if (auto l = node.TryScoped()) {
                if (l->lib->file_hash == file_hash && l->lib->path == path) return k_nullopt;
            }
        }

        auto lib = TRY(sample_lib::Read(reader, args.format, path, job.result.arena, scratch_arena));
        lib->file_hash = file_hash;
        return lib;
    };

    job.result.result = try_read();
}

static void DoScanFolderJob(PendingLibraryJobs::Job::ScanFolder& job,
                            ArenaAllocator& scratch_arena,
                            PendingLibraryJobs& pending_library_jobs,
                            LibrariesAtomicList& lib_list) {
    auto folder = job.args.folder;
    ASSERT(folder);

    auto const& path = folder->path;
    ZoneScoped;
    ZoneText(path.data, path.size);

    auto const try_job = [&]() -> ErrorCodeOr<void> {
        auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                    path,
                                                    {
                                                        .wildcard = "*",
                                                        .get_file_size = false,
                                                    }));
        DEFER { dir_iterator::Destroy(it); };
        while (auto const entry = TRY(dir_iterator::Next(it, scratch_arena))) {
            if (ContainsSpan(entry->subpath, k_temporary_directory_prefix)) continue;
            auto const full_path = dir_iterator::FullPath(it, *entry, scratch_arena);
            if (auto format = sample_lib::DetermineFileFormat(full_path))
                ReadLibraryAsync(pending_library_jobs, lib_list, String(full_path), *format);
        }
        return k_success;
    };

    job.result.outcome = try_job();
}

// threadsafe
static void AddAsyncJob(PendingLibraryJobs& pending_library_jobs,
                        LibrariesAtomicList& lib_list,
                        PendingLibraryJobs::Job::DataUnion data) {
    ZoneNamed(add_job, true);
    PendingLibraryJobs::Job* job;
    {
        pending_library_jobs.job_mutex.Lock();
        DEFER { pending_library_jobs.job_mutex.Unlock(); };

        job = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job>();
        PLACEMENT_NEW(job)
        PendingLibraryJobs::Job {
            .data = data,
            .next = pending_library_jobs.jobs.Load(LoadMemoryOrder::Relaxed),
            .result_handled = false,
        };
        pending_library_jobs.jobs.Store(job, StoreMemoryOrder::Release);
    }

    pending_library_jobs.num_uncompleted_jobs.FetchAdd(1, RmwMemoryOrder::AcquireRelease);

    pending_library_jobs.thread_pool.AddJob([&pending_library_jobs, &job = *job, &lib_list]() {
        try {
            ZoneNamed(do_job, true);
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            switch (job.data.tag) {
                case PendingLibraryJobs::Job::Type::ReadLibrary: {
                    DoReadLibraryJob(*job.data.Get<PendingLibraryJobs::Job::ReadLibrary*>(), scratch_arena);
                    break;
                }
                case PendingLibraryJobs::Job::Type::ScanFolder: {
                    DoScanFolderJob(*job.data.Get<PendingLibraryJobs::Job::ScanFolder*>(),
                                    scratch_arena,
                                    pending_library_jobs,
                                    lib_list);
                    break;
                }
            }

            job.completed.Store(true, StoreMemoryOrder::Release);
            pending_library_jobs.work_signaller.Signal();
        } catch (PanicException) {
            // pass
        }
    });
}

// threadsafe
static void ReadLibraryAsync(PendingLibraryJobs& pending_library_jobs,
                             LibrariesAtomicList& lib_list,
                             PathOrMemory path_or_memory,
                             sample_lib::FileFormat format) {
    auto read_job = ({
        pending_library_jobs.job_mutex.Lock();
        DEFER { pending_library_jobs.job_mutex.Unlock(); };
        auto j = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job::ReadLibrary>();
        PLACEMENT_NEW(j)
        PendingLibraryJobs::Job::ReadLibrary {
            .args =
                {
                    .path_or_memory = path_or_memory.Is<String>()
                                          ? PathOrMemory(String(pending_library_jobs.job_arena.Clone(
                                                path_or_memory.Get<String>())))
                                          : path_or_memory,
                    .format = format,
                    .libraries = lib_list,
                },
            .result = {},
        };
        j;
    });

    AddAsyncJob(pending_library_jobs, lib_list, read_job);
}

static bool MarkNotScannedFoldersRescanRequested(Span<ScanFolder*> scan_folders) {
    bool any_rescan_requested = false;
    for (auto& f : scan_folders) {
        ASSERT(f);
        auto expected = ScanFolder::State::NotScanned;
        if (f->state.CompareExchangeStrong(expected,
                                           ScanFolder::State::RescanRequested,
                                           RmwMemoryOrder::AcquireRelease,
                                           LoadMemoryOrder::Acquire))
            any_rescan_requested = true;
    }
    return any_rescan_requested;
}

// server-thread
static void NotifyAllChannelsOfLibraryChange(Server& server, sample_lib::LibraryIdRef library_id) {
    server.channels.Use([&](ArenaList<AsyncCommsChannel>& channels) {
        for (auto& c : channels)
            if (c.used.Load(LoadMemoryOrder::Relaxed)) c.library_changed_callback(library_id);
    });
}

// server-thread
static bool UpdateLibraryJobs(Server& server,
                              PendingLibraryJobs& pending_library_jobs,
                              ArenaAllocator& scratch_arena,
                              Optional<DirectoryWatcher>& watcher) {
    ASSERT_EQ(CurrentThreadId(), pending_library_jobs.server_thread_id);
    ZoneNamed(outer, true);

    // Trigger folder scanning they're marked as 'rescan-requested'.
    for (auto& f : pending_library_jobs.folders) {
        ASSERT(f);
        auto expected = ScanFolder::State::RescanRequested;
        if (!f->state.CompareExchangeStrong(expected,
                                            ScanFolder::State::Scanning,
                                            RmwMemoryOrder::AcquireRelease,
                                            LoadMemoryOrder::Acquire))
            continue;

        PendingLibraryJobs::Job::ScanFolder* scan_job;
        {
            pending_library_jobs.job_mutex.Lock();
            DEFER { pending_library_jobs.job_mutex.Unlock(); };
            scan_job = pending_library_jobs.job_arena.NewUninitialised<PendingLibraryJobs::Job::ScanFolder>();
            PLACEMENT_NEW(scan_job)
            PendingLibraryJobs::Job::ScanFolder {
                .args =
                    {
                        .folder = f,
                        .libraries = server.libraries,
                    },
                .result = {},
            };
        }

        AddAsyncJob(pending_library_jobs, server.libraries, scan_job);
    }

    // Handle async jobs that have completed.
    for (auto node = pending_library_jobs.jobs.Load(LoadMemoryOrder::Acquire); node != nullptr;
         node = node->next) {
        if (node->result_handled) continue;
        if (!node->completed.Load(LoadMemoryOrder::Acquire)) continue;

        DEFER {
            node->result_handled = true;
            pending_library_jobs.num_uncompleted_jobs.FetchSub(1, RmwMemoryOrder::AcquireRelease);
        };
        auto const& job = *node;
        switch (job.data.tag) {
            case PendingLibraryJobs::Job::Type::ReadLibrary: {
                auto& j = *job.data.Get<PendingLibraryJobs::Job::ReadLibrary*>();
                DEFER { j.PendingLibraryJobs::Job::ReadLibrary::~ReadLibrary(); };
                auto const& args = j.args;
                String const path =
                    args.path_or_memory.Is<String>() ? args.path_or_memory.Get<String>() : ":memory:";
                ZoneScopedN("job completed: library read");
                ZoneText(path.data, path.size);
                if (!j.result.result) {
                    TracyMessageEx({k_trace_category, k_trace_colour, {}},
                                   "skipping {}, it already exists",
                                   path::Filename(path));
                    continue;
                }
                auto const& outcome = *j.result.result;

                auto const error_id = HashMultiple(Array {"sls-read-lib"_s, path});

                switch (outcome.tag) {
                    case ResultType::Value: {
                        auto lib = outcome.GetFromTag<ResultType::Value>();
                        TracyMessageEx({k_trace_category, k_trace_colour, {}},
                                       "adding new library {}",
                                       path::Filename(path));

                        bool not_wanted = false;

                        // Check if we actually want this library.
                        for (auto it = server.libraries.begin(); it != server.libraries.end();)
                            if (path::Equal(it->value.lib->path, lib->path)) {
                                it = server.libraries.Remove(it);
                                NotifyAllChannelsOfLibraryChange(server, lib->Id());
                            } else if (it->value.lib->Id() == lib->Id()) {
                                if (it->value.lib->minor_version > lib->minor_version) {
                                    not_wanted = true; // The existing library is newer.
                                    ++it;
                                } else {
                                    it = server.libraries.Remove(it);
                                    NotifyAllChannelsOfLibraryChange(server, lib->Id());
                                }
                            } else {
                                ++it;
                            }

                        if (!not_wanted) {
                            auto new_node = server.libraries.AllocateUninitialised();
                            PLACEMENT_NEW(&new_node->value)
                            ListedLibrary {
                                .arena = Move(j.result.arena),
                                .lib = lib,
                                .scan_timepoint = TimePoint::Now(),
                            };
                            server.libraries.Insert(new_node);
                        }

                        server.error_notifications.RemoveError(error_id);
                        break;
                    }
                    case ResultType::Error: {
                        auto const error = outcome.GetFromTag<ResultType::Error>();
                        if (error.code == FilesystemError::PathDoesNotExist) {
                            for (auto it = server.libraries.begin(); it != server.libraries.end();)
                                if (it->value.lib->path == path)
                                    it = server.libraries.Remove(it);
                                else
                                    ++it;
                            continue;
                        }

                        if (auto err = server.error_notifications.BeginWriteError(error_id)) {
                            DEFER { server.error_notifications.EndWriteError(*err); };
                            dyn::AssignFitInCapacity(err->title, "Failed to read library"_s);
                            dyn::AssignFitInCapacity(err->message, path);
                            if (error.message.size) fmt::Append(err->message, "\n{}\n", error.message);
                            err->error_code = error.code;
                        }
                        break;
                    }
                }

                break;
            }
            case PendingLibraryJobs::Job::Type::ScanFolder: {
                auto const& j = *job.data.Get<PendingLibraryJobs::Job::ScanFolder*>();
                DEFER { j.PendingLibraryJobs::Job::ScanFolder::~ScanFolder(); };
                auto folder = j.args.folder;
                ASSERT(folder);
                auto const& path = folder->path;
                ZoneScopedN("job completed: folder scanned");
                ZoneText(path.data, path.size);

                auto const error_id = HashMultiple(Array {"sls-scan-folder"_s, path});

                ScanFolder::State new_state {};

                if (!j.result.outcome.HasError()) {
                    server.error_notifications.RemoveError(error_id);
                    new_state = ScanFolder::State::ScannedSuccessfully;
                } else {
                    auto const is_always_scanned_folder =
                        folder->source == ScanFolder::Source::AlwaysScannedFolder;
                    if (!(is_always_scanned_folder &&
                          j.result.outcome.Error() == FilesystemError::PathDoesNotExist)) {

                        if (auto err = server.error_notifications.BeginWriteError(error_id)) {
                            DEFER { server.error_notifications.EndWriteError(*err); };
                            dyn::AssignFitInCapacity(err->title, "Failed to scan library folder"_s);
                            dyn::AssignFitInCapacity(err->message, path);
                            err->error_code = j.result.outcome.Error();
                        }
                    }
                    new_state = ScanFolder::State::ScanFailed;
                }

                // This scan folder might have been given another request for a rescan while it was
                // mid-scan. We want to honour that request still, so we use a CAS to ensure that we only
                // mark this as completed if no rescan request was given.

                // The server thread will trigger a new scanning job if the state is RescanRequested
                // (simultaneously turning the state to Scanning). There could already be a scanning job
                // running though. The first scanning job should not overwrite the state if it is
                // RescanRequested because the server thread needs to see RescanRequested in order to trigger
                // a new scanning job.
                {
                    usize deadlock_count = 0;
                    auto state = folder->state.Load(LoadMemoryOrder::Acquire);
                    while (true) {
                        if (state == ScanFolder::State::RescanRequested) {
                            // Don't overwrite RescanRequested - let server thread handle it.
                            break;
                        }

                        if (folder->state.CompareExchangeWeak(state,
                                                              new_state,
                                                              RmwMemoryOrder::AcquireRelease,
                                                              LoadMemoryOrder::Acquire)) {
                            // Successfully updated to new_state.
                            break;
                        }
                        // State was automatically updated by CompareExchangeWeak, try again.
                        ++deadlock_count;
                        ASSERT(deadlock_count < 10000);
                    }
                }

                break;
            }
        }
    }

    // check if the scan-folders have changed
    if (watcher) {
        ZoneNamedN(fs_watch, "fs watch", true);

        auto const dirs_to_watch = ({
            DynamicArray<DirectoryToWatch> dirs {scratch_arena};
            for (auto f : pending_library_jobs.folders)
                if (f->state.Load(LoadMemoryOrder::Acquire) == ScanFolder::State::ScannedSuccessfully)
                    dyn::Append(dirs,
                                {
                                    .path = f->path,
                                    .recursive = true,
                                    .user_data = f,
                                });
            dirs.ToOwnedSpan();
        });

        // we buffer these up so we don't spam the channels with notifications
        DynamicArray<LibrariesAtomicList::Node*> libraries_that_changed {scratch_arena};

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
        } else if (!server.disable_file_watching.Load(LoadMemoryOrder::Relaxed)) {
            auto const dir_changes_span = outcome.Value();
            for (auto const& dir_changes : dir_changes_span) {
                ASSERT(FindIf(pending_library_jobs.folders, [&](ScanFolder* f) {
                    return f == dir_changes.linked_dir_to_watch->user_data;
                }));
                auto& scan_folder = *(ScanFolder*)dir_changes.linked_dir_to_watch->user_data;

                if (dir_changes.error) {
                    // IMPROVE: handle this
                    LogDebug(ModuleName::SampleLibraryServer,
                             "Reading directory changes failed for {}: {}",
                             scan_folder.path,
                             dir_changes.error);
                    continue;
                }

                for (auto const& subpath_changeset : dir_changes.subpath_changesets) {
                    if (subpath_changeset.changes & DirectoryWatcher::ChangeType::ManualRescanNeeded) {
                        scan_folder.state.Store(ScanFolder::State::RescanRequested,
                                                StoreMemoryOrder::Release);
                        continue;
                    }

                    // Changes to the watched directory itself.
                    if (subpath_changeset.subpath.size == 0) continue;

                    auto const full_path =
                        path::Join(scratch_arena,
                                   Array {(String)scan_folder.path, subpath_changeset.subpath});

                    // If a directory has been renamed, it might have moved from somewhere else and it
                    // might contain libraries. We need to rescan because we likely won't get 'created'
                    // notifications for the files inside it.
                    if (subpath_changeset.changes & (DirectoryWatcher::ChangeType::RenamedNewName |
                                                     DirectoryWatcher::ChangeType::RenamedOldOrNewName)) {
                        auto const file_type = ({
                            Optional<FileType> t {};
                            if (subpath_changeset.file_type)
                                t = subpath_changeset.file_type;
                            else if (auto const o = GetFileType(full_path); o.HasValue())
                                t = o.Value();
                            t;
                        });

                        if (file_type == FileType::Directory) {
                            scan_folder.state.Store(ScanFolder::State::RescanRequested,
                                                    StoreMemoryOrder::Release);
                            continue;
                        }
                    }

                    if (auto const lib_format = sample_lib::DetermineFileFormat(full_path)) {
                        // We queue-up a scan of the file. It will handle new/deleted/modified.
                        ReadLibraryAsync(pending_library_jobs,
                                         server.libraries,
                                         String(full_path),
                                         *lib_format);
                    } else {
                        for (auto& node : server.libraries) {
                            auto const& lib = *node.value.lib;
                            if (lib.file_format_specifics.tag != sample_lib::FileFormat::Lua) continue;
                            auto const lib_dir = TRY_OPT_OR(path::Directory(lib.path), continue);

                            if (path::Equal(full_path, lib_dir)) {
                                // The library folder itself has changed. We queue-up a scan of the library.
                                // It will handle new/deleted/modified.
                                ReadLibraryAsync(pending_library_jobs,
                                                 server.libraries,
                                                 lib.path,
                                                 lib.file_format_specifics.tag);
                            } else if (path::IsWithinDirectory(full_path, lib_dir)) {
                                if (path::Equal(path::Extension(full_path), ".lua")) {
                                    // If the file is a Lua file, it's probably a file used by the main lua
                                    // file. We need to rescan the library.
                                    ReadLibraryAsync(pending_library_jobs,
                                                     server.libraries,
                                                     lib.path,
                                                     lib.file_format_specifics.tag);
                                } else {
                                    // Something within the library folder has changed
                                    dyn::AppendIfNotAlreadyThere(libraries_that_changed, &node);

                                    for (auto& d : node.value.audio_datas) {
                                        auto const full_audio_path =
                                            path::Join(scratch_arena, Array {lib_dir, d.path.str});
                                        if (path::Equal(full_audio_path, full_path)) d.file_modified = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        for (auto& l : libraries_that_changed)
            NotifyAllChannelsOfLibraryChange(server, l->value.lib->Id());
    }

    // remove libraries that are not in any active scan-folders
    for (auto it = server.libraries.begin(); it != server.libraries.end();) {
        auto const& lib = *it->value.lib;

        bool within_any_folder = false;
        if (lib.Id() == sample_lib::k_builtin_library_id)
            within_any_folder = true;
        else
            for (auto folder : pending_library_jobs.folders) {
                ASSERT(folder);
                if (path::IsWithinDirectory(lib.path, folder->path)) {
                    within_any_folder = true;
                    break;
                }
            }

        if (!within_any_folder)
            it = server.libraries.Remove(it);
        else
            ++it;
    }

    // Remove libraries do not exist on the filesystem.
    for (auto it = server.libraries.begin(); it != server.libraries.end();) {
        auto const& lib = *it->value.lib;
        if (lib.Id() != sample_lib::k_builtin_library_id && GetFileType(lib.path).HasError())
            it = server.libraries.Remove(it);
        else
            ++it;
    }

    // Update libraries_by_id.
    {
        ZoneNamedN(rebuild_htab, "rehash", true);
        server.libraries_by_id_mutex.Lock();
        DEFER { server.libraries_by_id_mutex.Unlock(); };
        server.libraries_by_id.DeleteAll();
        for (auto& n : server.libraries) {
            auto const& lib = *n.value.lib;

            auto found = server.libraries_by_id.FindOrInsert(lib.Id(), &n);
            if (!found.inserted) {
                // If it's already there, we replace it with the one that's more recent
                if (n.value.scan_timepoint > found.element.data->value.scan_timepoint)
                    found.element.data = &n;
            }
        }
    }

    auto const library_work_still_pending =
        pending_library_jobs.num_uncompleted_jobs.Load(LoadMemoryOrder::Acquire) != 0;
    return library_work_still_pending;
}

static Optional<DirectoryWatcher> CreateDirectoryWatcher(ThreadsafeErrorNotifications& error_notifications) {
    Optional<DirectoryWatcher> watcher;
    auto watcher_outcome = CreateDirectoryWatcher(PageAllocator::Instance());
    auto const error_id = SourceLocationHash();
    if (!watcher_outcome.HasError()) {
        error_notifications.RemoveError(error_id);
        watcher.Emplace(watcher_outcome.ReleaseValue());
    } else {
        LogDebug(ModuleName::SampleLibraryServer,
                 "Failed to create directory watcher: {}",
                 watcher_outcome.Error());
        if (auto err = error_notifications.BeginWriteError(error_id)) {
            DEFER { error_notifications.EndWriteError(*err); };
            dyn::AssignFitInCapacity(err->title, "Warning: unable to monitor library folders"_s);
            err->error_code = watcher_outcome.Error();
        }
    }
    return watcher;
}

// ==========================================================================================================
// Library resource loading

using AudioDataAllocator = PageAllocator;

ListedAudioData::~ListedAudioData() {
    ZoneScoped;
    auto const s = state.Load(LoadMemoryOrder::Acquire);
    ASSERT(s == FileLoadingState::CompletedCancelled || s == FileLoadingState::CompletedWithError ||
           s == FileLoadingState::CompletedSucessfully);
    ASSERT(ref_count.Load(LoadMemoryOrder::Relaxed) == 0);
    if (audio_data.interleaved_samples.size)
        AudioDataAllocator::Instance().Free(audio_data.interleaved_samples.ToByteSpan());
    library_ref_count.FetchSub(1, RmwMemoryOrder::Relaxed);
}

ListedInstrument::~ListedInstrument() {
    ASSERT(ref_count.Load(LoadMemoryOrder::Relaxed) == 0);
    ZoneScoped;
    for (auto a : audio_data_set)
        a->ref_count.FetchSub(1, RmwMemoryOrder::Relaxed);
}

ListedImpulseResponse::~ListedImpulseResponse() {
    ASSERT(ref_count.Load(LoadMemoryOrder::Relaxed) == 0);
    audio_data->ref_count.FetchSub(1, RmwMemoryOrder::Relaxed);
}

// Just a little helper that we pass around when working with the thread pool.
struct ThreadPoolArgs {
    ThreadPool& pool;
    AtomicCountdown& num_thread_pool_jobs;
    WorkSignaller& completed_signaller;
};

static void
LoadAudioAsync(ListedAudioData& audio_data, sample_lib::Library const& lib, ThreadPoolArgs thread_pool_args) {
    thread_pool_args.num_thread_pool_jobs.Increase();
    thread_pool_args.pool.AddJob([&, thread_pool_args]() {
        try {
            ZoneScoped;
            DEFER {
                thread_pool_args.completed_signaller.Signal();

                // NOTE: it's important that we do this last, because once the number of thread pool jobs
                // reaches 0, objects in the thread_pool_args could be destroyed.
                thread_pool_args.num_thread_pool_jobs.CountDown();
            };

            {
                auto state = audio_data.state.Load(LoadMemoryOrder::Acquire);
                FileLoadingState new_state;
                do {
                    if (state == FileLoadingState::PendingLoad)
                        new_state = FileLoadingState::Loading;
                    else if (state == FileLoadingState::PendingCancel)
                        new_state = FileLoadingState::CompletedCancelled;
                    else
                        PanicIfReached();
                } while (!audio_data.state.CompareExchangeWeak(state,
                                                               new_state,
                                                               RmwMemoryOrder::AcquireRelease,
                                                               LoadMemoryOrder::Acquire));

                if (new_state == FileLoadingState::CompletedCancelled) return;
            }

            // At this point we must be in the Loading state so other threads know not to interfere. The
            // memory ordering used with the atomic 'state' variable reflects this: the Acquire memory order
            // above, and the Release memory order at the end.
            ASSERT_EQ(audio_data.state.Load(LoadMemoryOrder::Acquire), FileLoadingState::Loading);

            auto const outcome = [&audio_data, &lib]() -> ErrorCodeOr<AudioData> {
                auto reader = TRY(lib.create_file_reader(lib, audio_data.path));
                return DecodeAudioFile(reader, audio_data.path.str, AudioDataAllocator::Instance());
            }();

            FileLoadingState result;
            if (outcome.HasValue()) {
                audio_data.audio_data = outcome.Value();
                result = FileLoadingState::CompletedSucessfully;
            } else {
                audio_data.error = outcome.Error();
                result = FileLoadingState::CompletedWithError;
            }
            audio_data.state.Store(result, StoreMemoryOrder::Release);
        } catch (PanicException) {
            // Pass. We're an audio plugin, we don't want to crash the host.
        }
    });
}

// if the audio load is cancelled, or pending-cancel, then queue up a load again
static void TriggerReloadIfAudioIsCancelled(ListedAudioData& audio_data,
                                            sample_lib::Library const& lib,
                                            ThreadPoolArgs thread_pool_args,
                                            u32 debug_inst_id) {
    auto expected = FileLoadingState::PendingCancel;
    if (!audio_data.state.CompareExchangeStrong(expected,
                                                FileLoadingState::PendingLoad,
                                                RmwMemoryOrder::AcquireRelease,
                                                LoadMemoryOrder::Acquire)) {
        if (expected == FileLoadingState::CompletedCancelled) {
            audio_data.state.Store(FileLoadingState::PendingLoad, StoreMemoryOrder::Release);
            TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                           "instID:{}, reloading CompletedCancelled audio",
                           debug_inst_id);
            LoadAudioAsync(audio_data, lib, thread_pool_args);
        } else {
            TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                           "instID:{}, reusing audio which is in state: {}",
                           debug_inst_id,
                           EnumToString(expected));
        }
    } else {
        TracyMessageEx({k_trace_category, k_trace_colour, -1u},
                       "instID:{}, audio swapped PendingCancel with PendingLoad",
                       debug_inst_id);
    }

    {
        auto const state = audio_data.state.Load(LoadMemoryOrder::Acquire);
        ASSERT(state != FileLoadingState::CompletedCancelled && state != FileLoadingState::PendingCancel);
    }
}

static ListedAudioData* FetchOrCreateAudioData(LibrariesAtomicList::Node& lib_node,
                                               sample_lib::LibraryPath path,
                                               ThreadPoolArgs thread_pool_args,
                                               u32 debug_inst_id) {
    auto const& lib = *lib_node.value.lib;
    for (auto& d : lib_node.value.audio_datas) {
        if (d.path == path && !d.file_modified) {
            TriggerReloadIfAudioIsCancelled(d, lib, thread_pool_args, debug_inst_id);
            return &d;
        }
    }

    auto audio_data = lib_node.value.audio_datas.PrependUninitialised(lib_node.value.arena);
    PLACEMENT_NEW(audio_data)
    ListedAudioData {
        .path = path,
        .file_modified = false,
        .audio_data = {},
        .ref_count = 0u,
        .library_ref_count = lib_node.reader_uses,
        .state = FileLoadingState::PendingLoad,
        .error = {},
    };
    lib_node.reader_uses.FetchAdd(1, RmwMemoryOrder::Relaxed);

    LoadAudioAsync(*audio_data, lib, thread_pool_args);
    return audio_data;
}

static ListedInstrument* FetchOrCreateInstrument(LibrariesAtomicList::Node& lib_node,
                                                 sample_lib::Instrument const& inst,
                                                 ThreadPoolArgs thread_pool_args) {
    auto& lib = lib_node.value;
    ASSERT_EQ(&inst.library, lib.lib);

    for (auto& i : lib.instruments)
        if (i.inst.instrument.name == inst.name) {
            bool any_modified = false;
            for (auto d : i.audio_data_set) {
                if (d->file_modified) {
                    any_modified = true;
                    break;
                }
            }
            if (any_modified) break;

            for (auto d : i.audio_data_set)
                TriggerReloadIfAudioIsCancelled(*d, *lib.lib, thread_pool_args, i.debug_id);
            return &i;
        }

    static u32 g_inst_debug_id {};

    auto new_inst = lib.instruments.PrependUninitialised(lib_node.value.arena);
    PLACEMENT_NEW(new_inst)
    ListedInstrument {
        .debug_id = g_inst_debug_id++,
        .inst = {inst},
        .ref_count = 0u,
    };

    DynamicArray<ListedAudioData*> audio_data_set {new_inst->arena};

    new_inst->inst.audio_datas =
        new_inst->arena.AllocateExactSizeUninitialised<AudioData const*>(inst.regions.size);
    for (auto region_index : Range(inst.regions.size)) {
        auto& region_info = inst.regions[region_index];
        auto& audio_data = new_inst->inst.audio_datas[region_index];

        auto ref_audio_data =
            FetchOrCreateAudioData(lib_node, region_info.path, thread_pool_args, new_inst->debug_id);
        audio_data = &ref_audio_data->audio_data;

        dyn::AppendIfNotAlreadyThere(audio_data_set, ref_audio_data);

        if (inst.audio_file_path_for_waveform == region_info.path)
            new_inst->inst.file_for_gui_waveform = &ref_audio_data->audio_data;
    }

    for (auto d : audio_data_set)
        d->ref_count.FetchAdd(1, RmwMemoryOrder::Relaxed);

    ASSERT(audio_data_set.size);
    new_inst->audio_data_set = audio_data_set.ToOwnedSpan();

    return new_inst;
}

static ListedImpulseResponse* FetchOrCreateImpulseResponse(LibrariesAtomicList::Node& lib_node,
                                                           sample_lib::ImpulseResponse const& ir,
                                                           ThreadPoolArgs thread_pool_args) {
    auto audio_data = FetchOrCreateAudioData(lib_node, ir.path, thread_pool_args, 999999);
    audio_data->ref_count.FetchAdd(1, RmwMemoryOrder::Relaxed);

    auto new_ir = lib_node.value.irs.PrependUninitialised(lib_node.value.arena);
    PLACEMENT_NEW(new_ir)
    ListedImpulseResponse {
        .ir = {ir, &audio_data->audio_data},
        .audio_data = audio_data,
        .ref_count = 0u,
    };
    return new_ir;
}

static void CancelLoadingAudioForInstrumentIfPossible(ListedInstrument const* i, uintptr_t trace_id) {
    ASSERT(i);
    ZoneScoped;
    TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                   "cancel instID:{}, num audio: {}",
                   i->debug_id,
                   i->audio_data_set.size);

    usize num_attempted_cancel = 0;
    for (auto audio_data : i->audio_data_set) {
        auto const audio_refs = audio_data->ref_count.Load(LoadMemoryOrder::Relaxed);
        ASSERT(audio_refs != 0);
        if (audio_refs == 1) {
            auto expected = FileLoadingState::PendingLoad;
            audio_data->state.CompareExchangeStrong(expected,
                                                    FileLoadingState::PendingCancel,
                                                    RmwMemoryOrder::AcquireRelease,
                                                    LoadMemoryOrder::Acquire);

            TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                           "instID:{} cancel attempt audio from state: {}",
                           i->debug_id,
                           EnumToString(expected));

            ++num_attempted_cancel;
        }
    }

    TracyMessageEx({k_trace_category, k_trace_colour, trace_id},
                   "instID:{} num audio attempted cancel: {}",
                   i->debug_id,
                   num_attempted_cancel);
}

struct PendingResource {
    enum class State {
        AwaitingLibrary,
        AwaitingAudio,
        Cancelled,
        Failed,
        CompletedSuccessfully,
    };

    using ListedPointer = TaggedUnion<LoadRequestType,
                                      TypeAndTag<ListedInstrument*, LoadRequestType::Instrument>,
                                      TypeAndTag<ListedImpulseResponse*, LoadRequestType::Ir>>;

    using StateUnion = TaggedUnion<State,
                                   TypeAndTag<ListedPointer, State::AwaitingAudio>,
                                   TypeAndTag<ErrorCode, State::Failed>,
                                   TypeAndTag<Resource, State::CompletedSuccessfully>>;

    u32 LayerIndex() const {
        if (auto i = request.request.TryGet<LoadRequestInstrumentIdWithLayer>()) return i->layer_index;
        PanicIfReached();
        return 0;
    }
    bool IsDesired() const {
        return state.Get<ListedPointer>().Get<ListedInstrument*>() ==
               request.async_comms_channel.desired_inst[LayerIndex()];
    }
    auto& LoadingPercent() { return request.async_comms_channel.instrument_loading_percents[LayerIndex()]; }

    StateUnion state {State::AwaitingLibrary};
    QueuedRequest request;
    uintptr_t debug_id;

    PendingResource* next = nullptr;
};

struct PendingResources {
    u64 server_thread_id;
    IntrusiveSinglyLinkedList<PendingResource> list {};
    AtomicCountdown thread_pool_jobs {0};
};

static void DumpPendingResourcesDebugInfo(PendingResources& pending_resources) {
    ASSERT_EQ(CurrentThreadId(), pending_resources.server_thread_id);
    LogDebug(ModuleName::SampleLibraryServer,
             "Thread pool jobs: {}",
             pending_resources.thread_pool_jobs.counter.Load(LoadMemoryOrder::Relaxed));
    LogDebug(ModuleName::SampleLibraryServer, "\nPending results:");
    for (auto& pending_resource : pending_resources.list) {
        LogDebug(ModuleName::SampleLibraryServer, "  Pending result: {}", pending_resource.debug_id);
        switch (pending_resource.state.tag) {
            case PendingResource::State::AwaitingLibrary:
                LogDebug(ModuleName::SampleLibraryServer, "    Awaiting library");
                break;
            case PendingResource::State::AwaitingAudio: {
                auto& resource = pending_resource.state.Get<PendingResource::ListedPointer>();
                switch (resource.tag) {
                    case LoadRequestType::Instrument: {
                        auto inst = resource.Get<ListedInstrument*>();
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "    Awaiting audio for instrument {}",
                                 inst->inst.instrument.name);
                        for (auto& audio_data : inst->audio_data_set) {
                            LogDebug(ModuleName::SampleLibraryServer,
                                     "      Audio data: {}, {}",
                                     audio_data->audio_data.hash,
                                     EnumToString(audio_data->state.Load(LoadMemoryOrder::Acquire)));
                        }
                        break;
                    }
                    case LoadRequestType::Ir: {
                        auto ir = resource.Get<ListedImpulseResponse*>();
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "    Awaiting audio for IR {}",
                                 ir->ir.ir.path);
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "      Audio data: {}, {}",
                                 ir->audio_data->audio_data.hash,
                                 EnumToString(ir->audio_data->state.Load(LoadMemoryOrder::Acquire)));
                        break;
                    }
                }
                break;
            }
            case PendingResource::State::Cancelled:
                LogDebug(ModuleName::SampleLibraryServer, "    Cancelled");
                break;
            case PendingResource::State::Failed:
                LogDebug(ModuleName::SampleLibraryServer, "    Failed");
                break;
            case PendingResource::State::CompletedSuccessfully:
                LogDebug(ModuleName::SampleLibraryServer, "    Completed successfully");
                break;
        }
    }
}

static bool ConsumeResourceRequests(PendingResources& pending_resources,
                                    ArenaAllocator& arena,
                                    ThreadsafeQueue<QueuedRequest>& request_queue) {
    ASSERT_EQ(CurrentThreadId(), pending_resources.server_thread_id);
    bool any_requests = false;
    while (auto queued_request = request_queue.TryPop()) {
        ZoneNamedN(req, "request", true);

        if (!queued_request->async_comms_channel.used.Load(LoadMemoryOrder::Acquire)) continue;

        static uintptr debug_result_id = 0;
        auto pending_resource = arena.NewUninitialised<PendingResource>();
        PLACEMENT_NEW(pending_resource)
        PendingResource {
            .state = PendingResource::State::AwaitingLibrary,
            .request = *queued_request,
            .debug_id = debug_result_id++,
        };
        SinglyLinkedListPrepend(pending_resources.list.first, pending_resource);
        any_requests = true;

        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource->debug_id},
                       "pending result added");
    }
    return any_requests;
}

static bool UpdatePendingResources(PendingResources& pending_resources,
                                   Server& server,
                                   bool libraries_are_still_loading) {
    ASSERT_EQ(CurrentThreadId(), server.server_thread_id);

    if (pending_resources.list.Empty()) return false;

    ThreadPoolArgs thread_pool_args {
        .pool = server.thread_pool,
        .num_thread_pool_jobs = pending_resources.thread_pool_jobs,
        .completed_signaller = server.work_signaller,
    };

    // Fill in library
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state != PendingResource::State::AwaitingLibrary) continue;

        auto const library_id = ({
            sample_lib::LibraryId n {};
            switch (pending_resource.request.request.tag) {
                case LoadRequestType::Instrument:
                    n = pending_resource.request.request.Get<LoadRequestInstrumentIdWithLayer>().id.library;
                    break;
                case LoadRequestType::Ir:
                    n = pending_resource.request.request.Get<sample_lib::IrId>().library;
                    break;
            }
            n;
        });
        ASSERT(library_id.name.size != 0);
        ASSERT(library_id.author.size != 0);

        LibrariesAtomicList::Node* lib {};
        if (auto l_ptr = server.libraries_by_id.Find(library_id)) lib = *l_ptr;

        auto const find_lib_error_id =
            HashMultiple(Array {"sls-find-lib"_s, library_id.name, library_id.author});
        auto& error_notifications = pending_resource.request.async_comms_channel.error_notifications;

        if (!lib) {
            // If libraries are still loading, then we just wait to see if the library we're missing is
            // about to be loaded. If not, then it's an error.
            if (!libraries_are_still_loading) {
                if (auto err = error_notifications.BeginWriteError(find_lib_error_id)) {
                    DEFER { error_notifications.EndWriteError(*err); };
                    err->error_code = CommonError::NotFound;
                    fmt::Assign(err->title, "{} library not found"_s, library_id);
                    fmt::Assign(
                        err->message,
                        "\"{}\" is not installed or is otherwise unavailable. Check your preferences or consult the library installation instructions.",
                        library_id);
                    if (library_id == sample_lib::k_mirage_compat_library_id) {
                        fmt::Append(
                            err->message,
                            " For compatibility with Mirage please install the Mirage Compatibility library (freely available from FrozenPlain).");
                    }
                }

                pending_resource.state = ErrorCode {CommonError::NotFound};
            }
        } else {
            error_notifications.RemoveError(find_lib_error_id);

            switch (pending_resource.request.request.tag) {
                case LoadRequestType::Instrument: {
                    auto const& load_inst =
                        pending_resource.request.request.Get<LoadRequestInstrumentIdWithLayer>();
                    auto const inst_name = load_inst.id.inst_name;

                    ASSERT(inst_name.size != 0);

                    auto const find_inst_error_id = HashMultiple(
                        Array {"sls-find-inst"_s, library_id.name, library_id.author, inst_name});

                    if (auto const i = lib->value.lib->insts_by_name.Find(inst_name)) {
                        error_notifications.RemoveError(find_inst_error_id);

                        pending_resource.request.async_comms_channel
                            .instrument_loading_percents[load_inst.layer_index]
                            .Store(0, StoreMemoryOrder::Relaxed);

                        auto inst = FetchOrCreateInstrument(*lib, **i, thread_pool_args);
                        ASSERT(inst);

                        pending_resource.request.async_comms_channel.desired_inst[load_inst.layer_index] =
                            inst;
                        pending_resource.state = PendingResource::ListedPointer {inst};

                        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource.debug_id},
                                       "option: instID:{} load Sampler inst[{}], {}, {}, {}",
                                       inst->debug_id,
                                       load_inst.layer_index,
                                       (void const*)inst,
                                       lib->value.lib->name,
                                       inst_name);
                    } else {
                        if (auto err = error_notifications.BeginWriteError(find_inst_error_id)) {
                            DEFER { error_notifications.EndWriteError(*err); };
                            fmt::Assign(err->title, "Cannot find instrument \"{}\""_s, inst_name);
                            err->error_code = CommonError::NotFound;
                        }

                        pending_resource.state = ErrorCode {CommonError::NotFound};
                    }
                    break;
                }
                case LoadRequestType::Ir: {
                    auto const ir_id = pending_resource.request.request.Get<sample_lib::IrId>();
                    auto const ir = lib->value.lib->irs_by_name.Find(ir_id.ir_name);

                    auto const find_ir_error_id = HashMultiple(
                        Array {"sls-find-ir"_s, library_id.name, library_id.author, ir_id.ir_name});

                    if (ir) {
                        error_notifications.RemoveError(find_ir_error_id);

                        auto listed_ir = FetchOrCreateImpulseResponse(*lib, **ir, thread_pool_args);

                        pending_resource.state = PendingResource::ListedPointer {listed_ir};

                        TracyMessageEx({k_trace_category, k_trace_colour, pending_resource.debug_id},
                                       "option: load IR, {}, {}",
                                       ir_id.library,
                                       ir_id.ir_name);
                    } else {
                        if (auto err = error_notifications.BeginWriteError(find_ir_error_id)) {
                            DEFER { error_notifications.EndWriteError(*err); };
                            fmt::Assign(err->title, "Cannot find IR \"{}\""_s, ir_id.ir_name);
                            fmt::Assign(err->message,
                                        "Could not find reverb impulse response: {}, in library: {}",
                                        ir_id.ir_name,
                                        library_id);
                            err->error_code = CommonError::NotFound;
                        }

                        pending_resource.state = ErrorCode {CommonError::NotFound};
                    }
                    break;
                }
            }
        }
    }

    // For each inst, check for errors
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto const& listed_inst = *({
            auto i = pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedInstrument*>();
            if (!i) continue;
            *i;
        });

        ASSERT(listed_inst.audio_data_set.size);

        Optional<ErrorCode> error {};
        Optional<String> audio_path {};
        for (auto a : listed_inst.audio_data_set) {
            if (a->state.Load(LoadMemoryOrder::Acquire) == FileLoadingState::CompletedWithError) {
                error = a->error;
                audio_path = a->path;
                break;
            }
        }

        auto const audio_load_error_id = HashMultiple(Array {
            "sls-audio-load"_s,
            audio_path.ValueOr(""),
            listed_inst.inst.instrument.library.name,
            listed_inst.inst.instrument.library.author,
        });

        auto& error_notifications = pending_resource.request.async_comms_channel.error_notifications;
        if (!error) {
            error_notifications.RemoveError(audio_load_error_id);
        } else {
            if (auto err = error_notifications.BeginWriteError(audio_load_error_id)) {
                DEFER { error_notifications.EndWriteError(*err); };
                dyn::AssignFitInCapacity(err->title, "Failed to load audio");
                err->error_code = *error;
                fmt::Assign(err->message,
                            "Failed to load audio file '{}', part of instrument '{}', in library '{}'",
                            *audio_path,
                            listed_inst.inst.instrument.name,
                            listed_inst.inst.instrument.library.Id());
            }

            CancelLoadingAudioForInstrumentIfPossible(&listed_inst, pending_resource.debug_id);
            if (pending_resource.IsDesired())
                pending_resource.LoadingPercent().Store(-1, StoreMemoryOrder::Relaxed);
            pending_resource.state = *error;
        }
    }

    // For each inst, check if it's still needed, and cancel if not. And update percent markers
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto i_ptr = pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedInstrument*>();
        if (!i_ptr) continue;
        auto i = *i_ptr;

        if (pending_resource.IsDesired()) {
            auto const num_completed = ({
                u32 n = 0;
                for (auto& a : i->audio_data_set)
                    if (a->state.Load(LoadMemoryOrder::Acquire) == FileLoadingState::CompletedSucessfully)
                        ++n;
                n;
            });
            if (num_completed == i->audio_data_set.size) {
                pending_resource.LoadingPercent().Store(-1, StoreMemoryOrder::Relaxed);
                pending_resource.state =
                    Resource {ResourcePointer<sample_lib::LoadedInstrument> {i->inst, i->ref_count}};
            } else {
                f32 const percent = 100.0f * ((f32)num_completed / (f32)i->audio_data_set.size);
                pending_resource.LoadingPercent().Store(RoundPositiveFloat(percent),
                                                        StoreMemoryOrder::Relaxed);
            }
        } else {
            // If it's not desired by any others it can be cancelled
            bool const is_desired_by_another = ({
                bool desired = false;
                for (auto& other_pending_resource : pending_resources.list) {
                    for (auto other_desired :
                         other_pending_resource.request.async_comms_channel.desired_inst) {
                        if (other_desired == i) {
                            desired = true;
                            break;
                        }
                    }
                    if (desired) break;
                }
                desired;
            });
            if (!is_desired_by_another)
                CancelLoadingAudioForInstrumentIfPossible(i, pending_resource.debug_id);

            pending_resource.state = PendingResource::State::Cancelled;
        }
    }

    // Store the result of the IR load in the result, if needed
    for (auto& pending_resource : pending_resources.list) {
        if (pending_resource.state.tag != PendingResource::State::AwaitingAudio) continue;

        auto ir_ptr_ptr =
            pending_resource.state.Get<PendingResource::ListedPointer>().TryGet<ListedImpulseResponse*>();
        if (!ir_ptr_ptr) continue;
        auto ir_ptr = *ir_ptr_ptr;
        auto const& ir = *ir_ptr;

        auto const audio_load_error_id = HashMultiple(Array {
            "sls-audio-load"_s,
            ir.audio_data->path.str,
            ir.ir.ir.library.name,
            ir.ir.ir.library.author,
        });

        auto& error_notifications = pending_resource.request.async_comms_channel.error_notifications;

        switch (ir.audio_data->state.Load(LoadMemoryOrder::Acquire)) {
            case FileLoadingState::CompletedSucessfully: {
                error_notifications.RemoveError(audio_load_error_id);
                pending_resource.state = Resource {
                    ResourcePointer<sample_lib::LoadedIr> {
                        ir_ptr->ir,
                        ir_ptr->ref_count,
                    },
                };
                break;
            }
            case FileLoadingState::CompletedWithError: {
                auto const ir_id = pending_resource.request.request.Get<sample_lib::IrId>();
                if (auto err = error_notifications.BeginWriteError(audio_load_error_id)) {
                    DEFER { error_notifications.EndWriteError(*err); };
                    dyn::AssignFitInCapacity(err->title, "Failed to load IR");
                    err->error_code = *ir.audio_data->error;
                    fmt::Assign(err->message,
                                "File '{}', in library {} failed to load. Check your Lua file: {}",
                                ir.audio_data->path.str,
                                ir_id.library,
                                ir.ir.ir.library.path);
                }

                pending_resource.state = *ir.audio_data->error;
                break;
            }
            case FileLoadingState::PendingLoad:
            case FileLoadingState::Loading: break;
            case FileLoadingState::PendingCancel:
            case FileLoadingState::CompletedCancelled: PanicIfReached(); break;
            case FileLoadingState::Count: PanicIfReached(); break;
        }
    }

    // For each result, check if all loading has completed and if so, dispatch the result
    // and remove it from the pending list
    SinglyLinkedListRemoveIf(
        pending_resources.list.first,
        [&](PendingResource const& pending_resource) {
            switch (pending_resource.state.tag) {
                case PendingResource::State::AwaitingLibrary:
                case PendingResource::State::AwaitingAudio: return false;
                case PendingResource::State::Cancelled:
                case PendingResource::State::Failed:
                case PendingResource::State::CompletedSuccessfully: break;
            }

            LoadResult result {
                .id = pending_resource.request.id,
                .result = ({
                    LoadResult::Result r {LoadResult::ResultType::Cancelled};
                    switch (pending_resource.state.tag) {
                        case PendingResource::State::AwaitingLibrary:
                        case PendingResource::State::AwaitingAudio: PanicIfReached(); break;
                        case PendingResource::State::Cancelled: break;
                        case PendingResource::State::Failed:
                            r = pending_resource.state.Get<ErrorCode>();
                            break;
                        case PendingResource::State::CompletedSuccessfully:
                            r = pending_resource.state.Get<Resource>();
                            break;
                    }
                    r;
                }),
            };

            server.channels.Use([&](auto&) {
                if (pending_resource.request.async_comms_channel.used.Load(LoadMemoryOrder::Acquire)) {
                    // We always add results with a ref count of 1, we do that manually rather than call
                    // Retain() because here we accept that ref count might be 0 here: something that is
                    // disallowed in Retain().
                    if (auto ref_count = ({
                            Atomic<u32>* rc = nullptr;
                            if (auto resource = pending_resource.state.TryGet<Resource>()) {
                                switch (resource->tag) {
                                    case LoadRequestType::Instrument:
                                        rc = resource->Get<ResourcePointer<sample_lib::LoadedInstrument>>()
                                                 .ref_count;
                                        break;
                                    case LoadRequestType::Ir:
                                        rc = resource->Get<ResourcePointer<sample_lib::LoadedIr>>().ref_count;
                                        break;
                                }
                            }
                            rc;
                        })) {
                        ref_count->FetchAdd(1, RmwMemoryOrder::Relaxed);
                    }
                    pending_resource.request.async_comms_channel.results.Push(result);
                    pending_resource.request.async_comms_channel.result_added_callback();
                }
            });
            return true;
        },
        [](PendingResource*) {
            // delete function
        });

    return !pending_resources.list.Empty();
}

// ==========================================================================================================
// Server thread

static void ServerThreadUpdateMetrics(Server& server) {
    ASSERT_EQ(CurrentThreadId(), server.server_thread_id);
    u32 num_insts_loaded = 0;
    u32 num_samples_loaded = 0;
    u64 total_bytes_used = 0;
    for (auto& i : server.libraries) {
        for (auto& _ : i.value.instruments)
            ++num_insts_loaded;
        for (auto const& audio : i.value.audio_datas) {
            ++num_samples_loaded;
            if (audio.state.Load(LoadMemoryOrder::Acquire) == FileLoadingState::CompletedSucessfully)
                total_bytes_used += audio.audio_data.RamUsageBytes();
        }
    }

    server.num_insts_loaded.Store(num_insts_loaded, StoreMemoryOrder::Relaxed);
    server.num_samples_loaded.Store(num_samples_loaded, StoreMemoryOrder::Relaxed);
    server.total_bytes_used_by_samples.Store(total_bytes_used, StoreMemoryOrder::Relaxed);
}

static void RemoveUnreferencedObjects(Server& server) {
    ZoneScoped;
    ASSERT_EQ(CurrentThreadId(), server.server_thread_id);

    server.channels.Use([](auto& channels) {
        channels.RemoveIf([](AsyncCommsChannel const& h) { return !h.used.Load(LoadMemoryOrder::Acquire); });
    });

    auto remove_unreferenced_in_lib = [](auto& lib) {
        auto remove_unreferenced = [](auto& list) {
            list.RemoveIf([](auto const& n) { return n.ref_count.Load(LoadMemoryOrder::Acquire) == 0; });
        };
        remove_unreferenced(lib.instruments);
        remove_unreferenced(lib.irs);
        remove_unreferenced(lib.audio_datas);
    };

    for (auto& l : server.libraries)
        remove_unreferenced_in_lib(l.value);
    for (auto n = server.libraries.dead_list; n != nullptr; n = n->writer_next)
        remove_unreferenced_in_lib(n->value);

    server.libraries.DeleteRemovedAndUnreferenced();
}

static void ServerThreadProc(Server& server) {
    ZoneScoped;

    server.server_thread_id = CurrentThreadId();

    ArenaAllocator scratch_arena {PageAllocator::Instance(), Kb(128)};
    auto watcher = CreateDirectoryWatcher(server.error_notifications);
    DEFER {
        if (PanicOccurred()) return;
        if (watcher) DestoryDirectoryWatcher(*watcher);
    };

    while (!server.end_thread.Load(LoadMemoryOrder::Relaxed)) {
        PendingResources pending_resources {
            .server_thread_id = server.server_thread_id,
        };
        auto scan_folders = CopyActiveFolders(server.scan_folders);
        PendingLibraryJobs pending_library_jobs {
            .server_thread_id = server.server_thread_id,
            .thread_pool = server.thread_pool,
            .work_signaller = server.work_signaller,
            .num_uncompleted_jobs = server.num_uncompleted_library_jobs,
            .folders = scan_folders,
        };

        while (true) {
            // We have a timeout because we want to check for directory watching events.
            server.work_signaller.WaitUntilSignalledOrSpurious(250u);

            if (!PRODUCTION_BUILD &&
                server.request_debug_dump_current_state.Exchange(false, RmwMemoryOrder::Relaxed)) {
                ZoneNamedN(dump, "dump", true);
                LogDebug(ModuleName::SampleLibraryServer, "Dumping current state of loading thread");
                LogDebug(ModuleName::SampleLibraryServer,
                         "Libraries currently loading: {}",
                         pending_library_jobs.num_uncompleted_jobs.Load(LoadMemoryOrder::Acquire));
                DumpPendingResourcesDebugInfo(pending_resources);
                LogDebug(ModuleName::SampleLibraryServer, "\nAvailable Libraries:");
                for (auto& lib : server.libraries) {
                    LogDebug(ModuleName::SampleLibraryServer, "  Library: {}", lib.value.lib->name);
                    for (auto& inst : lib.value.instruments)
                        LogDebug(ModuleName::SampleLibraryServer,
                                 "    Instrument: {}",
                                 inst.inst.instrument.name);
                }
            }

            ZoneNamedN(working, "working", true);

            TracyMessageEx({k_trace_category, k_trace_colour, {}},
                           "poll, thread_pool_jobs: {}",
                           pending_resources.thread_pool_jobs.counter.Load(LoadMemoryOrder::Relaxed));

            if (ConsumeResourceRequests(pending_resources, scratch_arena, server.request_queue)) {
                // For quick initialisation, we load libraries only when there's been a request.
                MarkNotScannedFoldersRescanRequested(scan_folders);
            }

            // There's 2 separate systems here. The library loading, and then the audio loading (which
            // includes Instruments and IRs). Before we can fulfill a request for an instrument or IR, we need
            // to have a loaded library. The library contains the information needed to locate the audio.

            auto const libraries_are_still_loading =
                UpdateLibraryJobs(server, pending_library_jobs, scratch_arena, watcher);
            if (!libraries_are_still_loading) {
                server.is_scanning_libraries.Store(false, StoreMemoryOrder::SequentiallyConsistent);
                WakeWaitingThreads(server.is_scanning_libraries, NumWaitingThreads::All);
            }

            auto const resources_are_still_loading =
                UpdatePendingResources(pending_resources, server, libraries_are_still_loading);

            ServerThreadUpdateMetrics(server);

            if (!resources_are_still_loading && !libraries_are_still_loading) break;
        }

        ZoneNamedN(post_inner, "post inner", true);

        TracyMessageEx({k_trace_category, k_trace_colour, -1u}, "poll completed");

        DeleteUnusedScanFolders(server.scan_folders);

        // We have completed all the loading requests, but there might still be audio data that is in the
        // thread pool. We need for them to finish before we potentially delete the memory that they rely on.
        pending_resources.thread_pool_jobs.WaitUntilZero();

        RemoveUnreferencedObjects(server);
        scratch_arena.ResetCursorAndConsolidateRegions();
    }

    // It's necessary to do this at the end of this function because it is not guaranteed to be called in the
    // loop; the 'end' boolean can be changed at a point where the loop ends before calling this.
    RemoveUnreferencedObjects(server);

    server.libraries.RemoveAll();
    server.libraries.DeleteRemovedAndUnreferenced();
    server.libraries_by_id.DeleteAll();
}

inline String ToString(EmbeddedString s) { return {s.data, s.size}; }

// not threadsafe
static sample_lib::Library* BuiltinLibrary() {
    static constexpr String k_icon_path = "builtin-library-icon";
    static constexpr String k_background_path = "builtin-library-background";
    static sample_lib::Library builtin_library {
        .name = sample_lib::k_builtin_library_id.name,
        .tagline = "Built-in IRs",
        .library_url = FLOE_HOMEPAGE_URL,
        .author = sample_lib::k_builtin_library_id.author,
        .minor_version = 1,
        .background_image_path = k_background_path,
        .icon_image_path = k_icon_path,
        .insts_by_name = {},
        .irs_by_name = {},
        .path = ":memory:",
        .file_hash = 100,
        .create_file_reader = [](sample_lib::Library const&,
                                 sample_lib::LibraryPath path) -> ErrorCodeOr<Reader> {
            if (path == k_icon_path) {
                auto data = EmbeddedIconImage();
                return Reader::FromMemory({data.data, data.size});
            } else if (path == k_background_path) {
                auto data = EmbeddedDefaultBackground();
                return Reader::FromMemory({data.data, data.size});
            }

            auto const embedded_irs = GetEmbeddedIrs();
            for (auto& ir : Span<EmbeddedIr const> {embedded_irs.irs, embedded_irs.count})
                if (ToString(ir.data.filename) == path.str)
                    return Reader::FromMemory({ir.data.data, ir.data.size});

            return ErrorCode(FilesystemError::PathDoesNotExist);
        },
        .file_format_specifics = sample_lib::LuaSpecifics {}, // unused
    };

    static bool init = false;
    if (!Exchange(init, true)) {
        static FixedSizeAllocator<Kb(15)> alloc {nullptr};

        auto const embedded_irs = GetEmbeddedIrs();

        builtin_library.irs_by_name =
            decltype(builtin_library.irs_by_name)::Create(alloc, embedded_irs.count);

        PathPool folders_path_pool;
        sample_lib::detail::InitialiseRootFolders(builtin_library, alloc);

        for (auto const& embedded_ir : Span<EmbeddedIr const> {embedded_irs.irs, embedded_irs.count}) {
            usize num_tags = 0;
            if (embedded_ir.tag1.size) ++num_tags;
            if (embedded_ir.tag2.size) ++num_tags;

            auto tags = Set<String>::Create(alloc, num_tags);
            if (embedded_ir.tag1.size) tags.InsertWithoutGrowing(ToString(embedded_ir.tag1));
            if (embedded_ir.tag2.size) tags.InsertWithoutGrowing(ToString(embedded_ir.tag2));

            auto const name = ToString(embedded_ir.name);

            ArenaAllocatorWithInlineStorage<200> scratch_arena {PageAllocator::Instance()};

            auto ir = alloc.NewUninitialised<sample_lib::ImpulseResponse>();
            PLACEMENT_NEW(ir)
            sample_lib::ImpulseResponse {
                .library = builtin_library,
                .name = name,
                .path = {ToString(embedded_ir.data.filename)},
                .folder =
                    FindOrInsertFolderNode(&builtin_library.root_folders[ToInt(sample_lib::ResourceType::Ir)],
                                           ToString(embedded_ir.folder),
                                           sample_lib::k_max_folders,
                                           {
                                               .node_allocator = alloc,
                                               .name_allocator =
                                                   FolderNodeAllocators::NameAllocator {
                                                       .path_pool = folders_path_pool,
                                                       .path_pool_arena = alloc,
                                                   },
                                           }),
                .tags = tags,
                .description = ToString(embedded_ir.description),
            };
            builtin_library.irs_by_name.InsertWithoutGrowing(name, ir);
        }

        ArenaAllocatorWithInlineStorage<100> scratch_arena {PageAllocator::Instance()};
        if (sample_lib::detail::PostReadBookkeeping(builtin_library, alloc, scratch_arena).HasError())
            Panic("Failed to load builtin library");

        LogDebug(ModuleName::SampleLibraryServer,
                 "Built-in library loaded, used {} bytes",
                 alloc.UsedStackData().size);
    }

    return &builtin_library;
}

Server::Server(ThreadPool& pool,
               String always_scanned_folder,
               ThreadsafeErrorNotifications& error_notifications)
    : error_notifications(error_notifications)
    , thread_pool(pool) {
    if (always_scanned_folder.size) {
        auto folder = scan_folders.folder_allocator.PrependUninitialised(scan_folders.folder_arena);
        PLACEMENT_NEW(folder) ScanFolder();
        dyn::Assign(folder->path, always_scanned_folder);
        folder->source = ScanFolder::Source::AlwaysScannedFolder;
        folder->state.raw = ScanFolder::State::NotScanned;
        dyn::Append(scan_folders.folders, folder);
    }

    {
        auto node = libraries.AllocateUninitialised();
        PLACEMENT_NEW(&node->value)
        ListedLibrary {.arena = PageAllocator::Instance(), .lib = BuiltinLibrary()};
        libraries.Insert(node);

        libraries_by_id.Insert(BuiltinLibrary()->Id(), node);
    }

    thread.Start([this]() { ServerThreadProc(*this); }, "samp-lib-server");
}

Server::~Server() {
    end_thread.Store(true, StoreMemoryOrder::Release);
    work_signaller.Signal();
    thread.Join();
    ASSERT(channels.Use([](auto& c) { return c.Empty(); }), "missing channel close");

    scan_folders.folder_allocator.Clear();
}

AsyncCommsChannel& OpenAsyncCommsChannel(Server& server, OpenAsyncCommsChannelArgs const& args) {
    return server.channels.Use([&](auto& channels) -> AsyncCommsChannel& {
        auto channel = channels.PrependUninitialised(server.channels_arena);
        PLACEMENT_NEW(channel)
        AsyncCommsChannel {
            .error_notifications = args.error_notifications,
            .result_added_callback = Move(args.result_added_callback),
            .library_changed_callback = Move(args.library_changed_callback),
            .used = true,
        };
        for (auto& p : channel->instrument_loading_percents)
            p.raw = -1;
        return *channel;
    });
}

void CloseAsyncCommsChannel(Server& server, AsyncCommsChannel& channel) {
    server.channels.Use([&channel](auto& channels) {
        (void)channels;
        channel.used.Store(false, StoreMemoryOrder::Release);
        while (auto r = channel.results.TryPop())
            r->Release();
    });
}

RequestId SendAsyncLoadRequest(Server& server, AsyncCommsChannel& channel, LoadRequest const& request) {
    QueuedRequest const queued_request {
        .id = server.request_id_counter.FetchAdd(1, RmwMemoryOrder::Relaxed),
        .request = request,
        .async_comms_channel = channel,
    };
    server.request_queue.Push(queued_request);
    server.work_signaller.Signal();
    return queued_request.id;
}

void RequestScanningOfUnscannedFolders(Server& server) {
    server.scan_folders.mutex.Lock();
    auto const rescan_requested = MarkNotScannedFoldersRescanRequested(server.scan_folders.folders);
    server.scan_folders.mutex.Unlock();
    if (rescan_requested) {
        server.is_scanning_libraries.Store(true, StoreMemoryOrder::SequentiallyConsistent);
        server.work_signaller.Signal();
    }
}

void RescanFolder(Server& server, String path) {
    bool found = false;
    {
        server.scan_folders.mutex.Lock();
        DEFER { server.scan_folders.mutex.Unlock(); };

        for (auto folder : server.scan_folders.folders) {
            ASSERT(folder);
            if (path::Equal(folder->path, path) || path::IsWithinDirectory(path, folder->path)) {
                folder->state.Store(ScanFolder::State::RescanRequested, StoreMemoryOrder::Release);
                found = true;
            }
        }
    }
    if (found) {
        server.is_scanning_libraries.Store(true, StoreMemoryOrder::SequentiallyConsistent);
        server.work_signaller.Signal();
    }
}

void SetExtraScanFolders(Server& server, Span<String const> extra_folders) {
    ASSERT(extra_folders.size <= k_max_extra_scan_folders);
    bool edited = false;
    {
        server.scan_folders.mutex.Lock();
        DEFER { server.scan_folders.mutex.Unlock(); };

        // Remove folders that are not in the new extra_folders.
        edited = dyn::RemoveValueIf(server.scan_folders.folders, [&](ScanFolder* folder) {
                     return folder->source == ScanFolder::Source::ExtraFolder &&
                            !Find(extra_folders, folder->path);
                 }) != 0;

        // Add any new extra_folders that are not already present.
        for (auto const path : extra_folders) {
            ASSERT(IsValidUtf8(path));

            // We don't need to add folders that are already in the array.
            if (FindIf(server.scan_folders.folders, [path](ScanFolder* f) {
                    return f->source == ScanFolder::Source::ExtraFolder && f->path == path;
                })) {
                continue;
            }

            ASSERT(server.scan_folders.folders.size != ScanFolders::Folders::Capacity());

            auto folder =
                server.scan_folders.folder_allocator.PrependUninitialised(server.scan_folders.folder_arena);
            PLACEMENT_NEW(folder) ScanFolder();
            dyn::Assign(folder->path, path);
            folder->source = ScanFolder::Source::ExtraFolder;
            folder->state.Store(ScanFolder::State::NotScanned, StoreMemoryOrder::Release);
            dyn::Append(server.scan_folders.folders, folder);
            edited = true;
        }
    }

    if (edited) {
        server.is_scanning_libraries.Store(true, StoreMemoryOrder::SequentiallyConsistent);
        server.work_signaller.Signal();
    }
}

detail::LibrariesAtomicList& LibrariesList(Server& server) { return server.libraries; }

Span<ResourcePointer<sample_lib::Library>> AllLibrariesRetained(Server& server, ArenaAllocator& arena) {
    // IMPROVE: is this slow to do at every request for a library?
    RequestScanningOfUnscannedFolders(server);

    DynamicArray<ResourcePointer<sample_lib::Library>> result(arena);
    for (auto& i : server.libraries) {
        if (i.TryRetain()) {
            auto ref = ResourcePointer<sample_lib::Library>(*i.value.lib, i.reader_uses);
            dyn::Append(result, ref);
        }
    }
    return result.ToOwnedSpan();
}

ResourcePointer<sample_lib::Library> FindLibraryRetained(Server& server, sample_lib::LibraryIdRef id) {
    // IMPROVE: is this slow to do at every request for a library?
    RequestScanningOfUnscannedFolders(server);

    server.libraries_by_id_mutex.Lock();
    DEFER { server.libraries_by_id_mutex.Unlock(); };
    auto l = server.libraries_by_id.Find(id);
    if (!l) return {};
    auto& node = **l;
    if (!node.TryRetain()) return {};
    return ResourcePointer<sample_lib::Library>(*node.value.lib, node.reader_uses);
}

void LoadResult::ChangeRefCount(RefCountChange t) {
    if (auto resource_union = result.TryGet<Resource>()) {
        switch (resource_union->tag) {
            case LoadRequestType::Instrument:
                resource_union->Get<ResourcePointer<sample_lib::LoadedInstrument>>().ChangeRefCount(t);
                break;
            case LoadRequestType::Ir:
                resource_union->Get<ResourcePointer<sample_lib::LoadedIr>>().ChangeRefCount(t);
                break;
        }
    }
}

void LoadResult::ChangeRefCount(RefCountChange t) const { const_cast<LoadResult*>(this)->ChangeRefCount(t); }

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

template <typename Type>
static Type& ExtractSuccess(tests::Tester& tester, LoadResult const& result, LoadRequest const& request) {
    switch (request.tag) {
        case LoadRequestType::Instrument: {
            auto inst = request.Get<LoadRequestInstrumentIdWithLayer>();
            tester.log.Debug("Instrument: {} - {}", inst.id.library, inst.id.inst_name);
            break;
        }
        case LoadRequestType::Ir: {
            auto ir = request.Get<sample_lib::IrId>();
            tester.log.Debug("Ir: {} - {}", ir.library, ir.ir_name);
            break;
        }
    }

    if (auto err = result.result.TryGet<ErrorCode>())
        LogDebug(ModuleName::SampleLibraryServer, "Error: {}", *err);
    REQUIRE_EQ(result.result.tag, LoadResult::ResultType::Success);
    auto opt_r = result.result.Get<Resource>().TryGetMut<Type>();
    REQUIRE(opt_r);
    return *opt_r;
}

TEST_CASE(TestSampleLibraryServer) {
    struct Fixture {
        [[maybe_unused]] Fixture(tests::Tester&) { thread_pool.Init("pool", 8u); }
        bool initialised = false;
        ArenaAllocatorWithInlineStorage<2000> arena {Malloc::Instance()};
        String test_lib_path;
        ThreadPool thread_pool;
        ThreadsafeErrorNotifications error_notif {};
        Array<String, 1> scan_folders;
    };

    auto& fixture = CreateOrFetchFixtureObject<Fixture>(tester);
    if (!fixture.initialised) {
        fixture.initialised = true;

        auto const lib_dir = (String)path::Join(tester.scratch_arena,
                                                Array {
                                                    tests::TempFolder(tester),
                                                    "floe libraries",
                                                });
        // We copy the test library files to a temp directory so that we can modify them without messing up
        // our test data. And also on Windows WSL, we can watch for directory changes - which doesn't work on
        // the WSL filesystem.
        auto _ =
            Delete(lib_dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
        {
            auto const source =
                (String)path::Join(tester.scratch_arena,
                                   Array {TestFilesFolder(tester), tests::k_libraries_test_files_subdir});

            auto it = TRY(dir_iterator::RecursiveCreate(tester.scratch_arena, source, {}));
            DEFER { dir_iterator::Destroy(it); };
            while (auto entry = TRY(dir_iterator::Next(it, tester.scratch_arena))) {
                auto const relative_path = entry->subpath;
                auto const dest_file = path::Join(tester.scratch_arena, Array {lib_dir, relative_path});
                switch (entry->type) {
                    case FileType::File: {
                        if (auto const dir = path::Directory(dest_file)) {
                            TRY(CreateDirectory(
                                *dir,
                                {.create_intermediate_directories = true, .fail_if_exists = false}));
                        }
                        TRY(CopyFile(dir_iterator::FullPath(it, *entry, tester.scratch_arena),
                                     dest_file,
                                     ExistingDestinationHandling::Overwrite));
                        break;
                    }
                    case FileType::Directory: {
                        TRY(CreateDirectory(
                            dest_file,
                            {.create_intermediate_directories = true, .fail_if_exists = false}));
                        break;
                    }
                }
            }
        }

        fixture.test_lib_path = path::Join(fixture.arena, Array {lib_dir, "shared_files_test_lib.mdata"_s});

        fixture.scan_folders[0] = fixture.arena.Clone(lib_dir);
    }

    auto& scratch_arena = tester.scratch_arena;
    Server server {fixture.thread_pool, {}, fixture.error_notif};
    SetExtraScanFolders(server, fixture.scan_folders);

    auto const open_args = OpenAsyncCommsChannelArgs {
        .error_notifications = fixture.error_notif,
        .result_added_callback = []() {},
        .library_changed_callback = [](sample_lib::LibraryIdRef) {},
    };

    SUBCASE("single channel") {
        auto& channel = OpenAsyncCommsChannel(server, open_args);
        CloseAsyncCommsChannel(server, channel);
    }

    SUBCASE("multiple channels") {
        auto& channel1 = OpenAsyncCommsChannel(server, open_args);
        auto& channel2 = OpenAsyncCommsChannel(server, open_args);
        CloseAsyncCommsChannel(server, channel1);
        CloseAsyncCommsChannel(server, channel2);
    }

    SUBCASE("registering again after unregistering all") {
        auto& channel1 = OpenAsyncCommsChannel(server, open_args);
        auto& channel2 = OpenAsyncCommsChannel(server, open_args);
        CloseAsyncCommsChannel(server, channel1);
        CloseAsyncCommsChannel(server, channel2);
        auto& channel3 = OpenAsyncCommsChannel(server, open_args);
        CloseAsyncCommsChannel(server, channel3);
    }

    SUBCASE("unregister a channel directly after sending a request") {
        auto& channel = OpenAsyncCommsChannel(server, open_args);

        SendAsyncLoadRequest(server,
                             channel,
                             LoadRequestInstrumentIdWithLayer {
                                 .id =
                                     {
                                         .library = {{.author = "Tester"_s, .name = "Test Lua"_s}},
                                         .inst_name = "Auto Mapped Samples"_s,
                                     },
                                 .layer_index = 0,
                             });
        CloseAsyncCommsChannel(server, channel);
    }

    SUBCASE("loading works") {
        struct Request {
            LoadRequest request;
            TrivialFixedSizeFunction<24, void(LoadResult const&, LoadRequest const& request)> check_result;
            RequestId request_id; // filled in later
        };
        DynamicArray<Request> requests {scratch_arena};

        SUBCASE("ir") {
            auto const builtin_ir = GetEmbeddedIrs().irs[0];
            dyn::Append(
                requests,
                {
                    .request =
                        sample_lib::IrId {.library = sample_lib::k_builtin_library_id,
                                          .ir_name = String {builtin_ir.name.data, builtin_ir.name.size}},
                    .check_result =
                        [&](LoadResult const& r, LoadRequest const& request) {
                            auto ir =
                                ExtractSuccess<ResourcePointer<sample_lib::LoadedIr>>(tester, r, request);
                            REQUIRE(ir->audio_data);
                            CHECK(ir->audio_data->interleaved_samples.size);
                        },
                });
        }

        SUBCASE("library and instrument") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{.author = sample_lib::k_mdata_library_author,
                                                         .name = "SharedFilesMdata"_s}},
                                            .inst_name = "Groups And Refs"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto inst = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK(inst->audio_datas.size);
                                },
                        });
        }

        SUBCASE("library and instrument (lua)") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{.author = "Tester"_s, .name = "Test Lua"_s}},
                                            .inst_name = "Single Sample"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto inst = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK(inst->audio_datas.size);
                                },
                        });
        }

        SUBCASE("audio file shared across insts") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{
                                                .author = sample_lib::k_mdata_library_author,
                                                .name = "SharedFilesMdata"_s,
                                            }},
                                            .inst_name = "Groups And Refs"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto i = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK_EQ(i->instrument.name, "Groups And Refs"_s);
                                    CHECK_EQ(i->audio_datas.size, 4u);
                                    for (auto& d : i->audio_datas)
                                        CHECK_NEQ(d->interleaved_samples.size, 0u);
                                },
                        });
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{
                                                .author = sample_lib::k_mdata_library_author,
                                                .name = "SharedFilesMdata"_s,
                                            }},
                                            .inst_name = "Groups And Refs (copy)"_s,
                                        },
                                    .layer_index = 1,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto i = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK_EQ(i->instrument.name, "Groups And Refs (copy)"_s);
                                    CHECK_EQ(i->audio_datas.size, 4u);
                                    for (auto& d : i->audio_datas)
                                        CHECK_NEQ(d->interleaved_samples.size, 0u);
                                },
                        });
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{
                                                .author = sample_lib::k_mdata_library_author,
                                                .name = "SharedFilesMdata"_s,
                                            }},
                                            .inst_name = "Single Sample"_s,
                                        },
                                    .layer_index = 2,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto i = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK_EQ(i->instrument.name, "Single Sample"_s);
                                    CHECK_EQ(i->audio_datas.size, 1u);
                                    for (auto& d : i->audio_datas)
                                        CHECK_NEQ(d->interleaved_samples.size, 0u);
                                },
                        });
        }

        SUBCASE("audio files shared within inst") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{
                                                .author = sample_lib::k_mdata_library_author,
                                                .name = "SharedFilesMdata"_s,
                                            }},
                                            .inst_name = "Same Sample Twice"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const& request) {
                                    auto i = ExtractSuccess<ResourcePointer<sample_lib::LoadedInstrument>>(
                                        tester,
                                        r,
                                        request);
                                    CHECK_EQ(i->instrument.name, "Same Sample Twice"_s);
                                    CHECK_EQ(i->audio_datas.size, 2u);
                                    for (auto& d : i->audio_datas)
                                        CHECK_NEQ(d->interleaved_samples.size, 0u);
                                },
                        });
        };

        SUBCASE("invalid lib+path") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{.author = "foo"_s, .name = "bar"_s}},
                                            .inst_name = "bar"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const&) {
                                    auto err = r.result.TryGet<ErrorCode>();
                                    REQUIRE(err);
                                    REQUIRE(*err == CommonError::NotFound);
                                },
                        });
        }
        SUBCASE("invalid path only") {
            dyn::Append(requests,
                        {
                            .request =
                                LoadRequestInstrumentIdWithLayer {
                                    .id =
                                        {
                                            .library = {{.author = sample_lib::k_mdata_library_author,
                                                         .name = "SharedFilesMdata"_s}},
                                            .inst_name = "bar"_s,
                                        },
                                    .layer_index = 0,
                                },
                            .check_result =
                                [&](LoadResult const& r, LoadRequest const&) {
                                    auto err = r.result.TryGet<ErrorCode>();
                                    REQUIRE(err);
                                    REQUIRE(*err == CommonError::NotFound);
                                },
                        });
        }

        AtomicCountdown countdown {(u32)requests.size};
        auto& channel = OpenAsyncCommsChannel(server,
                                              {
                                                  .error_notifications = fixture.error_notif,
                                                  .result_added_callback = [&]() { countdown.CountDown(); },
                                                  .library_changed_callback = [](sample_lib::LibraryIdRef) {},
                                              });
        DEFER { CloseAsyncCommsChannel(server, channel); };

        if (requests.size) {
            for (auto& j : requests)
                j.request_id = SendAsyncLoadRequest(server, channel, j.request);

            constexpr u32 k_timeout_secs = 120;
            auto const countdown_result = countdown.WaitUntilZero(k_timeout_secs * 1000);

            if (countdown_result == WaitResult::TimedOut) {
                tester.log.Error("Timed out waiting for library resource loading to complete");
                server.request_debug_dump_current_state.Store(true, StoreMemoryOrder::Release);
                server.work_signaller.Signal();
                SleepThisThread(1000);
                // We need to hard-exit without cleaning up because the loading thread is probably deadlocked
                __builtin_abort();
            }

            usize num_results = 0;
            while (auto r = channel.results.TryPop()) {
                DEFER { r->Release(); };
                for (auto const& request : requests) {
                    if (r->id == request.request_id) {
                        fixture.error_notif.ForEach([&](ThreadsafeErrorNotifications::Item const& n) {
                            tester.log.Debug("Error Notification  {}: {}: {}",
                                             n.title,
                                             n.message,
                                             n.error_code);
                            return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
                        });
                        request.check_result(*r, request.request);
                    }
                }
                ++num_results;
            }
            REQUIRE_EQ(num_results, requests.size);
        }
    }

    SUBCASE("randomly send lots of requests") {
        sample_lib::InstrumentId const inst_ids[] {
            {
                .library = {{.author = sample_lib::k_mdata_library_author, .name = "SharedFilesMdata"_s}},
                .inst_name = "Groups And Refs"_s,
            },
            {
                .library = {{.author = sample_lib::k_mdata_library_author, .name = "SharedFilesMdata"_s}},
                .inst_name = "Groups And Refs (copy)"_s,
            },
            {
                .library = {{.author = sample_lib::k_mdata_library_author, .name = "SharedFilesMdata"_s}},
                .inst_name = "Single Sample"_s,
            },
            {
                .library = {{.author = "Tester"_s, .name = "Test Lua"_s}},
                .inst_name = "Auto Mapped Samples"_s,
            },
        };
        auto const builtin_irs = GetEmbeddedIrs();

        constexpr u32 k_num_calls = 200;
        auto random_seed = RandomSeed();
        AtomicCountdown countdown {k_num_calls};

        auto& channel = OpenAsyncCommsChannel(server,
                                              {
                                                  .error_notifications = fixture.error_notif,
                                                  .result_added_callback = [&]() { countdown.CountDown(); },
                                                  .library_changed_callback = [](sample_lib::LibraryIdRef) {},
                                              });
        DEFER { CloseAsyncCommsChannel(server, channel); };

        // We sporadically rename the library file to test the error handling of the loading thread
        DynamicArray<char> temp_rename {fixture.test_lib_path, scratch_arena};
        dyn::AppendSpan(temp_rename, ".foo");
        bool is_renamed = false;

        for (auto _ : Range(k_num_calls)) {
            SendAsyncLoadRequest(
                server,
                channel,
                (RandomIntInRange(random_seed, 0, 2) == 0)
                    ? LoadRequest {sample_lib::IrId {.library = sample_lib::k_builtin_library_id,
                                                     .ir_name = ({
                                                         auto const ele = RandomElement(
                                                             Span<EmbeddedIr const> {builtin_irs.irs,
                                                                                     builtin_irs.count},
                                                             random_seed);
                                                         String {ele.name.data, ele.name.size};
                                                     })}}
                    : LoadRequest {LoadRequestInstrumentIdWithLayer {
                          .id = RandomElement(Span<sample_lib::InstrumentId const> {inst_ids}, random_seed),
                          .layer_index = RandomIntInRange<u32>(random_seed, 0, k_num_layers - 1)}});

            SleepThisThread(RandomIntInRange(random_seed, 0, 3));

            // Let's make this a bit more interesting by simulating a file rename mid-move
            if (RandomIntInRange(random_seed, 0, 4) == 0) {
                if (is_renamed)
                    auto _ = Rename(temp_rename, fixture.test_lib_path);
                else
                    auto _ = Rename(fixture.test_lib_path, temp_rename);
                is_renamed = !is_renamed;
            }

            // Additionally, let's release one the results to test ref-counting/reuse
            if (auto r = channel.results.TryPop()) r->Release();
        }

        constexpr u32 k_timeout_secs = 25;
        auto const countdown_result = countdown.WaitUntilZero(k_timeout_secs * 1000);

        if (countdown_result == WaitResult::TimedOut) {
            tester.log.Error("Timed out waiting for library resource loading to complete");
            server.request_debug_dump_current_state.Store(true, StoreMemoryOrder::Release);
            SleepThisThread(1000);
            // We need to hard-exit without cleaning up because the loading thread is probably deadlocked
            __builtin_abort();
        }
    }

    return k_success;
}

} // namespace sample_lib_server

TEST_REGISTRATION(RegisterSampleLibraryServerTests) {
    REGISTER_TEST(sample_lib_server::TestSampleLibraryServer);
}
