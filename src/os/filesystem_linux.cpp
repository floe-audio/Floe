// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <ftw.h>
#include <link.h>
#include <linux/limits.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "filesystem.hpp"

static ErrorCodeOr<ssize> CopyFile(char const* source, char const* destination) {
    int input;
    int output;
    if ((input = open(source, O_RDONLY)) == -1) return FilesystemErrnoErrorCode(errno);
    DEFER { close(input); };
    if ((output = creat(destination, 0660)) == -1) return FilesystemErrnoErrorCode(errno);
    DEFER { close(output); };

    off_t bytes_copied = 0;
    struct stat fileinfo = {};
    if (fstat(input, &fileinfo) != 0) return FilesystemErrnoErrorCode(errno);
    ssize result = sendfile(output, input, &bytes_copied, CheckedCast<usize>(fileinfo.st_size));
    if (result == -1) return FilesystemErrnoErrorCode(errno);

    return result;
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    PathArena temp_path_allocator {Malloc::Instance()};
    auto from_nt = NullTerminated(from, temp_path_allocator);
    auto to_nt = NullTerminated(to, temp_path_allocator);

    if (access(to_nt, F_OK) == 0) {
        switch (existing) {
            case ExistingDestinationHandling::Fail: return ErrorCode(FilesystemError::PathAlreadyExists);
            case ExistingDestinationHandling::Skip: return k_success;
            case ExistingDestinationHandling::Overwrite: break;
        }
    }

    TRY(CopyFile(from_nt, to_nt));
    return k_success;
}

ErrorCodeOr<MutableString> AbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);

    PathArena temp_path_allocator {Malloc::Instance()};
    DynamicArray<char> path_nt(path, temp_path_allocator);

    if (StartsWith(path_nt, '~')) {
        char const* home = secure_getenv("HOME");
        if (home == nullptr) return ErrorCode(FilesystemError::PathDoesNotExist);
        dyn::Remove(path_nt, 0, 1);
        dyn::PrependSpan(path_nt, FromNullTerminated(home));
    }

    char result[PATH_MAX] {};
    auto _ = realpath(dyn::NullTerminated(path_nt), result);
    auto const result_path = FromNullTerminated(result);
    if (path::IsAbsolute(result_path)) return result_path.Clone(a);

    return FilesystemErrnoErrorCode(errno);
}

ErrorCodeOr<MutableString> CanonicalizePath(Allocator& a, String path) { return AbsolutePath(a, path); }

ErrorCodeOr<String> TrashFileOrDirectory(String path, Allocator&) {
    // IMPROVE: try to work out the trash directory from the environment and use that
    TRY(Delete(path, {.type = DeleteOptions::Type::Any, .fail_if_not_exists = true}));
    return path;
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    PathArena temp_path_allocator {Malloc::Instance()};
    auto const path_ptr = NullTerminated(path, temp_path_allocator);

    if (remove(path_ptr) == 0)
        return k_success;
    else {
        if (errno == ENOENT && !options.fail_if_not_exists) return k_success;
        if ((errno == EEXIST || errno == ENOTEMPTY) &&
            (options.type == DeleteOptions::Type::Any ||
             options.type == DeleteOptions::Type::DirectoryRecursively)) {

            char* files[] = {(char*)path_ptr, nullptr};
            auto ftsp = fts_open(files, FTS_NOCHDIR | FTS_PHYSICAL, nullptr);
            if (!ftsp) return FilesystemErrnoErrorCode(errno);
            DEFER { fts_close(ftsp); };

            while (auto curr = fts_read(ftsp)) {
                switch (curr->fts_info) {
                    // IMPROVE: handle more error cases
                    case FTS_DP:
                    case FTS_F:
                    case FTS_SL:
                    case FTS_SLNONE:
                    case FTS_DEFAULT:
                        if (remove(curr->fts_accpath) != 0) return FilesystemErrnoErrorCode(errno);
                        break;
                }
            }
            return k_success;
        }

        return FilesystemErrnoErrorCode(errno);
    }
}

ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options) {
    PathArena temp_path_allocator {Malloc::Instance()};
    DynamicArray<char> buffer {path, temp_path_allocator};

    if (mkdir(dyn::NullTerminated(buffer), 0700) == 0) {
        return k_success;
    } else {
        if (errno == EEXIST && !options.fail_if_exists) return k_success;
        if (errno == ENOENT && options.create_intermediate_directories) {
            dyn::Clear(buffer);
            for (auto const part : SplitIterator {path, '/'}) {
                dyn::Append(buffer, '/');
                dyn::AppendSpan(buffer, part);
                if (mkdir(dyn::NullTerminated(buffer), 0700) != 0) {
                    if (errno == EEXIST)
                        continue;
                    else
                        return FilesystemErrnoErrorCode(errno);
                }
            }
            return k_success;
        }

        return FilesystemErrnoErrorCode(errno);
    }
}

static ErrorCodeOr<String> FindMountPoint(char const* path, ArenaAllocator& arena) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) return FilesystemErrnoErrorCode(errno, "stat");

    auto mtab = setmntent("/etc/mtab", "r");
    if (!mtab) return FilesystemErrnoErrorCode(errno, "setmntent");
    DEFER { endmntent(mtab); };

    auto mnt_buffer = arena.AllocateExactSizeUninitialised<char>(1024);
    struct mntent entry;

    while (getmntent_r(mtab, &entry, mnt_buffer.data, (int)mnt_buffer.size) != nullptr) {
        struct stat mount_stat;
        if (stat(entry.mnt_dir, &mount_stat) == 0) {
            // ok to return the string here because it's in the arena
            if (path_stat.st_dev == mount_stat.st_dev) return FromNullTerminated(entry.mnt_dir);
        }
    }

    return ErrorCode {FilesystemError::PathDoesNotExist};
}

static bool PathsHaveSameDevice(char const* path1, char const* path2) {
    struct stat path1_stat;
    if (stat(path1, &path1_stat) != 0) return false;

    struct stat path2_stat;
    if (stat(path2, &path2_stat) != 0) return false;

    return path1_stat.st_dev == path2_stat.st_dev;
}

ErrorCodeOr<MutableString> TemporaryDirectoryOnSameFilesystemAs(String path, Allocator& a) {
    ASSERT(path::IsAbsolute(path));

    auto const standard_temp = ({
        char const* str = "/tmp";
        if (auto const dir = secure_getenv("TMPDIR"))
            str = dir;
        else
            str = P_tmpdir;
        str;
    });

    PathArena temp_path_allocator {Malloc::Instance()};
    auto const path_nt = NullTerminated(path, temp_path_allocator);

    String base_path {};
    if (PathsHaveSameDevice(path_nt, standard_temp))
        base_path = FromNullTerminated(standard_temp);
    else
        base_path = TRY(FindMountPoint(path_nt, temp_path_allocator));

    auto seed = (u64)NanosecondsSinceEpoch();
    auto const result =
        path::Join(a, Array {base_path, UniqueFilename(k_temporary_directory_prefix, "", seed)});
    TRY(CreateDirectory(result, {.create_intermediate_directories = true, .fail_if_exists = false}));

    return result;
}

MutableString KnownDirectory(Allocator& a, KnownDirectoryType type, KnownDirectoryOptions options) {
    Optional<String> home_path {};
    String abs_path {};
    switch (type) {
        case KnownDirectoryType::Temporary: {
            MutableString result {};
            if (auto const dir = secure_getenv("TMPDIR"))
                result = a.Clone(FromNullTerminated(dir));
            else
                result = a.Clone(String(P_tmpdir));
            if (options.create) {
                if (auto const o = CreateDirectory(result,
                                                   {
                                                       .create_intermediate_directories = true,
                                                       .fail_if_exists = false,
                                                   });
                    o.HasError()) {
                    if (options.error_log)
                        auto _ = fmt::FormatToWriter(*options.error_log,
                                                     "Failed to create directory '{}': {}\n",
                                                     result,
                                                     o.Error());
                }
            }
            return result;
        }
        case KnownDirectoryType::Documents: home_path = "Documents"; break;
        case KnownDirectoryType::Downloads: home_path = "Downloads"; break;
        case KnownDirectoryType::Logs: home_path = ".local/state"; break;
        case KnownDirectoryType::GlobalData: home_path = ""; break;
        case KnownDirectoryType::UserData: home_path = ".local/state"; break;

        case KnownDirectoryType::UserClapPlugins: home_path = ".clap"; break;
        case KnownDirectoryType::UserVst3Plugins: home_path = ".vst3"; break;
        case KnownDirectoryType::GlobalClapPlugins: abs_path = "/usr/lib/clap"_s; break;
        case KnownDirectoryType::GlobalVst3Plugins: abs_path = "/usr/lib/vst3"_s; break;

        case KnownDirectoryType::MiragePreferencesAlternate: home_path = ""; break;
        case KnownDirectoryType::MirageGlobalPreferences:
        case KnownDirectoryType::MiragePreferences: home_path = ".config"; break;
        case KnownDirectoryType::MirageGlobalData: home_path = ""; break;

        case KnownDirectoryType::Count: PanicIfReached();
    }

    MutableString result {};
    if (home_path) {
        char const* home = secure_getenv("HOME");
        if (home == nullptr) {
            if (options.error_log)
                auto _ = fmt::FormatToWriter(*options.error_log, "HOME environment variable not set\n");
            home = "unknown";
        }
        result = path::Join(a, Array {FromNullTerminated(home), *home_path});
    } else {
        result = a.Clone(abs_path);
    }

    if (options.create) {
        auto const o = CreateDirectory(result,
                                       {
                                           .create_intermediate_directories = true,
                                           .fail_if_exists = false,
                                       });
        if (o.HasError() && options.error_log)
            auto _ = fmt::FormatToWriter(*options.error_log,
                                         "Failed to create directory '{}': {}\n",
                                         result,
                                         o.Error());
    }
    return result;
}

struct LinuxWatchedDirectory {
    struct SubDir {
        int watch_id;
        String subpath;
        bool watch_id_invalidated;
        Optional<u32> rename_cookie;
    };

    int root_watch_id;
    ArenaList<SubDir> subdirs;
    PathPool path_pool;
};

static ErrorCodeOr<int> InotifyWatch(int inotify_id, char const* path) {
    ZoneScoped;
    auto const watch_id = inotify_add_watch(inotify_id,
                                            path,
                                            IN_EXCL_UNLINK | IN_ONLYDIR | IN_MODIFY | IN_CREATE | IN_DELETE |
                                                IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM);
    if (watch_id == -1) return FilesystemErrnoErrorCode(errno);
    return watch_id;
}

static void InotifyUnwatch(int inotify_id, int watch_id) { inotify_rm_watch(inotify_id, watch_id); }

static void UnwatchDirectory(int inotify_id, DirectoryWatcher::WatchedDirectory& d) {
    auto native_data = (LinuxWatchedDirectory*)d.native_data.pointer;
    if (!native_data) return;
    auto const rc = inotify_rm_watch(inotify_id, native_data->root_watch_id);
    ASSERT_EQ(rc, 0);
    d.arena.Delete(native_data);
}

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a) {
    ZoneScoped;
    DirectoryWatcher result {
        .allocator = a,
        .watched_dirs = {},
    };
    auto h = inotify_init1(IN_NONBLOCK);
    if (h == -1) return FilesystemErrnoErrorCode(errno);
    result.native_data.int_id = h;
    return result;
}

void DestoryDirectoryWatcher(DirectoryWatcher& watcher) {
    ZoneScoped;
    ArenaAllocatorWithInlineStorage<1000> scratch_arena {Malloc::Instance()};

    // We do not need to close each watch: "all associated watches are automatically freed"
    for (auto& dir : watcher.watched_dirs)
        if (dir.native_data.pointer != nullptr)
            dir.arena.Delete((LinuxWatchedDirectory*)dir.native_data.pointer);

    watcher.watched_dirs.Clear();

    close(watcher.native_data.int_id);
}

static ErrorCodeOr<LinuxWatchedDirectory*> WatchDirectory(DirectoryWatcher::WatchedDirectory& dir,
                                                          int inotify_id,
                                                          String path,
                                                          bool recursive,
                                                          Allocator& allocator,
                                                          ArenaAllocator& scratch_arena) {
    bool success = false;
    auto watch_id = TRY(InotifyWatch(inotify_id, NullTerminated(path, scratch_arena)));
    DEFER {
        if (!success) InotifyUnwatch(inotify_id, watch_id);
    };

    ArenaList<LinuxWatchedDirectory::SubDir> subdirs {};
    PathPool path_pool {};
    if (recursive) {
        auto const try_watch_subdirs = [&]() -> ErrorCodeOr<void> {
            auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                        dir.path,
                                                        {
                                                            .wildcard = "*",
                                                            .get_file_size = false,
                                                        }));
            DEFER { dir_iterator::Destroy(it); };
            DynamicArray<char> full_subpath {dir.path, scratch_arena};
            while (auto const entry = TRY(dir_iterator::Next(it, scratch_arena))) {
                if (entry->type == FileType::Directory) {
                    String subpath = entry->subpath;

                    dyn::Resize(full_subpath, dir.path.size);
                    path::JoinAppend(full_subpath, subpath);

                    auto subdir = subdirs.PrependUninitialised(dir.arena);
                    PLACEMENT_NEW(subdir)
                    LinuxWatchedDirectory::SubDir {
                        .watch_id = TRY(InotifyWatch(inotify_id, dyn::NullTerminated(full_subpath))),
                        .subpath = path_pool.Clone(subpath, dir.arena),
                        .watch_id_invalidated = false,
                    };
                }
            }
            return k_success;
        };

        auto const outcome = try_watch_subdirs();
        if (outcome.HasError()) {
            for (auto const& subdir : subdirs)
                InotifyUnwatch(inotify_id, subdir.watch_id);
            return outcome.Error();
        }
    }

    auto result = allocator.New<LinuxWatchedDirectory>(watch_id, Move(subdirs), path_pool);
    success = true;
    return result;
}

constexpr bool k_debug_inotify = false && !PRODUCTION_BUILD;

ErrorCodeOr<Span<DirectoryWatcher::DirectoryChanges const>>
PollDirectoryChanges(DirectoryWatcher& watcher, PollDirectoryChangesArgs args) {
    watcher.HandleWatchedDirChanges(args.dirs_to_watch, args.retry_failed_directories);

    for (auto& dir : watcher.watched_dirs) {
        dir.directory_changes.Clear();

        if (dir.native_data.pointer != nullptr) {
            auto& native_dir = *(LinuxWatchedDirectory*)dir.native_data.pointer;
            native_dir.subdirs.RemoveIf([&](auto& subdir) {
                if (subdir.watch_id_invalidated) {
                    native_dir.path_pool.Free(subdir.subpath);
                    return true;
                }
                return false;
            });
        }

        switch (dir.state) {
            case DirectoryWatcher::WatchedDirectory::State::NotWatching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::Watching: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::WatchingFailed: break; // no change
            case DirectoryWatcher::WatchedDirectory::State::NeedsWatching: {
                auto const outcome = WatchDirectory(dir,
                                                    watcher.native_data.int_id,
                                                    dir.path,
                                                    dir.recursive,
                                                    dir.arena,
                                                    args.scratch_arena);
                if (outcome.HasValue()) {
                    dir.state = DirectoryWatcher::WatchedDirectory::State::Watching;
                    dir.native_data.pointer = outcome.Value();
                } else {
                    dir.state = DirectoryWatcher::WatchedDirectory::State::WatchingFailed;
                    dir.directory_changes.error = outcome.Error();
                    ASSERT(dir.native_data.pointer == nullptr);
                }
                break;
            }
            case DirectoryWatcher::WatchedDirectory::State::NeedsUnwatching: {
                if (dir.native_data.pointer != nullptr) {
                    auto& native_dir = *(LinuxWatchedDirectory*)dir.native_data.pointer;
                    for (auto& subdir : native_dir.subdirs) {
                        ASSERT_EQ(subdir.watch_id_invalidated, false);
                        InotifyUnwatch(watcher.native_data.int_id, subdir.watch_id);
                    }
                    UnwatchDirectory(watcher.native_data.int_id, dir);
                }
                dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;
                dir.native_data.pointer = nullptr;
                break;
            }
        }
    }

    watcher.RemoveAllNotWatching();

    alignas(struct inotify_event) char buf[4096];
    while (true) {
        ZoneNamedN(read_trace, "inotify read", true);
        auto const bytes_read = read(watcher.native_data.int_id, buf, sizeof(buf));
        if (bytes_read == -1 && errno != EAGAIN) return FilesystemErrnoErrorCode(errno);

        if (bytes_read <= 0) break;

        const struct inotify_event* event_ptr;
        for (char* ptr = buf; ptr < buf + bytes_read; ptr += sizeof(struct inotify_event) + event_ptr->len) {
            event_ptr = CheckedPointerCast<const struct inotify_event*>(ptr);
            auto const& event = *event_ptr;

            // event queue overflowed, we might be missing any number of events. event->wd is -1.
            if (event.mask & IN_Q_OVERFLOW) {
                for (auto& d : watcher.watched_dirs) {
                    d.directory_changes.Add(
                        {
                            .subpath = {},
                            .file_type = FileType::Directory,
                            .changes = DirectoryWatcher::ChangeType::ManualRescanNeeded,
                        },
                        args.result_arena);
                }
                continue;
            }

            struct ThisDir {
                String RootDirPath() const { return dir.path; }
                String SubDirPath() const { return subdir ? subdir->subpath : ""; }
                auto& Native() { return *(LinuxWatchedDirectory*)dir.native_data.pointer; }
                bool IsForRoot() const { return subdir == nullptr; }
                DirectoryWatcher::WatchedDirectory& dir;
                LinuxWatchedDirectory::SubDir* subdir; // if null, then it's for the root directory
            };
            ThisDir this_dir = ({
                Optional<ThisDir> d {};
                for (auto& watch : watcher.watched_dirs) {
                    if (watch.state != DirectoryWatcher::WatchedDirectory::State::Watching) continue;

                    auto& native_data = *(LinuxWatchedDirectory*)watch.native_data.pointer;
                    if (native_data.root_watch_id == event.wd) {
                        d.Emplace(watch, nullptr);
                        break;
                    } else {
                        for (auto& subdir : native_data.subdirs) {
                            if (subdir.watch_id == event.wd && !subdir.watch_id_invalidated) {
                                d.Emplace(watch, &subdir);
                                break;
                            }
                        }
                        if (d.HasValue()) break;
                    }
                }
                if (!d.HasValue()) {
                    if constexpr (k_debug_inotify) {
                        LogDebug(ModuleName::Filesystem,
                                 "ERROR: inotify event for unknown watch id: {}, name_len: {}, name: {}",
                                 event.wd,
                                 event.len,
                                 FromNullTerminated(event.len ? event.name : ""));
                        LogDebug({}, "Available watch ids:");
                        bool found_ids = false;
                        for (auto& watch : watcher.watched_dirs) {
                            if (watch.state != DirectoryWatcher::WatchedDirectory::State::Watching) continue;
                            found_ids = true;
                            auto& native_data = *(LinuxWatchedDirectory*)watch.native_data.pointer;
                            LogDebug(ModuleName::Filesystem,
                                     "  {}: {}",
                                     native_data.root_watch_id,
                                     watch.path);
                            for (auto& subdir : native_data.subdirs)
                                LogDebug(ModuleName::Filesystem,
                                         "    {}: {}",
                                         subdir.watch_id,
                                         subdir.subpath);
                        }
                        if (!found_ids) LogDebug(ModuleName::Filesystem, "  none");
                    }
                    continue;
                }
                d.Value();
            });

            if constexpr (k_debug_inotify) {
                DynamicArrayBounded<char, 2000> printout;
                auto _ = [&]() -> ErrorCodeOr<void> {
                    auto writer = dyn::WriterFor(printout);
                    TRY(fmt::AppendLine(writer, "{{"));
                    TRY(fmt::AppendLine(writer, "  .wd = {}", event.wd));

                    {
                        TRY(fmt::FormatToWriter(writer, "  .mask = "));
                        auto const sz = printout.size;
                        if (event.mask & IN_ACCESS) dyn::AppendSpan(printout, "ACCESS, "_s);
                        if (event.mask & IN_ATTRIB) dyn::AppendSpan(printout, "ATTRIB, "_s);
                        if (event.mask & IN_CLOSE_WRITE) dyn::AppendSpan(printout, "CLOSE_WRITE, "_s);
                        if (event.mask & IN_CLOSE_NOWRITE) dyn::AppendSpan(printout, "CLOSE_NOWRITE, "_s);
                        if (event.mask & IN_CREATE) dyn::AppendSpan(printout, "CREATE, "_s);
                        if (event.mask & IN_DELETE) dyn::AppendSpan(printout, "DELETE, "_s);
                        if (event.mask & IN_DELETE_SELF) dyn::AppendSpan(printout, "DELETE_SELF, "_s);
                        if (event.mask & IN_MODIFY) dyn::AppendSpan(printout, "MODIFY, "_s);
                        if (event.mask & IN_MOVE_SELF) dyn::AppendSpan(printout, "MOVE_SELF, "_s);
                        if (event.mask & IN_MOVED_FROM) dyn::AppendSpan(printout, "MOVED_FROM, "_s);
                        if (event.mask & IN_MOVED_TO) dyn::AppendSpan(printout, "MOVED_TO, "_s);
                        if (event.mask & IN_OPEN) dyn::AppendSpan(printout, "OPEN, "_s);
                        if (event.mask & IN_IGNORED) dyn::AppendSpan(printout, "IGNORED, "_s);
                        if (event.mask & IN_ISDIR) dyn::AppendSpan(printout, "ISDIR, "_s);
                        if (sz != printout.size) printout.size -= 2;
                        dyn::Append(printout, '\n');
                    }

                    TRY(fmt::AppendLine(writer,
                                        "  .path = \"{}\" => \"{}\" => \"{}\"",
                                        this_dir.RootDirPath(),
                                        this_dir.SubDirPath(),
                                        FromNullTerminated(event.len ? event.name : "")));
                    TRY(writer.WriteChars("}}"));
                    return k_success;
                }();
                LogDebug(ModuleName::Filesystem, printout);
            }

            // "Watch was removed explicitly (inotify_rm_watch()) or automatically (file was deleted, or
            // filesystem was unmounted)"
            // This can be given BEFORE other events for this watch_id so we mustn't invalidate it here.
            if (event.mask & IN_IGNORED) {
                if (this_dir.IsForRoot()) {
                    this_dir.dir.state = DirectoryWatcher::WatchedDirectory::State::NotWatching;
                    this_dir.dir.native_data.pointer = nullptr;
                    this_dir.dir.directory_changes.Add(
                        {
                            .subpath = {},
                            .file_type = FileType::Directory,
                            .changes = DirectoryWatcher::ChangeType::Deleted,
                        },
                        args.result_arena);
                } else {
                    this_dir.subdir->watch_id_invalidated = true;
                }
                continue;
            }

            if (this_dir.dir.recursive && event.mask & IN_ISDIR) {
                // NOTE: we handle the 'deleted' case under IN_IGNORED above

                DynamicArray<char> subpath {args.scratch_arena};
                if (!this_dir.IsForRoot()) path::JoinAppend(subpath, this_dir.SubDirPath());
                if (event.len) path::JoinAppend(subpath, FromNullTerminated(event.name));

                // If a folder has changed it's name we need to update that.
                // NOTE: IN_MOVED_TO and IN_MOVED_FROM are given to the parent directory of the thing that was
                // moved.
                if (event.mask & IN_MOVED_FROM) {
                    auto& native = this_dir.Native();
                    for (auto& s : native.subdirs)
                        if (s.subpath == subpath) s.rename_cookie = event.cookie;
                }
                if (event.mask & IN_MOVED_TO) {
                    auto& native = this_dir.Native();
                    for (auto& s : native.subdirs) {
                        if (s.rename_cookie == event.cookie) {
                            native.path_pool.Free(s.subpath);
                            s.subpath = native.path_pool.Clone(subpath, this_dir.dir.arena);
                            s.rename_cookie = k_nullopt;
                        }
                    }
                }

                // A new directory was created, we need to watch it if we are watching recursively
                if (event.mask & IN_CREATE) {
                    DynamicArray<char> full_path {this_dir.RootDirPath(), args.scratch_arena};
                    path::JoinAppend(full_path, ArrayT<String>({subpath}));

                    // watch the created dir
                    {
                        auto const watch_id_outcome =
                            InotifyWatch(watcher.native_data.int_id, dyn::NullTerminated(full_path));
                        if (watch_id_outcome.HasError()) {
                            auto const err = watch_id_outcome.Error();
                            if (err == FilesystemError::PathDoesNotExist) {
                                // The directory was deleted before we could watch it
                                continue;
                            } else {
                                return err;
                            }
                        }

                        auto subdir = this_dir.Native().subdirs.PrependUninitialised(this_dir.dir.arena);
                        PLACEMENT_NEW(subdir)
                        LinuxWatchedDirectory::SubDir {
                            .watch_id = watch_id_outcome.Value(),
                            .subpath = this_dir.Native().path_pool.Clone(subpath, this_dir.dir.arena),
                        };
                    }

                    // we also need to check the contents of the new directory, it might have already have
                    // files or subdirectories added
                    {
                        auto it = TRY(dir_iterator::RecursiveCreate(args.scratch_arena,
                                                                    full_path,
                                                                    {
                                                                        .wildcard = "*",
                                                                        .get_file_size = false,
                                                                    }));
                        DEFER { dir_iterator::Destroy(it); };
                        while (auto const entry = TRY(dir_iterator::Next(it, args.scratch_arena))) {
                            DynamicArray<char> entry_full_path {String(full_path), args.scratch_arena};
                            path::JoinAppend(entry_full_path, entry->subpath);

                            ASSERT(StartsWithSpan(entry_full_path, this_dir.RootDirPath()));
                            auto subsubpath = entry_full_path.Items().SubSpan(this_dir.RootDirPath().size);
                            if (StartsWith(subsubpath, '/')) subsubpath = subsubpath.SubSpan(1);

                            this_dir.dir.directory_changes.Add(
                                {
                                    .subpath = args.result_arena.Clone(subsubpath),
                                    .file_type = entry->type,
                                    .changes = DirectoryWatcher::ChangeType::Added,
                                },
                                args.result_arena);

                            if (entry->type == FileType::Directory) {
                                auto const sub_watch_id_outcome =
                                    InotifyWatch(watcher.native_data.int_id,
                                                 dyn::NullTerminated(entry_full_path));
                                bool skip = false;
                                if (sub_watch_id_outcome.HasError()) {
                                    auto const err = sub_watch_id_outcome.Error();
                                    if (err == FilesystemError::PathDoesNotExist)
                                        skip = true;
                                    else
                                        return err;
                                }

                                if (!skip) {
                                    auto subsubdir =
                                        this_dir.Native().subdirs.PrependUninitialised(this_dir.dir.arena);
                                    PLACEMENT_NEW(subsubdir)
                                    LinuxWatchedDirectory::SubDir {
                                        .watch_id = sub_watch_id_outcome.Value(),
                                        .subpath =
                                            this_dir.Native().path_pool.Clone(subpath, this_dir.dir.arena),
                                    };
                                }
                            }
                        }
                    }
                }
            }

            if (event.mask & IN_MOVE_SELF) {
                // I've not seen this event occur yet, if it does happen we should probably do something
                if constexpr (!PRODUCTION_BUILD) PanicIfReached();
            }

            auto const event_name = event.len ? FromNullTerminated(event.name) : String {};

            DirectoryWatcher::ChangeTypeFlags changes {};
            if (event.mask & IN_MODIFY || event.mask & IN_CLOSE_WRITE)
                changes |= DirectoryWatcher::ChangeType::Modified;
            else if (event.mask & IN_MOVED_TO)
                changes |= DirectoryWatcher::ChangeType::RenamedNewName;
            else if (event.mask & IN_MOVED_FROM)
                changes |= DirectoryWatcher::ChangeType::RenamedOldName;
            else if (event.mask & IN_DELETE)
                changes |= DirectoryWatcher::ChangeType::Deleted;
            else if (event.mask & IN_CREATE)
                changes |= DirectoryWatcher::ChangeType::Added;

            if (changes) {
                this_dir.dir.directory_changes.Add(
                    {
                        .subpath =
                            this_dir.SubDirPath().size
                                ? path::Join(args.result_arena, Array {this_dir.SubDirPath(), event_name})
                                : args.result_arena.Clone(event_name),
                        .file_type = (event.mask & IN_ISDIR) ? FileType::Directory : FileType::File,
                        .changes = changes,
                    },
                    args.result_arena);
            }
        }
    }

    return watcher.AllDirectoryChanges(args.result_arena);
}
