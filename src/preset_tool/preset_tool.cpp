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
    PrintShape,
    PrintParamsJson,
    Count,
};

auto constexpr k_command_line_args_defs = MakeCommandLineArgDefs<CliArgId>({
    {
        .id = (u32)CliArgId::ScriptFile,
        .key = "script-file",
        .description =
            "Path to a Lua script to run for each preset. Without --script-file the preset is\n"
            "serialised to stdout (no writes). The script sees these globals:\n"
            "  preset                    table - the current preset. Modify it to save (unless\n"
            "                            --read-only). Run --print-shape for its full structure.\n"
            "  default_preset            table - a default-initialised preset in the same shape.\n"
            "                            Useful as a reference or source of default values.\n"
            "  preset_path               string - absolute path of the preset being processed.\n"
            "  inspect_library(path)     function - returns a table describing a floe.lua file\n"
            "                            (same shape as library-inspector --format=lua).\n"
            "Use --print-shape for a description of the preset table, and --print-params-json for\n"
            "the parameter id_strings, ranges, and enum integer values.\n",
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
        .id = (u32)CliArgId::PrintShape,
        .key = "print-shape",
        .description = "Print a description of every field in the 'preset' Lua table (types, ranges,\n"
                       "encoding notes). No preset file required.\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
    {
        .id = (u32)CliArgId::PrintParamsJson,
        .key = "print-params-json",
        .description = "Print a JSON catalog of every parameter (id_string, default, range - both projected\n"
                       "numeric and formatted display forms, legacy flag) plus the enum integer tables\n"
                       "(InstrumentType, WaveformType, EffectType). Pipe into jq to query. No preset file\n"
                       "required.\n",
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

static ErrorCodeOr<void> PrintShape(ArenaAllocator& arena) {
    constexpr auto k_header =
        "preset-tool: 'preset' Lua table shape.\n"
        "\n"
        "Pass --script-file to run a Lua script that modifies the global 'preset' table. The\n"
        "script also sees 'default_preset' (a default-initialised preset of the same shape) and\n"
        "'preset_path' (the current file), and can call inspect_library(path).\n"
        "\n"
        "Value encoding for param_values:\n"
        "  Writes emit formatted display strings (\"50 %\", \"-12.0 dB\", \"Sine\") keyed by stable\n"
        "  id_string. Reads accept either a formatted string or the underlying projected number\n"
        "  (e.g. 0.5, -12.0, 440), so scripts can assign whichever is more convenient. String\n"
        "  reads are permissive: extra precision and either unit are accepted (\"1.567 s\" or\n"
        "  \"1567 ms\" both work even though writes emit \"1.6 s\").\n"
        "\n"
        "Round-trip note: display formats truncate precision, so saving a preset back will alter\n"
        "the stored numeric value of many params (snapped to the display grid). This is\n"
        "intentional and does not change perceived audio - the truncation sits well below audible\n"
        "thresholds.\n"
        "\n";

    DynamicArray<char> buf {arena};
    dyn::AppendSpan(buf, FromNullTerminated(k_header));
    AppendPresetLuaTableShape(buf);
    StdPrintF(StdStream::Out, "{}", (String)buf);
    return k_success;
}

static ErrorCodeOr<void> PrintParamsJson(ArenaAllocator& arena) {
    DynamicArray<char> buf {arena};
    TRY(WriteParamsJson(dyn::WriterFor(buf)));
    dyn::Append(buf, '\n');
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

    if (cli_args[ToInt(CliArgId::PrintShape)].was_provided) {
        TRY(PrintShape(arena));
        return 0;
    }

    if (cli_args[ToInt(CliArgId::PrintParamsJson)].was_provided) {
        TRY(PrintParamsJson(arena));
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
