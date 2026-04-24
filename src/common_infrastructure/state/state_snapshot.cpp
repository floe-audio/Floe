// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "state_snapshot.hpp"

#include "tests/framework.hpp"

template <typename DynArrayT>
requires dyn::DynArray<DynArrayT>
void AssignDiffDescription(DynArrayT& diff_desc,
                           StateSnapshot const& old_state,
                           StateSnapshot const& new_state) {
    dyn::Clear(diff_desc);

    if (old_state.ir_id != new_state.ir_id) {
        auto const old_lib =
            old_state.ir_id.HasValue()
                ? sample_lib::LookupLibraryIdString(old_state.ir_id.Value().library).ValueOr("?"_s)
                : "null"_s;
        auto const new_lib =
            new_state.ir_id.HasValue()
                ? sample_lib::LookupLibraryIdString(new_state.ir_id.Value().library).ValueOr("?"_s)
                : "null"_s;
        fmt::Append(diff_desc,
                    "IR changed, old: {}:{} vs new: {}:{}\n"_s,
                    old_lib,
                    old_state.ir_id.HasValue() ? old_state.ir_id.Value().ir_id.Items() : "null"_s,
                    new_lib,
                    new_state.ir_id.HasValue() ? new_state.ir_id.Value().ir_id.Items() : "null"_s);
    }

    for (auto layer_index : Range(k_num_layers)) {
        if (old_state.inst_ids[layer_index] != new_state.inst_ids[layer_index]) {
            fmt::Append(diff_desc,
                        "Layer {}: {} vs {}\n"_s,
                        layer_index,
                        PrintInstrumentId(old_state.inst_ids[layer_index]),
                        PrintInstrumentId(new_state.inst_ids[layer_index]));
        }
    }

    for (auto param_index : Range(k_num_parameters)) {
        if (old_state.param_values[param_index] != new_state.param_values[param_index]) {
            fmt::Append(diff_desc,
                        "Param {}: {} vs {}\n"_s,
                        k_param_descriptors[param_index].name,
                        old_state.param_values[param_index],
                        new_state.param_values[param_index]);
        }
    }

    if (old_state.fx_order != new_state.fx_order) fmt::Append(diff_desc, "FX order changed\n"_s);
    if (old_state.fx_visible != new_state.fx_visible) fmt::Append(diff_desc, "FX visibility changed\n"_s);

    for (auto cc : Range<usize>(1, 128)) {
        for (auto param_index : Range(k_num_parameters)) {
            if (old_state.param_learned_ccs[param_index].Get(cc) !=
                new_state.param_learned_ccs[param_index].Get(cc)) {
                fmt::Append(diff_desc,
                            "CC {}: Param {}: {} vs {}\n"_s,
                            cc,
                            k_param_descriptors[param_index].name,
                            old_state.param_learned_ccs[param_index].Get(cc),
                            new_state.param_learned_ccs[param_index].Get(cc));
            }
        }
    }

    if (old_state.metadata.author != new_state.metadata.author)
        fmt::Append(diff_desc,
                    "Author changed: {} vs {}\n"_s,
                    old_state.metadata.author,
                    new_state.metadata.author);

    if (old_state.metadata.description != new_state.metadata.description)
        fmt::Append(diff_desc,
                    "Description changed: {} vs {}\n"_s,
                    old_state.metadata.description,
                    new_state.metadata.description);

    if (old_state.metadata.tags != new_state.metadata.tags) {
        fmt::Append(diff_desc, "Tags changed:\n"_s);
        old_state.metadata.tags.ForEachSetBit(
            [&](usize bit) { fmt::Append(diff_desc, "  - {}\n"_s, GetTagInfo((TagType)bit).name); });
        new_state.metadata.tags.ForEachSetBit(
            [&](usize bit) { fmt::Append(diff_desc, "  + {}\n"_s, GetTagInfo((TagType)bit).name); });
    }

    if (old_state.instance_id != new_state.instance_id) dyn::AppendSpan(diff_desc, "instance ID changes");

    for (auto layer_index : Range(k_num_layers))
        if (old_state.velocity_curve_points[layer_index] != new_state.velocity_curve_points[layer_index])
            fmt::Append(diff_desc, "Velocity curve points changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.harmony_intervals[layer_index] != new_state.harmony_intervals[layer_index])
            fmt::Append(diff_desc, "Harmony intervals changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.arp_steps[layer_index] != new_state.arp_steps[layer_index])
            fmt::Append(diff_desc, "Arp steps changed for layer {}\n"_s, layer_index);

    for (auto layer_index : Range(k_num_layers))
        if (old_state.slice_arp_configs[layer_index] != new_state.slice_arp_configs[layer_index])
            fmt::Append(diff_desc, "Slice arp config changed for layer {}\n"_s, layer_index);

    for (auto macro_index : Range(k_num_macros))
        if (old_state.macro_names[macro_index] != new_state.macro_names[macro_index])
            fmt::Append(diff_desc,
                        "Macro {} name changed: {} vs {}\n"_s,
                        macro_index,
                        old_state.macro_names[macro_index],
                        new_state.macro_names[macro_index]);

    for (auto macro_index : Range(k_num_macros))
        if (old_state.macro_destinations[macro_index] != new_state.macro_destinations[macro_index])
            fmt::Append(diff_desc, "Macro {} destinations changed\n"_s, macro_index);

    if (old_state.instance_config != new_state.instance_config)
        dyn::AppendSpan(diff_desc, "Instance config changed\n"_s);
}

template void AssignDiffDescription<DynamicArrayBounded<char, 0>>(DynamicArrayBounded<char, 0>&,
                                                                  StateSnapshot const&,
                                                                  StateSnapshot const&);
template void AssignDiffDescription<DynamicArrayBounded<char, 200>>(DynamicArrayBounded<char, 200>&,
                                                                    StateSnapshot const&,
                                                                    StateSnapshot const&);
template void AssignDiffDescription<DynamicArrayBounded<char, Kb(4)>>(DynamicArrayBounded<char, Kb(4)>&,
                                                                      StateSnapshot const&,
                                                                      StateSnapshot const&);
template void
AssignDiffDescription<DynamicArray<char>>(DynamicArray<char>&, StateSnapshot const&, StateSnapshot const&);

StateSnapshot const& DefaultStateSnapshot() {
    static StateSnapshot const state = ({
        StateSnapshot s {};
        for (auto const i : Range(k_num_parameters))
            s.param_values[i] = k_param_descriptors[i].default_linear_value;
        for (auto& velo : s.velocity_curve_points)
            velo = k_default_velocity_curve_points;
        s.macro_names = DefaultMacroNames();
        for (auto const i : Range<u8>(s.fx_order.size))
            s.fx_order[i] = (EffectType)i;
        s;
    });
    return state;
}

static usize SelectorLength(ParamModules const& m) {
    usize n = 0;
    for (auto p : m) {
        if (p == ParameterModule::None) break;
        ++n;
    }
    return n;
}

StateSnapshot OverlaySection(StateSnapshotView target, StateSnapshotView source) {
    auto result = target.snapshot;

    auto const src_len = SelectorLength(source.selector.modules);
    auto const tgt_len = SelectorLength(target.selector.modules);
    if (src_len == 0 || src_len != tgt_len) return result;

    for (auto const i : Range(src_len)) {
        if (source.selector.modules[i] == target.selector.modules[i]) continue;
        if (i != 0) return result;
        auto const src_is_layer = LayerIndexFromModule(source.selector.modules[0]).HasValue();
        auto const tgt_is_layer = LayerIndexFromModule(target.selector.modules[0]).HasValue();
        if (!src_is_layer || !tgt_is_layer) return result;
    }

    auto const src_layer = LayerIndexFromModule(source.selector.modules[0]);
    auto const dst_layer = LayerIndexFromModule(target.selector.modules[0]);
    auto const is_macro = source.selector.modules[0] == ParameterModule::Macro;

    for (auto const i : Range(k_num_parameters)) {
        auto const& parts = k_param_descriptors[i].module_parts;
        if (parts.size < src_len) continue;

        bool matches = true;
        for (auto const j : Range(src_len)) {
            if (parts[j] != source.selector.modules[j]) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;

        usize target_idx;
        if (src_layer && dst_layer) {
            auto const info = LayerParamIndexAndLayerFor((ParamIndex)i);
            if (!info) continue;
            target_idx = ToInt(ParamIndexFromLayerParamIndex(*dst_layer, info->param));
        } else if (is_macro) {
            auto const macro = MacroIndexFromParamIndex((ParamIndex)i);
            if (!macro || *macro != source.selector.macro_index) continue;
            target_idx = ToInt(ParamIndexFromMacroIndex(target.selector.macro_index));
        } else {
            target_idx = i;
        }

        result.param_values[target_idx] = source.snapshot.param_values[i];
    }

    if (is_macro) {
        result.macro_names[target.selector.macro_index] =
            source.snapshot.macro_names[source.selector.macro_index];
        result.macro_destinations[target.selector.macro_index] =
            source.snapshot.macro_destinations[source.selector.macro_index];
    } else if (src_layer && dst_layer) {
        auto const copy_arp = [&] {
            result.arp_steps[*dst_layer] = source.snapshot.arp_steps[*src_layer];
            result.slice_arp_configs[*dst_layer] = source.snapshot.slice_arp_configs[*src_layer];
        };
        auto const copy_velocity = [&] {
            result.velocity_curve_points[*dst_layer] = source.snapshot.velocity_curve_points[*src_layer];
        };
        auto const copy_harmony = [&] {
            result.harmony_intervals[*dst_layer] = source.snapshot.harmony_intervals[*src_layer];
        };

        if (src_len == 1) {
            copy_arp();
            copy_velocity();
            copy_harmony();
        } else {
            switch (source.selector.modules[1]) {
                case ParameterModule::Arp: copy_arp(); break;
                case ParameterModule::Config: copy_velocity(); break;
                case ParameterModule::Playback: copy_harmony(); break;
                default: break;
            }
        }
    }

    return result;
}

TEST_CASE(TestStateSnapshotSection) {
    StateSnapshot source {};
    for (auto const i : Range(k_num_parameters))
        source.param_values[i] = (f32)i;
    for (auto const i : Range(k_num_macros))
        dyn::AssignFitInCapacity(source.macro_names[i],
                                 fmt::IntToString(i, {.base = fmt::IntToStringOptions::Base::Decimal}));

    StateSnapshot target {};

    SUBCASE("empty selector is a no-op") {
        auto const result =
            OverlaySection({.snapshot = target, .selector = {}}, {.snapshot = source, .selector = {}});
        CHECK(result == target);
    }

    SUBCASE("cross-layer copies only the matched tab on the target layer") {
        auto const result = OverlaySection(
            {.snapshot = target, .selector = {.modules = {ParameterModule::Layer2, ParameterModule::Main}}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Main}}});

        for (auto const param_index : Range(k_num_parameters)) {
            auto const& parts = k_param_descriptors[param_index].module_parts;
            auto const info = LayerParamIndexAndLayerFor((ParamIndex)param_index);
            bool const in_main_layer2 =
                info && info->layer_num == 1 && parts.size >= 2 && parts[1] == ParameterModule::Main;

            if (in_main_layer2) {
                auto const src = ToInt(ParamIndexFromLayerParamIndex(0, info->param));
                CHECK_EQ(result.param_values[param_index], source.param_values[src]);
            } else {
                CHECK_EQ(result.param_values[param_index], target.param_values[param_index]);
            }
        }
    }

    SUBCASE("straight overlay (identical selectors)") {
        auto const result = OverlaySection(
            {.snapshot = target, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Lfo}}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Lfo}}});

        for (auto const param_index : Range(k_num_parameters)) {
            auto const& parts = k_param_descriptors[param_index].module_parts;
            bool const matches =
                parts.size >= 2 && parts[0] == ParameterModule::Layer1 && parts[1] == ParameterModule::Lfo;
            auto const expected =
                matches ? source.param_values[param_index] : target.param_values[param_index];
            CHECK_EQ(result.param_values[param_index], expected);
        }
    }

    SUBCASE("cross-layer round-trip to self") {
        auto const result = OverlaySection(
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer3, ParameterModule::Eq}}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer3, ParameterModule::Eq}}});
        CHECK(result == source);
    }

    SUBCASE("Arp cross-layer copies arp_steps and slice_arp_configs") {
        source.arp_steps[0][0].velocity = 42;
        source.slice_arp_configs[0].start_offset = 7;

        auto const result = OverlaySection(
            {.snapshot = target, .selector = {.modules = {ParameterModule::Layer2, ParameterModule::Arp}}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Arp}}});

        CHECK_EQ(result.arp_steps[1][0].velocity, 42);
        CHECK_EQ(result.slice_arp_configs[1].start_offset, 7);
        CHECK_EQ(result.arp_steps[0][0].velocity, target.arp_steps[0][0].velocity);
    }

    SUBCASE("macro cross copies value, name and destinations") {
        auto const result = OverlaySection(
            {.snapshot = target, .selector = {.modules = {ParameterModule::Macro}, .macro_index = 0}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Macro}, .macro_index = 2}});

        auto const dst_param = ToInt(ParamIndexFromMacroIndex(0));
        auto const src_param = ToInt(ParamIndexFromMacroIndex(2));
        CHECK_EQ(result.param_values[dst_param], source.param_values[src_param]);
        CHECK(result.macro_names[0] == source.macro_names[2]);
        CHECK(result.macro_destinations[0] == source.macro_destinations[2]);

        for (auto const i : Range(k_num_macros)) {
            if (i == 0) continue;
            CHECK(result.macro_names[i] == target.macro_names[i]);
        }
    }

    SUBCASE("mismatched shape is a no-op") {
        auto const result = OverlaySection(
            {.snapshot = target, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Eq}}},
            {.snapshot = source, .selector = {.modules = {ParameterModule::Layer1, ParameterModule::Main}}});
        CHECK(result == target);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterStateSnapshotTests) { REGISTER_TEST(TestStateSnapshotSection); }
