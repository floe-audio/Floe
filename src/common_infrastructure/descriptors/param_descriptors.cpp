// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "param_descriptors.hpp"

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"

#include "audio_utils.hpp"

Span<String const> ParameterMenuItems(ParamIndex param_index) {
    auto const& param = k_param_descriptors[ToInt(param_index)];
    ASSERT_EQ(param.value_type, ParamValueType::Menu);
    return MenuItems(param.menu_type);
}

Optional<f32> ParamDescriptor::StringToLinearValue(String str) const {
    str = WhitespaceStripped(str);

    switch (display_format) {
        case ParamDisplayFormat::None: {
            switch (value_type) {
                case ParamValueType::Float: {
                    break;
                }
                case ParamValueType::Menu: {
                    auto const items = ParameterMenuItems(ParamIdToIndex(id).Value());
                    for (auto [menu_index, menu_item] : Enumerate(items))
                        if (IsEqualToCaseInsensitiveAscii(str, menu_item)) return (f32)menu_index;
                    break;
                }
                case ParamValueType::Bool: {
                    if (IsEqualToCaseInsensitiveAscii(str, "on"_s) || str == "1"_s) return 1.0f;
                    if (IsEqualToCaseInsensitiveAscii(str, "off"_s) || str == "0"_s) return 0.0f;
                    break;
                }
                case ParamValueType::Int: {
                    break;
                }
            }
            break;
        }
        case ParamDisplayFormat::Percent: {
            if (auto const opt_value = ParseFloat(str)) return LineariseValue((f32)*opt_value / 100.0f, true);
            break;
        }
        case ParamDisplayFormat::Pan: {
            usize num_chars_read = 0;
            if (auto const opt_value = ParseFloat(str, &num_chars_read)) {
                auto const val = opt_value.Value();
                auto const suffix = WhitespaceStripped(str.SubSpan(num_chars_read));
                bool right = true;
                if (StartsWithCaseInsensitiveAscii(suffix, "L"_s)) right = false;

                if (right)
                    return LineariseValue((f32)val / 100.0f, true);
                else
                    return LineariseValue(-(f32)val / 100.0f, true);
            }
            break;
        }
        case ParamDisplayFormat::SinevibesFilter: {
            if (IsEqualToCaseInsensitiveAscii(str, "off"_s)) return 0.0f;

            auto const lo_prefix = "lo-cut"_s;
            auto const hi_prefix = "hi-cut"_s;
            if (StartsWithCaseInsensitiveAscii(str, lo_prefix)) {
                str.RemovePrefix(lo_prefix.size);
                if (auto const opt_value = ParseFloat(str))
                    return LineariseValue(-(f32)*opt_value / 100.0f, true);
            } else if (StartsWithCaseInsensitiveAscii(str, hi_prefix)) {
                str.RemovePrefix(hi_prefix.size);
                if (auto const opt_value = ParseFloat(str))
                    return LineariseValue((f32)*opt_value / 100.0f, true);
            }
            break;
        }
        case ParamDisplayFormat::Ms: {
            usize num_chars_read = 0;
            if (auto const opt_value = ParseFloat(str, &num_chars_read)) {
                auto suffix = WhitespaceStripped(str.SubSpan(num_chars_read));

                auto value = (f32)*opt_value;
                if (StartsWithCaseInsensitiveAscii(suffix, "s"_s)) value *= 1000.0f;

                return LineariseValue(value, true);
            }
            break;
        }
        case ParamDisplayFormat::VolumeAmp: {
            if (str == "-\u221E"_s) return 0.0f;
            if (auto const opt_value = ParseFloat(str)) {
                auto const amp = DbToAmp((f32)*opt_value);
                return LineariseValue(amp, true);
            }
            break;
        }
        case ParamDisplayFormat::Hz: {
            usize num_chars_read = 0;
            if (auto const opt_value = ParseFloat(str, &num_chars_read)) {
                auto suffix = WhitespaceStripped(str.SubSpan(num_chars_read));

                auto value = (f32)*opt_value;
                if (StartsWithCaseInsensitiveAscii(suffix, "k"_s)) value *= 1000.0f;

                return LineariseValue(value, true);
            }
            break;
        }
        case ParamDisplayFormat::VolumeDbRange: {
            if (auto const opt_value = ParseFloat(str)) {
                auto const val = opt_value.Value();
                return LineariseValue((f32)val, true);
            }
            break;
        }
        case ParamDisplayFormat::Cents: {
            break;
        }
        case ParamDisplayFormat::Semitones: {
            break;
        }
    }

    if (auto const opt_value = ParseFloat(str)) return LineariseValue((f32)*opt_value, true);
    return {};
}

static bool NumberStartsWithNegativeZero(String str) {
    if (str[0] == '-') {
        usize zero_end_index = 1;
        for (auto const i : ::Range(1uz, str.size))
            if (str[i] == '0' || str[i] == '.')
                zero_end_index++;
            else
                break;
        if (zero_end_index == str.size || str[zero_end_index] == ' ') return true;
    }
    return false;
}

TEST_CASE(TestNumberStartsWithNegativeZero) {
    CHECK(NumberStartsWithNegativeZero("-0"_s));
    CHECK(NumberStartsWithNegativeZero("-0.0"_s));
    CHECK(NumberStartsWithNegativeZero("-0.000"_s));
    CHECK(NumberStartsWithNegativeZero("-0.000 "_s));
    CHECK(NumberStartsWithNegativeZero("-0.000  "_s));
    CHECK(NumberStartsWithNegativeZero("-0.000 1"_s));
    CHECK(!NumberStartsWithNegativeZero("-0.0001"_s));
    CHECK(!NumberStartsWithNegativeZero("-0.0001 "_s));
    CHECK(!NumberStartsWithNegativeZero("-0.0001  "_s));
    return k_success;
}

Optional<DynamicArrayBounded<char, 128>> ParamDescriptor::LinearValueToString(f32 linear_value) const {
    constexpr usize k_size = 128;
    using ResultType = DynamicArrayBounded<char, k_size>;
    ResultType result;
    auto const value = ProjectValue(linear_value);

    switch (display_format) {
        case ParamDisplayFormat::None: {
            switch (value_type) {
                case ParamValueType::Float: {
                    result = fmt::FormatInline<k_size>("{.1}", value);
                    break;
                }
                case ParamValueType::Menu: {
                    result = ParameterMenuItems(
                        ParamIdToIndex(id).Value())[CheckedCast<u32>(ParamToInt<u32>(linear_value))];
                    break;
                }
                case ParamValueType::Bool: {
                    result = ResultType {(value >= 0.5f) ? "On"_s : "Off"_s};
                    break;
                }
                case ParamValueType::Int: {
                    result = fmt::FormatInline<k_size>("{}", ParamToInt<int>(linear_value));
                    break;
                }
            }
            break;
        }
        case ParamDisplayFormat::Percent: {
            result = fmt::FormatInline<k_size>("{.0}%", value * 100.0f);
            break;
        }
        case ParamDisplayFormat::Pan: {
            auto const scaled_value = value * 100.0f;
            if (scaled_value > -0.5f && scaled_value < 0.5f)
                result = ResultType("0");
            else if (scaled_value < 0)
                result = fmt::FormatInline<k_size>("{.0} L", -scaled_value);
            else
                result = fmt::FormatInline<k_size>("{.0} R", scaled_value);
            break;
        }
        case ParamDisplayFormat::SinevibesFilter: {
            auto const scaled_value = value * 100.0f;
            if (scaled_value > -0.5f && scaled_value < 0.5f)
                result = ResultType {"Off"};
            else if (scaled_value < 0)
                result = fmt::FormatInline<k_size>("Lo-cut {.0}%", -scaled_value);
            else
                result = fmt::FormatInline<k_size>("Hi-cut {.0}%", scaled_value);
            break;
        }
        case ParamDisplayFormat::Ms: {
            if (RoundPositiveFloat(value) >= 1000)
                result = fmt::FormatInline<k_size>("{.1} s", value / 1000);
            else if (value < 10)
                result = fmt::FormatInline<k_size>("{.2} ms", value);
            else
                result = fmt::FormatInline<k_size>("{.0} ms", value);
            break;
        }
        case ParamDisplayFormat::VolumeAmp: {
            if (value > k_silence_amp_80) {
                result = fmt::FormatInline<k_size>("{.1} dB", AmpToDb(value));
                break;
            } else
                result = ResultType("-\u221E");
            break;
        }
        case ParamDisplayFormat::Hz: {
            if (RoundPositiveFloat(value) >= 1000)
                result = fmt::FormatInline<k_size>("{.1} kHz", value / 1000);
            else if (projection->range.Delta() > 100)
                result = fmt::FormatInline<k_size>("{.0} Hz", value);
            else if (projection->range.min < 0.01f)
                result = fmt::FormatInline<k_size>("{.3} Hz", value);
            else
                result = fmt::FormatInline<k_size>("{.1} Hz", value);
            break;
        }
        case ParamDisplayFormat::VolumeDbRange: {
            result = fmt::FormatInline<k_size>("{.1} dB", value);
            break;
        }
        case ParamDisplayFormat::Cents: {
            result = fmt::FormatInline<k_size>("{.0} cents", value);
            break;
        }
        case ParamDisplayFormat::Semitones: {
            result = fmt::FormatInline<k_size>("{.0} semitones", value);
            break;
        }
    }

    if (!result.size) result = fmt::FormatInline<k_size>("{.1}", value);

    if (NumberStartsWithNegativeZero(result)) dyn::Remove(result, 0);

    return result;
}

String ParamMenuText(ParamIndex index, f32 value) {
    auto const menu_items = ParameterMenuItems(index);
    ASSERT(menu_items.size);
    auto text_index = ParamToInt<int>(value);
    ASSERT(text_index >= 0 && text_index < (int)menu_items.size);
    return menu_items[(usize)text_index];
}

namespace legacy_params {

namespace still_exists {

struct LayerParamId {
    String id_suffix;
    LayerParamIndex index;
};

// The legacy layer parameter were prefixed with L0, L1, L2, etc., where the number is the layer index. In
// this array we just store the suffixes. The prefix is programmatically handled when needed.
constexpr auto k_layer_params = ArrayT<LayerParamId>({
    {"Vol", LayerParamIndex::Volume},
    {"Mute", LayerParamIndex::Mute},
    {"Solo", LayerParamIndex::Solo},
    {"Pan", LayerParamIndex::Pan},
    {"Detune", LayerParamIndex::TuneCents},
    {"Pitch", LayerParamIndex::TuneSemitone},
    {"LpStrt", LayerParamIndex::LoopStart},
    {"LpEnd", LayerParamIndex::LoopEnd},
    {"LpXf", LayerParamIndex::LoopCrossfade},
    {"Offs", LayerParamIndex::SampleOffset},
    {"Rev", LayerParamIndex::Reverse},
    {"VlEnOn", LayerParamIndex::VolEnvOn},
    {"Att", LayerParamIndex::VolumeAttack},
    {"Dec", LayerParamIndex::VolumeDecay},
    {"Sus", LayerParamIndex::VolumeSustain},
    {"Rel", LayerParamIndex::VolumeRelease},
    {"FlOn", LayerParamIndex::FilterOn},
    {"FlCut", LayerParamIndex::FilterCutoff},
    {"FfRes", LayerParamIndex::FilterResonance},
    {"FlTy", LayerParamIndex::FilterType},
    {"FlAm", LayerParamIndex::FilterEnvAmount},
    {"FlAtt", LayerParamIndex::FilterAttack},
    {"FLDec", LayerParamIndex::FilterDecay},
    {"FlSus", LayerParamIndex::FilterSustain},
    {"FlRel", LayerParamIndex::FilterRelease},
    {"LfoOn", LayerParamIndex::LfoOn},
    {"LfoSh", LayerParamIndex::LfoShape},
    {"LfoMd", LayerParamIndex::LfoRestart},
    {"LfoAm", LayerParamIndex::LfoAmount},
    {"LfoTg", LayerParamIndex::LfoDestination},
    {"LfoSyt", LayerParamIndex::LfoRateTempoSynced},
    {"LfoHZ", LayerParamIndex::LfoRateHz},
    {"LfoSyO", LayerParamIndex::LfoSyncSwitch},
    {"EqOn", LayerParamIndex::EqOn},
    {"EqFr0", LayerParamIndex::EqFreq1},
    {"EqRs0", LayerParamIndex::EqResonance1},
    {"EqGn0", LayerParamIndex::EqGain1},
    {"EqTy0", LayerParamIndex::EqType1},
    {"EqFr1", LayerParamIndex::EqFreq2},
    {"EqRs1", LayerParamIndex::EqResonance2},
    {"EqGn1", LayerParamIndex::EqGain2},
    {"EqTy1", LayerParamIndex::EqType2},
    {"Vel", LayerParamIndex::VelocityMapping},
    {"KTr", LayerParamIndex::Keytrack},
    {"Mono", LayerParamIndex::Monophonic},
    {"Trn", LayerParamIndex::MidiTranspose},
});

struct NonLayerParamId {
    String id;
    ParamIndex index;
};

constexpr auto k_non_layer_params = ArrayT<NonLayerParamId>({
    {"MastVol", ParamIndex::MasterVolume},
    {"MastVel", ParamIndex::MasterVelocity},
    {"MastDyn", ParamIndex::MasterTimbre},
    {"DistType", ParamIndex::DistortionType},
    {"DistDrive", ParamIndex::DistortionDrive},
    {"DistOn", ParamIndex::DistortionOn},
    {"BitcBits", ParamIndex::BitCrushBits},
    {"BitcRate", ParamIndex::BitCrushBitRate},
    {"BitcWet", ParamIndex::BitCrushWet},
    {"BitcDry", ParamIndex::BitCrushDry},
    {"BitcOn", ParamIndex::BitCrushOn},
    {"CompThr", ParamIndex::CompressorThreshold},
    {"CompRt", ParamIndex::CompressorRatio},
    {"CompGain", ParamIndex::CompressorGain},
    {"CompAuto", ParamIndex::CompressorAutoGain},
    {"CompOn", ParamIndex::CompressorOn},
    {"FlOn", ParamIndex::FilterOn},
    {"FlCut", ParamIndex::FilterCutoff},
    {"FlRes", ParamIndex::FilterResonance},
    {"FlGain", ParamIndex::FilterGain},
    {"FlType", ParamIndex::FilterType},
    {"SterWd", ParamIndex::StereoWidenWidth},
    {"SterOn", ParamIndex::StereoWidenOn},
    {"ChorRate", ParamIndex::ChorusRate},
    {"ChorHP", ParamIndex::ChorusHighpass},
    {"ChorDpth", ParamIndex::ChorusDepth},
    {"ChorWet", ParamIndex::ChorusWet},
    {"ChorDry", ParamIndex::ChorusDry},
    {"ChorOn", ParamIndex::ChorusOn},
    {"ConvHP", ParamIndex::ConvolutionReverbHighpass},
    {"ConvWet", ParamIndex::ConvolutionReverbWet},
    {"ConvDry", ParamIndex::ConvolutionReverbDry},
    {"ConvOn", ParamIndex::ConvolutionReverbOn},
});

} // namespace still_exists

namespace no_longer_exists {

struct NoLongerExistsParam {
    String id;
    NoLongerExistingParam index;
};

constexpr auto k_params = ArrayT<NoLongerExistsParam>({
    {"L0LpOn", NoLongerExistingParam::Layer1LoopOnSwitch},
    {"L0LpPP", NoLongerExistingParam::Layer1LoopPingPongOnSwitch},
    {"L1LpOn", NoLongerExistingParam::Layer2LoopOnSwitch},
    {"L1LpPP", NoLongerExistingParam::Layer2LoopPingPongOnSwitch},
    {"L2LpOn", NoLongerExistingParam::Layer3LoopOnSwitch},
    {"L2LpPP", NoLongerExistingParam::Layer3LoopPingPongOnSwitch},
    {"ConvIR", NoLongerExistingParam::ConvolutionLegacyMirageIrName},
    {"RvDamp", NoLongerExistingParam::ReverbFreeverbDampingPercent},
    {"RvWidth", NoLongerExistingParam::ReverbFreeverbWidthPercent},
    {"RvWet", NoLongerExistingParam::ReverbFreeverbWetPercent},
    {"RvDry", NoLongerExistingParam::ReverbDryDb},
    {"RvSize", NoLongerExistingParam::ReverbSizePercent},
    {"RvOn", NoLongerExistingParam::ReverbOnSwitch},
    {"RvLeg", NoLongerExistingParam::ReverbUseFreeverbSwitch},
    {"SvRvPre", NoLongerExistingParam::ReverbSvPreDelayMs},
    {"SvRvMs", NoLongerExistingParam::ReverbSvModFreqHz},
    {"SvRvMd", NoLongerExistingParam::ReverbSvModDepthPercent},
    {"SvRvDm", NoLongerExistingParam::ReverbSvFilterBidirectionalPercent},
    {"SvRvWet", NoLongerExistingParam::ReverbSvWetDb},
    {"SvPhFr", NoLongerExistingParam::SvPhaserFreqHz},
    {"SvPhMf", NoLongerExistingParam::SvPhaserModFreqHz},
    {"SvPhMd", NoLongerExistingParam::SvPhaserModDepth},
    {"SvPhFd", NoLongerExistingParam::SvPhaserFeedback},
    {"SvPhSg", NoLongerExistingParam::SvPhaserNumStages},
    {"SvPhSt", NoLongerExistingParam::SvPhaserModStereo},
    {"SvPhWet", NoLongerExistingParam::SvPhaserWet},
    {"SvPhDry", NoLongerExistingParam::SvPhaserDry},
    {"SvPhOn", NoLongerExistingParam::SvPhaserOn},
    {"DlMsL", NoLongerExistingParam::DelayOldDelayTimeLMs},
    {"DlMsR", NoLongerExistingParam::DelayOldDelayTimeRMs},
    {"DlDamp", NoLongerExistingParam::DelayOldDamping},
    {"DlSyncL", NoLongerExistingParam::DelayTimeSyncedL},
    {"DlSyncR", NoLongerExistingParam::DelayTimeSyncedR},
    {"DlFeed", NoLongerExistingParam::DelayFeedback},
    {"DlSyncOn", NoLongerExistingParam::DelayTimeSyncSwitch},
    {"DlWet", NoLongerExistingParam::DelayWet},
    {"DlOn", NoLongerExistingParam::DelayOn},
    {"DlLeg", NoLongerExistingParam::DelayLegacyAlgorithm},
    {"SvDlMode", NoLongerExistingParam::DelaySinevibesMode},
    {"SvDlMsL", NoLongerExistingParam::DelaySinevibesDelayTimeLMs},
    {"SvDlMsR", NoLongerExistingParam::DelaySinevibesDelayTimeRMs},
    {"SvDlFl", NoLongerExistingParam::DelaySinevibesFilter},
});

} // namespace no_longer_exists

} // namespace legacy_params

Optional<DynamicArrayBounded<char, 64>> ParamToLegacyId(LegacyParam index) {
    switch (index.tag) {
        case ParamExistance::StillExists: {
            auto const i = index.Get<ParamIndex>();
            if (auto const layer_param_desc = LayerParamIndexAndLayerFor(i)) {
                for (auto const& legacy : legacy_params::still_exists::k_layer_params) {
                    if (layer_param_desc->param == legacy.index) {
                        auto result = DynamicArrayBounded<char, 64> {};
                        dyn::Append(result, 'L');
                        dyn::Append(result, (char)('0' + layer_param_desc->layer_num));
                        dyn::AppendSpan(result, legacy.id_suffix);
                        return result;
                    }
                }
            } else {
                for (auto const& legacy : legacy_params::still_exists::k_non_layer_params)
                    if (index == legacy.index) return legacy.id;
            }
            break;
        }
        case ParamExistance::NoLongerExists: {
            for (auto const& legacy : legacy_params::no_longer_exists::k_params)
                if (index == legacy.index) return legacy.id;
            break;
        }
    }

    return k_nullopt;
}

Optional<LegacyParam> ParamFromLegacyId(String id) {
    for (auto const i : Range(3u)) {
        if (StartsWithSpan(id, Array {'L', (char)('0' + i)})) {
            String const str = id.SubSpan(2);
            for (auto const& p : legacy_params::still_exists::k_layer_params)
                if (p.id_suffix == str) return ParamIndexFromLayerParamIndex(i, p.index);
        }
    }

    for (auto const& p : legacy_params::still_exists::k_non_layer_params)
        if (p.id == id) return p.index;

    for (auto const& p : legacy_params::no_longer_exists::k_params)
        if (p.id == id) return p.index;

    return k_nullopt;
}

TEST_CASE(TestParamStringConversion) {
    {
        auto const& attack_param =
            k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(0, LayerParamIndex::VolumeAttack))];
        tester.log.Debug("Attack param id: {}", attack_param.id);
        auto const str = attack_param.LinearValueToString(0.4708353049341293f);
        REQUIRE(str);
        auto const val = attack_param.StringToLinearValue(*str);
        REQUIRE(val);
        auto const str2 = attack_param.LinearValueToString(*val);
        tester.log.Debug("Attack param str: {}, value: {}, str2: {}", *str, *val, *str2);
    }
    {
        auto const& detune_param =
            k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(0, LayerParamIndex::TuneCents))];
        tester.log.Debug("Detune param id: {}", detune_param.id);
        auto const str = detune_param.LinearValueToString(-0.010595884688319623f);
        REQUIRE(str);
        auto const val = detune_param.StringToLinearValue(*str);
        REQUIRE(val);
        auto const str2 = detune_param.LinearValueToString(*val);
        tester.log.Debug("Detune param str: {}, value: {}, str2: {}", *str, *val, *str2);
    }
    return k_success;
}

TEST_CASE(TestLegacyConversion) {
    auto const i = ParamFromLegacyId("L0Vol");
    REQUIRE(i.HasValue());
    REQUIRE(i.Value().tag == ParamExistance::StillExists);
    auto const p = i.Value().Get<ParamIndex>();
    CHECK(p == ParamIndexFromLayerParamIndex(0, LayerParamIndex::Volume));
    auto const b = ParamToLegacyId(p);
    REQUIRE(b);
    CHECK(*b == "L0Vol"_s);
    return k_success;
}

TEST_REGISTRATION(RegisterParamDescriptorTests) {
    REGISTER_TEST(TestNumberStartsWithNegativeZero);
    REGISTER_TEST(TestLegacyConversion);
    REGISTER_TEST(TestParamStringConversion);
}
