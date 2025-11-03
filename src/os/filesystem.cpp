// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filesystem.hpp"

#include <cerrno>
#include <errno.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"

static constexpr ErrorCodeCategory k_fp_error_category {
    .category_id = "FS",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((FilesystemError)code) {
                case FilesystemError::PathDoesNotExist: return "file or folder does not exist";
                case FilesystemError::TooManyFilesOpen: return "too many files open";
                case FilesystemError::FolderContainsTooManyFiles: return "folder is too large";
                case FilesystemError::AccessDenied: return "access is denied to this file or folder";
                case FilesystemError::UsedByAnotherProcess:
                    return "file or folder is used by another process";
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
                        path::JoinAppend(it.dir_path_to_iterate, entry.subpath);
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

TEST_CASE(TestDirectoryWatcher) {
    auto& a = tester.scratch_arena;

    for (auto const recursive : Array {true, false}) {
        CAPTURE(recursive);

        auto const dir = (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-test"});
        auto _ =
            Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false, .fail_if_exists = true}));

        struct TestPath {
            static TestPath Create(ArenaAllocator& a, String root_dir, String subpath) {
                auto const full = path::Join(a, Array {root_dir, subpath});
                return TestPath {
                    .full_path = full,
                    .subpath = full.SubSpan(full.size - subpath.size, subpath.size),
                };
            }
            String full_path;
            String subpath;
        };

        auto const file = TestPath::Create(a, dir, "file1.txt");
        TRY(WriteFile(file.full_path, "data"));

        auto const subdir = TestPath::Create(a, dir, "subdir");
        TRY(CreateDirectory(subdir.full_path,
                            {.create_intermediate_directories = false, .fail_if_exists = true}));

        auto const subfile = TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2.txt"}));
        TRY(WriteFile(subfile.full_path, "data"));

        auto watcher = TRY(CreateDirectoryWatcher(a));
        DEFER { DestoryDirectoryWatcher(watcher); };

        auto const dirs_to_watch = Array {DirectoryToWatch {
            .path = dir,
            .recursive = recursive,
        }};
        auto const args = PollDirectoryChangesArgs {
            .dirs_to_watch = dirs_to_watch,
            .retry_failed_directories = false,
            .result_arena = a,
            .scratch_arena = a,
        };

        if (auto const dir_changes_span = TRY(PollDirectoryChanges(watcher, args)); dir_changes_span.size) {
            // macOS FSEvents may report file creation operations as changes to the watcher, even when they
            // occurred during test setup before monitoring began.
            tester.log.Debug("Unexpected result");
            for (auto const& dir_changes : dir_changes_span) {
                tester.log.Debug("  {}", dir_changes.linked_dir_to_watch->path);
                tester.log.Debug("  {}", dir_changes.error);
                for (auto const& subpath_changeset : dir_changes.subpath_changesets)
                    tester.log.Debug("    {} {}",
                                     subpath_changeset.subpath,
                                     DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes));
            }
            if constexpr (!IS_MACOS) REQUIRE(false);
        }

        auto check = [&](Span<DirectoryWatcher::DirectoryChanges::Change const> expected_changes)
            -> ErrorCodeOr<void> {
            auto found_expected = a.NewMultiple<bool>(expected_changes.size);

            // we give the watcher some time and a few attempts to detect the changes
            for (auto const _ : Range(100)) {
                SleepThisThread(2);
                auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));

                for (auto const& directory_changes : directory_changes_span) {
                    auto const& path = directory_changes.linked_dir_to_watch->path;

                    CHECK(path::Equal(path, dir));
                    if (directory_changes.error) {
                        tester.log.Debug("Error in {}: {}", path, *directory_changes.error);
                        continue;
                    }
                    CHECK(!directory_changes.error.HasValue());

                    for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                        if (subpath_changeset.changes & DirectoryWatcher::ChangeType::ManualRescanNeeded) {
                            tester.log.Error("Manual rescan needed for {}", path);
                            continue;
                        }

                        bool was_expected = false;
                        for (auto const [index, expected] : Enumerate(expected_changes)) {
                            if (path::Equal(subpath_changeset.subpath, expected.subpath) &&
                                (!subpath_changeset.file_type.HasValue() ||
                                 subpath_changeset.file_type.Value() == expected.file_type)) {
                                if (expected.changes & subpath_changeset.changes) {
                                    was_expected = true;
                                    found_expected[index] = true;
                                    break;
                                }
                            }
                        }

                        tester.log.Debug("{} change: \"{}\" {{ {} }} in \"{}\"",
                                         was_expected ? "Expected" : "Unexpected",
                                         subpath_changeset.subpath,
                                         DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes),
                                         path);
                    }
                }

                if (ContainsOnly(found_expected, true)) break;
            }

            for (auto const [index, expected] : Enumerate(expected_changes)) {
                CAPTURE(expected.subpath);
                CAPTURE(DirectoryWatcher::ChangeType::ToString(expected.changes));
                if (!found_expected[index]) {
                    tester.log.Debug("Expected change not found: {} {}",
                                     expected.subpath,
                                     DirectoryWatcher::ChangeType::ToString(expected.changes));
                }
                CHECK(found_expected[index]);
            }

            return k_success;
        };

        SUBCASE(recursive ? "recursive"_s : "non-recursive"_s) {
            SUBCASE("delete is detected") {
                TRY(Delete(file.full_path, {}));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Deleted,
                }}));
            }

            SUBCASE("modify is detected") {
                TRY(WriteFile(file.full_path, "new data"));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Modified,
                }}));
            }

            SUBCASE("rename is detected") {
                auto const new_file = TestPath::Create(a, dir, "file1_renamed.txt");
                TRY(Rename(file.full_path, new_file.full_path));
                TRY(check(Array {
                    DirectoryWatcher::DirectoryChanges::Change {
                        file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedOldName,
                    },
                    DirectoryWatcher::DirectoryChanges::Change {
                        new_file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedNewName,
                    },
                }));
            }

            // On Windows, the root folder does not receive events
            if constexpr (!IS_WINDOWS) {
                SUBCASE("deleting root is detected") {
                    auto const delete_outcome =
                        Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                    if (!delete_outcome.HasError()) {
                        auto args2 = args;
                        bool found_delete_self = false;
                        for (auto const _ : Range(4)) {
                            SleepThisThread(5);
                            auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args2));
                            for (auto const& directory_changes : directory_changes_span) {
                                for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                                    if (subpath_changeset.subpath.size == 0 &&
                                        subpath_changeset.changes & DirectoryWatcher::ChangeType::Deleted) {
                                        CHECK(subpath_changeset.file_type == FileType::Directory);
                                        found_delete_self = true;
                                        args2.dirs_to_watch = {};
                                        break;
                                    }
                                }
                            }
                            if (found_delete_self) break;
                        }
                        CHECK(found_delete_self);
                    } else {
                        tester.log.Debug(
                            "Failed to delete root watched dir: {}. This is probably normal behaviour",
                            delete_outcome.Error());
                    }
                }
            }

            SUBCASE("no crash moving root dir") {
                auto const dir_name = fmt::Format(a, "{}-moved", dir);
                auto const move_outcome = Rename(dir, dir_name);
                if (!move_outcome.HasError()) {
                    DEFER { auto _ = Delete(dir_name, {.type = DeleteOptions::Type::DirectoryRecursively}); };
                    // On Linux, we don't get any events. Perhaps a MOVE only triggers when the underlying
                    // file object really moves and perhaps a rename like this doesn't do that. Either way I
                    // think we just need to check nothing bad happens in this case and that will do.
                } else {
                    tester.log.Debug("Failed to move root watched dir: {}. This is probably normal behaviour",
                                     move_outcome.Error());
                }
            }

            // Wine seems to have trouble with recursive watching
            static auto const recursive_supported = !IsRunningUnderWine();

            if (recursive && recursive_supported) {
                SUBCASE("delete in subfolder is detected") {
                    TRY(Delete(subfile.full_path, {}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("modify is detected") {
                    TRY(WriteFile(subfile.full_path, "new data"));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Modified,
                    }}));
                }

                SUBCASE("rename is detected") {
                    auto const new_subfile =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2_renamed.txt"}));
                    TRY(Rename(subfile.full_path, new_subfile.full_path));
                    TRY(check(Array {
                        DirectoryWatcher::DirectoryChanges::Change {
                            subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedOldName,
                        },
                        DirectoryWatcher::DirectoryChanges::Change {
                            new_subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedNewName,
                        },
                    }));
                }

                SUBCASE("deleting subfolder is detected") {
                    TRY(Delete(subdir.full_path, {.type = DeleteOptions::Type::DirectoryRecursively}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subdir.subpath,
                        FileType::Directory,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("newly created subfolder is watched") {
                    // create a new subdir
                    auto const subdir2 = TestPath::Create(a, dir, "subdir2");
                    TRY(CreateDirectory(subdir2.full_path,
                                        {.create_intermediate_directories = false, .fail_if_exists = true}));

                    // create a file within it
                    auto const subfile2 =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir2.subpath, "file2.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        // Windows doesn't seem to give us the subdir2 'added' event
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir2.subpath,
                                FileType::Directory,
                                DirectoryWatcher::ChangeType::Added,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }

                SUBCASE("moved subfolder is still watched") {
                    auto const subdir_moved = TestPath::Create(a, dir, "subdir-moved");
                    TRY(Rename(subdir.full_path, subdir_moved.full_path));

                    auto const subfile2 =
                        TestPath::Create(a,
                                         dir,
                                         path::Join(a, Array {subdir_moved.subpath, "file-in-moved.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedOldName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir_moved.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedNewName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }
            } else {
                SUBCASE("delete in subfolder is not detected") {
                    TRY(Delete(subfile.full_path, {}));

                    for (auto const _ : Range(2)) {
                        SleepThisThread(2);
                        auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));
                        for (auto const& directory_changes : directory_changes_span)
                            for (auto const& subpath_changeset : directory_changes.subpath_changesets)
                                CHECK(!path::Equal(subpath_changeset.subpath, subfile.subpath));
                    }
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestDirectoryWatcherErrors) {
    auto& a = tester.scratch_arena;

    auto const dir =
        (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-errors-test"});

    auto watcher = TRY(CreateDirectoryWatcher(a));
    DEFER { DestoryDirectoryWatcher(watcher); };

    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        // we're not expecting a top-level error, that should only be for if the whole watching system fails
        REQUIRE(outcome.HasValue());

        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    // retrying should not repeat the error unless retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        CHECK(outcome.Value().size == 0);
    }

    // the error should repeat if retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = true,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    return k_success;
}

TEST_CASE(TestFileApi) {
    auto& scratch_arena = tester.scratch_arena;
    auto const filename1 = path::Join(scratch_arena, Array {tests::TempFolder(tester), "filename1"});
    auto const filename2 = path::Join(scratch_arena, Array {tests::TempFolder(tester), "filename2"});
    DEFER { auto _ = Delete(filename1, {}); };
    DEFER { auto _ = Delete(filename2, {}); };
    constexpr auto k_data = "data"_s;

    SUBCASE("Write and read") {
        TRY(CreateDirectory(tests::TempFolder(tester), {.create_intermediate_directories = true}));

        SUBCASE("Open API") {
            {
                auto f = TRY(OpenFile(filename1, FileMode::Write()));
                CHECK(f.Write(k_data.ToByteSpan()).HasValue());
            }
            {
                auto f = TRY(OpenFile(filename1, FileMode::Read()));
                CHECK_EQ(TRY(f.FileSize()), k_data.size);
                CHECK_EQ(TRY(f.ReadWholeFile(scratch_arena)), k_data);
            }
        }
        SUBCASE("read-all API") {
            TRY(WriteFile(filename1, k_data.ToByteSpan()));
            CHECK_EQ(TRY(ReadEntireFile(filename1, scratch_arena)), k_data);
        }
    }

    SUBCASE("Seek") {
        TRY(WriteFile(filename1, k_data.ToByteSpan()));
        auto f = TRY(OpenFile(filename1, FileMode::Read()));
        TRY(f.Seek(2, File::SeekOrigin::Start));
        char buffer[2];
        CHECK_EQ(TRY(f.Read(buffer, 2)), 2u);
        CHECK_EQ(String(buffer, 2), k_data.SubSpan(2));
    }

    SUBCASE("Lock a file") {
        for (auto const type : Array {FileLockOptions::Type::Exclusive, FileLockOptions::Type::Shared}) {
            for (auto const non_blocking : Array {true, false}) {
                auto f = TRY(OpenFile(filename1, FileMode::Write()));
                auto locked = TRY(f.Lock({.type = type, .non_blocking = non_blocking}));
                CHECK(locked);
                if (locked) TRY(f.Unlock());
            }
        }
    }

    SUBCASE("Move a File object") {
        auto f = OpenFile(filename1, FileMode::Read());
        auto f2 = Move(f);
    }

    SUBCASE("Read from one large file and write to another") {
        auto buffer = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(Mb(8));
        {
            auto f = TRY(OpenFile(filename1, FileMode::Write()));
            FillMemory(buffer, 'a');
            TRY(f.Write(buffer));
            FillMemory(buffer, 'b');
            TRY(f.Write(buffer));
        }

        {
            auto f = TRY(OpenFile(filename1, FileMode::Read()));

            {
                TRY(ReadSectionOfFileAndWriteToOtherFile(f, 0, buffer.size, filename2));
                auto f2 = TRY(ReadEntireFile(filename2, tester.scratch_arena));
                FillMemory(buffer, 'a');
                CHECK(f2.ToByteSpan() == buffer);
            }

            {
                TRY(ReadSectionOfFileAndWriteToOtherFile(f, buffer.size, 8, filename2));
                auto f2 = TRY(ReadEntireFile(filename2, tester.scratch_arena));
                FillMemory({buffer.data, 8}, 'b');
                CHECK(f2.ToByteSpan() == Span<u8> {buffer.data, 8});
            }
        }
    }

    SUBCASE("Last modified time") {
        auto time = NanosecondsSinceEpoch();
        {
            auto f = TRY(OpenFile(filename1, FileMode::Write()));
            TRY(f.Write(k_data.ToByteSpan()));
            TRY(f.Flush());
            TRY(f.SetLastModifiedTimeNsSinceEpoch(time));
        }
        {
            auto f = TRY(OpenFile(filename1, FileMode::Read()));
            auto last_modified = TRY(f.LastModifiedTimeNsSinceEpoch());
            CHECK_EQ(last_modified, time);
        }
    }

    SUBCASE("Try opening a file that does not exist") {
        auto const f = OpenFile("foo", FileMode::Read());
        REQUIRE(f.HasError());
    }

    SUBCASE("Try reading an entire file that does not exist") {
        auto const data = ReadEntireFile("foo", tester.scratch_arena);
        REQUIRE(data.HasError());
    }
    return k_success;
}

TEST_CASE(TestFilesystemApi) {
    auto& a = tester.scratch_arena;

    SUBCASE("DirectoryIteratorV2") {
        auto dir = String(path::Join(a, Array {tests::TempFolder(tester), "DirectoryIteratorV2 test"}));
        auto _ = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));
        DEFER {
            if (auto o = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively}); o.HasError())
                LOG_WARNING("failed to delete temp dir: {}", o.Error());
        };

        SUBCASE("empty dir") {
            SUBCASE("non-recursive") {
                auto it = REQUIRE_UNWRAP(dir_iterator::Create(a, dir, {}));
                DEFER { dir_iterator::Destroy(it); };
                auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                CHECK(!opt_entry.HasValue());
            }
            SUBCASE("recursive") {
                auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a, dir, {}));
                DEFER { dir_iterator::Destroy(it); };
                auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                CHECK(!opt_entry.HasValue());
            }
        }

        SUBCASE("dir with files") {
            auto const file1 = path::Join(a, Array {dir, "file1.txt"_s});
            auto const file2 = path::Join(a, Array {dir, "file2.txt"_s});
            auto const file3 = path::Join(a, Array {dir, ".file3.wav"_s});
            auto const subdir1 = String(path::Join(a, Array {dir, "subdir1"_s}));
            auto const subdir1_file1 = path::Join(a, Array {subdir1, "subdir1_file1.txt"_s});
            auto const subdir2 = String(path::Join(a, Array {dir, "subdir2"_s}));
            auto const subdir2_file1 = path::Join(a, Array {subdir2, "subdir2_file1.txt"_s});
            auto const subdir2_subdir = String(path::Join(a, Array {subdir2, "subdir2_subdir"_s}));

            TRY(CreateDirectory(subdir1, {.create_intermediate_directories = false}));
            TRY(CreateDirectory(subdir2, {.create_intermediate_directories = false}));
            TRY(CreateDirectory(subdir2_subdir, {.create_intermediate_directories = false}));

            TRY(WriteFile(file1, "data"_s.ToByteSpan()));
            TRY(WriteFile(file2, "data"_s.ToByteSpan()));
            TRY(WriteFile(file3, "data"_s.ToByteSpan()));
            TRY(WriteFile(subdir1_file1, "data"_s.ToByteSpan()));
            TRY(WriteFile(subdir2_file1, "data"_s.ToByteSpan()));

            auto contains = [](Span<dir_iterator::Entry const> entries, dir_iterator::Entry entry) {
                for (auto const& e : entries)
                    if (e.subpath == entry.subpath && e.type == entry.type) return true;
                return false;
            };
            DynamicArrayBounded<dir_iterator::Entry, 10> entries;

            SUBCASE("non-recursive") {
                SUBCASE("standard options") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 5u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone(".file3.wav"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                }

                SUBCASE("skip dot files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = true,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 4u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                }

                SUBCASE("only .txt files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*.txt",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 2u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                }

                SUBCASE("get file size") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = true,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };
                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        if (opt_entry->type == FileType::File) CHECK_EQ(opt_entry->file_size, 4u);
                }

                SUBCASE("no files matching pattern") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "sef9823ksdjf39s*",
                                                                      .get_file_size = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };
                    auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                    CHECK(!opt_entry.HasValue());
                }

                SUBCASE("non existent dir") {
                    auto it = dir_iterator::Create(a,
                                                   IS_WINDOWS ? "C:/seflskflks"_s : "/aoidlkdsf"_s,
                                                   {
                                                       .wildcard = "*",
                                                       .get_file_size = false,
                                                   });
                    // Create is allow to succeed even if the path does not exist.
                    if (it.HasValue()) {
                        auto const next = dir_iterator::Next(it.Value(), a);
                        CHECK(next.HasError() && next.Error() == FilesystemError::PathDoesNotExist);
                    } else {
                        CHECK(it.Error() == FilesystemError::PathDoesNotExist);
                    }
                }
            }

            SUBCASE("recursive") {
                SUBCASE("standard options") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = false,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 8u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone(".file3.wav"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_subdir"_s}),
                                    .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }

                SUBCASE("skip dot files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = true,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 7u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_subdir"_s}),
                                    .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }

                SUBCASE("only .txt files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*.txt",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = false,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 4u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }
            }
        }
    }

    SUBCASE("Absolute") {
        auto check = [&](String str, bool expecting_success) {
            CAPTURE(str);
            CAPTURE(expecting_success);
            auto o = AbsolutePath(a, str);
            if (!expecting_success) {
                REQUIRE(o.HasError());
                return;
            }
            if (o.HasError()) {
                LOG_WARNING("Failed to AbsolutePath: {}", o.Error());
                return;
            }
            REQUIRE(o.HasValue());
            tester.log.Debug(o.Value());
            REQUIRE(path::IsAbsolute(o.Value()));
        };

        check("foo", true);
        check("something/foo.bar", true);
        check("/something/foo.bar", true);
    }

    SUBCASE("KnownDirectory") {
        auto error_writer = StdWriter(StdStream::Err);
        for (auto const i : Range(ToInt(KnownDirectoryType::Count))) {
            auto type = (KnownDirectoryType)i;
            auto known_folder = KnownDirectory(a, type, {.create = false, .error_log = &error_writer});
            String type_name = EnumToString(type);
            tester.log.Debug("Found {} dir: {} ", type_name, known_folder);
            CHECK(path::IsAbsolute(known_folder));
        }
    }

    SUBCASE("TemporaryDirectoryOnSameFilesystemAs") {
        auto const abs_path = KnownDirectory(tester.arena, KnownDirectoryType::GlobalData, {.create = true});
        auto temp_dir = TRY(TemporaryDirectoryOnSameFilesystemAs(abs_path, a));
        tester.log.Debug("Temporary directory on same filesystem: {}", temp_dir);
        CHECK(path::IsAbsolute(temp_dir));
        CHECK(GetFileType(temp_dir).HasValue());
    }

    SUBCASE("DeleteDirectory") {
        auto test_delete_directory = [&a, &tester]() -> ErrorCodeOr<void> {
            auto const dir = path::Join(a, Array {tests::TempFolder(tester), "DeleteDirectory test"});
            TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));

            // create files and folders within the dir
            {
                DynamicArray<char> file = {dir, a};
                path::JoinAppend(file, "test_file1.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "test_file2.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "folder"_s);
                TRY(CreateDirectory(file));
            }

            TRY(Delete(dir, {}));
            CHECK(GetFileType(dir).HasError());
            return k_success;
        };

        if (auto o = test_delete_directory(); o.HasError())
            LOG_WARNING("Failed to test DeleteDirectory: {}", o.Error());
    }

    SUBCASE("CreateDirectory") {
        auto const dir = path::Join(a, Array {tests::TempFolder(tester), "CreateDirectory test"});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false}));
        CHECK(TRY(GetFileType(dir)) == FileType::Directory);
        TRY(Delete(dir, {}));
    }

    SUBCASE("relocate files") {
        auto const dir = String(path::Join(a, Array {tests::TempFolder(tester), "Relocate files test"}));
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false}));
        DEFER { auto _ = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

        auto const path1 = path::Join(a, Array {dir, "test-path1"});
        auto const path2 = path::Join(a, Array {dir, "test-path2"});

        SUBCASE("Rename") {
            SUBCASE("basic file rename") {
                TRY(WriteFile(path1, "data"_s.ToByteSpan()));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::File);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("file rename replaces existing") {
                TRY(WriteFile(path1, "data1"_s.ToByteSpan()));
                TRY(WriteFile(path2, "data2"_s.ToByteSpan()));
                TRY(Rename(path1, path2));
                CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("move dir") {
                TRY(CreateDirectory(path1, {.create_intermediate_directories = false}));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::Directory);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("move dir ok if new_name exists but is empty") {
                TRY(CreateDirectory(path1, {.create_intermediate_directories = false}));
                TRY(CreateDirectory(path2, {.create_intermediate_directories = false}));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::Directory);
                CHECK(GetFileType(path1).HasError());
            }
        }

        SUBCASE("CopyFile") {
            SUBCASE("basic file copy") {
                TRY(WriteFile(path1, "data"_s.ToByteSpan()));
                TRY(CopyFile(path1, path2, ExistingDestinationHandling::Fail));
            }

            SUBCASE("ExistingDesinationHandling") {
                TRY(WriteFile(path1, "data1"_s.ToByteSpan()));
                TRY(WriteFile(path2, "data2"_s.ToByteSpan()));

                SUBCASE("ExistingDestinationHandling::Fail works") {
                    auto const o = CopyFile(path1, path2, ExistingDestinationHandling::Fail);
                    REQUIRE(o.HasError());
                    CHECK(o.Error() == FilesystemError::PathAlreadyExists);
                }

                SUBCASE("ExistingDestinationHandling::Overwrite works") {
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Overwrite));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                }

                SUBCASE("ExistingDestinationHandling::Skip works") {
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Skip));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data2"_s);
                }

                SUBCASE("Overwrite a hidden file") {
                    TRY(WindowsSetFileAttributes(path2, WindowsFileAttributes {.hidden = true}));
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Overwrite));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                }
            }
        }
    }

    SUBCASE("Trash") {
        SUBCASE("file") {
            auto const filename = tests::TempFilename(tester);
            TRY(WriteFile(filename, "data"_s));
            auto const o = TrashFileOrDirectory(filename, tester.scratch_arena);
            if (o.HasError() && o.Error() == FilesystemError::NotSupported) {
                tester.log.Info("Trash not supported on this platform, skipping test");
                return k_success;
            }
            auto const trashed_file = o.Value();
            tester.log.Debug("File in trash: {}", trashed_file);
            CHECK(GetFileType(filename).HasError());
        }

        SUBCASE("folder") {
            auto const folder = tests::TempFilename(tester);
            TRY(CreateDirectory(folder, {.create_intermediate_directories = false}));
            auto const subfile = path::Join(tester.scratch_arena, Array {folder, "subfile.txt"});
            TRY(WriteFile(subfile, "data"_s));
            auto const o = TrashFileOrDirectory(folder, tester.scratch_arena);
            if (o.HasError() && o.Error() == FilesystemError::NotSupported) {
                tester.log.Info("Trash not supported on this platform, skipping test");
                return k_success;
            }
            auto const trashed_folder = o.Value();
            tester.log.Debug("Folder in trash: {}", trashed_folder);
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFilesystemTests) {
    REGISTER_TEST(TestDirectoryWatcher);
    REGISTER_TEST(TestDirectoryWatcherErrors);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestFilesystemApi);
}
