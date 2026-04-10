// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auto_description.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

enum class PhraseKind : u8 {
    SlowAttack,
    SharpAttack,
    DualLayer,
    TripleLayer,
    Granular,
    OneShot,
    Looping,
    Monophonic,
    FilterSwept,
    Filtered,
    LfoGated,
    LfoStuttering,
    LfoTremolo,
    LfoVolumeSwell,
    LfoSteppedFilter,
    LfoFilterWobble,
    LfoAutoPan,
    LfoPitchModulation,
    LfoVibrato,
    LfoPitchWarping,
    HeavyDistortion,
    Bitcrushed,
    WidelyStereo,
    LongWetReverb,
    LongReverb,
    HeavyConvolutionReverb,
    FxDistortion,
    FxBitCrush,
    FxCompressor,
    FxFilter,
    FxStereoWiden,
    FxChorus,
    FxDelay,
    FxPhaser,
    FxReverb,
    FxConvolutionReverb,
};

struct PhraseText {
    // Each has 2 styles so the most appropriate version can be picked when assembling the sentence.
    String descriptor; // used in leading position
    String modifier; // used after "with"
};

static PhraseText ResolvePhraseText(PhraseKind kind, u64 seed) {
    auto const pick = [&](Span<PhraseText const> variants) -> PhraseText {
        auto const mixed = seed ^ ((u64)kind * 0x9E3779B97F4A7C15ULL);
        return variants[mixed % variants.size];
    };
    switch (kind) {
        case PhraseKind::SlowAttack: {
            static constexpr PhraseText k_variants[] = {
                {"slow-attack"_s, "a slow attack"_s},
                {"slowly swelling"_s, "a slow swell"_s},
                {"gradually rising"_s, "a gradual rise"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::SharpAttack: {
            static constexpr PhraseText k_variants[] = {
                {"sharp attack"_s, "a sharp attack"_s},
                {"punchy"_s, "a punchy attack"_s},
                {"snappy"_s, "a snappy attack"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::DualLayer: {
            static constexpr PhraseText k_variants[] = {
                {"dual-layer"_s, "2 layers"_s},
                {"2-layer"_s, "2 layers"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::TripleLayer: {
            static constexpr PhraseText k_variants[] = {
                {"3-layer"_s, "3 layers"_s},
                {"triple-layered"_s, "3 layers"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::Granular: {
            static constexpr PhraseText k_variants[] = {
                {"granular"_s, "granular processing"_s},
                {"grain-engined"_s, "grain processing"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::OneShot: return {"one-shot"_s, "one-shot playback"_s};
        case PhraseKind::Looping: return {"looping"_s, "looping"_s};
        case PhraseKind::Monophonic: {
            static constexpr PhraseText k_variants[] = {
                {"monophonic"_s, "monophonic playback"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::FilterSwept: {
            static constexpr PhraseText k_variants[] = {
                {"filter-swept"_s, "a filter sweep"_s},
                {"sweeping"_s, "a sweep"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::Filtered: return {"filtered"_s, "filtering"_s};
        case PhraseKind::LfoGated: {
            static constexpr PhraseText k_variants[] = {
                {"gated"_s, "gating"_s},
                {"chopped"_s, "chopping"_s},
                {"pulsing"_s, "a pulse"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LfoStuttering: return {"stuttering"_s, "stuttering"_s};
        case PhraseKind::LfoTremolo: {
            static constexpr PhraseText k_variants[] = {
                {"tremolo"_s, "tremolo"_s},
                {"wavering"_s, "a wavering tremolo"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LfoVolumeSwell: return {"swelling"_s, "a volume swell"_s};
        case PhraseKind::LfoSteppedFilter: return {"stepped filter"_s, "a stepped filter"_s};
        case PhraseKind::LfoFilterWobble: {
            static constexpr PhraseText k_variants[] = {
                {"wobbling filter"_s, "a filter wobble"_s},
                {"woozy"_s, "a woozy filter"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LfoAutoPan: return {"auto-panned"_s, "auto-pan"_s};
        case PhraseKind::LfoPitchModulation: return {"pitch-modulated"_s, "pitch modulation"_s};
        case PhraseKind::LfoVibrato: {
            static constexpr PhraseText k_variants[] = {
                {"vibrato"_s, "vibrato"_s},
                {"vibrato-like"_s, "a kind of vibrato"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LfoPitchWarping: {
            static constexpr PhraseText k_variants[] = {
                {"pitch-warped"_s, "pitch warping"_s},
                {"pitch-bent"_s, "pitch bending"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::HeavyDistortion: {
            static constexpr PhraseText k_variants[] = {
                {"heavy distortion"_s, "heavy distortion"_s},
                {"grinding"_s, "grinding distortion"_s},
                {"saturated"_s, "heavy saturation"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::Bitcrushed: return {"bitcrushed"_s, "bitcrush"_s};
        case PhraseKind::WidelyStereo: {
            static constexpr PhraseText k_variants[] = {
                {"widely stereo"_s, "wide stereo"_s},
                {"broad stereo"_s, "a broad stereo field"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LongWetReverb: {
            static constexpr PhraseText k_variants[] = {
                {"long wet reverb"_s, "long wet reverb"_s},
                {"drenched in reverb"_s, "drenched reverb"_s},
                {"cavernous"_s, "cavernous reverb"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::LongReverb: {
            static constexpr PhraseText k_variants[] = {
                {"long reverb"_s, "long reverb"_s},
                {"spacious"_s, "a spacious tail"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::HeavyConvolutionReverb: {
            static constexpr PhraseText k_variants[] = {
                {"heavy convolution reverb"_s, "heavy convolution reverb"_s},
                {"drenched in convolution"_s, "a drenched convolution tail"_s},
                {"immersed in space"_s, "an immersive convolution space"_s},
                {"soaked in convolution"_s, "soaked convolution"_s},
                {"enveloped in reverb"_s, "an enveloping convolution space"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::FxDistortion: return {"distorted"_s, "distortion"_s};
        case PhraseKind::FxBitCrush: return {"bitcrushed"_s, "bitcrush"_s};
        case PhraseKind::FxCompressor: return {"compressed"_s, "compression"_s};
        case PhraseKind::FxFilter: return {"filtered"_s, "filtering"_s};
        case PhraseKind::FxStereoWiden: return {"widened"_s, "stereo widening"_s};
        case PhraseKind::FxChorus: return {"chorus"_s, "chorus"_s};
        case PhraseKind::FxDelay: return {"delayed"_s, "delay"_s};
        case PhraseKind::FxPhaser: return {"phased"_s, "phaser"_s};
        case PhraseKind::FxReverb: return {"reverbed"_s, "reverb"_s};
        case PhraseKind::FxConvolutionReverb: return {"convolution reverb"_s, "convolution reverb"_s};
    }
    return {};
}

DynamicArrayBounded<char, 200>
GenerateAutoDescription(StateSnapshot const& state,
                        Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                        u64 random_seed,
                        s32 max_items) {
    DynamicArrayBounded<char, 200> result {};

    auto const lp = [&](u32 layer, LayerParamIndex p) -> f32 {
        return state.param_values[ToInt(ParamIndexFromLayerParamIndex(layer, p))];
    };
    auto const lp_default = [&](LayerParamIndex p) -> f32 {
        return k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(0, p))].default_linear_value;
    };
    auto const gp = [&](ParamIndex p) -> f32 { return state.LinearParam(p); };
    auto const gp_default = [&](ParamIndex p) -> f32 {
        return k_param_descriptors[ToInt(p)].default_linear_value;
    };
    auto const is_on = [&](ParamIndex p) -> bool { return gp(p) >= 0.5f; };

    // Compute a 0-1 wet mix ratio from separate wet/dry volume params
    auto const wet_dry_mix = [&](ParamIndex wet_param, ParamIndex dry_param) -> f32 {
        auto const wet = gp(wet_param);
        auto const dry = gp(dry_param);
        auto const sum = wet + dry;
        if (sum < 0.001f) return 0.0f;
        return wet / sum;
    };

    // Count active layers and check for same-instrument stacking
    u32 num_layers = 0;
    bool all_same_instrument = true;
    String first_inst_name {};
    DynamicArrayBounded<f32, k_num_layers> layer_volumes {};
    for (u32 i = 0; i < k_num_layers; i++) {
        if (state.inst_ids[i].tag == InstrumentType::None || lp(i, LayerParamIndex::Mute) >= 0.5f) continue;
        num_layers++;
        dyn::Append(layer_volumes, lp(i, LayerParamIndex::Volume));
        auto const name = layer_info[i].inst_name;
        if (name.size) {
            if (!first_inst_name.size)
                first_inst_name = name;
            else if (name != first_inst_name)
                all_same_instrument = false;
        }
    }

    if (num_layers == 0) return result;

    // Compute layer balance: average of (volume / max_volume) across all active layers.
    // 1.0 = perfectly balanced; lower values mean one or more layers dominate the mix.
    f32 layer_balance = 1.0f;
    if (num_layers >= 2) {
        f32 max_vol = 0;
        for (auto v : layer_volumes)
            max_vol = Max(max_vol, v);
        if (max_vol > 0.001f) {
            f32 sum_ratio = 0;
            for (auto v : layer_volumes)
                sum_ratio += v / max_vol;
            layer_balance = sum_ratio / (f32)num_layers;
        }
    }

    // Analyse per-layer characteristics
    u32 slow_attack_count = 0;
    u32 fast_attack_count = 0;
    u32 sharp_attack_count = 0;
    u32 envelope_layer_count = 0;
    bool any_filter_active = false;
    bool any_filter_sweep = false;
    bool any_lfo_active = false;
    Optional<param_values::LfoDestination> lfo_dest {};
    f32 lfo_rate = 0;
    Optional<param_values::LfoShape> lfo_shape {};
    bool any_looping = false;
    bool any_one_shot = false;
    bool any_granular = false;
    u32 mono_layer_count = 0;

    for (u32 i = 0; i < k_num_layers; i++) {
        if (state.inst_ids[i].tag == InstrumentType::None) continue;
        if (lp(i, LayerParamIndex::Mute) >= 0.5f) continue;

        // Attack
        if (lp(i, LayerParamIndex::VolEnvOn) >= 0.5f) {
            envelope_layer_count++;
            auto const attack = lp(i, LayerParamIndex::VolumeAttack);
            auto const sustain = lp(i, LayerParamIndex::VolumeSustain);
            if (attack > 0.4f)
                slow_attack_count++;
            else if (attack < 0.05f && sustain < 0.4f)
                fast_attack_count++;
            else if (attack < 0.05f)
                sharp_attack_count++;
        }

        // Filter
        if (lp(i, LayerParamIndex::FilterOn) >= 0.5f) {
            any_filter_active = true;
            auto const env_amount = lp(i, LayerParamIndex::FilterEnvAmount);
            auto const env_default = lp_default(LayerParamIndex::FilterEnvAmount);
            if (Abs(env_amount - env_default) > 0.15f) any_filter_sweep = true;
        }

        // LFO
        if (lp(i, LayerParamIndex::LfoOn) >= 0.5f) {
            auto const amount = lp(i, LayerParamIndex::LfoAmount);
            auto const amount_default = lp_default(LayerParamIndex::LfoAmount);
            if (Abs(amount - amount_default) > 0.1f) {
                any_lfo_active = true;
                lfo_dest = (param_values::LfoDestination)(u8)lp(i, LayerParamIndex::LfoDestination);
                lfo_rate = lp(i, LayerParamIndex::LfoRateHz);
                lfo_shape = (param_values::LfoShape)(u8)lp(i, LayerParamIndex::LfoShape);
            }
        }

        // Monophonic
        auto const mono_mode = (param_values::MonophonicMode)(u8)lp(i, LayerParamIndex::MonophonicMode);
        if (mono_mode != param_values::MonophonicMode::Off) mono_layer_count++;

        // Playback mode and looping
        auto const play_mode = (param_values::PlayMode)(u8)lp(i, LayerParamIndex::PlayMode);
        if (play_mode != param_values::PlayMode::Standard) {
            any_granular = true;
        } else {
            auto const loop_mode = (param_values::LoopMode)(u8)lp(i, LayerParamIndex::LoopMode);
            if (loop_mode == param_values::LoopMode::None)
                any_one_shot = true;
            else if (loop_mode == param_values::LoopMode::InstrumentDefault && !layer_info[i].inst_has_loops)
                any_one_shot = true;
            else
                any_looping = true;
        }
    }

    // Build description phrases, each tagged with a salience score (0.0-1.0). Higher salience = more defining
    // characteristic. Phrases are sorted by salience so the most prominent features appear first in the
    // description.
    struct Phrase {
        PhraseKind kind;
        f32 salience;
        // Index into fx_entries (below) if this phrase mentions an FX, else -1. Used to recount FX mentions
        // after capping by max_items.
        s32 fx_entry_index = -1;
    };
    DynamicArrayBounded<Phrase, 16> phrases {};

    // Attack character
    bool const all_slow = envelope_layer_count > 0 && slow_attack_count == envelope_layer_count;
    if (all_slow) {
        auto const max_attack = [&]() {
            f32 m = 0;
            for (u32 i = 0; i < k_num_layers; i++) {
                if (state.inst_ids[i].tag == InstrumentType::None) continue;
                if (lp(i, LayerParamIndex::Mute) >= 0.5f) continue;
                if (lp(i, LayerParamIndex::VolEnvOn) < 0.5f) continue;
                m = Max(m, lp(i, LayerParamIndex::VolumeAttack));
            }
            return m;
        }();
        dyn::Append(phrases, Phrase {PhraseKind::SlowAttack, 0.3f + (max_attack * 0.4f)});
    } else if (envelope_layer_count > 0 && (fast_attack_count + sharp_attack_count) == envelope_layer_count) {
        dyn::Append(phrases, Phrase {PhraseKind::SharpAttack, 0.4f});
    }

    // Layering - only mention when layers are reasonably balanced (otherwise one layer dominates and it
    // doesn't really feel "layered"). Keep salience low since most presets use multiple layers and it's not
    // very distinctive.
    bool const is_stacked = num_layers >= 2 && all_same_instrument && first_inst_name.size;
    if (is_stacked) {
        // Handled during assembly so we can format the instrument name
    } else if (num_layers >= 3 && layer_balance > 0.4f) {
        dyn::Append(phrases, Phrase {PhraseKind::TripleLayer, 0.1f});
    } else if (num_layers == 2 && layer_balance > 0.4f) {
        dyn::Append(phrases, Phrase {PhraseKind::DualLayer, 0.1f});
    }

    // Playback character
    if (any_granular)
        dyn::Append(phrases, Phrase {PhraseKind::Granular, 0.7f});
    else if (any_one_shot && !any_looping)
        dyn::Append(phrases, Phrase {PhraseKind::OneShot, 0.2f});
    else if (any_looping && !any_one_shot)
        dyn::Append(phrases, Phrase {PhraseKind::Looping, 0.15f});

    // Monophonic (all active layers)
    if (mono_layer_count > 0 && mono_layer_count == num_layers)
        dyn::Append(phrases, Phrase {PhraseKind::Monophonic, 0.8f});

    // Filter character (skip if LFO is targeting filter, since we'll describe that instead)
    bool const lfo_targets_filter =
        any_lfo_active && lfo_dest && *lfo_dest == param_values::LfoDestination::Filter;
    if (!lfo_targets_filter) {
        if (any_filter_sweep)
            dyn::Append(phrases, Phrase {PhraseKind::FilterSwept, 0.55f});
        else if (any_filter_active)
            dyn::Append(phrases, Phrase {PhraseKind::Filtered, 0.25f});
    }

    // LFO
    if (any_lfo_active && lfo_dest) {
        bool const fast_lfo = lfo_rate > 0.6f;
        bool const slow_lfo = lfo_rate < 0.25f;
        bool const smooth_shape = lfo_shape && (*lfo_shape == param_values::LfoShape::Sine ||
                                                *lfo_shape == param_values::LfoShape::Triangle);
        bool const harsh_shape = lfo_shape && (*lfo_shape == param_values::LfoShape::Sawtooth ||
                                               *lfo_shape == param_values::LfoShape::Square);
        f32 const lfo_salience = 0.4f + (lfo_rate * 0.3f);
        switch (*lfo_dest) {
            case param_values::LfoDestination::Volume:
                if (fast_lfo)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoGated, 0.75f});
                else if (harsh_shape)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoStuttering, 0.65f});
                else if (!slow_lfo)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoTremolo, lfo_salience});
                else
                    dyn::Append(phrases, Phrase {PhraseKind::LfoVolumeSwell, 0.35f});
                break;
            case param_values::LfoDestination::Filter:
                if (harsh_shape)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoSteppedFilter, 0.6f});
                else
                    dyn::Append(phrases, Phrase {PhraseKind::LfoFilterWobble, 0.5f});
                break;
            case param_values::LfoDestination::Pan:
                dyn::Append(phrases, Phrase {PhraseKind::LfoAutoPan, 0.3f});
                break;
            case param_values::LfoDestination::Pitch:
                if (fast_lfo)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoPitchModulation, 0.7f});
                else if (smooth_shape)
                    dyn::Append(phrases, Phrase {PhraseKind::LfoVibrato, 0.4f});
                else
                    dyn::Append(phrases, Phrase {PhraseKind::LfoPitchWarping, 0.6f});
                break;
            case param_values::LfoDestination::GranularPosition: break;
            case param_values::LfoDestination::Count: break;
        }
    }

    // Track which effects are active and whether we've mentioned them
    struct FxEntry {
        ParamIndex on_param;
        PhraseKind generic_kind;
        bool mentioned;
    };
    // clang-format off
    FxEntry fx_entries[] = {
        {ParamIndex::DistortionOn,        PhraseKind::FxDistortion,        false},
        {ParamIndex::BitCrushOn,          PhraseKind::FxBitCrush,          false},
        {ParamIndex::CompressorOn,        PhraseKind::FxCompressor,        false},
        {ParamIndex::FilterOn,            PhraseKind::FxFilter,            false},
        {ParamIndex::StereoWidenOn,       PhraseKind::FxStereoWiden,       false},
        {ParamIndex::ChorusOn,            PhraseKind::FxChorus,            false},
        {ParamIndex::DelayOn,             PhraseKind::FxDelay,             false},
        {ParamIndex::PhaserOn,            PhraseKind::FxPhaser,            false},
        {ParamIndex::ReverbOn,            PhraseKind::FxReverb,            false},
        {ParamIndex::ConvolutionReverbOn, PhraseKind::FxConvolutionReverb, false},
    };
    // clang-format on
    static_assert(ArraySize(fx_entries) == ToInt(EffectType::Count));

    u32 num_fx = 0;
    for (auto& e : fx_entries)
        if (is_on(e.on_param)) num_fx++;

    // Notable effects - mention with descriptive characteristics
    u32 mentioned_fx = 0;
    auto const mention = [&](ParamIndex on_param, PhraseKind kind, f32 salience) {
        s32 fx_index = -1;
        for (s32 i = 0; i < (s32)ArraySize(fx_entries); i++)
            if (fx_entries[i].on_param == on_param) {
                fx_entries[i].mentioned = true;
                fx_index = i;
            }
        dyn::Append(phrases, Phrase {kind, salience, fx_index});
        mentioned_fx++;
    };

    if (is_on(ParamIndex::DistortionOn) && gp(ParamIndex::DistortionDrive) > 0.4f) {
        auto const drive = gp(ParamIndex::DistortionDrive);
        mention(ParamIndex::DistortionOn, PhraseKind::HeavyDistortion, 0.5f + (drive * 0.4f));
    }
    if (is_on(ParamIndex::BitCrushOn)) {
        auto const mix = wet_dry_mix(ParamIndex::BitCrushWet, ParamIndex::BitCrushDry);
        auto const bits_intensity = 1.0f - gp(ParamIndex::BitCrushBits);
        auto const rate_intensity = 1.0f - gp(ParamIndex::BitCrushBitRate);
        auto const crush_intensity = Max(bits_intensity, rate_intensity);
        auto const salience = mix * (0.3f + crush_intensity * 0.6f);
        if (salience > 0.1f) mention(ParamIndex::BitCrushOn, PhraseKind::Bitcrushed, salience);
    }
    if (is_on(ParamIndex::StereoWidenOn) &&
        gp(ParamIndex::StereoWidenWidth) > gp_default(ParamIndex::StereoWidenWidth) + 0.2f) {
        auto const width = gp(ParamIndex::StereoWidenWidth);
        mention(ParamIndex::StereoWidenOn, PhraseKind::WidelyStereo, 0.3f + (width * 0.3f));
    }
    if (is_on(ParamIndex::ReverbOn)) {
        auto const mix = gp(ParamIndex::ReverbMix);
        auto const decay = gp(ParamIndex::ReverbDecayTimeMs);
        if (decay > 0.6f && mix > 0.4f)
            mention(ParamIndex::ReverbOn, PhraseKind::LongWetReverb, 0.4f + (decay * 0.3f));
        else if (decay > 0.6f)
            mention(ParamIndex::ReverbOn, PhraseKind::LongReverb, 0.3f + (decay * 0.2f));
    }
    if (is_on(ParamIndex::ConvolutionReverbOn)) {
        auto const mix = wet_dry_mix(ParamIndex::ConvolutionReverbWet, ParamIndex::ConvolutionReverbDry);
        if (mix > 0.3f)
            mention(ParamIndex::ConvolutionReverbOn, PhraseKind::HeavyConvolutionReverb, 0.4f + (mix * 0.5f));
    }

    // For small FX counts, name any unmentioned ones explicitly
    if (num_fx <= 2) {
        for (s32 i = 0; i < (s32)ArraySize(fx_entries); i++) {
            auto& e = fx_entries[i];
            if (is_on(e.on_param) && !e.mentioned) {
                dyn::Append(phrases, Phrase {e.generic_kind, 0.2f, i});
                mentioned_fx++;
            }
        }
    }

    // Sort all phrases by salience (highest first)
    Sort(phrases, [](Phrase const& a, Phrase const& b) { return a.salience > b.salience; });

    // Cap items. Priority: instrument name first, then phrases by salience, then FX summary.
    bool const has_inst_name_item = is_stacked || (num_layers == 1 && first_inst_name.size);
    if (max_items >= 0) {
        s32 const phrase_budget = Max(0, max_items - (has_inst_name_item ? 1 : 0));
        if (phrases.size > (usize)phrase_budget) phrases.size = (usize)phrase_budget;
    }

    // Recount FX mentions among surviving phrases.
    mentioned_fx = 0;
    for (auto const& p : phrases)
        if (p.fx_entry_index >= 0) mentioned_fx++;

    // Show trailing FX summary if there are unmentioned FX and we have budget for it.
    auto const unmentioned_fx = num_fx - mentioned_fx;
    bool show_trailing_fx_line = unmentioned_fx > 0;
    if (show_trailing_fx_line && max_items >= 0) {
        s32 const used = (has_inst_name_item ? 1 : 0) + (s32)phrases.size;
        if (used >= max_items) show_trailing_fx_line = false;
    }

    // Assemble: first phrase as descriptor, rest as "with modifier, modifier and modifier"
    if (is_stacked)
        fmt::Append(result, "layered {}", first_inst_name);
    else if (num_layers == 1 && first_inst_name.size)
        dyn::AppendSpan(result, first_inst_name);

    if (phrases.size) {
        if (result.size) dyn::AppendSpan(result, ", ");
        dyn::AppendSpan(result, ResolvePhraseText(phrases[0].kind, random_seed).descriptor);

        if (phrases.size > 1) {
            dyn::AppendSpan(result, ", with ");
            for (usize i = 1; i < phrases.size; i++) {
                if (i > 1) {
                    if (i == phrases.size - 1)
                        dyn::AppendSpan(result, " and ");
                    else
                        dyn::AppendSpan(result, ", ");
                }
                dyn::AppendSpan(result, ResolvePhraseText(phrases[i].kind, random_seed).modifier);
            }
        }
    }

    if (show_trailing_fx_line) {
        if (mentioned_fx == 0) {
            if (result.size) dyn::AppendSpan(result, ". ");
            fmt::Append(result, "{} FX", num_fx);
        } else {
            dyn::AppendSpan(result, ", ");
            fmt::Append(result, "+{} more FX", unmentioned_fx);
        }
    }

    // Capitalise first letter
    if (result.size && result[0] >= 'a' && result[0] <= 'z') result[0] -= 32;

    return result;
}
