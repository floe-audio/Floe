// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "random_variation.hpp"

#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/engine.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static f32 EaseInQuad(f32 x) { return x * x; }
static f32 EaseOutQuad(f32 x) { return 1.0f - ((1.0f - x) * (1.0f - x)); }

// Picks round(curve * actives.size) entries uniformly at random and shifts each pinned
// value by ±[0.3, 0.7+0.3*curve] (clamped to [0,1]). Few-but-significant shifts feel
// more like a real variation than uniform tiny ones.
static void VarySubsetOfParams(u64& seed,
                               StateSnapshot const& source,
                               StateSnapshot& target,
                               Span<ParamIndex const> active,
                               f32 curve) {
    DynamicArrayBounded<ParamIndex, 32> shuffled {};
    for (auto const p : active)
        dyn::Append(shuffled, p);

    auto const num = (u32)Round(curve * (f32)shuffled.size);
    for (auto const i : Range(num)) {
        auto const swap_idx = RandomIntInRange<usize>(seed, i, shuffled.size - 1);
        Swap(shuffled[i], shuffled[swap_idx]);
    }

    auto const max_magnitude = 0.7f + (0.3f * curve);
    for (auto const i : Range(num)) {
        auto const idx = ToInt(shuffled[i]);
        auto const magnitude = RandomFloatInRange<f32>(seed, 0.3f, max_magnitude);
        auto const sign = RandomFloatInRange<f32>(seed, 0, 1) < 0.5f ? -1.0f : 1.0f;
        target.param_values[idx] = Clamp(source.param_values[idx] + (sign * magnitude), 0.0f, 1.0f);
    }
}

// Toggles the on-state of a few effects (count rises with amount). For each effect that ends
// up newly enabled, every inner param is reseeded to a fresh random value so the effect lands
// somewhere unexpected rather than reusing whatever was set previously.
static void VaryEffects(u64& seed, StateSnapshot const& source, StateSnapshot& target, f32 amount) {
    auto const num_to_vary = (u32)Round(amount * 4.0f);
    if (num_to_vary == 0) return;

    DynamicArrayBounded<EffectType, k_num_effect_types> shuffled {};
    for (auto const i : Range<u32>(k_num_effect_types))
        dyn::Append(shuffled, (EffectType)i);
    for (auto const i : Range(num_to_vary)) {
        auto const swap_idx = RandomIntInRange<usize>(seed, i, shuffled.size - 1);
        Swap(shuffled[i], shuffled[swap_idx]);
    }

    for (auto const i : Range(num_to_vary)) {
        auto const effect_type = shuffled[i];
        auto const& info = k_effect_info[ToInt(effect_type)];
        auto const on_idx = ToInt(info.on_param_index);
        bool const will_be_on = !(source.param_values[on_idx] > 0.5f);
        target.param_values[on_idx] = will_be_on ? 1.0f : 0.0f;
        target.fx_visible.SetToValue(ToInt(effect_type), will_be_on);
        if (!will_be_on) continue;

        auto const module = EffectTypeToParameterModule(effect_type);
        for (auto const& desc : k_param_descriptors) {
            if (desc.module_parts[0] != ParameterModule::Effect) continue;
            if (desc.module_parts[1] != module) continue;
            if (desc.flags.legacy) continue;
            if (desc.index == info.on_param_index) continue;
            if (desc.index == info.mix_param_index) continue;

            auto const idx = ToInt(desc.index);

            // Output/makeup gains: keep at default so newly-enabled effects don't suddenly
            // bring the patch up or down in level.
            bool const keep_default =
                desc.index == ParamIndex::CompressorGain || desc.index == ParamIndex::ChorusOutput ||
                desc.index == ParamIndex::ConvolutionReverbOutput || desc.index == ParamIndex::BitCrushOutput;
            if (keep_default) {
                target.param_values[idx] = desc.default_linear_value;
                continue;
            }

            auto range = desc.linear_range;
            // Filter/EQ gains span ±30 dB; constrain so randomization can't produce huge
            // boosts or cuts even at amount=1.
            bool const is_risky_gain = desc.index == ParamIndex::FilterGain ||
                                       desc.index == ParamIndex::EqGain1 ||
                                       desc.index == ParamIndex::EqGain2 || desc.index == ParamIndex::EqGain3;
            if (is_risky_gain) {
                range.min = Max(range.min, -0.4f);
                range.max = Min(range.max, 0.4f);
            }
            // Sample a band around the default that grows with amount. Sampling within an
            // already-narrowed band (rather than sampling the full range and clamping)
            // avoids the bunching at min/max that clamping produces.
            auto const d = Clamp(desc.default_linear_value, range.min, range.max);
            auto const lo = d + ((range.min - d) * amount);
            auto const hi = d + ((range.max - d) * amount);
            target.param_values[idx] = RandomFloatInRange<f32>(seed, lo, hi);
        }
    }
}

// Distance from origin instrument to candidate. Lower = more similar. Negative = exclude.
// The metric is intentionally extensible: future signals (timbral features, etc.) can subtract
// from the base distance to bring otherwise-far instruments closer.
static f32 InstrumentDistance(sample_lib::Instrument const& origin, sample_lib::Instrument const& candidate) {
    if (&origin == &candidate) return -1.0f;

    auto const base = ({
        f32 b;
        if (origin.library.id != candidate.library.id)
            b = 1.0f;
        else if (origin.folder && candidate.folder && IsInsideFolder(candidate.folder, origin.folder->Hash()))
            b = 0.1f;
        else
            b = 0.5f;
        b;
    });

    // Tag-overlap pulls candidates closer (Jaccard on the shared tag bitset). Capped so a
    // shared-tag instrument in another library can't outrank an unrelated folder-mate.
    auto const tag_similarity = ({
        f32 s = 0.0f;
        auto const union_count = (origin.tags | candidate.tags).NumSet();
        if (union_count != 0) {
            auto const intersection_count = (origin.tags & candidate.tags).NumSet();
            s = (f32)intersection_count / (f32)union_count;
        }
        s;
    });
    constexpr f32 k_max_tag_pull = 0.35f;
    return Max(0.0f, base - (tag_similarity * k_max_tag_pull));
}

// Per-distance-level weight. amount in [0,1] is the similarity-radius control: at 0, only the
// closest level wins meaningfully; at 1, all levels are equally likely. Distances inside the
// radius get full weight; beyond, weight tapers smoothly.
static f32 DistanceWeight(f32 distance, f32 amount) {
    constexpr f32 k_falloff_exponent = 2.0f;
    auto const overshoot = Max(0.0f, distance - amount);
    return Pow(Max(0.0f, 1.0f - overshoot), k_falloff_exponent);
}

// Picks an instrument uniformly across all libraries. Used when there's no origin to
// measure distance from (e.g. blank state with nothing pinned).
static Optional<sample_lib::InstrumentId> PickAnyInstrument(u64& seed,
                                                            sample_lib_server::LibrariesSpan libraries) {
    usize total = 0;
    for (auto const& lib : libraries)
        total += lib->sorted_instruments.size;
    if (total == 0) return k_nullopt;

    auto pick = RandomIntInRange<usize>(seed, 0, total - 1);
    for (auto const& lib : libraries) {
        if (pick < lib->sorted_instruments.size) {
            auto const* inst = lib->sorted_instruments[pick];
            return sample_lib::InstrumentId {.library = inst->library.id, .inst_id = inst->id};
        }
        pick -= lib->sorted_instruments.size;
    }
    return k_nullopt;
}

// Picks an instrument by (1) computing per-candidate distance, (2) grouping by distance level
// and weighting each level by amount, (3) picking a level proportionally, (4) picking
// uniformly within that level. Bucket-normalising by level prevents large libraries from
// drowning small folder-mate sets.
static Optional<sample_lib::InstrumentId> PickRandomInstrument(u64& seed,
                                                               ArenaAllocator& arena,
                                                               sample_lib_server::LibrariesSpan libraries,
                                                               sample_lib::InstrumentId const& origin_id,
                                                               f32 amount) {
    sample_lib::Instrument const* origin_inst = nullptr;
    for (auto const& lib : libraries) {
        if (lib->id == origin_id.library) {
            if (auto const inst_pp = lib->insts_by_id.Find(origin_id.inst_id)) origin_inst = *inst_pp;
            break;
        }
    }
    if (!origin_inst) return k_nullopt;

    struct Candidate {
        sample_lib::Instrument const* inst;
        f32 distance;
    };
    DynamicArray<Candidate> candidates {arena};
    for (auto const& lib : libraries)
        for (auto const* inst : lib->sorted_instruments) {
            auto const d = InstrumentDistance(*origin_inst, *inst);
            if (d < 0) continue;
            // Quantise so instruments with similar (but not identical) distances share a level
            // and the bucket-normalisation prevents large libraries from drowning small ones.
            constexpr f32 k_quantum = 0.1f;
            auto const quantised = Round(d / k_quantum) * k_quantum;
            dyn::Append(candidates, {inst, quantised});
        }

    if (candidates.size == 0) return k_nullopt;

    DynamicArrayBounded<f32, 16> distance_levels {};
    for (auto const& c : candidates) {
        bool present = false;
        for (auto const d : distance_levels)
            if (d == c.distance) {
                present = true;
                break;
            }
        if (!present) dyn::Append(distance_levels, c.distance);
    }

    f32 total_weight = 0;
    for (auto const d : distance_levels)
        total_weight += DistanceWeight(d, amount);
    if (total_weight <= 0) return k_nullopt;

    auto const r = RandomFloatInRange<f32>(seed, 0, total_weight);
    f32 acc = 0;
    f32 picked_distance = distance_levels[0];
    for (auto const d : distance_levels) {
        acc += DistanceWeight(d, amount);
        if (r <= acc) {
            picked_distance = d;
            break;
        }
    }

    DynamicArray<sample_lib::Instrument const*> at_level {arena};
    for (auto const& c : candidates)
        if (c.distance == picked_distance) dyn::Append(at_level, c.inst);
    if (at_level.size == 0) return k_nullopt;

    auto const pick = RandomIntInRange<usize>(seed, 0, at_level.size - 1);
    return sample_lib::InstrumentId {.library = at_level[pick]->library.id, .inst_id = at_level[pick]->id};
}

void LoadRandomVariation(Engine& engine, f32 amount) {
    ASSERT(g_is_logical_main_thread);
    amount = Clamp(amount, 0.0f, 1.0f);

    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(16)};
    auto libraries =
        sample_lib_server::AllLibrariesRetained(engine.shared_engine_systems.sample_library_server,
                                                scratch_arena);
    DEFER { sample_lib_server::ReleaseAll(libraries); };

    auto const& pinned = engine.pinned_snapshot.state;
    auto& seed = engine.random_seed;
    auto snapshot = pinned;

    Array<bool, k_num_layers> pinned_was_sampler {};
    u32 pinned_sampler_count = 0;
    for (auto const layer_index : Range(k_num_layers))
        if (pinned.inst_ids[layer_index].TryGet<sample_lib::InstrumentId>()) {
            pinned_was_sampler[layer_index] = true;
            ++pinned_sampler_count;
        }

    // Blank state (nothing pinned to vary from): seed one layer with a uniformly-random
    // instrument, then derive any additional layers from it via the usual distance-based
    // pick. Without this branch, every layer would skip for lack of an origin.
    if (pinned_sampler_count == 0) {
        DynamicArrayBounded<u32, k_num_layers> eligible_layers {};
        for (auto const layer_index : Range<u32>(k_num_layers))
            if (pinned.inst_ids[layer_index].tag != InstrumentType::WaveformSynth)
                dyn::Append(eligible_layers, layer_index);

        if (eligible_layers.size != 0) {
            if (auto const seed_inst = PickAnyInstrument(seed, libraries)) {
                snapshot.inst_ids[eligible_layers[0]] = *seed_inst;
                auto const populate_probability = 0.4f + (0.4f * amount);
                for (auto const i : Range<usize>(1, eligible_layers.size)) {
                    if (RandomFloatInRange<f32>(seed, 0, 1) >= populate_probability) continue;
                    if (auto const new_id =
                            PickRandomInstrument(seed, scratch_arena, libraries, *seed_inst, amount))
                        snapshot.inst_ids[eligible_layers[i]] = *new_id;
                }
            }
        }

        // Octave shifts on populated layers still apply below; macros/fx subsets are
        // no-ops since nothing was pinned to perturb.
    } else {
        // Decide which sampler slots will be present after variation. WaveformSynth layers
        // are preserved as-is. Add/remove probabilities are asymmetric: removing a layer is
        // much rarer than adding one, so wilder variations still occasionally drop
        // instruments but don't tend to collapse to a single sampler.
        Array<bool, k_num_layers> will_have_sampler = pinned_was_sampler;
        // Softer ease-in than EaseInQuad so adding new layers is a touch more likely in the
        // mid-amount range, while still tapering toward zero at the bottom.
        auto const add_probability = Pow(amount, 1.5f) * 0.5f;
        auto const remove_probability = EaseInQuad(amount) * 0.25f;
        for (auto const layer_index : Range(k_num_layers)) {
            if (pinned.inst_ids[layer_index].tag == InstrumentType::WaveformSynth) continue;
            auto const toggle_probability =
                pinned_was_sampler[layer_index] ? remove_probability : add_probability;
            if (RandomFloatInRange<f32>(seed, 0, 1) < toggle_probability)
                will_have_sampler[layer_index] = !will_have_sampler[layer_index];
        }

        // Guarantee at least one sampler remains.
        bool any_will = false;
        for (auto const v : will_have_sampler)
            if (v) any_will = true;
        if (!any_will) {
            DynamicArrayBounded<u32, k_num_layers> restorable_layers {};
            for (auto const layer_index : Range<u32>(k_num_layers))
                if (pinned_was_sampler[layer_index]) dyn::Append(restorable_layers, layer_index);
            auto const pick = RandomIntInRange<usize>(seed, 0, restorable_layers.size - 1);
            will_have_sampler[restorable_layers[pick]] = true;
        }

        // Reference instrument for layers being newly populated (no pinned id of their own).
        sample_lib::InstrumentId const* reference_id = nullptr;
        for (auto const layer_index : Range(k_num_layers))
            if (auto const* id = pinned.inst_ids[layer_index].TryGet<sample_lib::InstrumentId>()) {
                reference_id = id;
                break;
            }

        for (auto const layer_index : Range(k_num_layers)) {
            auto const& pinned_id = pinned.inst_ids[layer_index];
            if (pinned_id.tag == InstrumentType::WaveformSynth) continue;

            if (!will_have_sampler[layer_index]) {
                snapshot.inst_ids[layer_index] = InstrumentType::None;
                continue;
            }

            auto const* pinned_sampler = pinned_id.TryGet<sample_lib::InstrumentId>();

            // When multiple sampler slots are populated, occasionally leave one untouched
            // so variations don't always replace every instrument. Probability falls off
            // with amount so wilder variations are still likely to swap everything out.
            if (pinned_sampler && pinned_sampler_count >= 2) {
                auto const keep_probability = (1.0f - amount) * 0.5f;
                if (RandomFloatInRange<f32>(seed, 0, 1) < keep_probability) continue;
            }

            auto const* origin = pinned_sampler ? pinned_sampler : reference_id;
            if (!origin) continue;
            if (auto const new_id = PickRandomInstrument(seed, scratch_arena, libraries, *origin, amount))
                snapshot.inst_ids[layer_index] = *new_id;
            else if (!pinned_sampler)
                snapshot.inst_ids[layer_index] = InstrumentType::None;
        }
    }

    // Ease-out curve so subset variations ramp up faster across the lower-mid range.
    auto const subset_curve = EaseOutQuad(amount);

    DynamicArrayBounded<ParamIndex, k_num_macros> active_macros {};
    for (auto const [macro_index, param_index] : Enumerate<u32>(k_macro_params))
        if (engine.processor.main_macro_destinations[macro_index].Size() != 0)
            dyn::Append(active_macros, param_index);
    VarySubsetOfParams(seed, pinned, snapshot, active_macros, subset_curve);

    DynamicArrayBounded<ParamIndex, k_num_effect_types> active_fx_mix {};
    for (auto const& info : k_effect_info)
        if (pinned.param_values[ToInt(info.on_param_index)] > 0.5f)
            dyn::Append(active_fx_mix, info.mix_param_index);
    VarySubsetOfParams(seed, pinned, snapshot, active_fx_mix, subset_curve);

    VaryEffects(seed, pinned, snapshot, amount);

    for (auto const macro_index : Range(k_num_macros)) {
        auto& destinations = snapshot.macro_destinations[macro_index];
        for (usize i = destinations.Size(); i-- != 0;) {
            auto const param_index = *destinations.items[i].param_index;
            if (!IsParamCurrentlyRelevant(param_index, snapshot.param_values)) destinations.RemoveAt(i);
        }
        if (destinations.Size() == 0) snapshot.macro_names[macro_index] = DefaultMacroNames()[macro_index];
    }

    // Octave shift: small chance per active layer to transpose by ±12 or ±24 semitones.
    // Stays a flavour rather than the rule, so probability rises only mildly with amount.
    auto const octave_shift_probability = 0.15f + (0.15f * amount);
    for (auto const layer_index : Range<u32>(k_num_layers)) {
        if (snapshot.inst_ids[layer_index].tag == InstrumentType::None) continue;
        if (RandomFloatInRange<f32>(seed, 0, 1) >= octave_shift_probability) continue;

        constexpr Array k_octave_choices = {-24.0f, -12.0f, 12.0f, 24.0f};
        auto const shift = k_octave_choices[RandomIntInRange<usize>(seed, 0, k_octave_choices.size - 1)];
        auto const param = ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::MidiTranspose);
        auto const& desc = k_param_descriptors[ToInt(param)];
        snapshot.param_values[ToInt(param)] =
            Clamp(pinned.param_values[ToInt(param)] + shift, desc.linear_range.min, desc.linear_range.max);
    }

    LoadState(engine, snapshot, {.source = StateSource::PresetFile, .update_pinned_snapshot = false});
    RecordUndoableStep(engine, "Random variation"_s);
}
