// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/sample_library/library_dump.hpp"
#include "common_infrastructure/sample_library/library_id_cache.hpp"
#include "common_infrastructure/state/instrument.hpp"
#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "preset_tool_lua_codec.hpp"

// IMPROVE: export a Lua LSP def file for the preset table.

enum class CliArgId : u8 {
    ScriptFile,
    Raw,
    ReadOnly,
    PrintExample,
    Count,
};

auto constexpr k_command_line_args_defs = MakeCommandLineArgDefs<CliArgId>({
    {
        .id = (u32)CliArgId::ScriptFile,
        .key = "script-file",
        .description =
            "Path to the script file to edit. If not provided, the preset file will be printed to stdout.\n"
            "In the script, you have access to a global 'preset'. Modify this global and the changes will\n"
            "be saved to the file. Run this tool without a script file to see the format of 'preset'.\n"
            "param_values are keyed by stable id_string (e.g. 'fx.distortion.drive', 'l1.volume').\n"
            "Also available: the global 'preset_path' (absolute path\n"
            "of the preset currently being processed), 'default_preset' (a default-initialised preset\n"
            "table, useful as a reference for default values), and inspect_library(path) which returns\n"
            "a table describing the given floe.lua file (same shape as library-inspector\n"
            "--format=lua).\n",
        .value_type = "path",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)CliArgId::Raw,
        .key = "raw",
        .description = "Load the preset without applying legacy→modern parameter adaptation. The printed\n"
                       "values reflect what is actually stored in the file. Cannot be combined with\n"
                       "--script-file (saving a raw-loaded preset would corrupt it).\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)CliArgId::ReadOnly,
        .key = "read-only",
        .description = "Never write the preset file, even if --script-file modifies the 'preset' global.\n"
                       "Useful for inspection scripts that print or assert.\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)CliArgId::PrintExample,
        .key = "print-example",
        .description =
            "Print a documented example: a default-initialised 'preset' table plus a reference appendix\n"
            "listing available globals, stable id_strings, parameter ranges, and enum integer values. No\n"
            "preset file required. Pipe to a file and use it as a reference when writing --script-file\n"
            "scripts.\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
});

// Cache of serialised library dumps shared across per-preset lua_States. A script that calls
// inspect_library() on the same library for every preset would otherwise repeat the full
// read/parse/serialise pipeline N times.
struct LibraryDumpCache {
    ArenaAllocator arena {PageAllocator::Instance()};
    DynamicHashTable<String, String> entries {arena};
};

constexpr char const* k_library_dump_cache_registry_key = "floe_preset_tool_library_dump_cache";

static void SetLibraryDumpCache(lua_State* lua, LibraryDumpCache* cache) {
    lua_pushlightuserdata(lua, cache);
    lua_setfield(lua, LUA_REGISTRYINDEX, k_library_dump_cache_registry_key);
}

static LibraryDumpCache* GetLibraryDumpCache(lua_State* lua) {
    lua_getfield(lua, LUA_REGISTRYINDEX, k_library_dump_cache_registry_key);
    auto* const p = lua_touserdata(lua, -1);
    lua_pop(lua, 1);
    return (LibraryDumpCache*)p;
}

// Lua-callable: inspect_library(path) -> table. Reads the given floe.lua/.mdata file, runs the shared
// library_dump pipeline to produce a Lua table literal, then loads it back into the caller's lua_State so
// the script sees a normal table (no JSON round-trip). On failure, raises a Lua error.
static int LuaInspectLibrary(lua_State* lua) {
    auto const path_c = luaL_checkstring(lua, 1);
    auto const path = FromNullTerminated(path_c);

    auto* const cache = GetLibraryDumpCache(lua);
    if (cache) {
        if (auto const hit = cache->entries.Find(path)) {
            if (luaL_loadbuffer(lua, hit->data, hit->size, "library") != LUA_OK) return lua_error(lua);
            if (lua_pcall(lua, 0, 1, 0) != LUA_OK) return lua_error(lua);
            return 1;
        }
    }

    ArenaAllocator scratch {PageAllocator::Instance()};
    ArenaAllocator lib_arena {PageAllocator::Instance()};

    auto const format = sample_lib::DetermineFileFormat(path);
    if (!format) return luaL_error(lua, "not a recognised Floe library file: %s", path_c);

    auto file_data = ReadEntireFile(path, scratch);
    if (file_data.HasError()) {
        auto const msg = fmt::Format(scratch, "{}", file_data.Error());
        return luaL_error(lua, "failed to read library: %s", NullTerminated(msg, scratch));
    }

    auto reader = Reader::FromMemory(file_data.Value());
    auto outcome = sample_lib::Read(reader, *format, path, lib_arena, scratch);
    if (outcome.HasError()) {
        auto const msg = fmt::Format(scratch, "{}: {}", outcome.Error().message, outcome.Error().code);
        return luaL_error(lua, "failed to parse library: %s", NullTerminated(msg, scratch));
    }
    auto* lib = outcome.Get<sample_lib::Library*>();

    DynamicArray<char> buf {scratch};
    dyn::AppendSpan(buf, "return "_s);
    library_dump::Context ctx {.out = dyn::WriterFor(buf), .format = library_dump::Format::Lua};
    if (auto const r = library_dump::WriteObjectBegin(ctx); r.HasError())
        return luaL_error(lua, "library_dump write failed");
    if (auto const r = library_dump::Dump(ctx, *lib, scratch); r.HasError())
        return luaL_error(lua, "library_dump emit failed");
    if (auto const r = library_dump::WriteObjectEnd(ctx); r.HasError())
        return luaL_error(lua, "library_dump close failed");

    if (cache) {
        // Clone key and value into the cache's long-lived arena.
        auto const cached_key = cache->arena.Clone(path);
        auto const cached_value = cache->arena.Clone((String) {buf.data, buf.size});
        cache->entries.Insert(cached_key, cached_value);
    }

    if (luaL_loadbuffer(lua, buf.data, buf.size, "library") != LUA_OK) return lua_error(lua);
    if (lua_pcall(lua, 0, 1, 0) != LUA_OK) return lua_error(lua);
    return 1;
}

// Print-only mode serializer. Defined at file scope so it can be reused per preset.
constexpr auto k_lua_print_serializer = R"lua(
local function serializeValue(value, indent)
    indent = indent or 0
    local indentStr = string.rep("  ", indent)

    if type(value) == "table" then
        local result = "{\n"
        local keys = {}

        -- Collect and sort keys for consistent output
        for k in pairs(value) do
            table.insert(keys, k)
        end
        table.sort(keys, function(a, b)
            if type(a) == type(b) then
                if type(a) == "number" then
                    return a < b
                else
                    return tostring(a) < tostring(b)
                end
            else
                return type(a) < type(b)
            end
        end)

        for _, k in ipairs(keys) do
            local v = value[k]
            result = result .. indentStr .. "  "

            -- Format key
            if type(k) == "string" and string.match(k, "^[%a_][%w_]*$") then
                result = result .. k
            else
                result = result .. "[" .. serializeValue(k, 0) .. "]"
            end

            result = result .. " = " .. serializeValue(v, indent + 1) .. ",\n"
        end

        result = result .. indentStr .. "}"
        return result
    elseif type(value) == "string" then
        return string.format("%q", value)
    elseif type(value) == "number" then
        -- Format numbers nicely
        if value == math.floor(value) then
            return tostring(math.floor(value))
        else
            return string.format("%.10g", value)
        end
    else
        return tostring(value)
    end
end

-- Print the preset as Lua code
print("local preset = " .. serializeValue(preset))
print("return preset")
)lua";

// Emit a documented Lua template: header explaining globals + a default preset table + appendix listing
// parameter ranges/defaults and enum integer values. The table itself comes from BuildPresetLuaTable so it
// stays in sync with the codec; the appendix walks the same descriptor table the codec keys on.
static ErrorCodeOr<void> PrintExample(ArenaAllocator& arena) {
    constexpr auto k_header =
        "-- Floe preset script example (generated by preset-tool --print-example).\n"
        "--\n"
        "-- preset-tool runs your Lua script with these globals:\n"
        "--   preset                    table; the current preset (see shape below).\n"
        "--                             Modify it in --script-file mode (without --read-only) to save.\n"
        "--   default_preset            table; a default-initialised preset in the same shape as 'preset'.\n"
        "--                             Useful as a reference or source of default values.\n"
        "--   preset_path               string; absolute path of the preset currently being processed.\n"
        "--   inspect_library(path)     function; returns a table describing a floe.lua file\n"
        "--                             (same shape as library-inspector --format=lua).\n"
        "--\n"
        "-- Value forms in param_values:\n"
        "--   Values are emitted as formatted display strings (\"50 %\", \"-12.0 dB\", \"Sine\"), keyed by\n"
        "--   stable id_string. Reads accept either a formatted string or the underlying projected number\n"
        "--   (e.g. 0.5, -12.0, 440), so scripts can assign whichever is more convenient.\n"
        "--   String reads are permissive: extra precision and either unit are accepted\n"
        "--   (\"1.567 s\" or \"1567 ms\" both work even though writes emit \"1.6 s\").\n"
        "--\n"
        "-- Round-trip note: display formats truncate precision, so saving a preset back will alter the\n"
        "-- stored numeric value of many params (snapped to the display grid). This is intentional and\n"
        "-- does not change perceived audio - the truncation sits well below audible thresholds.\n"
        "--\n"
        "-- Below: a default-initialised preset, followed by reference appendices.\n"
        "\n";

    StdPrintF(StdStream::Out, "{}", k_header);

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };
    luaL_openlibs(lua);
    BuildPresetLuaTable(lua, DefaultStateSnapshot(), {.example_mode = true});

    if (auto const r = luaL_loadbuffer(lua,
                                       k_lua_print_serializer,
                                       NullTerminatedSize(k_lua_print_serializer),
                                       "schema-serializer");
        r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to load serializer: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    if (auto const r = lua_pcall(lua, 0, 0, 0); r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: serializer failed: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    DynamicArray<char> buf {arena};
    auto w = dyn::WriterFor(buf);

    TRY(fmt::AppendLine(w, ""));
    TRY(fmt::AppendLine(w, "-- ====================================================================="));
    TRY(fmt::AppendLine(w, "-- Parameter reference (id_string : default : range)"));
    TRY(fmt::AppendLine(w, "-- ====================================================================="));
    TRY(fmt::AppendLine(w, "-- Defaults and ranges are shown as the underlying projected number, with the"));
    TRY(fmt::AppendLine(w, "-- formatted display equivalent in parens where one exists. Params marked"));
    TRY(fmt::AppendLine(w, "-- [legacy] are kept for old-file compatibility; avoid setting them."));
    TRY(fmt::AppendLine(w, "--"));

    for (auto const i : Range<u16>(k_num_parameters)) {
        auto const& d = k_param_descriptors[i];
        auto const proj_min = d.ProjectValue(d.linear_range.min);
        auto const proj_max = d.ProjectValue(d.linear_range.max);
        auto const proj_def = d.DefaultProjectedValue();

        auto const pretty_def = d.LinearValueToString(d.default_linear_value);
        auto const pretty_min = d.LinearValueToString(d.linear_range.min);
        auto const pretty_max = d.LinearValueToString(d.linear_range.max);

        DynamicArrayBounded<char, 256> line;
        fmt::Append(line, "-- {} default {g}", d.id_string, proj_def);
        if (pretty_def) fmt::Append(line, " (\"{}\")", *pretty_def);
        fmt::Append(line, " range {g}..{g}", proj_min, proj_max);
        if (pretty_min && pretty_max) fmt::Append(line, " (\"{}\"..\"{}\")", *pretty_min, *pretty_max);
        if (d.flags.legacy) fmt::Append(line, " [legacy]");
        TRY(fmt::AppendLine(w, "{}", (String)line));
    }

    TRY(fmt::AppendLine(w, ""));
    TRY(fmt::AppendLine(w, "-- ====================================================================="));
    TRY(fmt::AppendLine(w, "-- Enum integer values"));
    TRY(fmt::AppendLine(w, "-- ====================================================================="));
    TRY(fmt::AppendLine(w, "--"));
    TRY(fmt::AppendLine(w, "-- inst_ids[layer].type (InstrumentType):"));
    TRY(fmt::AppendLine(w, "--   {} = None", (int)InstrumentType::None));
    TRY(fmt::AppendLine(w,
                        "--   {} = WaveformSynth   (also set .waveform_type = WaveformType integer)",
                        (int)InstrumentType::WaveformSynth));
    TRY(fmt::AppendLine(w,
                        "--   {} = Sampler         (also set .library_id = \"Author/Library\" or "
                        "hashed int, .instrument_id = \"id\")",
                        (int)InstrumentType::Sampler));
    TRY(fmt::AppendLine(w, "--"));
    TRY(fmt::AppendLine(w, "-- inst_ids[layer].waveform_type (WaveformType):"));
    for (auto const wt : Range<u16>((u16)WaveformType::Count))
        TRY(fmt::AppendLine(w, "--   {} = {}", wt, k_waveform_type_names[wt]));
    TRY(fmt::AppendLine(w, "--"));
    TRY(fmt::AppendLine(w, "-- fx_order[i] / fx_visible[i] integer (EffectType):"));
    for (auto const fx : Range<u16>(k_num_effect_types))
        TRY(fmt::AppendLine(w, "--   {} = {}", fx, k_effect_info[fx].name));
    TRY(fmt::AppendLine(w, "--"));
    TRY(fmt::AppendLine(w, "-- metadata.tags is a list of tag-name strings (see Floe's tag list)."));
    TRY(fmt::AppendLine(w, "-- ir_id is nil for no IR, or a table {{library_id = ..., ir_id = \"...\"}}."));

    StdPrintF(StdStream::Out, "{}", (String)buf);
    return k_success;
}

struct ProcessPresetOptions {
    String preset_path;
    String script_source;
    String script_name;
    LibraryDumpCache* library_dump_cache;
    bool have_script;
    bool raw;
    bool read_only;
    bool print_path_header;
};

static ErrorCodeOr<void> ProcessPreset(ArenaAllocator& arena, ProcessPresetOptions const& opts) {
    auto const preset_state = TRY(LoadPresetFile(opts.preset_path, arena, opts.raw));

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    SetLibraryDumpCache(lua, opts.library_dump_cache);
    BuildPresetLuaTable(lua, preset_state, {});
    BuildPresetLuaTable(lua, DefaultStateSnapshot(), {.global_name = "default_preset"});

    luaL_openlibs(lua);
    lua_register(lua, "inspect_library", LuaInspectLibrary);
    lua_pushlstring(lua, opts.preset_path.data, opts.preset_path.size);
    lua_setglobal(lua, "preset_path");

    if (opts.print_path_header && !opts.have_script)
        StdPrintF(StdStream::Out, "-- file: {}\n", opts.preset_path);

    if (auto const r = luaL_loadbuffer(lua,
                                       opts.script_source.data,
                                       opts.script_source.size,
                                       NullTerminated(opts.script_name, arena));
        r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to load script: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    if (auto const r = lua_pcall(lua, 0, LUA_MULTRET, 0); r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: script execution failed: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    if (!opts.have_script || opts.read_only) return k_success;

    lua_getglobal(lua, "preset");
    if (!lua_istable(lua, -1)) {
        StdPrintF(StdStream::Err, "Error: preset global is not a table\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    auto modified_state = preset_state;
    ExtractPresetFromLuaTable(lua, -1, modified_state);

    if (modified_state == preset_state) return k_success;

    auto const temp_dir = TRY(TemporaryDirectoryOnSameFilesystemAs(opts.preset_path, arena));
    DEFER { auto _ = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

    auto seed = RandomSeed();
    auto const out_path =
        path::Join(arena,
                   Array {(String)temp_dir, UniqueFilename("preset- ", FLOE_PRESET_FILE_EXTENSION, seed)});
    TRY(SavePresetFile(out_path, modified_state, false));

    auto new_preset_path = opts.preset_path;
    if (auto const ext = path::Extension(opts.preset_path); ext != FLOE_PRESET_FILE_EXTENSION) {
        new_preset_path.RemoveSuffix(ext.size);
        new_preset_path = fmt::Join(arena, Array {(String)new_preset_path, FLOE_PRESET_FILE_EXTENSION});
    }

    TRY(Rename(out_path, new_preset_path));
    return k_success;
}

// Expand a positional path: if a file, append it as-is (no extension check; the user named it explicitly).
// If a directory, recursively append every file whose extension matches a known preset format (Floe or
// any .mirage-* variant). We can't express that with the single-string wildcard so scan all files and
// filter via PresetFormatFromPath.
static ErrorCodeOr<void>
CollectPresetFiles(ArenaAllocator& arena, String input_path, DynamicArray<String>& out_files) {
    auto const abs = TRY(AbsolutePath(arena, input_path));
    auto const ft = TRY(GetFileType(abs));
    switch (ft) {
        case FileType::File: dyn::Append(out_files, (String)abs); break;
        case FileType::Directory: {
            auto entries = TRY(FindEntriesInFolder(arena,
                                                   abs,
                                                   {
                                                       .options = {.wildcard = "*"},
                                                       .recursive = true,
                                                       .only_file_type = FileType::File,
                                                   }));
            for (auto const& e : entries) {
                if (!PresetFormatFromPath(e.subpath)) continue;
                dyn::Append(out_files, (String)path::Join(arena, Array {(String)abs, (String)e.subpath}));
            }
            break;
        }
    }
    return k_success;
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = false,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    Span<String> positional_paths {};
    auto const cli_args = TRY(ParseCommandLineArgsStandard(
        arena,
        args,
        k_command_line_args_defs,
        {
            .handle_help_option = true,
            .print_usage_on_error = true,
            .description = "Edit one or more presets using a Lua script.\n"
                           "\n"
                           "Library names are not stored in preset files (only "
                           "their hashed IDs are). On startup, this tool loads "
                           "the library name cache (library_id_cache.bin) "
                           "written by Floe's library scanner so libraries you "
                           "have open in Floe resolve to names instead of raw "
                           "integers. Cache location:\n"
                           "  Linux:   ~/.local/state/Floe/library_id_cache.bin\n"
                           "  macOS:   ~/Library/Application Support/Floe/"
                           "library_id_cache.bin\n"
                           "  Windows: %APPDATA%\\Floe\\library_id_cache.bin\n"
                           "Open Floe once after installing a new library to "
                           "refresh the cache; delete the cache file to force a "
                           "full rescan on next launch.",
            .version = FLOE_VERSION_STRING,
            .positionals =
                {
                    .name = "preset-path",
                    .description =
                        "Preset file or directory. Directories are scanned recursively for *" FLOE_PRESET_FILE_EXTENSION
                        " files.",
                    .out = &positional_paths,
                },
        }));

    if (cli_args[ToInt(CliArgId::PrintExample)].was_provided) {
        if (auto const r = PrintExample(arena); r.HasError()) return r.Error();
        return 0;
    }

    if (positional_paths.size == 0) {
        StdPrintF(StdStream::Err, "Error: at least one preset file or directory must be provided\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    auto const raw = cli_args[ToInt(CliArgId::Raw)].was_provided;
    if (raw && cli_args[ToInt(CliArgId::ScriptFile)].was_provided) {
        StdPrintF(StdStream::Err, "Error: --raw cannot be combined with --script-file\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    LoadLibraryIdCache(arena);

    DynamicArray<String> preset_files {arena};
    for (auto const& p : positional_paths) {
        if (auto const r = CollectPresetFiles(arena, p, preset_files); r.HasError()) {
            StdPrintF(StdStream::Err, "Error: cannot read '{}': {}\n", p, r.Error());
            return r.Error();
        }
    }

    if (preset_files.size == 0) {
        StdPrintF(StdStream::Err, "Error: no preset files found in the given paths\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    String script_source {};
    String script_name {};
    auto const have_script = cli_args[ToInt(CliArgId::ScriptFile)].was_provided;
    if (have_script) {
        auto const script_path =
            TRY_OR(AbsolutePath(arena, cli_args[ToInt(CliArgId::ScriptFile)].values[0]), {
                StdPrintF(StdStream::Err, "Error: failed to resolve script path\n");
                return error;
            });

        auto const script_file_data = TRY_OR(ReadEntireFile(script_path, arena), {
            StdPrintF(StdStream::Err, "Error: failed to read script file: {}\n", error);
            return error;
        });
        script_source = script_file_data;
        script_name = script_path;
    } else {
        script_source = FromNullTerminated(k_lua_print_serializer);
        script_name = "print-serializer"_s;
    }

    auto const read_only = cli_args[ToInt(CliArgId::ReadOnly)].was_provided;
    auto const print_path_header = preset_files.size > 1;

    LibraryDumpCache library_dump_cache {};

    int exit_code = 0;
    for (auto const& preset_path : preset_files) {
        ArenaAllocatorWithInlineStorage<4000> scratch {PageAllocator::Instance()};
        auto const r = ProcessPreset(scratch,
                                     {
                                         .preset_path = preset_path,
                                         .script_source = script_source,
                                         .script_name = script_name,
                                         .library_dump_cache = &library_dump_cache,
                                         .have_script = have_script,
                                         .raw = raw,
                                         .read_only = read_only,
                                         .print_path_header = print_path_header,
                                     });
        if (r.HasError()) {
            StdPrintF(StdStream::Err, "Error processing '{}': {}\n", preset_path, r.Error());
            exit_code = 1;
        }
    }

    return exit_code;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
