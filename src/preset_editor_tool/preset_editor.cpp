#include <lauxlib.h>
#include <lobject.h>
#include <lua.h>
#include <lualib.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/reader.hpp"

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

// struct StateSnapshot {
//     f32& LinearParam(ParamIndex index) { return param_values[ToInt(index)]; }
//     f32 LinearParam(ParamIndex index) const { return param_values[ToInt(index)]; }
//
//     bool operator==(StateSnapshot const& other) const = default;
//     bool operator!=(StateSnapshot const& other) const = default;
//
//     Optional<sample_lib::IrId> ir_id {};
//     InitialisedArray<InstrumentId, k_num_layers> inst_ids {InstrumentType::None};
//     Array<f32, k_num_parameters> param_values {};
//     Array<EffectType, k_num_effect_types> fx_order {};
//     Array<Bitset<128>, k_num_parameters> param_learned_ccs {};
//     StateMetadata metadata {};
//     DynamicArrayBounded<char, k_max_instance_id_size> instance_id;
// };
static void BuildPresetLuaTable(lua_State* lua, StateSnapshot const& preset_state) {
    // We want to make a Lua table available to the script under the name "preset". It will contain all the
    // preset data.

    // Let's just start with including the param_values which will be a table of numbers: param_id => value.
    lua_newtable(lua); // Create a new table on the stack
    for (u16 i = 0u; i < preset_state.param_values.size; ++i) {
        auto const id = ParamIndexToId(ParamIndex {i});
        lua_pushinteger(lua, id); // Lua is 1-indexed
        lua_pushnumber(lua, (f64)preset_state.param_values[i]);
        lua_settable(lua, -3); // Set the table at index -3 (the new table we just created)
    }
    lua_setglobal(lua, "preset"); // Set the table as a global variable named "preset"
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

    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    auto const result = Main({argc, argv});
    if (result.HasError()) return 1;
    return result.Value();
}
