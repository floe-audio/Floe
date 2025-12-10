// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <lauxlib.h>
#include <lobject.h>
#include <lua.h>
#include <lualib.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/cli_arg_parse.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/global.hpp"
#include "common_infrastructure/state/state_coding.hpp"

enum class CliArgId : u32 {
    PresetFile,
    ScriptFile,
    Count,
};

auto constexpr k_command_line_args_defs = MakeCommandLineArgDefs<CliArgId>({
    {
        .id = (u32)CliArgId::PresetFile,
        .key = "preset-file",
        .description = "Path to the preset file to edit",
        .value_type = "path",
        .required = true,
        .num_values = 1,
    },
    {
        .id = (u32)CliArgId::ScriptFile,
        .key = "script-file",
        .description =
            "Path to the script file to edit. If not provided, the preset file will be printed to stdout.",
        .value_type = "path",
        .required = false,
        .num_values = 1,
    },
});

static ErrorCodeOr<void>
ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state) {
    // Extract param_values
    lua_getfield(lua, table_index, "param_values");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isnumber(lua, -1)) {
                auto param_id = (u32)lua_tointeger(lua, -2);
                auto value = (f32)lua_tonumber(lua, -1);
                auto param_index = ParamIdToIndex(param_id);
                if (param_index) {
                    auto const& param_descriptor = k_param_descriptors[ToInt(*param_index)];
                    if (auto const new_val = param_descriptor.LineariseValue(value, true)) {
                        // We don't write the value if it's only changed by a small amount due to rounding
                        // errors.
                        constexpr f32 k_epsilon = 0.0001f;
                        auto const current_val = preset_state.LinearParam(*param_index);
                        if (Abs(current_val - *new_val) >= k_epsilon)
                            preset_state.LinearParam(*param_index) = *new_val;
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    // Extract inst_ids
    lua_getfield(lua, table_index, "inst_ids");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto index = (u32)lua_tointeger(lua, -2) - 1; // Convert from 1-indexed
                if (index < k_num_layers) {
                    // Get instrument type
                    lua_getfield(lua, -1, "type");
                    if (lua_isinteger(lua, -1)) {
                        auto inst_type = (InstrumentType)lua_tointeger(lua, -1);

                        switch (inst_type) {
                            case InstrumentType::None:
                                preset_state.inst_ids[index] = InstrumentId {InstrumentType::None};
                                break;
                            case InstrumentType::WaveformSynth: {
                                lua_getfield(lua, -2, "waveform_type");
                                if (lua_isinteger(lua, -1)) {
                                    auto waveform_type = (WaveformType)lua_tointeger(lua, -1);
                                    preset_state.inst_ids[index] = InstrumentId {waveform_type};
                                }
                                lua_pop(lua, 1);
                                break;
                            }
                            case InstrumentType::Sampler: {
                                sample_lib::InstrumentId sampler_id;

                                // Extract library author
                                lua_getfield(lua, -2, "library_id");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sampler_id.library.Capacity());
                                    dyn::Assign(sampler_id.library, {str, copy_len});
                                }
                                lua_pop(lua, 1);

                                // Extract instrument name
                                lua_getfield(lua, -2, "instrument_id");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sampler_id.inst_id.Capacity());
                                    dyn::Assign(sampler_id.inst_id, {str, copy_len});
                                }
                                lua_pop(lua, 1);

                                preset_state.inst_ids[index] = InstrumentId {Move(sampler_id)};
                                break;
                            }
                        }
                    }
                    lua_pop(lua, 1); // pop type field
                }
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    // Extract fx_order
    lua_getfield(lua, table_index, "fx_order");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isinteger(lua, -1)) {
                auto index = (u32)lua_tointeger(lua, -2) - 1; // Convert from 1-indexed
                auto fx_type = (EffectType)lua_tointeger(lua, -1);
                if (index < k_num_effect_types) preset_state.fx_order[index] = fx_type;
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    // Extract metadata
    lua_getfield(lua, table_index, "metadata");
    if (lua_istable(lua, -1)) {
        // Extract author
        lua_getfield(lua, -1, "author");
        if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            auto copy_len = Min(len, preset_state.metadata.author.Capacity());
            dyn::Assign(preset_state.metadata.author, {str, copy_len});
        }
        lua_pop(lua, 1);

        // Extract description
        lua_getfield(lua, -1, "description");
        if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            auto copy_len = Min(len, preset_state.metadata.description.Capacity());
            dyn::Assign(preset_state.metadata.description, {str, copy_len});
        }
        lua_pop(lua, 1);

        // Extract tags
        lua_getfield(lua, -1, "tags");
        if (lua_istable(lua, -1)) {
            dyn::Clear(preset_state.metadata.tags);
            lua_pushnil(lua);
            while (lua_next(lua, -2) != 0) {
                if (lua_isstring(lua, -1)) {
                    size_t len;
                    auto str = lua_tolstring(lua, -1, &len);
                    if (preset_state.metadata.tags.size < preset_state.metadata.tags.Capacity()) {
                        DynamicArrayBounded<char, k_max_tag_size> new_tag;
                        auto copy_len = Min(len, new_tag.Capacity());
                        dyn::Assign(new_tag, {str, copy_len});
                        dyn::AppendAssumeCapacity(preset_state.metadata.tags, Move(new_tag));
                    }
                }
                lua_pop(lua, 1);
            }
        }
        lua_pop(lua, 1);
    }
    lua_pop(lua, 1);

    // Extract instance_id
    lua_getfield(lua, table_index, "instance_id");
    if (lua_isstring(lua, -1)) {
        size_t len;
        auto str = lua_tolstring(lua, -1, &len);
        auto copy_len = Min(len, preset_state.instance_id.Capacity());
        dyn::Assign(preset_state.instance_id, {str, copy_len});
    }
    lua_pop(lua, 1);

    // Extract ir_id
    lua_getfield(lua, table_index, "ir_id");
    if (lua_istable(lua, -1)) {
        sample_lib::IrId ir_id;

        // Extract library author
        lua_getfield(lua, -1, "library_id");
        if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            auto copy_len = Min(len, ir_id.library.Capacity());
            dyn::Assign(ir_id.library, {str, copy_len});
        }
        lua_pop(lua, 1);

        // Extract ir name
        lua_getfield(lua, -1, "ir_name");
        if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            auto copy_len = Min(len, ir_id.ir_name.Capacity());
            dyn::Assign(ir_id.ir_name, {str, copy_len});
        }
        lua_pop(lua, 1);

        preset_state.ir_id = Move(ir_id);
    } else {
        preset_state.ir_id = k_nullopt;
    }
    lua_pop(lua, 1);

    // Extract velocity_curve_points
    lua_getfield(lua, table_index, "velocity_curve_points");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto layer_index = (u32)lua_tointeger(lua, -2) - 1; // Convert from 1-indexed
                if (layer_index < k_num_layers) {
                    dyn::Clear(preset_state.velocity_curve_points[layer_index]);
                    lua_pushnil(lua);
                    while (lua_next(lua, -2) != 0) {
                        if (lua_istable(lua, -1)) {
                            CurveMap::Point point {};
                            lua_getfield(lua, -1, "x");
                            if (lua_isnumber(lua, -1)) point.x = (f32)lua_tonumber(lua, -1);
                            lua_pop(lua, 1);
                            lua_getfield(lua, -1, "y");
                            if (lua_isnumber(lua, -1)) point.y = (f32)lua_tonumber(lua, -1);
                            lua_pop(lua, 1);
                            lua_getfield(lua, -1, "curve");
                            if (lua_isnumber(lua, -1)) point.curve = (f32)lua_tonumber(lua, -1);
                            lua_pop(lua, 1);
                            if (preset_state.velocity_curve_points[layer_index].size <
                                preset_state.velocity_curve_points[layer_index].Capacity()) {
                                dyn::AppendAssumeCapacity(preset_state.velocity_curve_points[layer_index],
                                                          point);
                            }
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    // Extract macro_names
    lua_getfield(lua, table_index, "macro_names");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isstring(lua, -1)) {
                auto macro_index = (u32)lua_tointeger(lua, -2) - 1; // Convert from 1-indexed
                if (macro_index < k_num_macros) {
                    size_t len;
                    auto str = lua_tolstring(lua, -1, &len);
                    auto copy_len = Min(len, preset_state.macro_names[macro_index].Capacity());
                    dyn::Assign(preset_state.macro_names[macro_index], {str, copy_len});
                }
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    // Extract macro_destinations
    lua_getfield(lua, table_index, "macro_destinations");
    if (lua_istable(lua, -1)) {
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto macro_index = (u32)lua_tointeger(lua, -2) - 1; // Convert from 1-indexed
                if (macro_index < k_num_macros) {
                    dyn::Clear(preset_state.macro_destinations[macro_index]);
                    lua_pushnil(lua);
                    while (lua_next(lua, -2) != 0) {
                        if (lua_istable(lua, -1)) {
                            MacroDestination dest {};
                            lua_getfield(lua, -1, "param_index");
                            if (lua_isinteger(lua, -1)) {
                                auto param_id = (u32)lua_tointeger(lua, -1);
                                if (auto const param_index = ParamIdToIndex(param_id))
                                    dest.param_index = *param_index;
                            }
                            lua_pop(lua, 1);
                            lua_getfield(lua, -1, "value");
                            if (lua_isnumber(lua, -1)) dest.value = (f32)lua_tonumber(lua, -1);
                            lua_pop(lua, 1);
                            if (preset_state.macro_destinations[macro_index].size <
                                preset_state.macro_destinations[macro_index].Capacity()) {
                                dyn::AppendAssumeCapacity(preset_state.macro_destinations[macro_index], dest);
                            }
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
    lua_pop(lua, 1);

    return k_success;
}

static void BuildPresetLuaTable(lua_State* lua, StateSnapshot const& preset_state) {
    lua_newtable(lua); // Create main preset table

    // param_values - table of param_id => value
    lua_newtable(lua);
    for (auto const i : Range<u16>(preset_state.param_values.size)) {
        auto const& param_descriptor = k_param_descriptors[i];
        auto const id = ParamIndexToId(ParamIndex {i});
        lua_pushinteger(lua, id);
        lua_pushnumber(lua, (f64)param_descriptor.ProjectValue(preset_state.param_values[i]));
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "param_values");

    // inst_ids - array of instrument objects
    lua_newtable(lua);
    for (u16 i = 0u; i < k_num_layers; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed

        // Create instrument object
        lua_newtable(lua);

        auto const& inst_id = preset_state.inst_ids[i];
        lua_pushinteger(lua, (lua_Integer)inst_id.tag);
        lua_setfield(lua, -2, "type");

        switch (inst_id.tag) {
            case InstrumentType::None:
                // No additional data needed
                break;
            case InstrumentType::WaveformSynth: {
                auto waveform_type = inst_id.Get<WaveformType>();
                lua_pushinteger(lua, (lua_Integer)waveform_type);
                lua_setfield(lua, -2, "waveform_type");
                break;
            }
            case InstrumentType::Sampler: {
                auto const& sampler_id = inst_id.Get<sample_lib::InstrumentId>();
                lua_pushlstring(lua, sampler_id.library.data, sampler_id.library.size);
                lua_setfield(lua, -2, "library_id");
                lua_pushlstring(lua, sampler_id.inst_id.data, sampler_id.inst_id.size);
                lua_setfield(lua, -2, "instrument_id");
                break;
            }
        }

        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "inst_ids");

    // fx_order - array of effect types
    lua_newtable(lua);
    for (u16 i = 0u; i < k_num_effect_types; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed
        lua_pushinteger(lua, (lua_Integer)preset_state.fx_order[i]);
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "fx_order");

    // metadata table
    lua_newtable(lua);

    // author
    lua_pushlstring(lua, preset_state.metadata.author.data, preset_state.metadata.author.size);
    lua_setfield(lua, -2, "author");

    // description
    lua_pushlstring(lua, preset_state.metadata.description.data, preset_state.metadata.description.size);
    lua_setfield(lua, -2, "description");

    // tags - array of strings
    lua_newtable(lua);
    for (u16 i = 0u; i < preset_state.metadata.tags.size; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed
        lua_pushlstring(lua, preset_state.metadata.tags[i].data, preset_state.metadata.tags[i].size);
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "tags");

    lua_setfield(lua, -2, "metadata");

    // instance_id
    lua_pushlstring(lua, preset_state.instance_id.data, preset_state.instance_id.size);
    lua_setfield(lua, -2, "instance_id");

    // ir_id
    if (preset_state.ir_id.HasValue()) {
        lua_newtable(lua);
        auto const& ir_id = preset_state.ir_id.Value();
        lua_pushlstring(lua, ir_id.library.data, ir_id.library.size);
        lua_setfield(lua, -2, "library_id");
        lua_pushlstring(lua, ir_id.ir_name.data, ir_id.ir_name.size);
        lua_setfield(lua, -2, "ir_name");
        lua_setfield(lua, -2, "ir_id");
    }

    // velocity_curve_points - array of layer arrays of curve points
    lua_newtable(lua);
    for (u16 i = 0u; i < k_num_layers; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed
        lua_newtable(lua);
        for (u16 j = 0u; j < preset_state.velocity_curve_points[i].size; ++j) {
            lua_pushinteger(lua, j + 1); // Lua is 1-indexed
            lua_newtable(lua);
            auto const& point = preset_state.velocity_curve_points[i][j];
            lua_pushnumber(lua, (f64)point.x);
            lua_setfield(lua, -2, "x");
            lua_pushnumber(lua, (f64)point.y);
            lua_setfield(lua, -2, "y");
            lua_pushnumber(lua, (f64)point.curve);
            lua_setfield(lua, -2, "curve");
            lua_settable(lua, -3);
        }
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "velocity_curve_points");

    // macro_names - array of strings
    lua_newtable(lua);
    for (u16 i = 0u; i < k_num_macros; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed
        lua_pushlstring(lua, preset_state.macro_names[i].data, preset_state.macro_names[i].size);
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "macro_names");

    // macro_destinations - array of arrays of destination objects
    lua_newtable(lua);
    for (u16 i = 0u; i < k_num_macros; ++i) {
        lua_pushinteger(lua, i + 1); // Lua is 1-indexed
        lua_newtable(lua);
        for (u16 j = 0u; j < preset_state.macro_destinations[i].size; ++j) {
            lua_pushinteger(lua, j + 1); // Lua is 1-indexed
            lua_newtable(lua);
            auto const& dest = preset_state.macro_destinations[i][j];
            lua_pushinteger(lua, ParamIndexToId(dest.param_index));
            lua_setfield(lua, -2, "param_index");
            lua_pushnumber(lua, (f64)dest.value);
            lua_setfield(lua, -2, "value");
            lua_settable(lua, -3);
        }
        lua_settable(lua, -3);
    }
    lua_setfield(lua, -2, "macro_destinations");

    lua_setglobal(lua, "preset"); // Set as global variable
}

static ErrorCodeOr<int> Main(ArgsCstr args) {
    GlobalInit({.init_error_reporting = true, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    ArenaAllocator arena {PageAllocator::Instance()};

    auto const cli_args =
        TRY(ParseCommandLineArgsStandard(arena,
                                         args,
                                         k_command_line_args_defs,
                                         {
                                             .handle_help_option = true,
                                             .print_usage_on_error = true,
                                             .description = "Edit a preset using a Lua script",
                                             .version = FLOE_VERSION_STRING,
                                         }));

    auto const preset_path = TRY_OR(AbsolutePath(arena, cli_args[ToInt(CliArgId::PresetFile)].values[0]), {
        StdPrintF(StdStream::Err, "Error: failed to resolve preset path\n");
        return error;
    });

    auto const preset_state = TRY_OR(LoadPresetFile(preset_path, arena, false), {
        StdPrintF(StdStream::Err, "Error: failed to open preset file: {}\n", error);
        return error;
    });

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    BuildPresetLuaTable(lua, preset_state);

    luaL_openlibs(lua);

    if (cli_args[ToInt(CliArgId::ScriptFile)].was_provided) {
        auto const script_path =
            TRY_OR(AbsolutePath(arena, cli_args[ToInt(CliArgId::ScriptFile)].values[0]), {
                StdPrintF(StdStream::Err, "Error: failed to resolve script path\n");
                return error;
            });

        auto const script_file_data = TRY_OR(ReadEntireFile(script_path, arena), {
            StdPrintF(StdStream::Err, "Error: failed to read script file: {}\n", error);
            return error;
        });

        if (auto const r = luaL_loadbuffer(lua,
                                           script_file_data.data,
                                           script_file_data.size,
                                           NullTerminated(script_path, arena));
            r != LUA_OK) {
            StdPrintF(StdStream::Err, "Error: failed to load script file: {}\n", lua_tostring(lua, -1));
            return ErrorCode {CommonError::InvalidFileFormat};
        }
    } else {
        // Print-only mode.
        // We use Lua to do this because we already have the preset data in Lua format.
        constexpr auto k_lua_serializer = R"lua(
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

        if (auto const r = luaL_loadstring(lua, k_lua_serializer); r != LUA_OK) {
            StdPrintF(StdStream::Err, "Error: failed to load serializer script: {}\n", lua_tostring(lua, -1));
            return ErrorCode {CommonError::InvalidFileFormat};
        }
    }

    // Execute the script
    if (auto const r = lua_pcall(lua, 0, LUA_MULTRET, 0); r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to execute script file: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    if (cli_args[ToInt(CliArgId::ScriptFile)].was_provided) {
        lua_getglobal(lua, "preset");
        if (!lua_istable(lua, -1)) {
            StdPrintF(StdStream::Err, "Error: preset global is not a table\n");
            return ErrorCode {CommonError::InvalidFileFormat};
        }

        auto modified_state = preset_state;
        TRY(ExtractPresetFromLuaTable(lua, -1, modified_state));

        {
            auto const temp_dir = TRY_OR(TemporaryDirectoryOnSameFilesystemAs(preset_path, arena), {
                StdPrintF(StdStream::Err, "Error: failed to create temporary directory: {}\n", error);
                return error;
            });
            DEFER { auto _ = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

            auto seed = RandomSeed();
            auto const out_path = path::Join(
                arena,
                Array {(String)temp_dir, UniqueFilename("preset- ", FLOE_PRESET_FILE_EXTENSION, seed)});
            if (auto const r = SavePresetFile(out_path, modified_state); !r.Succeeded()) {
                StdPrintF(StdStream::Err, "Error: failed to save modified preset file: {}\n", r.Error());
                return r.Error();
            }

            auto new_preset_path = preset_path;
            if (auto const ext = path::Extension(preset_path); ext != FLOE_PRESET_FILE_EXTENSION) {
                new_preset_path.RemoveSuffix(ext.size);
                new_preset_path =
                    fmt::Join(arena, Array {(String)new_preset_path, FLOE_PRESET_FILE_EXTENSION});
            }

            if (auto const r = Rename(out_path, new_preset_path); !r.Succeeded()) {
                StdPrintF(StdStream::Err, "Error: failed to rename modified preset file: {}\n", r.Error());
                return r.Error();
            }
        }
    }

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
