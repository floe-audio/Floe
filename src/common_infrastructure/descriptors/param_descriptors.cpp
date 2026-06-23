// Copyright 2018-2026 Sam Windell
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
        case ParamDisplayFormat::Percent:
        case ParamDisplayFormat::Percent2dp: {
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
        case ParamDisplayFormat::Ratio: {
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
            result = fmt::FormatInline<k_size>("{.1}%", value * 100.0f);
            break;
        }
        case ParamDisplayFormat::Percent2dp: {
            result = fmt::FormatInline<k_size>("{.2}%", value * 100.0f);
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
            else if (value < 0.5f)
                result = fmt::FormatInline<k_size>("{.2} Hz", value);
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
        case ParamDisplayFormat::Ratio: {
            result = fmt::FormatInline<k_size>("{.2} : 1", value);
            break;
        }
    }

    if (!result.size) result = fmt::FormatInline<k_size>("{.1}", value);

    if (NumberStartsWithNegativeZero(result)) dyn::Remove(result, 0);

    return result;
}

bool IsParamCurrentlyRelevant(ParamIndex index, StaticSpan<f32 const, k_num_parameters> linear_param_values) {
    auto const linear = [&](ParamIndex i) { return linear_param_values[ToInt(i)]; };
    auto const layer_linear = [&](u32 layer, LayerParamIndex p) {
        return linear_param_values[ToInt(ParamIndexFromLayerParamIndex(layer, p))];
    };
    auto const is_on = [&](ParamIndex i) { return ParamToBool(linear(i)); };
    auto const layer_is_on = [&](u32 layer, LayerParamIndex p) {
        return ParamToBool(layer_linear(layer, p));
    };

    if (k_param_descriptors[ToInt(index)].flags.legacy) return false;

    if (auto const layer_info = LayerParamIndexAndLayerFor(index)) {
        auto const lp = layer_info->param;
        auto const ln = layer_info->layer_num;

        auto const play_mode =
            ParamToInt<param_values::PlayMode>(layer_linear(ln, LayerParamIndex::PlayMode));
        auto const is_granular = play_mode == param_values::PlayMode::GranularPlayback ||
                                 play_mode == param_values::PlayMode::GranularFixed;

        switch (lp) {
            case LayerParamIndex::Volume:
            case LayerParamIndex::Mute:
            case LayerParamIndex::Solo:
            case LayerParamIndex::Pan:
            case LayerParamIndex::StereoWidth:
            case LayerParamIndex::TuneCents:
            case LayerParamIndex::TuneSemitone:
            case LayerParamIndex::Reverse:
            case LayerParamIndex::VolEnvOn:
            case LayerParamIndex::FilterOn:
            case LayerParamIndex::LfoOn:
            case LayerParamIndex::EqOn:
            case LayerParamIndex::Keytrack:
            case LayerParamIndex::MonophonicMode:
            case LayerParamIndex::MidiTranspose:
            case LayerParamIndex::PitchBendRange:
            case LayerParamIndex::KeyRangeLow:
            case LayerParamIndex::KeyRangeHigh:
            case LayerParamIndex::KeyRangeLowFade:
            case LayerParamIndex::KeyRangeHighFade:
            case LayerParamIndex::PlayMode:
            case LayerParamIndex::ArpOn: return true;

            case LayerParamIndex::LoopMode:
            case LayerParamIndex::SampleOffset: return play_mode != param_values::PlayMode::GranularFixed;

            case LayerParamIndex::LoopStart:
            case LayerParamIndex::LoopEnd:
            case LayerParamIndex::LoopCrossfade: {
                if (play_mode == param_values::PlayMode::GranularFixed) return false;
                auto const loop_mode =
                    ParamToInt<param_values::LoopMode>(layer_linear(ln, LayerParamIndex::LoopMode));
                return loop_mode != param_values::LoopMode::None;
            }

            case LayerParamIndex::VolumeAttack:
            case LayerParamIndex::VolumeDecay:
            case LayerParamIndex::VolumeSustain:
            case LayerParamIndex::VolumeRelease: return layer_is_on(ln, LayerParamIndex::VolEnvOn);

            case LayerParamIndex::FilterCutoff:
            case LayerParamIndex::FilterResonance:
            case LayerParamIndex::FilterType:
            case LayerParamIndex::FilterEnvAmount: return layer_is_on(ln, LayerParamIndex::FilterOn);

            case LayerParamIndex::FilterAttack:
            case LayerParamIndex::FilterDecay:
            case LayerParamIndex::FilterSustain:
            case LayerParamIndex::FilterRelease:
                return layer_is_on(ln, LayerParamIndex::FilterOn) &&
                       layer_linear(ln, LayerParamIndex::FilterEnvAmount) != 0;

            case LayerParamIndex::LfoRestart:
            case LayerParamIndex::LfoAmount:
            case LayerParamIndex::LfoSyncSwitch:
            case LayerParamIndex::LfoShape:
            case LayerParamIndex::LfoDestination: return layer_is_on(ln, LayerParamIndex::LfoOn);

            case LayerParamIndex::LfoRateTempoSynced:
                return layer_is_on(ln, LayerParamIndex::LfoOn) &&
                       layer_is_on(ln, LayerParamIndex::LfoSyncSwitch);
            case LayerParamIndex::LfoRateHz:
                return layer_is_on(ln, LayerParamIndex::LfoOn) &&
                       !layer_is_on(ln, LayerParamIndex::LfoSyncSwitch);

            case LayerParamIndex::EqFreq1:
            case LayerParamIndex::EqResonance1:
            case LayerParamIndex::EqType1:
            case LayerParamIndex::EqFreq2:
            case LayerParamIndex::EqResonance2:
            case LayerParamIndex::EqType2:
            case LayerParamIndex::EqFreq3:
            case LayerParamIndex::EqResonance3:
            case LayerParamIndex::EqType3: return layer_is_on(ln, LayerParamIndex::EqOn);

            case LayerParamIndex::EqGain1:
                return layer_is_on(ln, LayerParamIndex::EqOn) &&
                       param_values::EqTypeUsesGain(
                           ParamToInt<param_values::EqType>(layer_linear(ln, LayerParamIndex::EqType1)));
            case LayerParamIndex::EqGain2:
                return layer_is_on(ln, LayerParamIndex::EqOn) &&
                       param_values::EqTypeUsesGain(
                           ParamToInt<param_values::EqType>(layer_linear(ln, LayerParamIndex::EqType2)));
            case LayerParamIndex::EqGain3:
                return layer_is_on(ln, LayerParamIndex::EqOn) &&
                       param_values::EqTypeUsesGain(
                           ParamToInt<param_values::EqType>(layer_linear(ln, LayerParamIndex::EqType3)));

            case LayerParamIndex::GranularSpeed: return play_mode == param_values::PlayMode::GranularPlayback;
            case LayerParamIndex::GranularPosition: return play_mode == param_values::PlayMode::GranularFixed;
            case LayerParamIndex::GranularDensity:
            case LayerParamIndex::GranularLength:
            case LayerParamIndex::GranularSpread:
            case LayerParamIndex::GranularSmoothing:
            case LayerParamIndex::GranularRandomPan:
            case LayerParamIndex::GranularRandomDetune:
            case LayerParamIndex::GranularRandomDirection:
            case LayerParamIndex::GranularHarmony: return is_granular;

            case LayerParamIndex::ArpMode:
            case LayerParamIndex::ArpNoteOrder:
            case LayerParamIndex::ArpTriggerMode:
            case LayerParamIndex::ArpRate:
            case LayerParamIndex::ArpAutoRate:
            case LayerParamIndex::ArpLength:
            case LayerParamIndex::ArpHumanise:
            case LayerParamIndex::ArpOctavePolyrate:
            case LayerParamIndex::ArpOneShot: return layer_is_on(ln, LayerParamIndex::ArpOn);

            case LayerParamIndex::LegacyFilterCutoff:
            case LayerParamIndex::LegacyFilterResonance:
            case LayerParamIndex::LegacyFilterType:
            case LayerParamIndex::LegacyLfoShape:
            case LayerParamIndex::LegacyLfoDestination:
            case LayerParamIndex::LegacyLfoShapeV2:
            case LayerParamIndex::LegacyEqFreq1:
            case LayerParamIndex::LegacyEqResonance1:
            case LayerParamIndex::LegacyEqType1:
            case LayerParamIndex::LegacyEqFreq2:
            case LayerParamIndex::LegacyEqResonance2:
            case LayerParamIndex::LegacyEqType2:
            case LayerParamIndex::LegacyEqFreq3:
            case LayerParamIndex::LegacyVelocityMapping:
            case LayerParamIndex::LegacyMonophonicBool: return false;

            case LayerParamIndex::Count: PanicIfReached();
        }
        PanicIfReached();
    }

    switch (index) {
        case ParamIndex::MasterVolume:
        case ParamIndex::MasterTimbre:
        case ParamIndex::Macro1:
        case ParamIndex::Macro2:
        case ParamIndex::Macro3:
        case ParamIndex::Macro4:
        case ParamIndex::DistortionOn:
        case ParamIndex::BitCrushOn:
        case ParamIndex::CompressorOn:
        case ParamIndex::FilterOn:
        case ParamIndex::StereoWidenOn:
        case ParamIndex::ChorusOn:
        case ParamIndex::DelayOn:
        case ParamIndex::PhaserOn:
        case ParamIndex::EqOn:
        case ParamIndex::ConvolutionReverbOn:
        case ParamIndex::ReverbOn: return true;

        case ParamIndex::DistortionType:
        case ParamIndex::DistortionDrive:
        case ParamIndex::DistortionMix: return is_on(ParamIndex::DistortionOn);

        case ParamIndex::BitCrushBits:
        case ParamIndex::BitCrushBitRate:
        case ParamIndex::BitCrushMix:
        case ParamIndex::BitCrushOutput: return is_on(ParamIndex::BitCrushOn);

        case ParamIndex::CompressorThreshold:
        case ParamIndex::CompressorRatio:
        case ParamIndex::CompressorGain:
        case ParamIndex::CompressorType:
        case ParamIndex::CompressorMix: return is_on(ParamIndex::CompressorOn);

        case ParamIndex::CompressorAttack:
        case ParamIndex::CompressorRelease:
            return is_on(ParamIndex::CompressorOn) &&
                   ParamToInt<param_values::CompressorType>(linear(ParamIndex::CompressorType)) ==
                       param_values::CompressorType::Modern;
        case ParamIndex::CompressorAutoGain:
            return is_on(ParamIndex::CompressorOn) &&
                   ParamToInt<param_values::CompressorType>(linear(ParamIndex::CompressorType)) ==
                       param_values::CompressorType::Vintage;

        case ParamIndex::FilterCutoff:
        case ParamIndex::FilterResonance:
        case ParamIndex::FilterType:
        case ParamIndex::FilterMix: return is_on(ParamIndex::FilterOn);

        case ParamIndex::FilterGain:
            return is_on(ParamIndex::FilterOn) &&
                   param_values::EffectFilterTypeUsesGain(
                       ParamToInt<param_values::EffectFilterType>(linear(ParamIndex::FilterType)));

        case ParamIndex::StereoWidenWidth:
        case ParamIndex::StereoWidenMode:
        case ParamIndex::StereoWidenMix: return is_on(ParamIndex::StereoWidenOn);
        case ParamIndex::StereoWidenBassMono:
            return is_on(ParamIndex::StereoWidenOn) &&
                   ParamToInt<param_values::StereoWidenMode>(linear(ParamIndex::StereoWidenMode)) ==
                       param_values::StereoWidenMode::BassMono;

        case ParamIndex::ChorusRate:
        case ParamIndex::ChorusHighpass:
        case ParamIndex::ChorusDepth:
        case ParamIndex::ChorusMix:
        case ParamIndex::ChorusOutput: return is_on(ParamIndex::ChorusOn);

        case ParamIndex::DelayMode:
        case ParamIndex::DelayFilterCutoffSemitones:
        case ParamIndex::DelayFilterSpread:
        case ParamIndex::DelayMix:
        case ParamIndex::DelayFeedback:
        case ParamIndex::DelayTimeSyncSwitch: return is_on(ParamIndex::DelayOn);
        case ParamIndex::DelayTimeLMs:
        case ParamIndex::DelayTimeRMs:
            return is_on(ParamIndex::DelayOn) && !is_on(ParamIndex::DelayTimeSyncSwitch);
        case ParamIndex::DelayTimeSyncedL:
        case ParamIndex::DelayTimeSyncedR:
            return is_on(ParamIndex::DelayOn) && is_on(ParamIndex::DelayTimeSyncSwitch);

        case ParamIndex::PhaserCenterSemitones:
        case ParamIndex::PhaserModFreqHz:
        case ParamIndex::PhaserModDepth:
        case ParamIndex::PhaserFeedback:
        case ParamIndex::PhaserShape:
        case ParamIndex::PhaserStereoAmount:
        case ParamIndex::PhaserMix: return is_on(ParamIndex::PhaserOn);

        case ParamIndex::EqMix:
        case ParamIndex::EqType1:
        case ParamIndex::EqFreq1:
        case ParamIndex::EqResonance1:
        case ParamIndex::EqType2:
        case ParamIndex::EqFreq2:
        case ParamIndex::EqResonance2:
        case ParamIndex::EqType3:
        case ParamIndex::EqFreq3:
        case ParamIndex::EqResonance3: return is_on(ParamIndex::EqOn);

        case ParamIndex::EqGain1:
            return is_on(ParamIndex::EqOn) && param_values::EqTypeUsesGain(ParamToInt<param_values::EqType>(
                                                  linear(ParamIndex::EqType1)));
        case ParamIndex::EqGain2:
            return is_on(ParamIndex::EqOn) && param_values::EqTypeUsesGain(ParamToInt<param_values::EqType>(
                                                  linear(ParamIndex::EqType2)));
        case ParamIndex::EqGain3:
            return is_on(ParamIndex::EqOn) && param_values::EqTypeUsesGain(ParamToInt<param_values::EqType>(
                                                  linear(ParamIndex::EqType3)));

        case ParamIndex::ConvolutionReverbHighpass:
        case ParamIndex::ConvolutionReverbMix:
        case ParamIndex::ConvolutionReverbOutput: return is_on(ParamIndex::ConvolutionReverbOn);

        case ParamIndex::ReverbDecayTimeMs:
        case ParamIndex::ReverbSize:
        case ParamIndex::ReverbDelay:
        case ParamIndex::ReverbMix:
        case ParamIndex::ReverbPreLowPassCutoff:
        case ParamIndex::ReverbPreHighPassCutoff:
        case ParamIndex::ReverbLowShelfCutoff:
        case ParamIndex::ReverbLowShelfGain:
        case ParamIndex::ReverbHighShelfCutoff:
        case ParamIndex::ReverbHighShelfGain:
        case ParamIndex::ReverbChorusFrequency:
        case ParamIndex::ReverbChorusAmount: return is_on(ParamIndex::ReverbOn);

        case ParamIndex::LegacyMasterVelocity:
        case ParamIndex::LegacyBitCrushWet:
        case ParamIndex::LegacyBitCrushDry:
        case ParamIndex::LegacyCompressorThreshold:
        case ParamIndex::LegacyCompressorRatio:
        case ParamIndex::LegacyFilterCutoff:
        case ParamIndex::LegacyFilterResonance:
        case ParamIndex::LegacyFilterGain:
        case ParamIndex::LegacyFilterType:
        case ParamIndex::LegacyChorusHighpass:
        case ParamIndex::LegacyChorusWet:
        case ParamIndex::LegacyChorusDry:
        case ParamIndex::LegacyConvolutionReverbHighpass:
        case ParamIndex::LegacyConvolutionReverbWet:
        case ParamIndex::LegacyConvolutionReverbDry: return false;

        case ParamIndex::CountHelper:
        case ParamIndex::NonLayerParamsCount: PanicIfReached();
    }
    PanicIfReached();
    return true;
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
    {"FlCut", LayerParamIndex::LegacyFilterCutoff},
    {"FfRes", LayerParamIndex::LegacyFilterResonance},
    {"FlTy", LayerParamIndex::LegacyFilterType},
    {"FlAm", LayerParamIndex::FilterEnvAmount},
    {"FlAtt", LayerParamIndex::FilterAttack},
    {"FLDec", LayerParamIndex::FilterDecay},
    {"FlSus", LayerParamIndex::FilterSustain},
    {"FlRel", LayerParamIndex::FilterRelease},
    {"LfoOn", LayerParamIndex::LfoOn},
    {"LfoSh", LayerParamIndex::LegacyLfoShape},
    {"LfoMd", LayerParamIndex::LfoRestart},
    {"LfoAm", LayerParamIndex::LfoAmount},
    {"LfoTg", LayerParamIndex::LegacyLfoDestination},
    {"LfoSyt", LayerParamIndex::LfoRateTempoSynced},
    {"LfoHZ", LayerParamIndex::LfoRateHz},
    {"LfoSyO", LayerParamIndex::LfoSyncSwitch},
    {"EqOn", LayerParamIndex::EqOn},
    {"EqFr0", LayerParamIndex::LegacyEqFreq1},
    {"EqRs0", LayerParamIndex::LegacyEqResonance1},
    {"EqGn0", LayerParamIndex::EqGain1},
    {"EqTy0", LayerParamIndex::LegacyEqType1},
    {"EqFr1", LayerParamIndex::LegacyEqFreq2},
    {"EqRs1", LayerParamIndex::LegacyEqResonance2},
    {"EqGn1", LayerParamIndex::EqGain2},
    {"EqTy1", LayerParamIndex::LegacyEqType2},
    {"Vel", LayerParamIndex::LegacyVelocityMapping},
    {"KTr", LayerParamIndex::Keytrack},
    {"Mono", LayerParamIndex::LegacyMonophonicBool},
    {"Trn", LayerParamIndex::MidiTranspose},
});

struct NonLayerParamId {
    String id;
    ParamIndex index;
};

constexpr auto k_non_layer_params = ArrayT<NonLayerParamId>({
    {"MastVol", ParamIndex::MasterVolume},
    {"MastVel", ParamIndex::LegacyMasterVelocity},
    {"MastDyn", ParamIndex::MasterTimbre},
    {"DistType", ParamIndex::DistortionType},
    {"DistDrive", ParamIndex::DistortionDrive},
    {"DistOn", ParamIndex::DistortionOn},
    {"BitcBits", ParamIndex::BitCrushBits},
    {"BitcRate", ParamIndex::BitCrushBitRate},
    {"BitcWet", ParamIndex::LegacyBitCrushWet},
    {"BitcDry", ParamIndex::LegacyBitCrushDry},
    {"BitcOn", ParamIndex::BitCrushOn},
    {"CompThr", ParamIndex::LegacyCompressorThreshold},
    {"CompRt", ParamIndex::LegacyCompressorRatio},
    {"CompGain", ParamIndex::CompressorGain},
    {"CompAuto", ParamIndex::CompressorAutoGain},
    {"CompOn", ParamIndex::CompressorOn},
    {"FlOn", ParamIndex::FilterOn},
    {"FlCut", ParamIndex::LegacyFilterCutoff},
    {"FlRes", ParamIndex::LegacyFilterResonance},
    {"FlGain", ParamIndex::LegacyFilterGain},
    {"FlType", ParamIndex::LegacyFilterType},
    {"SterWd", ParamIndex::StereoWidenWidth},
    {"SterOn", ParamIndex::StereoWidenOn},
    {"ChorRate", ParamIndex::ChorusRate},
    {"ChorHP", ParamIndex::LegacyChorusHighpass},
    {"ChorDpth", ParamIndex::ChorusDepth},
    {"ChorWet", ParamIndex::LegacyChorusWet},
    {"ChorDry", ParamIndex::LegacyChorusDry},
    {"ChorOn", ParamIndex::ChorusOn},
    {"ConvHP", ParamIndex::LegacyConvolutionReverbHighpass},
    {"ConvWet", ParamIndex::LegacyConvolutionReverbWet},
    {"ConvDry", ParamIndex::LegacyConvolutionReverbDry},
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

TEST_CASE(TestParamIdStringsUnique) {
    for (auto const i : Range(k_num_parameters)) {
        auto const& a = k_param_descriptors[i];
        CHECK(a.id_string.size != 0);
        for (auto const j : Range(i + 1, k_num_parameters)) {
            auto const& b = k_param_descriptors[j];
            CHECK_OP(a.id_string, !=, b.id_string);
        }
    }
    return k_success;
}

TEST_REGISTRATION(RegisterParamDescriptorTests) {
    REGISTER_TEST(TestNumberStartsWithNegativeZero);
    REGISTER_TEST(TestLegacyConversion);
    REGISTER_TEST(TestParamStringConversion);
    REGISTER_TEST(TestParamIdStringsUnique);
}
