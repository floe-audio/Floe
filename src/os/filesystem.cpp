// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filesystem.hpp"

#include <cerrno>
#include <errno.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

static constexpr ErrorCodeCategory k_fp_error_category {
    .category_id = "FS",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((FilesystemError)code) {
                case FilesystemError::PathDoesNotExist: return "file or folder does not exist";
                case FilesystemError::TooManyFilesOpen: return "too many files open";
                case FilesystemError::FolderContainsTooManyFiles: return "folder is too large";
                case FilesystemError::AccessDenied: return "access is denied to this file or folder";
                case FilesystemError::PathIsAFile: return "path is a file";
                case FilesystemError::PathIsAsDirectory: return "path is a folder";
                case FilesystemError::PathAlreadyExists: return "path already exists";
                case FilesystemError::FileWatcherCreationFailed: return "file watcher creation failed";
                case FilesystemError::FilesystemBusy: return "filesystem is busy";
                case FilesystemError::DiskFull: return "disk is full";
                case FilesystemError::NotSupported: return "not supported";
                case FilesystemError::DifferentFilesystems: return "paths are on different filesystems";
                case FilesystemError::NotEmpty: return "folder is not empty";
                case FilesystemError::Count: break;
            }
            return "";
        };
        return writer.WriteChars(get_str());
    },
};

ErrorCodeCategory const& ErrorCategoryForEnum(FilesystemError) { return k_fp_error_category; }

static constexpr Optional<FilesystemError> TranslateErrnoCode(s64 ec) {
    switch (ec) {
        case ENOENT: return FilesystemError::PathDoesNotExist;
        case EEXIST: return FilesystemError::PathAlreadyExists;
        case ENFILE: return FilesystemError::TooManyFilesOpen;
        case EROFS: // read-only
        case EACCES:
        case EPERM: {
            // POSIX defines EACCES as "an attempt was made to access a file in a way forbidden by its file
            // access permissions" and EPERM as "an attempt was made to perform an operation limited to
            // processes with appropriate privileges or to the owner of a file or other resource". These are
            // so similar that I think we will just consider them the same.
            return FilesystemError::AccessDenied;
        }
        case EBUSY: return FilesystemError::FilesystemBusy;
#ifdef EDQUOT
        case EDQUOT: return FilesystemError::DiskFull;
#endif
        case ENOSPC: return FilesystemError::DiskFull;
        case EXDEV: return FilesystemError::DifferentFilesystems;
        case ENOTEMPTY: return FilesystemError::NotEmpty;
    }
    return {};
}

ErrorCode FilesystemErrnoErrorCode(s64 error_code, char const* extra_debug_info, SourceLocation loc) {
    if (auto code = TranslateErrnoCode(error_code))
        return ErrorCode {ErrorCategoryForEnum(FilesystemError {}), (s64)code.Value(), extra_debug_info, loc};
    return ErrnoErrorCode(error_code, extra_debug_info, loc);
}

MutableString KnownDirectoryWithSubdirectories(Allocator& a,
                                               KnownDirectoryType type,
                                               Span<String const> subdirectories,
                                               Optional<String> filename,
                                               KnownDirectoryOptions options) {
    auto path = KnownDirectory(a, type, options);
    if (!subdirectories.size && !filename) return path;

    auto const full_path = a.ResizeType(path,
                                        path.size,
                                        path.size + TotalSize(subdirectories) + subdirectories.size +
                                            (filename ? filename->size + 1 : 0));
    usize pos = path.size;
    for (auto const& sub : subdirectories) {
        ASSERT(sub.size);
        ASSERT(IsValidUtf8(sub));

        WriteAndIncrement(pos, full_path, path::k_dir_separator);
        WriteAndIncrement(pos, full_path, sub);

        if (options.create) {
            auto const dir = String {full_path.data, pos};
            auto const o = CreateDirectory(dir,
                                           {
                                               .create_intermediate_directories = false,
                                               .fail_if_exists = false,
                                               .win32_hide_dirs_starting_with_dot = true,
                                           });
            if (o.HasError() && options.error_log) {
                auto _ = fmt::FormatToWriter(*options.error_log,
                                             "Failed to create directory '{}': {}\n",
                                             dir,
                                             o.Error());
            }
        }
    }
    if (filename) {
        WriteAndIncrement(pos, full_path, path::k_dir_separator);
        WriteAndIncrement(pos, full_path, *filename);
    }

    ASSERT(path::IsAbsolute(full_path));
    ASSERT(IsValidUtf8(full_path));
    return full_path;
}

MutableString FloeKnownDirectory(Allocator& a,
                                 FloeKnownDirectoryType type,
                                 Optional<String> filename,
                                 KnownDirectoryOptions options) {
    KnownDirectoryType known_dir_type {};
    Span<String const> subdirectories {};
    switch (type) {
        case FloeKnownDirectoryType::Logs: {
            known_dir_type = KnownDirectoryType::Logs;
#if IS_MACOS
            // On macOS, the folder is ~/Library/Logs
            static constexpr auto k_dirs = Array {"Floe"_s};
#else
            static constexpr auto k_dirs = Array {"Floe"_s, "Logs"};
#endif
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::Preferences: {
            known_dir_type = KnownDirectoryType::GlobalData;
            static constexpr auto k_dirs = Array {"Floe"_s, "Preferences"};
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::Presets: {
            known_dir_type = KnownDirectoryType::GlobalData;
            static constexpr auto k_dirs = Array {"Floe"_s, "Presets"};
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::Libraries: {
            known_dir_type = KnownDirectoryType::GlobalData;
            static constexpr auto k_dirs = Array {"Floe"_s, "Libraries"};
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::Autosaves: {
            known_dir_type = KnownDirectoryType::GlobalData;
            static constexpr auto k_dirs = Array {"Floe"_s, "Autosaves"};
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::MirageDefaultLibraries: {
            known_dir_type = KnownDirectoryType::MirageGlobalData;
            static constexpr auto k_dirs = Array {"FrozenPlain"_s, "Mirage", "Libraries"};
            subdirectories = k_dirs;
            break;
        }
        case FloeKnownDirectoryType::MirageDefaultPresets: {
            known_dir_type = KnownDirectoryType::MirageGlobalData;
            static constexpr auto k_dirs = Array {"FrozenPlain"_s, "Mirage", "Presets"};
            subdirectories = k_dirs;
            break;
        }
    }
    return KnownDirectoryWithSubdirectories(a, known_dir_type, subdirectories, filename, options);
}

static String g_log_folder_path;
static CallOnceFlag g_log_folder_flag {};

void InitLogFolderIfNeeded() {
    static FixedSizeAllocator<500> g_log_folder_allocator {&PageAllocator::Instance()};
    CallOnce(g_log_folder_flag, [] {
        auto writer = StdWriter(StdStream::Err);
        g_log_folder_path = FloeKnownDirectory(g_log_folder_allocator,
                                               FloeKnownDirectoryType::Logs,
                                               k_nullopt,
                                               {.create = true, .error_log = &writer});
    });
}

Optional<String> LogFolder() {
    if (!g_log_folder_flag.Called()) return k_nullopt;
    ASSERT(g_log_folder_path.size);
    ASSERT(IsValidUtf8(g_log_folder_path));
    return g_log_folder_path;
}

String PreferencesFilepath(String* error_log) {
    static DynamicArrayBounded<char, 200> error_log_buffer;
    static String path = []() {
        static FixedSizeAllocator<500> allocator {&PageAllocator::Instance()};
        auto writer = dyn::WriterFor(error_log_buffer);
        return FloeKnownDirectory(allocator,
                                  FloeKnownDirectoryType::Preferences,
                                  "floe.ini"_s,
                                  {.create = true, .error_log = &writer});
    }();
    if (error_log) *error_log = error_log_buffer;
    return path;
}

ErrorCodeOr<MutableString>
TemporaryDirectoryWithinFolder(String existing_abs_folder, Allocator& a, u64& seed) {
    auto result =
        path::Join(a, Array {existing_abs_folder, UniqueFilename(k_temporary_directory_prefix, "", seed)});
    TRY(CreateDirectory(result,
                        {
                            .create_intermediate_directories = false,
                            .fail_if_exists = true,
                            .win32_hide_dirs_starting_with_dot = true,
                        }));
    return result;
}

// uses Rename() to move a file or folder into a given destination folder
ErrorCodeOr<void> MoveIntoFolder(String from, String destination_folder) {
    PathArena path_allocator {Malloc::Instance()};
    auto new_name = path::Join(path_allocator, Array {destination_folder, path::Filename(from)});
    return Rename(from, new_name);
}

ErrorCodeOr<Span<dir_iterator::Entry>>
FindEntriesInFolder(ArenaAllocator& a, String folder, FindEntriesInFolderOptions options) {
    DynamicArray<dir_iterator::Entry> result {a};

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {Malloc::Instance()};

    auto iterate = [&](auto create_function) -> ErrorCodeOr<void> {
        auto it = TRY(create_function(scratch_arena, folder, options.options));
        DEFER { dir_iterator::Destroy(it); };
        while (auto const entry = TRY(dir_iterator::Next(it, a)))
            if (!options.only_file_type || *options.only_file_type == entry->type)
                dyn::Append(result, *entry);
        return k_success;
    };

    if (options.recursive)
        TRY(iterate(dir_iterator::RecursiveCreate));
    else
        TRY(iterate(dir_iterator::Create));

    return result.ToOwnedSpan();
}

namespace dir_iterator {

static ErrorCodeOr<Iterator> CreateSubIterator(ArenaAllocator& a, String path, Options options) {
    // We do not pass the wildcard into the sub iterators because we need to get the folders, not just paths
    // that match the pattern.
    options.wildcard = "*";
    return Create(a, path, options);
}

ErrorCodeOr<RecursiveIterator> RecursiveCreate(ArenaAllocator& a, String path, Options options) {
    auto it = TRY(CreateSubIterator(a, path, options));
    RecursiveIterator result {
        .arena = a,
        .stack = {},
        .dir_path_to_iterate = {a},
        .base_path = a.Clone(it.base_path),
        .options = options,
    };
    result.stack.Prepend(result.arena, it);
    result.options.wildcard = a.Clone(options.wildcard);
    result.dir_path_to_iterate.Reserve(240);
    return result;
}

void Destroy(RecursiveIterator& it) {
    for (auto& i : it.stack)
        Destroy(i);
}

ErrorCodeOr<Optional<Entry>> Next(RecursiveIterator& it, ArenaAllocator& result_arena) {
    do {
        if (it.dir_path_to_iterate.size) {
            it.stack.Prepend(it.arena,
                             TRY(CreateSubIterator(result_arena, it.dir_path_to_iterate, it.options)));
            dyn::Clear(it.dir_path_to_iterate);
        }

        while (!it.stack.Empty()) {
            // Break to outer loop because we need to add another iterator to the stack. If we don't break, we
            // might overwrite dir_path_to_iterate (since we just use a single string rather than a queue).
            if (it.dir_path_to_iterate.size) break;

            auto& first = *it.stack.begin();

            auto entry_outcome = Next(first, result_arena);
            if (entry_outcome.HasValue()) {
                auto& opt_entry = entry_outcome.Value();
                if (opt_entry) {
                    auto& entry = *opt_entry;

                    // If it's a directory we will queue it up to be iterated next time. We don't do this here
                    // because if creating the subiterator fails, we have lost this current entry.
                    if (entry.type == FileType::Directory) {
                        dyn::Assign(it.dir_path_to_iterate, first.base_path);
                        ASSERT(!EndsWith(it.dir_path_to_iterate, path::k_dir_separator));
                        dyn::Append(it.dir_path_to_iterate, path::k_dir_separator);
                        dyn::AppendSpan(it.dir_path_to_iterate, entry.subpath);
                    }

                    if (!MatchWildcard(it.options.wildcard, path::Filename(entry.subpath)) ||
                        (it.options.skip_dot_files && entry.subpath.size && entry.subpath[0] == '.')) {
                        continue;
                    }

                    // Each entry's subpath is relative to the base path of the iterator that created it. We
                    // need convert the subpath relative from each iterator to the base path of this recursive
                    // iterator.
                    if (auto subiterator_path_delta = first.base_path.SubSpan(it.base_path.size);
                        subiterator_path_delta.size) {
                        subiterator_path_delta.RemovePrefix(1); // remove the '/'

                        auto subpath = result_arena.AllocateExactSizeUninitialised<char>(
                            subiterator_path_delta.size + 1 + entry.subpath.size);
                        usize write_pos = 0;
                        WriteAndIncrement(write_pos, subpath, subiterator_path_delta);
                        WriteAndIncrement(write_pos, subpath, path::k_dir_separator);
                        WriteAndIncrement(write_pos, subpath, entry.subpath);
                        entry.subpath = subpath;
                    }

                    return entry;
                } else {
                    ASSERT_EQ(first.reached_end, true);
                    Destroy(first);
                    it.stack.RemoveFirst();
                    continue;
                }
            } else {
                Destroy(first);
                it.stack.RemoveFirst();
                return entry_outcome.Error();
            }
        }
    } while (it.dir_path_to_iterate.size);

    ASSERT(it.stack.Empty());
    return k_nullopt;
}

} // namespace dir_iterator

#ifndef __APPLE__
ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String) { return false; }
#endif

File::~File() {
    CloseFile();
    handle = File::k_invalid_file_handle;
}

ErrorCodeOr<usize> WriteFile(String filename, Span<u8 const> data) {
    auto file = OpenFile(filename, FileMode::Write());
    if (file.HasError()) return file.Error();
    return file.Value().Write(data);
}

ErrorCodeOr<usize> AppendFile(String filename, Span<u8 const> data) {
    auto file = OpenFile(filename, FileMode::Append());
    if (file.HasError()) return file.Error();
    return file.Value().Write(data);
}

ErrorCodeOr<MutableString> ReadEntireFile(String filename, Allocator& a) {
    auto file = TRY(OpenFile(filename, FileMode::Read()));
    return file.ReadWholeFile(a);
}

ErrorCodeOr<MutableString> ReadSectionOfFile(String filename,
                                             usize const bytes_offset_from_file_start,
                                             usize const size_in_bytes,
                                             Allocator& a) {
    auto file = TRY(OpenFile(filename, FileMode::Read()));
    return file.ReadSectionOfFile(bytes_offset_from_file_start, size_in_bytes, a);
}

ErrorCodeOr<u64> FileSize(String filename) { return TRY(OpenFile(filename, FileMode::Read())).FileSize(); }

ErrorCodeOr<s128> LastModifiedTimeNsSinceEpoch(String filename) {
    return TRY(OpenFile(filename, FileMode::Read())).LastModifiedTimeNsSinceEpoch();
}

ErrorCodeOr<void> SetLastModifiedTimeNsSinceEpoch(String filename, s128 time) {
    return TRY(OpenFile(filename,
                        {
                            .capability = FileMode::Capability::Write,
                            .win32_share = FileMode::Share::ReadWrite | FileMode::Share::DeleteRename,
                            .creation = FileMode::Creation::OpenExisting,
                        }))
        .SetLastModifiedTimeNsSinceEpoch(time);
}

ErrorCodeOr<MutableString>
File::ReadSectionOfFile(usize const bytes_offset_from_file_start, usize const size_in_bytes, Allocator& a) {
    TRY(Seek((s64)bytes_offset_from_file_start, SeekOrigin::Start));
    auto result = a.AllocateExactSizeUninitialised<u8>(size_in_bytes);
    auto const num_read = TRY(Read(result.data, size_in_bytes));
    if (num_read != size_in_bytes)
        result = a.Resize({.allocation = result.ToByteSpan(), .new_size = num_read});
    return MutableString {(char*)result.data, result.size};
}

ErrorCodeOr<MutableString> File::ReadWholeFile(Allocator& a) {
    auto const file_size = TRY(FileSize());
    return ReadSectionOfFile(0, (usize)file_size, a);
}

ErrorCodeOr<void> ReadSectionOfFileAndWriteToOtherFile(File& file_to_read_from,
                                                       usize const section_start,
                                                       usize const section_size,
                                                       String const filename_to_write_to) {
    ASSERT(section_size);

    auto out_file = TRY(OpenFile(filename_to_write_to, FileMode::Write()));
    TRY(file_to_read_from.Seek((s64)section_start, File::SeekOrigin::Start));

    constexpr usize k_four_mb = Mb(4);
    usize const buffer_size = Min(section_size, k_four_mb);
    auto const buffer = PageAllocator::Instance().AllocateBytesForTypeOversizeAllowed<u8>(buffer_size);
    DEFER { PageAllocator::Instance().Free(buffer); };
    usize size_remaining = section_size;
    while (size_remaining != 0) {
        usize const chunk = Min(size_remaining, k_four_mb);
        auto const buffer_span = Span<u8> {buffer.data, chunk};
        TRY(file_to_read_from.Read(buffer_span.data, buffer_span.size));
        TRY(out_file.Write(buffer_span));
        size_remaining -= chunk;
    }
    return k_success;
}

Optional<String>
SearchForExistingFolderUpwards(String dir, String folder_name_to_find, Allocator& allocator) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {Malloc::Instance()};
    DynamicArray<char> buf {dir, scratch_arena};
    dyn::AppendSpan(buf, "/.");

    constexpr usize k_max_folder_heirarchy = 20;
    for (auto _ : Range(k_max_folder_heirarchy)) {
        auto const opt_dir = path::Directory(dir);
        if (!opt_dir.HasValue()) break;
        ASSERT(dir.size != opt_dir->size);
        dir = *opt_dir;

        dyn::Resize(buf, dir.size);
        path::JoinAppend(buf, folder_name_to_find);
        if (auto const o = GetFileType(buf); o.HasValue() && o.Value() == FileType::Directory)
            return Optional<String> {allocator.Clone(buf)};
    }

    return k_nullopt;
}
