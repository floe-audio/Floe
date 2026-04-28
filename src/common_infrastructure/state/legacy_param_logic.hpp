// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "plugin/processing_utils/curve_map.hpp"

struct StateSnapshot;

// Per-index lookup tables that map a legacy enum value to its modern equivalent. The index into
// each table is the legacy enum's underlying integer; the entry is an explicitly-chosen modern
// enum value. Modern enums can be reordered or extended freely — only the legacy ordering is
// fixed (since legacy enums never change), so each entry stays unambiguous.

constexpr auto k_legacy_eq_type_to_current = ArrayT<param_values::EqType>({
    param_values::EqType::Peak,
    param_values::EqType::LowShelf,
    param_values::EqType::HighShelf,
});
static_assert(k_legacy_eq_type_to_current.size == ToInt(param_values::LegacyEqType::Count));

constexpr auto k_legacy_lfo_destination_to_current = ArrayT<param_values::LfoDestination>({
    param_values::LfoDestination::Volume,
    param_values::LfoDestination::Filter,
    param_values::LfoDestination::Pan,
    param_values::LfoDestination::Pitch,
});
static_assert(k_legacy_lfo_destination_to_current.size == ToInt(param_values::LegacyLfoDestination::Count));

constexpr auto k_legacy_lfo_shape_to_current = ArrayT<param_values::LfoShape>({
    param_values::LfoShape::Sine,
    param_values::LfoShape::Triangle,
    param_values::LfoShape::Sawtooth,
    param_values::LfoShape::Square,
});
static_assert(k_legacy_lfo_shape_to_current.size == ToInt(param_values::LegacyLfoShape::Count));

constexpr auto k_legacy_lfo_shape_v2_to_current = ArrayT<param_values::LfoShape>({
    param_values::LfoShape::Sine,
    param_values::LfoShape::Triangle,
    param_values::LfoShape::Sawtooth,
    param_values::LfoShape::Square,
    param_values::LfoShape::RandomSteps,
    param_values::LfoShape::RandomGlide,
});
static_assert(k_legacy_lfo_shape_v2_to_current.size == ToInt(param_values::LegacyLfoShapeV2::Count));

// Allpass is removed in the modern enum; map it to the modern default (Lowpass) so old presets
// using it land on a sensible audible filter rather than nothing.
constexpr auto k_legacy_layer_filter_type_to_current = ArrayT<param_values::LayerFilterType>({
    param_values::LayerFilterType::Lowpass,
    param_values::LayerFilterType::BandpassResonant,
    param_values::LayerFilterType::Highpass,
    param_values::LayerFilterType::Bandpass,
    param_values::LayerFilterType::BandShelving,
    param_values::LayerFilterType::Notch,
    param_values::LayerFilterType::Lowpass,
    param_values::LayerFilterType::Peak,
});
static_assert(k_legacy_layer_filter_type_to_current.size ==
              ToInt(param_values::LegacyLayerFilterType::Count));

// Legacy LowPass/HighPass (which were 24dB due to the 2-pass DSP topology) map to the new *24
// variants so existing presets keep their audible response.
constexpr auto k_legacy_effect_filter_type_to_current = ArrayT<param_values::EffectFilterType>({
    param_values::EffectFilterType::LowPass24,
    param_values::EffectFilterType::HighPass24,
    param_values::EffectFilterType::BandPass,
    param_values::EffectFilterType::Notch,
    param_values::EffectFilterType::Peak,
    param_values::EffectFilterType::LowShelf,
    param_values::EffectFilterType::HighShelf,
});
static_assert(k_legacy_effect_filter_type_to_current.size ==
              ToInt(param_values::LegacyEffectFilterType::Count));

// "Modernising" a legacy parameter means producing the modern parameter value that is audibly
// equivalent to a given legacy value, so we can hand control over from the legacy parameter to its
// modern replacement with zero audible change.
//
// The same math is used in two places:
//   1. State load: when reading an old preset/DAW project, we modernise the saved values so that
//      the modern parameter is set to the value that produces the same sound the legacy parameter
//      used to drive (see state_coding.cpp's AdaptNewerParams).
//   2. Live action: the Legacy Parameters panel exposes a "Modernise" action per parameter and a
//      "Modernise all" button — both clear the legacy override and copy the audibly-equivalent
//      value onto the modern parameter so the user's sound is preserved.

// A hidden (legacy) param is "overriding" when it has been set to a non-default value by a DAW
// automation lane, meaning it takes precedence over the modern equivalent param.
//
// IMPORTANT INVARIANT: when the legacy param is at its default value, the audio engine reads the
// modern param instead. DAW automation curves can momentarily pass through legacy_default, so the
// modern param must hold a value that is *audibly equivalent to legacy_default* — otherwise the
// crossing produces a click/jump. State load seeds the modern param with that equivalent value
// via ModerniseLegacyValue below. This decouples the modern param's `default_linear_value` (which
// is free to change in future Floe versions) from the legacy compatibility behaviour.
inline bool IsLegacyParamOverridingModern(ParamDescriptor const& desc, f32 linear_value) {
    ASSERT(desc.flags.legacy);
    return linear_value != desc.default_linear_value;
}

struct ModernisedValue {
    ParamIndex modern_param;
    f32 modern_linear;
};

// Returns the modernised value for `legacy` given its current `legacy_linear`. Returns nullopt if
// `legacy` isn't a legacy parameter, or if it has no 1:1 modern counterpart (e.g. the legacy
// velocity-mapping parameter, which folds into the velocity-curve points rather than a single
// modern parameter).
Optional<ModernisedValue> ModerniseLegacyValue(ParamIndex legacy, f32 legacy_linear);

// Returns the modern parameter that supersedes `legacy`, or nullopt under the same conditions as
// ModerniseLegacyValue.
Optional<ParamIndex> ModernCounterpartOf(ParamIndex legacy);

// Updates any macro destinations that target `legacy` to instead target `modern`. The
// destination's modulation-depth value is independent of the target and is preserved as-is. Only
// Float/Int params can be assigned as macro destinations from the GUI, so this is a no-op for
// Menu/Bool legacy params.
void ModerniseMacroDestinations(StateSnapshot& state, ParamIndex legacy, ParamIndex modern);

// Modernises the legacy velocity system (per-layer LegacyVelocityMapping mode + global
// MasterVelocity strength) into the equivalent velocity curve points for one layer. The legacy
// mode picks a base curve shape (None = flat at 1, TopToBottom = linear, etc.); the master
// strength then scales y values down toward x=0 so low-velocity notes get quieter.
CurveMap::Points ModerniseVelocityToCurve(param_values::VelocityMappingMode mode,
                                          f32 velocity_volume_strength);

// Returns the legacy parameter that supersedes a given modern one, or nullopt if `modern` has no
// 1:1 legacy counterpart (e.g. velocity, which folds into curves rather than a single param).
// Inverse of ModernCounterpartOf. Doesn't read any parameter values — combine with
// IsLegacyParamOverridingModern to check whether the legacy is currently overriding.
Optional<ParamIndex> LegacyCounterpartOf(ParamIndex modern);

// Returns the legacy parameter that is currently overriding `modern`, or nullopt if there is no
// counterpart or it isn't overriding. `all_linear_values` is the full param-value array (e.g.
// `Parameters::values` in the audio processor, or `StateSnapshot::param_values`).
inline Optional<ParamIndex> LegacyOverridingParam(ParamIndex modern, Span<f32 const> all_linear_values) {
    auto const legacy = LegacyCounterpartOf(modern);
    if (!legacy) return k_nullopt;
    auto const& legacy_desc = k_param_descriptors[ToInt(*legacy)];
    if (IsLegacyParamOverridingModern(legacy_desc, all_linear_values[ToInt(*legacy)])) return legacy;
    return k_nullopt;
}
