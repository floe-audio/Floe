// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"

enum class LayerParamIndex : u8 {
    Volume,
    Mute,
    Solo,
    Pan,
    StereoWidth,
    TuneCents,
    LegacyTuneSemitone,
    LoopMode,
    LoopStart,
    LoopEnd,
    LoopCrossfade,
    SampleOffset,
    Reverse,
    VolEnvOn,
    VolumeAttack,
    VolumeDecay,
    VolumeSustain,
    VolumeRelease,
    FilterOn,
    LegacyFilterCutoff,
    FilterCutoff,
    LegacyFilterResonance,
    FilterResonance,
    LegacyFilterType,
    FilterType,
    FilterEnvAmount,
    FilterAttack,
    FilterDecay,
    FilterSustain,
    FilterRelease,
    LfoOn,
    LegacyLfoShape,
    LfoRestart,
    LfoAmount,
    LegacyLfoDestination,
    LegacyLfoRateTempoSynced,
    LfoRateHz,
    LfoSyncSwitch,
    LegacyLfoShapeV2,
    LfoShape,
    LfoDestination,
    EqOn,
    LegacyEqFreq1,
    EqFreq1,
    LegacyEqResonance1,
    EqResonance1,
    EqGain1,
    LegacyEqType1,
    EqType1,
    LegacyEqFreq2,
    EqFreq2,
    LegacyEqResonance2,
    EqResonance2,
    EqGain2,
    LegacyEqType2,
    EqType2,
    LegacyEqFreq3,
    EqFreq3,
    EqResonance3,
    EqGain3,
    EqType3,
    LegacyVelocityMapping,
    Keytrack,
    LegacyMonophonicBool,
    MonophonicMode,
    MidiTranspose,
    PitchBendRange,
    KeyRangeLow,
    KeyRangeHigh,
    KeyRangeLowFade,
    KeyRangeHighFade,

    PlayMode,
    GranularSpeed,
    GranularPosition,
    GranularDensity,
    GranularLength,
    GranularSpread,
    GranularSmoothing,
    GranularRandomPan,
    GranularRandomDetune,
    GranularRandomDirection,
    GranularHarmony,

    ArpOn,
    ArpMode,
    ArpNoteOrder,
    ArpTriggerMode,
    LegacyArpRate,
    ArpAutoRate,
    ArpLength,
    ArpHumanise,
    ArpOctavePolyrate,
    ArpOneShot,

    MpePressDestination,
    MpePressAmount,
    MpeSlideDestination,
    MpeSlideAmount,

    // Reverse-ordered successors of the tempo-synced rate params (higher value = faster). The legacy
    // originals are kept for DAW automation backwards compatibility.
    LfoRateTempoSynced,
    ArpRate,

    // Wider-range successor of the layer semitone pitch param.
    TuneSemitone,

    Count,
};

constexpr auto k_num_layer_parameters = ToInt(LayerParamIndex::Count);

enum class ParamIndex : u16 {
    FirstNonLayerParam = ToInt(LayerParamIndex::Count) * k_num_layers,

    MasterVolume = FirstNonLayerParam,
    LegacyMasterVelocity, // Legacy
    MasterTimbre,

    Macro1,
    Macro2,
    Macro3,
    Macro4,

    LegacyDistortionType,
    DistortionType,
    DistortionDrive,
    DistortionMix,
    DistortionOn,
    DistortionPunish,
    DistortionTilt,
    DistortionGain,
    DistortionAutoGain,

    BitCrushBits,
    BitCrushBitRate,
    LegacyBitCrushWet,
    LegacyBitCrushDry,
    BitCrushMix,
    BitCrushOutput,
    BitCrushOn,

    LegacyCompressorThreshold,
    CompressorThreshold,
    LegacyCompressorRatio,
    CompressorRatio,
    CompressorGain,
    CompressorAutoGain,
    CompressorOn,
    CompressorType,
    CompressorAttack,
    CompressorRelease,
    CompressorMix,

    FilterOn,
    LegacyFilterCutoff,
    FilterCutoff,
    LegacyFilterResonance,
    FilterResonance,
    LegacyFilterGain,
    FilterGain,
    LegacyFilterType,
    FilterType,
    FilterMix,

    StereoWidenWidth,
    StereoWidenOn,
    StereoWidenMode,
    StereoWidenBassMono,
    StereoWidenMix,

    ChorusRate,
    LegacyChorusHighpass,
    ChorusHighpass,
    ChorusDepth,
    LegacyChorusWet,
    LegacyChorusDry,
    ChorusMix,
    ChorusOutput,
    ChorusOn,

    DelayMode,
    DelayFilterCutoffSemitones,
    DelayFilterSpread,
    DelayMix,
    DelayFeedback,
    DelayTimeLMs,
    DelayTimeRMs,
    DelayTimeSyncSwitch,
    LegacyDelayTimeSyncedL,
    LegacyDelayTimeSyncedR,
    DelayOn,

    PhaserCenterSemitones,
    PhaserModFreqHz,
    PhaserModDepth,
    PhaserFeedback,
    PhaserShape,
    PhaserStereoAmount,
    PhaserMix,
    PhaserOn,

    EqOn,
    EqMix,
    EqType1,
    EqFreq1,
    EqResonance1,
    EqGain1,
    EqType2,
    EqFreq2,
    EqResonance2,
    EqGain2,
    EqType3,
    EqFreq3,
    EqResonance3,
    EqGain3,

    LegacyConvolutionReverbHighpass,
    ConvolutionReverbHighpass,
    LegacyConvolutionReverbWet,
    LegacyConvolutionReverbDry,
    ConvolutionReverbMix,
    ConvolutionReverbOutput,
    ConvolutionReverbOn,

    ReverbDecayTimeMs,
    ReverbSize,
    ReverbDelay,
    ReverbMix,
    ReverbPreLowPassCutoff,
    ReverbPreHighPassCutoff,
    ReverbLowShelfCutoff,
    ReverbLowShelfGain,
    ReverbHighShelfCutoff,
    ReverbHighShelfGain,
    ReverbChorusFrequency,
    ReverbChorusAmount,
    ReverbOn,

    LimiterOn,
    LimiterMix,
    LimiterGain,
    LimiterCeiling,

    // Reverse-ordered successors of the tempo-synced delay time params (higher value = faster). The legacy
    // originals are kept for DAW automation backwards compatibility.
    DelayTimeSyncedL,
    DelayTimeSyncedR,

    CountHelper,
    NonLayerParamsCount = CountHelper - FirstNonLayerParam,
};

constexpr auto k_num_parameters =
    (ToInt(LayerParamIndex::Count) * k_num_layers) + ToInt(ParamIndex::NonLayerParamsCount);

enum class ParamDisplayFormat : u8 {
    None,
    Percent,
    Percent2dp,
    Pan,
    SinevibesFilter,
    Ms,
    VolumeAmp,
    Hz,
    VolumeDbRange,
    Cents,
    Semitones,
    Ratio,
    CompressorAttackMs,
    CompressorReleaseMs,
};

enum class ParamValueType : u8 {
    Float,
    Menu,
    Bool,
    Int,
};

struct ParamFlags {
    u8 not_automatable : 1;
    u8 legacy : 1;
    // Experimental params may be removed or changed in future versions without breaking compatibility, they
    // don't require a StateVersion bump, and are defaulted on load if not present in the file. We store
    // params as {id, value} pairs - unknown IDs are skipped on load, so removed experimental params are
    // harmlessly ignored.
    u8 experimental : 1;
    u8 cutoff_frequency : 1; // Will be displayed either semitones or hz - user's preference.
};

enum class ParameterModule : u8 {
    None = 0,

    Layer1,
    Layer2,
    Layer3,

    Effect,
    Master,
    Macro,

    Main,
    Playback,
    Lfo,
    Eq,
    Config,
    Arp,

    VolEnv,
    Filter,
    Loop,
    Granular,

    Distortion,
    Reverb,
    Delay,
    StereoWiden,
    Chorus,
    Phaser,
    ConvolutionReverb,
    Bitcrush,
    Compressor,
    Limiter,

    Band1,
    Band2,
    Band3,

    Count
};

struct ModuleNames {
    String name;
    String abbr;
};

constexpr ModuleNames k_parameter_module_strings[] = {
    {"", ""},

    {"Layer 1", "L1"},
    {"Layer 2", "L2"},
    {"Layer 3", "L3"},

    {"Effect", ""},
    {"Master", "Mst"},
    {"Macro", ""},

    {"Main", ""},
    {"Playback", ""},
    {"LFO", "Lfo"},
    {"EQ", "Eq"},
    {"Config", ""},
    {"Arp", "Arp"},

    {"Volume Envelope", "Vol"},
    {"Filter", "Filt"},
    {"Loop", "Lp"},
    {"Granular", "Grn"},

    {"Distortion", "Dist"},
    {"Reverb", "Rvrb"},
    {"Delay", "Dly"},
    {"StereoWiden", "Ster"},
    {"Chorus", "Chr"},
    {"Phaser", "Phs"},
    {"Convolution Reverb", "Conv"},
    {"Bitcrush", "Bitc"},
    {"Compressor", "Comp"},
    {"Limiter", "Lmtr"},

    {"Band 1", "B1"},
    {"Band 2", "B2"},
    {"Band 3", "B3"},
};

static_assert(ArraySize(k_parameter_module_strings) == ToInt(ParameterModule::Count));

using ParamModules = Array<ParameterModule, 4>;

constexpr Optional<u8> LayerIndexFromModule(ParameterModule m) {
    switch (m) {
        case ParameterModule::Layer1: return (u8)0;
        case ParameterModule::Layer2: return (u8)1;
        case ParameterModule::Layer3: return (u8)2;
        default: return k_nullopt;
    }
}

constexpr ParameterModule LayerModuleFromIndex(u8 layer_index) {
    switch (layer_index) {
        case 0: return ParameterModule::Layer1;
        case 1: return ParameterModule::Layer2;
        case 2: return ParameterModule::Layer3;
    }
    PanicIfReached();
    return ParameterModule::None;
}

namespace param_values {

enum class LegacyEqType : u8 { // never reorder
    Peak,
    LowShelf,
    HighShelf,
    Count,
};
constexpr auto k_legacy_eq_type_strings = ArrayT<String>({
    "Peak",
    "Low-shelf",
    "High-shelf",
});
static_assert(k_legacy_eq_type_strings.size == ToInt(LegacyEqType::Count));

enum class EqType : u8 { // never reorder
    Peak,
    LowShelf,
    HighShelf,
    Notch,
    LowPass12,
    LowPass24,
    HighPass12,
    HighPass24,
    Count,
};
constexpr auto k_eq_type_strings = ArrayT<String>({
    "Peak",
    "Low-shelf",
    "High-shelf",
    "Notch",
    "Low-pass 12dB",
    "Low-pass 24dB",
    "High-pass 12dB",
    "High-pass 24dB",
});
static_assert(k_eq_type_strings.size == ToInt(EqType::Count));

constexpr bool EqTypeUsesGain(EqType t) {
    return t == EqType::Peak || t == EqType::LowShelf || t == EqType::HighShelf;
}

enum class LoopMode : u8 { // never reorder
    InstrumentDefault,
    BuiltInLoopStandard,
    BuiltInLoopPingPong,
    None,
    Standard,
    PingPong,
    Count,
};
constexpr auto k_loop_mode_strings = ArrayT<String>({
    "Default",
    "Built-in Loop - Standard",
    "Built-in Loop - Ping-pong",
    "No Loop",
    "Custom Loop - Standard",
    "Custom Loop - Ping-pong",
});
static_assert(k_loop_mode_strings.size == ToInt(LoopMode::Count));

enum class LegacyLfoSyncedRate : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _1_64T,
    _1_64,
    _1_64D,
    _1_32T,
    _1_32,
    _1_32D,
    _1_16T,
    _1_16,
    _1_16D,
    _1_8T,
    _1_8,
    _1_8D,
    _1_4T,
    _1_4,
    _1_4D,
    _1_2T,
    _1_2,
    _1_2D,
    _1_1T,
    _1_1,
    _1_1D,
    _2_1T,
    _2_1,
    _2_1D,
    _4_1T,
    _4_1,
    _4_1D,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_legacy_lfo_synced_rate_strings = ArrayT<String>({
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D",
    "1/8T",  "1/8",  "1/8D",  "1/4T",  "1/4",  "1/4D",  "1/2T",  "1/2",  "1/2D",
    "1/1T",  "1/1",  "1/1D",  "2/1T",  "2/1",  "2/1D",  "4/1T",  "4/1",  "4/1D",
});
static_assert(k_legacy_lfo_synced_rate_strings.size == ToInt(LegacyLfoSyncedRate::Count));

// Reverse-ordered so that a higher parameter value is a faster rate: dragging the control up/right speeds
// the LFO up, matching expectation. Same set of divisions as the legacy enum; only the numeric ordering
// differs. DSP maps by member name (SyncedTimesFromParam), so the reversal is transparent to it.
enum class LfoSyncedRate : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _4_1D,
    _4_1,
    _4_1T,
    _2_1D,
    _2_1,
    _2_1T,
    _1_1D,
    _1_1,
    _1_1T,
    _1_2D,
    _1_2,
    _1_2T,
    _1_4D,
    _1_4,
    _1_4T,
    _1_8D,
    _1_8,
    _1_8T,
    _1_16D,
    _1_16,
    _1_16T,
    _1_32D,
    _1_32,
    _1_32T,
    _1_64D,
    _1_64,
    _1_64T,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_lfo_synced_rate_strings = []() {
    auto strings = k_legacy_lfo_synced_rate_strings;
    Reverse(strings);
    return strings;
}();
static_assert(k_lfo_synced_rate_strings.size == ToInt(LfoSyncedRate::Count));

enum class LfoRestartMode : u8 { // never reorder
    Retrigger,
    Free,
    Count,
};
constexpr auto k_lfo_restart_mode_strings = ArrayT<String>({
    "Retrigger",
    "Free",
});
static_assert(k_lfo_restart_mode_strings.size == ToInt(LfoRestartMode::Count));
constexpr String LfoRestartModeDescription(LfoRestartMode mode) {
    switch (mode) {
        case LfoRestartMode::Retrigger:
            return "Every note starts the LFO from the beginning of its cycle, so each note gets the same movement from the moment you play it."_s;
        case LfoRestartMode::Free:
            return "A new note joins in wherever this layer's sounding notes have reached in the cycle, so everything moves together. If nothing is sounding, the cycle starts fresh."_s;
        case LfoRestartMode::Count: break;
    }
    return {};
}

enum class LegacyLfoDestination : u8 { // never reorder
    Volume,
    Filter,
    Pan,
    Pitch,
    Count,
};
constexpr auto k_legacy_lfo_destination_strings = ArrayT<String>({
    "Volume",
    "Filter",
    "Pan",
    "Pitch",
});
static_assert(k_legacy_lfo_destination_strings.size == ToInt(LegacyLfoDestination::Count));

enum class LfoDestination : u8 { // never reorder
    Volume,
    Filter,
    Pan,
    Pitch,
    GranularPosition,
    Count,
};
constexpr auto k_lfo_destination_strings = ArrayT<String>({
    "Volume",
    "Filter",
    "Pan",
    "Pitch",
    "Grain Position",
});
static_assert(k_lfo_destination_strings.size == ToInt(LfoDestination::Count));
constexpr String LfoDestinationDescription(LfoDestination dest) {
    switch (dest) {
        case LfoDestination::Volume:
            return "Dips the layer's level down from the volume slider's setting. The top of the dip always stays at the slider's level; Amount controls how far down towards silence the bottom reaches, hitting silence at full Amount."_s;
        case LfoDestination::Filter:
            return "Sweeps the cutoff of the filter on the MAIN tab. At full Amount it moves the cutoff by up to half the knob's travel either way. The filter needs to be switched on to hear it."_s;
        case LfoDestination::Pan:
            return "Sweeps the layer from side to side around its Pan setting. At full Amount it reaches fully left and fully right."_s;
        case LfoDestination::Pitch:
            return "Bends the pitch up and down for vibrato. At full Amount it moves up to a semitone either way."_s;
        case LfoDestination::GranularPosition:
            return "Moves the point in the sample that grains are taken from, scanning up to half the sample either side of the Position setting. Only has an effect in Granular Fixed play mode, chosen on the PLAYBACK tab."_s;
        case LfoDestination::Count: break;
    }
    return {};
}

enum class MpeDestination : u8 { // never reorder
    Off,
    Volume,
    Filter,
    Timbre,
    Count,
};
constexpr auto k_mpe_destination_strings = ArrayT<String>({
    "Off",
    "Volume",
    "Filter",
    "Timbre",
});
static_assert(k_mpe_destination_strings.size == ToInt(MpeDestination::Count));
constexpr String MpeDestinationDescription(MpeDestination destination) {
    switch (destination) {
        case MpeDestination::Off: return {};
        case MpeDestination::Volume:
            return "Fade each note in and out as the gesture moves, up to the layer's Volume setting. Good for swells and for playing one note of a chord louder than the rest."_s;
        case MpeDestination::Filter:
            return "Move the layer's filter cutoff per note, so you can open one note up while the others stay dark. The layer's filter needs to be switched on for this to be heard."_s;
        case MpeDestination::Timbre:
            return "Sweep each note through the Instrument's crossfade layers, the same thing the master Timbre knob does but note by note. Only Instruments built with crossfade layers respond."_s;
        case MpeDestination::Count: break;
    }
    return {};
}

enum class LegacyLfoShapeV1 : u8 { // oldest. never reorder
    Sine,
    Triangle,
    Sawtooth,
    Square,
    Count,
};
constexpr auto k_legacy_lfo_shape_strings = ArrayT<String>({
    "Sine",
    "Triangle",
    "Sawtooth",
    "Square",
});
static_assert(k_legacy_lfo_shape_strings.size == ToInt(LegacyLfoShapeV1::Count));

enum class LegacyLfoShapeV2 : u8 { // never reorder
    Sine,
    Triangle,
    Sawtooth,
    Square,
    RandomSteps,
    RandomGlide,
    Count,
};
constexpr auto k_legacy_lfo_shape_v2_strings = ArrayT<String>({
    "Sine",
    "Triangle",
    "Sawtooth",
    "Square",
    "Random Steps",
    "Random Glide",
});
static_assert(k_legacy_lfo_shape_v2_strings.size == ToInt(LegacyLfoShapeV2::Count));

enum class LfoShape : u8 { // never reorder
    Sine,
    Triangle,
    Sawtooth,
    Square,
    RandomSteps,
    RandomGlide,
    Pluck,
    PluckSharp,
    PulseNarrow,
    PulseWide,
    Trapezoid,
    Count,
};
constexpr auto k_lfo_shape_strings = ArrayT<String>({
    "Sine",
    "Triangle",
    "Sawtooth",
    "Square",
    "Random Steps",
    "Random Glide",
    "Pluck",
    "Pluck Sharp",
    "Pulse Narrow",
    "Pulse Wide",
    "Trapezoid",
});
static_assert(k_lfo_shape_strings.size == ToInt(LfoShape::Count));
constexpr String LfoShapeDescription(LfoShape shape) {
    switch (shape) {
        case LfoShape::Sine:
        case LfoShape::Triangle:
        case LfoShape::Sawtooth:
        case LfoShape::Square:
        case LfoShape::Pluck:
        case LfoShape::PluckSharp:
        case LfoShape::PulseNarrow:
        case LfoShape::PulseWide:
        case LfoShape::Trapezoid: return {};
        case LfoShape::RandomSteps:
            return "Unlike the other shapes, this runs freely rather than following a fixed pattern: it jumps to a new random value at the start of every cycle and holds it there, like a classic sample-and-hold."_s;
        case LfoShape::RandomGlide:
            return "Unlike the other shapes, this runs freely rather than following a fixed pattern: it picks a new random value every cycle and slides smoothly from the previous one to it."_s;
        case LfoShape::Count: break;
    }
    return {};
}

enum class LegacyLayerFilterType : u8 { // never reorder
    Lowpass,
    Bandpass,
    Highpass,
    UnitGainBandpass,
    BandShelving,
    Notch,
    Allpass,
    Peak,
    Count,
};
constexpr auto k_legacy_layer_filter_type_strings = ArrayT<String>({
    "Low-pass",
    "Band-pass A",
    "High-pass",
    "Band-pass B",
    "Band-shelving",
    "Notch",
    "All-pass",
    "Peak",
});
static_assert(k_legacy_layer_filter_type_strings.size == ToInt(LegacyLayerFilterType::Count));

enum class LayerFilterType : u8 { // never reorder
    Lowpass,
    Highpass,
    Bandpass,
    BandpassResonant,
    BandShelving,
    Notch,
    Peak,
    Count,
};
constexpr auto k_layer_filter_type_strings = ArrayT<String>({
    "Low-pass",
    "High-pass",
    "Band-pass",
    "Band-pass (resonant)",
    "Band-shelving",
    "Notch",
    "Peak",
});
static_assert(k_layer_filter_type_strings.size == ToInt(LayerFilterType::Count));
constexpr String LayerFilterTypeDescription(LayerFilterType type) {
    switch (type) {
        case LayerFilterType::Lowpass:
            return "Cuts the frequencies above the cutoff, rolling off at 12 dB per octave. Resonance adds a peak at the cutoff."_s;
        case LayerFilterType::Highpass:
            return "Cuts the frequencies below the cutoff, rolling off at 12 dB per octave. Resonance adds a peak at the cutoff."_s;
        case LayerFilterType::Bandpass:
            return "Keeps only a band around the cutoff, rolling off at 6 dB per octave on each side. The band's level stays fixed; Resonance sets its width, with higher settings giving a narrower band."_s;
        case LayerFilterType::BandpassResonant:
            return "Like Band-pass, but the band's level rises with Resonance, reaching around 20 dB at maximum for a loud, whistling peak."_s;
        case LayerFilterType::BandShelving:
            return "Boosts a band around the cutoff by a fixed 9.5 dB, leaving everything else untouched. Resonance sets its width, with higher settings giving a narrower band."_s;
        case LayerFilterType::Notch:
            return "Cuts a band around the cutoff, leaving everything else untouched. Resonance sets its width, with higher settings giving a narrower notch."_s;
        case LayerFilterType::Peak:
            return "Boosts a band around the cutoff. Resonance sets the size of the boost, from nothing at 0% up to around 26 dB at maximum."_s;
        case LayerFilterType::Count: break;
    }
    return {};
}

enum class LegacyEffectFilterType : u8 { // never reorder
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    Count,
};
constexpr auto k_legacy_effect_filter_type_strings = ArrayT<String>({
    "Low-pass",
    "High-pass",
    "Band-pass",
    "Notch",
    "Peak",
    "Low-shelf",
    "High-shelf",
});
static_assert(k_legacy_effect_filter_type_strings.size == ToInt(LegacyEffectFilterType::Count));

enum class EffectFilterType : u8 { // never reorder
    LowPass12,
    LowPass24,
    HighPass12,
    HighPass24,
    BandPass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    Count,
};
constexpr auto k_effect_filter_type_strings = ArrayT<String>({
    "Low-pass 12dB",
    "Low-pass 24dB",
    "High-pass 12dB",
    "High-pass 24dB",
    "Band-pass",
    "Notch",
    "Peak",
    "Low-shelf",
    "High-shelf",
});
static_assert(k_effect_filter_type_strings.size == ToInt(EffectFilterType::Count));

constexpr bool EffectFilterTypeUsesGain(EffectFilterType t) {
    return t == EffectFilterType::Peak || t == EffectFilterType::LowShelf || t == EffectFilterType::HighShelf;
}

enum class LegacyDistortionType : u8 { // never reorder
    TubeLog,
    TubeAsym3,
    Sine,
    Raph1,
    Decimate,
    Atan,
    Clip,
    Foldback,
    Rectifier,
    RingMod,
    Count,
};
constexpr auto k_legacy_distortion_type_strings = ArrayT<String>({
    "Tube Log (Legacy)",
    "Tube Asym3 (Legacy)",
    "Sine (Legacy)",
    "Raph1 (Legacy)",
    "Decimate (Legacy)",
    "Atan (Legacy)",
    "Clip (Legacy)",
    "Foldback (Legacy)",
    "Rectifier (Legacy)",
    "Ring Mod (Legacy)",
});
static_assert(k_legacy_distortion_type_strings.size == ToInt(LegacyDistortionType::Count));

enum class DistortionType : u8 { // never reorder
    Tape,
    Valve,

    Overdrive,
    HardClip,
    Octave,

    Bitcrush,
    RingMod,
    SineFold,
    Warp,
    Wavefolder,

    LegacyTubeLog,
    LegacyTubeAsym3,
    LegacySine,
    LegacyRaph1,
    LegacyDecimate,
    LegacyAtan,
    LegacyClip,
    LegacyFoldback,
    LegacyRectifier,
    LegacyRingMod,

    Count,
};
// clang-format off
constexpr auto k_distortion_type_strings = ArrayT<String>({
    "Tape",
    "Valve",

    "Overdrive",
    "Hard Clip",
    "Octave",

    "Bitcrush",
    "Ring Mod",
    "Sine Fold",
    "Warp",
    "Wavefolder",

    "Tube Log",
    "Tube Asym3",
    "Sine",
    "Raph1",
    "Tanh (Decimate)",
    "Atan",
    "Clip",
    "Foldback",
    "Rectifier",
    "Ring Mod (44.1k)",
});
// clang-format on
static_assert(k_distortion_type_strings.size == ToInt(DistortionType::Count));

struct DistortionTypeCategory {
    String name;
    Span<DistortionType const> members;
    bool is_legacy;
};
constexpr DistortionType k_bite_distortion_types[] = {
    DistortionType::Tape,
    DistortionType::Valve,
    DistortionType::Overdrive,
    DistortionType::HardClip,
    DistortionType::Octave,
};
constexpr DistortionType k_mangle_distortion_types[] = {
    DistortionType::Bitcrush,
    DistortionType::RingMod,
    DistortionType::SineFold,
    DistortionType::Warp,
    DistortionType::Wavefolder,
};
constexpr DistortionType k_legacy_distortion_types[] = {
    DistortionType::LegacyTubeLog,
    DistortionType::LegacyTubeAsym3,
    DistortionType::LegacySine,
    DistortionType::LegacyRaph1,
    DistortionType::LegacyDecimate,
    DistortionType::LegacyAtan,
    DistortionType::LegacyClip,
    DistortionType::LegacyFoldback,
    DistortionType::LegacyRectifier,
    DistortionType::LegacyRingMod,
};
constexpr DistortionTypeCategory k_distortion_type_categories[] = {
    {"Bite"_s, k_bite_distortion_types, false},
    {"Mangle"_s, k_mangle_distortion_types, false},
    {"Legacy"_s, k_legacy_distortion_types, true},
};

// Legacy types are the original algorithms, kept so older presets sound the same. They have no level
// compensation, so their loudness rises with the drive; the modern types are held steady.
constexpr bool IsLegacyDistortionType(DistortionType type) {
    for (auto const& category : k_distortion_type_categories)
        for (auto const member : category.members)
            if (member == type) return category.is_legacy;
    return false;
}

enum class CompressorType : u8 { // never reorder
    Vintage,
    Modern,
    Count,
};
constexpr auto k_compressor_type_strings = ArrayT<String>({
    "Vintage",
    "Modern",
});
static_assert(k_compressor_type_strings.size == ToInt(CompressorType::Count));

enum class LegacyDelaySyncedTime : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _1_64T,
    _1_64,
    _1_64D,
    _1_32T,
    _1_32,
    _1_32D,
    _1_16T,
    _1_16,
    _1_16D,
    _1_8T,
    _1_8,
    _1_8D,
    _1_4T,
    _1_4,
    _1_4D,
    _1_2T,
    _1_2,
    _1_2D,
    _1_1T,
    _1_1,
    _1_1D,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_legacy_delay_synced_time_strings = ArrayT<String>({
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
    "1/8D",  "1/4T", "1/4",   "1/4D",  "1/2T", "1/2",   "1/2D",  "1/1T", "1/1",   "1/1D",
});
static_assert(k_legacy_delay_synced_time_strings.size == ToInt(LegacyDelaySyncedTime::Count));

// Reverse-ordered so a higher parameter value is a shorter (faster) delay time; see LfoSyncedRate.
enum class DelaySyncedTime : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _1_1D,
    _1_1,
    _1_1T,
    _1_2D,
    _1_2,
    _1_2T,
    _1_4D,
    _1_4,
    _1_4T,
    _1_8D,
    _1_8,
    _1_8T,
    _1_16D,
    _1_16,
    _1_16T,
    _1_32D,
    _1_32,
    _1_32T,
    _1_64D,
    _1_64,
    _1_64T,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_delay_synced_time_strings = []() {
    auto strings = k_legacy_delay_synced_time_strings;
    Reverse(strings);
    return strings;
}();
static_assert(k_delay_synced_time_strings.size == ToInt(DelaySyncedTime::Count));

enum class DelayMode : u8 { // never reorder
    Mono,
    Stereo,
    PingPong,
    MidPingPong,
    Count,
};
constexpr auto k_delay_mode_strings = ArrayT<String>({
    "Mono",
    "Stereo",
    "Ping-pong",
    "Mid ping-pong",
});
static_assert(k_delay_mode_strings.size == ToInt(DelayMode::Count));
constexpr String DelayModeDescription(DelayMode mode) {
    switch (mode) {
        case DelayMode::Mono:
            return "Both channels repeat at the Time L setting, so the echoes sit where the original sound sits in the stereo image. Time R is unused here."_s;
        case DelayMode::Stereo:
            return "The left and right channels each get their own delay line, so setting Time L and Time R apart from each other spreads the repeats across the stereo image."_s;
        case DelayMode::PingPong:
            return "The sound is summed to the centre and the repeats then bounce from one side to the other, with Time L and Time R setting how long each half of the bounce lasts."_s;
        case DelayMode::MidPingPong:
            return "The repeats bounce from side to side as they do in Ping-pong, but the sound keeps its stereo image on the way in."_s;
        case DelayMode::Count: break;
    }
    return {};
}

enum class VelocityMappingMode : u8 { // never reorder
    None,
    TopToBottom,
    BottomToTop,
    TopToMiddle,
    MiddleOutwards,
    MiddleToBottom,
    Count,
};
constexpr auto k_velocity_mapping_mode_strings = ArrayT<String>({
    "None",
    "Top To Bottom",
    "Bottom To Top",
    "Top To Middle",
    "Middle Outwards",
    "Middle To Bottom",
});
static_assert(k_velocity_mapping_mode_strings.size == ToInt(VelocityMappingMode::Count));

enum class MonophonicMode : u8 { // never reorder
    Off,
    Retrigger,
    Latch,
    Count,
};
constexpr auto k_monophonic_mode_strings = ArrayT<String>({
    "Off",
    "Retrigger",
    "Latch",
});
static_assert(k_monophonic_mode_strings.size == ToInt(MonophonicMode::Count));
constexpr String MonophonicModeDescription(MonophonicMode mode) {
    switch (mode) {
        case MonophonicMode::Off:
            return "The layer is polyphonic: hold down a chord and every note sounds."_s;
        case MonophonicMode::Retrigger:
            return "Each new note cuts off whatever was sounding and starts again, so you only ever hear the most recent note. The classic monophonic lead behaviour, and also a way to stop a long sample piling up on itself."_s;
        case MonophonicMode::Latch:
            return "The first note you play keeps sounding and later notes are ignored, until you lift every key. Handy for holding a drone or a long pad from a single key press."_s;
        case MonophonicMode::Count: break;
    }
    return {};
}

enum class StereoWidenMode : u8 { // never reorder
    Balanced,
    BassMono,
    Legacy,
    Count,
};
constexpr auto k_stereo_widen_mode_strings = ArrayT<String>({
    "Balanced",
    "Bass Mono",
    "Legacy",
});
static_assert(k_stereo_widen_mode_strings.size == ToInt(StereoWidenMode::Count));
constexpr String StereoWidenModeDescription(StereoWidenMode mode) {
    switch (mode) {
        case StereoWidenMode::Balanced:
            return "Rebalances the mid and side signals against each other, keeping the perceived loudness the same wherever Width is set. The one to reach for by default."_s;
        case StereoWidenMode::BassMono:
            return "Widens as Balanced does, but sums everything below the Bass Mono frequency to the centre, keeping the low end solid and mono-compatible."_s;
        case StereoWidenMode::Legacy:
            return "The widening used by older versions of Floe, kept so that existing presets sound as they always did. It boosts the side signal as Width goes up, so the level rises with it."_s;
        case StereoWidenMode::Count: break;
    }
    return {};
}

enum class PlayMode : u8 {
    Standard,
    GranularPlayback,
    GranularFixed,
    Count,
};
constexpr auto k_play_mode_strings = ArrayT<String>({
    "Standard Playback",
    "Granular Playback",
    "Granular Fixed",
});
static_assert(k_play_mode_strings.size == ToInt(PlayMode::Count));

enum class ArpMode : u8 { // never reorder
    Played,
    Fixed,
    Count,
};
constexpr auto k_arp_type_strings = ArrayT<String>({
    "Played Notes",
    "Fixed Notes",
});
static_assert(k_arp_type_strings.size == ToInt(ArpMode::Count));

enum class ArpNoteOrder : u8 { // never reorder
    Chord,
    Up,
    Down,
    UpDown,
    DownUp,
    Random,
    RandomNoRepeat,
    UpX2,
    DownX2,
    UpDownX2,
    Converge,
    Diverge,
    Thumb,
    UpPlus,
    Count,
};
constexpr auto k_arp_note_order_strings = ArrayT<String>({
    "Chord",
    "Up",
    "Down",
    "Up/Down",
    "Down/Up",
    "Random",
    "Random No Repeat",
    "Up x2",
    "Down x2",
    "Up/Down x2",
    "Converge",
    "Diverge",
    "Thumb",
    "Up+",
});
static_assert(k_arp_note_order_strings.size == ToInt(ArpNoteOrder::Count));
constexpr String ArpNoteOrderDescription(ArpNoteOrder order) {
    switch (order) {
        case ArpNoteOrder::Chord:
            return "Every held note sounds together on each step, so the pattern becomes rhythmic chord stabs rather than an arpeggio. Ideal for percussion and for chopping a chord into a rhythm."_s;
        case ArpNoteOrder::Up:
        case ArpNoteOrder::Down:
        case ArpNoteOrder::Random: return {};
        case ArpNoteOrder::UpDown:
            return "Climbs to the top note then comes back down. The top and bottom notes aren't repeated at the turnarounds."_s;
        case ArpNoteOrder::DownUp:
            return "Falls to the bottom note then climbs back up. The top and bottom notes aren't repeated at the turnarounds."_s;
        case ArpNoteOrder::RandomNoRepeat:
            return "Picks at random, but avoids landing on the same note twice in a row so the pattern always keeps moving."_s;
        case ArpNoteOrder::UpX2: return "Climbs upwards, playing each note twice before moving on."_s;
        case ArpNoteOrder::DownX2: return "Falls downwards, playing each note twice before moving on."_s;
        case ArpNoteOrder::UpDownX2:
            return "Climbs to the top and back down, playing each note twice before moving on."_s;
        case ArpNoteOrder::Converge:
            return "Works inwards from the outside of the chord: lowest, highest, second lowest, second highest, and so on until it meets in the middle."_s;
        case ArpNoteOrder::Diverge:
            return "Starts in the middle of the chord and works outwards, alternating above and below."_s;
        case ArpNoteOrder::Thumb:
            return "Returns to the lowest note between every other note, like a thumb holding down a bass note while the fingers pick out the rest."_s;
        case ArpNoteOrder::UpPlus:
            return "Climbs upwards, then adds one extra step: the top note an octave higher."_s;
        case ArpNoteOrder::Count: break;
    }
    return {};
}

enum class ArpOctavePolyrate : u8 { // never reorder
    Off,
    Double,
    ThreeToTwo,
    FourToThree,
    Count,
};
constexpr auto k_arp_octave_polyrate_strings = ArrayT<String>({
    "Off",
    "Double at octaves",
    "3:2 at octaves",
    "4:3 at octaves",
});
static_assert(k_arp_octave_polyrate_strings.size == ToInt(ArpOctavePolyrate::Count));
constexpr String ArpOctavePolyrateDescription(ArpOctavePolyrate mode) {
    switch (mode) {
        case ArpOctavePolyrate::Off: return {};
        case ArpOctavePolyrate::Double:
            return "Each octave up runs at twice the speed of the one below. The strongest of the three ratios, and the easiest to hear."_s;
        case ArpOctavePolyrate::ThreeToTwo:
            return "Each octave up fits three notes into the time the octave below takes for two, so the two lines pull apart and meet again."_s;
        case ArpOctavePolyrate::FourToThree:
            return "Each octave up fits four notes into the time the octave below takes for three. The subtlest of the three ratios, and the slowest to come back into line."_s;
        case ArpOctavePolyrate::Count: break;
    }
    return {};
}

enum class ArpTriggerMode : u8 { // never reorder
    Free,
    Retrigger,
    Count,
};
constexpr auto k_arp_trigger_mode_strings = ArrayT<String>({
    "Free",
    "Retrigger",
});
static_assert(k_arp_trigger_mode_strings.size == ToInt(ArpTriggerMode::Count));

enum class ArpAutoRate : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    Off,
    _1x,
    _2x,
    _0_5x,
    _4x,
    _0_25x,
    _1xD,
    _2xD,
    _0_5xD,
    _4xD,
    _0_25xD,
    _1xT,
    _2xT,
    _0_5xT,
    _4xT,
    _0_25xT,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_arp_auto_rate_strings = ArrayT<String>({
    "Off",
    "1x",
    "2x",
    "0.5x",
    "4x",
    "0.25x",
    "1x Dotted",
    "2x Dotted",
    "0.5x Dotted",
    "4x Dotted",
    "0.25x Dotted",
    "1x Triplet",
    "2x Triplet",
    "0.5x Triplet",
    "4x Triplet",
    "0.25x Triplet",
});
static_assert(k_arp_auto_rate_strings.size == ToInt(ArpAutoRate::Count));

enum class LegacyArpSyncedRate : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _1_64T,
    _1_64,
    _1_64D,
    _1_32T,
    _1_32,
    _1_32D,
    _1_16T,
    _1_16,
    _1_16D,
    _1_8T,
    _1_8,
    _1_8D,
    _1_4T,
    _1_4,
    _1_4D,
    _1_2T,
    _1_2,
    _1_2D,
    _1_1T,
    _1_1,
    _1_1D,
    _2_1T,
    _2_1,
    _2_1D,
    _4_1T,
    _4_1,
    _4_1D,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_legacy_arp_synced_rate_strings = ArrayT<String>({
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D",
    "1/8T",  "1/8",  "1/8D",  "1/4T",  "1/4",  "1/4D",  "1/2T",  "1/2",  "1/2D",
    "1/1T",  "1/1",  "1/1D",  "2/1T",  "2/1",  "2/1D",  "4/1T",  "4/1",  "4/1D",
});
static_assert(k_legacy_arp_synced_rate_strings.size == ToInt(LegacyArpSyncedRate::Count));

// Reverse-ordered so a higher parameter value is a faster arpeggiator rate; see LfoSyncedRate.
enum class ArpSyncedRate : u8 { // never reorder
    // NOLINTBEGIN(readability-identifier-naming)
    _4_1D,
    _4_1,
    _4_1T,
    _2_1D,
    _2_1,
    _2_1T,
    _1_1D,
    _1_1,
    _1_1T,
    _1_2D,
    _1_2,
    _1_2T,
    _1_4D,
    _1_4,
    _1_4T,
    _1_8D,
    _1_8,
    _1_8T,
    _1_16D,
    _1_16,
    _1_16T,
    _1_32D,
    _1_32,
    _1_32T,
    _1_64D,
    _1_64,
    _1_64T,
    Count,
    // NOLINTEND(readability-identifier-naming)
};
constexpr auto k_arp_synced_rate_strings = []() {
    auto strings = k_legacy_arp_synced_rate_strings;
    Reverse(strings);
    return strings;
}();
static_assert(k_arp_synced_rate_strings.size == ToInt(ArpSyncedRate::Count));

} // namespace param_values

struct ParamDescriptor {
    enum class MenuType : u8 {
        None,
        LoopMode,
        LegacyEqType,
        EqType,
        LegacyLfoSyncedRate,
        LfoSyncedRate,
        LfoRestartMode,
        LegacyLfoDestination,
        LfoDestination,
        LegacyLfoShape,
        LegacyLfoShapeV2,
        LfoShape,
        LegacyLayerFilterType,
        LayerFilterType,
        LegacyEffectFilterType,
        EffectFilterType,
        LegacyDistortionType,
        DistortionType,
        CompressorType,
        LegacyDelaySyncedTime,
        DelaySyncedTime,
        DelayMode,
        VelocityMappingMode,
        MonophonicMode,
        StereoWidenMode,
        PlayMode,
        ArpMode,
        ArpNoteOrder,
        ArpTriggerMode,
        LegacyArpSyncedRate,
        ArpSyncedRate,
        ArpOctavePolyrate,
        ArpAutoRate,
        MpeDestination,
        Count,
    };

    struct Range {
        constexpr f32 Remap(f32 in, Range out_range) const {
            auto const delta = Delta();
            if (delta == 0) return 0;
            return out_range.min + (((in - min) / delta) * out_range.Delta());
        }
        constexpr f32 RamapTo01(f32 in) const { return (in - min) * (1 / Delta()); }
        constexpr f32 Delta() const { return max - min; }
        constexpr bool Contains(f32 v) const { return v >= min && v <= max; }
        f32 min, max;
    };

    struct Projection {
        // NOTE: we could offer other projections other than just exponential. For examples, a sigmoid
        // function (an s curve).
        //
        // https://www.desmos.com/calculator/uribj4sbw4
        //
        // This function would satisfy the criteria for having a mapping from 0 to 1:
        // f(x) = 1 - (1 / (1 + pow((1 / x) - 1, -k))) where k is a constant that determines the steepness of
        // the curve. Values from 0 to 1 display properties like the tan functions, while values from 1 above
        // display a typical S shape.
        //
        // Additionally, an extra parameter can be added to skew the curve: to change the point at which f(x)
        // = 0.5: g(x) = pow(x, -log(2) / log(t)) where t is the skew factor from 0 to 1.
        //
        // Credits:
        // https://math.stackexchange.com/questions/1832177/sigmoid-function-with-fixed-bounds-and-variable-steepness-partially-solved
        // https://colab.research.google.com/drive/1uaMKr-1dAX231Z7Bdew4MKj-c4vDD604?usp=sharing

        enum class Type : u8 {
            Exponential,
            LinearThenExponential,
            // Log: equal pixels per octave. linear ∈ [0,1] maps to projected ∈ [range.min, range.max]
            // via exp2(lerp(log2(min), log2(max), t)). `exponent` is unused. range.min must be > 0.
            Log,
        };

        constexpr f32 ProjectValue(f32 linear_value, Range linear_range_) const {
            switch (type) {
                case Type::Log: {
                    auto const log2 = [](f32 x) {
                        if consteval {
                            return constexpr_math::Log2f(x);
                        }
                        return Log2(x);
                    };
                    auto const exp2 = [](f32 x) {
                        if consteval {
                            return constexpr_math::Exp2f(x);
                        }
                        return Exp2(x);
                    };
                    auto const value_01 = linear_range_.RamapTo01(linear_value);
                    auto const lo = log2(range.min);
                    auto const hi = log2(range.max);
                    return exp2(lo + (value_01 * (hi - lo)));
                }
                case Type::LinearThenExponential: {
                    auto const value_01 = linear_range_.RamapTo01(linear_value);
                    if (value_01 <= split_01) {
                        auto const t = value_01 / split_01;
                        return range.min + (t * (split_value - range.min));
                    } else {
                        auto const t = (value_01 - split_01) / (1.0f - split_01);
                        return split_value + (Pow(t, exponent) * (range.max - split_value));
                    }
                }
                case Type::Exponential: {
                    if (exponent == 1) return linear_range_.Remap(linear_value, range);

                    if (linear_range_.min == -1 && linear_range_.max == 1) {
                        if (linear_value >= 0)
                            return Abs(range.max) * Pow(linear_value, exponent);
                        else
                            return -Abs(range.min) * Pow(-linear_value, exponent);
                    }

                    auto const value_01 = linear_range_.RamapTo01(linear_value);
                    return range.min + (Pow(value_01, exponent) * range.Delta());
                }
            }
            throw "";
        }

        constexpr f32 LineariseValue(Range linear_range_, f32 projected_value) const {
            auto const pow_fn = [](f32 base, f32 exp) {
                if consteval {
                    return constexpr_math::Powf(base, exp);
                }
                return Pow(base, exp);
            };
            switch (type) {
                case Type::Log: {
                    auto const log2 = [](f32 x) {
                        if consteval {
                            return constexpr_math::Log2f(x);
                        }
                        return Log2(x);
                    };
                    auto const lo = log2(range.min);
                    auto const hi = log2(range.max);
                    auto const value_01 = (log2(projected_value) - lo) / (hi - lo);
                    return linear_range_.min + (value_01 * linear_range_.Delta());
                }
                case Type::LinearThenExponential: {
                    if (projected_value <= split_value) {
                        auto const t = (split_value == range.min)
                                           ? 0.0f
                                           : (projected_value - range.min) / (split_value - range.min);
                        auto const value_01 = t * split_01;
                        return linear_range_.min + (value_01 * linear_range_.Delta());
                    } else {
                        auto const t = (projected_value - split_value) / (range.max - split_value);
                        auto const value_01 = split_01 + (pow_fn(t, 1.0f / exponent) * (1.0f - split_01));
                        return linear_range_.min + (value_01 * linear_range_.Delta());
                    }
                }
                case Type::Exponential: {
                    if (exponent == 1) return range.Remap(projected_value, linear_range_);

                    if (linear_range_.min == -1 && linear_range_.max == 1) {
                        if (projected_value >= 0)
                            return pow_fn(projected_value / range.max, 1 / exponent);
                        else
                            return -pow_fn((-projected_value) / (-range.min), 1 / exponent);
                    }
                    auto const value_01 = range.RamapTo01(projected_value);
                    return linear_range_.min + (pow_fn(value_01, 1 / exponent) * linear_range_.Delta());
                }
            }
            throw "";
        }

        Range range;
        f32 exponent;
        Type type = Type::Exponential;
        f32 split_01 = 0; // For LinearThenExponential: normalized split position in [0, 1]
        f32 split_value = 0; // For LinearThenExponential: projected value at the split point
    };

    constexpr ParamDescriptor() = default;

    struct ConstructorArgs {
        struct ValueConfig {
            Range linear_range;
            Optional<Projection> projection;
            f32 default_linear_value;
            ParamDisplayFormat display_format;
            ParamValueType value_type;
            MenuType menu_type;
        };

        u32 id;
        // A persistent, human-readable identifier in lowercase reverse-domain style (e.g.
        // "fx.compressor.ratio", "layer1.filter.cutoff"). Must be set alongside `id` and must never change
        // once added. The numeric `id` is the primary key used in serialised state and DAW automation; this
        // string is a more easily identifiable alternative for use in logs, scripts, and tooling.
        String id_string;
        // Release generation in which this parameter was first introduced. Generation 0 is the v1.1.2
        // baseline; bump by one for every release that adds new parameters. Used to compute the AUv2
        // parameter ordering so existing automation lanes in Logic/GarageBand stay stable when new
        // parameters are added in later versions.
        u8 added_in_generation;
        ValueConfig value_config;
        ParamModules modules;
        String name;
        String gui_label;
        String tooltip;
        u8 related_params_group;
        ParamFlags flags;
    };

    constexpr ParamDescriptor(ConstructorArgs args)
        : index((ParamIndex)-1)
        , id(args.id)
        , id_string(args.id_string)
        , flags(args.flags)
        , display_format(args.value_config.display_format)
        , value_type(args.value_config.value_type)
        , linear_range(args.value_config.linear_range)
        , default_linear_value(args.value_config.default_linear_value)
        , projection(args.value_config.projection)
        , module_parts(args.modules)
        , name(args.name)
        , gui_label(args.gui_label)
        , tooltip(args.tooltip)
        , menu_type(args.value_config.menu_type)
        , grouping_within_module(args.related_params_group)
        , added_in_generation(args.added_in_generation) {}

    constexpr f32 ProjectValue(f32 linear_value) const {
        ASSERT(linear_range.Contains(linear_value));

        if (projection) return projection->ProjectValue(linear_value, linear_range);

        return linear_value;
    }

    constexpr Range ProjectionRange() const {
        if (projection) return projection->range;
        return linear_range;
    }

    constexpr f32 DefaultProjectedValue() const { return ProjectValue(default_linear_value); }

    // NaN-proof: Clamp passes NaN through, so handle it explicitly.
    constexpr f32 SanitiseLinearValue(f32 linear_value) const {
        if (__builtin_isnan(linear_value)) return default_linear_value;
        return Clamp(linear_value, linear_range.min, linear_range.max);
    }

    constexpr Optional<f32> LineariseValue(f32 projected_value, bool clamp_if_out_of_range) const {
        if (__builtin_isnan(projected_value)) {
            if (clamp_if_out_of_range) return default_linear_value;
            return k_nullopt;
        }
        auto const projection_range = ProjectionRange();
        if (clamp_if_out_of_range)
            projected_value = Clamp(projected_value, projection_range.min, projection_range.max);
        else if (projected_value < projection_range.min || projected_value > projection_range.max)
            return k_nullopt;

        if (projection) return projection->LineariseValue(linear_range, projected_value);

        return projected_value;
    }

    // show_cutoff_in_semitones only has any effect on params with flags.cutoff_frequency set: nullopt keeps
    // the param's native display_format, otherwise it overrides the unit to Hz (false) or semitones (true).
    Optional<f32> StringToLinearValue(String str, Optional<bool> show_cutoff_in_semitones = k_nullopt) const;
    Optional<DynamicArrayBounded<char, 128>>
    LinearValueToString(f32 linear_value, Optional<bool> show_cutoff_in_semitones = k_nullopt) const;

    constexpr bool IsEffectParam() const { return module_parts[0] == ParameterModule::Effect; }
    constexpr bool IsLayerParam() const { return LayerIndexFromModule(module_parts[0]).HasValue(); }

    DynamicArrayBounded<char, 128> ModuleString(String separator = "/", bool abbreviated = false) const {
        DynamicArrayBounded<char, 128> result {};
        for (auto m : module_parts) {
            if (m == ParameterModule::None) break;
            if (result.size != 0) dyn::AppendSpan(result, separator);
            dyn::AppendSpan(result,
                            !abbreviated ? k_parameter_module_strings[int(m)].name
                                         : k_parameter_module_strings[int(m)].abbr);
        }
        return result;
    }

    ParamIndex index;
    u32 id; // never change
    // The numeric `id` above is the primary identifier (used in serialised state and DAW automation).
    // `id_string` is a more easily identifiable alternative for some cases (logs, scripts, tooling). Both
    // are unique and stable - once added, neither can change.
    String id_string;
    ParamFlags flags;
    ParamDisplayFormat display_format;
    ParamValueType value_type;
    Range linear_range;
    f32 default_linear_value;
    Optional<Projection> projection;
    ParamModules module_parts;
    String name;
    String gui_label;
    String tooltip;
    MenuType menu_type;
    // If non-zero, signifies that it might be shown grouped with others with the same module_parts in
    // ascending order.
    u8 grouping_within_module;
    u8 added_in_generation;
};

constexpr ParamIndex ParamIndexFromLayerParamIndex(u32 layer_index, LayerParamIndex layer_param_index) {
    return ParamIndex((layer_index * (u32)LayerParamIndex::Count) + (u32)layer_param_index);
}

constexpr Optional<u8> MacroIndexFromParamIndex(ParamIndex p) {
    switch (p) {
        case ParamIndex::Macro1: return (u8)0;
        case ParamIndex::Macro2: return (u8)1;
        case ParamIndex::Macro3: return (u8)2;
        case ParamIndex::Macro4: return (u8)3;
        default: return k_nullopt;
    }
}

constexpr ParamIndex ParamIndexFromMacroIndex(u8 macro_index) {
    switch (macro_index) {
        case 0: return ParamIndex::Macro1;
        case 1: return ParamIndex::Macro2;
        case 2: return ParamIndex::Macro3;
        case 3: return ParamIndex::Macro4;
    }
    PanicIfReached();
    return ParamIndex::Macro1;
}

constexpr bool IsLayerParamOfSpecificType(ParamIndex global_index, LayerParamIndex layer_index) {
    for (auto const i : Range(k_num_layers))
        if (global_index == ParamIndexFromLayerParamIndex(i, layer_index)) return true;
    return false;
}

struct LayerParamIndexAndLayer {
    LayerParamIndex param;
    u32 layer_num;
};

constexpr Optional<LayerParamIndexAndLayer> LayerParamIndexAndLayerFor(ParamIndex global_index) {
    if (global_index >= ParamIndex::FirstNonLayerParam) return k_nullopt;

    return LayerParamIndexAndLayer {
        .param = (LayerParamIndex)(ToInt(global_index) % k_num_layer_parameters),
        .layer_num = ToInt(global_index) / (u32)k_num_layer_parameters,
    };
}

constexpr Span<String const> MenuItems(ParamDescriptor::MenuType type) {
    using namespace param_values;
    switch (type) {
        case ParamDescriptor::MenuType::LegacyEqType: return k_legacy_eq_type_strings;
        case ParamDescriptor::MenuType::EqType: return k_eq_type_strings;
        case ParamDescriptor::MenuType::LoopMode: return k_loop_mode_strings;
        case ParamDescriptor::MenuType::LegacyLfoSyncedRate: return k_legacy_lfo_synced_rate_strings;
        case ParamDescriptor::MenuType::LfoSyncedRate: return k_lfo_synced_rate_strings;
        case ParamDescriptor::MenuType::LfoRestartMode: return k_lfo_restart_mode_strings;
        case ParamDescriptor::MenuType::LegacyLfoDestination: return k_legacy_lfo_destination_strings;
        case ParamDescriptor::MenuType::LfoDestination: return k_lfo_destination_strings;
        case ParamDescriptor::MenuType::LegacyLfoShape: return k_legacy_lfo_shape_strings;
        case ParamDescriptor::MenuType::LegacyLfoShapeV2: return k_legacy_lfo_shape_v2_strings;
        case ParamDescriptor::MenuType::LfoShape: return k_lfo_shape_strings;
        case ParamDescriptor::MenuType::LegacyLayerFilterType: return k_legacy_layer_filter_type_strings;
        case ParamDescriptor::MenuType::LayerFilterType: return k_layer_filter_type_strings;
        case ParamDescriptor::MenuType::LegacyEffectFilterType: return k_legacy_effect_filter_type_strings;
        case ParamDescriptor::MenuType::EffectFilterType: return k_effect_filter_type_strings;
        case ParamDescriptor::MenuType::LegacyDistortionType: return k_legacy_distortion_type_strings;
        case ParamDescriptor::MenuType::DistortionType: return k_distortion_type_strings;
        case ParamDescriptor::MenuType::CompressorType: return k_compressor_type_strings;
        case ParamDescriptor::MenuType::LegacyDelaySyncedTime: return k_legacy_delay_synced_time_strings;
        case ParamDescriptor::MenuType::DelaySyncedTime: return k_delay_synced_time_strings;
        case ParamDescriptor::MenuType::DelayMode: return k_delay_mode_strings;
        case ParamDescriptor::MenuType::VelocityMappingMode: return k_velocity_mapping_mode_strings;
        case ParamDescriptor::MenuType::MonophonicMode: return k_monophonic_mode_strings;
        case ParamDescriptor::MenuType::StereoWidenMode: return k_stereo_widen_mode_strings;
        case ParamDescriptor::MenuType::PlayMode: return k_play_mode_strings;
        case ParamDescriptor::MenuType::ArpMode: return k_arp_type_strings;
        case ParamDescriptor::MenuType::ArpNoteOrder: return k_arp_note_order_strings;
        case ParamDescriptor::MenuType::ArpTriggerMode: return k_arp_trigger_mode_strings;
        case ParamDescriptor::MenuType::LegacyArpSyncedRate: return k_legacy_arp_synced_rate_strings;
        case ParamDescriptor::MenuType::ArpSyncedRate: return k_arp_synced_rate_strings;
        case ParamDescriptor::MenuType::ArpOctavePolyrate: return k_arp_octave_polyrate_strings;
        case ParamDescriptor::MenuType::ArpAutoRate: return k_arp_auto_rate_strings;
        case ParamDescriptor::MenuType::MpeDestination: return k_mpe_destination_strings;
        case ParamDescriptor::MenuType::None:
        case ParamDescriptor::MenuType::Count: break;
    }
    throw "";
    return {};
}

// Whether the menu's options form a monotonic scale (note divisions, for example) rather than a set of
// unrelated modes. Only these are worth offering a drag-to-change affordance on.
constexpr bool MenuIsOrderedScale(ParamDescriptor::MenuType type) {
    switch (type) {
        case ParamDescriptor::MenuType::LegacyLfoSyncedRate:
        case ParamDescriptor::MenuType::LfoSyncedRate:
        case ParamDescriptor::MenuType::LegacyDelaySyncedTime:
        case ParamDescriptor::MenuType::DelaySyncedTime:
        case ParamDescriptor::MenuType::LegacyArpSyncedRate:
        case ParamDescriptor::MenuType::ArpSyncedRate: return true;

        case ParamDescriptor::MenuType::None:
        case ParamDescriptor::MenuType::LoopMode:
        case ParamDescriptor::MenuType::LegacyEqType:
        case ParamDescriptor::MenuType::EqType:
        case ParamDescriptor::MenuType::LfoRestartMode:
        case ParamDescriptor::MenuType::LegacyLfoDestination:
        case ParamDescriptor::MenuType::LfoDestination:
        case ParamDescriptor::MenuType::LegacyLfoShape:
        case ParamDescriptor::MenuType::LegacyLfoShapeV2:
        case ParamDescriptor::MenuType::LfoShape:
        case ParamDescriptor::MenuType::LegacyLayerFilterType:
        case ParamDescriptor::MenuType::LayerFilterType:
        case ParamDescriptor::MenuType::LegacyEffectFilterType:
        case ParamDescriptor::MenuType::EffectFilterType:
        case ParamDescriptor::MenuType::LegacyDistortionType:
        case ParamDescriptor::MenuType::DistortionType:
        case ParamDescriptor::MenuType::CompressorType:
        case ParamDescriptor::MenuType::DelayMode:
        case ParamDescriptor::MenuType::VelocityMappingMode:
        case ParamDescriptor::MenuType::MonophonicMode:
        case ParamDescriptor::MenuType::StereoWidenMode:
        case ParamDescriptor::MenuType::PlayMode:
        case ParamDescriptor::MenuType::ArpMode:
        case ParamDescriptor::MenuType::ArpNoteOrder:
        case ParamDescriptor::MenuType::ArpTriggerMode:
        case ParamDescriptor::MenuType::ArpOctavePolyrate:
        case ParamDescriptor::MenuType::ArpAutoRate:
        case ParamDescriptor::MenuType::MpeDestination:
        case ParamDescriptor::MenuType::Count: break;
    }
    return false;
}

namespace val_config_helpers {

using ValConfig = ParamDescriptor::ConstructorArgs::ValueConfig;

constexpr f32 DbToAmp(f32 db) { return (f32)constexpr_math::Pow(10.0, (f64)db / 20.0); }
constexpr f32 LogWithBase(f32 base, f32 x) {
    return (f32)(constexpr_math::Log((f64)x) / constexpr_math::Log((f64)base));
}

struct PercentOptions {
    f32 default_percent;
    ParamDisplayFormat display_format = ParamDisplayFormat::Percent;
};
constexpr ValConfig Percent(PercentOptions opts) {
    return ValConfig {
        .linear_range = {0, 1},
        .default_linear_value = opts.default_percent / 100,
        .display_format = opts.display_format,
    };
}

struct BidirectionalPercentOptions {
    f32 default_percent;
    ParamDisplayFormat display_format;
    f32 max_percent = 100;
};
constexpr ValConfig BidirectionalPercent(BidirectionalPercentOptions opts) {
    return ValConfig {
        .linear_range = {-opts.max_percent / 100, opts.max_percent / 100},
        .default_linear_value = opts.default_percent / 100,
        .display_format = opts.display_format,
    };
}

struct CustomLinearOptions {
    ParamValueType value_type = ParamValueType::Float;
    ParamDescriptor::Range range;
    f32 default_val;
};
constexpr ValConfig CustomLinear(CustomLinearOptions opts) {
    return ValConfig {
        .linear_range = opts.range,
        .projection = k_nullopt,
        .default_linear_value = opts.default_val,
        .value_type = opts.value_type,
    };
}

struct SemitonesOptions {
    f32 default_val;
    ParamDescriptor::Range range = {0, 128};
};
constexpr ValConfig Semitones(SemitonesOptions opts) {
    return ValConfig {
        .linear_range = opts.range,
        .projection = k_nullopt,
        .default_linear_value = opts.default_val,
        .display_format = ParamDisplayFormat::Semitones,
    };
}

struct IntOptions {
    ParamDescriptor::Range range;
    f32 default_val;
};
constexpr ValConfig Int(IntOptions opts) {
    return CustomLinear({
        .value_type = ParamValueType::Int,
        .range = opts.range,
        .default_val = opts.default_val,
    });
}

struct BoolOptions {
    bool default_state;
};
constexpr ValConfig Bool(BoolOptions opts) {
    return CustomLinear({
        .value_type = ParamValueType::Bool,
        .range = {0, 1},
        .default_val = (f32)opts.default_state,
    });
}

struct MenuOptions {
    ParamDescriptor::MenuType type;
    u32 default_val;
};
constexpr ValConfig Menu(MenuOptions opts) {
    auto const items = MenuItems(opts.type);
    auto const range = ParamDescriptor::Range {0, (f32)items.size - 1};
    return ValConfig {
        .linear_range = range,
        .projection = k_nullopt,
        .default_linear_value = (f32)opts.default_val,
        .value_type = ParamValueType::Menu,
        .menu_type = opts.type,
    };
}

struct VolumeOptions {
    f32 default_db;
    f32 max_db = 12;
    Optional<f32> exponent = k_nullopt;
};
constexpr ValConfig Volume(VolumeOptions opts) {
    auto const max_amp = DbToAmp(opts.max_db);

    // By default, make it so that 0.5 linear value (the middle) maps to -6dB.
    if (!opts.exponent) opts.exponent = LogWithBase(0.5f, DbToAmp(-6) / max_amp);

    ParamDescriptor::Projection const p {{0, max_amp}, *opts.exponent};
    ParamDescriptor::Range const linear_range = {0, 1};
    return ValConfig {
        .linear_range = linear_range,
        .projection = p,
        .default_linear_value = p.LineariseValue(linear_range, DbToAmp(opts.default_db)),
        .display_format = ParamDisplayFormat::VolumeAmp,
    };
}

struct SustainOptions {
    f32 default_db;
};
constexpr ValConfig Sustain(SustainOptions opts) {
    return Volume({
        .default_db = opts.default_db,
        .max_db = 0,
        .exponent = 1.3f,
    });
}

struct GainOptions {
    f32 default_db;
};
constexpr ValConfig Gain(GainOptions opts) {
    ParamDescriptor::Projection const projection {{-30, 30}, 1.6f};
    ParamDescriptor::Range const linear_range = {-1, 1};
    return ValConfig {
        .linear_range = linear_range,
        .projection = projection,
        .default_linear_value = projection.LineariseValue(linear_range, opts.default_db),
        .display_format = ParamDisplayFormat::VolumeDbRange,
    };
}

struct MsOptions {
    ParamDescriptor::Projection projection;
    f32 default_ms;
};
constexpr ValConfig Ms(MsOptions opts) {
    ParamDescriptor::Range const linear_range = {0, 1};
    return ValConfig {
        .linear_range = linear_range,
        .projection = opts.projection,
        .default_linear_value = opts.projection.LineariseValue(linear_range, opts.default_ms),
        .display_format = ParamDisplayFormat::Ms,
    };
}

struct MsHelperOptions {
    f32 default_ms;
};
constexpr ValConfig DelayNewMs(MsHelperOptions opts) {
    return Ms({
        .projection = {{15, 8000}, 2.5f},
        .default_ms = opts.default_ms,
    });
}

constexpr ValConfig DelayOldMs(MsHelperOptions opts) {
    return Ms({
        .projection = {{15, 1000}, 1.25f},
        .default_ms = opts.default_ms,
    });
}

constexpr ValConfig EnvelopeMs(MsHelperOptions opts) {
    return Ms({
        .projection = {{0, 10000}, 3},
        .default_ms = opts.default_ms,
    });
}

struct HzOptions {
    ParamDescriptor::Projection projection;
    f32 default_hz;
};
constexpr ValConfig Hz(HzOptions opts) {
    ParamDescriptor::Range const linear_range {0, 1};
    return ValConfig {
        .linear_range = linear_range,
        .projection = opts.projection,
        .default_linear_value = opts.projection.LineariseValue(linear_range, opts.default_hz),
        .display_format = ParamDisplayFormat::Hz,
    };
}

struct FilterOptions {
    f32 default_hz;
};
constexpr ValConfig Filter(FilterOptions opts) {
    return Hz({
        .projection = {.range = {15, 20000}, .exponent = 0, .type = ParamDescriptor::Projection::Type::Log},
        .default_hz = opts.default_hz,
    });
}
// Old pow-skewed mapping, retained only for backwards-compatibility shadow params that read DAW
// automation written before the switch to log mapping.
constexpr ValConfig LegacyFilter(FilterOptions opts) {
    return Hz({
        .projection = {{15, 20000}, 2.8f},
        .default_hz = opts.default_hz,
    });
}

struct HzSlowOptions {
    f32 default_hz;
    f32 exponent = 1.8f;
    ParamDescriptor::Range range = {0.1f, 20};
};
constexpr ValConfig HzSlow(HzSlowOptions opts) {
    return Hz({
        .projection = {opts.range, opts.exponent},
        .default_hz = opts.default_hz,
    });
}

struct CustomProjectedOptions {
    ParamDisplayFormat display_format;
    f32 default_val;
    ParamDescriptor::Projection projection;
};
constexpr ValConfig CustomProjected(CustomProjectedOptions opts) {
    ParamDescriptor::Range const linear_range {0, 1};
    return ValConfig {
        .linear_range = linear_range,
        .projection = opts.projection,
        .default_linear_value = opts.projection.LineariseValue(linear_range, opts.default_val),
        .display_format = opts.display_format,
    };
}
} // namespace val_config_helpers

using IdMapIntType = u16;
constexpr IdMapIntType k_invalid_param_id = LargestRepresentableValue<IdMapIntType>();

consteval auto CreateParams() {
    // =====================================================================================================
    constexpr u32 k_ids_per_region = 160; // never change

    enum class IdRegion : u8 {
        Master = 0, // never change
        Layer1 = 1, // never change
        Layer2 = 2, // never change
        Layer3 = 3, // never change

        // You can add more regions here
        Count,
    };

    auto const id = [](IdRegion region, u32 index) {
        if (index >= k_ids_per_region) throw "region overflow";
        return ((u32)region * k_ids_per_region) + index;
    };

    // =====================================================================================================
    struct Result {
        Array<ParamDescriptor, k_num_parameters> params;

        // index is an ID, value is a ParamIndex
        Array<IdMapIntType, k_ids_per_region * u32(IdRegion::Count)> id_map;
    };
    Result result {};
    for (auto& i : result.id_map)
        i = k_invalid_param_id;

    // =====================================================================================================
    auto mp = [&result](ParamIndex index) -> ParamDescriptor& { return result.params[ToInt(index)]; };

    using enum ParamIndex;
    using namespace param_values;
    using Args = ParamDescriptor::ConstructorArgs;

    // =====================================================================================================
    mp(MasterVolume) = Args {
        .id = id(IdRegion::Master, 0), // never change
        .id_string = "master.volume"_s,
        .value_config = val_config_helpers::Volume({.default_db = 0}),
        .modules = {ParameterModule::Master},
        .name = "Volume"_s,
        .gui_label = "Vol"_s,
        .tooltip =
            "The Master Volume is the final stage of Floe's signal chain: it sits after the effects rack, so it sets the overall level of everything just before the audio leaves Floe."_s,
    };

    mp(LegacyMasterVelocity) = Args {
        .id = id(IdRegion::Master, 1), // never change
        .id_string = "master.legacy_velocity"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Master},
        .name = "Legacy Velocity To Volume Strength"_s,
        .gui_label = "Velo"_s,
        .tooltip =
            "Legacy parameter. The amount that the MIDI velocity affects the volume of notes; 100% means notes will be silent when the velocity is very soft, and 0% means that notes will play full volume regardless of the velocity"_s,
        .flags = {.legacy = true},
    };
    mp(MasterTimbre) = Args {
        .id = id(IdRegion::Master, 2), // never change
        .id_string = "master.timbre"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 80}),
        .modules = {ParameterModule::Master},
        .name = "Timbre"_s,
        .gui_label = "Timbre"_s,
        .tooltip =
            "Sweep between an instrument's crossfade layers, such as soft-to-hard or dark-to-bright variations of the same sound.\n\nOnly instruments that were built with crossfade layers respond to this knob; they are highlighted while you drag it. If none of the loaded instruments have crossfade layers, the knob is inactive."_s,
    };

    constexpr String k_macro_tooltip =
        "A macro is a single knob that moves one or more other parameters at once. Preset authors usually set these up and give them custom names, so each macro shapes the sound in a way that's tailored to that particular preset.\n\n"
        "If you want to set up your own, go to the Layers or Effects page and open the MACROS tab in the bottom panel. There you can choose what each macro controls and rename it.";

    mp(Macro1) = Args {
        .id = id(IdRegion::Master, 101), // never change
        .id_string = "macro.1"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Macro},
        .name = "1"_s,
        .gui_label = "Macro 1"_s,
        .tooltip = k_macro_tooltip,
    };
    mp(Macro2) = Args {
        .id = id(IdRegion::Master, 102), // never change
        .id_string = "macro.2"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Macro},
        .name = "2"_s,
        .gui_label = "Macro 2"_s,
        .tooltip = k_macro_tooltip,
    };
    mp(Macro3) = Args {
        .id = id(IdRegion::Master, 103), // never change
        .id_string = "macro.3"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Macro},
        .name = "3"_s,
        .gui_label = "Macro 3"_s,
        .tooltip = k_macro_tooltip,
    };
    mp(Macro4) = Args {
        .id = id(IdRegion::Master, 104), // never change
        .id_string = "macro.4"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Macro},
        .name = "4"_s,
        .gui_label = "Macro 4"_s,
        .tooltip = k_macro_tooltip,
    };

    // =====================================================================================================
    mp(LegacyDistortionType) = Args {
        .id = id(IdRegion::Master, 3), // never change
        .id_string = "fx.distortion.legacy_type"_s,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::LegacyDistortionType,
            .default_val = (u32)LegacyDistortionType::TubeLog,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Legacy Type"_s,
        .gui_label = "Type"_s,
        .tooltip = "Legacy type parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(DistortionType) = Args {
        .id = id(IdRegion::Master, 151), // never change
        .id_string = "fx.distortion.type"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DistortionType,
            .default_val = (u32)DistortionType::Tape,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip =
            "The Type sets the character and flavour of the distortion. The Bite types saturate and thicken the sound; the Mangle types tear it apart into something more extreme.\n\nThe Legacy types are the original algorithms from Floe's early days, kept so that older presets still sound as they were made."_s,
    };
    mp(DistortionDrive) = Args {
        .id = id(IdRegion::Master, 4), // never change
        .id_string = "fx.distortion.drive"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Drive"_s,
        .gui_label = "Drive"_s,
        .tooltip =
            "Drive sets how hard the signal is pushed into the Type's shaping curve, and is the main control for how much distortion you hear. For the Mangle types it also moves the effect itself, so the character changes as well as the intensity."_s,
    };
    mp(DistortionMix) = Args {
        .id = id(IdRegion::Master, 115), // never change
        .id_string = "fx.distortion.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip =
            "Blend between the dry input and the distorted signal. Backing it off keeps the clarity of the original sound with the distortion sitting underneath it."_s,
    };
    mp(DistortionOn) = Args {
        .id = id(IdRegion::Master, 5), // never change
        .id_string = "fx.distortion.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "On"_s,
        .gui_label = "Distortion"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(DistortionPunish) = Args {
        .id = id(IdRegion::Master, 149), // never change
        .id_string = "fx.distortion.punish"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Punish"_s,
        .gui_label = "Punish"_s,
        .tooltip =
            "Punish stacks extra distortion stages after the first, each one driven harder than the last, for a denser, grittier, more compressed edge. Drive sets how hard the signal hits the first stage; Punish sets how much is piled on top."_s,
    };
    mp(DistortionTilt) = Args {
        .id = id(IdRegion::Master, 150), // never change
        .id_string = "fx.distortion.tilt"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::BidirectionalPercent({
            .default_percent = 0,
            .display_format = ParamDisplayFormat::Percent,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Tilt EQ"_s,
        .gui_label = "Tilt EQ"_s,
        .tooltip =
            "Tilt the tone going into the distortion. Positive lifts the highs so they saturate more for a brighter result; negative lifts the lows for a warmer, fatter drive."_s,
    };
    mp(DistortionGain) = Args {
        .id = id(IdRegion::Master, 152), // never change
        .id_string = "fx.distortion.gain"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip =
            "Change the level of the distorted signal, applied after all the shaping and before the Mix blend."_s,
    };
    mp(DistortionAutoGain) = Args {
        .id = id(IdRegion::Master, 148), // never change
        .id_string = "fx.distortion.auto_gain"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Bool({.default_state = true}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Auto Gain"_s,
        .gui_label = "Auto Gain"_s,
        .tooltip =
            "Enable Auto Gain to hold this Legacy type's loudness steady as Drive is increased; it's the same compensation that the Bite and Mangle types always use. Presets made before Floe had this switch load with it off, so they sound just as they were made."_s,
    };

    // =====================================================================================================
    mp(BitCrushBits) = Args {
        .id = id(IdRegion::Master, 6), // never change
        .id_string = "fx.bitcrush.bits"_s,
        .value_config = val_config_helpers::Int({.range = {2, 32}, .default_val = 32}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Bits"_s,
        .gui_label = "Bits"_s,
        .tooltip =
            "Reduce the bit depth of the signal, adding a gritty digital noise that's most obvious in quiet passages and tails."_s,
    };
    mp(BitCrushBitRate) = Args {
        .id = id(IdRegion::Master, 7), // never change
        .id_string = "fx.bitcrush.bit_rate"_s,
        .value_config = val_config_helpers::CustomProjected({
            .display_format = ParamDisplayFormat::Hz,
            .default_val = 44100,
            .projection = {{256, 44100}, 3.0f},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Sample Rate"_s,
        .gui_label = "Samp Rate"_s,
        .tooltip =
            "Reduce the sample rate by holding each sample for longer. Lower rates fold high frequencies back down into ringing, metallic aliasing - the sound of early samplers and retro hardware."_s,
    };
    mp(LegacyBitCrushWet) = Args {
        .id = id(IdRegion::Master, 8), // never change
        .id_string = "fx.bitcrush.legacy_wet"_s,
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Legacy Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(LegacyBitCrushDry) = Args {
        .id = id(IdRegion::Master, 9), // never change
        .id_string = "fx.bitcrush.legacy_dry"_s,
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Legacy Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(BitCrushMix) = Args {
        .id = id(IdRegion::Master, 109), // never change
        .id_string = "fx.bitcrush.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the bitcrushed signal."_s,
    };
    mp(BitCrushOutput) = Args {
        .id = id(IdRegion::Master, 110), // never change
        .id_string = "fx.bitcrush.output"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Volume({.default_db = 0, .max_db = 18}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Output"_s,
        .gui_label = "Output"_s,
        .tooltip = "Output level after the mix."_s,
    };
    mp(BitCrushOn) = Args {
        .id = id(IdRegion::Master, 10), // never change
        .id_string = "fx.bitcrush.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "On"_s,
        .gui_label = "Bit Crush"_s,
        .tooltip = "Enable/disable this effect."_s,
    };

    // =====================================================================================================
    mp(LegacyCompressorThreshold) = Args {
        .id = id(IdRegion::Master, 11), // never change
        .id_string = "fx.compressor.legacy_threshold"_s,
        .value_config = val_config_helpers::Volume({.default_db = 0, .max_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Legacy Threshold"_s,
        .gui_label = "Threshold"_s,
        .tooltip = "Legacy threshold parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(CompressorThreshold) = Args {
        .id = id(IdRegion::Master, 118), // never change
        .id_string = "fx.compressor.threshold"_s,
        .added_in_generation = 1,
        .value_config =
            ParamDescriptor::ConstructorArgs::ValueConfig {
                .linear_range = {-60, 0},
                .projection = k_nullopt,
                .default_linear_value = 0,
                .display_format = ParamDisplayFormat::VolumeDbRange,
            },
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Threshold"_s,
        .gui_label = "Threshold"_s,
        .tooltip =
            "The threshold that the audio has to pass above before the compression should start taking place."_s,
    };
    mp(LegacyCompressorRatio) = Args {
        .id = id(IdRegion::Master, 12), // never change
        .id_string = "fx.compressor.legacy_ratio"_s,
        .value_config = val_config_helpers::CustomLinear({
            .range = {1, 20},
            .default_val = 2,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Legacy Ratio"_s,
        .gui_label = "Ratio"_s,
        .tooltip = "Legacy ratio parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(CompressorRatio) = Args {
        .id = id(IdRegion::Master, 119), // never change
        .id_string = "fx.compressor.ratio"_s,
        .added_in_generation = 1,
        .value_config = ({
            ParamDescriptor::Projection const projection {{1.0f, 20.0f}, 2.66f};
            ParamDescriptor::Range const linear_range = {0, 1};
            ParamDescriptor::ConstructorArgs::ValueConfig {
                .linear_range = linear_range,
                .projection = projection,
                .default_linear_value = projection.LineariseValue(linear_range, 2.0f),
                .display_format = ParamDisplayFormat::Ratio,
            };
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Ratio"_s,
        .gui_label = "Ratio"_s,
        .tooltip =
            "The intensity of compression (high ratios mean more compression). Above around 2:1 begins to make the pumping effect much more noticable."_s,
    };
    mp(CompressorGain) = Args {
        .id = id(IdRegion::Master, 13), // never change
        .id_string = "fx.compressor.gain"_s,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = "An additional control for volume after compression."_s,
    };
    mp(CompressorAutoGain) = Args {
        .id = id(IdRegion::Master, 14), // never change
        .id_string = "fx.compressor.auto_gain"_s,
        .value_config = val_config_helpers::Bool({.default_state = true}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Auto Gain"_s,
        .gui_label = "Auto Gain"_s,
        .tooltip =
            "Automatically re-adjust the gain to stay consistent regardless of compression intensity."_s,
    };
    mp(CompressorOn) = Args {
        .id = id(IdRegion::Master, 15), // never change
        .id_string = "fx.compressor.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "On"_s,
        .gui_label = "Compressor",
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(CompressorType) = Args {
        .id = id(IdRegion::Master, 33), // never change
        .id_string = "fx.compressor.type"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::CompressorType,
            .default_val = (u32)CompressorType::Modern,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip =
            "The compressor algorithm to use. The Vintage type has a bit of character, and automatically set the attack and release. The Digital type offers more precision."_s,
    };
    mp(CompressorAttack) = Args {
        .id = id(IdRegion::Master, 34), // never change
        .id_string = "fx.compressor.attack"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({
            .default_percent = 50,
            .display_format = ParamDisplayFormat::CompressorAttackMs,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Attack"_s,
        .gui_label = "Attack"_s,
        .tooltip = "How quickly the compressor responds to a rise in level."_s,
    };
    mp(CompressorRelease) = Args {
        .id = id(IdRegion::Master, 35), // never change
        .id_string = "fx.compressor.release"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({
            .default_percent = 50,
            .display_format = ParamDisplayFormat::CompressorReleaseMs,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Release"_s,
        .gui_label = "Release"_s,
        .tooltip = "How quickly the compressor recovers after the level drops."_s,
    };
    mp(CompressorMix) = Args {
        .id = id(IdRegion::Master, 36), // never change
        .id_string = "fx.compressor.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the compressed signal."_s,
    };

    // =====================================================================================================
    mp(FilterOn) = Args {
        .id = id(IdRegion::Master, 16), // never change
        .id_string = "fx.filter.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "On"_s,
        .gui_label = "Filter"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(LegacyFilterCutoff) = Args {
        .id = id(IdRegion::Master, 17), // never change
        .id_string = "fx.filter.legacy_cutoff"_s,
        .value_config = val_config_helpers::LegacyFilter({.default_hz = 5000}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Legacy Cutoff Frequency"_s,
        .gui_label = "Cutoff"_s,
        .tooltip = "Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(FilterCutoff) = Args {
        .id = id(IdRegion::Master, 106), // never change
        .id_string = "fx.filter.cutoff"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 5000}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Cutoff Frequency"_s,
        .gui_label = "Cutoff"_s,
        .tooltip =
            "Cutoff sets where the filter does its work: the point the pass filters roll off from, the centre of the band for Band-pass and Notch, or the corner for the shelves."_s,
        .flags = {.cutoff_frequency = true},
    };
    mp(LegacyFilterResonance) = Args {
        .id = id(IdRegion::Master, 18), // never change
        .id_string = "fx.filter.legacy_resonance"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 30}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Legacy Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip = "Legacy resonance parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(FilterResonance) = Args {
        .id = id(IdRegion::Master, 29), // never change
        .id_string = "fx.filter.resonance"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip =
            "Resonance emphasises the frequencies right at the cutoff, adding a peak there on the pass filters. For Band-pass and Notch it narrows the band, for Peak it tightens the boost, and for the shelves it steepens the corner."_s,
    };
    mp(LegacyFilterGain) = Args {
        .id = id(IdRegion::Master, 19), // never change
        .id_string = "fx.filter.legacy_gain"_s,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Legacy Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = "Legacy gain parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(FilterGain) = Args {
        .id = id(IdRegion::Master, 30), // never change
        .id_string = "fx.filter.gain"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = "Gain sets how far the Peak and shelf types boost or cut around the cutoff."_s,
    };
    mp(LegacyFilterType) = Args {
        .id = id(IdRegion::Master, 20), // never change
        .id_string = "fx.filter.legacy_type"_s,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::LegacyEffectFilterType,
            .default_val = (u32)LegacyEffectFilterType::LowPass,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Legacy Type"_s,
        .gui_label = "Type"_s,
        .tooltip = "Legacy type parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(FilterType) = Args {
        .id = id(IdRegion::Master, 105), // never change
        .id_string = "fx.filter.type"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::EffectFilterType,
            .default_val = (u32)EffectFilterType::LowPass24,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip =
            "Select the filter type. Peak, Low-shelf and High-shelf bring in the Gain knob, letting you boost as well as cut."_s,
    };
    mp(FilterMix) = Args {
        .id = id(IdRegion::Master, 117), // never change
        .id_string = "fx.filter.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the filtered signal."_s,
    };

    // =====================================================================================================
    mp(StereoWidenWidth) = Args {
        .id = id(IdRegion::Master, 21), // never change
        .id_string = "fx.stereo_widen.width"_s,
        .value_config = val_config_helpers::BidirectionalPercent({
            .default_percent = 15,
            .display_format = ParamDisplayFormat::Percent,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "Width"_s,
        .gui_label = "Width"_s,
        .tooltip =
            "Width narrows or widens the stereo image. Negative values pull the sound towards mono, positive values push it wider, and 0% leaves it as it is.\n\n"
            "It rebalances the mid (what both channels share) against the side (what differs between them), so at -100% only the mid is left, and at +100% only the side, which makes anything dead centre disappear.\n\n"
            "Tip: each layer has its own Stereo control if you want to widen just one of them."_s,
    };
    mp(StereoWidenOn) = Args {
        .id = id(IdRegion::Master, 22), // never change
        .id_string = "fx.stereo_widen.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "On"_s,
        .gui_label = "Stereo Widen On"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(StereoWidenMode) = Args {
        .id = id(IdRegion::Master, 31), // never change
        .id_string = "fx.stereo_widen.mode"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::StereoWidenMode,
            .default_val = (u32)param_values::StereoWidenMode::Balanced,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "Mode"_s,
        .gui_label = "Mode"_s,
        .tooltip =
            "Mode sets how the widening is done. Hover over the options in the menu for a description of each."_s,
    };
    mp(StereoWidenBassMono) = Args {
        .id = id(IdRegion::Master, 32), // never change
        .id_string = "fx.stereo_widen.bass_mono"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Hz({
            .projection = {.range = {20, 1000},
                           .exponent = 0,
                           .type = ParamDescriptor::Projection::Type::Log},
            .default_hz = 120,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "Bass Mono"_s,
        .gui_label = "Bass Mono"_s,
        .tooltip =
            "Bass Mono sets the point below which the sound is summed to the centre. Holding the low end in the middle keeps kicks and basslines solid, and stops them losing power on mono systems.\n\n"
            "The two bands are split with a 24 dB per octave Linkwitz-Riley crossover, so they recombine without a dip around the crossover point."_s,
    };
    mp(StereoWidenMix) = Args {
        .id = id(IdRegion::Master, 116), // never change
        .id_string = "fx.stereo_widen.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the stereo-widened signal."_s,
    };

    // =====================================================================================================
    mp(ChorusRate) = Args {
        .id = id(IdRegion::Master, 23), // never change
        .id_string = "fx.chorus.rate"_s,
        .value_config = val_config_helpers::HzSlow({.default_hz = 5}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Rate"_s,
        .gui_label = "Rate"_s,
        .tooltip =
            "The Rate sets how quickly the pitch drifts up and down. Slow rates give a gentle swelling movement, while faster rates become a shimmering warble."_s,
    };
    mp(LegacyChorusHighpass) = Args {
        .id = id(IdRegion::Master, 24), // never change
        .id_string = "fx.chorus.legacy_highpass"_s,
        .value_config = val_config_helpers::LegacyFilter({.default_hz = 1000}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Legacy High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip = "Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(ChorusHighpass) = Args {
        .id = id(IdRegion::Master, 107), // never change
        .id_string = "fx.chorus.highpass"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 1000}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip =
            "Remove low frequencies from the chorused copies. The dry signal is unaffected. Raise this to keep the bass tight and free from movement."_s,
        .flags = {.cutoff_frequency = true},
    };
    mp(ChorusDepth) = Args {
        .id = id(IdRegion::Master, 25), // never change
        .id_string = "fx.chorus.depth"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 10}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Depth"_s,
        .gui_label = "Depth"_s,
        .tooltip =
            "The Depth sets how far the pitch drifts away from the original. Small amounts thicken the sound, while larger amounts give an obvious, tape-like wobble. At zero, the delayed copies are still there, adding a static colouration."_s,
    };
    mp(LegacyChorusWet) = Args {
        .id = id(IdRegion::Master, 26), // never change
        .id_string = "fx.chorus.legacy_wet"_s,
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Legacy Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(LegacyChorusDry) = Args {
        .id = id(IdRegion::Master, 27), // never change
        .id_string = "fx.chorus.legacy_dry"_s,
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Legacy Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(ChorusMix) = Args {
        .id = id(IdRegion::Master, 111), // never change
        .id_string = "fx.chorus.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the chorused signal."_s,
    };
    mp(ChorusOutput) = Args {
        .id = id(IdRegion::Master, 112), // never change
        .id_string = "fx.chorus.output"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Volume({.default_db = 0, .max_db = 18}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Output"_s,
        .gui_label = "Output"_s,
        .tooltip = "Output level after the mix."_s,
    };
    mp(ChorusOn) = Args {
        .id = id(IdRegion::Master, 28), // never change
        .id_string = "fx.chorus.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "On"_s,
        .gui_label = "Chorus"_s,
        .tooltip = "Enable/disable this effect."_s,
    };

    // =====================================================================================================
    mp(DelayMode) = Args {
        .id = id(IdRegion::Master, 90), // never change
        .id_string = "fx.delay.mode"_s,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelayMode,
            .default_val = (u32)DelayMode::Stereo,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Mode"_s,
        .gui_label = "Mode"_s,
        .tooltip =
            "Mode sets how the repeats move around the stereo image. Hover over the options in the menu for a description of each."_s,
    };

    mp(DelayFilterCutoffSemitones) = Args {
        .id = id(IdRegion::Master, 91), // never change
        .id_string = "fx.delay.filter_cutoff"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 60}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Filter Cutoff"_s,
        .gui_label = "Filter"_s,
        .tooltip =
            "Filter sets the centre of a band-pass that the repeats run through, with Spread setting how wide that band is. The dry signal is left untouched.\n\n"
            "It sits inside the feedback loop, so every repeat is filtered again and the echoes thin out as they fade."_s,
        .flags = {.cutoff_frequency = true},
    };

    mp(DelayFilterSpread) = Args {
        .id = id(IdRegion::Master, 92), // never change
        .id_string = "fx.delay.filter_spread"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Filter Spread"_s,
        .gui_label = "Spread"_s,
        .tooltip =
            "Spread sets how wide the repeats' filter band is. Wide open leaves them close to the original sound; narrowed, they squeeze towards the Filter frequency, losing a little more of the top and bottom ends with every pass."_s,
    };
    mp(DelayTimeLMs) = Args {
        .id = id(IdRegion::Master, 93), // never change
        .id_string = "fx.delay.time_l_ms"_s,
        .value_config = val_config_helpers::Ms({.projection = {{15, 4000}, 2.0f}, .default_ms = 470}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Left (ms)"_s,
        .gui_label = "Time L"_s,
        .tooltip =
            "The gap between the repeats in the left channel, set freely in milliseconds.\n\nTip: very short times stop sounding like separate echoes and become a metallic, resonant tone instead."_s,
    };
    mp(DelayTimeRMs) = Args {
        .id = id(IdRegion::Master, 94), // never change
        .id_string = "fx.delay.time_r_ms"_s,
        .value_config = val_config_helpers::Ms({.projection = {{15, 4000}, 2.0f}, .default_ms = 470}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Legacy Time Right (ms)"_s,
        .gui_label = "Time R"_s,
        .tooltip =
            "The gap between the repeats in the right channel, set freely in milliseconds. It's unused in Mono mode, where both channels follow Time L."_s,
    };
    mp(LegacyDelayTimeSyncedL) = Args {
        .id = id(IdRegion::Master, 95), // never change
        .id_string = "fx.delay.time_synced_l"_s,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::LegacyDelaySyncedTime,
            .default_val = (u32)LegacyDelaySyncedTime::_1_4,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Left (Tempo Synced) (Legacy)"_s,
        .gui_label = "Time L"_s,
        .tooltip = "Legacy left delay time. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(LegacyDelayTimeSyncedR) = Args {
        .id = id(IdRegion::Master, 96), // never change
        .id_string = "fx.delay.time_synced_r"_s,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::LegacyDelaySyncedTime,
            .default_val = (u32)LegacyDelaySyncedTime::_1_8,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Right (Tempo Synced) (Legacy)"_s,
        .gui_label = "Time R"_s,
        .tooltip = "Legacy right delay time. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(DelayTimeSyncedL) = Args {
        .id = id(IdRegion::Master, 146), // never change
        .id_string = "fx.delay.time_synced_l_v2"_s,
        .added_in_generation = 5,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelaySyncedTime,
            .default_val = (u32)DelaySyncedTime::_1_4,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Left (Tempo Synced)"_s,
        .gui_label = "Time L"_s,
        .tooltip =
            "The gap between the repeats in the left channel, as a note length that follows the host's tempo."_s,
    };
    mp(DelayTimeSyncedR) = Args {
        .id = id(IdRegion::Master, 147), // never change
        .id_string = "fx.delay.time_synced_r_v2"_s,
        .added_in_generation = 5,
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelaySyncedTime,
            .default_val = (u32)DelaySyncedTime::_1_8,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Right (Tempo Synced)"_s,
        .gui_label = "Time R"_s,
        .tooltip =
            "The gap between the repeats in the right channel, as a note length that follows the host's tempo. It's unused in Mono mode, where both channels follow Time L."_s,
    };
    mp(DelayTimeSyncSwitch) = Args {
        .id = id(IdRegion::Master, 97), // never change
        .id_string = "fx.delay.time_sync"_s,
        .value_config = val_config_helpers::Bool({.default_state = true}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "On"_s,
        .gui_label = "Tempo Sync"_s,
        .tooltip =
            "Enable Tempo Sync to set the delay times as note lengths that follow the host's tempo, keeping the repeats in time with your track. With it off, the times are dialled in freely in milliseconds."_s,
    };
    mp(DelayMix) = Args {
        .id = id(IdRegion::Master, 98), // never change
        .id_string = "fx.delay.mix"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the delayed signal."_s,
    };
    mp(DelayOn) = Args {
        .id = id(IdRegion::Master, 99), // never change
        .id_string = "fx.delay.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "On"_s,
        .gui_label = "Delay"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(DelayFeedback) = Args {
        .id = id(IdRegion::Master, 100), // never change
        .id_string = "fx.delay.feedback"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Feedback"_s,
        .gui_label = "Feedback"_s,
        .tooltip =
            "Feedback sets how long the echoes trail on for: low amounts give a handful of repeats, while high amounts carry on long after the note has gone. It works by feeding each repeat back in to make the next one."_s,
    };

    // =====================================================================================================
    mp(PhaserFeedback) = Args {
        .id = id(IdRegion::Master, 82), // never change
        .id_string = "fx.phaser.feedback"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 40}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Feedback"_s,
        .gui_label = "Feedback"_s,
        .tooltip =
            "Feedback routes the phased signal back into the filters, sharpening the peaks and deepening the notches. Small amounts keep it as a gentle swoosh, while higher amounts give the sweep a resonant, whistling edge."_s,
        .related_params_group = 1,
    };
    mp(PhaserModFreqHz) = Args {
        .id = id(IdRegion::Master, 83), // never change
        .id_string = "fx.phaser.mod_freq"_s,
        .value_config = val_config_helpers::HzSlow({
            .default_hz = 0.2f,
            .exponent = 2.5f,
            .range = {0.01f, 20},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mod Rate"_s,
        .gui_label = "Rate"_s,
        .tooltip =
            "The Rate sets how quickly the peaks sweep up and down the frequency range. Slow rates give a gentle, drifting movement, while faster rates turn the sweep into a warble."_s,
        .related_params_group = 3,
    };
    mp(PhaserCenterSemitones) = Args {
        .id = id(IdRegion::Master, 84), // never change
        .id_string = "fx.phaser.center"_s,
        .value_config = val_config_helpers::Semitones({
            .default_val = 60,
            .range = {8, 136},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Center Frequency"_s,
        .gui_label = "Freq"_s,
        .tooltip =
            "Freq sets the centre of the sweep: the frequency that the peaks and notches travel around. Keep it low for a deep, throaty movement, or raise it to place the phasing amongst the brighter harmonics."_s,
        .related_params_group = 0,
        .flags = {.cutoff_frequency = true},
    };
    mp(PhaserShape) = Args {
        .id = id(IdRegion::Master, 85), // never change
        .id_string = "fx.phaser.shape"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Shape"_s,
        .gui_label = "Shape"_s,
        .tooltip =
            "Shape sets how many peaks and notches are carved into the sound. Turned down there's a single broad one for a soft, vowel-like sweep; turned up there are more of them stacked up the spectrum for a richer, more obvious phasing."_s,
        .related_params_group = 2,
    };
    mp(PhaserModDepth) = Args {
        .id = id(IdRegion::Master, 86), // never change
        .id_string = "fx.phaser.mod_depth"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 20, .range = {0, 48}}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mod Depth"_s,
        .gui_label = "Depth"_s,
        .tooltip =
            "The Depth sets how far the peaks travel either side of the Freq setting. Small amounts give a narrow, shimmering movement, while larger amounts sweep across the whole spectrum.\n\nTip: with Depth all the way down the peaks hold still, leaving a fixed colouration that you can sweep by hand with Freq."_s,
        .related_params_group = 3,
    };
    mp(PhaserStereoAmount) = Args {
        .id = id(IdRegion::Master, 87), // never change
        .id_string = "fx.phaser.stereo_amount"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 5}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Stereo Amount"_s,
        .gui_label = "Stereo"_s,
        .tooltip =
            "Stereo offsets the left and right sweeps from each other so the movement drifts across the stereo image. At zero both channels sweep together; at maximum they sweep in opposite directions, for a wide, swirling feel."_s,
        .related_params_group = 4,
    };
    mp(PhaserMix) = Args {
        .id = id(IdRegion::Master, 88), // never change
        .id_string = "fx.phaser.mix"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the phased signal."_s,
        .related_params_group = 5,
    };
    mp(PhaserOn) = Args {
        .id = id(IdRegion::Master, 89), // never change
        .id_string = "fx.phaser.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "On"_s,
        .gui_label = "Phaser"_s,
        .tooltip = "Enable/disable this effect."_s,
    };

    // =====================================================================================================
    // Shared with each layer's EQ, which has the same bands and types.
    constexpr String k_eq_type_tooltip =
        "Choose the shape of this band: a peak or shelf for boosting and cutting, a notch for removing a narrow slice, or a pass filter for rolling off one end of the spectrum.\n\n"
        "Only the Peak, Low-shelf and High-shelf types use the Gain knob; with the others it's inactive.";
    constexpr String k_eq_freq_tooltip =
        "Frequency sets where this band does its work: the centre of a peak or notch, the corner of a shelf, or the cutoff of a pass filter.";
    constexpr String k_eq_resonance_tooltip =
        "Resonance sets how tightly the band is focused. For the Peak and Notch types it's the width, going from broad and gentle to tight and focused. The shelves use it to steepen the transition at their corner, and the pass filters use it to add a peak at the cutoff.";
    constexpr String k_eq_gain_tooltip =
        "Gain sets how far this band boosts or cuts at its frequency.\n\n"
        "Only the Peak, Low-shelf and High-shelf types use Gain. The notch and pass filters always cut by their own fixed shape, so with those the knob is inactive.";

    mp(EqOn) = Args {
        .id = id(IdRegion::Master, 120), // never change
        .id_string = "fx.eq.on"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq},
        .name = "On"_s,
        .gui_label = "EQ"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(EqMix) = Args {
        .id = id(IdRegion::Master, 121), // never change
        .id_string = "fx.eq.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the equalised signal."_s,
    };
    mp(EqType1) = Args {
        .id = id(IdRegion::Master, 122), // never change
        .id_string = "fx.eq.band1.type"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu(
            {.type = ParamDescriptor::MenuType::EqType, .default_val = (u32)EqType::LowShelf}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band1},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip = k_eq_type_tooltip,
    };
    mp(EqFreq1) = Args {
        .id = id(IdRegion::Master, 123), // never change
        .id_string = "fx.eq.band1.freq"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band1},
        .name = "Frequency"_s,
        .gui_label = "Freq"_s,
        .tooltip = k_eq_freq_tooltip,
        .flags = {.cutoff_frequency = true},
    };
    mp(EqResonance1) = Args {
        .id = id(IdRegion::Master, 124), // never change
        .id_string = "fx.eq.band1.resonance"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 20}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band1},
        .name = "Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip = k_eq_resonance_tooltip,
    };
    mp(EqGain1) = Args {
        .id = id(IdRegion::Master, 125), // never change
        .id_string = "fx.eq.band1.gain"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band1},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = k_eq_gain_tooltip,
    };
    mp(EqType2) = Args {
        .id = id(IdRegion::Master, 126), // never change
        .id_string = "fx.eq.band2.type"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu(
            {.type = ParamDescriptor::MenuType::EqType, .default_val = (u32)EqType::Peak}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band2},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip = k_eq_type_tooltip,
    };
    mp(EqFreq2) = Args {
        .id = id(IdRegion::Master, 127), // never change
        .id_string = "fx.eq.band2.freq"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 1000}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band2},
        .name = "Frequency"_s,
        .gui_label = "Freq"_s,
        .tooltip = k_eq_freq_tooltip,
        .flags = {.cutoff_frequency = true},
    };
    mp(EqResonance2) = Args {
        .id = id(IdRegion::Master, 128), // never change
        .id_string = "fx.eq.band2.resonance"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 20}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band2},
        .name = "Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip = k_eq_resonance_tooltip,
    };
    mp(EqGain2) = Args {
        .id = id(IdRegion::Master, 129), // never change
        .id_string = "fx.eq.band2.gain"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band2},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = k_eq_gain_tooltip,
    };
    mp(EqType3) = Args {
        .id = id(IdRegion::Master, 130), // never change
        .id_string = "fx.eq.band3.type"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Menu(
            {.type = ParamDescriptor::MenuType::EqType, .default_val = (u32)EqType::HighShelf}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band3},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip = k_eq_type_tooltip,
    };
    mp(EqFreq3) = Args {
        .id = id(IdRegion::Master, 131), // never change
        .id_string = "fx.eq.band3.freq"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 8000}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band3},
        .name = "Frequency"_s,
        .gui_label = "Freq"_s,
        .tooltip = k_eq_freq_tooltip,
        .flags = {.cutoff_frequency = true},
    };
    mp(EqResonance3) = Args {
        .id = id(IdRegion::Master, 132), // never change
        .id_string = "fx.eq.band3.resonance"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 20}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band3},
        .name = "Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip = k_eq_resonance_tooltip,
    };
    mp(EqGain3) = Args {
        .id = id(IdRegion::Master, 133), // never change
        .id_string = "fx.eq.band3.gain"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Eq, ParameterModule::Band3},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = k_eq_gain_tooltip,
    };

    // =====================================================================================================
    mp(LegacyConvolutionReverbHighpass) = Args {
        .id = id(IdRegion::Master, 65), // never change
        .id_string = "fx.convolution_reverb.legacy_highpass"_s,
        .value_config = val_config_helpers::LegacyFilter({.default_hz = 30}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Legacy High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip = "Legacy high-pass parameter. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(ConvolutionReverbHighpass) = Args {
        .id = id(IdRegion::Master, 108), // never change
        .id_string = "fx.convolution_reverb.highpass"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Filter({.default_hz = 30}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip =
            "Roll off the low end of the reverb, keeping its rumble and boom clear of the dry sound. It applies to the reverb signal alone, before the Mix blend."_s,
        .flags = {.cutoff_frequency = true},
    };
    mp(LegacyConvolutionReverbWet) = Args {
        .id = id(IdRegion::Master, 66), // never change
        .id_string = "fx.convolution_reverb.legacy_wet"_s,
        .value_config = val_config_helpers::Volume({.default_db = -30}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Legacy Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Legacy processed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(LegacyConvolutionReverbDry) = Args {
        .id = id(IdRegion::Master, 67), // never change
        .id_string = "fx.convolution_reverb.legacy_dry"_s,
        .value_config = val_config_helpers::Volume({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Legacy Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Legacy unprocessed-signal volume. Kept for backwards-compatibility with DAW automation"_s,
        .flags = {.legacy = true},
    };
    mp(ConvolutionReverbMix) = Args {
        .id = id(IdRegion::Master, 113), // never change
        .id_string = "fx.convolution_reverb.mix"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Percent({.default_percent = 25}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the reverb signal."_s,
    };
    mp(ConvolutionReverbOutput) = Args {
        .id = id(IdRegion::Master, 114), // never change
        .id_string = "fx.convolution_reverb.output"_s,
        .added_in_generation = 1,
        .value_config = val_config_helpers::Volume({.default_db = 0, .max_db = 18}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Output"_s,
        .gui_label = "Output"_s,
        .tooltip = "The effect's output level, applied after the Mix blend."_s,
    };
    mp(ConvolutionReverbOn) = Args {
        .id = id(IdRegion::Master, 68), // never change
        .id_string = "fx.convolution_reverb.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "On"_s,
        .gui_label = "Convol Reverb"_s,
        .tooltip = "Enable/disable this effect."_s,
    };

    // =====================================================================================================
    mp(ReverbDecayTimeMs) = Args {
        .id = id(IdRegion::Master, 69), // never change
        .id_string = "fx.reverb.decay_time"_s,
        .value_config = val_config_helpers::Ms({
            .projection = {{10, 60000}, 5},
            .default_ms = 1000,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Decay Time"_s,
        .gui_label = "Decay"_s,
        .tooltip =
            "The Decay Time sets how long the tail takes to fade away once a sound stops. It holds steady whatever the Size, so you can choose the length of the reverb and the character of the space separately."_s,
        .related_params_group = 0,
    };

    mp(ReverbPreLowPassCutoff) = Args {
        .id = id(IdRegion::Master, 70), // never change
        .id_string = "fx.reverb.pre_lowpass_cutoff"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 128}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Pre Low-pass Cutoff"_s,
        .gui_label = "Pre LP"_s,
        .tooltip =
            "The Pre LP is a low-pass filter cutoff for rolling the highs off the signal on its way into the reverb, for a darker, more distant space. The dry signal keeps its full range."_s,
        .related_params_group = 2,
        .flags = {.cutoff_frequency = true},
    };

    mp(ReverbPreHighPassCutoff) = Args {
        .id = id(IdRegion::Master, 71), // never change
        .id_string = "fx.reverb.pre_highpass_cutoff"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Pre High-pass Cutoff"_s,
        .gui_label = "Pre HP"_s,
        .tooltip =
            "The Pre HP is a high-pass filter cutoff for rolling the lows off the signal on its way into the reverb, keeping the tail clear so the bass stays tight and centred. The dry signal keeps its full range."_s,
        .related_params_group = 2,
        .flags = {.cutoff_frequency = true},
    };

    mp(ReverbLowShelfCutoff) = Args {
        .id = id(IdRegion::Master, 72), // never change
        .id_string = "fx.reverb.low_shelf_cutoff"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 40}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Low-shelf Cutoff"_s,
        .gui_label = "Lo-Shelf"_s,
        .tooltip =
            "The Lo-Shelf sets the frequency below which the reverb tail is cut by the Lo-Gain amount. Turn Lo-Gain down to hear it work."_s,
        .related_params_group = 3,
        .flags = {.cutoff_frequency = true},
    };

    auto const shelf_gain_value_config = ParamDescriptor::ConstructorArgs::ValueConfig {
        .linear_range = {0, 1},
        .projection = ParamDescriptor::Projection {.range = {-24, 0}, .exponent = 0.5f},
        .default_linear_value = 1,
        .display_format = ParamDisplayFormat::VolumeDbRange,
    };

    mp(ReverbLowShelfGain) = Args {
        .id = id(IdRegion::Master, 73), // never change
        .id_string = "fx.reverb.low_shelf_gain"_s,
        .value_config = shelf_gain_value_config,
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Low Gain"_s,
        .gui_label = "Lo-Gain"_s,
        .tooltip =
            "The Lo-Gain sets how much the reverb tail is cut below the Lo-Shelf frequency. The cut builds up as the tail recirculates, so the low end thins out as the reverb fades - handy for stopping long tails clouding the bass.",
        .related_params_group = 3,
    };

    mp(ReverbHighShelfCutoff) = Args {
        .id = id(IdRegion::Master, 74), // never change
        .id_string = "fx.reverb.high_shelf_cutoff"_s,
        .value_config = val_config_helpers::Semitones({.default_val = 128}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "High-shelf Cutoff"_s,
        .gui_label = "Hi-Shelf"_s,
        .tooltip =
            "The Hi-Shelf sets the frequency above which the reverb tail is cut by the Hi-Gain amount. Turn Hi-Gain down to hear it work."_s,
        .related_params_group = 4,
        .flags = {.cutoff_frequency = true},
    };

    mp(ReverbHighShelfGain) = Args {
        .id = id(IdRegion::Master, 75), // never change
        .id_string = "fx.reverb.high_shelf_gain"_s,
        .value_config = shelf_gain_value_config,
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "High Gain"_s,
        .gui_label = "Hi-Gain"_s,
        .tooltip =
            "The Hi-Gain sets how much the reverb tail is cut above the Hi-Shelf frequency. The cut builds up as the tail recirculates, so the highs die away first, just as soft furnishings absorb them in a real room.",
        .related_params_group = 4,
    };

    mp(ReverbChorusAmount) = Args {
        .id = id(IdRegion::Master, 76), // never change
        .id_string = "fx.reverb.chorus_amount"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Chorus Amount"_s,
        .gui_label = "Depth"_s,
        .tooltip =
            "The Depth sets how far the reverb's reflections drift, smearing the resonances that build up in the tail. A little stops long tails ringing; more gives an obvious shimmer and movement."_s,
        .related_params_group = 1,
    };

    mp(ReverbChorusFrequency) = Args {
        .id = id(IdRegion::Master, 77), // never change
        .id_string = "fx.reverb.chorus_freq"_s,
        .value_config = val_config_helpers::Hz({
            .projection = {.range = {0.003f, 2}, .exponent = 4.5f},
            .default_hz = 0.01f,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Chorus Frequency"_s,
        .gui_label = "Mod Rate"_s,
        .tooltip =
            "The Mod Rate sets how quickly the drift set by Depth moves through the tail. Very slow rates give a gentle, breathing wash, while faster rates add a distinct warble."_s,
        .related_params_group = 1,
    };

    mp(ReverbSize) = Args {
        .id = id(IdRegion::Master, 78), // never change
        .id_string = "fx.reverb.size"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Size"_s,
        .gui_label = "Size"_s,
        .tooltip =
            "The Size scales the spacing of the simulated reflections, setting how large the space feels. Small values pack them together for a boxy, coloured character; large values spread them out for something open and airy."_s,
        .related_params_group = 0,
    };

    mp(ReverbDelay) = Args {
        .id = id(IdRegion::Master, 79), // never change
        .id_string = "fx.reverb.delay"_s,
        .value_config = val_config_helpers::Ms({
            .projection = {{0, 1000}, 1.5f},
            .default_ms = 0,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Delay"_s,
        .gui_label = "Predelay"_s,
        .tooltip =
            "The Predelay holds the reverb back for a moment so the dry sound is heard on its own first. Short amounts keep the sound upfront and defined, while longer amounts push the walls further away."_s,
        .related_params_group = 0,
    };

    mp(ReverbMix) = Args {
        .id = id(IdRegion::Master, 80), // never change
        .id_string = "fx.reverb.mix"_s,
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the reverb signal."_s,
        .related_params_group = 8,
    };

    mp(ReverbOn) = Args {
        .id = id(IdRegion::Master, 81), // never change
        .id_string = "fx.reverb.on"_s,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "On"_s,
        .gui_label = "Reverb"_s,
        .tooltip = "Enable/disable this effect."_s,
    };

    mp(LimiterOn) = Args {
        .id = id(IdRegion::Master, 140), // never change
        .id_string = "fx.limiter.on"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Limiter},
        .name = "On"_s,
        .gui_label = "Limiter"_s,
        .tooltip = "Enable/disable this effect."_s,
    };
    mp(LimiterMix) = Args {
        .id = id(IdRegion::Master, 141), // never change
        .id_string = "fx.limiter.mix"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Percent({.default_percent = 100}),
        .modules = {ParameterModule::Effect, ParameterModule::Limiter},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Blend between the dry input and the limited signal."_s,
    };
    mp(LimiterGain) = Args {
        .id = id(IdRegion::Master, 142), // never change
        .id_string = "fx.limiter.gain"_s,
        .added_in_generation = 6,
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Limiter},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip =
            "Boost the level going into the limiter. The harder you push, the more it's limited, making the sound louder and denser."_s,
    };
    mp(LimiterCeiling) = Args {
        .id = id(IdRegion::Master, 143), // never change
        .id_string = "fx.limiter.ceiling"_s,
        .added_in_generation = 6,
        .value_config =
            ParamDescriptor::ConstructorArgs::ValueConfig {
                .linear_range = {-12, 0},
                .projection = k_nullopt,
                .default_linear_value = -1.0f,
                .display_format = ParamDisplayFormat::VolumeDbRange,
            },
        .modules = {ParameterModule::Effect, ParameterModule::Limiter},
        .name = "Ceiling"_s,
        .gui_label = "Ceiling"_s,
        .tooltip =
            "The level the output is never allowed to exceed. Leaving a small margin below 0 dB gives headroom for whatever comes after Floe."_s,
    };

    // =====================================================================================================
    for (auto const layer_index : Range(k_num_layers)) {
        using enum LayerParamIndex;

        auto lp = [&result, layer_index](LayerParamIndex index) -> ParamDescriptor& {
            auto const global_index = ParamIndexFromLayerParamIndex(layer_index, index);
            return result.params[ToInt(global_index)];
        };

        IdRegion region {};
        ParameterModule layer_module {};
        switch (layer_index) {
            case 0:
                region = IdRegion::Layer1;
                layer_module = ParameterModule::Layer1;
                break;
            case 1:
                region = IdRegion::Layer2;
                layer_module = ParameterModule::Layer2;
                break;
            case 2:
                region = IdRegion::Layer3;
                layer_module = ParameterModule::Layer3;
                break;
            default: throw "create a new region & module for this layer";
        }

        // Picks one of three id_string literals based on the current layer. Use the LAYER_ID(suffix) macro
        // below to expand a single suffix into three "layerN.<suffix>" literals via preprocessor string
        // concatenation - this keeps id_strings as views into static string-literal storage.
        auto layer_id = [layer_index](String l1, String l2, String l3) -> String {
            switch (layer_index) {
                case 0: return l1;
                case 1: return l2;
                case 2: return l3;
            }
            return {};
        };
#define LAYER_ID(suffix) layer_id("layer1." suffix ""_s, "layer2." suffix ""_s, "layer3." suffix ""_s)

        // =================================================================================================
        lp(Volume) = Args {
            .id = id(region, 0), // never change
            .id_string = LAYER_ID("volume"),
            .value_config = val_config_helpers::Volume({.default_db = -6}),
            .modules = {layer_module},
            .name = "Volume"_s,
            .gui_label = "Volume"_s,
            .tooltip =
                "The Layer Volume sets the level of this layer.\n\n"
                "The marks on the slider show the level of each sounding voice: its velocity, key range fade and any MPE volume expression.\n\n"
                "Tip: to change how velocity relates to volume, edit the velocity to volume curve on the CONFIG tab."_s,
        };
        lp(Mute) = Args {
            .id = id(region, 1), // never change
            .id_string = LAYER_ID("mute"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module},
            .name = "Mute"_s,
            .gui_label = "Mute"_s,
            .tooltip =
                "Mute silences this layer. Handy for hearing how the other layers sit together while you're designing a sound.\n\n"
                "A muted layer keeps running in the background exactly as if you could hear it, so when you unmute, the sound carries on seamlessly from wherever it has naturally got to. If you're trying to save CPU, unload the Instrument instead.\n\n"
                "If a layer is still audible after muting it, check whether it's also soloed: Solo takes priority over Mute."_s,
        };
        lp(Solo) = Args {
            .id = id(region, 2), // never change
            .id_string = LAYER_ID("solo"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module},
            .name = "Solo"_s,
            .gui_label = "Solo"_s,
            .tooltip =
                "Solo lets you hear this layer on its own. Any layer that isn't soloed goes quiet. Handy for focusing on one part of a sound while you're designing it.\n\n"
                "Silenced layers keep running in the background exactly as if you could hear them, so when you unsolo, they carry on seamlessly from wherever they have naturally got to. If you're trying to save CPU, unload their Instruments instead.\n\n"
                "If a layer has gone quiet unexpectedly, check whether another layer is soloed. And if a muted layer is still audible, that's because Solo takes priority over Mute."_s,
        };
        lp(Pan) = Args {
            .id = id(region, 3), // never change
            .id_string = LAYER_ID("pan"),
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Pan,
            }),
            .modules = {layer_module},
            .name = "Pan"_s,
            .gui_label = "Pan"_s,
            .tooltip =
                "Pan places this layer in the stereo field, anywhere from fully left to fully right.\n\n"
                "It uses a constant-power pan law (-3 dB at centre), so the layer stays at the same perceived loudness wherever you put it.\n\n"
                "Pan is applied after the Stereo control, so you can narrow a wide sound first and then place it as a single point."_s,
        };
        lp(StereoWidth) = Args {
            .id = id(region, 95), // never change
            .id_string = LAYER_ID("stereo_width"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Percent,
            }),
            .modules = {layer_module},
            .name = "Stereo Width"_s,
            .gui_label = "Stereo"_s,
            .tooltip =
                "Stereo narrows or widens this layer's stereo image. Negative values pull it toward mono, positive values push it wider, and 0% leaves the sound as recorded.\n\n"
                "It works by splitting the sound into mid (what both channels share) and side (what differs between them), then rebalancing the two with a constant-power crossfade. At -100% only the mid remains; at +100% only the side, so anything dead centre disappears. Mono sounds have no side, so pushing to +100% silences them."_s,
        };
        lp(TuneCents) = Args {
            .id = id(region, 4), // never change
            .id_string = LAYER_ID("tune_cents"),
            .value_config =
                {
                    .linear_range {-1, 1},
                    .projection = ParamDescriptor::Projection {{-1200, 1200}, 1.8f},
                    .default_linear_value = 0,
                    .display_format = ParamDisplayFormat::Cents,
                },
            .modules = {layer_module},
            .name = "Detune Cents"_s,
            .gui_label = "Detune"_s,
            .tooltip =
                "Detune fine-tunes this layer's pitch in cents (100 cents is one semitone). This works by speeding up or slowing down the audio."_s,
        };
        lp(LegacyTuneSemitone) = Args {
            .id = id(region, 5), // never change
            .id_string = LAYER_ID("tune_semitones"),
            .value_config = val_config_helpers::Int({.range = {-36, 36}, .default_val = 0}),
            .modules = {layer_module},
            .name = "Pitch Semitones (Legacy)"_s,
            .gui_label = "Pitch"_s,
            .tooltip =
                "Legacy layer pitch in semitones. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(TuneSemitone) = Args {
            .id = id(region, 102), // never change
            .id_string = LAYER_ID("tune_semitones_v2"),
            .added_in_generation = 6,
            .value_config = val_config_helpers::Int({.range = {-88, 88}, .default_val = 0}),
            .modules = {layer_module},
            .name = "Pitch Semitones"_s,
            .gui_label = "Pitch"_s,
            .tooltip =
                "Pitch moves this layer up or down in semitones. This works by speeding up or slowing down the audio.\n\n"
                "Tip: for a multisampled Instrument, you might get better results from Transpose on the CONFIG tab. It changes which samples are played rather than processing them, which can sound more natural."_s,
        };

        // =================================================================================================
        lp(LoopMode) = Args {
            .id = id(region, 49), // never change
            .id_string = LAYER_ID("loop.mode"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LoopMode,
                .default_val = (u32)LoopMode::InstrumentDefault,
            }),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "Loop Mode"_s,
            .gui_label = "Loop"_s,
            .tooltip =
                "Select the Loop Mode for this layer. Floe can loop a portion of the sound for as long as you hold a note, so even a short sample can sustain indefinitely.\n\n"
                "Some Instruments come with loop points built in, while others let you set your own on the waveform. You can also turn looping off. There's 2 modes: 'standard' wrap-around loops jump from the loop end back to the start; 'ping-pong' loops bounce back and forth, alternating playback direction.\n\n"
                "The options that are available depend on the Instrument: library authors can provide built-in loops, allow or disallow custom loops, or require that certain sounds always loop."_s,
        };
        lp(LoopStart) = Args {
            .id = id(region, 7), // never change
            .id_string = LAYER_ID("loop.start"),
            .value_config = val_config_helpers::Percent(
                {.default_percent = 0, .display_format = ParamDisplayFormat::Percent2dp}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "Start"_s,
            .gui_label = "Start"_s,
            .tooltip =
                "Set the loop start point, where the loop begins within the sample. While a note is held, playback loops between here and the loop end."_s,
        };
        lp(LoopEnd) = Args {
            .id = id(region, 8), // never change
            .id_string = LAYER_ID("loop.end"),
            .value_config = val_config_helpers::Percent(
                {.default_percent = 100, .display_format = ParamDisplayFormat::Percent2dp}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "End"_s,
            .gui_label = "End"_s,
            .tooltip =
                "Set the loop end point, where the loop finishes within the sample. While a note is held, playback loops between the loop start and here."_s,
        };
        lp(LoopCrossfade) = Args {
            .id = id(region, 9), // never change
            .id_string = LAYER_ID("loop.crossfade"),
            .value_config = val_config_helpers::Percent(
                {.default_percent = 1, .display_format = ParamDisplayFormat::Percent2dp}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "Crossfade Size"_s,
            .gui_label = "XFade"_s,
            .tooltip =
                "Set the size of the loop crossfade, which blends audio across the loop point so the loop doesn't click. A small amount usually helps, especially for sustained or tonal sounds.\n\n"
                "Floe keeps the crossfade within the loop and the sample, so it shrinks automatically if you make the loop too small for it."_s,
        };
        lp(SampleOffset) = Args {
            .id = id(region, 11), // never change
            .id_string = LAYER_ID("sample_offset"),
            .value_config = val_config_helpers::Percent(
                {.default_percent = 0, .display_format = ParamDisplayFormat::Percent2dp}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "Sample Start Offset"_s,
            .gui_label = "Start"_s,
            .tooltip = "Change the starting point of the sample"_s,
        };
        lp(Reverse) = Args {
            .id = id(region, 12), // never change
            .id_string = LAYER_ID("reverse"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Loop},
            .name = "Reverse On"_s,
            .gui_label = "Reverse"_s,
            .tooltip =
                "Play the Instrument's samples backwards, from the end to the start. It works in every play mode and whether looping is on or off. The waveform display flips to match, so playback always runs from left to right (unless in the reverse portion of a ping-pong loop)."};

        // =================================================================================================
        lp(VolEnvOn) = Args {
            .id = id(region, 13), // never change
            .id_string = LAYER_ID("vol_env.on"),
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::VolEnv},
            .name = "On"_s,
            .gui_label = "Volume Envelope"_s,
            .tooltip =
                "Switch the volume envelope on or off.\n\n"
                "It's typically best left on. When it's off, every note plays its sample straight through to the end, however briefly you press the key. Handy for one-shot sounds that should always be heard in full. Looping is disabled while the envelope is off, since nothing would ever bring a looping note to an end.\n\n"
                "Careful: with the envelope off, notes can't be cut short, so voices pile up if you play quickly, which costs CPU."_s,
        };
        lp(VolumeAttack) = Args {
            .id = id(region, 14), // never change
            .id_string = LAYER_ID("vol_env.attack"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 0}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::VolEnv},
            .name = "Attack"_s,
            .gui_label = "Attack"_s,
            .tooltip =
                "Attack sets how long each note takes to fade in. A few milliseconds is enough to avoid clicks if you've moved the sample start point."_s,
        };
        lp(VolumeDecay) = Args {
            .id = id(region, 15), // never change
            .id_string = LAYER_ID("vol_env.decay"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 1000}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::VolEnv},
            .name = "Decay"_s,
            .gui_label = "Decay"_s,
            .tooltip = "Decay sets how long the note takes to fall to the Sustain level after the attack."_s,
        };
        lp(VolumeSustain) = Args {
            .id = id(region, 16), // never change
            .id_string = LAYER_ID("vol_env.sustain"),
            .value_config = val_config_helpers::Sustain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::VolEnv},
            .name = "Sustain"_s,
            .gui_label = "Sustain"_s,
            .tooltip =
                "Sustain sets the level the note holds at while the key is down. At 0 dB, Decay has nothing to do."_s,
        };
        lp(VolumeRelease) = Args {
            .id = id(region, 17), // never change
            .id_string = LAYER_ID("vol_env.release"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 800}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::VolEnv},
            .name = "Release"_s,
            .gui_label = "Release"_s,
            .tooltip = "Release sets how long the note takes to fade out after you lift the key."_s,
        };

        // =================================================================================================
        lp(FilterOn) = Args {
            .id = id(region, 18), // never change
            .id_string = LAYER_ID("filter.on"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "On"_s,
            .gui_label = "Filter"_s,
            .tooltip =
                "Switch the filter on or off.\n\n"
                "It's a clean, digital-sounding state-variable filter (12 dB per octave). Every voice gets its own copy, so the filter envelope and the LFO (on the LFO tab) can sweep each note independently.\n\n"
                "Tip: for broader tone shaping, the EQ tab has a three-band equaliser."_s,
        };
        lp(LegacyFilterCutoff) = Args {
            .id = id(region, 19), // never change
            .id_string = LAYER_ID("filter.legacy_cutoff"),
            .value_config = val_config_helpers::LegacyFilter({.default_hz = 6000}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Legacy Cutoff Frequency"_s,
            .gui_label = "Cut"_s,
            .tooltip = "Legacy cutoff parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(FilterCutoff) = Args {
            .id = id(region, 90), // never change
            .id_string = LAYER_ID("filter.cutoff"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Filter({.default_hz = 6000}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Cutoff Frequency"_s,
            .gui_label = "Cut"_s,
            .tooltip =
                "Cutoff Frequency sets where the filter takes effect. The filter envelope and the LFO sweep the cutoff around this value, so it's the centre of any modulation rather than the starting point."_s,
            .flags = {.cutoff_frequency = true},
        };
        lp(LegacyFilterResonance) = Args {
            .id = id(region, 20), // never change
            .id_string = LAYER_ID("filter.legacy_resonance"),
            .value_config = val_config_helpers::Percent({.default_percent = 25}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Legacy Resonance"_s,
            .gui_label = "Res"_s,
            .tooltip = "Legacy resonance parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(FilterResonance) = Args {
            .id = id(region, 69), // never change
            .id_string = LAYER_ID("filter.resonance"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Resonance"_s,
            .gui_label = "Res"_s,
            .tooltip =
                "Resonance emphasises the frequencies at the cutoff. At 0% the slope is smooth and rounded; at 100% it peaks by around 20 dB, without ever self-oscillating.\n\n"
                "For the band-pass, notch and band-shelving types it also narrows the band, and for Peak it sets the size of the boost."_s,
        };
        lp(LegacyFilterType) = Args {
            .id = id(region, 21), // never change
            .id_string = LAYER_ID("filter.legacy_type"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyLayerFilterType,
                .default_val = (u32)LegacyLayerFilterType::Lowpass,
            }),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Legacy Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Legacy filter type parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(FilterType) = Args {
            .id = id(region, 94), // never change
            .id_string = LAYER_ID("filter.type"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LayerFilterType,
                .default_val = (u32)LayerFilterType::Lowpass,
            }),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip =
                "Select the filter type. Hover over the options in the menu for a description of each."_s,
        };
        lp(FilterEnvAmount) = Args {
            .id = id(region, 22), // never change
            .id_string = LAYER_ID("filter.env_amount"),
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Percent,
            }),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Envelope Amount"_s,
            .gui_label = "Env"_s,
            .tooltip =
                "Envelope Amount sets how strongly the filter envelope (shown to the right) moves the cutoff on every note. At 0% the envelope does nothing. Turn it up and the cutoff follows the envelope's shape, rising and falling with it. Turn it down and the cutoff moves the opposite way, falling as the envelope rises.\n\n"
                "The envelope is centred on the Cutoff setting: the top half of the display pushes the cutoff above it, the bottom half pulls it below. A negative amount flips this.\n\n"
                "For example, with a low-pass filter, a long attack opens the sound up gradually at the start of each note."_s,
        };
        lp(FilterAttack) = Args {
            .id = id(region, 23), // never change
            .id_string = LAYER_ID("filter.attack"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 0}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Attack"_s,
            .gui_label = "Attack"_s,
            .tooltip = "Attack sets how long the filter envelope takes to reach its peak."_s,
        };
        lp(FilterDecay) = Args {
            .id = id(region, 24), // never change
            .id_string = LAYER_ID("filter.decay"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 1000}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Decay"_s,
            .gui_label = "Decay"_s,
            .tooltip =
                "Decay sets how long the filter envelope takes to fall to the Sustain level after the attack."_s,
        };
        lp(FilterSustain) = Args {
            .id = id(region, 25), // never change
            .id_string = LAYER_ID("filter.sustain"),
            .value_config = val_config_helpers::Percent({.default_percent = 100}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Sustain"_s,
            .gui_label = "Sustain"_s,
            .tooltip = "Sustain sets the level the filter envelope holds at while the key is down."_s,
        };
        lp(FilterRelease) = Args {
            .id = id(region, 26), // never change
            .id_string = LAYER_ID("filter.release"),
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 800}),
            .modules = {layer_module, ParameterModule::Main, ParameterModule::Filter},
            .name = "Release"_s,
            .gui_label = "Release"_s,
            .tooltip =
                "Release sets how long the filter envelope takes to fall back after you lift the key."_s,
        };

        // =================================================================================================
        lp(LfoOn) = Args {
            .id = id(region, 27), // never change
            .id_string = LAYER_ID("lfo.on"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "On"_s,
            .gui_label = "LFO"_s,
            .tooltip =
                "Switch this layer's LFO on or off.\n\n"
                "Enable Floe's LFO (low frequency oscillator) for adding movement to a one of this layer's controls (such as the volume or filter cutoff)."_s,
        };
        lp(LegacyLfoShape) = Args {
            .id = id(region, 28), // never change
            .id_string = LAYER_ID("lfo.legacy_shape"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyLfoShape,
                .default_val = (u32)LegacyLfoShapeV1::Sine,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Legacy Shape"_s,
            .gui_label = "Shape"_s,
            .tooltip = "Legacy LFO shape parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(LfoRestart) = Args {
            .id = id(region, 29), // never change
            .id_string = LAYER_ID("lfo.restart"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoRestartMode,
                .default_val = (u32)LfoRestartMode::Retrigger,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Mode"_s,
            .gui_label = "Mode"_s,
            .tooltip =
                "Choose where the LFO starts when you play a new note.\n\n"
                "With Retrigger, every note starts the LFO from the beginning of its cycle. With Free, a new note joins in wherever this layer's sounding notes have reached, so every note modulates precisely together."_s,
        };
        lp(LfoAmount) = Args {
            .id = id(region, 30), // never change
            .id_string = LAYER_ID("lfo.amount"),
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 50,
                .display_format = ParamDisplayFormat::Percent,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Amount"_s,
            .gui_label = "Amount"_s,
            .tooltip =
                "Amount sets how far the LFO moves its target. 0% is no movement at all and 100% is the full range for that target: silence to full level for Volume, a semitone either way for Pitch, and so on.\n\n"
                "Negative values flip the shape upside down, so a falling sawtooth becomes a rising one. The LFO display shows the shape at the current Amount."_s,
        };
        lp(LegacyLfoDestination) = Args {
            .id = id(region, 31), // never change
            .id_string = LAYER_ID("lfo.legacy_destination"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyLfoDestination,
                .default_val = (u32)LegacyLfoDestination::Volume,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Target (Legacy)"_s,
            .gui_label = "Target"_s,
            .tooltip = "Legacy LFO target parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(LegacyLfoRateTempoSynced) = Args {
            .id = id(region, 32), // never change
            .id_string = LAYER_ID("lfo.rate_synced"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyLfoSyncedRate,
                .default_val = (u32)LegacyLfoSyncedRate::_1_4,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Time (Tempo Synced) (Legacy)"_s,
            .gui_label = "Time"_s,
            .tooltip = "Legacy LFO rate. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(LfoRateTempoSynced) = Args {
            .id = id(region, 100), // never change
            .id_string = LAYER_ID("lfo.rate_synced_v2"),
            .added_in_generation = 3,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoSyncedRate,
                .default_val = (u32)LfoSyncedRate::_1_4,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Time (Tempo Synced)"_s,
            .gui_label = "Time"_s,
            .tooltip =
                "Time sets how long one LFO cycle lasts, as a note length at your DAW's tempo. 1/4 is a quarter note; a D on the end is dotted (one and a half times as long) and a T is triplet (two thirds as long).\n\n"
                "The LFO follows tempo changes, but it isn't locked to the bar: each cycle starts from the note, as set by Mode."_s,
        };
        lp(LfoRateHz) = Args {
            .id = id(region, 33), // never change
            .id_string = LAYER_ID("lfo.rate_hz"),
            .value_config = val_config_helpers::HzSlow({.default_hz = 5}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Time (Hz)"_s,
            .gui_label = "Time"_s,
            .tooltip =
                "Time sets how fast the LFO cycles, in Hz (cycles per second).\n\n"
                "Tip: for movement that stays in time with your track, switch Sync on and choose a note length instead."_s,
        };
        lp(LfoSyncSwitch) = Args {
            .id = id(region, 34), // never change
            .id_string = LAYER_ID("lfo.sync"),
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Sync On"_s,
            .gui_label = "Sync"_s,
            .tooltip =
                "Sync ties the LFO's speed to your DAW's tempo. When it's on, Time is chosen as a note length that follows the tempo; when it's off, Time is set freely in Hz.\n\n"
                "Both settings are remembered, so you can flick between them without losing either."_s,
        };
        lp(LegacyLfoShapeV2) = Args {
            .id = id(region, 67), // never change
            .id_string = LAYER_ID("lfo.legacy_shape_v2"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyLfoShapeV2,
                .default_val = (u32)LegacyLfoShapeV2::Sine,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Legacy Shape V2"_s,
            .gui_label = "Shape"_s,
            .tooltip =
                "Legacy LFO shape parameter (v2). Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(LfoShape) = Args {
            .id = id(region, 82), // never change
            .id_string = LAYER_ID("lfo.shape"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoShape,
                .default_val = (u32)LfoShape::Sine,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Shape"_s,
            .gui_label = "Shape"_s,
            .tooltip =
                "Choose the Shape of the LFO. Alongside the classics there are a few more interesting options, such as random and plucky shapes."_s,
        };
        lp(LfoDestination) = Args {
            .id = id(region, 68), // never change
            .id_string = LAYER_ID("lfo.destination"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoDestination,
                .default_val = (u32)LfoDestination::Volume,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Target"_s,
            .gui_label = "Target"_s,
            .tooltip =
                "Choose the Target: what the LFO modulates.\n\n"
                "The modulation is applied relative to the target's current knob/slider. For example, when Volume is chosen, the movement will occur around wherever the layer's volume slider is currently set. Hover over each option for details."_s,
        };

        // =================================================================================================
        lp(EqOn) = Args {
            .id = id(region, 35), // never change
            .id_string = LAYER_ID("eq.on"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Eq},
            .name = "On"_s,
            .gui_label = "EQ"_s,
            .tooltip =
                "Switch on this layer's three-band equaliser to shape its tone before it's mixed with the other layers."};
        lp(LegacyEqFreq1) = Args {
            .id = id(region, 36), // never change
            .id_string = LAYER_ID("eq.band1.legacy_freq"),
            .value_config = val_config_helpers::LegacyFilter({.default_hz = 8000}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Legacy Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip =
                "Legacy band 1 frequency parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqFreq1) = Args {
            .id = id(region, 91), // never change
            .id_string = LAYER_ID("eq.band1.freq"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Filter({.default_hz = 100}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip = k_eq_freq_tooltip,
            .flags = {.cutoff_frequency = true},
        };
        lp(LegacyEqResonance1) = Args {
            .id = id(region, 37), // never change
            .id_string = LAYER_ID("eq.band1.legacy_resonance"),
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Legacy Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = "Legacy resonance parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqResonance1) = Args {
            .id = id(region, 79), // never change
            .id_string = LAYER_ID("eq.band1.resonance"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 20}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = k_eq_resonance_tooltip,
        };
        lp(EqGain1) = Args {
            .id = id(region, 38), // never change
            .id_string = LAYER_ID("eq.band1.gain"),
            .value_config = val_config_helpers::Gain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Gain"_s,
            .gui_label = "Gain"_s,
            .tooltip = k_eq_gain_tooltip,
        };
        lp(LegacyEqType1) = Args {
            .id = id(region, 39), // never change
            .id_string = LAYER_ID("eq.band1.legacy_type"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyEqType,
                .default_val = (u32)LegacyEqType::Peak,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Legacy Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Legacy type parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqType1) = Args {
            .id = id(region, 84), // never change
            .id_string = LAYER_ID("eq.band1.type"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::EqType,
                .default_val = (u32)EqType::LowShelf,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = k_eq_type_tooltip,
        };
        lp(LegacyEqFreq2) = Args {
            .id = id(region, 40), // never change
            .id_string = LAYER_ID("eq.band2.legacy_freq"),
            .value_config = val_config_helpers::LegacyFilter({.default_hz = 500}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Legacy Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip =
                "Legacy band 2 frequency parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqFreq2) = Args {
            .id = id(region, 92), // never change
            .id_string = LAYER_ID("eq.band2.freq"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Filter({.default_hz = 1000}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip = k_eq_freq_tooltip,
            .flags = {.cutoff_frequency = true},
        };
        lp(LegacyEqResonance2) = Args {
            .id = id(region, 41), // never change
            .id_string = LAYER_ID("eq.band2.legacy_resonance"),
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Legacy Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = "Legacy resonance parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqResonance2) = Args {
            .id = id(region, 80), // never change
            .id_string = LAYER_ID("eq.band2.resonance"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = k_eq_resonance_tooltip,
        };
        lp(EqGain2) = Args {
            .id = id(region, 42), // never change
            .id_string = LAYER_ID("eq.band2.gain"),
            .value_config = val_config_helpers::Gain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Gain"_s,
            .gui_label = "Gain"_s,
            .tooltip = k_eq_gain_tooltip,
        };
        lp(LegacyEqType2) = Args {
            .id = id(region, 43), // never change
            .id_string = LAYER_ID("eq.band2.legacy_type"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyEqType,
                .default_val = (u32)LegacyEqType::Peak,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Legacy Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Legacy type parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqType2) = Args {
            .id = id(region, 85), // never change
            .id_string = LAYER_ID("eq.band2.type"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::EqType,
                .default_val = (u32)EqType::Peak,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = k_eq_type_tooltip,
        };
        lp(LegacyEqFreq3) = Args {
            .id = id(region, 86), // never change
            .id_string = LAYER_ID("eq.band3.legacy_freq"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::LegacyFilter({.default_hz = 2000}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band3},
            .name = "Legacy Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip =
                "Legacy band 3 frequency parameter. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(EqFreq3) = Args {
            .id = id(region, 93), // never change
            .id_string = LAYER_ID("eq.band3.freq"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Filter({.default_hz = 10000}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band3},
            .name = "Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip = k_eq_freq_tooltip,
            .flags = {.cutoff_frequency = true},
        };
        lp(EqResonance3) = Args {
            .id = id(region, 87), // never change
            .id_string = LAYER_ID("eq.band3.resonance"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 20}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band3},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = k_eq_resonance_tooltip,
        };
        lp(EqGain3) = Args {
            .id = id(region, 88), // never change
            .id_string = LAYER_ID("eq.band3.gain"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Gain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band3},
            .name = "Gain"_s,
            .gui_label = "Gain"_s,
            .tooltip = k_eq_gain_tooltip,
        };
        lp(EqType3) = Args {
            .id = id(region, 89), // never change
            .id_string = LAYER_ID("eq.band3.type"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::EqType,
                .default_val = (u32)EqType::HighShelf,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band3},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = k_eq_type_tooltip,
        };

        // =================================================================================================
        lp(LegacyVelocityMapping) = Args {
            .id = id(region, 44), // never change
            .id_string = LAYER_ID("config.legacy_velocity_mapping"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::VelocityMappingMode,
                .default_val = (u32)VelocityMappingMode::None,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Legacy Velocity Mapping"_s,
            .gui_label = "Velocity Mapping"_s,
            .tooltip =
                "Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above,"_s,
            .flags = {.legacy = true},
        };
        lp(Keytrack) = Args {
            .id = id(region, 45), // never change
            .id_string = LAYER_ID("config.keytrack"),
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Keytrack On"_s,
            .gui_label = "Keytrack"_s,
            .tooltip =
                "With Keytrack on, this layer follows the keyboard: higher keys play higher pitches. Switch it off and every key plays the sample at its recorded pitch, which might be what you want for drum hits, loops and sound effects that shouldn't be transposed.\n\n"
                "Some sample libraries may override this - marking individual samples as always or never keytracked."_s,
        };
        lp(LegacyMonophonicBool) = Args {
            .id = id(region, 46), // never change
            .id_string = LAYER_ID("config.legacy_monophonic"),
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Legacy Monophonic On"_s,
            .gui_label = "Monophonic"_s,
            .tooltip = "Only allow one voice of each sound to play at a time"_s,
            .flags = {.legacy = true},
        };
        lp(MonophonicMode) = Args {
            .id = id(region, 55), // never change
            .id_string = LAYER_ID("config.monophonic"),
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::MonophonicMode,
                .default_val = (u32)MonophonicMode::Off,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Monophonic Mode"_s,
            .gui_label = "Monophonic"_s,
            .tooltip =
                "Monophonic decides what happens when notes on this layer overlap. Left off, the layer is polyphonic and every note you play gets a voice of its own; the other two modes hold it to one note at a time, in different ways.\n\n"
                "It's set per layer, so a monophonic lead can sit on top of a polyphonic pad in the same preset.\n\n"
                "The arpeggiator handles its own notes, so this has no effect while the arpeggiator is running on this layer."_s,
        };
        lp(MidiTranspose) = Args {
            .id = id(region, 48), // never change
            .id_string = LAYER_ID("config.midi_transpose"),
            .value_config = val_config_helpers::Int({.range = {-36, 36}, .default_val = 0}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "MIDI Transpose On"_s,
            .gui_label = "Transpose"_s,
            .tooltip =
                "Transpose shifts this layer up or down in semitones by changing which samples get triggered, rather than by speeding the audio up or down. On a multi-sampled Instrument that keeps the character of the sound intact, so it's the better choice for large shifts.\n\n"
                "The key range isn't affected: you play the same keys as before, they just reach for different samples.\n\n"
                "Tip: the Pitch control at the top of the layer transposes by re-pitching the audio instead. That's the one to reach for on a single-sample Instrument, and Detune beside it covers amounts smaller than a semitone."_s,
        };
        // Range and Key Fade are four parameters describing one mechanism, so each of them explains the
        // whole thing rather than just its own end of it.
#define KEY_RANGE_BAR_TIP                                                                                    \
    "\n\nTip: see each layer's range on the bar above the keyboard in the bottom panel. Hover it to "        \
    "enlarge it, and use the octave arrows beside the keyboard to scroll beyond the keys shown."

        constexpr String k_key_range_tooltip =
            "This layer only plays within the given range of keys, letting you give each layer its own portion of the keyboard.\n\n"
            "Key Fade below softens the edges of the range, so the layer eases in and out instead of switching on abruptly." KEY_RANGE_BAR_TIP;
        constexpr String k_key_fade_tooltip =
            "Key Fade eases the layer in and out at the edges of its key range instead of letting it switch on abruptly. Both values are in semitones: the left one fades up from Range Low, the right one fades down from Range High.\n\n"
            "Give two layers overlapping fades and they crossfade into one another as you play up the keyboard." KEY_RANGE_BAR_TIP;

#undef KEY_RANGE_BAR_TIP

        lp(KeyRangeLow) = Args {
            .id = id(region, 50), // never change
            .id_string = LAYER_ID("config.key_range_low"),
            .value_config = val_config_helpers::Int({.range = {0, 127}, .default_val = 0}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Key Range Low"_s,
            .gui_label = "Key Range Low"_s,
            .tooltip = k_key_range_tooltip,
        };
        lp(KeyRangeHigh) = Args {
            .id = id(region, 51), // never change
            .id_string = LAYER_ID("config.key_range_high"),
            .value_config = val_config_helpers::Int({.range = {0, 127}, .default_val = 127}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Key Range High"_s,
            .gui_label = "Key Range High"_s,
            .tooltip = k_key_range_tooltip,
        };
        lp(KeyRangeLowFade) = Args {
            .id = id(region, 52), // never change
            .id_string = LAYER_ID("config.key_range_low_fade"),
            .value_config = val_config_helpers::Int({.range = {0, 127}, .default_val = 0}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Key Range Low Fade"_s,
            .gui_label = "Key Range Low Fade"_s,
            .tooltip = k_key_fade_tooltip,
        };
        lp(KeyRangeHighFade) = Args {
            .id = id(region, 53), // never change
            .id_string = LAYER_ID("config.key_range_high_fade"),
            .value_config = val_config_helpers::Int({.range = {0, 127}, .default_val = 0}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Key Range High Fade"_s,
            .gui_label = "Key Range High Fade"_s,
            .tooltip = k_key_fade_tooltip,
        };
        lp(PitchBendRange) = Args {
            .id = id(region, 54), // never change
            .id_string = LAYER_ID("config.pitch_bend_range"),
            .value_config = val_config_helpers::Int({.range = {0, 60}, .default_val = 2}),
            .modules = {layer_module, ParameterModule::Config},
            .name = "Pitch Bend Range"_s,
            .gui_label = "Pitch Bend Range"_s,
            .tooltip =
                "Pitch Bend Range sets how far the pitch wheel bends this layer, in semitones. Each layer has its own, so for example, a lead can bend a whole tone while the pad underneath can be set to zero to ignore the wheel entirely.\n\n"
                "Bending re-pitches the audio by speeding it up or slowing it down, so large bends will noticeably change the character of the sound.\n\n"
                "With MPE enabled, this governs the wheel on the master channel; the per-note bend of each individual note follows the range the controller asks for."_s,
        };
        lp(PlayMode) = Args {
            .id = id(region, 56), // never change
            .id_string = LAYER_ID("playback.mode"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::PlayMode,
                .default_val = (u32)param_values::PlayMode::Standard,
            }),
            .modules = {layer_module, ParameterModule::Playback},
            .name = "Play Mode"_s,
            .gui_label = "Mode"_s,
            .tooltip =
                "Select the Play Mode for this layer: the engine it uses to play its Instrument.\n\n"
                "Standard Playback plays each sample straight through: the playhead moves steadily from start to end, with options for looping and reversing along the way.\n\n"
                "The granular modes instead rebuild the sound from a stream of tiny snippets called grains. In Granular Playback, the point grains are drawn from moves through the sample at a rate you set with Speed, giving a time-stretch-like effect. In Granular Fixed, you place that point yourself with Position, letting you freeze on one part of the sample and explore its texture."_s,
        };
        lp(GranularSpeed) = Args {
            .id = id(region, 58), // never change
            .id_string = LAYER_ID("granular.speed"),
            .added_in_generation = 1,
            .value_config = ({
                ParamDescriptor::Range const linear_range = {0, 1};
                ParamDescriptor::Projection const projection {
                    .range = {0, 8},
                    .exponent = 3.0f,
                    .type = ParamDescriptor::Projection::Type::LinearThenExponential,
                    .split_01 = 0.5f,
                    .split_value = 1.0f,
                };
                val_config_helpers::ValConfig {
                    .linear_range = linear_range,
                    .projection = projection,
                    .default_linear_value = projection.LineariseValue(linear_range, 1),
                    .display_format = ParamDisplayFormat::Percent,
                };
            }),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Speed"_s,
            .gui_label = "Speed"_s,
            .tooltip =
                "Speed sets how fast the playhead travels through the sample in Granular Playback mode. Grains are drawn from wherever the playhead is.\n\n"
                "100% is the sample's original speed. Turn it down to slow the sound to a crawl or stop it altogether, or up to race through at up to eight times normal speed. The playhead still respects the sample start, looping and Reverse settings.\n\n"
                "In Granular Fixed, this knob is replaced by Position."_s,
        };
        lp(GranularPosition) = Args {
            .id = id(region, 59), // never change
            .id_string = LAYER_ID("granular.position"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Position"_s,
            .gui_label = "Position"_s,
            .tooltip =
                "Position chooses where in the sample grains are drawn from, shown as the highlighted region on the waveform. Grains come from here for as long as you hold the note, so you can freeze on one moment of the sound and sustain it indefinitely.\n\n"
                "Tip: you can create movement with this parameter by setting the LFO's target to Grain Position, or by assigning this knob to a macro."_s,
        };
        lp(GranularDensity) = Args {
            .id = id(region, 60), // never change
            .id_string = LAYER_ID("granular.density"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 50}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Density"_s,
            .gui_label = "Density"_s,
            .tooltip =
                "Density sets how many grains overlap at once, relative to their Length. At 0%, each grain starts as the previous one ends, so they follow on one after another. Turning it up starts new grains sooner, stacking more and more of them on top of each other.\n\n"
                "Low values give an open texture where individual grains can be picked out. High values pile up so many grains that they smear into a thick, continuous wash."_s,
        };
        lp(GranularLength) = Args {
            .id = id(region, 57), // never change
            .id_string = LAYER_ID("granular.length"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Ms({.projection = {{5, 1000}, 1.5f}, .default_ms = 200}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Length"_s,
            .gui_label = "Length"_s,
            .tooltip =
                "Length sets how long each grain lasts, from a few milliseconds up to a second.\n\n"
                "Short grains chop the sound into a fine, buzzy texture that keeps little of the original's shape. Long grains preserve much more of the sample's natural character. Density and Smooth are both measured relative to Length, so changing it here changes how they feel too."_s,
        };
        lp(GranularSpread) = Args {
            .id = id(region, 61), // never change
            .id_string = LAYER_ID("granular.spread"),
            .added_in_generation = 1,
            .value_config = ({
                ParamDescriptor::Range const linear_range {0.005f, 1};
                ParamDescriptor::Projection const projection {linear_range, 2.0f};
                val_config_helpers::ValConfig {
                    .linear_range = linear_range,
                    .projection = projection,
                    .default_linear_value = projection.LineariseValue(linear_range, 0.05f),
                    .display_format = ParamDisplayFormat::Percent,
                };
            }),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Spread"_s,
            .gui_label = "Spread"_s,
            .tooltip =
                "Spread widens the area of the sample that grains can start from. Each new grain begins somewhere between the playhead and a point up to this far past it, measured as a percentage of the whole sample.\n\n"
                "Small values keep every grain close to the playhead for a focused, precise sound. Large values scatter grains across a wide stretch of the sample."_s,
        };
        lp(GranularSmoothing) = Args {
            .id = id(region, 62), // never change
            .id_string = LAYER_ID("granular.smoothing"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 50}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Smooth"_s,
            .gui_label = "Smooth"_s,
            .tooltip =
                "Smooth shapes the envelope of each grain: how gently it fades in and out, relative to its Length.\n\n"
                "At 0% grains start and stop abruptly, giving a hard, percussive edge. At 100% the fade-in and fade-out meet in the middle, so neighbouring grains blend into each other for a seamless texture."_s,
        };
        lp(GranularRandomPan) = Args {
            .id = id(region, 63), // never change
            .id_string = LAYER_ID("granular.random_pan"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 20}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Pan"_s,
            .gui_label = "Pan"_s,
            .tooltip =
                "Pan randomises where each grain sits in the stereo field. Every new grain is given its own position, picked at random within the range you set here.\n\n"
                "At 0% every grain plays dead centre. At 100% grains can land anywhere from fully left to fully right, spreading the cloud from one side to the other. A quick way to create an interestingly wide stereo image."_s,
        };
        lp(GranularRandomDetune) = Args {
            .id = id(region, 64), // never change
            .id_string = LAYER_ID("granular.random_detune"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Detune"_s,
            .gui_label = "Detune"_s,
            .tooltip =
                "Detune randomises the pitch of each grain. Every new grain is nudged sharp or flat by a random amount within the range you set here.\n\n"
                "At 0% all grains play in tune. At 100% each grain can be up to a semitone sharp or flat. Small amounts thicken the sound like a chorus; larger amounts get progressively more smeared and out of tune."_s,
        };
        lp(GranularRandomDirection) = Args {
            .id = id(region, 65), // never change
            .id_string = LAYER_ID("granular.random_direction"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Direction"_s,
            .gui_label = "Direction"_s,
            .tooltip =
                "Direction gives each grain a chance of playing the opposite way to the main playhead.\n\n"
                "At 0% every grain follows the playhead's direction. At 100% each grain is equally likely to play forwards or backwards. In between, you can dial in a blend of the two for meandering, less predictable tones."_s,
        };
        lp(GranularHarmony) = Args {
            .id = id(region, 66), // never change
            .id_string = LAYER_ID("granular.harmony"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Playback, ParameterModule::Granular},
            .name = "Harmony"_s,
            .gui_label = "Harmony"_s,
            .tooltip =
                "Harmony gives each grain a chance of playing at a musical interval above or below the note you played, so a single note can bloom into a chord or a shimmering octave.\n\n"
                "At 0% every grain plays at the root. Turning it up shifts more of the grains, until at 100% every grain picks at random from the root and the intervals you've chosen.\n\n"
                "Choose which intervals are allowed with the Intervals menu next to this knob: pick a preset such as Octaves or Major Triad, or toggle individual semitones yourself."_s,
        };

        // Arpeggiator
        // =================================================================================================
        lp(ArpOn) = Args {
            .id = id(region, 81), // never change
            .id_string = LAYER_ID("arp.on"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Arpeggiator"_s,
            .gui_label = "Arpeggiator"_s,
            .tooltip =
                "Switch on the arpeggiator to turn the notes you hold into a rhythmic pattern locked to your host's tempo. Each step retriggers this layer's Instrument exactly as if you'd played the note yourself, so the envelopes, filter, LFOs and effects all respond as normal.\n\n"
                "Every layer has its own arpeggiator, so you can run different patterns side by side."_s,
        };
        lp(ArpMode) = Args {
            .id = id(region, 74), // never change
            .id_string = LAYER_ID("arp.mode"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpMode,
                .default_val = (u32)param_values::ArpMode::Played,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Arpeggiator Mode"_s,
            .gui_label = "Mode"_s,
            .tooltip =
                "Choose where the steps get their notes from. Played Notes arpeggiates whatever you're holding, giving each step one of those notes in the order set by Order. Fixed Notes ignores what you play and runs a sequence of notes you've set yourself, either by dragging each step's note or by capturing a performance with the Record button."_s,
        };
        lp(ArpNoteOrder) = Args {
            .id = id(region, 70), // never change
            .id_string = LAYER_ID("arp.note_order"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpNoteOrder,
                .default_val = (u32)param_values::ArpNoteOrder::Up,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Note Order"_s,
            .gui_label = "Order"_s,
            .tooltip =
                "Order sets how the notes you're holding are shared out across the steps: climbing, falling, bouncing between the two, picked at random, or one of the less common shapes. Chord is the odd one out — it plays every held note together on each step, which is ideal for rhythmic stabs and percussion.\n\n"
                "Not available in Fixed Notes mode."_s,
        };
        lp(ArpTriggerMode) = Args {
            .id = id(region, 71), // never change
            .id_string = LAYER_ID("arp.trigger_mode"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpTriggerMode,
                .default_val = (u32)param_values::ArpTriggerMode::Free,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Trigger"_s,
            .gui_label = "Trigger"_s,
            .tooltip =
                "Trigger decides what a new note does to a pattern that's already running. Free lets it carry on, so you can change chord underneath without breaking the groove. Retrigger restarts it from step 1 every time you press a note, which keeps the pattern locked to your playing."_s,
        };
        lp(LegacyArpRate) = Args {
            .id = id(region, 72), // never change
            .id_string = LAYER_ID("arp.rate"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LegacyArpSyncedRate,
                .default_val = (u32)param_values::LegacyArpSyncedRate::_1_8,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Rate (Legacy)"_s,
            .gui_label = "Rate"_s,
            .tooltip = "Legacy arpeggiator rate. Kept for backwards-compatibility with DAW automation"_s,
            .flags = {.legacy = true},
        };
        lp(ArpRate) = Args {
            .id = id(region, 101), // never change
            .id_string = LAYER_ID("arp.rate_v2"),
            .added_in_generation = 3,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpSyncedRate,
                .default_val = (u32)param_values::ArpSyncedRate::_1_8,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Rate"_s,
            .gui_label = "Rate"_s,
            .tooltip =
                "Rate sets how long each step lasts, synced to your host's tempo. Together with Length it decides how long the whole pattern takes to come round — eight steps at 1/8 fills one bar of 4/4.\n\n"
                "The T and D suffixes are triplet and dotted divisions, useful for swung or lopsided patterns."_s,
        };
        lp(ArpAutoRate) = Args {
            .id = id(region, 76), // never change
            .id_string = LAYER_ID("arp.auto_rate"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpAutoRate,
                .default_val = (u32)param_values::ArpAutoRate::_1x,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Auto Rate"_s,
            .gui_label = "Auto Rate"_s,
            .tooltip =
                "Auto Rate lets Floe pick the step rate so a sliced Instrument plays close to the speed it was recorded at, rather than leaving silent gaps between slices when your host tempo is a long way from the loop's own. While it's on, the rate Floe has chosen is shown in place of the Rate menu.\n\n"
                "The multiplier shifts that choice: 2x for double speed, 0.5x for half, with Dotted and Triplet variants for a different feel. Because the choice has to land on a tempo-synced division it moves in jumps as the tempo changes, and every layer using Auto Rate jumps at the same points so they stay in the same rhythmic relationship."_s,
        };
        lp(ArpLength) = Args {
            .id = id(region, 73), // never change
            .id_string = LAYER_ID("arp.length"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Int({.range = {1, k_arp_max_steps}, .default_val = 8}),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Length"_s,
            .gui_label = "Length"_s,
            .tooltip =
                "Length sets how many steps the pattern runs through before it loops back to the start.\n\n"
                "Steps beyond the length are hidden from the sequencer but keep their settings, so you can shorten a pattern and lengthen it again without losing anything."_s,
        };
        lp(ArpHumanise) = Args {
            .id = id(region, 75), // never change
            .id_string = LAYER_ID("arp.humanise"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Percent({.default_percent = 5}),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Humanise"_s,
            .gui_label = "Humanise"_s,
            .tooltip =
                "Humanise loosens the pattern up by nudging each step's timing and velocity by a random amount. A few percent takes the machine-like edge off; higher settings give a much looser, hand-played feel, which suits percussion especially well.\n\n"
                "Steps are only ever pushed later, never earlier, and never by more than a fifth of a step."_s,
        };
        lp(ArpOctavePolyrate) = Args {
            .id = id(region, 77), // never change
            .id_string = LAYER_ID("arp.octave_polyrate"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::ArpOctavePolyrate,
                .default_val = (u32)param_values::ArpOctavePolyrate::Off,
            }),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "Polyrate"_s,
            .gui_label = "Polyrate"_s,
            .tooltip =
                "Polyrate gives each octave of the notes you're holding its own speed, so a single chord can produce several interleaving rhythms at once. The octave starting at middle C plays at the Rate you've set, and the octaves above and below scale from there by the ratio you choose.\n\n"
                "Tip: hold two notes an octave apart to hear the effect at its clearest."_s,
        };
        lp(ArpOneShot) = Args {
            .id = id(region, 78), // never change
            .id_string = LAYER_ID("arp.one_shot"),
            .added_in_generation = 1,
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Arp},
            .name = "One Shot"_s,
            .gui_label = "One Shot"_s,
            .tooltip =
                "With One Shot on, the pattern plays through once and stops rather than looping for as long as you hold the notes. It's handy for fills and one-off flourishes, or for letting a sliced loop play through exactly once.",
        };

        // =================================================================================================
        lp(MpePressDestination) = Args {
            .id = id(region, 96), // never change
            .id_string = LAYER_ID("config.mpe_press_destination"),
            .added_in_generation = 2,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::MpeDestination,
                .default_val = (u32)MpeDestination::Off,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "MPE Press Target"_s,
            .gui_label = "Press"_s,
            .tooltip =
                "Press is the channel pressure, or aftertouch, that an MPE controller sends on each note's own channel. On most keyboards it follows how hard you're pushing into the key once it's sounding. Choose what it controls on this layer here, and how strongly with Amount beside it.\n\n"
                "Press rests at nothing and climbs from there, so it only ever adds to the destination you point it at.\n\n"
                "MPE has to be switched on in the Performance Controls panel before press does anything."_s,
        };
        lp(MpePressAmount) = Args {
            .id = id(region, 97), // never change
            .id_string = LAYER_ID("config.mpe_press_amount"),
            .added_in_generation = 2,
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 50,
                .display_format = ParamDisplayFormat::Percent,
                .max_percent = 400,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "MPE Press Amount"_s,
            .gui_label = "Amount"_s,
            .tooltip =
                "Amount sets how far press moves its destination, and a negative value flips the direction so the effect runs in reverse.\n\n"
                "Past 100% the destination reaches its limit before press reaches its own, so you get the full effect from a lighter touch."_s,
        };
        lp(MpeSlideDestination) = Args {
            .id = id(region, 98), // never change
            .id_string = LAYER_ID("config.mpe_slide_destination"),
            .added_in_generation = 2,
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::MpeDestination,
                .default_val = (u32)MpeDestination::Off,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "MPE Slide Target"_s,
            .gui_label = "Slide"_s,
            .tooltip =
                "Slide is CC74, which an MPE controller sends on each note's own channel. On most keyboards it follows where your finger sits along the front-to-back axis of the key. Choose what it controls on this layer here, and how strongly with Amount beside it.\n\n"
                "Slide rests in the middle, so it pushes the destination either side of where you've set it.\n\n"
                "MPE has to be switched on in the Performance Controls panel before slide does anything."_s,
        };
        lp(MpeSlideAmount) = Args {
            .id = id(region, 99), // never change
            .id_string = LAYER_ID("config.mpe_slide_amount"),
            .added_in_generation = 2,
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 100,
                .display_format = ParamDisplayFormat::Percent,
                .max_percent = 400,
            }),
            .modules = {layer_module, ParameterModule::Config},
            .name = "MPE Slide Amount"_s,
            .gui_label = "Amount"_s,
            .tooltip =
                "Amount sets how far slide moves its destination, and a negative value flips the direction so the effect runs in reverse.\n\n"
                "Past 100% the destination reaches its limit before slide reaches its own, so you get the full effect from a smaller movement."_s,
        };
    }
#undef LAYER_ID

    // =====================================================================================================
    static_assert(k_num_parameters <= LargestRepresentableValue<IdMapIntType>(),
                  "choose a larger integer for storing the param map");

    Bitset<result.id_map.size> used_ids;
    for (auto const i : Range(k_num_parameters)) {
        auto const param_id = result.params[i].id;
        if (used_ids.Get(param_id))
            throw "duplicate ID"; // if this codepath is reached, there will be a compile-error
        result.id_map[param_id] = (IdMapIntType)i;
        used_ids.Set(param_id);
    }

    for (auto const i : Range(k_num_parameters))
        result.params[i].index = (ParamIndex)i;

    for (auto& p : result.params) {
        if (!p.linear_range.Contains(p.default_linear_value)) throw "";
        if (!p.name.size) throw "";
        if (!p.id_string.size) throw "missing id_string";
        for (auto c : p.id_string) {
            bool const valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
            if (!valid) throw "id_string must be lowercase reverse-domain style";
        }
    }
    // id_string uniqueness is verified by a runtime test (the consteval O(n^2) string compare across
    // ~380 parameters exceeds the compiler's constant-evaluation step limit).

    return result;
}

constexpr auto k_create_params_result = CreateParams();

constexpr auto k_param_descriptors = k_create_params_result.params;
constexpr auto k_id_map = k_create_params_result.id_map;

constexpr usize k_num_experimental_parameters = []() {
    usize n = 0;
    for (auto& p : k_param_descriptors)
        if (p.flags.experimental) ++n;
    return n;
}();

constexpr usize k_num_non_experimental_parameters = k_num_parameters - k_num_experimental_parameters;

struct ComptimeParamSearchOptions {
    ParamModules modules {};
    Optional<ParamIndex> skip {};
    Optional<ParamIndex> skip2 {};
};

template <ComptimeParamSearchOptions k_criteria>
constexpr auto ComptimeParamSearch() {
    constexpr auto k_modules = Span<ParameterModule const> {k_criteria.modules}.SubSpan(0, []() {
        usize n = 0;
        for (auto m : k_criteria.modules) {
            if (m == ParameterModule::None) break;
            ++n;
        }
        return n;
    }());

    constexpr auto k_matches_criteria = [k_modules](ParamDescriptor p) {
        if (k_criteria.skip && *k_criteria.skip == p.index) return false;
        if (k_criteria.skip2 && *k_criteria.skip2 == p.index) return false;
        return StartsWithSpan(p.module_parts, k_modules);
    };

    constexpr auto k_num_results = [k_matches_criteria]() {
        usize n = 0;
        for (auto& p : k_param_descriptors)
            if (k_matches_criteria(p)) ++n;
        return n;
    }();

    Array<ParamIndex, k_num_results> result {};
    usize n = 0;
    for (auto& p : k_param_descriptors)
        if (k_matches_criteria(p)) result[n++] = p.index;

    Sort(result, [](ParamIndex a, ParamIndex b) {
        auto const& a_desc = k_param_descriptors[ToInt(a)];
        auto const& b_desc = k_param_descriptors[ToInt(b)];
        if (a_desc.grouping_within_module == b_desc.grouping_within_module) return a < b;
        return a_desc.grouping_within_module < b_desc.grouping_within_module;
    });

    return result;
}

constexpr ParamDescriptor const& ParamDescriptorAt(ParamIndex index) {
    return k_param_descriptors[ToInt(index)];
}

constexpr Optional<ParamIndex> ParamIdToIndex(u32 id) {
    if (id >= k_id_map.size) return {};
    auto const result = k_id_map[id];
    if (result == k_invalid_param_id) return {};
    return (ParamIndex)result;
}
constexpr u32 ParamIndexToId(ParamIndex index) { return k_param_descriptors[ToInt(index)].id; }

constexpr Optional<ParamIndex> ParamIndexFromIdString(String id_string) {
    struct IdStringLookup {
        struct Element {
            String key {};
            ParamIndex value {};
            u64 hash {};
        };

        constexpr IdStringLookup() {
            for (auto const& p : k_param_descriptors)
                Insert(p.id_string, p.index);
        }

        constexpr usize Lookup(String name, u64 hash) const {
            auto const k_mask = elements.size - 1;

            usize index = hash;
            usize step = 1;

            while (true) {
                auto const array_index = index & k_mask;
                auto& element = elements[array_index];
                if (element.hash == 0) return array_index; // empty
                if (element.hash == hash && element.key == name) return array_index; // found

                // quadratic probing
                index += step;
                ++step;
            }
        }

        constexpr void Insert(String name, ParamIndex index) {
            auto const hash = HashFnv1a(name);
            auto& element = elements[Lookup(name, hash)];
            element.key = name;
            element.value = index;
            element.hash = hash;
        }

        constexpr Optional<ParamIndex> Find(String name) const {
            auto& element = elements[Lookup(name, HashFnv1a(name))];
            if (element.hash == 0) return k_nullopt;
            return element.value;
        }

        Array<Element, NextPowerOf2((u32)k_num_parameters) * 2> elements;
    };

    constexpr IdStringLookup k_id_string_lookup;
    return k_id_string_lookup.Find(id_string);
}

Span<String const> ParameterMenuItems(ParamIndex param_index);
Optional<String> ParameterMenuItemDescription(ParamIndex param_index, u32 item_index);

String ParamMenuText(ParamIndex index, f32 value);
inline bool ParamToBool(f32 value) { return value != 0; }

template <typename Type>
Type ParamToInt(f32 value) {
    auto const i = (s64)Trunc(value);
    if constexpr (Enum<Type>) {
        ASSERT(i >= 0);
        ASSERT(i < (s64)Type::Count);
    }
    return (Type)i;
}

enum class NoLongerExistingParam : u8 {
    ConvolutionLegacyMirageIrName,

    Layer1LoopOnSwitch,
    Layer1LoopPingPongOnSwitch,
    Layer2LoopOnSwitch,
    Layer2LoopPingPongOnSwitch,
    Layer3LoopOnSwitch,
    Layer3LoopPingPongOnSwitch,

    // Reverb had 2 modes: freeverb or sv
    // Params affecting both modes:
    ReverbOnSwitch,
    ReverbDryDb,
    ReverbSizePercent,
    ReverbUseFreeverbSwitch,
    // Freeverb mode:
    ReverbFreeverbDampingPercent,
    ReverbFreeverbWidthPercent,
    ReverbFreeverbWetPercent,
    // Sv mode:
    ReverbSvPreDelayMs,
    ReverbSvModFreqHz,
    ReverbSvModDepthPercent,
    ReverbSvFilterBidirectionalPercent, // 0 is no filter, larger positives cause strong lowpass, larger
                                        // negatives cause strong highpass
    ReverbSvWetDb,

    SvPhaserFreqHz,
    SvPhaserModFreqHz,
    SvPhaserModDepth,
    SvPhaserFeedback,
    SvPhaserNumStages,
    SvPhaserModStereo,
    SvPhaserWet,
    SvPhaserDry,
    SvPhaserOn,

    DelayOldDelayTimeLMs,
    DelayOldDelayTimeRMs,
    DelayOldDamping,
    DelayTimeSyncedL,
    DelayTimeSyncedR,
    DelayFeedback,
    DelayTimeSyncSwitch,
    DelayWet,
    DelayOn,
    DelayLegacyAlgorithm,
    DelaySinevibesMode,
    DelaySinevibesDelayTimeLMs,
    DelaySinevibesDelayTimeRMs,
    DelaySinevibesFilter,

    Count,
};

enum class ParamExistance : u8 {
    StillExists,
    NoLongerExists,
};

using LegacyParam = TaggedUnion<ParamExistance,
                                TypeAndTag<ParamIndex, ParamExistance::StillExists>,
                                TypeAndTag<NoLongerExistingParam, ParamExistance::NoLongerExists>>;

Optional<LegacyParam> ParamFromLegacyId(String id);
Optional<DynamicArrayBounded<char, 64>> ParamToLegacyId(LegacyParam index);

bool IsParamCurrentlyRelevant(ParamIndex index, StaticSpan<f32 const, k_num_parameters> linear_param_values);
