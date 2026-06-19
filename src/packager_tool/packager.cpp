// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "packager.hpp"

#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/json/json_writer.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/encrypted_package.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/license.hpp"
#include "common_infrastructure/package_format.hpp"
#include "common_infrastructure/preset_bank_info.hpp"
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

static bool IsLibraryFileReferenced(String subpath, Set<String> const& referenced) {
    if (path::Extension(subpath) == ".lua") return true;
    return referenced.Find(subpath) != nullptr;
}

static bool IsPresetFolderFileNeeded(String subpath) {
    if (PresetFormatFromPath(subpath)) return true;
    return path::Filename(subpath) == k_preset_bank_filename;
}

static Set<String>
BuildLibraryReferencedSet(sample_lib::Library const& lib, Paths const& paths, ArenaAllocator& arena) {
    Set<String> referenced;
    auto add = [&](String s) {
        auto r = referenced.FindOrInsertGrowIfNeeded(arena, s);
        if (r.inserted) r.element.key = arena.Clone(s);
    };
    add(path::Filename(paths.lua));
    add(path::Filename(paths.license));
    sample_lib::ForEachReferencedFile(lib, [&](sample_lib::LibraryPath p) { add(p.str); });
    return referenced;
}

static ErrorCodeOr<void> ReportUnreferencedLibraryFiles(sample_lib::Library const& lib,
                                                        String library_folder,
                                                        Set<String> const& referenced,
                                                        bool omit,
                                                        ArenaAllocator& scratch) {
    auto it = TRY(dir_iterator::RecursiveCreate(scratch,
                                                library_folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                    .skip_dot_files = true,
                                                }));
    DEFER { dir_iterator::Destroy(it); };
    while (auto const entry = TRY(dir_iterator::Next(it, scratch))) {
        if (entry->type != FileType::File) continue;
        if (entry->subpath == package::k_checksums_file) continue;
        if (path::IgnorableSystemFile(entry->subpath)) continue;
        if (IsLibraryFileReferenced(entry->subpath, referenced)) continue;
        StdPrintF(StdStream::Err,
                  "{}: unreferenced file in library '{}': {}\n",
                  omit ? "Omitting"_s : "Warning"_s,
                  lib.name,
                  entry->subpath);
    }
    return k_success;
}

static ErrorCodeOr<void>
ReportNonPresetFilesInPresetFolder(String preset_folder, bool omit, ArenaAllocator& scratch) {
    auto it = TRY(dir_iterator::RecursiveCreate(scratch,
                                                preset_folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                    .skip_dot_files = true,
                                                }));
    DEFER { dir_iterator::Destroy(it); };
    while (auto const entry = TRY(dir_iterator::Next(it, scratch))) {
        if (entry->type != FileType::File) continue;
        if (path::IgnorableSystemFile(entry->subpath)) continue;
        if (IsPresetFolderFileNeeded(entry->subpath)) continue;
        StdPrintF(StdStream::Err,
                  "{}: non-preset file in preset folder '{}': {}\n",
                  omit ? "Omitting"_s : "Warning"_s,
                  path::Filename(preset_folder),
                  entry->subpath);
    }
    return k_success;
}

static ErrorCodeOr<bool> ValidateSourcePathsPortability(String folder, ArenaAllocator& scratch) {
    auto it = TRY(dir_iterator::RecursiveCreate(scratch,
                                                folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = false,
                                                    .skip_dot_files = true,
                                                }));
    DEFER { dir_iterator::Destroy(it); };
    bool all_valid = true;
    while (auto const entry = TRY(dir_iterator::Next(it, scratch))) {
        if (entry->type != FileType::File) continue;
        if (path::IgnorableSystemFile(entry->subpath)) continue;
        if (!path::IsPortableAcrossOs(entry->subpath, StdWriter(StdStream::Err))) all_valid = false;
    }
    return all_valid;
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
    auto const input_packages_arg = args[ToInt(PackagerCliArgId::InputPackages)];

    if (!library_folders_arg.values.size && !presets_folders_arg.values.size &&
        !input_packages_arg.values.size) {
        StdPrintF(StdStream::Err,
                  "Error: at least one of --{}, --{}, or --{} must be provided\n",
                  library_folders_arg.info.key,
                  presets_folders_arg.info.key,
                  input_packages_arg.info.key);
        return ErrorCode {CliError::InvalidArguments};
    }

    return k_success;
}

static String
PackageName(ArenaAllocator& arena, sample_lib::Library const* lib, Span<CommandLineArg const> args) {
    if (args[ToInt(PackagerCliArgId::PackageName)].was_provided) {
        auto raw = args[ToInt(PackagerCliArgId::PackageName)].values[0];
        if (package::HasPackageExtension(raw)) raw = path::FilenameWithoutExtension(raw);
        return fmt::Format(arena,
                           "{} Package{}",
                           path::MakeSafeForFilename(raw, arena),
                           package::k_file_extension);
    }
    if (lib)
        return path::MakeSafeForFilename(
            fmt::Format(arena, "{} - {} Package{}", lib->author, lib->name, package::k_file_extension),
            arena);
    if (args[ToInt(PackagerCliArgId::InputPackages)].was_provided)
        return args[ToInt(PackagerCliArgId::InputPackages)].values[0];

    return fmt::Format(arena, "Floe Package{}", package::k_file_extension);
}

struct PackageInfo {
    struct Instrument {
        String name;
        Optional<String> description;
        Instrument* next;
    };

    struct PresetName {
        String name;
        PresetName* next;
    };

    struct Library {
        String name;
        String author;
        sample_lib::FileFormat file_format {};
        OrderedHashTable<String, Instrument*> instruments_by_folder;
    };

    struct TagCount {
        TagCategoryGroup group;
        u32 count;
    };

    static bool LibraryLessThan(sample_lib::LibraryId const& a,
                                Library const&,
                                sample_lib::LibraryId const& b,
                                Library const&) {
        return sample_lib::LibraryIdLessThan(a, b);
    }

    OrderedHashTable<sample_lib::LibraryId, Library, NoHash, LibraryLessThan> libraries;
    OrderedHashTable<String, PresetName*> preset_folders;
    OrderedHashTable<String, TagCount> instrument_tags;
    OrderedHashTable<String, TagCount> preset_tags;
    Set<String> extra_files;
    usize package_size {0};
    String name;
};

static TagCategoryGroup GroupOfTag(TagType t) {
    for (auto const c : EnumIterator<TagCategory>()) {
        auto const info = Tags(c);
        for (auto const tag : info.tags)
            if (tag == t) return info.group;
    }
    PanicIfReached();
}

static String GroupLabel(TagCategoryGroup g) {
    switch (g) {
        case TagCategoryGroup::Source: return "Sources"_s;
        case TagCategoryGroup::Type: return "Sound types"_s;
        case TagCategoryGroup::Mood: return "Moods & genres"_s;
        case TagCategoryGroup::Timbre: return "Timbres"_s;
        case TagCategoryGroup::Count: PanicIfReached();
    }
    return {};
}

static void AddLibrary(PackageInfo& info,
                       sample_lib::Library const& lib,
                       ArenaAllocator& arena,
                       ArenaAllocator& scratch) {
    auto lib_result = info.libraries.FindOrInsertGrowIfNeeded(arena, lib.id, {});
    if (lib_result.inserted) {
        lib_result.element.data.name = arena.Clone(lib.name);
        lib_result.element.data.author = arena.Clone(lib.author);
        lib_result.element.data.file_format = lib.file_format_specifics.tag;
    }

    auto& insts = lib_result.element.data.instruments_by_folder;
    for (auto const [inst_id, inst, _] : lib.insts_by_id) {
        // Add instrument.
        {
            auto instrument = arena.New<PackageInfo::Instrument>(PackageInfo::Instrument {
                .name = arena.Clone(inst->name),
                .description = inst->description.Clone(arena),
                .next = nullptr,
            });

            DynamicArray<char> folder {scratch};
            for (auto f = inst->folder; f; f = f->parent) {
                if (!f->parent) continue; // skip the root folder
                if (folder.size) dyn::Prepend(folder, '/');
                if (f->parent) dyn::PrependSpan(folder, f->name);
            }

            auto inst_result = insts.FindOrInsertGrowIfNeeded(arena, folder, instrument);
            if (inst_result.inserted) inst_result.element.key = arena.Clone((String)folder);
            if (!inst_result.inserted) SinglyLinkedListPrepend(inst_result.element.data, instrument);
        }

        inst->tags.ForEachSetBit([&](usize bit) {
            auto const tag_type = (TagType)bit;
            auto const tag_name = GetTagInfo(tag_type).name;
            auto tag_result = info.instrument_tags.FindOrInsertGrowIfNeeded(
                arena,
                tag_name,
                PackageInfo::TagCount {.group = GroupOfTag(tag_type), .count = 0});
            if (tag_result.inserted) tag_result.element.key = arena.Clone(tag_name);
            ++tag_result.element.data.count;
        });
    }
}

static void
AddPresetIfNeeded(PackageInfo& info, String path_in_zip, ArenaAllocator& arena, Span<u8 const> file_data) {
    if (!StartsWithSpan(path_in_zip, package::k_presets_subdir)) return;
    if (!PresetFormatFromPath(path_in_zip)) return;

    {
        path_in_zip = path_in_zip.SubSpan(package::k_presets_subdir.size);
        path_in_zip = path::TrimDirectorySeparatorsStart(path_in_zip);

        auto const folder = path::Directory(path_in_zip, path::Format::Posix).ValueOr(""_s);
        auto const preset_name = path::FilenameWithoutExtension(path_in_zip);

        auto preset = arena.New<PackageInfo::PresetName>(PackageInfo::PresetName {
            .name = arena.Clone(preset_name),
            .next = nullptr,
        });

        auto found = info.preset_folders.FindOrInsertGrowIfNeeded(arena, folder, preset);
        if (found.inserted) found.element.key = arena.Clone(folder);
        if (!found.inserted) SinglyLinkedListPrepend(found.element.data, preset);
    }

    if (auto const outcome = DecodeFromMemory(file_data, StateSource::PresetFile); outcome.HasValue()) {
        auto const& state = outcome.Value();
        state.metadata.tags.ForEachSetBit([&](usize bit) {
            auto const tag_type = (TagType)bit;
            auto const tag_name = GetTagInfo(tag_type).name;
            auto tag_result = info.preset_tags.FindOrInsertGrowIfNeeded(
                arena,
                tag_name,
                PackageInfo::TagCount {.group = GroupOfTag(tag_type), .count = 0});
            if (tag_result.inserted) tag_result.element.key = arena.Clone(tag_name);
            ++tag_result.element.data.count;
        });
    }
}

static ErrorCodeOr<void> WriteTagGroups(json::WriteContext& json,
                                        String key,
                                        OrderedHashTable<String, PackageInfo::TagCount> const& tags,
                                        ArenaAllocator& scratch) {
    if (tags.size == 0) return k_success;

    struct Entry {
        String name;
        u32 count;
    };

    TRY(json::WriteKeyArrayBegin(json, key));
    for (auto const group : EnumIterator<TagCategoryGroup>()) {
        DynamicArray<Entry> group_tags {scratch};
        for (auto const& [name, tc, _] : tags)
            if (tc.group == group) dyn::Append(group_tags, Entry {name, tc.count});
        if (group_tags.size == 0) continue;

        Sort(group_tags, [](Entry const& a, Entry const& b) { return a.count > b.count; });

        TRY(json::WriteObjectBegin(json));
        TRY(json::WriteKeyValue(json, "label", GroupLabel(group)));
        TRY(json::WriteKeyArrayBegin(json, "tags"));
        for (auto const& t : group_tags) {
            TRY(json::WriteObjectBegin(json));
            TRY(json::WriteKeyValue(json, "name", t.name));
            TRY(json::WriteKeyValue(json, "count", t.count));
            TRY(json::WriteObjectEnd(json));
        }
        TRY(json::WriteArrayEnd(json));
        TRY(json::WriteObjectEnd(json));
    }
    TRY(json::WriteArrayEnd(json));
    return k_success;
}

static ErrorCodeOr<String> ToJson(PackageInfo const& info, ArenaAllocator& arena) {
    ArenaAllocator scratch {PageAllocator::Instance()};
    DynamicArray<char> json_buffer {arena};
    json::WriteContext json {
        .out = dyn::WriterFor(json_buffer),
    };

    TRY(json::WriteObjectBegin(json));

    TRY(json::WriteKeyValue(json, "schema_version", 2));
    TRY(json::WriteKeyValue(json, "name", info.name));
    TRY(json::WriteKeyValue(json, "size", info.package_size));

    if (info.libraries.size) {
        TRY(json::WriteKeyArrayBegin(json, "libraries"));
        for (auto const& [lib_id, lib, _] : info.libraries) {
            TRY(json::WriteObjectBegin(json));
            TRY(json::WriteKeyValue(json, "name", lib.name));
            TRY(json::WriteKeyValue(json, "file_format", ({
                                        String s;
                                        switch (lib.file_format) {
                                            case sample_lib::FileFormat::Lua: s = "Floe"_s; break;
                                            case sample_lib::FileFormat::Mdata: s = "Mirage"_s; break;
                                        }
                                        s;
                                    })));
            if (lib.instruments_by_folder.size) {
                TRY(json::WriteKeyArrayBegin(json, "folders"));
                for (auto const& [folder, inst_list, _] : lib.instruments_by_folder) {
                    TRY(json::WriteObjectBegin(json));
                    TRY(json::WriteKeyValue(json, "name", folder));
                    TRY(json::WriteKeyArrayBegin(json, "instruments"));
                    for (auto i = inst_list; i; i = i->next) {
                        TRY(json::WriteObjectBegin(json));
                        TRY(json::WriteKeyValue(json, "name", i->name));
                        if (i->description && i->description->size)
                            TRY(json::WriteKeyValue(json, "description", *i->description));
                        TRY(json::WriteObjectEnd(json));
                    }
                    TRY(json::WriteArrayEnd(json));
                    TRY(json::WriteObjectEnd(json));
                }
                TRY(json::WriteArrayEnd(json));
            }
            TRY(json::WriteObjectEnd(json));
        }
        TRY(json::WriteArrayEnd(json));
    }

    if (info.preset_folders.size) {
        TRY(json::WriteKeyArrayBegin(json, "presets"));
        for (auto const& [folder, preset_list, _] : info.preset_folders) {
            TRY(json::WriteObjectBegin(json));
            TRY(json::WriteKeyValue(json, "folder", folder));
            TRY(json::WriteKeyArrayBegin(json, "names"));
            for (auto p = preset_list; p; p = p->next)
                TRY(json::WriteValue(json, p->name));
            TRY(json::WriteArrayEnd(json));
            TRY(json::WriteObjectEnd(json));
        }
        TRY(json::WriteArrayEnd(json));
    }

    TRY(WriteTagGroups(json, "instrument_tags"_s, info.instrument_tags, scratch));
    TRY(WriteTagGroups(json, "preset_tags"_s, info.preset_tags, scratch));

    TRY(json::WriteObjectEnd(json));

    return json_buffer.ToOwnedSpan();
}

static void PrintSummary(PackageInfo const& info, ArenaAllocator& arena) {
    auto const out = StdStream::Err;
    StdPrintF(out, "\nPackage: {} ({})\n", info.name, fmt::PrettyFileSize((f64)info.package_size));

    if (info.libraries.size) {
        StdPrintF(out, "  {}/\n", package::k_libraries_subdir);
        for (auto const& [lib_id, lib, _] : info.libraries) {
            usize num_insts = 0;
            for (auto const& [folder, inst_list, _2] : lib.instruments_by_folder)
                for (auto i = inst_list; i; i = i->next)
                    ++num_insts;
            StdPrintF(out, "    {} - {}/ ({} instruments)\n", lib.author, lib.name, num_insts);
        }
    }

    if (info.preset_folders.size) {
        OrderedHashTable<String, u32> top_level;
        for (auto const& [folder, preset_list, _] : info.preset_folders) {
            u32 count = 0;
            for (auto p = preset_list; p; p = p->next) ++count;
            auto top = folder;
            if (auto const slash = Find(folder, '/')) top = folder.SubSpan(0, *slash);
            auto r = top_level.FindOrInsertGrowIfNeeded(arena, top, 0);
            if (r.inserted) r.element.key = top;
            r.element.data += count;
        }
        StdPrintF(out, "  {}/\n", package::k_presets_subdir);
        for (auto const& [folder, count, _] : top_level)
            StdPrintF(out, "    {}/ ({} presets)\n", folder, count);
    }

    for (auto const& [name, _] : info.extra_files)
        StdPrintF(out, "  {}\n", name);
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = true,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaAllocator scratch {PageAllocator::Instance()};
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

    auto const omit_unreferenced = cli_args[ToInt(PackagerCliArgId::OmitUnreferenced)].was_provided;

    sample_lib::Library* lib_for_package_name = nullptr;

    for (auto const path : cli_args[ToInt(PackagerCliArgId::LibraryFolder)].values) {
        auto const library_path = TRY_OR(AbsolutePath(scratch, path), {
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
            auto outcome = sample_lib::ReadMdata(reader, library_path, arena, scratch);
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

            AddLibrary(package_info, *lib, arena, scratch);

            TRY_OR(package::WriterAddLibrary(package, *lib, scratch, program_name), {
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

        AddLibrary(package_info, *lib, arena, scratch);

        if (!sample_lib::CheckAllReferencedFilesExist(*lib, StdWriter(StdStream::Err))) {
            StdPrintF(StdStream::Err,
                      "Error: library {} has missing files, cannot create package\n",
                      lib->name);
            return ErrorCode {CommonError::NotFound};
        }

        auto const portable = TRY_OR(ValidateSourcePathsPortability(library_path, scratch), {
            StdPrintF(StdStream::Err,
                      "Error: failed to scan library folder for path validation: {}\n",
                      error);
            return error;
        });
        if (!portable) {
            StdPrintF(StdStream::Err,
                      "Error: library {} contains paths that aren't portable across Windows/macOS/Linux; "
                      "rename the offending files and retry\n",
                      lib->name);
            return ErrorCode {CommonError::InvalidFileFormat};
        }

        auto const referenced = BuildLibraryReferencedSet(*lib, paths, scratch);

        TRY_OR(ReportUnreferencedLibraryFiles(*lib, library_path, referenced, omit_unreferenced, scratch), {
            StdPrintF(StdStream::Err,
                      "Warning: failed to scan library folder for unreferenced files: {}\n",
                      error);
        });

        auto const include_pred = [&](String subpath) {
            return IsLibraryFileReferenced(subpath, referenced);
        };
        auto const library_folder_in_zip =
            TRY_OR(package::WriterAddLibrary(package,
                                             *lib,
                                             scratch,
                                             program_name,
                                             omit_unreferenced ? FunctionRef<bool(String)> {include_pred}
                                                               : FunctionRef<bool(String)> {}),
                   {
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
            StdPrintF(StdStream::Err,
                      "Error: auto-generated {} already exists - remove it\n",
                      about_doc.filename_in_zip);
            return ErrorCode {FilesystemError::PathAlreadyExists};
        }
    }

    for (auto const p : cli_args[ToInt(PackagerCliArgId::PresetFolder)].values) {
        auto const preset_folder = TRY_OR(AbsolutePath(scratch, p), {
            StdPrintF(StdStream::Err, "Error: failed to resolve preset folder '{}'\n", p);
            return error;
        });

        TRY_OR(ReportNonPresetFilesInPresetFolder(preset_folder, omit_unreferenced, scratch), {
            StdPrintF(StdStream::Err, "Warning: failed to scan preset folder for extra files: {}\n", error);
        });

        auto const portable = TRY_OR(ValidateSourcePathsPortability(preset_folder, scratch), {
            StdPrintF(StdStream::Err, "Error: failed to scan preset folder for path validation: {}\n", error);
            return error;
        });
        if (!portable) {
            StdPrintF(StdStream::Err,
                      "Error: preset folder {} contains paths that aren't portable across "
                      "Windows/macOS/Linux; rename the offending files and retry\n",
                      preset_folder);
            return ErrorCode {CommonError::InvalidFileFormat};
        }
        // We add presets to the package even when generating package info only because the ZIP structure
        // is convenient to read the filename from.
        TRY_OR(package::WriterAddPresetsFolder(
                   package,
                   preset_folder,
                   scratch,
                   program_name,
                   [&](String path, Span<u8 const> file_data) {
                       AddPresetIfNeeded(package_info, path, arena, file_data);
                   },
                   omit_unreferenced ? FunctionRef<bool(String)> {IsPresetFolderFileNeeded}
                                     : FunctionRef<bool(String)> {}),
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
            StdPrintF(StdStream::Err,
                      "Error: auto-generated {} already exists - remove it\n",
                      k_installation_doc_name);
            return ErrorCode {FilesystemError::PathAlreadyExists};
        }
        auto r = package_info.extra_files.FindOrInsertGrowIfNeeded(arena, k_installation_doc_name);
        if (r.inserted) r.element.key = arena.Clone(k_installation_doc_name);
    }

    // We do input packages last because we prioritise file from libraries/presets. We ignore files from
    // packages if they already exist from the libraries/presets.
    for (auto const path : cli_args[ToInt(PackagerCliArgId::InputPackages)].values) {
        auto const package_path = TRY_OR(AbsolutePath(scratch, path), {
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

        {
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
                    AddLibrary(package_info, *component->library, arena, scratch);
            }
        }

        TRY_OR(package::WriterAddPackage(package,
                                         input_package,
                                         scratch,
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

        auto const package_name = PackageName(arena, lib_for_package_name, cli_args);
        package_info.name = package_name;

        Array<u8, encrypted_package::k_key_size> package_key {};
        bool const should_encrypt = cli_args[ToInt(PackagerCliArgId::Encrypt)].was_provided;
        if (should_encrypt) CryptoRandomBytes(package_key.data, encrypted_package::k_key_size);

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

            if (should_encrypt) {
                auto const encrypted = TRY_OR(encrypted_package::Encrypt(zip_data, package_key, arena), {
                    StdPrintF(StdStream::Err, "Error: failed to encrypt package: {}\n", error);
                    return error;
                });

                auto enc_name = fmt::Format(arena,
                                            "{}{}",
                                            path::FilenameWithoutExtension(package_name),
                                            encrypted_package::k_file_extension);
                auto const enc_path = path::Join(arena, Array {folder, String(enc_name)});
                TRY_OR(WriteFile(enc_path, encrypted), {
                    StdPrintF(StdStream::Err,
                              "Error: failed to write encrypted package to '{}': {}\n",
                              enc_path,
                              error);
                    return error;
                });
                StdPrintF(StdStream::Err, "Wrote encrypted package: {}\n", enc_path);
                StdPrintF(StdStream::Err,
                          "Package key written to stdout - store it securely, it is needed to sign "
                          "license keys.\n");
                for (auto const byte : package_key)
                    StdPrintF(StdStream::Out, "{02x}", byte);
                StdPrintF(StdStream::Out, "\n");
            } else {
                auto const package_path = path::Join(arena, Array {folder, package_name});
                TRY_OR(WriteFile(package_path, zip_data), {
                    StdPrintF(StdStream::Err,
                              "Error: failed to write package file to '{}': {}\n",
                              package_path,
                              error);
                    return error;
                });
                StdPrintF(StdStream::Err, "Wrote package: {}\n", package_path);
            }
        }

        package_info.package_size = zip_data.size;
    }

    if (generate_package_info) {
        auto const json = TRY_OR(ToJson(package_info, arena), {
            StdPrintF(StdStream::Err, "Error: failed to write package info JSON: {}\n", error);
            return error;
        });

        auto const output_json_path = cli_args[ToInt(PackagerCliArgId::OutputPackageInfoJsonFile)].values[0];
        if (output_json_path == "-"_s) {
            StdPrintF(StdStream::Out, "{}\n", json);
        } else {
            TRY_OR(WriteFile(output_json_path, json), {
                StdPrintF(StdStream::Err,
                          "Error: failed to write package info JSON file to '{}': {}\n",
                          output_json_path,
                          error);
                return error;
            });
            StdPrintF(StdStream::Err, "Wrote package info JSON: {}\n", output_json_path);
        }
    }

    PrintSummary(package_info, arena);

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
