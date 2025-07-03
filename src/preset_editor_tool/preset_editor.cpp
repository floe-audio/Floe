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
        .description = "Path to the script file to edit",
        .value_type = "path",
        .required = true,
        .num_values = 1,
    },
});

// We want to make a Lua table available to the script under the name "preset". It will contain all the
// preset's data. The preset table will mirror the structure of the StateSnapshot. Fields will be subtables
// table or values as needed.
//
// It is a two-way operation, we want to convert the StateSnapshot into a Lua table, and then we want to
// convert the Lua table back into a StateSnapshot after the script has run.

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
                                lua_getfield(lua, -2, "library_author");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sampler_id.library.author.Capacity());
                                    dyn::Assign(sampler_id.library.author, {str, copy_len});
                                }
                                lua_pop(lua, 1);

                                // Extract library name
                                lua_getfield(lua, -2, "library_name");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sampler_id.library.name.Capacity());
                                    dyn::Assign(sampler_id.library.name, {str, copy_len});
                                }
                                lua_pop(lua, 1);

                                // Extract instrument name
                                lua_getfield(lua, -2, "instrument_name");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sampler_id.inst_name.Capacity());
                                    dyn::Assign(sampler_id.inst_name, {str, copy_len});
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
                lua_pushlstring(lua, sampler_id.library.author.data, sampler_id.library.author.size);
                lua_setfield(lua, -2, "library_author");
                lua_pushlstring(lua, sampler_id.library.name.data, sampler_id.library.name.size);
                lua_setfield(lua, -2, "library_name");
                lua_pushlstring(lua, sampler_id.inst_name.data, sampler_id.inst_name.size);
                lua_setfield(lua, -2, "instrument_name");
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

    auto const script_path = TRY_OR(AbsolutePath(arena, cli_args[ToInt(CliArgId::ScriptFile)].values[0]), {
        StdPrintF(StdStream::Err, "Error: failed to resolve script path\n");
        return error;
    });

    auto const preset_state = TRY_OR(LoadPresetFile(preset_path, arena, false), {
        StdPrintF(StdStream::Err, "Error: failed to open preset file: {}\n", error);
        return error;
    });

    StdPrintF(StdStream::Err, "Initial param values:\n");
    for (u16 i = 0; i < preset_state.param_values.size; ++i) {
        auto const& param_descriptor = k_param_descriptors[i];
        StdPrintF(StdStream::Err,
                  "  {}: {}\n",
                  param_descriptor.id,
                  param_descriptor.ProjectValue(preset_state.param_values[i]));
    }

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    BuildPresetLuaTable(lua, preset_state);

    // Load the Lua libraries
    luaL_openlibs(lua);

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

    // Execute the script
    if (auto const r = lua_pcall(lua, 0, LUA_MULTRET, 0); r != LUA_OK) {
        StdPrintF(StdStream::Err, "Error: failed to execute script file: {}\n", lua_tostring(lua, -1));
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    // Extract the modified preset table back into a StateSnapshot
    lua_getglobal(lua, "preset");
    if (!lua_istable(lua, -1)) {
        StdPrintF(StdStream::Err, "Error: preset global is not a table\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    auto modified_state = preset_state;
    TRY(ExtractPresetFromLuaTable(lua, -1, modified_state));

    StdPrintF(StdStream::Err, "Modified param values:\n");
    for (auto const i : Range(modified_state.param_values.size)) {
        auto const& param_descriptor = k_param_descriptors[i];
        StdPrintF(StdStream::Err,
                  "  {}: {}\n",
                  param_descriptor.id,
                  param_descriptor.ProjectValue(modified_state.param_values[i]));
    }

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

        if (auto const r = Rename(out_path, preset_path); !r.Succeeded()) {
            StdPrintF(StdStream::Err, "Error: failed to rename modified preset file: {}\n", r.Error());
            return r.Error();
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
