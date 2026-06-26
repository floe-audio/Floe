// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_description.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

enum class PhraseKind : u8 {
    SlowAttack,
    Percussive,
    GranularSpeed,
    GranularPosition,
    Looping,
    MicroLoop,
    Arpeggiated,
    ArpeggiatedLayer,
    FixedArpeggio,
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
    FxEq,
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
        case PhraseKind::GranularSpeed: {
            static constexpr PhraseText k_variants[] = {
                {"granular time-stretch"_s, "granular time-stretching"_s},
                {"granular speed"_s, "a granular speed engine"_s},
                {"grain-stretched"_s, "grain time-stretching"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::GranularPosition: {
            static constexpr PhraseText k_variants[] = {
                {"frozen granular"_s, "a frozen grain cloud"_s},
                {"granular freeze"_s, "granular position scrubbing"_s},
                {"grain-frozen"_s, "a grain cloud"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::Looping: return {"looping"_s, "looping"_s};
        case PhraseKind::Percussive: {
            static constexpr PhraseText k_variants[] = {
                {"percussive"_s, "a percussive envelope"_s},
                {"staccato"_s, "staccato playback"_s},
                {"short and sharp"_s, "a short, sharp envelope"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::Arpeggiated: {
            static constexpr PhraseText k_variants[] = {
                {"arpeggiated"_s, "an arpeggio"_s},
                {"sequenced"_s, "a sequenced pattern"_s},
                {"patterned"_s, "an arpeggiated pattern"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::ArpeggiatedLayer: {
            static constexpr PhraseText k_variants[] = {
                {"arp-layered"_s, "an arpeggiated layer"_s},
                {"part-arpeggiated"_s, "arpeggiated motion underneath"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::FixedArpeggio: {
            static constexpr PhraseText k_variants[] = {
                {"fixed-note arpeggio"_s, "a fixed-note arpeggio"_s},
                {"ostinato"_s, "an ostinato pattern"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::MicroLoop: {
            static constexpr PhraseText k_variants[] = {
                {"micro-looped"_s, "a micro-loop"_s},
                {"tightly looped"_s, "a tight micro-loop"_s},
                {"buzzing micro-loop"_s, "a buzzing micro-loop"_s},
            };
            return pick(k_variants);
        }
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
        case PhraseKind::LfoVolumeSwell: return {"volume pulse"_s, "a volume pulse"_s};
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
                {"vibrato-like"_s, "vibrato-like"_s},
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
        case PhraseKind::Bitcrushed: {
            static constexpr PhraseText k_variants[] = {
                {"heavily bitcrushed"_s, "heavy bitcrushing"_s},
                {"deeply crushed"_s, "deep bitcrushing"_s},
                {"lo-fi crushed"_s, "an intense bitcrush"_s},
            };
            return pick(k_variants);
        }
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
                {"wrapped in reverb"_s, "a wrapping convolution space"_s},
            };
            return pick(k_variants);
        }
        case PhraseKind::FxDistortion: return {"with a distorted edge"_s, "a distorted edge"_s};
        case PhraseKind::FxBitCrush: return {"with a digital grit"_s, "digital grit"_s};
        case PhraseKind::FxCompressor: return {"tightened with compression"_s, "tightening compression"_s};
        case PhraseKind::FxFilter: return {"shaped by filtering"_s, "shaping filtering"_s};
        case PhraseKind::FxStereoWiden: return {"with a widened image"_s, "a widened stereo image"_s};
        case PhraseKind::FxChorus: return {"with a chorused shimmer"_s, "a chorused shimmer"_s};
        case PhraseKind::FxDelay: return {"with delayed echoes"_s, "delayed echoes"_s};
        case PhraseKind::FxPhaser: return {"with a phaser sweep"_s, "a phaser sweep"_s};
        case PhraseKind::FxEq: return {"with EQ shaping"_s, "EQ shaping"_s};
        case PhraseKind::FxReverb: return {"with a reverberant tail"_s, "a reverberant tail"_s};
        case PhraseKind::FxConvolutionReverb:
            return {"set in a convolution space"_s, "a convolution space"_s};
    }
    return {};
}

String WriteAutoDescription(Allocator& allocator,
                            StateSnapshot const& state,
                            Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                            AutoDescriptionWriteOptions options) {
    DynamicArray<char> out {allocator};

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

    // Count active layers and check for same-instrument stacking
    u32 num_layers = 0;
    bool all_same_instrument = true;
    String first_inst_name {};
    for (u32 i = 0; i < k_num_layers; i++) {
        if (state.inst_ids[i].tag == InstrumentType::None || lp(i, LayerParamIndex::Mute) >= 0.5f) continue;
        num_layers++;
        auto const name = layer_info[i].inst_name;
        if (name.size) {
            if (!first_inst_name.size)
                first_inst_name = name;
            else if (name != first_inst_name)
                all_same_instrument = false;
        }
    }

    if (num_layers == 0) return out.ToOwnedSpan();

    // Analyse per-layer characteristics
    u32 slow_attack_count = 0;
    u32 percussive_count = 0;
    u32 envelope_layer_count = 0;
    bool any_filter_active = false;
    bool any_filter_sweep = false;
    bool any_lfo_active = false;
    Optional<param_values::LfoDestination> lfo_dest {};
    f32 lfo_rate = 0;
    Optional<param_values::LfoShape> lfo_shape {};
    bool any_looping = false;
    bool any_micro_loop = false;
    u32 granular_speed_count = 0;
    u32 granular_position_count = 0;
    u32 mono_layer_count = 0;
    u32 arp_layer_count = 0;
    bool any_arp_fixed = false;

    for (u32 i = 0; i < k_num_layers; i++) {
        if (state.inst_ids[i].tag == InstrumentType::None) continue;
        if (lp(i, LayerParamIndex::Mute) >= 0.5f) continue;

        // Attack
        if (lp(i, LayerParamIndex::VolEnvOn) >= 0.5f) {
            envelope_layer_count++;
            auto const attack = lp(i, LayerParamIndex::VolumeAttack);
            auto const decay = lp(i, LayerParamIndex::VolumeDecay);
            auto const sustain = lp(i, LayerParamIndex::VolumeSustain);
            if (attack > 0.4f)
                slow_attack_count++;
            else if (attack < 0.05f && sustain < 0.3f && decay < 0.35f)
                percussive_count++;
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

        // Arpeggiator
        if (lp(i, LayerParamIndex::ArpOn) >= 0.5f) {
            arp_layer_count++;
            auto const mode = (param_values::ArpMode)(u8)lp(i, LayerParamIndex::ArpMode);
            if (mode == param_values::ArpMode::Fixed) any_arp_fixed = true;
        }

        // Playback mode and looping
        auto const play_mode = (param_values::PlayMode)(u8)lp(i, LayerParamIndex::PlayMode);
        if (play_mode == param_values::PlayMode::GranularPlayback) {
            granular_speed_count++;
        } else if (play_mode == param_values::PlayMode::GranularFixed) {
            granular_position_count++;
        } else {
            switch (layer_info[i].actual_loop_behaviour.value.id) {
                case LoopBehaviourId::NoLoop: break;
                case LoopBehaviourId::CustomLoopStandard:
                case LoopBehaviourId::CustomLoopPingPong: {
                    any_looping = true;
                    auto const loop_length =
                        lp(i, LayerParamIndex::LoopEnd) - lp(i, LayerParamIndex::LoopStart);
                    if (loop_length > 0 && loop_length < 0.02f) any_micro_loop = true;
                    break;
                }
                case LoopBehaviourId::BuiltinLoopStandard:
                case LoopBehaviourId::BuiltinLoopPingPong:
                case LoopBehaviourId::MixedLoops:
                case LoopBehaviourId::MixedNonLoopsAndLoops: break;
            }
        }
    }

    // Build description phrases, each tagged with a salience score (0.0-1.0). Higher salience = more defining
    // characteristic. Phrases are sorted by salience so the most prominent features appear first in the
    // description.
    struct Phrase {
        PhraseKind kind;
        f32 salience;
        // Index into fx_entries (below) if this phrase mentions an FX, else -1.
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
    } else if (envelope_layer_count > 0 && percussive_count == envelope_layer_count) {
        dyn::Append(phrases, Phrase {PhraseKind::Percussive, 0.7f});
    }

    // Layering - only mention the same-instrument stacked case. Plain layer counts ("2 layers", "3 layers")
    // aren't distinctive enough to be worth describing.
    bool const is_stacked = num_layers >= 2 && all_same_instrument && first_inst_name.size;
    // is_stacked is handled during assembly so we can format the instrument name.

    // Playback character
    auto const granular_total = granular_speed_count + granular_position_count;
    if (granular_total > 0)
        if (granular_position_count >= granular_speed_count)
            dyn::Append(phrases, Phrase {PhraseKind::GranularPosition, 0.8f});
        else
            dyn::Append(phrases, Phrase {PhraseKind::GranularSpeed, 0.75f});
    else if (any_micro_loop)
        dyn::Append(phrases, Phrase {PhraseKind::MicroLoop, 0.85f});
    else if (any_looping)
        dyn::Append(phrases, Phrase {PhraseKind::Looping, 0.15f});

    // Arpeggiator: if every active layer is arpeggiated it's the defining feature; if only some are
    // arpeggiated it's a textural element layered over the rest.
    if (arp_layer_count > 0) {
        bool const all_arp = arp_layer_count == num_layers;
        if (any_arp_fixed && all_arp)
            dyn::Append(phrases, Phrase {PhraseKind::FixedArpeggio, 0.85f});
        else if (all_arp)
            dyn::Append(phrases, Phrase {PhraseKind::Arpeggiated, 0.8f});
        else
            dyn::Append(phrases, Phrase {PhraseKind::ArpeggiatedLayer, 0.45f});
    }

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
                                               *lfo_shape == param_values::LfoShape::Square ||
                                               *lfo_shape == param_values::LfoShape::Pluck ||
                                               *lfo_shape == param_values::LfoShape::PluckSharp ||
                                               *lfo_shape == param_values::LfoShape::PulseNarrow ||
                                               *lfo_shape == param_values::LfoShape::PulseWide ||
                                               *lfo_shape == param_values::LfoShape::Trapezoid);
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
        {ParamIndex::EqOn,                PhraseKind::FxEq,                false},
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
        auto const mix = gp(ParamIndex::BitCrushMix);
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
        auto const mix = gp(ParamIndex::ConvolutionReverbMix);
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

    auto const unmentioned_fx = num_fx - mentioned_fx;
    bool const show_trailing_fx_line = unmentioned_fx > 0;

    DynamicArrayBounded<char, k_max_preset_description_size> text {};

    // Headline: instrument name plus the most salient phrase descriptor.
    if (is_stacked)
        fmt::Append(text, "layered {}", first_inst_name);
    else if (num_layers == 1 && first_inst_name.size)
        dyn::AppendSpan(text, first_inst_name);

    if (phrases.size) {
        if (text.size) dyn::AppendSpan(text, ", ");
        dyn::AppendSpan(text, ResolvePhraseText(phrases[0].kind, options.random_seed).descriptor);
    }

    if (!text.size) return out.ToOwnedSpan();

    dyn::AppendSpan(text, "."_s);

    switch (options.form) {
        case AutoDescriptionForm::Headline: {
            if (options.folder_name.size) {
                auto const start = text.size;
                fmt::Append(text, " [{}]", options.folder_name);
                for (usize i = start + 3; i < text.size; i++)
                    if (text[i] >= 'A' && text[i] <= 'Z') text[i] += 32;
            }
            break;
        }
        case AutoDescriptionForm::FullBlock: {
            usize detail_letter = 0;
            if (phrases.size > 1) {
                dyn::AppendSpan(text, " "_s);
                detail_letter = text.size;
                for (usize i = 1; i < phrases.size; i++) {
                    if (i > 1) {
                        if (i == phrases.size - 1)
                            dyn::AppendSpan(text, " and "_s);
                        else
                            dyn::AppendSpan(text, ", "_s);
                    }
                    dyn::AppendSpan(text, ResolvePhraseText(phrases[i].kind, options.random_seed).modifier);
                }
                dyn::AppendSpan(text, "."_s);
            }

            if (show_trailing_fx_line && num_fx) {
                dyn::AppendSpan(text, " "_s);
                if (!detail_letter) detail_letter = text.size;
                fmt::Append(text, "Uses {} effects.", num_fx);
            }

            if (detail_letter && detail_letter < text.size && text[detail_letter] >= 'a' &&
                text[detail_letter] <= 'z') {
                text[detail_letter] -= 32;
            }
            break;
        }
    }

    if (text[0] >= 'a' && text[0] <= 'z') text[0] -= 32;

    dyn::AppendSpan(out, (String)text);
    return out.ToOwnedSpan();
}
