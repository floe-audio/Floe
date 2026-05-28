// Copyright 2025-2026 Sam Windell
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
#include "common_infrastructure/sample_library/library_id_cache.hpp"
#include "common_infrastructure/state/state_coding.hpp"

// IMPROVE: export a Lua LSP def file for the preset table.

enum class CliArgId : u8 {
    PresetFile,
    ScriptFile,
    Raw,
    Pretty,
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
            "Path to the script file to edit. If not provided, the preset file will be printed to stdout.\n"
            "In the script, you have access to a global 'preset'. Modify this global and the changes will\n"
            "be saved to the file. Run this tool without a script file to see the format of 'preset'.\n"
            "param_values are keyed by param ID.\n",
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
        .id = (u32)CliArgId::Pretty,
        .key = "pretty",
        .description =
            "Use human-readable identifiers and values: parameters are keyed by 'Module/Name' strings\n"
            "(e.g. 'Effect/Compressor/Threshold') and values are formatted (e.g. '-12.0 dB', '50%',\n"
            "'Sine'). Macro destinations use the same string keys. Round-trips with --script-file, but\n"
            "the string keys and formatted values are NOT stable across Floe versions: a rename or\n"
            "format change will break old scripts. Use the default numeric mode for scripts you want\n"
            "to keep working after upgrades.\n",
        .value_type = "",
        .required = false,
        .num_values = 0,
    },
});

// Two-way Lua <-> StateSnapshot codec.
//
// The schema lives in one place (VisitPreset). LuaWriter pushes the preset into a new table; LuaReader
// extracts back into a StateSnapshot. Each handler owns both directions for one field-kind, so the read and
// write sides cannot drift. Per-type readability choices (set-of-strings for tags, 1-indexed array for
// fx_order, sub-table for ir_id, etc.) stay private to the handler.

struct LuaWriter {
    lua_State* lua;

    template <typename T, typename H>
    void Field(char const* name, T const& value, H) {
        H::Write(lua, value);
        lua_setfield(lua, -2, name);
    }
};

struct LuaReader {
    lua_State* lua;

    template <typename T, typename H>
    void Field(char const* name, T& value, H) {
        lua_getfield(lua, -1, name);
        H::Read(lua, value);
        lua_pop(lua, 1);
    }
};

// Pretty mode is threaded through the Lua registry rather than every handler signature: only two
// handlers branch on it, so a global flag avoids churning the uniform Field template.
constexpr char const* k_pretty_mode_registry_key = "floe_preset_tool_pretty_mode";

static void SetPrettyMode(lua_State* lua, bool pretty) {
    lua_pushboolean(lua, pretty);
    lua_setfield(lua, LUA_REGISTRYINDEX, k_pretty_mode_registry_key);
}

static bool IsPrettyMode(lua_State* lua) {
    lua_getfield(lua, LUA_REGISTRYINDEX, k_pretty_mode_registry_key);
    auto const v = lua_toboolean(lua, -1) != 0;
    lua_pop(lua, 1);
    return v;
}

static DynamicArrayBounded<char, 256> ParamPrettyKey(ParamDescriptor const& d) {
    DynamicArrayBounded<char, 256> key {};
    dyn::AppendSpan(key, d.ModuleString("/"));
    if (key.size != 0) dyn::Append(key, '/');
    dyn::AppendSpan(key, d.name);
    return key;
}

static Optional<ParamIndex> FindParamByPrettyKey(String key) {
    for (auto const i : Range<u16>(k_num_parameters))
        if (String(ParamPrettyKey(k_param_descriptors[i])) == key) return ParamIndex {i};
    return k_nullopt;
}

// In Read functions, the value is on top of the stack on entry. The caller pops it.

struct StringH {
    template <typename DA>
    static void Write(lua_State* lua, DA const& s) {
        lua_pushlstring(lua, s.data, s.size);
    }
    template <typename DA>
    static void Read(lua_State* lua, DA& s) {
        if (!lua_isstring(lua, -1)) return;
        size_t len;
        auto str = lua_tolstring(lua, -1, &len);
        auto copy_len = Min(len, s.Capacity());
        dyn::Assign(s, {str, copy_len});
    }
};

struct BoolH {
    static void Write(lua_State* lua, bool v) { lua_pushboolean(lua, v); }
    static void Read(lua_State* lua, bool& v) {
        if (lua_isboolean(lua, -1)) v = lua_toboolean(lua, -1);
    }
};

template <typename T>
struct IntH {
    static void Write(lua_State* lua, T v) { lua_pushinteger(lua, (lua_Integer)v); }
    static void Read(lua_State* lua, T& v) {
        if (lua_isinteger(lua, -1)) v = (T)lua_tointeger(lua, -1);
    }
};

struct ParamValuesH {
    static void Write(lua_State* lua, Array<f32, k_num_parameters> const& vals) {
        auto const pretty = IsPrettyMode(lua);
        lua_newtable(lua);
        for (auto const i : Range<u16>(vals.size)) {
            auto const& d = k_param_descriptors[i];
            if (pretty) {
                auto const key = ParamPrettyKey(d);
                lua_pushlstring(lua, key.data, key.size);
                if (auto const formatted = d.LinearValueToString(vals[i]))
                    lua_pushlstring(lua, formatted->data, formatted->size);
                else
                    lua_pushnumber(lua, (f64)d.ProjectValue(vals[i]));
            } else {
                lua_pushinteger(lua, ParamIndexToId(ParamIndex {i}));
                lua_pushnumber(lua, (f64)d.ProjectValue(vals[i]));
            }
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<f32, k_num_parameters>& vals) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            Optional<ParamIndex> param_index;
            Optional<f32> new_linear;
            if (lua_isinteger(lua, -2)) {
                param_index = ParamIdToIndex((u32)lua_tointeger(lua, -2));
                if (param_index && lua_isnumber(lua, -1)) {
                    auto const& d = k_param_descriptors[ToInt(*param_index)];
                    new_linear = d.LineariseValue((f32)lua_tonumber(lua, -1), true);
                }
            } else if (lua_isstring(lua, -2)) {
                size_t key_len;
                auto const key_str = lua_tolstring(lua, -2, &key_len);
                param_index = FindParamByPrettyKey({key_str, key_len});
                if (param_index) {
                    auto const& d = k_param_descriptors[ToInt(*param_index)];
                    if (lua_isstring(lua, -1)) {
                        size_t val_len;
                        auto const val_str = lua_tolstring(lua, -1, &val_len);
                        new_linear = d.StringToLinearValue({val_str, val_len});
                    } else if (lua_isnumber(lua, -1)) {
                        new_linear = d.LineariseValue((f32)lua_tonumber(lua, -1), true);
                    }
                }
            }
            if (param_index && new_linear) {
                // Don't write if only changed by rounding error.
                constexpr f32 k_epsilon = 0.0001f;
                auto& cur = vals[ToInt(*param_index)];
                if (Abs(cur - *new_linear) >= k_epsilon) cur = *new_linear;
            }
            lua_pop(lua, 1);
        }
    }
};

struct InstIdsH {
    static void Write(lua_State* lua, InitialisedArray<InstrumentId, k_num_layers> const& ids) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            auto const& inst_id = ids[i];
            lua_pushinteger(lua, (lua_Integer)inst_id.tag);
            lua_setfield(lua, -2, "type");
            switch (inst_id.tag) {
                case InstrumentType::None: break;
                case InstrumentType::WaveformSynth: {
                    auto const wt = inst_id.Get<WaveformType>();
                    lua_pushinteger(lua, (lua_Integer)wt);
                    lua_setfield(lua, -2, "waveform_type");
                    break;
                }
                case InstrumentType::Sampler: {
                    auto const& sid = inst_id.Get<sample_lib::InstrumentId>();
                    if (auto const lib = sample_lib::LookupLibraryIdString(sid.library))
                        lua_pushlstring(lua, lib->data, lib->size);
                    else
                        lua_pushinteger(lua, (lua_Integer)sid.library);
                    lua_setfield(lua, -2, "library_id");
                    lua_pushlstring(lua, sid.inst_id.data, sid.inst_id.size);
                    lua_setfield(lua, -2, "instrument_id");
                    break;
                }
            }
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, InitialisedArray<InstrumentId, k_num_layers>& ids) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const layer_index = (u32)lua_tointeger(lua, -2) - 1;
                if (layer_index < k_num_layers) {
                    lua_getfield(lua, -1, "type");
                    if (lua_isinteger(lua, -1)) {
                        auto const inst_type = (InstrumentType)lua_tointeger(lua, -1);
                        switch (inst_type) {
                            case InstrumentType::None:
                                ids[layer_index] = InstrumentId {InstrumentType::None};
                                break;
                            case InstrumentType::WaveformSynth: {
                                lua_getfield(lua, -2, "waveform_type");
                                if (lua_isinteger(lua, -1)) {
                                    auto const wt = (WaveformType)lua_tointeger(lua, -1);
                                    ids[layer_index] = InstrumentId {wt};
                                }
                                lua_pop(lua, 1);
                                break;
                            }
                            case InstrumentType::Sampler: {
                                sample_lib::InstrumentId sid;
                                lua_getfield(lua, -2, "library_id");
                                if (lua_isinteger(lua, -1)) {
                                    sid.library = (sample_lib::LibraryId)lua_tointeger(lua, -1);
                                } else if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    sid.library = sample_lib::HashLibraryIdString({str, len});
                                }
                                lua_pop(lua, 1);
                                lua_getfield(lua, -2, "instrument_id");
                                if (lua_isstring(lua, -1)) {
                                    size_t len;
                                    auto str = lua_tolstring(lua, -1, &len);
                                    auto copy_len = Min(len, sid.inst_id.Capacity());
                                    dyn::Assign(sid.inst_id, {str, copy_len});
                                }
                                lua_pop(lua, 1);
                                ids[layer_index] = InstrumentId {Move(sid)};
                                break;
                            }
                        }
                    }
                    lua_pop(lua, 1);
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct FxOrderH {
    static void Write(lua_State* lua, Array<EffectType, k_num_effect_types> const& order) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_effect_types; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_pushinteger(lua, (lua_Integer)order[i]);
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<EffectType, k_num_effect_types>& order) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isinteger(lua, -1)) {
                auto const index = (u32)lua_tointeger(lua, -2) - 1;
                auto const fx = (EffectType)lua_tointeger(lua, -1);
                if (index < k_num_effect_types) order[index] = fx;
            }
            lua_pop(lua, 1);
        }
    }
};

struct FxVisibleH {
    static void Write(lua_State* lua, Bitset<k_num_effect_types> const& bits) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_effect_types; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_pushboolean(lua, bits.Get(i));
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Bitset<k_num_effect_types>& bits) {
        if (!lua_istable(lua, -1)) return;
        bits.ClearAll();
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isboolean(lua, -1)) {
                auto const index = (u32)lua_tointeger(lua, -2) - 1;
                if (index < k_num_effect_types && lua_toboolean(lua, -1)) bits.Set(index);
            }
            lua_pop(lua, 1);
        }
    }
};

struct TagsH {
    static void Write(lua_State* lua, TagsBitset const& tags) {
        lua_newtable(lua);
        int tag_index = 1;
        tags.ForEachSetBit([&](usize bit) {
            auto const name = GetTagInfo((TagType)bit).name;
            lua_pushinteger(lua, tag_index++);
            lua_pushlstring(lua, name.data, name.size);
            lua_settable(lua, -3);
        });
    }
    static void Read(lua_State* lua, TagsBitset& tags) {
        if (!lua_istable(lua, -1)) return;
        tags.ClearAll();
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isstring(lua, -1)) {
                size_t len;
                auto str = lua_tolstring(lua, -1, &len);
                if (auto const t = LookupTagName({str, len})) tags.Set(ToInt(t->tag));
            }
            lua_pop(lua, 1);
        }
    }
};

struct MetadataH {
    static void Write(lua_State* lua, StateMetadata const& m) {
        lua_newtable(lua);
        LuaWriter w {lua};
        w.Field("author", m.author, StringH {});
        w.Field("description", m.description, StringH {});
        w.Field("tags", m.tags, TagsH {});
    }
    static void Read(lua_State* lua, StateMetadata& m) {
        if (!lua_istable(lua, -1)) return;
        LuaReader r {lua};
        r.Field("author", m.author, StringH {});
        r.Field("description", m.description, StringH {});
        r.Field("tags", m.tags, TagsH {});
    }
};

struct IrIdH {
    static void Write(lua_State* lua, Optional<sample_lib::IrId> const& opt) {
        if (!opt.HasValue()) {
            lua_pushnil(lua);
            return;
        }
        auto const& ir_id = opt.Value();
        lua_newtable(lua);
        if (auto const lib = sample_lib::LookupLibraryIdString(ir_id.library))
            lua_pushlstring(lua, lib->data, lib->size);
        else
            lua_pushinteger(lua, (lua_Integer)ir_id.library);
        lua_setfield(lua, -2, "library_id");
        lua_pushlstring(lua, ir_id.ir_id.data, ir_id.ir_id.size);
        lua_setfield(lua, -2, "ir_id");
    }
    static void Read(lua_State* lua, Optional<sample_lib::IrId>& opt) {
        if (!lua_istable(lua, -1)) {
            opt = k_nullopt;
            return;
        }
        sample_lib::IrId ir_id;
        lua_getfield(lua, -1, "library_id");
        if (lua_isinteger(lua, -1)) {
            ir_id.library = (sample_lib::LibraryId)lua_tointeger(lua, -1);
        } else if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            ir_id.library = sample_lib::HashLibraryIdString({str, len});
        }
        lua_pop(lua, 1);
        lua_getfield(lua, -1, "ir_id");
        if (lua_isstring(lua, -1)) {
            size_t len;
            auto str = lua_tolstring(lua, -1, &len);
            auto copy_len = Min(len, ir_id.ir_id.Capacity());
            dyn::Assign(ir_id.ir_id, {str, copy_len});
        }
        lua_pop(lua, 1);
        opt = Move(ir_id);
    }
};

struct VelocityCurvePointsH {
    static void Write(lua_State* lua, Array<CurveMap::Points, k_num_layers> const& curves) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            for (u16 j = 0u; j < curves[i].size; ++j) {
                lua_pushinteger(lua, j + 1);
                lua_newtable(lua);
                auto const& p = curves[i][j];
                lua_pushnumber(lua, (f64)p.x);
                lua_setfield(lua, -2, "x");
                lua_pushnumber(lua, (f64)p.y);
                lua_setfield(lua, -2, "y");
                lua_pushnumber(lua, (f64)p.curve);
                lua_setfield(lua, -2, "curve");
                lua_settable(lua, -3);
            }
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<CurveMap::Points, k_num_layers>& curves) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const layer_index = (u32)lua_tointeger(lua, -2) - 1;
                if (layer_index < k_num_layers) {
                    dyn::Clear(curves[layer_index]);
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
                            if (curves[layer_index].size < curves[layer_index].Capacity())
                                dyn::AppendAssumeCapacity(curves[layer_index], point);
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct HarmonyIntervalsH {
    static void Write(lua_State* lua, Array<HarmonyIntervalsBitset, k_num_layers> const& bits) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            u16 entry_index = 1;
            bits[i].ForEachSetBit([&](usize bit) {
                lua_pushinteger(lua, entry_index++);
                lua_pushinteger(lua, HarmonyIntervalSemitones(bit));
                lua_settable(lua, -3);
            });
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<HarmonyIntervalsBitset, k_num_layers>& bits) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const layer_index = (u32)lua_tointeger(lua, -2) - 1;
                if (layer_index < k_num_layers) {
                    bits[layer_index].ClearAll();
                    lua_pushnil(lua);
                    while (lua_next(lua, -2) != 0) {
                        if (lua_isinteger(lua, -1)) {
                            auto const semitone = (int)lua_tointeger(lua, -1);
                            if (semitone >= k_harmony_interval_min_semitone &&
                                semitone <= k_harmony_interval_max_semitone) {
                                bits[layer_index].Set(HarmonyIntervalBitIndex(semitone));
                            }
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct ArpStepsH {
    static void Write(lua_State* lua, Array<Array<ArpStep, k_arp_max_steps>, k_num_layers> const& steps) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            for (u16 j = 0u; j < k_arp_max_steps; ++j) {
                lua_pushinteger(lua, j + 1);
                lua_newtable(lua);
                auto const& step = steps[i][j];
                lua_pushnumber(lua, (f64)step.Velocity01());
                lua_setfield(lua, -2, "velocity");
                lua_pushnumber(lua, (f64)step.Gate01());
                lua_setfield(lua, -2, "gate");
                lua_pushboolean(lua, step.on);
                lua_setfield(lua, -2, "on");
                lua_pushboolean(lua, step.tie);
                lua_setfield(lua, -2, "tie");
                lua_pushinteger(lua, (lua_Integer)step.interval);
                lua_setfield(lua, -2, "interval");
                lua_pushinteger(lua, (lua_Integer)step.note);
                lua_setfield(lua, -2, "note");
                lua_settable(lua, -3);
            }
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<Array<ArpStep, k_arp_max_steps>, k_num_layers>& steps) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const layer_index = (u32)lua_tointeger(lua, -2) - 1;
                if (layer_index < k_num_layers) {
                    lua_pushnil(lua);
                    while (lua_next(lua, -2) != 0) {
                        if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                            auto const step_index = (u32)lua_tointeger(lua, -2) - 1;
                            if (step_index < k_arp_max_steps) {
                                auto& step = steps[layer_index][step_index];
                                lua_getfield(lua, -1, "velocity");
                                if (lua_isnumber(lua, -1))
                                    step.velocity = ArpStep::From01((f32)lua_tonumber(lua, -1));
                                lua_pop(lua, 1);
                                lua_getfield(lua, -1, "gate");
                                if (lua_isnumber(lua, -1))
                                    step.gate = ArpStep::From01((f32)lua_tonumber(lua, -1));
                                lua_pop(lua, 1);
                                lua_getfield(lua, -1, "on");
                                if (lua_isboolean(lua, -1)) step.on = lua_toboolean(lua, -1);
                                lua_pop(lua, 1);
                                lua_getfield(lua, -1, "tie");
                                if (lua_isboolean(lua, -1)) step.tie = lua_toboolean(lua, -1);
                                lua_pop(lua, 1);
                                lua_getfield(lua, -1, "interval");
                                if (lua_isinteger(lua, -1)) step.interval = (s8)lua_tointeger(lua, -1);
                                lua_pop(lua, 1);
                                lua_getfield(lua, -1, "note");
                                if (lua_isinteger(lua, -1)) {
                                    auto const n = lua_tointeger(lua, -1);
                                    if (n >= 0 && n <= 127) step.note = (u7)n;
                                }
                                lua_pop(lua, 1);
                            }
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct SliceArpConfigsH {
    static void Write(lua_State* lua, Array<SliceArpConfig, k_num_layers> const& cfgs) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            auto const& cfg = cfgs[i];
            lua_pushinteger(lua, (lua_Integer)cfg.start_offset);
            lua_setfield(lua, -2, "start_offset");
            lua_pushinteger(lua, (lua_Integer)cfg.loop_length);
            lua_setfield(lua, -2, "loop_length");
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<SliceArpConfig, k_num_layers>& cfgs) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const layer_index = (u32)lua_tointeger(lua, -2) - 1;
                if (layer_index < k_num_layers) {
                    auto& cfg = cfgs[layer_index];
                    lua_getfield(lua, -1, "start_offset");
                    if (lua_isinteger(lua, -1)) cfg.start_offset = (u8)lua_tointeger(lua, -1);
                    lua_pop(lua, 1);
                    lua_getfield(lua, -1, "loop_length");
                    if (lua_isinteger(lua, -1)) cfg.loop_length = (u8)lua_tointeger(lua, -1);
                    lua_pop(lua, 1);
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct MacroNamesH {
    static void Write(lua_State* lua, MacroNames const& names) {
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_macros; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_pushlstring(lua, names[i].data, names[i].size);
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, MacroNames& names) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_isstring(lua, -1)) {
                auto const macro_index = (u32)lua_tointeger(lua, -2) - 1;
                if (macro_index < k_num_macros) {
                    size_t len;
                    auto str = lua_tolstring(lua, -1, &len);
                    auto copy_len = Min(len, names[macro_index].Capacity());
                    dyn::Assign(names[macro_index], {str, copy_len});
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct MacroDestinationsH {
    static void Write(lua_State* lua, MacroDestinations const& dests) {
        auto const pretty = IsPrettyMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_macros; ++i) {
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            // Array is null-terminated and packed.
            for (u16 j = 0u; j < dests[i].Size(); ++j) {
                auto const& dest = dests[i].items[j];
                lua_pushinteger(lua, j + 1);
                lua_newtable(lua);
                if (pretty) {
                    auto const key = ParamPrettyKey(k_param_descriptors[ToInt(*dest.param_index)]);
                    lua_pushlstring(lua, key.data, key.size);
                } else {
                    lua_pushinteger(lua, ParamIndexToId(*dest.param_index));
                }
                lua_setfield(lua, -2, "param_index");
                lua_pushnumber(lua, (f64)dest.value);
                lua_setfield(lua, -2, "value");
                lua_settable(lua, -3);
            }
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, MacroDestinations& dests) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            if (lua_isinteger(lua, -2) && lua_istable(lua, -1)) {
                auto const macro_index = (u32)lua_tointeger(lua, -2) - 1;
                if (macro_index < k_num_macros) {
                    dests[macro_index] = {};
                    usize dest_index = 0;
                    lua_pushnil(lua);
                    while (lua_next(lua, -2) != 0) {
                        if (lua_istable(lua, -1) && dest_index < k_max_macro_destinations) {
                            MacroDestination dest {};
                            lua_getfield(lua, -1, "param_index");
                            if (lua_isinteger(lua, -1)) {
                                auto const param_id = (u32)lua_tointeger(lua, -1);
                                if (auto const param_index = ParamIdToIndex(param_id))
                                    dest.param_index = *param_index;
                            } else if (lua_isstring(lua, -1)) {
                                size_t len;
                                auto const str = lua_tolstring(lua, -1, &len);
                                if (auto const param_index = FindParamByPrettyKey({str, len}))
                                    dest.param_index = *param_index;
                            }
                            lua_pop(lua, 1);
                            lua_getfield(lua, -1, "value");
                            if (lua_isnumber(lua, -1)) dest.value = (f32)lua_tonumber(lua, -1);
                            lua_pop(lua, 1);
                            dests[macro_index].items[dest_index++] = dest;
                        }
                        lua_pop(lua, 1);
                    }
                }
            }
            lua_pop(lua, 1);
        }
    }
};

struct InstanceConfigH {
    static void Write(lua_State* lua, InstanceConfig const& cfg) {
        lua_newtable(lua);
        LuaWriter w {lua};
        w.Field("reset_on_transport", cfg.reset_on_transport, BoolH {});
        if (cfg.reset_keyswitch.HasValue()) {
            lua_pushinteger(lua, (lua_Integer)cfg.reset_keyswitch.Value());
            lua_setfield(lua, -2, "reset_keyswitch");
        }
        w.Field("seed", cfg.seed, IntH<u8> {});
    }
    static void Read(lua_State* lua, InstanceConfig& cfg) {
        if (!lua_istable(lua, -1)) return;
        LuaReader r {lua};
        r.Field("reset_on_transport", cfg.reset_on_transport, BoolH {});
        lua_getfield(lua, -1, "reset_keyswitch");
        if (lua_isinteger(lua, -1)) {
            auto const n = lua_tointeger(lua, -1);
            if (n >= 0 && n <= 127) cfg.reset_keyswitch = (u7)n;
        } else {
            cfg.reset_keyswitch = k_nullopt;
        }
        lua_pop(lua, 1);
        r.Field("seed", cfg.seed, IntH<u8> {});
    }
};

// One declaration drives both directions. Adding a new field means: write one handler, add one line here.
template <typename V, typename S>
static void VisitPreset(V& v, S& s) {
    v.Field("param_values", s.param_values, ParamValuesH {});
    v.Field("inst_ids", s.inst_ids, InstIdsH {});
    v.Field("fx_order", s.fx_order, FxOrderH {});
    v.Field("fx_visible", s.fx_visible, FxVisibleH {});
    v.Field("metadata", s.metadata, MetadataH {});
    v.Field("instance_id", s.extras.instance_id, StringH {});
    v.Field("ir_id", s.ir_id, IrIdH {});
    v.Field("velocity_curve_points", s.velocity_curve_points, VelocityCurvePointsH {});
    v.Field("harmony_intervals", s.harmony_intervals, HarmonyIntervalsH {});
    v.Field("arp_steps", s.arp_steps, ArpStepsH {});
    v.Field("slice_arp_configs", s.slice_arp_configs, SliceArpConfigsH {});
    v.Field("macro_names", s.macro_names, MacroNamesH {});
    v.Field("macro_destinations", s.macro_destinations, MacroDestinationsH {});
    v.Field("instance_config", s.instance_config, InstanceConfigH {});
}

static void BuildPresetLuaTable(lua_State* lua, StateSnapshot const& preset_state) {
    lua_newtable(lua);
    LuaWriter w {lua};
    VisitPreset(w, preset_state);
    lua_setglobal(lua, "preset");
}

static ErrorCodeOr<void>
ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state) {
    lua_pushvalue(lua, table_index); // duplicate the preset table to the top so Field reads from -1
    LuaReader r {lua};
    VisitPreset(r, preset_state);
    lua_pop(lua, 1);
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

    auto const cli_args = TRY(ParseCommandLineArgsStandard(
        arena,
        args,
        k_command_line_args_defs,
        {
            .handle_help_option = true,
            .print_usage_on_error = true,
            .description = "Edit a preset using a Lua script.\n"
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
        }));

    auto const preset_path = TRY_OR(AbsolutePath(arena, cli_args[ToInt(CliArgId::PresetFile)].values[0]), {
        StdPrintF(StdStream::Err, "Error: failed to resolve preset path\n");
        return error;
    });

    auto const raw = cli_args[ToInt(CliArgId::Raw)].was_provided;
    if (raw && cli_args[ToInt(CliArgId::ScriptFile)].was_provided) {
        StdPrintF(StdStream::Err, "Error: --raw cannot be combined with --script-file\n");
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    LoadLibraryIdCache(arena);

    auto const preset_state = TRY_OR(LoadPresetFile(preset_path, arena, raw), {
        StdPrintF(StdStream::Err, "Error: failed to open preset file: {}\n", error);
        return error;
    });

    auto const pretty = cli_args[ToInt(CliArgId::Pretty)].was_provided;

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    SetPrettyMode(lua, pretty);
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

        if (modified_state == preset_state) {
            // Script didn't change anything; skip the write.
        } else {
            auto const temp_dir = TRY_OR(TemporaryDirectoryOnSameFilesystemAs(preset_path, arena), {
                StdPrintF(StdStream::Err, "Error: failed to create temporary directory: {}\n", error);
                return error;
            });
            DEFER { auto _ = Delete(temp_dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

            auto seed = RandomSeed();
            auto const out_path = path::Join(
                arena,
                Array {(String)temp_dir, UniqueFilename("preset- ", FLOE_PRESET_FILE_EXTENSION, seed)});
            if (auto const r = SavePresetFile(out_path, modified_state, false); r.HasError()) {
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
