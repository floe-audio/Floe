// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "packager.hpp"

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/json/json_writer.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/state/state_coding.hpp"

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

struct PackageInfo {
    struct Instrument {
        String name;
        Optional<String> description;
        Instrument* next;
    };

    struct Library {
        String name;
        HashTable<String, Instrument*> instruments_by_folder;
        Set<String> instrument_tags;
    };

    HashTable<sample_lib::LibraryId, Library> libraries;
    HashTable<String, u32> preset_folders; // path -> num presets
    HashTable<String, u32> preset_tags;
    usize package_size {0}; // total size of the package in bytes
    String name;
};

static void AddLibrary(PackageInfo& info, sample_lib::Library const& lib, ArenaAllocator& arena) {
    auto lib_result = info.libraries.FindOrInsertGrowIfNeeded(arena, lib.Id(), {});
    if (lib_result.inserted) lib_result.element.data.name = arena.Clone(lib.name);

    auto& insts = lib_result.element.data.instruments_by_folder;
    for (auto const [inst_name, inst, _] : lib.insts_by_name) {
        // Add instrument.
        {
            auto instrument = arena.New<PackageInfo::Instrument>(PackageInfo::Instrument {
                .name = arena.Clone(inst_name),
                .description = inst->description.Clone(arena),
                .next = nullptr,
            });

            DynamicArray<char> folder {arena};
            for (auto f = inst->folder; f; f = f->parent) {
                if (!f->parent) continue; // skip the root folder
                if (folder.size) dyn::Prepend(folder, '/');
                if (f->parent) dyn::PrependSpan(folder, f->name);
            }

            auto inst_result = insts.FindOrInsertGrowIfNeeded(arena, folder.ToOwnedSpan(), instrument);
            if (!inst_result.inserted) SinglyLinkedListPrepend(inst_result.element.data, instrument);
        }

        for (auto const [tag, _] : inst->tags) {
            auto tag_result = lib_result.element.data.instrument_tags.FindOrInsertGrowIfNeeded(arena, tag);
            if (tag_result.inserted) tag_result.element.key = arena.Clone(tag);
        }
    }
}

static void
AddPresetIfNeeded(PackageInfo& info, String path_in_zip, ArenaAllocator& arena, Span<u8 const> file_data) {
    if (!StartsWithSpan(path_in_zip, package::k_presets_subdir)) return;
    if (!PresetFormatFromPath(path_in_zip)) return;

    {
        path_in_zip = path_in_zip.SubSpan(package::k_presets_subdir.size);
        path_in_zip = path::TrimDirectorySeparatorsStart(path_in_zip);

        auto const folder = path::Directory(path_in_zip, path::Format::Posix).ValueOr("/");
        auto found = info.preset_folders.FindOrInsertGrowIfNeeded(arena, folder, 0);
        if (found.inserted) found.element.key = arena.Clone(folder);
        ++found.element.data;
    }

    if (auto const outcome = DecodeFromMemory(file_data, StateSource::PresetFile, true); outcome.HasValue()) {
        auto const& state = outcome.Value();
        for (auto const tag : state.metadata.tags) {
            auto tag_result = info.preset_tags.FindOrInsertGrowIfNeeded(arena, tag, 0);
            if (tag_result.inserted) tag_result.element.key = arena.Clone(tag);
            ++tag_result.element.data;
        }
    }
}

static ErrorCodeOr<String> ToJson(PackageInfo const& info, ArenaAllocator& arena) {
    DynamicArray<char> json_buffer {arena};
    json::WriteContext json {
        .out = dyn::WriterFor(json_buffer),
    };

    TRY(json::WriteObjectBegin(json));

    TRY(json::WriteKeyValue(json, "size", info.package_size));
    TRY(json::WriteKeyValue(json, "name", info.name));

    TRY(json::WriteKeyArrayBegin(json, "libraries"));
    for (auto const& [lib_id, lib, _] : info.libraries) {
        TRY(json::WriteObjectBegin(json));
        TRY(json::WriteKeyValue(json, "name", lib.name));
        TRY(json::WriteKeyArrayBegin(json, "instrument_folders"));
        for (auto const& [folder, inst_list, _] : lib.instruments_by_folder) {
            TRY(json::WriteObjectBegin(json));
            TRY(json::WriteKeyValue(json, "name", folder));

            TRY(json::WriteKeyArrayBegin(json, "instruments"));
            for (auto i = inst_list; i; i = i->next) {
                auto const inst = i;
                TRY(json::WriteObjectBegin(json));
                TRY(json::WriteKeyValue(json, "name", inst->name));
                if (inst->description) TRY(json::WriteKeyValue(json, "description", *inst->description));
                TRY(json::WriteObjectEnd(json));
            }
            TRY(json::WriteArrayEnd(json)); // instruments

            TRY(json::WriteKeyArrayBegin(json, "instrument_tags"));
            for (auto const [tag, _] : lib.instrument_tags)
                TRY(json::WriteValue(json, tag));
            TRY(json::WriteArrayEnd(json)); // instrument_tags

            TRY(json::WriteObjectEnd(json)); // folder
        }
        TRY(json::WriteArrayEnd(json)); // instruments
        TRY(json::WriteObjectEnd(json)); // library
    }
    TRY(json::WriteArrayEnd(json)); // libraries

    TRY(json::WriteKeyObjectBegin(json, "presets"));
    for (auto const& [folder, num_presets, _] : info.preset_folders) {
        TRY(json::WriteKeyObjectBegin(json, folder));
        TRY(json::WriteKeyValue(json, "num_presets", num_presets));
        TRY(json::WriteObjectEnd(json));
    }
    TRY(json::WriteObjectEnd(json));

    TRY(json::WriteKeyArrayBegin(json, "preset_tags"));
    for (auto const& [tag, num_presets, _] : info.preset_tags) {
        TRY(json::WriteObjectBegin(json));
        TRY(json::WriteKeyValue(json, "name", tag));
        TRY(json::WriteKeyValue(json, "num_presets", num_presets));
        TRY(json::WriteObjectEnd(json));
    }
    TRY(json::WriteArrayEnd(json)); // preset_tags

    TRY(json::WriteObjectEnd(json));

    return json_buffer.ToOwnedSpan();
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

    PackageInfo package_info {};

    auto const create_package = cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].was_provided;

    auto const generate_package_info =
        cli_args[ToInt(PackagerCliArgId::OutputPackageInfoJsonFile)].was_provided;

    sample_lib::Library* lib_for_package_name = nullptr;

    for (auto const path : cli_args[ToInt(PackagerCliArgId::LibraryFolder)].values) {
        auto const library_path = TRY_OR(AbsolutePath(arena, path), {
            StdPrintF(StdStream::Err, "Error: failed to resolve library path '{}'\n", path);
            return error;
        });
        // library_folder can actually be a MDATA file but this is an uncommon legacy case so we don't
        // document it.
        if (path::Extension(library_path) == ".mdata") {
            auto reader = TRY_OR(Reader::FromFile(library_path), {
                StdPrintF(StdStream::Err, "Error: failed to open library file '{}'\n", library_path);
                return error;
            });
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

            if (generate_package_info) AddLibrary(package_info, *lib, arena);

            TRY_OR(package::WriterAddLibrary(package, *lib, arena, program_name), {
                StdPrintF(StdStream::Err,
                          "Error: failed to add library {} to package: {}\n",
                          library_path,
                          error);
                return error;
            });

            continue;
        }

        auto const paths = TRY_OR(ScanLibraryFolder(arena, library_path), {
            StdPrintF(StdStream::Err, "Error: failed to scan library folder '{}'\n", library_path);
            return error;
        });

        auto lib = TRY_OR(ReadLua(paths.lua, arena), {
            StdPrintF(StdStream::Err, "Error: failed to read Lua file '{}'\n", paths.lua);
            return error;
        });
        lib_for_package_name = lib;

        if (generate_package_info) AddLibrary(package_info, *lib, arena);

        if (!sample_lib::CheckAllReferencedFilesExist(*lib, StdWriter(StdStream::Err))) {
            StdPrintF(StdStream::Err,
                      "Error: library {} has missing files, cannot create package\n",
                      lib->name);
            return ErrorCode {CommonError::NotFound};
        }

        auto const library_folder_in_zip =
            TRY_OR(package::WriterAddLibrary(package, *lib, arena, program_name), {
                StdPrintF(StdStream::Err,
                          "Error: failed to add library {} to package: {}\n",
                          library_path,
                          error);
                return error;
            });
        auto const about_doc = TRY_OR(WriteAboutLibraryDocument(*lib, arena, paths, *library_folder_in_zip), {
            StdPrintF(StdStream::Err,
                      "Error: failed to write about document for library {}: {}\n",
                      lib->name,
                      error);
            return error;
        });
        if (!package::WriterAddFile(package, about_doc.filename_in_zip, about_doc.file_data.ToByteSpan())) {
            StdPrintF(StdStream::Out,
                      "Error: auto-generated {} already exists - remove it\n",
                      about_doc.filename_in_zip);
            return ErrorCode {FilesystemError::PathAlreadyExists};
        }
        if (create_package)
            StdPrintF(StdStream::Out, "Added library document: {}\n", about_doc.filename_in_zip);
    }

    for (auto const p : cli_args[ToInt(PackagerCliArgId::PresetFolder)].values) {
        auto const preset_folder = TRY_OR(AbsolutePath(arena, p), {
            StdPrintF(StdStream::Err, "Error: failed to resolve preset folder '{}'\n", p);
            return error;
        });
        // We add presets to the package even when generating package info only because the ZIP structure
        // is convenient to read the filename from.
        TRY_OR(package::WriterAddPresetsFolder(package,
                                               preset_folder,
                                               arena,
                                               program_name,
                                               [&](String path, Span<u8 const> file_data) {
                                                   AddPresetIfNeeded(package_info, path, arena, file_data);
                                               }),
               {
                   StdPrintF(StdStream::Err,
                             "Error: failed to add presets folder {} to package: {}\n",
                             preset_folder,
                             error);
               });
    }

    {
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
        if (create_package)
            StdPrintF(StdStream::Out, "Added installation document: {}\n", k_installation_doc_name);
    }

    // We do input packages last because we prioritise file from libraries/presets. We ignore files from
    // packages if they already exist from the libraries/presets.
    for (auto const path : cli_args[ToInt(PackagerCliArgId::InputPackages)].values) {
        auto const package_path = TRY_OR(AbsolutePath(arena, path), {
            StdPrintF(StdStream::Err, "Error: failed to resolve input package path '{}'\n", path);
            return error;
        });

        auto reader = TRY_OR(Reader::FromFile(package_path), {
            StdPrintF(StdStream::Err, "Error: failed to open input package file '{}'\n", package_path);
            return error;
        });

        package::PackageReader input_package {reader};
        TRY_OR(package::ReaderInit(input_package), {
            StdPrintF(StdStream::Err, "Error: failed to read input package '{}': {}\n", package_path, error);
            return error;
        });
        DEFER { package::ReaderDeinit(input_package); };

        if (generate_package_info) {
            package::PackageComponentIndex iterator = 0;
            while (true) {
                auto const component = ({
                    auto const o = package::IteratePackageComponents(input_package, iterator, arena);
                    if (o.HasError()) {
                        StdPrintF(StdStream::Err,
                                  "Error: failed to read input package component: {}\n",
                                  o.Error());
                        return o.Error();
                    }
                    o.Value();
                });
                if (!component) break;

                if (component->type == package::ComponentType::Library)
                    AddLibrary(package_info, *component->library, arena);
            }
        }

        TRY_OR(package::WriterAddPackage(package,
                                         input_package,
                                         arena,
                                         [&](String path, Span<u8 const> file_data) {
                                             AddPresetIfNeeded(package_info, path, arena, file_data);
                                         }),
               {
                   StdPrintF(StdStream::Err,
                             "Error: failed to add input package {} to output package: {}\n",
                             package_path,
                             error);
                   return error;
               });
    }

    {
        package::WriterFinalise(package);

        auto const package_name =
            PackageName(arena, lib_for_package_name, cli_args[ToInt(PackagerCliArgId::PackageName)]);
        if (generate_package_info) package_info.name = package_name;

        if (create_package) {
            String const folder = TRY_OR(
                AbsolutePath(arena, cli_args[ToInt(PackagerCliArgId::OutputPackageFolder)].values[0]),
                {
                    StdPrintF(StdStream::Err, "Error: failed to resolve output package folder: {}\n", error);
                    return error;
                });
            TRY_OR(
                CreateDirectory(folder, {.create_intermediate_directories = true, .fail_if_exists = false}),
                {
                    StdPrintF(StdStream::Err,
                              "Error: failed to create output package folder '{}': {}\n",
                              folder,
                              error);
                    return error;
                });

            auto const package_path = path::Join(arena, Array {folder, package_name});

            TRY_OR(WriteFile(package_path, zip_data), {
                StdPrintF(StdStream::Err,
                          "Error: failed to write package file to '{}': {}\n",
                          package_path,
                          error);
                return error;
            });
            StdPrintF(StdStream::Out, "Successfully created package: {}\n", package_path);
        }

        if (generate_package_info) package_info.package_size = zip_data.size;
    }

    if (generate_package_info) {
        auto const json = TRY_OR(ToJson(package_info, arena), {
            StdPrintF(StdStream::Err, "Error: failed to write package info JSON: {}\n", error);
            return error;
        });

        auto const output_json_path =
            path::Join(arena, Array {cli_args[ToInt(PackagerCliArgId::OutputPackageInfoJsonFile)].values[0]});
        TRY_OR(WriteFile(output_json_path, json), {
            StdPrintF(StdStream::Err,
                      "Error: failed to write package info JSON file to '{}': {}\n",
                      output_json_path,
                      error);
            return error;
        });
        StdPrintF(StdStream::Out, "Successfully wrote package info JSON to: {}\n", output_json_path);
    }

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
