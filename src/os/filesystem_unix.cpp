// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#if __linux__
#include <sys/stat.h>
#endif

#include "foundation/foundation.hpp"

#include "filesystem.hpp"

static_assert(path::k_max >= PATH_MAX);

ErrorCodeOr<void> WindowsSetFileAttributes(String, Optional<WindowsFileAttributes>) { return k_success; }

ErrorCodeOr<void> Rename(String from, String to) {
    PathArena temp_path_allocator {Malloc::Instance()};
    auto const result =
        rename(NullTerminated(from, temp_path_allocator), NullTerminated(to, temp_path_allocator));
    if (result != 0) {
        switch (result) {
            case EINVAL:
            case EFAULT: PanicIfReached();
        }
        return FilesystemErrnoErrorCode(errno, "rename");
    }
    return k_success;
}

ErrorCodeOr<FileType> GetFileType(String path) {
    PathArena temp_path_allocator {Malloc::Instance()};
    struct ::stat info;
    auto r = ::stat(NullTerminated(path, temp_path_allocator), &info);
    if (r != 0) return FilesystemErrnoErrorCode(errno);

    if ((info.st_mode & S_IFMT) == S_IFDIR) return FileType::Directory;
    return FileType::File;
}

namespace dir_iterator {

ErrorCodeOr<Iterator> Create(ArenaAllocator& arena, String path, Options options) {
    path = path::TrimDirectorySeparatorsEnd(path);
    auto result = TRY(Iterator::InternalCreate(arena, path, options));

    ArenaAllocatorWithInlineStorage<1024> scratch_arena {Malloc::Instance()};
    auto handle = opendir(NullTerminated(result.base_path, scratch_arena));
    if (!handle) return FilesystemErrnoErrorCode(errno, "opendir");
    result.handle = handle;

    return result;
}

void Destroy(Iterator& it) {
    if (it.handle) closedir((DIR*)it.handle);
}

ErrorCodeOr<Optional<Entry>> Next(Iterator& it, ArenaAllocator& result_arena) {
    ASSERT(it.handle);
    if (it.reached_end) return Optional<Entry> {};
    bool skip;
    do {
        skip = false;
        errno = 0;
        // "modern implementations (including the glibc implementation), concurrent calls to readdir() that
        // specify different directory streams are thread-safe"
        auto entry = readdir((DIR*)it.handle); // NOLINT(concurrency-mt-unsafe)
        if (entry) {
            auto const entry_name = FromNullTerminated(entry->d_name);
            if (!MatchWildcard(it.options.wildcard, entry_name) || entry_name == "."_s ||
                entry_name == ".."_s ||
                (it.options.skip_dot_files && entry_name.size && entry_name[0] == '.')) {
                skip = true;
            } else {
                Entry result {
                    .subpath = result_arena.Clone(entry_name),
                    .type = entry->d_type == DT_DIR ? FileType::Directory : FileType::File,
                    .file_size = ({
                        u64 s = 0;
                        if (it.options.get_file_size) {
                            PathArena temp_path_allocator {Malloc::Instance()};
                            auto const full_path = fmt::Join(temp_path_allocator,
                                                             Array {
                                                                 it.base_path,
                                                                 "/"_s,
                                                                 entry_name,
                                                                 "\0"_s,
                                                             });
                            struct stat info;
                            if (stat(full_path.data, &info) != 0) return FilesystemErrnoErrorCode(errno);
                            s = (u64)info.st_size;
                        }
                        s;
                    }),
                };
                return result;
            }
        } else {
            it.reached_end = true;
            if (errno) return FilesystemErrnoErrorCode(errno);
            break;
        }
    } while (skip);
    return Optional<Entry> {};
}

} // namespace dir_iterator

ErrorCodeOr<bool> File::Lock(FileLockOptions options) {
    int const operation = ({
        int r {LOCK_UN};
        switch (options.type) {
            case FileLockOptions::Type::Shared: r = LOCK_SH; break;
            case FileLockOptions::Type::Exclusive: r = LOCK_EX; break;
        }
        if (options.non_blocking) r |= LOCK_NB;
        r;
    });
    auto const result = flock(handle, operation);
    if (result != 0) {
        if (errno == EWOULDBLOCK) return false;
        return FilesystemErrnoErrorCode(errno, "flock");
    }
    return true;
}

ErrorCodeOr<void> File::Unlock() {
    auto const result = flock(handle, LOCK_UN);
    if (result != 0) return FilesystemErrnoErrorCode(errno, "flock");
    return k_success;
}

ErrorCodeOr<s128> File::LastModifiedTimeNsSinceEpoch() {
    struct stat file_stat;
    if (fstat(handle, &file_stat) != 0) return FilesystemErrnoErrorCode(errno, "fstat");
#if IS_LINUX
    auto const modified_time = file_stat.st_mtim;
#elif IS_MACOS
    auto const modified_time = file_stat.st_mtimespec;
#endif
    return ((s128)modified_time.tv_sec * (s128)1'000'000'000) + (s128)modified_time.tv_nsec;
}

ErrorCodeOr<void> File::SetLastModifiedTimeNsSinceEpoch(s128 ns_since_epoch) {
    struct timespec times[2];
    times[0].tv_sec = decltype(times[0].tv_sec)(ns_since_epoch / (s128)1'000'000'000);
    times[0].tv_nsec = ns_since_epoch % 1'000'000'000;
    times[1] = times[0];
    if (futimens(handle, times) != 0) return FilesystemErrnoErrorCode(errno, "futimens");
    return k_success;
}

void File::CloseFile() {
    if (handle != -1) close(handle);
    handle = -1;
}

ErrorCodeOr<void> File::Flush() {
    auto result = fsync(handle);
    if (result == 0) return k_success;
    return FilesystemErrnoErrorCode(errno, "fsync");
}

static_assert(sizeof(off_t) == 8, "you must #define _FILE_OFFSET_BITS 64");

ErrorCodeOr<u64> File::CurrentPosition() {
    auto const result = lseek(handle, 0, SEEK_CUR);
    if (result == -1) return FilesystemErrnoErrorCode(errno, "lseek");
    ASSERT(result >= 0);
    if (result < 0) return 0ull;
    return (u64)result;
}

ErrorCodeOr<void> File::Seek(s64 const offset, SeekOrigin origin) {
    int const origin_flag = ({
        int r {SEEK_CUR};
        switch (origin) {
            case SeekOrigin::Start: r = SEEK_SET; break;
            case SeekOrigin::End: r = SEEK_END; break;
            case SeekOrigin::Current: r = SEEK_CUR; break;
        }
        r;
    });
    auto const result = lseek(handle, offset, origin_flag);
    if (result == -1) return FilesystemErrnoErrorCode(errno, "lseek");
    return k_success;
}

ErrorCodeOr<usize> File::Write(Span<u8 const> data) {
    auto const num_written = ::write(handle, data.data, data.size);
    if (num_written == -1) return FilesystemErrnoErrorCode(errno, "write");
    return static_cast<usize>(num_written);
}

ErrorCodeOr<usize> File::Read(void* data, usize num_bytes) {
    auto const num_read = ::read(handle, data, num_bytes);
    if (num_read == -1) return FilesystemErrnoErrorCode(errno, "read");
    return static_cast<usize>(num_read);
}

ErrorCodeOr<u64> File::FileSize() {
    TRY(Seek(0, SeekOrigin::End));
    auto const size = CurrentPosition();
    TRY(Seek(0, SeekOrigin::Start));
    return size.Value();
}

ErrorCodeOr<void> File::Truncate(u64 new_size) {
    auto const result = ftruncate(handle, (off_t)new_size);
    if (result != 0) return FilesystemErrnoErrorCode(errno, "ftruncate");
    return k_success;
}

ErrorCodeOr<File> OpenFile(String filename, FileMode mode) {
    PathArena temp_allocator {Malloc::Instance()};

    int flags = 0;

    {
        auto const capability = ToInt(mode.capability);

        if ((capability & ToInt(FileMode::Capability::ReadWrite)) == ToInt(FileMode::Capability::ReadWrite))
            flags |= O_RDWR;
        else if (capability & ToInt(FileMode::Capability::Write))
            flags |= O_WRONLY;
        else if (capability & ToInt(FileMode::Capability::Read))
            flags |= O_RDONLY;

        if (capability & ToInt(FileMode::Capability::Append)) {
            flags |= O_APPEND;

            // We want Append mode to match Windows, where by default it includes Write access. open()
            // requires at least one of O_RDONLY, O_WRONLY, or O_RDWR.

            // Add O_WRONLY if neither O_RDONLY nor O_RDWR are set.
            if (!(flags & (O_RDONLY | O_RDWR))) flags |= O_WRONLY;

            // If O_RDONLY is set, replace it with O_RDWR.
            if (flags & O_RDONLY) {
                flags &= ~O_RDONLY;
                flags |= O_RDWR;
            }
        }
    }

    switch (mode.creation) {
        case FileMode::Creation::OpenExisting: break;
        case FileMode::Creation::OpenAlways: flags |= O_CREAT; break;
        case FileMode::Creation::CreateNew: flags |= O_CREAT | O_EXCL; break;
        case FileMode::Creation::CreateAlways: flags |= O_CREAT | O_TRUNC; break;
        case FileMode::Creation::TruncateExisting: flags |= O_TRUNC; break;
    }

    int fd =
        open(NullTerminated(filename, temp_allocator), flags, CheckedCast<mode_t>(mode.default_permissions));
    if (fd == -1) return FilesystemErrnoErrorCode(errno, "open");
    ASSERT(fd >= 0);

    if (mode.everyone_read_write) {
        // It's necessary to use fchmod() to set the permissions instead of open(mode = 0666) because open()
        // uses umask and so will likely not actually set the permissions we want. fchmod() doesn't have that
        // problem.
        if (fchmod(fd, 0666) != 0) {
            close(fd);
            return FilesystemErrnoErrorCode(errno, "fchmod");
        }
    }

    return File(fd);
}

static void volatile* g_this_binary_symbol = nullptr;

ErrorCodeOr<MutableString> CurrentBinaryPath(Allocator& a) {
    Dl_info info;
    if (dladdr(&g_this_binary_symbol, &info) == 0) return FilesystemErrnoErrorCode(errno, "dladdr");
    auto const fname = FromNullTerminated(info.dli_fname);

    MutableString result {};
    if constexpr (IS_MACOS) {
        // macOS seems to always return full paths
        result = a.Clone(fname);
    } else {
        // on Linux, we might not have full paths
        if (path::IsAbsolute(fname)) {
            result = a.Clone(fname);
        } else {
            Array<char, Kb(32)> buffer {};
            auto size = readlink("/proc/self/maps", buffer.data, buffer.size);
            if (size != -1) {
                if (size == buffer.size) {
                    // truncated, we can't know if the path will be correct
                    return ErrorCode {FilesystemError::PathDoesNotExist};
                }

                usize line_index = 0;
                for (auto const line : SplitIterator {String {buffer.data, (usize)size}, '\n'}) {
                    // lines look like this:
                    // 7f6bbc12e000-7f6bbc2bf000 r-xp 00000000 08:02 135522 /usr/lib64/libglib-2.0.so.0.4400.1
                    usize word_index = 0;
                    for (auto const word : SplitIterator {line, ' ', true}) {
                        if (word_index++ == 5) {
                            auto const path = word;
                            if (path::IsAbsolute(path)) {
                                if (path::Equal(path::Filename(path), fname)) {
                                    result = a.Clone(path);
                                    break;
                                }
                            }
                        }
                        ASSERT(word_index < 100);
                    }

                    ++line_index;
                    ASSERT(line_index < 20000);
                }
            } else {
                size = readlink("/proc/self/exe", buffer.data, buffer.size);
                if (size == -1) return FilesystemErrnoErrorCode(errno, "readlink");
                if (size == buffer.size) {
                    // truncated, we can't know if the path will be correct
                    return ErrorCode {FilesystemError::PathDoesNotExist};
                }
                result = a.Clone(String {buffer.data, (usize)size});
            }

            if (result.size == 0) return ErrorCode {FilesystemError::PathDoesNotExist};
        }
    }

    ASSERT(path::IsAbsolute(result));
    ASSERT(IsValidUtf8(result));
    return result;
}
