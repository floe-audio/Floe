// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library_inspector.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/sample_library/library_dump.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

// Inspects a Floe sample library (Lua or MDATA) and dumps an extensive structured document to stdout for AI
// agents to consume. The library payload itself comes from `library_dump::Dump`; this tool adds the
// inspection-specific keys: captured Lua print() output, parse errors, and orphan/missing sample scanning.

namespace ld = library_dump;
using CliArgId = LibraryInspectorCliArgId;

// Locates the library file. If `path` points to a file, returns it. If it points to a directory, scans the
// (non-recursive) directory for a Floe Lua file or an MDATA file.
static ErrorCodeOr<String> ResolveLibraryFile(ArenaAllocator& arena, String input_path) {
    auto const abs = TRY(AbsolutePath(arena, input_path));
    auto const info = TRY(GetFileType(abs));
    if (info == FileType::File) return abs;
    if (info != FileType::Directory) return ErrorCode {CommonError::NotFound};

    auto it = TRY(dir_iterator::Create(arena,
                                       abs,
                                       {
                                           .wildcard = "*",
                                           .get_file_size = false,
                                       }));
    DEFER { dir_iterator::Destroy(it); };
    while (auto const entry = TRY(dir_iterator::Next(it, arena))) {
        if (entry->type == FileType::Directory) continue;
        if (sample_lib::FilenameIsFloeLuaFile(entry->subpath) ||
            sample_lib::FilenameIsMdataFile(entry->subpath))
            return dir_iterator::FullPath(it, *entry, arena);
    }
    return ErrorCode {CommonError::NotFound};
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = false,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_library_inspector_command_line_args_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                               .description = k_library_inspector_description,
                                                               .version = FLOE_VERSION_STRING,
                                                           }));

    auto const library_file =
        TRY_OR(ResolveLibraryFile(arena, cli_args[ToInt(CliArgId::LibraryPath)].values[0]), {
            StdPrintF(StdStream::Err, "Error: failed to locate library file: {}\n", error);
            return error;
        });

    auto out_format = ld::Format::Json;
    if (cli_args[ToInt(CliArgId::Format)].was_provided) {
        auto const v = cli_args[ToInt(CliArgId::Format)].values[0];
        if (v == "lua"_s)
            out_format = ld::Format::Lua;
        else if (v != "json"_s) {
            StdPrintF(StdStream::Err, "Error: --format must be 'json' or 'lua'\n");
            return ErrorCode {CommonError::InvalidFileFormat};
        }
    }

    auto const format = sample_lib::DetermineFileFormat(library_file);
    if (!format) {
        StdPrintF(StdStream::Err, "Error: file is not a recognised Floe library format\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    DynamicArray<char> lua_print_buf {arena};
    DynamicArray<char> file_check_buf {arena};
    ArenaAllocator lib_arena {PageAllocator::Instance()};
    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    sample_lib::Library* lib = nullptr;
    Optional<sample_lib::Error> parse_error {};

    if (auto const file_data = ReadEntireFile(library_file, scratch_arena); file_data.HasValue()) {
        auto reader = Reader::FromMemory(file_data.Value());
        sample_lib::Options opts {};
        if (*format == sample_lib::FileFormat::Lua) opts.print_capture = dyn::WriterFor(lua_print_buf);
        auto outcome = sample_lib::Read(reader, *format, library_file, lib_arena, scratch_arena, opts);
        if (outcome.HasError())
            parse_error = outcome.Error();
        else
            lib = outcome.Get<sample_lib::Library*>();
    } else {
        parse_error = sample_lib::Error {file_data.Error(), "failed to read library file"_s};
    }

    if (lib) sample_lib::CheckAllReferencedFilesExist(*lib, dyn::WriterFor(file_check_buf));

    // Build a set of referenced library-relative file paths so we can scan for orphans.
    Set<String> referenced_paths {};
    if (lib) {
        usize const expected = lib->num_instrument_samples + (usize)lib->irs_by_id.size + 4;
        referenced_paths = Set<String>::Create(arena, expected);
        auto const add = [&](String p) { referenced_paths.InsertGrowIfNeeded(arena, p); };
        if (lib->background_image_path) add((String)*lib->background_image_path);
        if (lib->icon_image_path) add((String)*lib->icon_image_path);
        for (auto [_, inst_ptr, _] : lib->insts_by_id)
            for (auto const& region : inst_ptr->regions)
                add((String)region.path);
        for (auto [_, ir_ptr, _] : lib->irs_by_id)
            add((String)ir_ptr->path);
    }

    DynamicArray<String> orphans {arena};
    DynamicArray<String> missing {arena};
    if (lib) {
        auto const lib_dir = path::Directory(library_file);
        if (lib_dir) {
            auto it_or = dir_iterator::RecursiveCreate(arena,
                                                       *lib_dir,
                                                       {
                                                           .wildcard = "*",
                                                           .get_file_size = false,
                                                       });
            if (it_or.HasValue()) {
                auto& it = it_or.Value();
                DEFER { dir_iterator::Destroy(it); };
                for (;;) {
                    auto next = dir_iterator::Next(it, arena);
                    if (next.HasError()) break;
                    auto const& entry = next.Value();
                    if (!entry) break;
                    if (entry->type == FileType::Directory) continue;
                    auto const ext = path::Extension(entry->subpath);
                    if (!IsEqualToCaseInsensitiveAscii(ext, ".flac"_s) &&
                        !IsEqualToCaseInsensitiveAscii(ext, ".wav"_s))
                        continue;
                    auto normalised = arena.Clone(entry->subpath);
                    for (auto& c : normalised)
                        if (c == '\\') c = '/';
                    if (!referenced_paths.Contains((String)normalised))
                        dyn::Append(orphans, (String)normalised);
                }
            }
        }

        if (lib->create_file_reader) {
            for (auto const item : referenced_paths) {
                auto r = lib->create_file_reader(*lib, sample_lib::LibraryPath {item.key});
                if (r.HasError()) dyn::Append(missing, item.key);
            }
        }
    }

    // Buffer the whole document so we can print it as a single write to stdout.
    DynamicArray<char> doc_buf {arena};
    ld::Context ctx {.out = dyn::WriterFor(doc_buf), .format = out_format};
    if (out_format == ld::Format::Lua) dyn::AppendSpan(doc_buf, "return "_s);
    TRY(ld::WriteObjectBegin(ctx));

    TRY(ld::WriteKeyObjectBegin(ctx, "parse"));
    TRY(ld::WriteKeyValue(ctx, "ok", lib != nullptr));
    if (parse_error) {
        TRY(ld::WriteKeyObjectBegin(ctx, "error"));
        TRY(ld::WriteKeyValue(ctx, "code", fmt::Format(arena, "{}", parse_error->code)));
        TRY(ld::WriteKeyValue(ctx, "message", parse_error->message));
        TRY(ld::WriteObjectEnd(ctx));
    } else {
        TRY(ld::WriteKeyNull(ctx, "error"));
    }
    TRY(ld::WriteKeyValue(ctx, "lua_print_output", (String)lua_print_buf));
    TRY(ld::WriteKeyValue(ctx, "file_check_output", (String)file_check_buf));
    TRY(ld::WriteObjectEnd(ctx));

    if (lib) TRY(ld::Dump(ctx, *lib, arena));

    TRY(ld::WriteKeyObjectBegin(ctx, "samples"));
    TRY(ld::WriteKeyValue(ctx, "total_referenced", (s64)referenced_paths.size));
    TRY(ld::WriteKeyArrayBegin(ctx, "orphans"));
    for (auto const& s : orphans)
        TRY(ld::WriteValue(ctx, s));
    TRY(ld::WriteArrayEnd(ctx));
    TRY(ld::WriteKeyArrayBegin(ctx, "missing"));
    for (auto const& s : missing)
        TRY(ld::WriteValue(ctx, s));
    TRY(ld::WriteArrayEnd(ctx));
    TRY(ld::WriteObjectEnd(ctx));

    TRY(ld::WriteObjectEnd(ctx));
    dyn::Append(doc_buf, '\n');

    auto _ = StdPrint(StdStream::Out, doc_buf);

    return parse_error ? 1 : 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
