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

#include "build_resources/embedded_files.h"
#include "preset_tool_lua_codec.hpp"

// IMPROVE: export a Lua LSP def file for the preset table.

enum class Verb : u8 { Inspect, Run, ApplyJson, DocsShape, DocsParams, Count };

enum class InspectArg : u8 { Format, Raw, Count };
constexpr auto k_inspect_arg_defs = MakeCommandLineArgDefs<InspectArg>({
    {
        .id = (u32)InspectArg::Format,
        .key = "format",
        .description = "Output format: 'lua' (default) or 'json'.\n",
        .value_type = "format",
        .required = false,
        .num_values = 1,
    },
    {
        .id = (u32)InspectArg::Raw,
        .key = "raw",
        .description = "Load the preset without applying legacy→modern parameter adaptation. The printed\n"
                       "values reflect what is actually stored in the file.\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
});

enum class RunArg : u8 { ReadOnly, Count };
constexpr auto k_run_arg_defs = MakeCommandLineArgDefs<RunArg>({
    {
        .id = (u32)RunArg::ReadOnly,
        .key = "read-only",
        .description = "Never write the preset file, even if the script modifies the 'preset' global.\n"
                       "Useful for inspection scripts that print or assert.\n",
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

constexpr auto k_lua_json_serializer = "print(json.encode(preset))\n";

// Load the embedded rxi/json.lua module and bind it as a 'json' global so scripts (and the
// --json print serializer) can call json.encode / json.decode.
static ErrorCodeOr<void> RegisterJsonGlobal(lua_State* lua) {
    auto const data = EmbeddedJsonLua();
    if (luaL_loadbuffer(lua, (char const*)data.data, data.size, "json.lua") != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to load json.lua: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    if (lua_pcall(lua, 0, 1, 0) != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to run json.lua: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    lua_setglobal(lua, "json");
    return k_success;
}

static ErrorCodeOr<void> PrintShape(ArenaAllocator& arena) {
    constexpr auto k_header =
        "preset-tool: 'preset' Lua table shape.\n"
        "\n"
        "Use 'run <script>' to run a Lua script that modifies the global 'preset' table. The\n"
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
    StateVersions file_versions {};
    auto const preset_state =
        TRY(LoadPresetFile(opts.preset_path,
                           arena,
                           {.skip_param_adaptation = opts.raw, .out_versions = &file_versions}));

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    SetLibraryDumpCache(lua, opts.library_dump_cache);
    BuildPresetLuaTable(lua, preset_state, {.file_versions = file_versions});
    BuildPresetLuaTable(lua, DefaultStateSnapshot(), {.global_name = "default_preset"});

    luaL_openlibs(lua);
    TRY(RegisterJsonGlobal(lua));
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

    // Skip rewriting only if the script changed nothing AND the file already has an embedded UUID.
    // For legacy files (no embedded UUID) we always re-save: preset_uuid was auto-populated on load to
    // the content-derived fallback, and embedding that into the file stamps a stable UUID without
    // breaking favourites users have already migrated to.
    if (modified_state == preset_state && !PresetFilePredatesEmbeddedUuid(file_versions.state_version))
        return k_success;

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

static ErrorCodeOr<Span<String>> ResolvePresetPathInputs(ArenaAllocator& arena, Span<String> provided) {
    if (provided.size > 0) return provided;
    if (StdinIsTty()) {
        StdPrintF(StdStream::Err,
                  "Error: at least one preset file or directory must be provided "
                  "(as arguments or piped to stdin, one path per line)\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    auto const stdin_data = TRY_OR(ReadAllStdin(arena), {
        StdPrintF(StdStream::Err, "Error: failed to read stdin: {}\n", error);
        return error;
    });
    DynamicArray<String> paths {arena};
    for (auto line : SplitIterator {.whole = stdin_data, .token = '\n'}) {
        if (line.size && Last(line) == '\r') line.RemoveSuffix(1);
        line = WhitespaceStripped(line);
        if (line.size == 0) continue;
        dyn::Append(paths, line);
    }
    if (paths.size == 0) {
        StdPrintF(StdStream::Err, "Error: no preset paths provided on stdin\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    return paths.ToOwnedSpan();
}

static ErrorCodeOr<Span<String>> ExpandPresetFiles(ArenaAllocator& arena, Span<String> inputs) {
    DynamicArray<String> files {arena};
    for (auto const& p : inputs) {
        if (auto const r = CollectPresetFiles(arena, p, files); r.HasError()) {
            StdPrintF(StdStream::Err, "Error: cannot read '{}': {}\n", p, r.Error());
            return r.Error();
        }
    }
    if (files.size == 0) {
        StdPrintF(StdStream::Err, "Error: no preset files found in the given paths\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    return files.ToOwnedSpan();
}

struct RunOverPresetsOptions {
    Span<String> preset_files;
    String script_source;
    String script_name;
    bool have_script;
    bool raw;
    bool read_only;
};

static int RunOverPresets(RunOverPresetsOptions const& opts) {
    LibraryDumpCache library_dump_cache {};
    auto const print_path_header = opts.preset_files.size > 1;
    int exit_code = 0;
    for (auto const& preset_path : opts.preset_files) {
        ArenaAllocatorWithInlineStorage<4000> scratch {PageAllocator::Instance()};
        auto const r = ProcessPreset(scratch,
                                     {
                                         .preset_path = preset_path,
                                         .script_source = opts.script_source,
                                         .script_name = opts.script_name,
                                         .library_dump_cache = &library_dump_cache,
                                         .have_script = opts.have_script,
                                         .raw = opts.raw,
                                         .read_only = opts.read_only,
                                         .print_path_header = print_path_header,
                                     });
        if (r.HasError()) {
            StdPrintF(StdStream::Err, "Error processing '{}': {}\n", preset_path, r.Error());
            exit_code = 1;
        }
    }
    return exit_code;
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = false,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = false}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    Span<String> inspect_positionals {};
    Span<String> run_positionals {};
    Span<String> apply_json_positionals {};

    auto const subcommands = Array {
        CommandLineSubcommand {
            .id = (u32)Verb::Inspect,
            .name = "inspect"_s,
            .description = "Print preset(s) as a Lua table or JSON."_s,
            .args = k_inspect_arg_defs,
            .positionals = {.name = "preset-path"_s,
                            .description =
                                "Preset file or directory (scanned recursively). If omitted, one path "
                                "per line is read from stdin."_s,
                            .out = &inspect_positionals},
        },
        CommandLineSubcommand {
            .id = (u32)Verb::Run,
            .name = "run"_s,
            .description = "Run a Lua script over preset(s) to transform them in place."_s,
            .args = k_run_arg_defs,
            .positionals =
                {.name = "lua-script"_s,
                 .description =
                     "Lua script path, followed by preset files or directories (scanned recursively). "
                     "If no preset paths are given, one path per line is read from stdin. The script "
                     "sees these globals: 'preset' (mutable), 'default_preset', 'preset_path', "
                     "'inspect_library(path)', 'json'. Run 'docs-shape' for the table structure."_s,
                 .min_count = 1,
                 .out = &run_positionals},
        },
        CommandLineSubcommand {
            .id = (u32)Verb::ApplyJson,
            .name = "apply-json"_s,
            .description = "Overwrite a preset file from a JSON document read on stdin (same shape as "
                           "'inspect --format=json')."_s,
            .positionals = {.name = "preset-path"_s,
                            .description = "Target preset file. Overwritten if it exists, created otherwise. "
                                           "Any unknown JSON fields are ignored; missing fields keep their "
                                           "default values."_s,
                            .min_count = 1,
                            .max_count = 1,
                            .out = &apply_json_positionals},
        },
        CommandLineSubcommand {
            .id = (u32)Verb::DocsShape,
            .name = "docs-shape"_s,
            .description =
                "Reference: describe every field in the 'preset' Lua table seen by 'run' scripts."_s,
        },
        CommandLineSubcommand {
            .id = (u32)Verb::DocsParams,
            .name = "docs-params"_s,
            .description = "Reference: JSON catalog of every parameter (id_string, default, range, enums)."_s,
        },
    };

    auto const parsed = TRY(ParseCommandLineSubcommandsStandard(
        arena,
        args,
        subcommands,
        {
            .description =
                "Inspect and transform Floe preset files.\n"
                "\n"
                "Library names are not stored in preset files (only their hashed IDs are). On startup, "
                "this tool loads the library name cache (library_id_cache.bin) written by Floe's "
                "library scanner so libraries you have open in Floe resolve to names instead of raw "
                "integers. Cache location:\n"
                "  Linux:   ~/.local/state/Floe/library_id_cache.bin\n"
                "  macOS:   ~/Library/Application Support/Floe/library_id_cache.bin\n"
                "  Windows: %APPDATA%\\Floe\\library_id_cache.bin\n"
                "Open Floe once after installing a new library to refresh the cache; delete the cache "
                "file to force a full rescan on next launch.",
            .version = FLOE_VERSION_STRING,
        }));

    switch ((Verb)parsed.id) {
        case Verb::DocsShape: {
            TRY(PrintShape(arena));
            return 0;
        }
        case Verb::DocsParams: {
            TRY(PrintParamsJson(arena));
            return 0;
        }
        case Verb::Inspect: {
            auto const path_inputs = TRY(ResolvePresetPathInputs(arena, inspect_positionals));

            String script_source = FromNullTerminated(k_lua_print_serializer);
            String script_name = "print-serializer"_s;
            if (auto const& fmt_arg = parsed.args[ToInt(InspectArg::Format)]; fmt_arg.was_provided) {
                if (fmt_arg.values[0] == "json"_s) {
                    script_source = FromNullTerminated(k_lua_json_serializer);
                    script_name = "json-serializer"_s;
                } else if (fmt_arg.values[0] != "lua"_s) {
                    StdPrintF(StdStream::Err,
                              "Error: --format must be 'lua' or 'json', got '{}'\n",
                              fmt_arg.values[0]);
                    return ErrorCode {CommonError::InvalidFileFormat};
                }
            }

            LoadLibraryIdCache(arena);
            auto const preset_files = TRY(ExpandPresetFiles(arena, path_inputs));
            return RunOverPresets({
                .preset_files = preset_files,
                .script_source = script_source,
                .script_name = script_name,
                .have_script = false,
                .raw = parsed.args[ToInt(InspectArg::Raw)].was_provided,
                .read_only = false,
            });
        }
        case Verb::Run: {
            ASSERT(run_positionals.size >= 1);
            auto const script_input = run_positionals[0];
            auto const path_inputs = TRY(ResolvePresetPathInputs(arena, run_positionals.SubSpan(1)));

            auto const script_path = TRY_OR(AbsolutePath(arena, script_input), {
                StdPrintF(StdStream::Err, "Error: failed to resolve script path\n");
                return error;
            });
            auto const script_source = TRY_OR(ReadEntireFile(script_path, arena), {
                StdPrintF(StdStream::Err, "Error: failed to read script file: {}\n", error);
                return error;
            });

            LoadLibraryIdCache(arena);
            auto const preset_files = TRY(ExpandPresetFiles(arena, path_inputs));
            return RunOverPresets({
                .preset_files = preset_files,
                .script_source = script_source,
                .script_name = script_path,
                .have_script = true,
                .raw = false,
                .read_only = parsed.args[ToInt(RunArg::ReadOnly)].was_provided,
            });
        }
        case Verb::ApplyJson: {
            ASSERT(apply_json_positionals.size == 1);
            auto const preset_path = TRY_OR(AbsolutePath(arena, apply_json_positionals[0]), {
                StdPrintF(StdStream::Err, "Error: failed to resolve preset path\n");
                return error;
            });

            auto const json_text = TRY_OR(ReadAllStdin(arena), {
                StdPrintF(StdStream::Err, "Error: failed to read JSON from stdin: {}\n", error);
                return error;
            });
            if (json_text.size == 0) {
                StdPrintF(StdStream::Err, "Error: no JSON provided on stdin\n");
                return ErrorCode {CommonError::InvalidFileFormat};
            }

            LoadLibraryIdCache(arena);

            auto lua = luaL_newstate();
            DEFER { lua_close(lua); };
            luaL_openlibs(lua);
            TRY(RegisterJsonGlobal(lua));

            lua_getglobal(lua, "json");
            lua_getfield(lua, -1, "decode");
            lua_remove(lua, -2);
            lua_pushlstring(lua, json_text.data, json_text.size);
            if (lua_pcall(lua, 1, 1, 0) != LUA_OK) {
                StdPrintF(StdStream::Err, "Error: failed to decode JSON: {}\n", lua_tostring(lua, -1));
                return ErrorCode {CommonError::InvalidFileFormat};
            }
            if (!lua_istable(lua, -1)) {
                StdPrintF(StdStream::Err, "Error: decoded JSON is not an object\n");
                return ErrorCode {CommonError::InvalidFileFormat};
            }

            auto state = DefaultStateSnapshot();
            ExtractPresetFromLuaTable(lua, -1, state);
            lua_pop(lua, 1);

            auto new_preset_path = (String)preset_path;
            if (auto const ext = path::Extension(new_preset_path); ext != FLOE_PRESET_FILE_EXTENSION) {
                new_preset_path.RemoveSuffix(ext.size);
                new_preset_path =
                    fmt::Join(arena, Array {new_preset_path, (String)FLOE_PRESET_FILE_EXTENSION});
            }

            auto const parent_dir = path::Directory(new_preset_path);
            if (!parent_dir) {
                StdPrintF(StdStream::Err, "Error: preset path has no parent directory\n");
                return ErrorCode {CommonError::InvalidFileFormat};
            }
            TRY(CreateDirectory(*parent_dir,
                                {.create_intermediate_directories = true, .fail_if_exists = false}));

            auto const temp_dir = TRY(TemporaryDirectoryOnSameFilesystemAs(*parent_dir, arena));
            DEFER { auto _ = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

            auto seed = RandomSeed();
            auto const out_path = path::Join(
                arena,
                Array {(String)temp_dir, UniqueFilename("preset- ", FLOE_PRESET_FILE_EXTENSION, seed)});
            TRY(SavePresetFile(out_path, state, false));
            TRY(Rename(out_path, new_preset_path));
            return 0;
        }
        case Verb::Count: PanicIfReached();
    }
    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
