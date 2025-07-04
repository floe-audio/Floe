// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/constants.hpp"

enum class LayerParamIndex : u16 {
    Volume,
    Mute,
    Solo,
    Pan,
    TuneCents,
    TuneSemitone,
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
    FilterCutoff,
    FilterResonance,
    FilterType,
    FilterEnvAmount,
    FilterAttack,
    FilterDecay,
    FilterSustain,
    FilterRelease,
    LfoOn,
    LfoShape,
    LfoRestart,
    LfoAmount,
    LfoDestination,
    LfoRateTempoSynced,
    LfoRateHz,
    LfoSyncSwitch,
    EqOn,
    EqFreq1,
    EqResonance1,
    EqGain1,
    EqType1,
    EqFreq2,
    EqResonance2,
    EqGain2,
    EqType2,
    VelocityMapping,
    Keytrack,
    Monophonic,
    MidiTranspose,

    Count,
};

constexpr auto k_num_layer_parameters = ToInt(LayerParamIndex::Count);

enum class ParamIndex : u16 {
    FirstNonLayerParam = ToInt(LayerParamIndex::Count) * k_num_layers,

    MasterVolume = FirstNonLayerParam,
    MasterVelocity,
    MasterTimbre,

    DistortionType,
    DistortionDrive,
    DistortionOn,

    BitCrushBits,
    BitCrushBitRate,
    BitCrushWet,
    BitCrushDry,
    BitCrushOn,

    CompressorThreshold,
    CompressorRatio,
    CompressorGain,
    CompressorAutoGain,
    CompressorOn,

    FilterOn,
    FilterCutoff,
    FilterResonance,
    FilterGain,
    FilterType,

    StereoWidenWidth,
    StereoWidenOn,

    ChorusRate,
    ChorusHighpass,
    ChorusDepth,
    ChorusWet,
    ChorusDry,
    ChorusOn,

    DelayMode,
    DelayFilterCutoffSemitones,
    DelayFilterSpread,
    DelayMix,
    DelayFeedback,
    DelayTimeLMs,
    DelayTimeRMs,
    DelayTimeSyncSwitch,
    DelayTimeSyncedL,
    DelayTimeSyncedR,
    DelayOn,

    PhaserCenterSemitones,
    PhaserModFreqHz,
    PhaserModDepth,
    PhaserFeedback,
    PhaserShape,
    PhaserStereoAmount,
    PhaserMix,
    PhaserOn,

    ConvolutionReverbHighpass,
    ConvolutionReverbWet,
    ConvolutionReverbDry,
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

    CountHelper,
    NonLayerParamsCount = CountHelper - FirstNonLayerParam,
};

constexpr auto k_num_parameters =
    (ToInt(LayerParamIndex::Count) * k_num_layers) + ToInt(ParamIndex::NonLayerParamsCount);

enum class ParamDisplayFormat : u8 {
    None,
    Percent,
    Pan,
    SinevibesFilter,
    Ms,
    VolumeAmp,
    Hz,
    VolumeDbRange,
    Cents,
    Semitones,
};

enum class ParamValueType : u8 {
    Float,
    Menu,
    Bool,
    Int,
};

struct ParamFlags {
    u8 not_automatable : 1;
    u8 hidden : 1;
};

enum class ParameterModule : u8 {
    None = 0,

    Layer1,
    Layer2,
    Layer3,

    Effect,
    Master,

    Lfo,
    Loop,
    Filter,
    Playback,
    Eq,
    VolEnv,

    Distortion,
    Reverb,
    Delay,
    StereoWiden,
    Chorus,
    Phaser,
    ConvolutionReverb,
    Bitcrush,
    Compressor,

    Band1,
    Band2,

    Count
};

constexpr String k_parameter_module_strings[] = {
    "",

    "Layer 1",    "Layer 2",    "Layer 3",

    "Effect",     "Master",

    "LFO",        "Loop",       "Filter",  "Playback",    "EQ",     "Volume Envelope",

    "Distortion", "Reverb",     "Delay",   "StereoWiden", "Chorus", "Phaser",          "Convolution Reverb",
    "Bitcrush",   "Compressor",

    "Band 1",     "Band 2",
};

static_assert(ArraySize(k_parameter_module_strings) == ToInt(ParameterModule::Count));

using ParamModules = Array<ParameterModule, 4>;

namespace param_values {

enum class EqType : u8 { // never reorder
    Peak,
    LowShelf,
    HighShelf,
    Count,
};
constexpr auto k_eq_type_strings = ArrayT<String>({
    "Peak",
    "Low-shelf",
    "High-shelf",
});
static_assert(k_eq_type_strings.size == ToInt(EqType::Count));

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

enum class LfoSyncedRate : u8 { // never reorder
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
constexpr auto k_lfo_synced_rate_strings = ArrayT<String>({
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D",
    "1/8T",  "1/8",  "1/8D",  "1/4T",  "1/4",  "1/4D",  "1/2T",  "1/2",  "1/2D",
    "1/1T",  "1/1",  "1/1D",  "2/1T",  "2/1",  "2/1D",  "4/1T",  "4/1",  "4/1D",
});
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

enum class LfoDestination : u8 { // never reorder
    Volume,
    Filter,
    Pan,
    Pitch,
    Count,
};
constexpr auto k_lfo_destinations_strings = ArrayT<String>({
    "Volume",
    "Filter",
    "Pan",
    "Pitch",
});
static_assert(k_lfo_destinations_strings.size == ToInt(LfoDestination::Count));

enum class LfoShape : u8 { // never reorder
    Sine,
    Triangle,
    Sawtooth,
    Square,
    Count,
};
constexpr auto k_lfo_shape_strings = ArrayT<String>({
    "Sine",
    "Triangle",
    "Sawtooth",
    "Square",
});
static_assert(k_lfo_shape_strings.size == ToInt(LfoShape::Count));

enum class LayerFilterType : u8 { // never reorder
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
constexpr auto k_layer_filter_type_strings = ArrayT<String>({
    "Low-pass",
    "Band-pass A",
    "High-pass",
    "Band-pass B",
    "Band-shelving",
    "Notch",
    "All-pass (Legacy)", // TODO: remove this
    "Peak",
});
static_assert(k_layer_filter_type_strings.size == ToInt(LayerFilterType::Count));

enum class EffectFilterType : u8 { // never reorder
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peak,
    LowShelf,
    HighShelf,
    Count,
};
constexpr auto k_effect_filter_type_strings = ArrayT<String>({
    "Low-pass",
    "High-pass",
    "Band-pass",
    "Notch",
    "Peak",
    "Low-shelf",
    "High-shelf",
});
static_assert(k_effect_filter_type_strings.size == ToInt(EffectFilterType::Count));

enum class DistortionType : u8 { // never reorder
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
constexpr auto k_distortion_type_strings = ArrayT<String>({
    "Tube Log",
    "Tube Asym3",
    "Sine",
    "Raph1",
    "Decimate",
    "Atan",
    "Clip",
    "Foldback",
    "Rectifier",
    "Ring Mod",
});
static_assert(k_distortion_type_strings.size == ToInt(DistortionType::Count));

enum class DelaySyncedTime : u8 { // never reorder
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
constexpr auto k_delay_synced_time_strings = ArrayT<String>({
    "1/64T", "1/64", "1/64D", "1/32T", "1/32", "1/32D", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
    "1/8D",  "1/4T", "1/4",   "1/4D",  "1/2T", "1/2",   "1/2D",  "1/1T", "1/1",   "1/1D",
});
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

} // namespace param_values

struct ParamDescriptor {
    enum class MenuType : u8 {
        None,
        LoopMode,
        EqType,
        LfoSyncedRate,
        LfoRestartMode,
        LfoDestination,
        LfoShape,
        LayerFilterType,
        EffectFilterType,
        DistortionType,
        DelaySyncedTime,
        DelayMode,
        VelocityMappingMode,
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

        constexpr f32 ProjectValue(f32 linear_value, Range linear_range_) const {
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

        template <f32 (*pow_fn)(f32, f32)>
        constexpr f32 LineariseValue(Range linear_range_, f32 projected_value) const {
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

        Range range;
        f32 exponent;
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
        , grouping_within_module(args.related_params_group) {}

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

    constexpr Optional<f32> LineariseValue(f32 projected_value, bool clamp_if_out_of_range) const {
        auto const projection_range = ProjectionRange();
        if (clamp_if_out_of_range)
            projected_value = Clamp(projected_value, projection_range.min, projection_range.max);
        else if (projected_value < projection_range.min || projected_value > projection_range.max)
            return k_nullopt;

        if (projection) return projection->LineariseValue<Pow<f32>>(linear_range, projected_value);

        return projected_value;
    }

    Optional<f32> StringToLinearValue(String str) const;
    Optional<DynamicArrayBounded<char, 128>> LinearValueToString(f32 linear_value) const;

    constexpr bool IsEffectParam() const { return module_parts[0] == ParameterModule::Effect; }
    constexpr bool IsLayerParam() const {
        return module_parts[0] == ParameterModule::Layer1 || module_parts[0] == ParameterModule::Layer2 ||
               module_parts[0] == ParameterModule::Layer3;
    }

    DynamicArrayBounded<char, 128> ModuleString(char separator = '/') const {
        DynamicArrayBounded<char, 128> result {};
        for (auto m : module_parts) {
            if (m == ParameterModule::None) break;
            if (result.size != 0) dyn::Append(result, separator);
            dyn::AppendSpan(result, k_parameter_module_strings[int(m)]);
        }
        return result;
    }

    ParamIndex index;
    u32 id; // never change
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
    // If non-zero, signifies that it might be shown grouped with others with the same group and in ascending
    // order.
    u8 grouping_within_module;
};

constexpr ParamIndex ParamIndexFromLayerParamIndex(u32 layer_index, LayerParamIndex layer_param_index) {
    return ParamIndex((layer_index * (u32)LayerParamIndex::Count) + (u32)layer_param_index);
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
        case ParamDescriptor::MenuType::EqType: return k_eq_type_strings;
        case ParamDescriptor::MenuType::LoopMode: return k_loop_mode_strings;
        case ParamDescriptor::MenuType::LfoSyncedRate: return k_lfo_synced_rate_strings;
        case ParamDescriptor::MenuType::LfoRestartMode: return k_lfo_restart_mode_strings;
        case ParamDescriptor::MenuType::LfoDestination: return k_lfo_destinations_strings;
        case ParamDescriptor::MenuType::LfoShape: return k_lfo_shape_strings;
        case ParamDescriptor::MenuType::LayerFilterType: return k_layer_filter_type_strings;
        case ParamDescriptor::MenuType::EffectFilterType: return k_effect_filter_type_strings;
        case ParamDescriptor::MenuType::DistortionType: return k_distortion_type_strings;
        case ParamDescriptor::MenuType::DelaySyncedTime: return k_delay_synced_time_strings;
        case ParamDescriptor::MenuType::DelayMode: return k_delay_mode_strings;
        case ParamDescriptor::MenuType::VelocityMappingMode: return k_velocity_mapping_mode_strings;
        case ParamDescriptor::MenuType::None:
        case ParamDescriptor::MenuType::Count: break;
    }
    throw "";
    return {};
}

namespace val_config_helpers {

using ValConfig = ParamDescriptor::ConstructorArgs::ValueConfig;

constexpr f32 DbToAmp(f32 db) { return (f32)constexpr_math::Pow(10.0, (f64)db / 20.0); }
constexpr f32 LogWithBase(f32 base, f32 x) {
    return (f32)(constexpr_math::Log((f64)x) / constexpr_math::Log((f64)base));
}

struct PercentOptions {
    f32 default_percent;
};
constexpr ValConfig Percent(PercentOptions opts) {
    return ValConfig {
        .linear_range = {0, 1},
        .default_linear_value = opts.default_percent / 100,
        .display_format = ParamDisplayFormat::Percent,
    };
}

struct BidirectionalPercentOptions {
    f32 default_percent;
    ParamDisplayFormat display_format;
};
constexpr ValConfig BidirectionalPercent(BidirectionalPercentOptions opts) {
    return ValConfig {
        .linear_range = {-1, 1},
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
        .default_linear_value =
            p.LineariseValue<constexpr_math::Powf>(linear_range, DbToAmp(opts.default_db)),
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
        .default_linear_value =
            projection.LineariseValue<constexpr_math::Powf>(linear_range, opts.default_db),
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
        .default_linear_value =
            opts.projection.LineariseValue<constexpr_math::Powf>(linear_range, opts.default_ms),
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
        .default_linear_value =
            opts.projection.LineariseValue<constexpr_math::Powf>(linear_range, opts.default_hz),
        .display_format = ParamDisplayFormat::Hz,
    };
}

struct FilterOptions {
    f32 default_hz;
};
constexpr ValConfig Filter(FilterOptions opts) {
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
        .default_linear_value =
            opts.projection.LineariseValue<constexpr_math::Powf>(linear_range, opts.default_val),
        .display_format = opts.display_format,
    };
}
} // namespace val_config_helpers

using IdMapIntType = u16;
constexpr IdMapIntType k_invalid_param_id = LargestRepresentableValue<IdMapIntType>();

consteval auto CreateParams() {
    // =====================================================================================================
    constexpr u32 k_ids_per_region = 160; // never change

    enum class IdRegion {
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
        .value_config = val_config_helpers::Volume({.default_db = 0}),
        .modules = {ParameterModule::Master},
        .name = "Volume"_s,
        .gui_label = "Vol"_s,
        .tooltip = "Master volume"_s,
    };

    mp(MasterVelocity) = Args {
        .id = id(IdRegion::Master, 1), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Master},
        .name = "Velocity To Volume Strength"_s,
        .gui_label = "Velo"_s,
        .tooltip =
            "The amount that the MIDI velocity affects the volume of notes; 100% means notes will be silent when the velocity is very soft, and 0% means that notes will play full volume regardless of the velocity"_s,
        .flags = {.hidden = true},
    };
    mp(MasterTimbre) = Args {
        .id = id(IdRegion::Master, 2), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 80}),
        .modules = {ParameterModule::Master},
        .name = "Timbre"_s,
        .gui_label = "Timbre"_s,
        .tooltip =
            "The intstruments timbre. Not every instrument contains timbre information; instruments that do will be highlighted when you click on this knob."_s,
    };

    // =====================================================================================================
    mp(DistortionType) = Args {
        .id = id(IdRegion::Master, 3), // never change
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DistortionType,
            .default_val = (u32)DistortionType::TubeLog,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip = "Distortion algorithm type"_s,
    };
    mp(DistortionDrive) = Args {
        .id = id(IdRegion::Master, 4), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "Drive"_s,
        .gui_label = "Drive"_s,
        .tooltip = "Distortion amount"_s,
    };
    mp(DistortionOn) = Args {
        .id = id(IdRegion::Master, 5), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Distortion},
        .name = "On"_s,
        .gui_label = "Distortion"_s,
        .tooltip = "Enable/disable the distortion effect"_s,
    };

    // =====================================================================================================
    mp(BitCrushBits) = Args {
        .id = id(IdRegion::Master, 6), // never change
        .value_config = val_config_helpers::Int({.range = {2, 32}, .default_val = 32}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Bits"_s,
        .gui_label = "Bits"_s,
        .tooltip = "Audio resolution"_s,
    };
    mp(BitCrushBitRate) = Args {
        .id = id(IdRegion::Master, 7), // never change
        .value_config = val_config_helpers::CustomProjected({
            .display_format = ParamDisplayFormat::Hz,
            .default_val = 44100,
            .projection = {{256, 44100}, 3.0f},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Sample Rate"_s,
        .gui_label = "Samp Rate"_s,
        .tooltip = "Sample rate"_s,
    };
    mp(BitCrushWet) = Args {
        .id = id(IdRegion::Master, 8), // never change
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Processed signal volume"_s,
    };
    mp(BitCrushDry) = Args {
        .id = id(IdRegion::Master, 9), // never change
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Unprocessed signal volume"_s,
    };
    mp(BitCrushOn) = Args {
        .id = id(IdRegion::Master, 10), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Bitcrush},
        .name = "On"_s,
        .gui_label = "Bit Crush"_s,
        .tooltip = "Enable/disable the bitcrush effect"_s,
    };

    // =====================================================================================================
    mp(CompressorThreshold) = Args {
        .id = id(IdRegion::Master, 11), // never change
        .value_config = val_config_helpers::Volume({.default_db = 0, .max_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Threshold"_s,
        .gui_label = "Threshold"_s,
        .tooltip =
            "The threshold that the audio has to pass above before the compression should start taking place"_s,
    };
    mp(CompressorRatio) = Args {
        .id = id(IdRegion::Master, 12), // never change
        .value_config = val_config_helpers::CustomLinear({
            .range = {1, 20},
            .default_val = 2,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Ratio"_s,
        .gui_label = "Ratio"_s,
        .tooltip = "The intensity of compression (high ratios mean more compression)"_s,
    };
    mp(CompressorGain) = Args {
        .id = id(IdRegion::Master, 13), // never change
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = "Additional control for volume after compression"_s,
    };
    mp(CompressorAutoGain) = Args {
        .id = id(IdRegion::Master, 14), // never change
        .value_config = val_config_helpers::Bool({.default_state = true}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "Auto Gain"_s,
        .gui_label = "Auto Gain"_s,
        .tooltip =
            "Automatically re-adjust the gain to stay consistent regardless of compression intensity"_s,
    };
    mp(CompressorOn) = Args {
        .id = id(IdRegion::Master, 15), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Compressor},
        .name = "On"_s,
        .gui_label = "Compressor",
        .tooltip = "Enable/disable the compression effect"_s,
    };

    // =====================================================================================================
    mp(FilterOn) = Args {
        .id = id(IdRegion::Master, 16), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "On"_s,
        .gui_label = "Filter"_s,
        .tooltip = "Enable/disable the filter"_s,
    };
    mp(FilterCutoff) = Args {
        .id = id(IdRegion::Master, 17), // never change
        .value_config = val_config_helpers::Filter({.default_hz = 5000}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Cutoff Frequency"_s,
        .gui_label = "Cutoff"_s,
        .tooltip = "Frequency of filter effect"_s,
    };
    mp(FilterResonance) = Args {
        .id = id(IdRegion::Master, 18), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 30}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Resonance"_s,
        .gui_label = "Reso"_s,
        .tooltip = "The intensity of the volume peak at the cutoff frequency"_s,
    };
    mp(FilterGain) = Args {
        .id = id(IdRegion::Master, 19), // never change
        .value_config = val_config_helpers::Gain({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Gain"_s,
        .gui_label = "Gain"_s,
        .tooltip = "Volume gain of shelf filter"_s,
    };
    mp(FilterType) = Args {
        .id = id(IdRegion::Master, 20), // never change
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::EffectFilterType,
            .default_val = (u32)EffectFilterType::LowPass,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Filter},
        .name = "Type"_s,
        .gui_label = "Type"_s,
        .tooltip = "Filter type"_s,
    };

    // =====================================================================================================
    mp(StereoWidenWidth) = Args {
        .id = id(IdRegion::Master, 21), // never change
        .value_config = val_config_helpers::BidirectionalPercent({
            .default_percent = 15,
            .display_format = ParamDisplayFormat::Percent,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "Width"_s,
        .gui_label = "Width"_s,
        .tooltip = "Increase or decrease the stereo width"_s,
    };
    mp(StereoWidenOn) = Args {
        .id = id(IdRegion::Master, 22), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::StereoWiden},
        .name = "On"_s,
        .gui_label = "Stereo Widen On"_s,
        .tooltip = "Turn the stereo widen effect on or off"_s,
    };

    // =====================================================================================================
    mp(ChorusRate) = Args {
        .id = id(IdRegion::Master, 23), // never change
        .value_config = val_config_helpers::HzSlow({.default_hz = 5}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Rate"_s,
        .gui_label = "Rate"_s,
        .tooltip = "Chorus modulation rate"_s,
    };
    mp(ChorusHighpass) = Args {
        .id = id(IdRegion::Master, 24), // never change
        .value_config = val_config_helpers::Filter({.default_hz = 1000}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip = "High-pass filter cutoff"_s,
    };
    mp(ChorusDepth) = Args {
        .id = id(IdRegion::Master, 25), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 10}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Depth"_s,
        .gui_label = "Depth"_s,
        .tooltip = "Chorus effect intensity"_s,
    };
    mp(ChorusWet) = Args {
        .id = id(IdRegion::Master, 26), // never change
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Processed signal volume"_s,
    };
    mp(ChorusDry) = Args {
        .id = id(IdRegion::Master, 27), // never change
        .value_config = val_config_helpers::Volume({.default_db = -6}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Unprocessed signal volume"_s,
    };
    mp(ChorusOn) = Args {
        .id = id(IdRegion::Master, 28), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Chorus},
        .name = "On"_s,
        .gui_label = "Chorus"_s,
        .tooltip = "Enable/disable the chorus effect"_s,
    };

    // =====================================================================================================
    mp(DelayMode) = Args {
        .id = id(IdRegion::Master, 90), // never change
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelayMode,
            .default_val = (u32)DelayMode::Stereo,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Mode"_s,
        .gui_label = "Mode"_s,
        .tooltip = "Delay type"_s,
    };

    mp(DelayFilterCutoffSemitones) = Args {
        .id = id(IdRegion::Master, 91), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 60}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Filter Cutoff"_s,
        .gui_label = "Filter"_s,
        .tooltip = "High/low frequency reduction"_s,
    };

    mp(DelayFilterSpread) = Args {
        .id = id(IdRegion::Master, 92), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Filter Spread"_s,
        .gui_label = "Spread"_s,
        .tooltip = "Width of the filter"_s,
    };
    mp(DelayTimeLMs) = Args {
        .id = id(IdRegion::Master, 93), // never change
        .value_config = val_config_helpers::Ms({.projection = {{15, 4000}, 2.0f}, .default_ms = 470}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Left (ms)"_s,
        .gui_label = "Time L"_s,
        .tooltip = "Left delay time (in milliseconds)"_s,
    };
    mp(DelayTimeRMs) = Args {
        .id = id(IdRegion::Master, 94), // never change
        .value_config = val_config_helpers::Ms({.projection = {{15, 4000}, 2.0f}, .default_ms = 470}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Legacy Time Right (ms)"_s,
        .gui_label = "Time R"_s,
        .tooltip = "Right delay time (in milliseconds)"_s,
    };
    mp(DelayTimeSyncedL) = Args {
        .id = id(IdRegion::Master, 95), // never change
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelaySyncedTime,
            .default_val = (u32)DelaySyncedTime::_1_4,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Left (Tempo Synced)"_s,
        .gui_label = "Time L"_s,
        .tooltip = "Left delay time (synced to the host tempo)"_s,
    };
    mp(DelayTimeSyncedR) = Args {
        .id = id(IdRegion::Master, 96), // never change
        .value_config = val_config_helpers::Menu({
            .type = ParamDescriptor::MenuType::DelaySyncedTime,
            .default_val = (u32)DelaySyncedTime::_1_8,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Time Right (Tempo Synced)"_s,
        .gui_label = "Time R"_s,
        .tooltip = "Right delay time (synced to the host tempo)"_s,
    };
    mp(DelayTimeSyncSwitch) = Args {
        .id = id(IdRegion::Master, 97), // never change
        .value_config = val_config_helpers::Bool({.default_state = true}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "On"_s,
        .gui_label = "Tempo Sync"_s,
        .tooltip = "Synchronise timings to the host's BPM"_s,
    };
    mp(DelayMix) = Args {
        .id = id(IdRegion::Master, 98), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Level of processed signal"_s,
    };
    mp(DelayOn) = Args {
        .id = id(IdRegion::Master, 99), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "On"_s,
        .gui_label = "Delay"_s,
        .tooltip = "Enable/disable the delay effect"_s,
    };
    mp(DelayFeedback) = Args {
        .id = id(IdRegion::Master, 100), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Delay},
        .name = "Feedback"_s,
        .gui_label = "Feedback"_s,
        .tooltip = "How much the signal repeats"_s,
    };

    // =====================================================================================================
    mp(PhaserFeedback) = Args {
        .id = id(IdRegion::Master, 82), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 40}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Feedback"_s,
        .gui_label = "Feedback"_s,
        .tooltip = "Feedback amount"_s,
        .related_params_group = 1,
    };
    mp(PhaserModFreqHz) = Args {
        .id = id(IdRegion::Master, 83), // never change
        .value_config = val_config_helpers::HzSlow({
            .default_hz = 0.2f,
            .exponent = 2.5f,
            .range = {0.01f, 20},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mod Rate"_s,
        .gui_label = "Rate"_s,
        .tooltip = "Speed at which the phaser filters modulate"_s,
        .related_params_group = 3,
    };
    mp(PhaserCenterSemitones) = Args {
        .id = id(IdRegion::Master, 84), // never change
        .value_config = val_config_helpers::Semitones({
            .default_val = 60,
            .range = {8, 136},
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Center Frequency"_s,
        .gui_label = "Freq"_s,
        .tooltip = "Center frequency of the phaser filters"_s,
        .related_params_group = 0,
    };
    mp(PhaserShape) = Args {
        .id = id(IdRegion::Master, 85), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Shape"_s,
        .gui_label = "Shape"_s,
        .tooltip = "Shape of the phaser filter's peaks"_s,
        .related_params_group = 2,
    };
    mp(PhaserModDepth) = Args {
        .id = id(IdRegion::Master, 86), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 20, .range = {0, 48}}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mod Depth"_s,
        .gui_label = "Depth"_s,
        .tooltip = "The range over which the phaser filters modulate"_s,
        .related_params_group = 3,
    };
    mp(PhaserStereoAmount) = Args {
        .id = id(IdRegion::Master, 87), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 5}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Stereo Amount"_s,
        .gui_label = "Stereo"_s,
        .tooltip = "Adds a stereo effect by offsetting the left and right filters"_s,
        .related_params_group = 4,
    };
    mp(PhaserMix) = Args {
        .id = id(IdRegion::Master, 88), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Mix between the wet and dry signals"_s,
        .related_params_group = 5,
    };
    mp(PhaserOn) = Args {
        .id = id(IdRegion::Master, 89), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Phaser},
        .name = "On"_s,
        .gui_label = "Phaser"_s,
        .tooltip = "Enable/disable the phaser effect"_s,
    };

    // =====================================================================================================
    mp(ConvolutionReverbHighpass) = Args {
        .id = id(IdRegion::Master, 65), // never change
        .value_config = val_config_helpers::Filter({.default_hz = 30}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "High-pass"_s,
        .gui_label = "High-pass"_s,
        .tooltip = "Wet high-pass filter cutoff"_s,
    };
    mp(ConvolutionReverbWet) = Args {
        .id = id(IdRegion::Master, 66), // never change
        .value_config = val_config_helpers::Volume({.default_db = -30}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Wet"_s,
        .gui_label = "Wet"_s,
        .tooltip = "Processed signal volume"_s,
    };
    mp(ConvolutionReverbDry) = Args {
        .id = id(IdRegion::Master, 67), // never change
        .value_config = val_config_helpers::Volume({.default_db = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "Dry"_s,
        .gui_label = "Dry"_s,
        .tooltip = "Unprocessed signal volume"_s,
    };
    mp(ConvolutionReverbOn) = Args {
        .id = id(IdRegion::Master, 68), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::ConvolutionReverb},
        .name = "On"_s,
        .gui_label = "Convol Reverb"_s,
        .tooltip = "Enable/disable the convolution reverb effect"_s,
    };

    // =====================================================================================================
    mp(ReverbDecayTimeMs) = Args {
        .id = id(IdRegion::Master, 69), // never change
        .value_config = val_config_helpers::Ms({
            .projection = {{10, 60000}, 5},
            .default_ms = 1000,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Decay Time"_s,
        .gui_label = "Decay"_s,
        .tooltip = "Reverb decay time"_s,
        .related_params_group = 0,
    };

    mp(ReverbPreLowPassCutoff) = Args {
        .id = id(IdRegion::Master, 70), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 128}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Pre Low Cutoff"_s,
        .gui_label = "Pre LP"_s,
        .tooltip = "Low-pass filter cutoff before reverb"_s,
        .related_params_group = 2,
    };

    mp(ReverbPreHighPassCutoff) = Args {
        .id = id(IdRegion::Master, 71), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 0}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Pre High Cutoff"_s,
        .gui_label = "Pre HP"_s,
        .tooltip = "High-pass filter cutoff before reverb"_s,
        .related_params_group = 2,
    };

    mp(ReverbLowShelfCutoff) = Args {
        .id = id(IdRegion::Master, 72), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 128}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Low Cutoff"_s,
        .gui_label = "Lo-Shelf"_s,
        .tooltip = "Low-pass filter cutoff after reverb"_s,
        .related_params_group = 3,
    };

    auto const shelf_gain_value_config = ParamDescriptor::ConstructorArgs::ValueConfig {
        .linear_range = {0, 1},
        .projection = ParamDescriptor::Projection {.range = {-24, 0}, .exponent = 0.5f},
        .default_linear_value = 1,
        .display_format = ParamDisplayFormat::VolumeDbRange,
    };

    mp(ReverbLowShelfGain) = Args {
        .id = id(IdRegion::Master, 73), // never change
        .value_config = shelf_gain_value_config,
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Low Gain"_s,
        .gui_label = "Lo-Gain"_s,
        .tooltip = "Low-pass filter gain",
        .related_params_group = 3,
    };

    mp(ReverbHighShelfCutoff) = Args {
        .id = id(IdRegion::Master, 74), // never change
        .value_config = val_config_helpers::Semitones({.default_val = 128}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "High Cutoff"_s,
        .gui_label = "Hi-Shelf"_s,
        .tooltip = "High-pass filter cutoff after reverb"_s,
        .related_params_group = 4,
    };

    mp(ReverbHighShelfGain) = Args {
        .id = id(IdRegion::Master, 75), // never change
        .value_config = shelf_gain_value_config,
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "High Gain"_s,
        .gui_label = "Hi-Gain"_s,
        .tooltip = "High-pass filter gain",
        .related_params_group = 4,
    };

    mp(ReverbChorusAmount) = Args {
        .id = id(IdRegion::Master, 76), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Chorus Amount"_s,
        .gui_label = "Depth"_s,
        .tooltip = "Chorus effect amount"_s,
        .related_params_group = 1,
    };

    mp(ReverbChorusFrequency) = Args {
        .id = id(IdRegion::Master, 77), // never change
        .value_config = val_config_helpers::Hz({
            .projection = {.range = {0.003f, 2}, .exponent = 4.5f},
            .default_hz = 0.01f,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Chorus Frequency"_s,
        .gui_label = "Mod Rate"_s,
        .tooltip = "Chorus effect frequency"_s,
        .related_params_group = 1,
    };

    mp(ReverbSize) = Args {
        .id = id(IdRegion::Master, 78), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Size"_s,
        .gui_label = "Size"_s,
        .tooltip = "Reverb size"_s,
        .related_params_group = 0,
    };

    mp(ReverbDelay) = Args {
        .id = id(IdRegion::Master, 79), // never change
        .value_config = val_config_helpers::Ms({
            .projection = {{0, 1000}, 1.5f},
            .default_ms = 0,
        }),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Delay"_s,
        .gui_label = "Predelay"_s,
        .tooltip = "Reverb delay"_s,
        .related_params_group = 0,
    };

    mp(ReverbMix) = Args {
        .id = id(IdRegion::Master, 80), // never change
        .value_config = val_config_helpers::Percent({.default_percent = 50}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "Mix"_s,
        .gui_label = "Mix"_s,
        .tooltip = "Processed signal volume"_s,
        .related_params_group = 0,
    };

    mp(ReverbOn) = Args {
        .id = id(IdRegion::Master, 81), // never change
        .value_config = val_config_helpers::Bool({.default_state = false}),
        .modules = {ParameterModule::Effect, ParameterModule::Reverb},
        .name = "On"_s,
        .gui_label = "Reverb"_s,
        .tooltip = "Enable/disable the new reverb effect"_s,
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

        // =================================================================================================
        lp(Volume) = Args {
            .id = id(region, 0), // never change
            .value_config = val_config_helpers::Volume({.default_db = -6}),
            .modules = {layer_module},
            .name = "Volume"_s,
            .gui_label = "Volume"_s,
            .tooltip = "Layer volume"_s,
        };
        lp(Mute) = Args {
            .id = id(region, 1), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module},
            .name = "Mute"_s,
            .gui_label = "Mute"_s,
            .tooltip = "Mute this layer"_s,
        };
        lp(Solo) = Args {
            .id = id(region, 2), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module},
            .name = "Solo"_s,
            .gui_label = "Solo"_s,
            .tooltip = "Mute all other layers"_s,
        };
        lp(Pan) = Args {
            .id = id(region, 3), // never change
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Pan,
            }),
            .modules = {layer_module},
            .name = "Pan"_s,
            .gui_label = "Pan"_s,
            .tooltip = "Left/right balance"_s,
        };
        lp(TuneCents) = Args {
            .id = id(region, 4), // never change
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
            .tooltip = "Layer pitch in cents; hold shift for finer adjustment"_s,
        };
        lp(TuneSemitone) = Args {
            .id = id(region, 5), // never change
            .value_config = val_config_helpers::Int({.range = {-36, 36}, .default_val = 0}),
            .modules = {layer_module},
            .name = "Pitch Semitones"_s,
            .gui_label = "Pitch"_s,
            .tooltip = "Layer pitch in semitones"_s,
        };

        // =================================================================================================
        lp(LoopMode) = Args {
            .id = id(region, 49), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LoopMode,
                .default_val = (u32)LoopMode::InstrumentDefault,
            }),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "Loop Mode"_s,
            .gui_label = "Loop"_s,
            .tooltip = "The mode for looping the samples"_s,
        };
        lp(LoopStart) = Args {
            .id = id(region, 7), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "Start"_s,
            .gui_label = "Start"_s,
            .tooltip = "Loop-start"_s,
        };
        lp(LoopEnd) = Args {
            .id = id(region, 8), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 100}),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "End"_s,
            .gui_label = "End"_s,
            .tooltip = "Loop-end"_s,
        };
        lp(LoopCrossfade) = Args {
            .id = id(region, 9), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 1}),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "Crossfade Size"_s,
            .gui_label = "XFade"_s,
            .tooltip = "Crossfade length; this smooths the transition from the loop-end to the loop-start"_s,
        };
        lp(SampleOffset) = Args {
            .id = id(region, 11), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "Sample Start Offset"_s,
            .gui_label = "Start"_s,
            .tooltip = "Change the starting point of the sample"_s,
        };
        lp(Reverse) = Args {
            .id = id(region, 12), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Loop},
            .name = "Reverse On"_s,
            .gui_label = "Reverse"_s,
            .tooltip = "Play the sound in reverse"_s,
        };

        // =================================================================================================
        lp(VolEnvOn) = Args {
            .id = id(region, 13), // never change
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::VolEnv},
            .name = "On"_s,
            .gui_label = "Volume Envelope"_s,
            .tooltip =
                "Enable/disable the volume envelope; when disabled, each sound will play out entirely"_s,
        };
        lp(VolumeAttack) = Args {
            .id = id(region, 14), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 0}),
            .modules = {layer_module, ParameterModule::VolEnv},
            .name = "Attack"_s,
            .gui_label = "Attack"_s,
            .tooltip = "Volume fade-in length"_s,
        };
        lp(VolumeDecay) = Args {
            .id = id(region, 15), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 1000}),
            .modules = {layer_module, ParameterModule::VolEnv},
            .name = "Decay"_s,
            .gui_label = "Decay"_s,
            .tooltip = "Volume ramp-down length (after the attack)"_s,
        };
        lp(VolumeSustain) = Args {
            .id = id(region, 16), // never change
            .value_config = val_config_helpers::Sustain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::VolEnv},
            .name = "Sustain"_s,
            .gui_label = "Sustain"_s,
            .tooltip = "Volume level to sustain (after decay)"_s,
        };
        lp(VolumeRelease) = Args {
            .id = id(region, 17), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 800}),
            .modules = {layer_module, ParameterModule::VolEnv},
            .name = "Release"_s,
            .gui_label = "Release"_s,
            .tooltip = "Volume fade-out length (after the note is released)"_s,
        };

        // =================================================================================================
        lp(FilterOn) = Args {
            .id = id(region, 18), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "On"_s,
            .gui_label = "Filter"_s,
            .tooltip = "Enable/disable the filter"_s,
        };
        lp(FilterCutoff) = Args {
            .id = id(region, 19), // never change
            .value_config = val_config_helpers::Filter({.default_hz = 6000}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Cutoff Frequency"_s,
            .gui_label = "Cutoff"_s,
            .tooltip = "The frequency at which the filter should take effect"_s,
        };
        lp(FilterResonance) = Args {
            .id = id(region, 20), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 25}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = "The intensity of the volume peak at the cutoff frequency"_s,
        };
        lp(FilterType) = Args {
            .id = id(region, 21), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LayerFilterType,
                .default_val = (u32)LayerFilterType::Lowpass,
            }),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Filter type"_s,
        };
        lp(FilterEnvAmount) = Args {
            .id = id(region, 22), // never change
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Percent,
            }),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Envelope Amount"_s,
            .gui_label = "Envelope"_s,
            .tooltip = "How strongly the envelope should control the filter cutoff"_s,
        };
        lp(FilterAttack) = Args {
            .id = id(region, 23), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 0}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Attack"_s,
            .gui_label = "Attack"_s,
            .tooltip = "Length of initial ramp-up"_s,
        };
        lp(FilterDecay) = Args {
            .id = id(region, 24), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 1000}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Decay"_s,
            .gui_label = "Decay"_s,
            .tooltip = "Length ramp-down after attack"_s,
        };
        lp(FilterSustain) = Args {
            .id = id(region, 25), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 100}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Sustain"_s,
            .gui_label = "Sustain"_s,
            .tooltip = "Level to sustain after decay has completed"_s,
        };
        lp(FilterRelease) = Args {
            .id = id(region, 26), // never change
            .value_config = val_config_helpers::EnvelopeMs({.default_ms = 800}),
            .modules = {layer_module, ParameterModule::Filter},
            .name = "Release"_s,
            .gui_label = "Release"_s,
            .tooltip = "Length of ramp-down after note is released"_s,
        };

        // =================================================================================================
        lp(LfoOn) = Args {
            .id = id(region, 27), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "On"_s,
            .gui_label = "LFO"_s,
            .tooltip = "Enable/disable the Low Frequency Oscillator (LFO)"_s,
        };
        lp(LfoShape) = Args {
            .id = id(region, 28), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoShape,
                .default_val = (u32)LfoShape::Sine,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Shape"_s,
            .gui_label = "Shape"_s,
            .tooltip = "Oscillator shape"_s,
        };
        lp(LfoRestart) = Args {
            .id = id(region, 29), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoRestartMode,
                .default_val = (u32)LfoRestartMode::Retrigger,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Mode"_s,
            .gui_label = "Mode"_s,
            .tooltip =
                "Oscillator phase mode. Retrigger: each voice has its own phase, Free: all voices that are playing simultaneously will have the same phase"_s,
        };
        lp(LfoAmount) = Args {
            .id = id(region, 30), // never change
            .value_config = val_config_helpers::BidirectionalPercent({
                .default_percent = 0,
                .display_format = ParamDisplayFormat::Percent,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Amount"_s,
            .gui_label = "Amount"_s,
            .tooltip = "Intensity of the LFO effect"_s,
        };
        lp(LfoDestination) = Args {
            .id = id(region, 31), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoDestination,
                .default_val = (u32)LfoDestination::Volume,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Target"_s,
            .gui_label = "Target"_s,
            .tooltip = "The parameter that the LFO will modulate"_s,
        };
        lp(LfoRateTempoSynced) = Args {
            .id = id(region, 32), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::LfoSyncedRate,
                .default_val = (u32)LfoSyncedRate::_1_4,
            }),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Time (Tempo Synced)"_s,
            .gui_label = "Time"_s,
            .tooltip = "LFO rate (synced to the host)"_s,
        };
        lp(LfoRateHz) = Args {
            .id = id(region, 33), // never change
            .value_config = val_config_helpers::HzSlow({.default_hz = 5}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Time (Hz)"_s,
            .gui_label = "Time"_s,
            .tooltip = "LFO rate (in Hz)"_s,
        };
        lp(LfoSyncSwitch) = Args {
            .id = id(region, 34), // never change
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::Lfo},
            .name = "Sync On"_s,
            .gui_label = "Sync"_s,
            .tooltip = "Sync the LFO speed to the host"_s,
        };

        // =================================================================================================
        lp(EqOn) = Args {
            .id = id(region, 35), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Eq},
            .name = "On"_s,
            .gui_label = "EQ"_s,
            .tooltip = "Turn on or off the equaliser effect for this layer"_s,
        };
        lp(EqFreq1) = Args {
            .id = id(region, 36), // never change
            .value_config = val_config_helpers::Filter({.default_hz = 8000}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip = "Band 1: frequency of this band"_s,
        };
        lp(EqResonance1) = Args {
            .id = id(region, 37), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = "Band 1: sharpness of the peak"_s,
        };
        lp(EqGain1) = Args {
            .id = id(region, 38), // never change
            .value_config = val_config_helpers::Gain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Gain"_s,
            .gui_label = "Gain"_s,
            .tooltip = "Band 1: volume gain at the frequency"_s,
        };
        lp(EqType1) = Args {
            .id = id(region, 39), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::EqType,
                .default_val = (u32)EqType::Peak,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band1},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Band 1: type of EQ band"_s,
        };
        lp(EqFreq2) = Args {
            .id = id(region, 40), // never change
            .value_config = val_config_helpers::Filter({.default_hz = 500}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Frequency"_s,
            .gui_label = "Freq"_s,
            .tooltip = "Band 2: frequency of this band"_s,
        };
        lp(EqResonance2) = Args {
            .id = id(region, 41), // never change
            .value_config = val_config_helpers::Percent({.default_percent = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Resonance"_s,
            .gui_label = "Reso"_s,
            .tooltip = "Band 2: sharpness of the peak"_s,
        };
        lp(EqGain2) = Args {
            .id = id(region, 42), // never change
            .value_config = val_config_helpers::Gain({.default_db = 0}),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Gain"_s,
            .gui_label = "Gain"_s,
            .tooltip = "Band 2: volume gain at the frequency"_s,
        };
        lp(EqType2) = Args {
            .id = id(region, 43), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::EqType,
                .default_val = (u32)EqType::Peak,
            }),
            .modules = {layer_module, ParameterModule::Eq, ParameterModule::Band2},
            .name = "Type"_s,
            .gui_label = "Type"_s,
            .tooltip = "Band 2: type of EQ band"_s,
        };

        // =================================================================================================
        lp(VelocityMapping) = Args {
            .id = id(region, 44), // never change
            .value_config = val_config_helpers::Menu({
                .type = ParamDescriptor::MenuType::VelocityMappingMode,
                .default_val = (u32)VelocityMappingMode::None,
            }),
            .modules = {layer_module, ParameterModule::Playback},
            .name = "Velocity Mapping"_s,
            .gui_label = "Velocity Mapping"_s,
            .tooltip =
                "Choose how MIDI velocity should affect the volume of this layer. There are 6 modes that can be selected for this parameter via the buttons on the GUI. By setting one layer to be quiet at high velocities and another layer to be quiet at low velocities you can create an instrument that sounds different based on how hard the notes are played. (0) Ignore velocity, always play full volume. (1) Loudest at high velocity, quietist at low velocity (2) Loudest at low velocity, quietist at high velocity (3) Loudest at high velocity, quietist at middle velocity and below (4) Loudest at middle velocity, quietist at both high and low velocities (5) Loudest at bottom velocity, quietist at middle velocity and above,"_s,
            .flags = {.hidden = true},
        };
        lp(Keytrack) = Args {
            .id = id(region, 45), // never change
            .value_config = val_config_helpers::Bool({.default_state = true}),
            .modules = {layer_module, ParameterModule::Playback},
            .name = "Keytrack On"_s,
            .gui_label = "Keytrack"_s,
            .tooltip =
                "Tune the sound to match the key played; if disabled it will always play the sound at its root pitch"_s,
        };
        lp(Monophonic) = Args {
            .id = id(region, 46), // never change
            .value_config = val_config_helpers::Bool({.default_state = false}),
            .modules = {layer_module, ParameterModule::Playback},
            .name = "Monophonic On"_s,
            .gui_label = "Monophonic"_s,
            .tooltip = "Only allow one voice of each sound to play at a time"_s,
        };
        lp(MidiTranspose) = Args {
            .id = id(region, 48), // never change
            .value_config = val_config_helpers::Int({.range = {-36, 36}, .default_val = 0}),
            .modules = {layer_module, ParameterModule::Playback},
            .name = "MIDI Transpose On"_s,
            .gui_label = "Transpose"_s,
            .tooltip =
                "Transpose the mapping of samples by the given semitone offset, meaning a higher/lower sample may be triggered instead of stretching/shrinking the audio by large amounts (only useful if the instrument is multi-sampled)"_s,
        };
    }

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
    }

    return result;
}

constexpr auto k_create_params_result = CreateParams();

constexpr auto k_param_descriptors = k_create_params_result.params;
constexpr auto k_id_map = k_create_params_result.id_map;

struct ComptimeParamSearchOptions {
    ParamModules modules {};
    Optional<ParamIndex> skip {};
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

Span<String const> ParameterMenuItems(ParamIndex param_index);

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

enum class NoLongerExistingParam : u16 {
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

enum class ParamExistance {
    StillExists,
    NoLongerExists,
};

using LegacyParam = TaggedUnion<ParamExistance,
                                TypeAndTag<ParamIndex, ParamExistance::StillExists>,
                                TypeAndTag<NoLongerExistingParam, ParamExistance::NoLongerExists>>;

Optional<LegacyParam> ParamFromLegacyId(String id);
Optional<DynamicArrayBounded<char, 64>> ParamToLegacyId(LegacyParam index);
