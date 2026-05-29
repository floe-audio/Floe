// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_tool_lua_codec.hpp"

#include <lauxlib.h>
#include <lobject.h>
#include <lua.h>
#include <lualib.h>

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/state/state_coding.hpp"

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

// Example mode: handlers writing a long array/dict early-out after the first entry. The output is a
// shape-only sample for --print-example; not round-trippable.
constexpr char const* k_example_mode_registry_key = "floe_preset_tool_example_mode";

static void SetExampleMode(lua_State* lua, bool example) {
    lua_pushboolean(lua, example);
    lua_setfield(lua, LUA_REGISTRYINDEX, k_example_mode_registry_key);
}

static bool IsExampleMode(lua_State* lua) {
    lua_getfield(lua, LUA_REGISTRYINDEX, k_example_mode_registry_key);
    auto const v = lua_toboolean(lua, -1) != 0;
    lua_pop(lua, 1);
    return v;
}

static Optional<ParamIndex> FindParamByIdString(String key) {
    for (auto const i : Range<u16>(k_num_parameters))
        if (k_param_descriptors[i].id_string == key) return ParamIndex {i};
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (auto const i : Range<u16>(vals.size)) {
            if (example && i > 0) break;
            auto const& d = k_param_descriptors[i];
            lua_pushlstring(lua, d.id_string.data, d.id_string.size);
            if (auto const formatted = d.LinearValueToString(vals[i]))
                lua_pushlstring(lua, formatted->data, formatted->size);
            else
                lua_pushnumber(lua, (f64)d.ProjectValue(vals[i]));
            lua_settable(lua, -3);
        }
    }
    static void Read(lua_State* lua, Array<f32, k_num_parameters>& vals) {
        if (!lua_istable(lua, -1)) return;
        lua_pushnil(lua);
        while (lua_next(lua, -2) != 0) {
            Optional<ParamIndex> param_index;
            Optional<f32> new_linear;
            if (lua_isstring(lua, -2)) {
                size_t key_len;
                auto const key_str = lua_tolstring(lua, -2, &key_len);
                param_index = FindParamByIdString({key_str, key_len});
                if (param_index) {
                    auto const& d = k_param_descriptors[ToInt(*param_index)];
                    // lua_isstring is true for numbers (auto-coerce), so check the exact type via lua_type.
                    if (lua_type(lua, -1) == LUA_TNUMBER) {
                        new_linear = d.LineariseValue((f32)lua_tonumber(lua, -1), true);
                    } else if (lua_type(lua, -1) == LUA_TSTRING) {
                        size_t val_len;
                        auto const val_str = lua_tolstring(lua, -1, &val_len);
                        new_linear = d.StringToLinearValue({val_str, val_len});
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_effect_types; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_effect_types; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            if (example && i > 0) break;
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            for (u16 j = 0u; j < k_arp_max_steps; ++j) {
                if (example && j > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_layers; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_macros; ++i) {
            if (example && i > 0) break;
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
        auto const example = IsExampleMode(lua);
        lua_newtable(lua);
        for (u16 i = 0u; i < k_num_macros; ++i) {
            if (example && i > 0) break;
            lua_pushinteger(lua, i + 1);
            lua_newtable(lua);
            // Array is null-terminated and packed.
            for (u16 j = 0u; j < dests[i].Size(); ++j) {
                if (example && j > 0) break;
                auto const& dest = dests[i].items[j];
                lua_pushinteger(lua, j + 1);
                lua_newtable(lua);
                auto const& id_str = k_param_descriptors[ToInt(*dest.param_index)].id_string;
                lua_pushlstring(lua, id_str.data, id_str.size);
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
                            if (lua_isstring(lua, -1)) {
                                size_t len;
                                auto const str = lua_tolstring(lua, -1, &len);
                                if (auto const param_index = FindParamByIdString({str, len}))
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

void BuildPresetLuaTable(lua_State* lua,
                         StateSnapshot const& preset_state,
                         BuildPresetLuaTableOptions options) {
    SetExampleMode(lua, options.example_mode);
    lua_newtable(lua);
    LuaWriter w {lua};
    VisitPreset(w, preset_state);
    lua_setglobal(lua, options.global_name);
}

void ExtractPresetFromLuaTable(lua_State* lua, int table_index, StateSnapshot& preset_state) {
    lua_pushvalue(lua, table_index); // duplicate the preset table to the top so Field reads from -1
    LuaReader r {lua};
    VisitPreset(r, preset_state);
    lua_pop(lua, 1);
}

// Tests
// ============================================================

// The codec uses stable id_string keys with formatted display-string values ("-12.0 dB", "50 %", "Sine").
// The format truncates precision so round-trips are only lossless when the source values land on the
// format's grid (typical for hand-set or default values, not for arbitrary floats); for arbitrary floats
// the stored numeric value will shift, but the audible result does not change.
//
// These tests bypass file I/O, so legacy→modern adaptation (AdaptNewerParams) does not apply; they verify
// the codec layer in isolation.

static ErrorCodeOr<void> RoundTrip(tests::Tester& tester, StateSnapshot const& original) {
    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    BuildPresetLuaTable(lua, original, {});

    auto roundtripped = original;
    lua_getglobal(lua, "preset");
    ExtractPresetFromLuaTable(lua, -1, roundtripped);
    lua_pop(lua, 1);

    if (roundtripped != original) {
        DynamicArray<char> diff {tester.scratch_arena};
        AssignDiffDescription(diff, original, roundtripped);
        tester.log.Error("round-trip diff:\n{}", diff.Items());
        for (auto const i : Range<u16>(k_num_parameters)) {
            if (roundtripped.param_values[i] != original.param_values[i]) {
                auto const& d = k_param_descriptors[i];
                tester.log.Error("  {} orig={} round={}",
                                 d.id_string,
                                 original.param_values[i],
                                 roundtripped.param_values[i]);
            }
        }
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    return k_success;
}

TEST_CASE(TestPresetLuaCodecRoundTripDefault) {
    // Default snapshot values sit on the display-format grid (whole percents, exact dB defaults,
    // "On"/"Off" for bools, etc.) so they must round-trip losslessly.
    auto const original = DefaultStateSnapshot();
    TRY(RoundTrip(tester, original));
    return k_success;
}

static StateSnapshot PopulatedSnapshot(u64 seed) {
    auto s = DefaultStateSnapshot();
    auto random_seed = seed;

    // Vary every parameter to a non-default linear value within its declared range.
    for (auto const i : Range<u16>(k_num_parameters)) {
        auto const& d = k_param_descriptors[i];
        auto const mix = RandomFloat01<f32>(random_seed);
        auto const range = d.linear_range;
        auto const span = range.max - range.min;
        auto const target_linear =
            Clamp(d.default_linear_value + (mix - 0.5f) * 0.4f * span, range.min, range.max);
        s.param_values[i] = target_linear;
    }

    // fx_order: reverse so it's not the default identity.
    for (auto const i : Range<u16>(k_num_effect_types))
        s.fx_order[i] = (EffectType)((k_num_effect_types - 1) - i);
    for (auto const i : Range<u16>(k_num_effect_types))
        if ((i & 1) == 0) s.fx_visible.Set(i);

    // instruments: one None, one WaveformSynth, then alternate by layer count.
    for (auto const layer : Range<u16>(k_num_layers)) {
        switch (layer % 3) {
            case 0: s.inst_ids[layer] = InstrumentId {InstrumentType::None}; break;
            case 1: s.inst_ids[layer] = InstrumentId {WaveformType::Sine}; break;
            case 2: {
                sample_lib::InstrumentId sid;
                sid.library = sample_lib::HashLibraryIdString("Test Author/Test Library"_s);
                dyn::Assign(sid.inst_id, "test_instrument"_s);
                s.inst_ids[layer] = InstrumentId {Move(sid)};
                break;
            }
        }
    }

    // metadata
    dyn::Assign(s.metadata.author, "Author Name"_s);
    dyn::Assign(s.metadata.description, "A description with unicode: \xe2\x9c\x93"_s);
    s.metadata.tags.Set(0);
    if (ToInt(TagType::Count) > 4) s.metadata.tags.Set(4);

    dyn::Assign(s.extras.instance_id, "instance-123"_s);

    // ir_id
    {
        sample_lib::IrId ir;
        ir.library = sample_lib::HashLibraryIdString("Reverb Library"_s);
        dyn::Assign(ir.ir_id, "hall1"_s);
        s.ir_id = Move(ir);
    }

    // velocity curve points
    for (auto const layer : Range<u16>(k_num_layers)) {
        dyn::Clear(s.velocity_curve_points[layer]);
        dyn::Append(s.velocity_curve_points[layer], CurveMap::Point {0.0f, 0.0f, 0.0f});
        dyn::Append(s.velocity_curve_points[layer], CurveMap::Point {0.5f, 0.7f, 0.25f});
        dyn::Append(s.velocity_curve_points[layer], CurveMap::Point {1.0f, 1.0f, -0.1f});
    }

    // harmony intervals
    for (auto const layer : Range<u16>(k_num_layers)) {
        s.harmony_intervals[layer].ClearAll();
        s.harmony_intervals[layer].Set(HarmonyIntervalBitIndex(0));
        s.harmony_intervals[layer].Set(HarmonyIntervalBitIndex(7));
        s.harmony_intervals[layer].Set(HarmonyIntervalBitIndex(-5));
    }

    // arp steps
    for (auto const layer : Range<u16>(k_num_layers)) {
        for (auto const step : Range<u16>(k_arp_max_steps)) {
            auto& a = s.arp_steps[layer][step];
            a.velocity = ArpStep::From01(0.5f + (step % 5) * 0.07f);
            a.gate = ArpStep::From01(0.3f + (step % 3) * 0.2f);
            a.on = (step % 2) == 0;
            a.tie = (step % 4) == 3;
            a.interval = (s8)((step % 7) - 3);
            a.note = (u7)((60 + step) & 0x7f);
        }
    }

    // slice arp configs
    for (auto const layer : Range<u16>(k_num_layers)) {
        s.slice_arp_configs[layer].start_offset = (u8)(2 + layer);
        s.slice_arp_configs[layer].loop_length = (u8)(4 + layer);
    }

    // macro names
    for (auto const i : Range<u16>(k_num_macros)) {
        DynamicArrayBounded<char, 32> name;
        fmt::Append(name, "macro-{}"_s, i);
        dyn::Assign(s.macro_names[i], name);
    }

    // macro destinations: route macro 0 to first two non-legacy params.
    {
        u16 placed = 0;
        for (auto const i : Range<u16>(k_num_parameters)) {
            if (placed >= 2) break;
            auto const& d = k_param_descriptors[i];
            if (ContainsSpan(d.id_string, "legacy"_s)) continue;
            s.macro_destinations[0].items[placed] = MacroDestination {
                .param_index = ParamIndex {i},
                .value = 0.5f + (f32)placed * 0.1f,
            };
            ++placed;
        }
    }

    s.instance_config.reset_on_transport = false;
    s.instance_config.reset_keyswitch = (u7)42;
    s.instance_config.seed = 17;

    return s;
}

// The encoder is lossy on arbitrary floats (truncated to display precision), so a single round-trip
// of an arbitrary populated state won't equal the original. But the truncation must be idempotent:
// once a value has been snapped to the display grid, encoding+decoding it again must be a no-op.
// This guards against display formats that don't parse back to a value re-formatting identically.
static ErrorCodeOr<void> RoundTripIsIdempotent(tests::Tester& tester, StateSnapshot const& original) {
    auto encode_decode = [&](StateSnapshot const& in) {
        auto lua = luaL_newstate();
        DEFER { lua_close(lua); };
        BuildPresetLuaTable(lua, in, {});
        auto out = in;
        lua_getglobal(lua, "preset");
        ExtractPresetFromLuaTable(lua, -1, out);
        lua_pop(lua, 1);
        return out;
    };

    auto const first = encode_decode(original);
    auto const second = encode_decode(first);

    if (first != second) {
        DynamicArray<char> diff {tester.scratch_arena};
        AssignDiffDescription(diff, first, second);
        tester.log.Error("round-trip not idempotent:\n{}", diff.Items());
        for (auto const i : Range<u16>(k_num_parameters)) {
            if (first.param_values[i] != second.param_values[i]) {
                auto const& d = k_param_descriptors[i];
                tester.log.Error("  {} first={} second={}",
                                 d.id_string,
                                 first.param_values[i],
                                 second.param_values[i]);
            }
        }
        return ErrorCode {CommonError::InvalidFileFormat};
    }
    return k_success;
}

TEST_CASE(TestPresetLuaCodecIdempotentPopulated) {
    auto const original = PopulatedSnapshot(tester.random_seed);
    TRY(RoundTripIsIdempotent(tester, original));
    return k_success;
}

TEST_REGISTRATION(RegisterPresetLuaCodecTests) {
    REGISTER_TEST(TestPresetLuaCodecRoundTripDefault);
    REGISTER_TEST(TestPresetLuaCodecIdempotentPopulated);
}
