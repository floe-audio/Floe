// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "foundation/utils/path.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

#include "checksum_crc32_file.hpp"
#include "sample_library/sample_library.hpp"

namespace package {

constexpr String k_libraries_subdir = "Libraries";
constexpr String k_presets_subdir = "Presets";
constexpr String k_file_extension = ".floe.zip"_s;
constexpr String k_checksums_file = ".Floe-Details/checksums.crc32"_s;

PUBLIC bool IsPathPackageFile(String path) { return EndsWithSpan(path, k_file_extension); }

PUBLIC mz_zip_archive WriterCreate() {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_heap(&zip, 0, Mb(100))) {
        PanicF(SourceLocation::Current(),
               "Failed to initialize zip writer: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return zip;
}

PUBLIC void WriterDestroy(mz_zip_archive& zip) { mz_zip_writer_end(&zip); }

PUBLIC void WriterAddFile(mz_zip_archive& zip, String path, Span<u8 const> data) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena;
    auto const archived_path = NullTerminated(path, scratch_arena);

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto p = archived_path; *p; ++p)
            if (*p == '\\') *p = '/';
    }

    if (!mz_zip_writer_add_mem(
            &zip,
            archived_path,
            data.data,
            data.size,
            (mz_uint)(path::Extension(path) != ".flac" ? MZ_DEFAULT_COMPRESSION : MZ_NO_COMPRESSION))) {
        PanicF(SourceLocation::Current(),
               "Failed to add file to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

// builds the package file data
PUBLIC Span<u8 const> WriterFinalise(mz_zip_archive& zip) {
    void* zip_data = nullptr;
    usize zip_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size)) {
        PanicF(SourceLocation::Current(),
               "Failed to finalize zip archive: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return Span {(u8 const*)zip_data, zip_size};
}

namespace detail {

static ErrorCodeOr<void> WriterAddAllFiles(mz_zip_archive& zip,
                                           String folder,
                                           ArenaAllocator& scratch_arena,
                                           Span<String const> subdirs_in_zip) {
    auto it = TRY(RecursiveDirectoryIterator::Create(scratch_arena,
                                                     folder,
                                                     {
                                                         .wildcard = "*",
                                                         .get_file_size = false,
                                                         .skip_dot_files = true,
                                                     }));
    ArenaAllocator inner_arena {PageAllocator::Instance()};
    while (it.HasMoreFiles()) {
        inner_arena.ResetCursorAndConsolidateRegions();

        auto const& entry = it.Get();
        if (entry.type == FileType::File) {
            auto const file_data = TRY(ReadEntireFile(entry.path, inner_arena)).ToByteSpan();

            DynamicArray<char> archive_path {inner_arena};
            path::JoinAppend(archive_path, subdirs_in_zip);
            path::JoinAppend(archive_path, entry.path.Items().SubSpan(it.CanonicalBasePath().size + 1));
            WriterAddFile(zip, archive_path, file_data);
        }

        TRY(it.Increment());
    }

    return k_success;
}

static Optional<String> RelativePathIfInFolder(String path, String folder) {
    if (path.size < folder.size) return nullopt;
    if (path[folder.size] != '/') return nullopt;
    if (!StartsWithSpan(path, folder)) return nullopt;
    return path.SubSpan(folder.size + 1);
}

static void WriterAddChecksumForFolder(mz_zip_archive& zip,
                                       String folder_in_archive,
                                       ArenaAllocator& scratch_arena,
                                       String program_name) {
    DynamicArray<char> checksums {scratch_arena};
    AppendCommentLine(checksums,
                      fmt::Format(scratch_arena,
                                  "Checksums for {} generated by {}"_s,
                                  path::Filename(folder_in_archive),
                                  program_name));
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            PanicF(SourceLocation::Current(),
                   "Failed to get file stat: {}",
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

        if (file_stat.m_is_directory) continue;

        auto const relative_path =
            RelativePathIfInFolder(FromNullTerminated(file_stat.m_filename), folder_in_archive);
        if (!relative_path) continue;

        AppendChecksumLine(checksums,
                           {
                               .path = *relative_path,
                               .crc32 = (u32)file_stat.m_crc32,
                               .file_size = file_stat.m_uncomp_size,
                           });
    }
    WriterAddFile(zip,
                  path::Join(scratch_arena, Array {folder_in_archive, k_checksums_file}, path::Format::Posix),
                  checksums.Items().ToByteSpan());
}

} // namespace detail

PUBLIC ErrorCodeOr<void> WriterAddLibrary(mz_zip_archive& zip,
                                          sample_lib::Library const& lib,
                                          ArenaAllocator& scratch_arena,
                                          String program_name) {
    auto const subdirs =
        Array {k_libraries_subdir, fmt::Format(scratch_arena, "{} - {}", lib.author, lib.name)};
    TRY(detail::WriterAddAllFiles(zip, *path::Directory(lib.path), scratch_arena, subdirs));
    detail::WriterAddChecksumForFolder(zip,
                                       path::Join(scratch_arena, subdirs, path::Format::Posix),
                                       scratch_arena,
                                       program_name);
    return k_success;
}

PUBLIC ErrorCodeOr<void> WriterAddPresetsFolder(mz_zip_archive& zip,
                                                String folder,
                                                ArenaAllocator& scratch_arena,
                                                String program_name) {
    auto const subdirs = Array {k_presets_subdir, path::Filename(folder)};
    TRY(detail::WriterAddAllFiles(zip, folder, scratch_arena, subdirs));
    detail::WriterAddChecksumForFolder(zip,
                                       path::Join(scratch_arena, subdirs, path::Format::Posix),
                                       scratch_arena,
                                       program_name);
    return k_success;
}

// =================================================================================================

/* TODO: the loading a package format
 *
 * There's 3 checksums: the zip file, the checksums.crc32 file, and the actual current filesystem
 * For a library, we should also consider the version number in the floe.lua file
 *
 *
 * InstallLibrary:
 * - extract the library to a temp folder
 *   - If the extraction completed successfully
 *     - do an atomic rename to the final location
 *     - done, success
 *   - If the extraction failed
 *     - delete the temp folder
 *     - done, show an error
 *
 * IsUnchangedSinceInstalled:
 * - if the library folder doesn't have a .checksums.crc32 file return 'false'
 * - read the .checksums.crc32 file
 * - if any current file's checksum deviates from the stored checksum return 'false'
 * - else return 'true'
 *
 *
 * For each library in the package:
 * - Get the floe.lua file from the package
 * - Read the floe.lua file
 * - If the library author+name is not already installed in any library folder (ask the sample library server)
 *   - InstallPackage
 * - If it is
 *   - Compare the version of the installed library vs the version in the package
 *   - If IsUnchangedSinceInstalled
 *     - If the version in the package is newer or equal
 *       - InstallPackage, it's safe to overwrite: libraries are backwards compatible
 *         If the version is equal then the developer forgot to increment the version number
 *       - return
 *     - Else
 *       - return: nothing to do, it's already installed
 *   - Else
 *     - Ask the user if they want to overwrite, giving information about versions and files that have changed
 *
 */

enum class PackageError {
    FileCorrupted,
    NotFloePackage,
};
PUBLIC ErrorCodeCategory const& PackageErrorCodeType() {
    static constexpr ErrorCodeCategory const k_cat {
        .category_id = "PK",
        .message =
            [](Writer const& writer, ErrorCode e) {
                return writer.WriteChars(({
                    String s {};
                    switch ((PackageError)e.code) {
                        case PackageError::FileCorrupted: s = "File is corrupted"_s; break;
                        case PackageError::NotFloePackage: s = "Not a valid Floe package"_s; break;
                    }
                    s;
                }));
            },
    };
    return k_cat;
}
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(PackageError) { return PackageErrorCodeType(); }

struct ReaderError {
    ErrorCode code;
    String message;
};

struct TryHelpersReader {
    TRY_HELPER_INHERIT(IsError, TryHelpers)
    TRY_HELPER_INHERIT(ExtractValue, TryHelpers)
    template <typename T>
    static ReaderError ExtractError(ErrorCodeOr<T> const& o) {
        return {o.Error(), ""_s};
    }
};

namespace detail {

static ErrorCodeOr<mz_zip_archive_file_stat> FileStat(mz_zip_archive& zip, mz_uint file_index) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
        return ErrorCode {PackageError::FileCorrupted};
    return file_stat;
}

static ValueOrError<mz_zip_archive_file_stat, ReaderError>
FindFloeLuaInZipInLibrary(mz_zip_archive& zip, String library_dir_in_zip) {
    using H = TryHelpersReader;
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        auto const file_stat = TRY_H(FileStat(zip, file_index));

        if (file_stat.m_is_directory) continue;
        auto const relative_path =
            RelativePathIfInFolder(FromNullTerminated(file_stat.m_filename), library_dir_in_zip);
        if (!relative_path) continue;
        if (Contains(*relative_path, '/')) continue;

        if (sample_lib::FilenameIsFloeLuaFile(*relative_path)) return file_stat;
    }
    return ReaderError {ErrorCode {PackageError::NotFloePackage}, "No floe.lua file in library"_s};
}

static ErrorCodeOr<Span<u8 const>>
ExtractFile(mz_zip_archive& zip, mz_zip_archive_file_stat const& file_stat, ArenaAllocator& arena) {
    auto const data = arena.AllocateExactSizeUninitialised<u8>(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&zip, file_stat.m_file_index, data.data, data.size, 0))
        return ErrorCode {PackageError::FileCorrupted};
    return data;
}

PUBLIC ErrorCodeOr<Span<String>>
SubfoldersOfFolder(mz_zip_archive& zip, String folder, ArenaAllocator& arena) {
    DynamicArray<String> folders {arena};

    for (auto file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        auto const file_stat = TRY(FileStat(zip, file_index));
        if (!file_stat.m_is_directory) continue;

        auto const path = FromNullTerminated(file_stat.m_filename);
        if (RelativePathIfInFolder(path, folder)) dyn::Append(folders, arena.Clone(path));
    }

    return folders.ToOwnedSpan();
}

} // namespace detail

PUBLIC ValueOrError<mz_zip_archive, ReaderError> ReaderCreate(File& file) {
    using H = TryHelpersReader;
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pRead = [](void* io_opaque_ptr, mz_uint64 file_offset, void* buffer, usize buffer_size) -> usize {
        auto& file = *(File*)io_opaque_ptr;
        auto const seek_outcome = file.Seek((s64)file_offset, File::SeekOrigin::Start);
        if (seek_outcome.HasError()) return 0;

        auto const read_outcome = file.Read(buffer, buffer_size);
        if (read_outcome.HasError()) return 0;
        return read_outcome.Value();
    };
    zip.m_pIO_opaque = &file;

    if (!mz_zip_reader_init(&zip, TRY_H(file.FileSize()), 0))
        return ReaderError {ErrorCode {PackageError::FileCorrupted}, ""_s};

    usize known_subdirs = 0;
    for (auto const known_subdir : Array {k_libraries_subdir, k_presets_subdir}) {
        mz_uint file_index = 0;
        if (!mz_zip_reader_locate_file_v2(&zip, known_subdir.data, nullptr, 0, &file_index)) continue;
        if (!mz_zip_reader_is_file_a_directory(&zip, file_index)) continue;
        ++known_subdirs;
    }

    if (!known_subdirs) {
        mz_zip_reader_end(&zip);
        return ReaderError {ErrorCode {PackageError::NotFloePackage},
                            "Doesn't contain Libraries or Presets subfolders"_s};
    }

    return zip;
}

PUBLIC void ReaderDestroy(mz_zip_archive& zip) { mz_zip_reader_end(&zip); }

PUBLIC ErrorCodeOr<Span<String>> ReaderFindLibraryDirs(mz_zip_archive& zip, ArenaAllocator& arena) {
    return detail::SubfoldersOfFolder(zip, k_libraries_subdir, arena);
}

PUBLIC ErrorCodeOr<Span<String>> ReaderFindPresetDirs(mz_zip_archive& zip, ArenaAllocator& arena) {
    return detail::SubfoldersOfFolder(zip, k_presets_subdir, arena);
}

PUBLIC ValueOrError<sample_lib::Library*, ReaderError>
ReaderReadLibraryLua(mz_zip_archive& zip, String library_dir_in_zip, ArenaAllocator& scratch_arena) {
    using H = TryHelpersReader;
    auto const floe_lua_stat = TRY(detail::FindFloeLuaInZipInLibrary(zip, library_dir_in_zip));
    auto const floe_lua_data = TRY_H(detail::ExtractFile(zip, floe_lua_stat, scratch_arena));

    auto lua_reader = ::Reader::FromMemory(floe_lua_data);
    auto const lib_outcome = sample_lib::ReadLua(lua_reader,
                                                 FromNullTerminated(floe_lua_stat.m_filename),
                                                 scratch_arena,
                                                 scratch_arena);
    if (lib_outcome.HasError()) return ReaderError {PackageError::NotFloePackage, "floe.lua file is invalid"};
    return lib_outcome.ReleaseValue();
}

} // namespace package
