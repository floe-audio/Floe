// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "macros.hpp"

#include "tests/framework.hpp"

f32 AdjustedLinearValue(Span<f32 const> param_values,
                        MacroDestinations const& macros,
                        f32 linear_value,
                        ParamIndex param_index,
                        Optional<MacroPositionOverride> position_override) {
    auto const& descriptor = k_param_descriptors[ToInt(param_index)];

    for (auto const [macro_index, dests] : Enumerate(macros)) {
        for (auto const& dest : dests.items)
            if (dest.param_index == param_index) {
                auto const macro_param = (position_override && position_override->macro_index == macro_index)
                                             ? position_override->value
                                             : param_values[ToInt(k_macro_params[macro_index])];
                linear_value += descriptor.linear_range.Delta() * (dest.ProjectedValue() * macro_param);
            }
    }

    return Clamp(linear_value, descriptor.linear_range.min, descriptor.linear_range.max);
}

// The destination's parameter as a line over its macro's position: at_0 + (slope * macro_value).
struct MacroDestinationLine {
    ParamDescriptor const& descriptor;
    f32 base;
    f32 other_macros_offset;
    // Other destinations of the same macro that target the same parameter also move with the macro.
    f32 same_macro_other_amounts;
    f32 amount;
    f32 macro_value;

    f32 At0() const { return base + other_macros_offset; }
    f32 Slope() const { return descriptor.linear_range.Delta() * (amount + same_macro_other_amounts); }
};

static MacroDestinationLine DestinationLine(Span<f32 const> param_values,
                                            MacroDestinations const& macros,
                                            u8 macro_index,
                                            u8 destination_index) {
    auto const& dest = macros[macro_index].items[destination_index];
    ASSERT(dest.param_index);
    auto const param_index = *dest.param_index;
    auto const& descriptor = k_param_descriptors[ToInt(param_index)];

    f32 other_macros_offset = 0;
    f32 same_macro_other_amounts = 0;
    for (auto const [other_macro_index, dests] : Enumerate<u8>(macros)) {
        for (auto const [other_dest_index, other_dest] : Enumerate<u8>(dests.items)) {
            if (other_dest.param_index != param_index) continue;
            if (other_macro_index != macro_index)
                other_macros_offset += descriptor.linear_range.Delta() * other_dest.ProjectedValue() *
                                       param_values[ToInt(k_macro_params[other_macro_index])];
            else if (other_dest_index != destination_index)
                same_macro_other_amounts += other_dest.ProjectedValue();
        }
    }

    return {
        .descriptor = descriptor,
        .base = param_values[ToInt(param_index)],
        .other_macros_offset = other_macros_offset,
        .same_macro_other_amounts = same_macro_other_amounts,
        .amount = dest.ProjectedValue(),
        .macro_value = param_values[ToInt(k_macro_params[macro_index])],
    };
}

MacroDestinationRange UnclampedMacroDestinationRange(Span<f32 const> param_values,
                                                     MacroDestinations const& macros,
                                                     u8 macro_index,
                                                     u8 destination_index) {
    auto const line = DestinationLine(param_values, macros, macro_index, destination_index);
    return {
        .at_0 = line.At0(),
        .at_100 = line.At0() + line.Slope(),
    };
}

MacroRangeEditResult EditMacroDestinationRange(Span<f32 const> param_values,
                                               MacroDestinations const& macros,
                                               u8 macro_index,
                                               u8 destination_index,
                                               MacroRangeEdit const& edit) {
    auto const line = DestinationLine(param_values, macros, macro_index, destination_index);
    auto const& linear_range = line.descriptor.linear_range;

    auto const old_at_0 = line.At0();
    auto const old_at_100 = old_at_0 + line.Slope();

    auto const new_base = ({
        f32 b = line.base;
        switch (edit.end) {
            case MacroRangeEnd::At0:
                b = Clamp(edit.target_linear_value - line.other_macros_offset,
                          linear_range.min,
                          linear_range.max);
                break;
            case MacroRangeEnd::At100: break;
        }
        b;
    });
    auto const new_at_0 = new_base + line.other_macros_offset;

    auto const desired_at_100 = ({
        f32 v = old_at_100;
        switch (edit.end) {
            case MacroRangeEnd::At0: break;
            case MacroRangeEnd::At100: v = edit.target_linear_value; break;
        }
        v;
    });
    auto const new_amount =
        Clamp(((desired_at_100 - new_at_0) / linear_range.Delta()) - line.same_macro_other_amounts,
              -1.0f,
              1.0f);

    Optional<f32> new_macro_value {};
    if (edit.keep_current_value) {
        auto const current_value =
            Clamp(old_at_0 + (line.Slope() * line.macro_value), linear_range.min, linear_range.max);
        auto const new_slope = linear_range.Delta() * (new_amount + line.same_macro_other_amounts);
        if (Abs(new_slope) > linear_range.Delta() * 0.00001f)
            new_macro_value = Clamp((current_value - new_at_0) / new_slope, 0.0f, 1.0f);
    }

    return {
        .base_linear_value = new_base,
        .destination_value = MacroDestination::ValueFromProjected(new_amount),
        .macro_value = new_macro_value,
    };
}

struct MacroRangeTestState {
    Array<f32, k_num_parameters> param_values;
    MacroDestinations macros;
};

static MacroRangeTestState MacroRangeTestStateWithCutoff(ParamIndex cutoff, f32 base, f32 macro_value) {
    MacroRangeTestState state {};
    for (auto const [index, descriptor] : Enumerate(k_param_descriptors))
        state.param_values[index] = descriptor.default_linear_value;
    state.param_values[ToInt(cutoff)] = base;
    state.param_values[ToInt(k_macro_params[0])] = macro_value;
    return state;
}

static f32 ResolvedValue(MacroRangeTestState const& state, ParamIndex param_index) {
    return AdjustedLinearValue(state.param_values,
                               state.macros,
                               state.param_values[ToInt(param_index)],
                               param_index);
}

static void
ApplyEdit(MacroRangeTestState& state, ParamIndex param_index, MacroRangeEditResult const& result) {
    state.param_values[ToInt(param_index)] = result.base_linear_value;
    state.macros[0].items[0].value = result.destination_value;
    if (result.macro_value) state.param_values[ToInt(k_macro_params[0])] = *result.macro_value;
}

TEST_CASE(TestMacroRangeEditKeepsOtherEnd) {
    auto const cutoff = ParamIndexFromLayerParamIndex(0, LayerParamIndex::FilterCutoff);
    auto const& range = k_param_descriptors[ToInt(cutoff)].linear_range;

    auto state = MacroRangeTestStateWithCutoff(cutoff, range.min + (range.Delta() * 0.1f), 0.5f);
    state.macros[0].items[0] = {.param_index = cutoff, .value = MacroDestination::ValueFromProjected(0.6f)};

    auto const before = UnclampedMacroDestinationRange(state.param_values, state.macros, 0, 0);

    SUBCASE("raising 0% keeps 100%") {
        auto s = state;
        auto const target = range.min + (range.Delta() * 0.2f);
        ApplyEdit(s,
                  cutoff,
                  EditMacroDestinationRange(s.param_values,
                                            s.macros,
                                            0,
                                            0,
                                            {.end = MacroRangeEnd::At0, .target_linear_value = target}));
        auto const after = UnclampedMacroDestinationRange(s.param_values, s.macros, 0, 0);
        CHECK_APPROX_EQ(after.at_0, target, 0.001f);
        CHECK_APPROX_EQ(after.at_100, before.at_100, 0.001f);
    }

    SUBCASE("lowering 100% keeps 0%") {
        auto s = state;
        auto const target = range.min + (range.Delta() * 0.4f);
        ApplyEdit(s,
                  cutoff,
                  EditMacroDestinationRange(s.param_values,
                                            s.macros,
                                            0,
                                            0,
                                            {.end = MacroRangeEnd::At100, .target_linear_value = target}));
        auto const after = UnclampedMacroDestinationRange(s.param_values, s.macros, 0, 0);
        CHECK_APPROX_EQ(after.at_0, before.at_0, 0.001f);
        CHECK_APPROX_EQ(after.at_100, target, 0.001f);
    }

    SUBCASE("keep current value moves the macro") {
        for (auto const end : Array {MacroRangeEnd::At0, MacroRangeEnd::At100}) {
            auto s = state;
            auto const value_before = ResolvedValue(s, cutoff);
            auto const target = range.min + (range.Delta() * (end == MacroRangeEnd::At0 ? 0.15f : 0.5f));
            auto const result = EditMacroDestinationRange(
                s.param_values,
                s.macros,
                0,
                0,
                {.end = end, .target_linear_value = target, .keep_current_value = true});
            REQUIRE(result.macro_value);
            ApplyEdit(s, cutoff, result);
            CHECK_APPROX_EQ(ResolvedValue(s, cutoff), value_before, 0.001f);
            CHECK_NEQ(*result.macro_value, 0.5f);
        }
    }

    SUBCASE("other macros offset both ends") {
        auto s = state;
        s.param_values[ToInt(k_macro_params[1])] = 0.5f;
        s.macros[1].items[0] = {.param_index = cutoff, .value = MacroDestination::ValueFromProjected(0.1f)};
        auto const with_other = UnclampedMacroDestinationRange(s.param_values, s.macros, 0, 0);
        CHECK_APPROX_EQ(with_other.at_0, before.at_0 + (range.Delta() * 0.05f), 0.001f);

        auto const target = with_other.at_0 + (range.Delta() * 0.1f);
        ApplyEdit(s,
                  cutoff,
                  EditMacroDestinationRange(s.param_values,
                                            s.macros,
                                            0,
                                            0,
                                            {.end = MacroRangeEnd::At0, .target_linear_value = target}));
        auto const after = UnclampedMacroDestinationRange(s.param_values, s.macros, 0, 0);
        CHECK_APPROX_EQ(after.at_0, target, 0.001f);
        CHECK_APPROX_EQ(after.at_100, with_other.at_100, 0.001f);
        CHECK_EQ(s.macros[1].items[0].value, MacroDestination::ValueFromProjected(0.1f));
    }

    SUBCASE("amount limit stops the edit") {
        auto s = state;
        auto const result = EditMacroDestinationRange(
            s.param_values,
            s.macros,
            0,
            0,
            {.end = MacroRangeEnd::At100, .target_linear_value = range.max + (range.Delta() * 5)});
        CHECK_EQ(result.destination_value, 1.0f);
    }

    SUBCASE("keep current value with flat range leaves the macro") {
        auto s = state;
        auto const result = EditMacroDestinationRange(
            s.param_values,
            s.macros,
            0,
            0,
            {.end = MacroRangeEnd::At100, .target_linear_value = before.at_0, .keep_current_value = true});
        CHECK(!result.macro_value);
    }

    return k_success;
}

TEST_CASE(TestMacroPositionOverride) {
    auto const cutoff = ParamIndexFromLayerParamIndex(0, LayerParamIndex::FilterCutoff);
    auto const& range = k_param_descriptors[ToInt(cutoff)].linear_range;
    auto state = MacroRangeTestStateWithCutoff(cutoff, range.min, 0.3f);
    state.macros[0].items[0] = {.param_index = cutoff, .value = 1};

    auto const at = [&](Optional<MacroPositionOverride> o) {
        return AdjustedLinearValue(state.param_values, state.macros, range.min, cutoff, o);
    };
    CHECK_APPROX_EQ(at({}), range.min + (range.Delta() * 0.3f), 0.001f);
    CHECK_APPROX_EQ(at(MacroPositionOverride {.macro_index = 0, .value = 0}), range.min, 0.001f);
    CHECK_APPROX_EQ(at(MacroPositionOverride {.macro_index = 0, .value = 1}), range.max, 0.001f);
    CHECK_APPROX_EQ(at(MacroPositionOverride {.macro_index = 1, .value = 1}), at({}), 0.001f);
    return k_success;
}

TEST_REGISTRATION(RegisterMacrosTests) {
    REGISTER_TEST(TestMacroRangeEditKeepsOtherEnd);
    REGISTER_TEST(TestMacroPositionOverride);
}
