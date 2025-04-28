// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "packager.hpp"

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/error_reporting.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "build_resources/embedded_files.h"

// Library packager CLI tool - see packager.hpp for more info

struct Paths {
    String lua;
    String license;
};

ErrorCodeOr<Paths> ScanLibraryFolder(ArenaAllocator& arena, String library_folder) {
    library_folder = path::TrimDirectorySeparatorsEnd(library_folder);

    Paths result {};

    auto it = TRY(dir_iterator::Create(arena,
                                       library_folder,
                                       {
                                           .wildcard = "*",
                                           .get_file_size = false,
                                       }));
    DEFER { dir_iterator::Destroy(it); };
    while (auto const entry = TRY(dir_iterator::Next(it, arena)))
        if (sample_lib::FilenameIsFloeLuaFile(entry->subpath))
            result.lua = dir_iterator::FullPath(it, *entry, arena);
        else if (IsEqualToCaseInsensitiveAscii(path::FilenameWithoutExtension(entry->subpath), "license") ||
                 IsEqualToCaseInsensitiveAscii(path::FilenameWithoutExtension(entry->subpath), "licence"))
            result.license = dir_iterator::FullPath(it, *entry, arena);

    if (!result.lua.size) {
        StdPrintF(StdStream::Err, "Error: no Floe Lua file found in {}\n", library_folder);
        return ErrorCode {CommonError::NotFound};
    }

    if (!result.license.size) {
        StdPrintF(StdStream::Err, "Error: no license file found in {}\n", library_folder);
        StdPrintF(
            StdStream::Err,
            "Expected a file called licence (or license) to be present. Any file extension is allowed.\n");
        return ErrorCode {CommonError::NotFound};
    }

    return result;
}

static ErrorCodeOr<sample_lib::Library*> ReadLua(String lua_path, ArenaAllocator& arena) {
    auto const lua_data = TRY(ReadEntireFile(lua_path, arena));
    auto reader {Reader::FromMemory(lua_data)};
    ArenaAllocator scratch_arena {PageAllocator::Instance()};
    auto const outcome = sample_lib::ReadLua(reader, lua_path, arena, scratch_arena, {});
    if (outcome.HasError()) {
        StdPrintF(StdStream::Err,
                  "Error: failed to read {}: {}, {}\n",
                  lua_path,
                  outcome.Error().message,
                  outcome.Error().code);
        return outcome.Error().code;
    }

    return outcome.Get<sample_lib::Library*>();
}

struct AboutLibraryDocument {
    String filename_in_zip;
    String file_data;
};

static ErrorCodeOr<AboutLibraryDocument> WriteAboutLibraryDocument(sample_lib::Library const& lib,
                                                                   ArenaAllocator& arena,
                                                                   Paths paths,
                                                                   String library_folder_in_zip) {
    ASSERT(lib.file_format_specifics.tag == sample_lib::FileFormat::Lua);

    auto const about_library_doc = ({
        auto data = EmbeddedAboutLibraryTemplateRtf();
        arena.Clone(Span {(char const*)data.data, data.size});
    });

    auto const file_data =
        fmt::FormatStringReplace(arena,
                                 about_library_doc,
                                 ArrayT<fmt::StringReplacement>({
                                     {"__LIBRARY_NAME__", lib.name},
                                     {"__LIBRARY_DESCRIPTION__", lib.description.ValueOr("")},
                                     {"__LUA_FILENAME__", path::Filename(paths.lua)},
                                     {"__LICENSE_FILENAME__", path::Filename(paths.license)},
                                     {"__FLOE_HOMEPAGE_URL__", FLOE_HOMEPAGE_URL},
                                     {"__FLOE_MANUAL_URL__", FLOE_MANUAL_URL},
                                     {"__FLOE_DOWNLOAD_URL__", FLOE_DOWNLOAD_URL},
                                 }));

    return AboutLibraryDocument {
        .filename_in_zip = path::Join(
            arena,
            Array {library_folder_in_zip,
                   fmt::Format(arena, "About {}.rtf"_s, path::MakeSafeForFilename(lib.name, arena))},
            path::Format::Posix),
        .file_data = file_data,
    };
}

static ErrorCodeOr<void> CheckNeededPackageCliArgs(Span<CommandLineArg const> args) {
    if (!args[ToInt(PackagerCliArgId::OutputPackageFolder)].was_provided) return k_success;

    auto const library_folders_arg = args[ToInt(PackagerCliArgId::LibraryFolder)];
    auto const presets_folders_arg = args[ToInt(PackagerCliArgId::PresetFolder)];

    if (!library_folders_arg.values.size && !presets_folders_arg.values.size) {
        StdPrintF(StdStream::Err,
                  "Error: either --{} or --{} must be provided\n",
                  library_folders_arg.info.key,
                  presets_folders_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    auto const package_name_arg = args[ToInt(PackagerCliArgId::PackageName)];
    if (library_folders_arg.values.size != 1 && !package_name_arg.was_provided) {
        StdPrintF(StdStream::Err,
                  "Error: if --{} is not set to 1 folder, --{} must be\n",
                  library_folders_arg.info.key,
                  package_name_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    if (package_name_arg.was_provided) {
        if (path::Equal(path::Extension(package_name_arg.values[0]), package::k_file_extension)) {
            StdPrintF(StdStream::Err, "Error: don't include the file extension in the package name\n");
            return ErrorCode {CliError::InvalidArguments};
        }
    }

    return k_success;
}

static String
PackageName(ArenaAllocator& arena, sample_lib::Library const* lib, CommandLineArg const& package_name_arg) {
    if (package_name_arg.was_provided)
        return fmt::Format(arena,
                           "{} Package{}",
                           path::MakeSafeForFilename(package_name_arg.values[0], arena),
                           package::k_file_extension);
    ASSERT(lib);
    return path::MakeSafeForFilename(
        fmt::Format(arena, "{} - {} Package{}", lib->author, lib->name, package::k_file_extension),
        arena);
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({.init_error_reporting = true, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const program_name = path::Filename(FromNullTerminated(args.args[0]));

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_packager_command_line_args_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                               .description = k_packager_description,
                                                               .version = FLOE_VERSION_STRING,
                                                           }));
    TRY(CheckNeededPackageCliArgs(cli_args));

    DynamicArray<u8> zip_data {arena};
    auto writer = dyn::WriterFor(zip_data);
    auto package = package::WriterCreate(writer);
    DEFER { package::WriterDestroy(package); };

    auto const create_package = cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].was_provided;

    sample_lib::Library* lib_for_package_name = nullptr;

    for (auto const path : cli_args[ToInt(PackagerCliArgId::LibraryFolder)].values) {
        auto const library_path = TRY(AbsolutePath(arena, path));
        // library_folder can actually be a MDATA file but this is an uncommon legacy case so we don't
        // document it.
        if (path::Extension(library_path) == ".mdata") {
            auto reader = TRY(Reader::FromFile(library_path));
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto outcome = sample_lib::ReadMdata(reader, library_path, arena, scratch_arena);
            if (outcome.HasError()) {
                StdPrintF(StdStream::Err,
                          "Error: failed to read {}: {}, {}\n",
                          library_path,
                          outcome.Error().message,
                          outcome.Error().code);
                return outcome.Error().code;
            }
            auto lib = outcome.Get<sample_lib::Library*>();
            lib_for_package_name = lib;
            if (create_package) TRY(package::WriterAddLibrary(package, *lib, arena, program_name));

            continue;
        }

        auto const paths = TRY(ScanLibraryFolder(arena, library_path));

        auto lib = TRY(ReadLua(paths.lua, arena));
        lib_for_package_name = lib;
        if (!sample_lib::CheckAllReferencedFilesExist(*lib, StdWriter(StdStream::Err)))
            return ErrorCode {CommonError::NotFound};

        if (create_package) {
            auto const library_folder_in_zip =
                TRY(package::WriterAddLibrary(package, *lib, arena, program_name));
            auto const about_doc = TRY(WriteAboutLibraryDocument(*lib, arena, paths, *library_folder_in_zip));
            if (!package::WriterAddFile(package,
                                        about_doc.filename_in_zip,
                                        about_doc.file_data.ToByteSpan())) {
                StdPrintF(StdStream::Out,
                          "Error: auto-generated {} already exists - remove it\n",
                          about_doc.filename_in_zip);
                return ErrorCode {FilesystemError::PathAlreadyExists};
            }
            StdPrintF(StdStream::Out, "Added library document: {}\n", about_doc.filename_in_zip);
        }
    }

    if (create_package)
        for (auto const preset_folder : cli_args[ToInt(PackagerCliArgId::PresetFolder)].values)
            TRY(package::WriterAddPresetsFolder(package, preset_folder, arena, program_name));

    if (create_package) {
        auto const how_to_install_doc = ({
            auto data = EmbeddedPackageInstallationRtf();
            arena.Clone(Span {(char const*)data.data, data.size});
        });
        constexpr String k_installation_doc_name = "Installation.rtf"_s;
        if (!package::WriterAddFile(package, k_installation_doc_name, how_to_install_doc.ToByteSpan())) {
            StdPrintF(StdStream::Out,
                      "Error: auto-generated {} already exists - remove it\n",
                      k_installation_doc_name);
            return ErrorCode {FilesystemError::PathAlreadyExists};
        }
        StdPrintF(StdStream::Out, "Added installation document: {}\n", k_installation_doc_name);

        auto const package_path = path::Join(
            arena,
            Array {cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].values[0],
                   PackageName(arena, lib_for_package_name, cli_args[ToInt(PackagerCliArgId::PackageName)])});
        package::WriterFinalise(package);
        TRY(WriteFile(package_path, zip_data));
        StdPrintF(StdStream::Out, "Successfully created package: {}\n", package_path);
    } else {
        StdPrintF(
            StdStream::Err,
            "No output packge folder provided, not creating a package file\nRun with --help for usage info\n");
    }

    return 0;
}

int main(int argc, char** argv) {
    ++g_is_logical_main_thread;
    auto const result = Main({argc, argv});
    if (result.HasError()) {
        StdPrintF(StdStream::Err, "Error: {}\n", result.Error());
        return 1;
    }
    return result.Value();
}
