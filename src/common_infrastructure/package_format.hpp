// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "common_infrastructure/preset_bank_info.hpp"

#include "checksum_crc32_file.hpp"
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

String ComponentTypeString(ComponentType type);

enum class PackageError {
    FileCorrupted,
    NotFloePackage,
    InvalidLibrary,
    InvalidPresetBank,
    AccessDenied,
    FilesystemError,
    NotEmpty,
};
extern ErrorCodeCategory const g_package_error_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(PackageError) { return g_package_error_category; }

// Reader
// =================================================================================================

struct PackageReader {
    Reader& zip_file_reader;
    mz_zip_archive zip {};
    u64 seed = RandomSeed();
    Optional<ErrorCode> read_callback_error {}; // we need a way to pass out the error from the read callback
};

ErrorCodeOr<void> ReaderInit(PackageReader& package);
void ReaderDeinit(PackageReader& package);

// The individual parts of a package, either a library or a presets folder.
struct Component {
    FileType InstallFileType() const { return mdata_checksum ? FileType::File : FileType::Directory; }

    String path; // path in the zip
    ComponentType type;
    HashTable<String, ChecksumValues> checksum_values;
    Optional<u32> mdata_checksum; // Only for libraries stored as an MDATA.

    // Only valid if this component's type is a library. nullptr otherwise. You can't use this library to read
    // library files since they're unextracted, but you can read basic fields like name and author.
    sample_lib::Library* library;

    // Only valid if this component's type is a preset bank.
    Optional<PresetBank> preset_bank;
};

// init to 0
using PackageComponentIndex = mz_uint;

// Call this repeatedly until it returns nullopt
ErrorCodeOr<Optional<Component>>
IteratePackageComponents(PackageReader& package, PackageComponentIndex& file_index, ArenaAllocator& arena);

// Writer
// =================================================================================================

mz_zip_archive WriterCreate(Writer& writer);
void WriterDestroy(mz_zip_archive& zip);
void WriterAddFolder(mz_zip_archive& zip, String path);
void WriterAddParentFolders(mz_zip_archive& zip, String path);
[[nodiscard]] bool WriterAddFile(mz_zip_archive& zip, String path, Span<u8 const> data);
void WriterFinalise(mz_zip_archive& zip);

ErrorCodeOr<Optional<String>> WriterAddLibrary(mz_zip_archive& zip,
                                               sample_lib::Library const& lib,
                                               ArenaAllocator& scratch_arena,
                                               String program_name);

ErrorCodeOr<void> WriterAddPresetsFolder(mz_zip_archive& zip,
                                         String folder,
                                         ArenaAllocator& scratch_arena,
                                         String program_name,
                                         FunctionRef<void(String, Span<u8 const>)> file_read_hook = {});

// If a file already exists in the zip, we don't replace it, we just skip it.
ErrorCodeOr<void>
WriterAddPackage(mz_zip_archive& zip,
                 PackageReader& package,
                 ArenaAllocator& scratch_arena,
                 FunctionRef<void(String path, Span<u8 const> file_data)> file_read_hook = {});

// Utils
// =================================================================================================

ErrorCodeOr<mz_zip_archive_file_stat> FileStat(PackageReader& package, mz_uint file_index);
ErrorCodeOr<void>
ExtractFileToFile(PackageReader& package, mz_zip_archive_file_stat const& file_stat, File& out_file);
Optional<String> RelativePathIfInFolder(String path, String folder);
String PathWithoutTrailingSlash(char const* path);

} // namespace package
