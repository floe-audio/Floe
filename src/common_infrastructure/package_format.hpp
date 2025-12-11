// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "foundation/utils/path.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"

#include "checksum_crc32_file.hpp"
#include "error_reporting.hpp"
#include "miniz_zip.h"
#include "sample_library/sample_library.hpp"

// Floe's package file format.
//
// See the markdown documentation file for information on the package format.
//
// We use the term 'component' to mean the individual, installable parts of a package. These are either
// libraries or preset folders.

namespace package {

constexpr String k_libraries_subdir = "Libraries";
constexpr String k_presets_subdir = "Presets";
constexpr auto k_component_subdirs = Array {k_libraries_subdir, k_presets_subdir};
constexpr String k_file_extension = ".zip"_s;
constexpr String k_checksums_file = "Floe-Details/checksums.crc32"_s;
enum class ComponentType : u8 { Library, Presets, Count };

PUBLIC String ComponentTypeString(ComponentType type) {
    switch (type) {
        case ComponentType::Library: return "Library"_s;
        case ComponentType::Presets: return "Presets"_s;
        case ComponentType::Count: break;
    }
    PanicIfReached();
}

enum class PackageError {
    FileCorrupted,
    NotFloePackage,
    InvalidLibrary,
    AccessDenied,
    FilesystemError,
    NotEmpty,
};
extern ErrorCodeCategory const g_package_error_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(PackageError) { return g_package_error_category; }

PUBLIC mz_zip_archive WriterCreate(Writer& writer) {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    zip.m_pWrite =
        [](void* io_opaque_ptr, mz_uint64 file_offset, void const* buffer, usize buffer_size) -> usize {
        // "The output is streamable, i.e. file_ofs in mz_file_write_func always increases only by n"
        (void)file_offset;
        auto& writer = *(Writer*)io_opaque_ptr;
        auto const o = writer.WriteBytes(Span {(u8 const*)buffer, buffer_size});
        if (o.HasError()) return 0;
        return buffer_size;
    };
    zip.m_pIO_opaque = &writer;
    if (!mz_zip_writer_init(&zip, 0)) {
        PanicF(SourceLocation::Current(),
               "Failed to initialize zip writer: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return zip;
}

PUBLIC void WriterDestroy(mz_zip_archive& zip) { mz_zip_writer_end(&zip); }

namespace detail {

static bool AlreadyExists(mz_zip_archive& zip, String path) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat))
            PanicF(SourceLocation::Current(),
                   "Failed to get file stat: {}",
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

        if (FromNullTerminated(file_stat.m_filename) == path) return true;
    }
    return false;
}

} // namespace detail

PUBLIC void WriterAddFolder(mz_zip_archive& zip, String path) {
    DynamicArrayBounded<char, path::k_max> archived_path;
    dyn::Assign(archived_path, path);
    if (!EndsWith(archived_path, '/')) dyn::Append(archived_path, '/');

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto& c : archived_path)
            if (c == '\\') c = '/';
    }

    if (detail::AlreadyExists(zip, archived_path)) return;

    if (!mz_zip_writer_add_mem(&zip, dyn::NullTerminated(archived_path), nullptr, 0, 0)) {
        PanicF(SourceLocation::Current(),
               "Failed to add folder to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

PUBLIC void WriterAddParentFolders(mz_zip_archive& zip, String path) {
    auto const parent_path = path::Directory(path, path::Format::Posix);
    if (!parent_path) return;
    WriterAddFolder(zip, *parent_path);
    WriterAddParentFolders(zip, *parent_path);
}

[[nodiscard]] PUBLIC bool WriterAddFile(mz_zip_archive& zip, String path, Span<u8 const> data) {
    ArenaAllocatorWithInlineStorage<200> scratch_arena {Malloc::Instance()};
    DynamicArray<char> archived_path {scratch_arena};
    dyn::Assign(archived_path, path);

    // archive paths use posix separators
    if constexpr (IS_WINDOWS) {
        for (auto& c : archived_path)
            if (c == '\\') c = '/';
    }

    if (detail::AlreadyExists(zip, archived_path)) return false;

    WriterAddParentFolders(zip, path);

    auto const ext = path::Extension(path);

    if (!mz_zip_writer_add_mem(
            &zip,
            dyn::NullTerminated(archived_path),
            data.data,
            data.size,
            (mz_uint)((ext == ".flac" || ext == ".mdata") ? MZ_NO_COMPRESSION : MZ_DEFAULT_COMPRESSION))) {
        PanicF(SourceLocation::Current(),
               "Failed to add file to zip: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    return true;
}

PUBLIC void WriterFinalise(mz_zip_archive& zip) {
    if (!mz_zip_writer_finalize_archive(&zip)) {
        PanicF(SourceLocation::Current(),
               "Failed to finalize zip archive: {}",
               mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
}

namespace detail {

static ErrorCodeOr<void> WriterAddAllFiles(mz_zip_archive& zip,
                                           String folder,
                                           ArenaAllocator& scratch_arena,
                                           Span<String const> subdirs_in_zip,
                                           FunctionRef<void(String, Span<u8 const>)> file_read_hook) {
    auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                    .skip_dot_files = true,
                                                }));
    DEFER { dir_iterator::Destroy(it); };
    ArenaAllocator inner_arena {PageAllocator::Instance()};
    while (auto entry = TRY(dir_iterator::Next(it, scratch_arena))) {
        inner_arena.ResetCursorAndConsolidateRegions();

        // we will manually add the checksums file later
        if (entry->subpath == k_checksums_file) continue;

        DynamicArray<char> archive_path {inner_arena};
        path::JoinAppend(archive_path, subdirs_in_zip);
        path::JoinAppend(archive_path, entry->subpath);
        if (entry->type == FileType::File) {
            auto const file_data =
                TRY(ReadEntireFile(dir_iterator::FullPath(it, *entry, inner_arena), inner_arena))
                    .ToByteSpan();
            if (file_read_hook) file_read_hook(archive_path, file_data);
            if (!WriterAddFile(zip, archive_path, file_data)) return {FilesystemError::PathAlreadyExists};
        }
    }

    return k_success;
}

static Optional<String> RelativePathIfInFolder(String path, String folder) {
    folder = TrimEndIfMatches(folder, '/');
    if (path.size <= folder.size) return k_nullopt;
    if (path[folder.size] != '/') return k_nullopt;
    if (!StartsWithSpan(path, folder)) return k_nullopt;
    auto result = path.SubSpan(folder.size + 1);
    result = TrimEndIfMatches(result, '/');
    return result;
}

static void WriterAddChecksumForFolder(mz_zip_archive& zip,
                                       String folder_in_archive,
                                       ArenaAllocator& scratch_arena,
                                       String program_name) {
    DynamicArray<char> checksums {scratch_arena};
    AppendCommentLine(checksums,
                      fmt::Format(scratch_arena,
                                  "Checksums for {}, generated by {}"_s,
                                  path::Filename(folder_in_archive, path::Format::Posix),
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
    auto const added_checksum = WriterAddFile(
        zip,
        path::Join(scratch_arena, Array {folder_in_archive, k_checksums_file}, path::Format::Posix),
        checksums.Items().ToByteSpan());
    ASSERT(added_checksum);
}

} // namespace detail

PUBLIC ErrorCodeOr<Optional<String>> WriterAddLibrary(mz_zip_archive& zip,
                                                      sample_lib::Library const& lib,
                                                      ArenaAllocator& scratch_arena,
                                                      String program_name) {
    if (lib.file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        LogDebug(ModuleName::Package, "Adding mdata file for library '{}'", lib.path);
        auto const mdata = TRY(ReadEntireFile(lib.path, scratch_arena)).ToByteSpan();
        if (!WriterAddFile(
                zip,
                path::Join(scratch_arena,
                           Array {k_libraries_subdir,
                                  path::MakeSafeForFilename(
                                      fmt::Format(scratch_arena, "{} - {}.mdata", lib.author, lib.name),
                                      scratch_arena)},
                           path::Format::Posix),
                mdata))
            return {FilesystemError::PathAlreadyExists};
        return k_nullopt;
    }

    auto const subdirs =
        Array {k_libraries_subdir,
               path::MakeSafeForFilename(fmt::Format(scratch_arena, "{} - {}", lib.author, lib.name),
                                         scratch_arena)};
    auto const subdirs_str = path::Join(scratch_arena, subdirs, path::Format::Posix);

    TRY(detail::WriterAddAllFiles(zip, *path::Directory(lib.path), scratch_arena, subdirs, {}));
    detail::WriterAddChecksumForFolder(zip, subdirs_str, scratch_arena, program_name);
    return subdirs_str;
}

PUBLIC ErrorCodeOr<void>
WriterAddPresetsFolder(mz_zip_archive& zip,
                       String folder,
                       ArenaAllocator& scratch_arena,
                       String program_name,
                       FunctionRef<void(String, Span<u8 const>)> file_read_hook = {}) {
    auto const subdirs = Array {k_presets_subdir, path::Filename(folder)};
    TRY(detail::WriterAddAllFiles(zip, folder, scratch_arena, subdirs, file_read_hook));
    detail::WriterAddChecksumForFolder(zip,
                                       path::Join(scratch_arena, subdirs, path::Format::Posix),
                                       scratch_arena,
                                       program_name);
    return k_success;
}

// =================================================================================================

struct PackageReader {
    Reader& zip_file_reader;
    mz_zip_archive zip {};
    u64 seed = RandomSeed();
    Optional<ErrorCode> read_callback_error {}; // we need a way to pass out the error from the read callback
};

namespace detail {

static ErrorCode ZipReadError(PackageReader const& package, SourceLocation loc = SourceLocation::Current()) {
    if (package.read_callback_error) {
        auto err = *package.read_callback_error;
        err.source_location = loc;
        return err;
    }
    return ErrorCode {PackageError::FileCorrupted, nullptr, loc};
}

static ErrorCodeOr<mz_zip_archive_file_stat> FileStat(PackageReader& package, mz_uint file_index) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&package.zip, file_index, &file_stat)) return ZipReadError(package);
    return file_stat;
}

static String PathWithoutTrailingSlash(char const* path) {
    return TrimEndIfMatches(FromNullTerminated(path), '/');
}

static ErrorCodeOr<Span<u8 const>>
ExtractFileToMem(PackageReader& package, mz_zip_archive_file_stat const& file_stat, ArenaAllocator& arena) {
    auto const data = arena.AllocateExactSizeUninitialised<u8>(file_stat.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&package.zip, file_stat.m_file_index, data.data, data.size, 0))
        return ZipReadError(package);
    return data;
}

[[maybe_unused]] static ErrorCodeOr<void>
ExtractFileToFile(PackageReader& package, mz_zip_archive_file_stat const& file_stat, File& out_file) {
    struct Context {
        File& out_file;
        ErrorCodeOr<void> result = k_success;
    };
    Context context {out_file};
    if (!mz_zip_reader_extract_to_callback(
            &package.zip,
            file_stat.m_file_index,
            [](void* user_data, mz_uint64 file_offset, void const* buffer, usize buffer_size) -> usize {
                auto& context = *(Context*)user_data;
                auto const o = context.out_file.WriteAt((s64)file_offset, {(u8 const*)buffer, buffer_size});
                if (o.HasError()) {
                    context.result = o.Error();
                    return 0;
                }
                return o.Value();
            },
            &context,
            0)) {
        if (context.result.HasError()) return context.result.Error();
        return ZipReadError(package);
    }
    return k_success;
}

static ErrorCodeOr<sample_lib::Library*>
ReaderReadLibraryLua(PackageReader& package, String library_dir_in_zip, ArenaAllocator& arena) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    // Floe libraries can have other Lua files besides the floe.lua file. When the script is run, it will load
    // these other files from the filesystem via a relative path. We therefore need to extract all Lua files
    // to a temporary directory else the script will fail to run.

    auto const temp_root = KnownDirectory(scratch_arena, KnownDirectoryType::Temporary, {.create = true});
    auto const temp = String(TRY(TemporaryDirectoryWithinFolder(temp_root, scratch_arena, package.seed)));
    DEFER { auto _ = Delete(temp, {.type = DeleteOptions::Type::DirectoryRecursively}); };

    Optional<mz_zip_archive_file_stat> floe_lua_stat;

    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        if (!StartsWithSpan(path, library_dir_in_zip)) continue;

        if (sample_lib::FilenameIsFloeLuaFile(path::Filename(path, path::Format::Posix))) {
            floe_lua_stat = file_stat;
        } else if (path::Equal(path::Extension(path), ".lua"_s)) {
            auto const temp_path = path::Join(scratch_arena, Array {temp, path}, path::Format::Posix);
            TRY(CreateDirectory(path::Directory(temp_path).Value(),
                                {.create_intermediate_directories = true,
                                 .fail_if_exists = false,
                                 .win32_hide_dirs_starting_with_dot = false}));
            auto file = TRY(OpenFile(temp_path, FileMode::Write()));
            TRY(ExtractFileToFile(package, file_stat, file));
        }
    }

    if (!floe_lua_stat) return (sample_lib::Library*)nullptr;
    auto const floe_lua_data = TRY(detail::ExtractFileToMem(package, *floe_lua_stat, arena));

    auto lua_reader = ::Reader::FromMemory(floe_lua_data);
    auto const full_lua_path =
        path::Join(arena, Array {temp, PathWithoutTrailingSlash(floe_lua_stat->m_filename)});
    auto const lib_outcome = sample_lib::ReadLua(lua_reader, full_lua_path, arena, arena);
    if (lib_outcome.HasError()) {
        LogDebug(ModuleName::Package,
                 "Failed to read library Lua file: {}, error: {}",
                 full_lua_path,
                 lib_outcome.Error().message);
        return ErrorCode {PackageError::InvalidLibrary};
    }
    return lib_outcome.ReleaseValue();
}

static ErrorCodeOr<sample_lib::Library*> ReaderReadLibraryMdata(PackageReader& package,
                                                                mz_uint file_index,
                                                                String path_in_zip,
                                                                ArenaAllocator& arena) {
    auto const stat = TRY(detail::FileStat(package, file_index));
    auto const mdata = TRY(detail::ExtractFileToMem(package, stat, arena));
    auto reader = ::Reader::FromMemory(mdata);
    LogDebug(ModuleName::Package, "Reading mdata file: {}", path_in_zip);
    auto const lib_outcome = sample_lib::ReadMdata(reader, path_in_zip, arena, arena);
    if (lib_outcome.HasError()) return ErrorCode {PackageError::InvalidLibrary};
    return lib_outcome.ReleaseValue();
}

static ErrorCodeOr<HashTable<String, ChecksumValues>>
ReaderChecksumValuesForDir(PackageReader& package, String dir_in_zip, ArenaAllocator& arena) {
    DynamicHashTable<String, ChecksumValues> table {arena};
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;
        auto const path = PathWithoutTrailingSlash(file_stat.m_filename);
        auto const relative_path = detail::RelativePathIfInFolder(path, dir_in_zip);
        if (!relative_path) continue;
        if (*relative_path == k_checksums_file) continue;
        table.Insert(arena.Clone(*relative_path),
                     ChecksumValues {(u32)file_stat.m_crc32, file_stat.m_uncomp_size});
    }
    return table.ToOwnedTable();
}

} // namespace detail

PUBLIC ErrorCodeOr<void> ReaderInit(PackageReader& package) {
    mz_zip_zero_struct(&package.zip);
    package.zip.m_pRead =
        [](void* io_opaque_ptr, mz_uint64 file_offset, void* buffer, usize buffer_size) -> usize {
        auto& package = *(PackageReader*)io_opaque_ptr;
        package.zip_file_reader.pos = file_offset;
        auto const num_read = TRY_OR(package.zip_file_reader.Read(buffer, buffer_size), {
            // We store the error because we can't pass it out in the return value.
            package.read_callback_error = error;
            return 0;
        });
        return num_read;
    };
    package.zip.m_pIO_opaque = &package;

    if (!mz_zip_reader_init(&package.zip, package.zip_file_reader.size, 0))
        return detail::ZipReadError(package);

    bool known_subdirs = false;
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const file_stat =
            TRY_OR(detail::FileStat(package, file_index), return detail::ZipReadError(package););
        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
        for (auto const known_subdir : Array {k_libraries_subdir, k_presets_subdir}) {
            if (path == known_subdir || detail::RelativePathIfInFolder(path, known_subdir)) {
                known_subdirs = true;
                break;
            }
        }
    }

    if (!known_subdirs) {
        mz_zip_reader_end(&package.zip);
        return {PackageError::NotFloePackage};
    }

    return k_success;
}

PUBLIC void ReaderDeinit(PackageReader& package) { mz_zip_reader_end(&package.zip); }

// The individual parts of a package, either a library or a presets folder.
struct Component {
    FileType InstallType() const { return mdata_checksum ? FileType::File : FileType::Directory; }

    String path; // path in the zip
    ComponentType type;
    HashTable<String, ChecksumValues> checksum_values;
    Optional<u32> mdata_checksum; // Only for libraries stored as an MDATA.

    // Only valid if this component's type is a library. nullptr otherwise. You can't use this library to read
    // library files since they're unextracted, but you can read basic fields like name and author.
    sample_lib::Library* library;
};

// init to 0
using PackageComponentIndex = mz_uint;

// Call this repeatedly until it returns nullopt
PUBLIC ErrorCodeOr<Optional<Component>>
IteratePackageComponents(PackageReader& package, PackageComponentIndex& file_index, ArenaAllocator& arena) {
    DEFER { ++file_index; };
    for (; file_index < mz_zip_reader_get_num_files(&package.zip); ++file_index) {
        auto const file_stat =
            TRY_OR(detail::FileStat(package, file_index), return detail::ZipReadError(package););
        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
        for (auto const folder : k_component_subdirs) {
            auto const relative_path = detail::RelativePathIfInFolder(path, folder);
            if (!relative_path) continue;
            if (relative_path->size == 0) continue;
            if (Contains(*relative_path, '/')) continue;

            auto const is_mdata_library =
                folder == k_libraries_subdir && path::Equal(path::Extension(path), ".mdata");

            return Optional<Component> {Component {
                .path = path.Clone(arena),
                .type = ({
                    ComponentType t;
                    if (folder == k_libraries_subdir)
                        t = ComponentType::Library;
                    else if (folder == k_presets_subdir)
                        t = ComponentType::Presets;
                    else
                        PanicIfReached();
                    t;
                }),
                .checksum_values = !is_mdata_library
                                       ? TRY_OR(detail::ReaderChecksumValuesForDir(package, path, arena),
                                                return detail::ZipReadError(package);)
                                       : HashTable<String, ChecksumValues> {},
                .mdata_checksum = is_mdata_library ? Optional<u32> {(u32)file_stat.m_crc32} : k_nullopt,
                .library = ({
                    sample_lib::Library* lib = nullptr;
                    if (folder == k_libraries_subdir) {
                        lib = ({
                            auto const library =
                                TRY(!is_mdata_library
                                        ? detail::ReaderReadLibraryLua(package, path, arena)
                                        : detail::ReaderReadLibraryMdata(package, file_index, path, arena));
                            if (!library) return {PackageError::InvalidLibrary};
                            library;
                        });
                    }
                    lib;
                }),
            }};
        }
    }
    return Optional<Component> {k_nullopt};
}

// If a file already exists in the zip, we don't replace it, we just skip it.
PUBLIC ErrorCodeOr<void>
WriterAddPackage(mz_zip_archive& zip,
                 PackageReader& package,
                 ArenaAllocator& scratch_arena,
                 FunctionRef<void(String path, Span<u8 const> file_data)> file_read_hook = {}) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&package.zip))) {
        auto const cursor = scratch_arena.TotalUsed();
        DEFER { scratch_arena.TryShrinkTotalUsed(cursor); };

        auto const file_stat = TRY(detail::FileStat(package, file_index));
        if (file_stat.m_is_directory) continue;

        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);

        auto file_data = TRY(detail::ExtractFileToMem(package, file_stat, scratch_arena));
        if (file_read_hook) file_read_hook(path, file_data);

        // We don't care if it failed because the file already exists, we just skip it.
        auto const _ = WriterAddFile(zip, path, file_data);
    }

    return k_success;
}

PUBLIC void ForEachFile(mz_zip_archive& zip, FunctionRef<void(String)> callback) {
    for (auto const file_index : Range(mz_zip_reader_get_num_files(&zip))) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat)) continue;
        if (file_stat.m_is_directory) continue;
        auto const path = detail::PathWithoutTrailingSlash(file_stat.m_filename);
        callback(path);
    }
}

} // namespace package
