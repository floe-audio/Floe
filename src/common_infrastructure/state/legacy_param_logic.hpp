// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "plugin/processing_utils/curve_map.hpp"

enum class StateSource : u8;

// Read and understand the legacy parameters documentation page as a preface to this overview.
//
// - Floe is perfectly backwards-compatible: it always produces the same audio for a given set of
//   parameters/state.
// - Floe never removes parameters.
// - When a parameter needs to be replaced (due to a new design, better projection, new menu options, etc.)
//   the old parameter is marked as legacy and a new parameter is created. The new parameter is defined as the
//   successor of the old; we use this to compute the inverse (predecessor) at compile time. Every parameter
//   has at most one successor and at most one predecessor — chains are linear, not branching. This invariant
//   is enforced by static_assert.
// - Adding a new legacy requires a few coordinated edits in this module: a case in the successor switch
//   (records the chain link and triggers the compile-time uniqueness check) and a case in the value remap or
//   enum remap tables (the transform from the legacy's encoding into its successor's). Additional changes
//   will be needed in the state_coding file.
// - Legacy parameters can form chains when a slot has been replaced multiple times (e.g. LegacyLfoShape →
//   LegacyLfoShapeV2 → LfoShape).
// - A legacy parameter is considered "active" (overriding its modern) when its linear value is not equal to
//   its default. A non-default value strongly suggests it was set when this slot was the visible parameter on
//   the GUI.
// - When a chain has more than one active legacy ancestor, the OLDEST one wins, because it's the one most
//   likely to carry the original DAW automation lane.
// - Legacy parameters are not shown on the main GUI (only via a dedicated Legacy Parameters panel).
// - State enters the system from either a preset file or the DAW — see state_coding.cpp.
//   - For presets, on load we forward each active legacy value onto its successor and clear the legacy slot
//     ('modernising' it). Migration runs one chain step per version boundary so a later step can never
//     clobber a value an earlier one already migrated.
//   - For DAW state we cannot move values out of legacy slots, since the DAW may still be writing automation
//     to them. Instead, the audio pipeline detects legacy override at runtime on every parameter change and
//     uses the chain-remapped legacy value when active.
//   - On DAW load we also seed the modern slot with the audible-equivalent of legacy_default — this is what
//     the audio engine reads in the brief moments when a DAW automation curve passes through legacy_default
//     and the override drops out, avoiding a click/jump.
// - When converting between encodings, the chain remap goes through the parameter's natural unit (Hz, dB,
//   etc.) so unit changes between generations are handled transparently. Callers (state coding and DSP) only
//   ever see modern's encoding; they never need to know about legacy details unless they need to.
// - The Legacy Parameters panel offers a per-parameter and a 'Modernise all' action that walk the full chain
//   at once. Users invoke this once they've confirmed no DAW automation targets the legacy slots, freeing
//   them to tweak via the improved modern parameters.
// - DSP code does not deal with legacies directly. The helpers in processor/param.hpp are typically used
//   instead.
// - When migrating, any macro modulation that target a legacy param must be retargeted to its successor.
// - Some legacy parameters don't fit the one-to-one successor model — for example, the legacy
//   velocity-mapping parameter folds into the per-layer velocity curve points combined with a global
//   strength. Such cases are handled by their own migration code.

struct StateSnapshot;

inline bool IsLegacyParamOverridingModern(ParamDescriptor const& desc, f32 linear_value) {
    ASSERT(desc.flags.legacy);
    return linear_value != desc.default_linear_value;
}

struct SuccessorValue {
    ParamIndex successor_param;
    f32 successor_linear;
};

Optional<SuccessorValue> SuccessorOfLegacyValue(ParamIndex legacy, f32 legacy_linear);

Optional<SuccessorValue> TopmostSuccessorOfLegacyValue(ParamIndex legacy, f32 legacy_linear);

void ModerniseMacroDestinations(StateSnapshot& state, ParamIndex legacy);

void ModerniseLegacyParamForDawState(StateSnapshot& state, ParamIndex legacy);
void ModerniseLegacyParamForPresetState(StateSnapshot& state, ParamIndex legacy);

// Migrate an effect that previously had separate Wet/Dry amplitude params to the new Mix + Output pair.
// Lossless via Output_amp = W_amp + D_amp, Mix = W_amp / (W_amp + D_amp).
// Preset path: read W/D, write Mix+Output, neutralise legacies, retarget macro destinations.
// DAW path: cannot remove legacy lanes; instead seed Mix+Output to the audible-equivalent of legacy
// defaults so transient automation passthrough at the legacy default doesn't click.
void ModerniseWetDryEffect(StateSnapshot& state,
                           ParamIndex legacy_wet,
                           ParamIndex legacy_dry,
                           ParamIndex modern_mix,
                           ParamIndex modern_output,
                           StateSource source);

bool IsWetDryLegacyOverriding(ParamIndex legacy_wet, ParamIndex legacy_dry, f32 wet_linear, f32 dry_linear);

struct WetDryEffectGroup {
    ParamIndex legacy_wet;
    ParamIndex legacy_dry;
    ParamIndex modern_mix;
    ParamIndex modern_output;
};

Optional<WetDryEffectGroup> WetDryGroupContaining(ParamIndex param);

struct WetDryToMixOutputLinear {
    f32 mix_linear;
    f32 output_linear;
};
WetDryToMixOutputLinear
ConvertWetDryLinearToMixOutput(WetDryEffectGroup const& g, f32 wet_linear, f32 dry_linear);

CurveMap::Points ModerniseVelocityToCurve(param_values::VelocityMappingMode mode,
                                          f32 velocity_volume_strength);

bool IsAnyLegacyOverriding(ParamIndex modern, StaticSpan<f32 const, k_num_parameters> linear_param_values);

Optional<ParamIndex> LegacyPredecessor(ParamIndex param);

f32 ResolveLegacyAware(ParamIndex modern, StaticSpan<f32 const, k_num_parameters> linear_param_values);
