// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "state/state_coding.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"
#include "utils/json/json_writer.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/sample_library/mdata.hpp"

#include "config.h"
#include "state_snapshot.hpp"

using namespace json;

namespace legacy_mappings {

struct MenuNameMapping {
    f32 value;
    String names[2];
};

static f32 FindMenuValue(Span<MenuNameMapping const> mappings, String search_name) {
    bool found = false;
    f32 result {};
    for (auto const& mapping : mappings) {
        for (auto const& name : mapping.names) {
            if (name.size == 0) continue;
            if (name == search_name) {
                result = mapping.value;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    ASSERT(found);
    return result;
}

static Span<MenuNameMapping const> MenuNameMappingsForParam(ParamIndex index) {
    if (IsLayerParamOfSpecificType(index, LayerParamIndex::EqType1) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::EqType2)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::EqType::Peak, {"Peaking", "Peak"}},
            {(f32)param_values::EqType::LowShelf, {"Low Shelf", "Low-shelf"}},
            {(f32)param_values::EqType::HighShelf, {"High Shelf", "High-shelf"}},
        });
        return k_types.Items();
    } else if (IsLayerParamOfSpecificType(index, LayerParamIndex::LfoRateTempoSynced)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::LfoSyncedRate::_1_64T, {"1/64T"}},
            {(f32)param_values::LfoSyncedRate::_1_64, {"1/64"}},
            {(f32)param_values::LfoSyncedRate::_1_64D, {"1/64D"}},
            {(f32)param_values::LfoSyncedRate::_1_32T, {"1/32T"}},
            {(f32)param_values::LfoSyncedRate::_1_32, {"1/32"}},
            {(f32)param_values::LfoSyncedRate::_1_32D, {"1/32D"}},
            {(f32)param_values::LfoSyncedRate::_1_16T, {"1/16T"}},
            {(f32)param_values::LfoSyncedRate::_1_16, {"1/16"}},
            {(f32)param_values::LfoSyncedRate::_1_16D, {"1/16D"}},
            {(f32)param_values::LfoSyncedRate::_1_8T, {"1/8T"}},
            {(f32)param_values::LfoSyncedRate::_1_8, {"1/8"}},
            {(f32)param_values::LfoSyncedRate::_1_8D, {"1/8D"}},
            {(f32)param_values::LfoSyncedRate::_1_4T, {"1/4T"}},
            {(f32)param_values::LfoSyncedRate::_1_4, {"1/4"}},
            {(f32)param_values::LfoSyncedRate::_1_4D, {"1/4D"}},
            {(f32)param_values::LfoSyncedRate::_1_2T, {"1/2T"}},
            {(f32)param_values::LfoSyncedRate::_1_2, {"1/2"}},
            {(f32)param_values::LfoSyncedRate::_1_2D, {"1/2D"}},
            {(f32)param_values::LfoSyncedRate::_1_1T, {"1/1T"}},
            {(f32)param_values::LfoSyncedRate::_1_1, {"1/1"}},
            {(f32)param_values::LfoSyncedRate::_1_1D, {"1/1D"}},
            {(f32)param_values::LfoSyncedRate::_2_1T, {"2/1T"}},
            {(f32)param_values::LfoSyncedRate::_2_1, {"2/1"}},
            {(f32)param_values::LfoSyncedRate::_2_1D, {"2/1D"}},
            {(f32)param_values::LfoSyncedRate::_4_1T, {"4/1T"}},
            {(f32)param_values::LfoSyncedRate::_4_1, {"4/1"}},
            {(f32)param_values::LfoSyncedRate::_4_1D, {"4/1D"}},
        });
        return k_types.Items();
    } else if (IsLayerParamOfSpecificType(index, LayerParamIndex::LfoRestart)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::LfoRestartMode::Retrigger, {"Retrigger"}},
            {(f32)param_values::LfoRestartMode::Free, {"Free"}},
        });
        return k_types.Items();

    } else if (IsLayerParamOfSpecificType(index, LayerParamIndex::LfoDestination)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::LfoDestination::Volume, {"Volume"}},
            {(f32)param_values::LfoDestination::Filter, {"Filter"}},
            {(f32)param_values::LfoDestination::Pan, {"Pan"}},
            {(f32)param_values::LfoDestination::Pitch, {"Pitch"}},
        });
        return k_types.Items();

    } else if (IsLayerParamOfSpecificType(index, LayerParamIndex::LfoShape)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::LfoShape::Sine, {"Sine"}},
            {(f32)param_values::LfoShape::Triangle, {"Triangle"}},
            {(f32)param_values::LfoShape::Sawtooth, {"Sawtooth"}},
            {(f32)param_values::LfoShape::Square, {"Square"}},
        });
        return k_types.Items();

    } else if (IsLayerParamOfSpecificType(index, LayerParamIndex::FilterType)) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::LayerFilterType::Lowpass, {"Lowpass", "Low-pass"}},
            {(f32)param_values::LayerFilterType::Bandpass, {"Bandpass", "Band-pass A"}},
            {(f32)param_values::LayerFilterType::Highpass, {"Highpass", "High-pass"}},
            {(f32)param_values::LayerFilterType::UnitGainBandpass, {"UnitGainBandpass", "Band-pass B"}},
            {(f32)param_values::LayerFilterType::BandShelving, {"BandShelving", "Band-shelving"}},
            {(f32)param_values::LayerFilterType::Notch, {"Notch", "Notch"}},
            {(f32)param_values::LayerFilterType::Allpass, {"Allpass", "All-pass (Legacy)"}},
            {(f32)param_values::LayerFilterType::Peak, {"Peak", "Peak"}},
        });
        return k_types.Items();

    } else if (index == ParamIndex::FilterType) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::EffectFilterType::LowPass, {"Low Pass", "Low-pass"}},
            {(f32)param_values::EffectFilterType::HighPass, {"High Pass", "High-pass"}},
            {(f32)param_values::EffectFilterType::BandPass, {"Band Pass", "Band-pass"}},
            {(f32)param_values::EffectFilterType::Notch, {"Notch", "Notch"}},
            {(f32)param_values::EffectFilterType::Peak, {"Peak", "Peak"}},
            {(f32)param_values::EffectFilterType::LowShelf, {"Low Shelf", "Low-shelf"}},
            {(f32)param_values::EffectFilterType::HighShelf, {"High Shelf", "High-shelf"}},
        });
        return k_types.Items();

    } else if (index == ParamIndex::DistortionType) {
        static constexpr auto k_types = ArrayT<legacy_mappings::MenuNameMapping>({
            {(f32)param_values::DistortionType::TubeLog, {"Tube Log"}},
            {(f32)param_values::DistortionType::TubeAsym3, {"Tube Asym3"}},
            {(f32)param_values::DistortionType::Sine, {"Sine"}},
            {(f32)param_values::DistortionType::Raph1, {"Raph1"}},
            {(f32)param_values::DistortionType::Decimate, {"Decimate"}},
            {(f32)param_values::DistortionType::Atan, {"Atan"}},
            {(f32)param_values::DistortionType::Clip, {"Clip"}},
        });
        return k_types.Items();
    }
    return {};
}

enum class ParamProjection {
    WasPercentNowFraction, // [-100, 100] to [-1, 1] or [0, 100] to [0, 1]
    WasDbNowAmp,
    WasOldBoolNowNewBool, // old: >= 0.5 == true, new: !0 == true
    WasOldIntNowNewInt, // old: used round() to convert, new: uses trunc()
};

static Optional<ParamProjection> ParamProjection(ParamIndex index) {
    if (IsLayerParamOfSpecificType(index, LayerParamIndex::LoopStart) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::LoopEnd) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::LoopCrossfade) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::SampleOffset) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::LfoAmount) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::FilterResonance) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::FilterEnvAmount) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::EqResonance1) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::EqResonance2) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::FilterSustain) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::Pan) || (index == ParamIndex::MasterVelocity) ||
        (index == ParamIndex::MasterTimbre) || (index == ParamIndex::DistortionDrive) ||
        (index == ParamIndex::StereoWidenWidth) || (index == ParamIndex::FilterResonance)) {
        ASSERT(k_param_descriptors[(u32)index].linear_range.min == 0 ||
               k_param_descriptors[(u32)index].linear_range.min == -1);
        ASSERT_EQ(k_param_descriptors[(u32)index].linear_range.max, 1.0f);
        return ParamProjection::WasPercentNowFraction;
    }

    if (IsLayerParamOfSpecificType(index, LayerParamIndex::Volume) ||
        IsLayerParamOfSpecificType(index, LayerParamIndex::VolumeSustain) ||
        (index == ParamIndex::MasterVolume) || (index == ParamIndex::BitCrushWet) ||
        (index == ParamIndex::BitCrushDry) || (index == ParamIndex::CompressorThreshold) ||
        (index == ParamIndex::ChorusWet) || (index == ParamIndex::ChorusDry) ||
        (index == ParamIndex::ConvolutionReverbWet) || (index == ParamIndex::ConvolutionReverbDry) ||
        (index == ParamIndex::BitCrushWet)) {
        ASSERT(k_param_descriptors[(u32)index].linear_range.min >= 0);
        ASSERT(k_param_descriptors[(u32)index].linear_range.max <
               30); // it's unlikely to have an amp above 30
        return ParamProjection::WasDbNowAmp;
    }

    if (k_param_descriptors[(u32)index].value_type == ParamValueType::Bool)
        return ParamProjection::WasOldBoolNowNewBool;

    if (IsAnyOf(k_param_descriptors[(u32)index].value_type,
                Array {ParamValueType::Int, ParamValueType::Menu}))
        return ParamProjection::WasOldIntNowNewInt;

    return k_nullopt;
}

} // namespace legacy_mappings

class JsonStateParser {
  public:
    enum class ParamValueType { None, Float, String };
    using ParamValue = TaggedUnion<ParamValueType,
                                   TypeAndTag<f32, ParamValueType::Float>,
                                   TypeAndTag<String, ParamValueType::String>>;

    JsonStateParser(StateSnapshot& state) : m_state(state) {}

    bool HandleEvent(EventHandlerStack& handler_stack, Event const& event) {
        if (SetIfMatchingArray(handler_stack,
                               event,
                               "fx_order",
                               [this](EventHandlerStack& handler_stack, Event const& event) {
                                   return HandleFxOrder(handler_stack, event);
                               }))
            return true;

        if (SetIfMatchingArray(handler_stack,
                               event,
                               "params",
                               [this](EventHandlerStack& handler_stack, Event const& event) {
                                   return HandleParams(handler_stack, event);
                               }))
            return true;

        if (SetIfMatchingObject(handler_stack,
                                event,
                                "master",
                                [this](EventHandlerStack& handler_stack, Event const& event) {
                                    return HandleMaster(handler_stack, event);
                                }))
            return true;

        if (SetIfMatchingObject(handler_stack,
                                event,
                                "library",
                                [this](EventHandlerStack& handler_stack, Event const& event) {
                                    return HandleLibrary(handler_stack, event);
                                }))
            return true;

        if (SetIfMatchingArray(handler_stack,
                               event,
                               "layers",
                               [this](EventHandlerStack& handler_stack, Event const& event) {
                                   return HandleLayers(handler_stack, event);
                               }))
            return true;

        return false;
    }

    Array<bool, k_num_parameters> param_value_is_present {};
    DynamicArrayBounded<EffectType, k_num_effect_types> fx_order {};

    Optional<Version> mirage_version {};
    String last_loaded_preset_name {};
    bool last_loaded_preset_changed {};
    String library_name {};

    InitialisedArray<ParamValue, ToInt(NoLongerExistingParam::Count)> non_existent_params {
        ParamValueType::None};

  private:
    void RegisterParsedParam() {
        if (!m_param_name.size) return;
        auto param_from_legacy = ParamFromLegacyId(m_param_name);
        if (!param_from_legacy) return;

        switch (param_from_legacy->tag) {
            case ParamExistance::StillExists: {
                auto const index = param_from_legacy->Get<ParamIndex>();
                Optional<f32> param_value {};

                switch (m_param_value.tag) {
                    case ParamValueType::None: break;
                    case ParamValueType::Float: {
                        param_value = m_param_value.Get<f32>();
                        break;
                    }
                    case ParamValueType::String: {
                        auto const mappings = legacy_mappings::MenuNameMappingsForParam(index);
                        ASSERT(mappings.size);
                        param_value = legacy_mappings::FindMenuValue(mappings, m_param_value.Get<String>());
                        break;
                    }
                }

                if (param_value) {
                    param_value_is_present[(usize)index] = true;
                    m_state.param_values[(usize)index] = *param_value;
                }
                break;
            }
            case ParamExistance::NoLongerExists: {
                non_existent_params[ToInt(param_from_legacy->Get<NoLongerExistingParam>())] = m_param_value;
                break;
            }
        }
    }

    bool HandleParams(EventHandlerStack& handler_stack, Event const& event) {
        if (SetIfMatchingObject(handler_stack, event, "", [this](EventHandlerStack&, Event const& event) {
                if (event.type == EventType::HandlingStarted) {
                    m_param_name = {};
                    m_param_value = ParamValueType::None;
                    return true;
                } else if (event.type == EventType::HandlingEnded) {
                    RegisterParsedParam();
                    return true;
                }

                if (SetIfMatchingRef(event, "name", m_param_name)) return true;

                if (event.key == "value"_s) {
                    if (event.type == EventType::String)
                        m_param_value = event.string;
                    else if (event.type == EventType::Double)
                        m_param_value = (f32)event.real;
                    else if (event.type == EventType::Int)
                        m_param_value = (f32)event.integer;
                    return true;
                }

                return false;
            }))
            return true;
        return false;
    }

    bool HandleLibrary(EventHandlerStack&, Event const& event) {
        // if (SetIfMatching(event, "id", m_state.library_name_hash)) return true;
        if (SetIfMatchingRef(event, "name", library_name)) return true;
        return false;
    }

    bool HandleLayers(EventHandlerStack& handler_stack, Event const& event) {
        if (event.type == EventType::HandlingStarted) {
            m_inst_index = 0;
            return true;
        }

        if (SetIfMatchingObject(handler_stack, event, "", [this](EventHandlerStack&, Event const& event) {
                String path;
                if (SetIfMatchingRef(event, "path", path)) {
                    if (path.size) {
                        auto special_type = mdata::SpecialAudioDataFromInstPath(path);
                        switch (special_type) {
                            case mdata::SpecialAudioDataTypeNone: {
                                auto id = path::Filename(path);

                                // MDATA libraries (which is what was used when we were using this JSON config
                                // format) didn't have the requirement that instrument names have to be unique
                                // within a library.
                                //
                                // These are the handful of conflicts that existed in the MDATA libraries, and
                                // the new names that we use to identify them.
                                //
                                // IMPORTANT: This is pretty hacky; it's paralleled with the renaming code in
                                // the sample_library files. You must keep them in sync.
                                if (path == "sampler/Rhythmic Movement/Strange Movements"_s)
                                    id = "Strange Movements 2"_s;
                                else if (path ==
                                         "sampler/Oneshots/Ghost Voice Phrases/Male/Vocal Join Us 01"_s)
                                    id = "Vocal Join Us 01 2"_s;
                                else if (path ==
                                         "sampler/Oneshots/Ghost Voice Phrases/Male/Vocal Join Us 02"_s)
                                    id = "Vocal Join Us 02 2"_s;
                                else if (path ==
                                         "sampler/Oneshots/Ghost Voice Phrases/Male/Vocal We Can See You"_s)
                                    id = "Vocal We Can See You 2"_s;

                                // MDATA libraries could mark instruments as one of the special types. It
                                // wasn't widely used. In Floe we have more advanced oscillator types so we
                                // want to use those instead. When loading MDATA files, we discard special
                                // types.
                                if (path == "sampler/Air/Noise - White"_s) {
                                    m_state.inst_ids[m_inst_index] = WaveformType::WhiteNoiseStereo;
                                    break;
                                } else if (path == "sampler/Mid/Mid - Sine"_s) {
                                    m_state.inst_ids[m_inst_index] = WaveformType::Sine;
                                    break;
                                }

                                ASSERT(id.size <= k_max_instrument_id_size);

                                m_state.inst_ids[m_inst_index] = sample_lib::InstrumentId {
                                    .library = {}, // filled in later
                                    .inst_id = id,
                                };
                                break;
                            }
                            case mdata::SpecialAudioDataTypeSine:
                                m_state.inst_ids[m_inst_index] = WaveformType::Sine;
                                break;
                            case mdata::SpecialAudioDataTypeWhiteNoiseStereo:
                                m_state.inst_ids[m_inst_index] = WaveformType::WhiteNoiseStereo;
                                break;
                            case mdata::SpecialAudioDataTypeWhiteNoiseMono:
                                m_state.inst_ids[m_inst_index] = WaveformType::WhiteNoiseMono;
                                break;
                            case mdata::SpecialAudioDataTypeCount: PanicIfReached(); break;
                        }
                    } else {
                        m_state.inst_ids[m_inst_index] = InstrumentType::None;
                    }
                    return true;
                }

                if (event.type == EventType::HandlingEnded) ++m_inst_index;

                return false;
            }))
            return true;
        return false;
    }

    bool HandleFxOrder(EventHandlerStack&, Event const& event) {
        if (event.type == EventType::HandlingStarted) {
            dyn::Clear(fx_order);
            return true;
        }

        String fx_name {};
        if (SetIfMatchingRef(event, "", fx_name)) {
            auto const e = FindEffectFromLegacyId(fx_name);
            dyn::AppendIfNotAlreadyThere(fx_order, e);
            return true;
        }
        return false;
    }

    bool HandleMaster(EventHandlerStack& handler_stack, Event const& event) {
        if (event.type == EventType::Int && event.key == "version") {
            mirage_version = Version((u32)event.integer);
            return true;
        }
        if (SetIfMatchingObject(handler_stack,
                                event,
                                "last loaded preset",
                                [this](EventHandlerStack&, Event const& event) {
                                    if (SetIfMatchingRef(event, "name", last_loaded_preset_name)) return true;
                                    if (SetIfMatching(event, "changed", last_loaded_preset_changed))
                                        return true;
                                    return false;
                                }))
            return true;

        return false;
    }

    static EffectType FindEffectFromLegacyId(String id) {
        if (id == "dist"_s) return EffectType::Distortion;
        if (id == "bitcrush"_s) return EffectType::BitCrush;
        if (id == "comp"_s) return EffectType::Compressor;
        if (id == "filt"_s) return EffectType::FilterEffect;
        if (id == "width"_s) return EffectType::StereoWiden;
        if (id == "chorus"_s) return EffectType::Chorus;
        if (id == "verb"_s) return EffectType::Reverb;
        if (id == "delay"_s) return EffectType::Delay;
        if (id == "phaser"_s) return EffectType::Phaser;
        if (id == "conv"_s) return EffectType::ConvolutionReverb;

        PanicIfReached();
        return {};
    }

    StateSnapshot& m_state;

    String m_param_name;

    ParamValue m_param_value = ParamValueType::None;
    usize m_inst_index = 0;
};

enum class StateVersion : u16 {
    Initial = 1,

    // Each layer now has velocity curve points. The old velocity-mapping menu is deprecated, as is the master
    // velocity-to-volume control.
    AddedLayerVelocityCurves,

    // Add Floe version to the state so that we can adapt the state if a bug was introduced in a specific
    // version.
    AddedFloeVersion,

    // Added macro parameters.
    AddedMacroAndKeyRangeAndPitchBendParameters,

    // Changed to using a single ID string for libraries instead of name+author.
    ReverseDnsLibraryId,

    LatestPlusOne,
    Latest = LatestPlusOne - 1,
};

static void AdaptNewerParams(StateSnapshot& state, StateVersion version, StateSource source) {
    static_assert(k_num_parameters == 225,
                  "You have changed the number of parameters. You must now bump the "
                  "state version number and handle setting any new parameters to "
                  "backwards-compatible states. In other words, these new parameters "
                  "should be deactivated when loading an old preset so that the old "
                  "preset does not sound different. After that's done, change this "
                  "static_assert to match the new number of parameters.");

    // We don't need to adapt parameters if the state is already aware of the new change.
    if (version < StateVersion::AddedLayerVelocityCurves) {
        state.velocity_curve_points = {};

        // We don't want to adapt parameters from the DAW because there might be automation on them.
        if (source == StateSource::Daw) {
            for (auto const layer_index : Range(k_num_layers)) {
                dyn::AssignAssumingAlreadyEmpty(state.velocity_curve_points[layer_index],
                                                Array {
                                                    CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                    CurveMap::Point {1.0f, 1.0f, 0.0f},
                                                });
            }
            return;
        }

        // Adapt LayerParamIndex::VelocityMapping.
        for (auto const layer_index : Range(k_num_layers)) {
            auto& val = state.LinearParam(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::VelocityMapping));
            auto const velocity_mapping_mode = (param_values::VelocityMappingMode)Round(val);

            // We don't use this param anymore.
            val = (f32)param_values::VelocityMappingMode::None;

            auto& points = state.velocity_curve_points[layer_index];
            switch (velocity_mapping_mode) {
                case param_values::VelocityMappingMode::None:
                    // Flat at max volume.
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 1.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::TopToBottom:
                    // Linear
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 1.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::BottomToTop:
                    // Inverse linear
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 0.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::TopToMiddle:
                    // Flat until middle, then linear ramp-up to end
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                        CurveMap::Point {0.5f, 0.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 1.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::MiddleOutwards:
                    // Linear ramp-up to middle, then linear ramp-down to end
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 0.0f, 0.0f},
                                                        CurveMap::Point {0.5f, 1.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 0.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::MiddleToBottom:
                    // Linear ramp-down to middle, then flat to end
                    dyn::AssignAssumingAlreadyEmpty(points,
                                                    Array {
                                                        CurveMap::Point {0.0f, 1.0f, 0.0f},
                                                        CurveMap::Point {0.5f, 0.0f, 0.0f},
                                                        CurveMap::Point {1.0f, 0.0f, 0.0f},
                                                    });
                    break;
                case param_values::VelocityMappingMode::Count: break;
            }
        }

        // Adapt MasterVelocity.
        {
            auto& val = state.LinearParam(ParamIndex::MasterVelocity);
            ASSERT(val >= 0.0f && val <= 1.0f);
            auto const velocity_volume_strength = val;
            val = 0.0f; // We don't use this param anymore, so set it to 0.

            for (auto& points : state.velocity_curve_points) {
                // Now, we must scale y values in a linear fashion. The stronger the velocity-volume value,
                // the more we should bring down the y values of the points nearer to x=0.
                for (auto& point : points)
                    point.y = Max(point.y - (point.y * (1.0f - point.x) * velocity_volume_strength), 0.0f);
            }
        }
    }

    if (version < StateVersion::AddedMacroAndKeyRangeAndPitchBendParameters) {
        // Macros did not exist.
        state.param_values[ToInt(ParamIndex::Macro1)] = 0.0f;
        state.param_values[ToInt(ParamIndex::Macro2)] = 0.0f;
        state.param_values[ToInt(ParamIndex::Macro3)] = 0.0f;
        state.param_values[ToInt(ParamIndex::Macro4)] = 0.0f;
        state.macro_names = DefaultMacroNames();
        state.macro_destinations = {};

        for (auto const layer_index : Range(k_num_layers)) {
            // There used to be no control over the key range.
            state.param_values[ToInt(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::KeyRangeLow))] = 0;
            state.param_values[ToInt(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::KeyRangeHigh))] = 127;
            state.param_values[ToInt(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::KeyRangeLowFade))] = 0;
            state.param_values[ToInt(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::KeyRangeHighFade))] = 0;

            // There used to be no pitch bend.
            state.param_values[ToInt(
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::PitchBendRange))] = 0;
        }
    }
}

static ErrorCodeOr<void> DecodeMirageJsonState(StateSnapshot& state,
                                               ArenaAllocator& scratch_arena,
                                               String data,
                                               bool adapt_for_latest_version) {
    if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
        for (auto& f : state.param_values)
            f = 999999999.f;
        for (auto& t : state.fx_order)
            t = (EffectType)k_num_effect_types;
        for (auto& i : state.inst_ids)
            i = sample_lib::InstrumentId {.library = "foo"_s, .inst_id = "bar"_s};
        state.ir_id = sample_lib::IrId {
            .library = sample_lib::k_mirage_compat_library_id,
            .ir_id = "Formant 1"_s,
        };
    }

    JsonStateParser parser(state);

    auto const json_parse_outcome = Parse(data,
                                          [&parser](EventHandlerStack& handler_stack, Event const& event) {
                                              return parser.HandleEvent(handler_stack, event);
                                          },
                                          scratch_arena,
                                          {});
    if (json_parse_outcome.HasError()) return ErrorCode {CommonError::InvalidFileFormat};

    if (parser.library_name == "None"_s || parser.library_name == ""_s) {
        for (auto& i : state.inst_ids)
            i = InstrumentType::None;
    } else {
        for (auto& i : state.inst_ids)
            if (auto s = i.TryGet<sample_lib::InstrumentId>())
                s->library = sample_lib::IdForMdataLibraryAlloc(parser.library_name, scratch_arena);
    }

    // Fill in missing values and convert the existing ones into their new formats
    // ======================================================================================================
    for (auto [index, v] : Enumerate<u16>(state.param_values)) {
        if (parser.param_value_is_present[index]) {
            auto const param_index = ParamIndex {index};
            auto const legacy_projection = legacy_mappings::ParamProjection(param_index);
            if (legacy_projection) {
                switch (*legacy_projection) {
                    case legacy_mappings::ParamProjection::WasPercentNowFraction: v /= 100.0f; break;
                    case legacy_mappings::ParamProjection::WasDbNowAmp: v = DbToAmp(v); break;
                    case legacy_mappings::ParamProjection::WasOldBoolNowNewBool:
                        v = (v >= 0.5f) ? 1.0f : 0.0f;
                        break;
                    case legacy_mappings::ParamProjection::WasOldIntNowNewInt: v = Round(v); break;
                }
            }

            v = k_param_descriptors[index].LineariseValue(v, true).Value();
        } else {
            v = k_param_descriptors[index].default_linear_value;
        }
    }

    auto old_p = [&](NoLongerExistingParam p) -> Optional<f32> {
        auto old_param = parser.non_existent_params[ToInt(p)];
        if (old_param.tag == JsonStateParser::ParamValueType::Float) return old_param.Get<f32>();
        return k_nullopt;
    };

    // Set the convolution IR based on the no-longer-existing param
    // ======================================================================================================
    {
        state.ir_id = k_nullopt;
        auto const old_param =
            parser.non_existent_params[ToInt(NoLongerExistingParam::ConvolutionLegacyMirageIrName)];
        if (old_param.tag == JsonStateParser::ParamValueType::String) {
            auto const ir_name = old_param.Get<String>();
            if (ir_name.size && ir_name != "None"_s) {
                state.ir_id = sample_lib::IrId {
                    .library = sample_lib::k_mirage_compat_library_id,
                    .ir_id = ir_name,
                };
            }
        }
    }

    // Set the reverb parameters based on the no-longer-existing params
    // ======================================================================================================
    {
        auto const uses_freeverb = old_p(NoLongerExistingParam::ReverbUseFreeverbSwitch).ValueOr(1) > 0.5f;

        auto const old_settings_on = old_p(NoLongerExistingParam::ReverbOnSwitch).ValueOr(0) >= 0.5f;
        auto const old_settings_dry_01 = DbToAmp(old_p(NoLongerExistingParam::ReverbDryDb).ValueOr(0));
        auto const old_settings_wet_01 =
            uses_freeverb ? old_p(NoLongerExistingParam::ReverbFreeverbWetPercent).ValueOr(0) / 100.0f
                          : DbToAmp(old_p(NoLongerExistingParam::ReverbSvWetDb).ValueOr(-90));

        auto const old_settings_size_01 =
            old_p(NoLongerExistingParam::ReverbSizePercent).ValueOr(40) / 100.0f;
        auto const old_settings_pre_delay_ms = old_p(NoLongerExistingParam::ReverbSvPreDelayMs).ValueOr(0);
        auto const old_settings_mod_freq_hz = old_p(NoLongerExistingParam::ReverbSvModFreqHz).ValueOr(0.1f);
        auto const old_settings_mod_depth_01 =
            old_p(NoLongerExistingParam::ReverbSvModDepthPercent).ValueOr(0) / 100.0f;
        auto const old_settings_filter_bidirectional =
            uses_freeverb
                ? (old_p(NoLongerExistingParam::ReverbFreeverbDampingPercent).ValueOr(0) / 100.0f) / 3
                : old_p(NoLongerExistingParam::ReverbSvFilterBidirectionalPercent).ValueOr(0) / 100.0f;

        state.LinearParam(ParamIndex::ReverbOn) = old_settings_on;
        state.LinearParam(ParamIndex::ReverbMix) =
            old_settings_wet_01 / (old_settings_wet_01 + old_settings_dry_01);
        state.LinearParam(ParamIndex::ReverbSize) = old_settings_size_01;
        state.LinearParam(ParamIndex::ReverbDecayTimeMs) =
            old_settings_size_01 * (uses_freeverb ? 0.5f : 0.8f);
        state.LinearParam(ParamIndex::ReverbDelay) = ParamDescriptorAt(ParamIndex::ReverbDelay)
                                                         .LineariseValue(old_settings_pre_delay_ms, true)
                                                         .Value();
        state.LinearParam(ParamIndex::ReverbChorusFrequency) =
            ParamDescriptorAt(ParamIndex::ReverbChorusFrequency)
                .LineariseValue(old_settings_mod_freq_hz, true)
                .Value();
        state.LinearParam(ParamIndex::ReverbChorusAmount) = old_settings_mod_depth_01 * 0.6f;
        if (old_settings_filter_bidirectional > 0) {
            auto const p = ParamIndex::ReverbPreLowPassCutoff;
            auto const info = k_param_descriptors[ToInt(p)];
            state.LinearParam(p) = MapFrom01(1 - old_settings_filter_bidirectional,
                                             info.linear_range.min,
                                             info.linear_range.max);
            state.LinearParam(ParamIndex::ReverbPreHighPassCutoff) = 0;
        } else {
            auto const p = ParamIndex::ReverbPreHighPassCutoff;
            auto const info = k_param_descriptors[ToInt(p)];
            state.LinearParam(p) =
                MapFrom01(-old_settings_filter_bidirectional, info.linear_range.min, info.linear_range.max);
            state.LinearParam(ParamIndex::ReverbPreLowPassCutoff) = 128;
        }
        constexpr auto k_zero_db = 0.0f;
        state.LinearParam(ParamIndex::ReverbLowShelfGain) =
            ParamDescriptorAt(ParamIndex::ReverbLowShelfGain).LineariseValue(k_zero_db, false).Value();
        state.LinearParam(ParamIndex::ReverbHighShelfGain) =
            ParamDescriptorAt(ParamIndex::ReverbHighShelfGain).LineariseValue(k_zero_db, false).Value();
    }

    // Set the phaser parameters based on the no-longer-existing params
    // ======================================================================================================
    {
        auto const old_settings_on = old_p(NoLongerExistingParam::SvPhaserOn).ValueOr(0) >= 0.5f;
        auto const old_setting_dry_01 = DbToAmp(old_p(NoLongerExistingParam::SvPhaserDry).ValueOr(0));
        auto const old_setting_wet_01 = DbToAmp(old_p(NoLongerExistingParam::SvPhaserWet).ValueOr(-90));
        auto const old_setting_centre_freq_hz = old_p(NoLongerExistingParam::SvPhaserFreqHz).ValueOr(3000);
        auto const old_setting_mod_freq_hz = old_p(NoLongerExistingParam::SvPhaserModFreqHz).ValueOr(0.2f);
        auto const old_setting_mod_depth_01 =
            old_p(NoLongerExistingParam::SvPhaserModDepth).ValueOr(0) / 100.0f;
        auto const old_feedback_01 = old_p(NoLongerExistingParam::SvPhaserFeedback).ValueOr(40) / 100.0f;
        auto const old_mod_stereo = old_p(NoLongerExistingParam::SvPhaserModStereo).ValueOr(0);

        state.LinearParam(ParamIndex::PhaserOn) = old_settings_on;
        state.LinearParam(ParamIndex::PhaserMix) =
            old_setting_wet_01 / (old_setting_wet_01 + old_setting_dry_01);
        state.LinearParam(ParamIndex::PhaserStereoAmount) = old_mod_stereo;
        state.LinearParam(ParamIndex::PhaserFeedback) = old_feedback_01;
        {
            auto const& depth_info = k_param_descriptors[ToInt(ParamIndex::PhaserModDepth)];
            state.LinearParam(ParamIndex::PhaserModDepth) =
                MapFrom01(old_setting_mod_depth_01, depth_info.linear_range.min, depth_info.linear_range.max);
        }
        state.LinearParam(ParamIndex::PhaserModFreqHz) = ParamDescriptorAt(ParamIndex::PhaserModFreqHz)
                                                             .LineariseValue(old_setting_mod_freq_hz, true)
                                                             .Value();
        state.LinearParam(ParamIndex::PhaserCenterSemitones) =
            FrequencyToMidiNote(old_setting_centre_freq_hz);
    }

    // Set the delay parameters based on the no-longer-existing params
    // ======================================================================================================
    {
        auto const uses_legacy = old_p(NoLongerExistingParam::DelayLegacyAlgorithm).ValueOr(1) >= 0.5f;

        auto const old_settings_on = old_p(NoLongerExistingParam::DelayOn).ValueOr(0) >= 0.5f;
        auto const old_settings_delay_time_ms_l =
            uses_legacy ? old_p(NoLongerExistingParam::DelayOldDelayTimeLMs).ValueOr(470)
                        : old_p(NoLongerExistingParam::DelaySinevibesDelayTimeLMs).ValueOr(470);
        auto const old_settings_delay_time_ms_r =
            uses_legacy ? old_p(NoLongerExistingParam::DelayOldDelayTimeRMs).ValueOr(490)
                        : old_p(NoLongerExistingParam::DelaySinevibesDelayTimeRMs).ValueOr(490);
        auto const old_settings_is_synced =
            old_p(NoLongerExistingParam::DelayTimeSyncSwitch).ValueOr(0) >= 0.5f;

        auto const old_settings_bidirectional_filter_01 =
            uses_legacy ? (old_p(NoLongerExistingParam::DelayOldDamping).ValueOr(0) / 100.0f) / 3
                        : old_p(NoLongerExistingParam::DelaySinevibesFilter).ValueOr(0) / 100.0f;

        auto const old_settings_feedback = old_p(NoLongerExistingParam::DelayFeedback).ValueOr(0) / 100.0f;

        auto const old_setting_wet_01 = DbToAmp(old_p(NoLongerExistingParam::DelayWet).ValueOr(-90));

        auto get_synced_delay_time = [&](NoLongerExistingParam p) -> Optional<f32> {
            auto old_param = parser.non_existent_params[ToInt(p)];
            if (old_param.tag == JsonStateParser::ParamValueType::String) {
                auto const str = old_param.Get<String>();
                if (str == "1/64T"_s) return (f32)param_values::DelaySyncedTime::_1_64T;
                if (str == "1/64"_s) return (f32)param_values::DelaySyncedTime::_1_64;
                if (str == "1/64D"_s) return (f32)param_values::DelaySyncedTime::_1_64D;
                if (str == "1/32T"_s) return (f32)param_values::DelaySyncedTime::_1_32T;
                if (str == "1/32"_s) return (f32)param_values::DelaySyncedTime::_1_32;
                if (str == "1/32D"_s) return (f32)param_values::DelaySyncedTime::_1_32D;
                if (str == "1/16T"_s) return (f32)param_values::DelaySyncedTime::_1_16T;
                if (str == "1/16"_s) return (f32)param_values::DelaySyncedTime::_1_16;
                if (str == "1/16D"_s) return (f32)param_values::DelaySyncedTime::_1_16D;
                if (str == "1/8T"_s) return (f32)param_values::DelaySyncedTime::_1_8T;
                if (str == "1/8"_s) return (f32)param_values::DelaySyncedTime::_1_8;
                if (str == "1/8D"_s) return (f32)param_values::DelaySyncedTime::_1_8D;
                if (str == "1/4T"_s) return (f32)param_values::DelaySyncedTime::_1_4T;
                if (str == "1/4"_s) return (f32)param_values::DelaySyncedTime::_1_4;
                if (str == "1/4D"_s) return (f32)param_values::DelaySyncedTime::_1_4D;
                if (str == "1/2T"_s) return (f32)param_values::DelaySyncedTime::_1_2T;
                if (str == "1/2"_s) return (f32)param_values::DelaySyncedTime::_1_2;
                if (str == "1/2D"_s) return (f32)param_values::DelaySyncedTime::_1_2D;
                if (str == "1/1T"_s) return (f32)param_values::DelaySyncedTime::_1_1T;
                if (str == "1/1"_s) return (f32)param_values::DelaySyncedTime::_1_1;
                if (str == "1/1D"_s) return (f32)param_values::DelaySyncedTime::_1_1D;
            }
            return k_nullopt;
        };

        state.LinearParam(ParamIndex::DelayOn) = old_settings_on;
        state.LinearParam(ParamIndex::DelayTimeLMs) = ParamDescriptorAt(ParamIndex::DelayTimeLMs)
                                                          .LineariseValue(old_settings_delay_time_ms_l, true)
                                                          .Value();
        state.LinearParam(ParamIndex::DelayTimeRMs) = ParamDescriptorAt(ParamIndex::DelayTimeRMs)
                                                          .LineariseValue(old_settings_delay_time_ms_r, true)
                                                          .Value();
        state.LinearParam(ParamIndex::DelayTimeSyncSwitch) = old_settings_is_synced;
        state.LinearParam(ParamIndex::DelayTimeSyncedL) =
            get_synced_delay_time(NoLongerExistingParam::DelayTimeSyncedL)
                .ValueOr((f32)param_values::DelaySyncedTime::_1_4);
        state.LinearParam(ParamIndex::DelayTimeSyncedR) =
            get_synced_delay_time(NoLongerExistingParam::DelayTimeSyncedR)
                .ValueOr((f32)param_values::DelaySyncedTime::_1_4);

        auto& new_mode = state.LinearParam(ParamIndex::DelayMode);
        new_mode = (f32)param_values::DelayMode::Stereo;
        if (auto const str = parser.non_existent_params[ToInt(NoLongerExistingParam::DelaySinevibesMode)]
                                 .TryGet<String>()) {
            if (*str == "Stereo"_s)
                new_mode = (f32)param_values::DelayMode::Stereo;
            else if (*str == "Ping-pong LR"_s)
                new_mode = (f32)param_values::DelayMode::PingPong;
            else if (*str == "Ping-pong RL"_s)
                new_mode = (f32)param_values::DelayMode::PingPong;
        }

        state.LinearParam(ParamIndex::DelayFilterSpread) = 1;
        state.LinearParam(ParamIndex::DelayFilterCutoffSemitones) =
            0.5f + (-old_settings_bidirectional_filter_01) / 2;

        state.LinearParam(ParamIndex::DelayFeedback) =
            uses_legacy ? old_settings_feedback : Pow(old_settings_feedback, 0.1f);
        state.LinearParam(ParamIndex::DelayMix) = old_setting_wet_01 * 0.3f;
    }

    // Set the layer loop-on parameters based on the no-longer-existing params
    // ======================================================================================================
    {
        struct LoopSwitches {
            NoLongerExistingParam loop_on;
            NoLongerExistingParam ping_pong_on;
            u32 layer_index;
        };

        for (auto const l : ArrayT<LoopSwitches>({
                 {NoLongerExistingParam::Layer1LoopOnSwitch,
                  NoLongerExistingParam::Layer1LoopPingPongOnSwitch,
                  0},
                 {NoLongerExistingParam::Layer2LoopOnSwitch,
                  NoLongerExistingParam::Layer2LoopPingPongOnSwitch,
                  1},
                 {NoLongerExistingParam::Layer3LoopOnSwitch,
                  NoLongerExistingParam::Layer3LoopPingPongOnSwitch,
                  2},
             })) {
            auto const old_layer1_loop_on = old_p(l.loop_on).ValueOr(0) >= 0.5f;
            auto const old_layer1_ping_pong = old_p(l.ping_pong_on).ValueOr(0) >= 0.5f;

            param_values::LoopMode mode = param_values::LoopMode::InstrumentDefault;
            if (old_layer1_loop_on)
                if (!old_layer1_ping_pong)
                    mode = param_values::LoopMode::Standard;
                else
                    mode = param_values::LoopMode::PingPong;
            else
                mode = param_values::LoopMode::InstrumentDefault;

            state.LinearParam(ParamIndexFromLayerParamIndex(l.layer_index, LayerParamIndex::LoopMode)) =
                (f32)mode;
        }
    }

    // Ensure there are no missing effects in the fx order
    // ======================================================================================================
    {
        Array<EffectType, k_num_effect_types> fallback_order_of_effects;
        {
            // Never rearrange this.
            // This order is important for backwards compatibility.
            constexpr EffectType k_effects_order_before_effects_could_be_reordered[] = {
                EffectType::Distortion,
                EffectType::BitCrush,
                EffectType::Compressor,
                EffectType::FilterEffect,
                EffectType::StereoWiden,
                EffectType::Chorus,
                EffectType::Reverb,
                EffectType::Delay,
                EffectType::Phaser,
                EffectType::ConvolutionReverb,
            };
            static_assert(ArraySize(k_effects_order_before_effects_could_be_reordered) == k_num_effect_types);

            usize index = 0;

            // Start with adding the effects in the order that there were before there
            // was the ability to reorder them
            for (auto fx_type : k_effects_order_before_effects_could_be_reordered)
                fallback_order_of_effects[index++] = fx_type;

            if (index != k_num_effect_types) {
                // Next, add any effects that have been added since adding reorderability.
                for (auto const fx_type : Range(k_num_effect_types))
                    if (!Find(fallback_order_of_effects, (EffectType)fx_type))
                        fallback_order_of_effects[index++] = (EffectType)fx_type;
            }
            ASSERT_EQ(index, fallback_order_of_effects.size);
        }

        if (parser.fx_order.size) {
            DynamicArrayBounded<EffectType, k_num_effect_types> effects {};
            for (auto fx_type : parser.fx_order)
                dyn::Append(effects, fx_type);

            if (effects.size != k_num_effect_types)
                for (auto fx_type : fallback_order_of_effects)
                    dyn::AppendIfNotAlreadyThere(effects, fx_type);
            ASSERT_EQ(effects.size, k_num_effect_types);

            for (auto const i : Range(k_num_effect_types))
                state.fx_order[i] = effects[i];
        } else {
            state.fx_order = fallback_order_of_effects;
        }
    }

    // Ensure backwards compatibility by recreating old Mirage bug behaviour
    // ======================================================================================================
    {
        auto const mirage_preset_version_hex = parser.mirage_version.ValueOr({}).Packed();

        auto layer_param_value = [&](u32 layer_index, LayerParamIndex param) -> f32& {
            return state.param_values[ToInt(ParamIndexFromLayerParamIndex(layer_index, param))];
        };

        // The pitch/detune sliders of a layer that was set to 'no key tracking' used to do nothing. This was
        // a bug. In order to not change the behaviour of people's old DAW projects, we recreate this
        // behaviour by setting those values to 0 here.
        constexpr auto k_version_that_fixed_no_key_tracking_tuning_bug = PackVersionIntoU32(1, 2, 0);
        if (mirage_preset_version_hex < k_version_that_fixed_no_key_tracking_tuning_bug) {
            for (auto const layer_index : Range(k_num_layers)) {
                auto const keytracking_off = layer_param_value(layer_index, LayerParamIndex::Keytrack) < 0.5f;
                if (keytracking_off) {
                    layer_param_value(layer_index, LayerParamIndex::TuneCents) = 0;
                    layer_param_value(layer_index, LayerParamIndex::TuneSemitone) = 0;
                }
            }
        }

        // There was a bug where if the sample offset position was more than twice the loop-end position of a
        // ping-pong loop, the sound would be silent. In order to not change the behaviour of people's old DAW
        // projects, we recreate this behaviour by muting the layer.
        constexpr auto k_version_that_fixed_start_offset_past_ping_pong_silent = PackVersionIntoU32(1, 2, 0);
        if (mirage_preset_version_hex < k_version_that_fixed_start_offset_past_ping_pong_silent) {
            for (auto const layer_index : Range(k_num_layers)) {
                if ((param_values::LoopMode)layer_param_value(layer_index, LayerParamIndex::LoopMode) ==
                    param_values::LoopMode::PingPong) {
                    // The start can be larger than the end.
                    auto const max_loop_pos = Max(layer_param_value(layer_index, LayerParamIndex::LoopStart),
                                                  layer_param_value(layer_index, LayerParamIndex::LoopEnd));
                    if (layer_param_value(layer_index, LayerParamIndex::SampleOffset) > (max_loop_pos * 2))
                        layer_param_value(layer_index, LayerParamIndex::Mute) = 1;
                }
            }
        }

        // Prior to Mirage 2.0.3, there was no such thing as a ping-pong crossfade - it was equivalent to
        // being set to 0. We recreate that behaviour here so as to maintain backwards compatibility.
        constexpr auto k_version_that_added_ping_pong_xfade = PackVersionIntoU32(2, 0, 3);
        if (mirage_preset_version_hex < k_version_that_added_ping_pong_xfade) {
            for (auto const layer_index : Range(k_num_layers)) {
                if ((param_values::LoopMode)layer_param_value(layer_index, LayerParamIndex::LoopMode) ==
                    param_values::LoopMode::PingPong) {
                    layer_param_value(layer_index, LayerParamIndex::LoopCrossfade) = 0;
                }
            }
        }
    }

    if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
        for (auto const i : Range(k_num_parameters)) {
            auto const& info = k_param_descriptors[i];
            auto const v = state.param_values[i];
            if (v < info.linear_range.min || v > info.linear_range.max) {
                LogDebug({},
                         "Param \"{} {}\" value ({}) is outside of the expected "
                         "range: ({}, {})",
                         info.ModuleString(),
                         info.name,
                         v,
                         info.linear_range.min,
                         info.linear_range.max);
                PanicIfReached();
            }
        }
    }
    if (adapt_for_latest_version) AdaptNewerParams(state, StateVersion::Initial, StateSource::PresetFile);

    return k_success;
}

ErrorCodeOr<void> DecodeMirageJsonState(StateSnapshot& state, ArenaAllocator& scratch_arena, String data) {
    return DecodeMirageJsonState(state, scratch_arena, data, true);
}

// ==========================================================================================================

//
// Here we have a backwards-compatible unified system for both reading and
// writing. Little-endian only.
//
// The format of this file is solely defined by the sequence of the code in this
// file; there is no external definition.
//
// Therefore it's _crucial_ to remember that you can never rearrange the
// sequence of calls to the serialise functions. The order of this code _is_ the
// file format.
//
// One of the first items in this binary file format is the StateVersion. When
// reading, we check this value against every field. If the value is only found
// in versions of the format that came _after_ the version that we are reading,
// we skip it. We can also remove fields, so long as we mark which versions of
// the format contain it; when reading those versions, we must still increment
// over the value even if its not used.
//
// https://handmade.network/p/29/swedish-cubes-for-unity/blog/p/2723-how_media_molecule_does_serialization
//

struct StateCoder {
    template <typename Type>
    requires(Arithmetic<Type>)
    ErrorCodeOr<void> CodeNumber(Type& number, StateVersion version_added) {
        return CodeTrivialObject(number, version_added);
    }

    template <TriviallyCopyable Type>
    ErrorCodeOr<void> CodeTrivialObject(Type& trivial_obj, StateVersion version_added) {
        if (version >= version_added) return args.read_or_write_data(&trivial_obj, sizeof(Type));
        return k_success;
    }

    ErrorCodeOr<void> CodeDynArray(dyn::DynArray auto& arr, StateVersion version_added) {
        using Type = RemoveReference<decltype(arr)>::ValueType;
        static_assert(Fundamental<Type>,
                      "structs might have padding between members which are hard to ensure consistency with");
        if (version >= version_added) {
            u32 size = 0;
            if (IsWriting()) size = CheckedCast<u32>(arr.size);
            TRY(args.read_or_write_data(&size, sizeof(size)));

            if (size) {
                if (IsReading())
                    if (!dyn::Resize(arr, size)) return ErrorCode(CommonError::InvalidFileFormat);
                TRY(args.read_or_write_data((void*)arr.data, size * sizeof(Type)));
            }
        }
        return k_success;
    }

    ErrorCodeOr<void> CodeString(String& string, ArenaAllocator& allocator, StateVersion version_added) {
        if (version >= version_added) {
            u16 size = 0;
            if (IsWriting()) size = CheckedCast<u16>(string.size);
            TRY(args.read_or_write_data(&size, sizeof(size)));

            if (size) {
                if (IsReading()) string = allocator.AllocateExactSizeUninitialised<char>(size);
                TRY(args.read_or_write_data((void*)string.data, size));
            }
        }
        return k_success;
    }

    template <TriviallyCopyable Type>
    ErrorCodeOr<Type>
    CodeNumberNowRemoved(Type& number, StateVersion version_added, StateVersion version_removed) {
        if (version >= version_added && version < version_removed)
            return CodeInternal(&number, sizeof(number));
        return k_success;
    }

    // This is a helper function that helps catch bugs in the state. A number is incremented and stored every
    // time this is called and therefore when reading, if the number is not what is expected, it suggests that
    // there is a misalignment in the state.
    ErrorCodeOr<void> CodeIntegrityCheckNumber(StateVersion version_added) {
        if (version >= version_added) {
            u32 check = counter;
            TRY(CodeNumber(check, version_added));
            ASSERT_EQ(check, counter);
            counter++;
        }
        return k_success;
    }

    // This is a helper function designed to only be used when debugging an issue. It inserts an ASCII string
    // into the state so that you can identify sections in the state when viewed hexidecimally; for example
    // 'xxd'.
    ErrorCodeOr<void> CodeDebugMarker(char const (&id)[5], StateVersion version_added) {
        auto mapping_marking = U32FromChars(id);
        TRY(CodeNumber(mapping_marking, version_added));
        return k_success;
    }

    bool IsWriting() const { return args.mode == CodeStateArguments::Mode::Encode; }
    bool IsReading() const { return args.mode == CodeStateArguments::Mode::Decode; }

    CodeStateArguments const& args;
    StateVersion version;
    u32 counter {0};
};

ErrorCodeOr<void> CodeLibraryId(StateCoder& coder, sample_lib::LibraryId& library_id) {
    if (coder.IsReading() && coder.version < StateVersion::ReverseDnsLibraryId) {
        DynamicArrayBounded<char, k_max_library_author_size> library_author;
        DynamicArrayBounded<char, k_max_library_name_size> library_name;
        TRY(coder.CodeDynArray(library_author, StateVersion::Initial));
        TRY(coder.CodeDynArray(library_name, StateVersion::Initial));
        if (library_author == sample_lib::k_old_mirage_author)
            library_id = sample_lib::IdForMdataLibraryInline(library_name);
        else
            library_id = sample_lib::IdFromAuthorAndNameInline(library_author, library_name);
    } else {
        TRY(coder.CodeDynArray(library_id, StateVersion::ReverseDnsLibraryId));
    }
    return k_success;
}

ErrorCodeOr<void> CodeState(StateSnapshot& state, CodeStateArguments const& args) {
    static_assert(k_endianness == Endianness::Little, "this code makes no attempt to be endian agnostic");
    ArenaAllocatorWithInlineStorage<1000> scratch_arena {Malloc::Instance()};

    StateCoder coder {
        .args = args,
        .version = StateVersion::Initial, // start at Initial so that we always
                                          // write the magic value
    };

    // =======================================================================================================
    {
        constexpr u32 k_magic = 0x2a491f93; // never change
        u32 magic {};
        if (coder.IsWriting()) magic = k_magic;
        TRY(coder.CodeNumber(magic, StateVersion::Initial));

        if (magic != k_magic) return ErrorCode(CommonError::InvalidFileFormat);
    }

    // =======================================================================================================
    {
        if (coder.IsWriting()) coder.version = StateVersion::Latest;
        TRY(coder.CodeNumber((UnderlyingType<StateVersion>&)coder.version, StateVersion::Initial));

        // Forwards compatibility is not supported.
        if (coder.version > StateVersion::Latest) return ErrorCode(CommonError::CurrentFloeVersionTooOld);
    }

    // =======================================================================================================
    u32 floe_version_in_state_packed = k_floe_version.Packed();
    TRY(coder.CodeNumber(floe_version_in_state_packed, StateVersion::AddedFloeVersion));
    Version floe_version_in_state(floe_version_in_state_packed);

    // =======================================================================================================
    {
        static_assert(k_num_layers == 3,
                      "You will need to bump the state version "
                      "number and change the code below");

        for (auto const i : Range(k_num_layers)) {
            // Instrument IDs.
            enum class Type : u8 {
                None = 0,
                Sampler = 1,
                WaveformSine = 2,
                WaveformWhiteNoiseMono = 3,
                WaveformWhiteNoiseStereo = 4,
            };
            Type type {};
            sample_lib::InstrumentId sampler_inst_id {};

            if (coder.IsWriting()) {
                switch (state.inst_ids[i].tag) {
                    case InstrumentType::Sampler: {
                        type = Type::Sampler;
                        sampler_inst_id = state.inst_ids[i].Get<sample_lib::InstrumentId>();
                        break;
                    }
                    case InstrumentType::WaveformSynth: {
                        switch (state.inst_ids[i].Get<WaveformType>()) {
                            case WaveformType::Sine: type = Type::WaveformSine; break;
                            case WaveformType::WhiteNoiseMono: type = Type::WaveformWhiteNoiseMono; break;
                            case WaveformType::WhiteNoiseStereo: type = Type::WaveformWhiteNoiseStereo; break;
                            case WaveformType::Count: break;
                        }
                        break;
                    }
                    case InstrumentType::None: {
                        type = Type::None;
                        break;
                    }
                }
            }

            TRY(coder.CodeNumber((UnderlyingType<Type>&)type, StateVersion::Initial));
            if (type == Type::Sampler) {
                TRY(CodeLibraryId(coder, sampler_inst_id.library));
                TRY(coder.CodeDynArray(sampler_inst_id.inst_id, StateVersion::Initial));
            }

            if (coder.IsReading()) {
                switch (type) {
                    case Type::None: state.inst_ids[i] = InstrumentType::None; break;
                    case Type::Sampler: state.inst_ids[i] = sampler_inst_id; break;
                    case Type::WaveformSine: state.inst_ids[i] = WaveformType::Sine; break;
                    case Type::WaveformWhiteNoiseMono:
                        state.inst_ids[i] = WaveformType::WhiteNoiseMono;
                        break;
                    case Type::WaveformWhiteNoiseStereo:
                        state.inst_ids[i] = WaveformType::WhiteNoiseStereo;
                        break;
                }
            }

            // Velocity curves.
            CurveMap::Points points = state.velocity_curve_points[i];
            auto num_points = CheckedCast<u8>(points.size);

            TRY(coder.CodeNumber(num_points, StateVersion::AddedLayerVelocityCurves));
            if (coder.IsReading()) {
                if (!dyn::Resize(points, num_points)) return ErrorCode(CommonError::InvalidFileFormat);
            }

            for (auto& point : points) {
                TRY(coder.CodeNumber(point.x, StateVersion::AddedLayerVelocityCurves));
                TRY(coder.CodeNumber(point.y, StateVersion::AddedLayerVelocityCurves));
                TRY(coder.CodeNumber(point.curve, StateVersion::AddedLayerVelocityCurves));
            }

            if (coder.IsReading()) state.velocity_curve_points[i] = points;
        }
    }

    // =======================================================================================================
    {
        u8 num_tags {};
        if (coder.IsWriting()) num_tags = CheckedCast<u8>(state.metadata.tags.size);
        TRY(coder.CodeNumber(num_tags, StateVersion::Initial));

        for (auto const i : Range(num_tags)) {
            String tag {};
            if (coder.IsWriting()) tag = state.metadata.tags[i];
            TRY(coder.CodeString(tag, scratch_arena, StateVersion::Initial));
            if (coder.IsReading()) {
                if (tag.size > k_max_tag_size) return ErrorCode(CommonError::InvalidFileFormat);
                dyn::Emplace(state.metadata.tags, tag);
            }
        }
    }

    // =======================================================================================================
    {
        String author {};
        if (coder.IsWriting()) author = state.metadata.author;
        TRY(coder.CodeString(author, scratch_arena, StateVersion::Initial));
        if (coder.IsReading()) {
            if (author.size > k_max_preset_author_size) return ErrorCode(CommonError::InvalidFileFormat);
            state.metadata.author = author;
        }

        String description {};
        if (coder.IsWriting()) description = state.metadata.description;
        TRY(coder.CodeString(description, scratch_arena, StateVersion::Initial));
        if (coder.IsReading()) {
            if (description.size > k_max_preset_description_size)
                return ErrorCode(CommonError::InvalidFileFormat);
            state.metadata.description = description;
        }
    }

    // =======================================================================================================
    {
        String instance_id {};
        if (coder.IsWriting()) instance_id = state.instance_id;
        TRY(coder.CodeString(instance_id, scratch_arena, StateVersion::Initial));
        if (coder.IsReading()) {
            if (instance_id.size > k_max_instance_id_size) return ErrorCode(CommonError::InvalidFileFormat);
            state.instance_id = instance_id;
        }
    }

    // =======================================================================================================
    {
        u16 num_params {};
        if (coder.IsWriting()) num_params = CheckedCast<u16>(k_num_parameters);
        TRY(coder.CodeNumber(num_params, StateVersion::Initial));

        for (auto const i : Range(num_params)) {
            u32 id {};
            f32 linear_value {};

            if (coder.IsWriting()) {
                id = ParamIndexToId((ParamIndex)i);
                linear_value = state.param_values[i];
            }

            TRY(coder.CodeNumber(id, StateVersion::Initial));
            TRY(coder.CodeNumber(linear_value, StateVersion::Initial));

            if (coder.IsReading()) {
                auto const param_index = ParamIdToIndex(id);
                if (!param_index) return ErrorCode(CommonError::InvalidFileFormat);

                state.param_values[(usize)*param_index] = linear_value;
            }
        }

        if (coder.IsReading()) {
            if (coder.version < StateVersion::AddedLayerVelocityCurves) state.velocity_curve_points = {};

            // In commit e0b15326e9528ca33de7d3c8f905a3449a36d31a we introduced a bug where the LFO amount was
            // inverted prior to all previous versions. We have now fixed this, however, for presets that were
            // saved with the broken version we need to maintain the broken behaviour.
            if (floe_version_in_state >= Version(0, 12, 0) && floe_version_in_state <= Version(1, 0, 1)) {
                for (auto const layer_index : Range(k_num_layers)) {
                    auto& lfo_amount = state.LinearParam(
                        ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::LfoAmount));
                    lfo_amount = -lfo_amount;
                }
            }
        }
    }

    // =======================================================================================================
    {
        constexpr auto k_added = StateVersion::AddedMacroAndKeyRangeAndPitchBendParameters;

        u8 num_macros {};
        if (coder.IsWriting()) num_macros = k_num_macros;
        TRY(coder.CodeNumber(num_macros, k_added));

        for (auto const macro_index : Range(num_macros)) {
            TRY(coder.CodeDynArray(state.macro_names[macro_index], k_added));

            auto& dests = state.macro_destinations[macro_index];
            u8 num_macro_destinations {};
            if (coder.IsWriting()) num_macro_destinations = CheckedCast<u8>(dests.size);
            TRY(coder.CodeNumber(num_macro_destinations, k_added));
            if (coder.IsReading()) {
                if (!dyn::Resize(dests, num_macro_destinations))
                    return ErrorCode(CommonError::InvalidFileFormat);
            }

            for (auto const dest_index : Range(num_macro_destinations)) {
                auto& dest = dests[dest_index];

                u32 param_id {};
                if (coder.IsWriting()) param_id = ParamIndexToId(dest.param_index);
                TRY(coder.CodeNumber(param_id, k_added));
                if (coder.IsReading()) {
                    auto const param_index = ParamIdToIndex(param_id);
                    if (!param_index) return ErrorCode(CommonError::InvalidFileFormat);
                    dest.param_index = *param_index;
                }

                TRY(coder.CodeNumber(dest.value, k_added));
            }
        }

        if (coder.IsReading()) {
            if (coder.version < k_added) {
                state.macro_names = DefaultMacroNames();
                state.macro_destinations = {};
            }
        }
    }

    // =======================================================================================================
    {
        bool has_ir {};
        if (coder.IsWriting()) has_ir = state.ir_id.HasValue();
        TRY(coder.CodeNumber(has_ir, StateVersion::Initial));

        if (has_ir) {
            if (coder.IsReading()) state.ir_id = sample_lib::IrId {};
            TRY(CodeLibraryId(coder, state.ir_id->library));
            TRY(coder.CodeDynArray(state.ir_id->ir_id, StateVersion::Initial));
        }
    }

    TRY(coder.CodeIntegrityCheckNumber(StateVersion::Initial));

    // =======================================================================================================
    // It's actually not that abbreviated...
    if (args.abbreviated_read) {
        ASSERT(coder.IsReading());
        return k_success;
    }

    // =======================================================================================================
    {
        u16 num_effects {};
        if (coder.IsWriting()) num_effects = CheckedCast<u16>(k_num_effect_types);
        TRY(coder.CodeNumber(num_effects, StateVersion::Initial));

        Array<u8, k_num_effect_types> ordered_effect_ids;
        if (coder.IsWriting()) {
            for (auto [i, fx_type] : Enumerate(state.fx_order))
                ordered_effect_ids[i] = k_effect_info[(usize)fx_type].id;
            if constexpr (RUNTIME_SAFETY_CHECKS_ON) {
                for (auto const i : Range(ordered_effect_ids.size)) {
                    for (auto const j : Range(ordered_effect_ids.size))
                        if (i != j) ASSERT(ordered_effect_ids[i] != ordered_effect_ids[j]);
                }
            }
        }

        TRY(coder.CodeTrivialObject(ordered_effect_ids, StateVersion::Initial));

        if (coder.IsReading()) {
            for (auto [i, fx_id] : Enumerate(ordered_effect_ids)) {
                auto const type =
                    FindIf(k_effect_info, [fx_id](EffectInfo const& info) { return info.id == fx_id; });
                if (!type.HasValue()) return ErrorCode(CommonError::InvalidFileFormat);
                state.fx_order[i] = (EffectType)*type;
            }

            if (num_effects != k_num_effect_types) {
                static_assert(k_num_effect_types == 10,
                              "You've changed the number of effects, you must add the new "
                              "effects here so that the fx_order contains all values");
            }
        }
    }

    // =======================================================================================================
    {
        struct Mapping {
            u8 cc_num;
            u32 param_id;
        };
        Mapping* mappings = nullptr;
        u32 num_mappings = 0;

        if (coder.IsWriting() && args.source == StateSource::Daw) {
            DynamicArray<Mapping> mappings_arr {scratch_arena};
            for (auto [param_index, ccs] : Enumerate(state.param_learned_ccs)) {
                for (auto const cc_num : Range(128uz))
                    if (ccs.Get(cc_num)) {
                        dyn::Append(mappings_arr,
                                    Mapping {
                                        .cc_num = (u8)cc_num,
                                        .param_id = ParamIndexToId(ParamIndex(param_index)),
                                    });
                    }
            }
            num_mappings = (u32)mappings_arr.size;
            mappings = mappings_arr.ToOwnedSpan().data;
        }

        TRY(coder.CodeNumber(num_mappings, StateVersion::Initial));
        for (auto i : Range(num_mappings)) {
            Mapping m {};
            if (coder.IsWriting()) m = mappings[i];
            TRY(coder.CodeNumber(m.cc_num, StateVersion::Initial));
            TRY(coder.CodeNumber(m.param_id, StateVersion::Initial));
            if (coder.IsReading() && args.source == StateSource::Daw) {
                auto const index = ParamIdToIndex(m.param_id);
                if (!index) return ErrorCode(CommonError::InvalidFileFormat);
                state.param_learned_ccs[(usize)*index].Set(m.cc_num);
            }
        }
    }

    // =======================================================================================================
    AdaptNewerParams(state, coder.version, args.source);

    return k_success;
}

Optional<PresetFormat> PresetFormatFromPath(String path) {
    auto const ext = path::Extension(path);
    if (path::Equal(ext, FLOE_PRESET_FILE_EXTENSION)) return PresetFormat::Floe;
    constexpr auto k_mirage_ext = ".mirage-"_s;
    if constexpr (IS_WINDOWS) {
        if (StartsWithCaseInsensitiveAscii(ext, k_mirage_ext)) return PresetFormat::Mirage;
    } else {
        if (StartsWithSpan(ext, k_mirage_ext)) return PresetFormat::Mirage;
    }
    return k_nullopt;
}

ErrorCodeOr<StateSnapshot>
LoadPresetFile(PresetFormat format, Reader& reader, ArenaAllocator& scratch_arena, bool abbreviated_read) {
    StateSnapshot state;
    switch (format) {
        case PresetFormat::Floe: {
            TRY(CodeState(state,
                          CodeStateArguments {
                              .mode = CodeStateArguments::Mode::Decode,
                              .read_or_write_data = [&reader](void* data, usize bytes) -> ErrorCodeOr<void> {
                                  TRY(reader.Read(data, bytes));
                                  return k_success;
                              },
                              .source = StateSource::PresetFile,
                              .abbreviated_read = abbreviated_read,
                          }));
            break;
        }
        case PresetFormat::Mirage: {
            auto const file_data = TRY(reader.ReadOrFetchAll(scratch_arena));
            TRY(DecodeMirageJsonState(state, scratch_arena, {(char const*)file_data.data, file_data.size}));
            break;
        }
        case PresetFormat::Count: PanicIfReached(); break;
    }
    return state;
}

ErrorCodeOr<StateSnapshot>
LoadPresetFile(String const filepath, ArenaAllocator& scratch_arena, bool abbreviated_read) {
    StateSnapshot state;
    auto reader = TRY(Reader::FromFile(filepath));
    return LoadPresetFile(PresetFormatFromPath(filepath).ValueOr(PresetFormat::Mirage),
                          reader,
                          scratch_arena,
                          abbreviated_read);
}

ErrorCodeOr<void> SavePresetFile(String path, StateSnapshot const& state) {
    ArenaAllocatorWithInlineStorage<4000> scratch_arena {Malloc::Instance()};
    if (auto const ext = path::Extension(path); ext != FLOE_PRESET_FILE_EXTENSION) {
        path = fmt::Join(scratch_arena,
                         Array {path.SubSpan(0, path.size - ext.size), FLOE_PRESET_FILE_EXTENSION});
    }

    auto file = TRY(OpenFile(path, FileMode::Write()));
    TRY(CodeState(const_cast<StateSnapshot&>(state),
                  CodeStateArguments {
                      .mode = CodeStateArguments::Mode::Encode,
                      .read_or_write_data = [&file](void* data, usize bytes) -> ErrorCodeOr<void> {
                          TRY(file.Write({(u8 const*)data, bytes}));
                          return k_success;
                      },
                      .source = StateSource::PresetFile,
                      .abbreviated_read = false,
                  }));
    return k_success;
}

ErrorCodeOr<StateSnapshot> DecodeFromMemory(Span<u8 const> data, StateSource source, bool abbreviated_read) {
    StateSnapshot state;
    usize read_pos = 0;
    TRY(CodeState(state,
                  CodeStateArguments {
                      .mode = CodeStateArguments::Mode::Decode,
                      .read_or_write_data = [&](void* out_data, usize bytes) -> ErrorCodeOr<void> {
                          if ((read_pos + bytes) > data.size)
                              return ErrorCode(CommonError::InvalidFileFormat);
                          CopyMemory(out_data, data.data + read_pos, bytes);
                          read_pos += bytes;
                          return k_success;
                      },
                      .source = source,
                      .abbreviated_read = abbreviated_read,
                  }));
    return state;
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestAdaptPreAddedLayerVelocityCurvesParams) {
    StateSnapshot state {};

    state.LinearParam(ParamIndexFromLayerParamIndex(0, LayerParamIndex::VelocityMapping)) =
        (f32)param_values::VelocityMappingMode::TopToMiddle;
    state.LinearParam(ParamIndexFromLayerParamIndex(1, LayerParamIndex::VelocityMapping)) =
        (f32)param_values::VelocityMappingMode::MiddleOutwards;
    state.LinearParam(ParamIndexFromLayerParamIndex(2, LayerParamIndex::VelocityMapping)) =
        (f32)param_values::VelocityMappingMode::MiddleToBottom;

    SUBCASE("when master velocity is set to 0") {
        // No additional mapping should occur.
        state.LinearParam(ParamIndex::MasterVelocity) = 0;

        AdaptNewerParams(state, StateVersion::Initial, StateSource::PresetFile);

        // Master velocity should be set to 0.
        CHECK_APPROX_EQ(state.LinearParam(ParamIndex::MasterVelocity), 0.0f, 0.01f);
    }

    SUBCASE("when master velocity is set to 1") {
        // No additional mapping should occur.
        state.LinearParam(ParamIndex::MasterVelocity) = 1;

        AdaptNewerParams(state, StateVersion::Initial, StateSource::PresetFile);

        // Master velocity should be set to 1.
        CHECK_APPROX_EQ(state.LinearParam(ParamIndex::MasterVelocity), 0.0f, 0.01f);
    }

    // All velocity mapping modes should be set to the none.
    for (auto const layer_index : Range(k_num_layers)) {
        CHECK_APPROX_EQ(
            state.LinearParam(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::VelocityMapping)),
            (f32)param_values::VelocityMappingMode::None,
            0.01f);
    }

    // There should be 3 points for each velocity curve.
    for (auto const layer_index : Range(k_num_layers)) {
        auto const& points = state.velocity_curve_points[layer_index];
        CHECK_EQ(points.size, 3uz);
    }

    return k_success;
}

template <typename Type>
struct JsonPresetParam {
    String name;
    Type value;
};

template <typename Type>
static ErrorCodeOr<String>
MakeJsonPresetFromParams(ArenaAllocator& arena, Version version, Span<JsonPresetParam<Type>> params) {
    DynamicArray<char> json {arena};
    json::WriteContext writer = {.out = dyn::WriterFor(json), .add_whitespace = false};

    TRY(WriteObjectBegin(writer));

    TRY(WriteKeyObjectBegin(writer, "master"));
    TRY(WriteKeyValue(writer, "version", version.Packed()));
    TRY(WriteObjectEnd(writer));

    for (auto p : params) {
        TRY(WriteKeyArrayBegin(writer, "params"));
        TRY(WriteObjectBegin(writer));
        TRY(WriteKeyValue(writer, "name", p.name));
        TRY(WriteKeyValue(writer, "value", p.value));
        TRY(WriteObjectEnd(writer));
        TRY(WriteArrayEnd(writer));
    }

    TRY(WriteObjectEnd(writer));
    return json.ToOwnedSpan();
}

template <typename Type>
static ErrorCodeOr<String> MakeJsonPreset(ArenaAllocator& arena, Version version, String name, Type value) {
    Array<JsonPresetParam<Type>, 1> params {{{name, value}}};
    return MakeJsonPresetFromParams(arena, version, params.Items());
}

static f32 ProjectedValue(StateSnapshot const& state, ParamIndex index) {
    auto const& param = k_param_descriptors[ToInt(index)];
    return param.ProjectValue(state.param_values[ToInt(index)]);
}

static f32 ProjectedLayerValue(StateSnapshot const& state, u32 layer_index, LayerParamIndex param) {
    return ProjectedValue(state, ParamIndexFromLayerParamIndex(layer_index, param));
}

static void CheckStateIsValid(tests::Tester& tester, StateSnapshot const& state) {
    for (auto [index, value] : Enumerate(state.param_values)) {
        auto const& info = k_param_descriptors[index];
        CHECK_OP(value, >=, info.linear_range.min);
        CHECK_OP(value, <=, info.linear_range.max);
    }
    DynamicArrayBounded<EffectType, k_num_effect_types> effects {};
    for (auto fx : state.fx_order)
        dyn::AppendIfNotAlreadyThere(effects, fx);
    CHECK_EQ(effects.size, k_num_effect_types);

    for (auto& i : state.inst_ids) {
        switch (i.tag) {
            case InstrumentType::None: {
                break;
            }
            case InstrumentType::WaveformSynth: {
                auto& w = i.Get<WaveformType>();
                CHECK(ToInt(w) < ToInt(WaveformType::Count));
                break;
            }
            case InstrumentType::Sampler: {
                auto& s = i.Get<sample_lib::InstrumentId>();
                CHECK(s.library.size);
                CHECK(s.inst_id.size);
                break;
            }
        }
    }
}

TEST_CASE(TestParsersHandleInvalidData) {
    auto& scratch_arena = tester.scratch_arena;
    auto seed = RandomSeed();

    auto const make_random_data = [&]() {
        auto const data_size = RandomIntInRange<usize>(seed, 1, 1000);
        auto data = scratch_arena.NewMultiple<char>(data_size);
        for (auto& b : data)
            b = RandomIntInRange<char>(seed,
                                       SmallestRepresentableValue<char>(),
                                       LargestRepresentableValue<char>());
        return data;
    };

    StateSnapshot state {};

    SUBCASE("json") {
        for ([[maybe_unused]] auto i : Range(0, 20)) {
            auto const result = DecodeMirageJsonState(state, scratch_arena, make_random_data());
            CHECK(result.HasError());
        }
    }

    SUBCASE("binary") {
        for ([[maybe_unused]] auto i : Range(0, 20)) {
            auto const data = make_random_data();
            auto const result = DecodeFromMemory(data.ToByteSpan(), StateSource::PresetFile, false);
            CHECK(!result.HasValue());
        }
    }

    return k_success;
}

TEST_CASE(TestNewSerialisation) {
    auto& scratch_arena = tester.scratch_arena;

    for (auto const source : Array {StateSource::PresetFile, StateSource::Daw}) {
        CAPTURE(source);

        StateSnapshot state {};
        auto random_seed = RandomSeed();
        for (auto [index, param] : Enumerate(state.param_values)) {
            auto const& info = k_param_descriptors[index];
            param = RandomFloatInRange(random_seed, info.linear_range.min, info.linear_range.max);
        }

        for (auto [i, type] : Enumerate(state.fx_order))
            type = (EffectType)i;
        Shuffle(state.fx_order, random_seed);

        state.ir_id = sample_lib::IrId {
            .library = "irlibname.irlib"_s,
            .ir_id = "irfile"_s,
        };
        for (auto [index, inst] : Enumerate(state.inst_ids)) {
            inst = sample_lib::InstrumentId {
                .library = (String)fmt::Format(scratch_arena, "TestAuthor{}.TestLib{}", index, index),
                .inst_id = String(fmt::Format(scratch_arena, "Test/Path{}", index)),
            };
        }

        for (auto const _ : Range(RandomIntInRange<usize>(random_seed, 0, k_max_num_tags - 1))) {
            DynamicArrayBounded<char, k_max_tag_size> tag;
            dyn::Resize(tag, RandomIntInRange<usize>(random_seed, 1, k_max_tag_size));
            FillRandomAsciiChars(random_seed, tag);
            dyn::Append(state.metadata.tags, tag);
        }

        {
            DynamicArrayBounded<char, k_max_preset_description_size> description;
            dyn::Resize(description, RandomIntInRange<usize>(random_seed, 1, k_max_preset_description_size));
            FillRandomAsciiChars(random_seed, description);
            state.metadata.description = description;
        }

        {
            DynamicArrayBounded<char, k_max_preset_author_size> author;
            dyn::Resize(author, RandomIntInRange<usize>(random_seed, 1, k_max_preset_author_size));
            FillRandomAsciiChars(random_seed, author);
            state.metadata.author = author;
        }

        {
            dyn::Assign(state.velocity_curve_points[0],
                        Array {
                            CurveMap::Point {0.0f, 0.0f, 0.0f},
                            CurveMap::Point {0.5f, 0.5f, 0.0f},
                            CurveMap::Point {1.0f, 1.0f, 0.0f},
                        });
            dyn::Assign(state.velocity_curve_points[1],
                        Array {
                            CurveMap::Point {0.0f, 1.0f, 0.0f},
                            CurveMap::Point {0.5f, 0.5f, 0.0f},
                            CurveMap::Point {1.0f, 1.0f, 0.0f},
                        });
        }

        {
            state.macro_names = DefaultMacroNames();
            dyn::Assign(state.macro_names[0], "First Macro"_s);
            dyn::Assign(state.macro_names[1], "Second"_s);

            dyn::Assign(state.macro_destinations[0],
                        Array {
                            MacroDestination {
                                .param_index = ParamIndex::ChorusDepth,
                                .value = 0.4f,
                            },
                            MacroDestination {
                                .param_index = ParamIndex::ReverbSize,
                                .value = -1.0f,
                            },
                        });

            dyn::Assign(state.macro_destinations[3],
                        Array {
                            MacroDestination {
                                .param_index = ParamIndexFromLayerParamIndex(0, LayerParamIndex::EqFreq1),
                                .value = 0.5f,
                            },
                        });
        }

        if (source == StateSource::Daw) {
            for (auto const param : Range(k_num_parameters)) {
                if (param % 4 == 0) {
                    Bitset<128> bits {};
                    bits.Set(20);
                    bits.Set(10);
                    bits.Set(1);
                    state.param_learned_ccs[param] = bits;
                }
            }
        } else {
            state.param_learned_ccs = {};
        }

        CheckStateIsValid(tester, state);

        DynamicArray<u8> serialised_data {scratch_arena};
        REQUIRE(CodeState(state,
                          CodeStateArguments {
                              .mode = CodeStateArguments::Mode::Encode,
                              .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                                  dyn::AppendSpan(serialised_data, Span<u8 const> {(u8 const*)data, bytes});
                                  return k_success;
                              },
                              .source = source,
                          })
                    .Succeeded());

        StateSnapshot out_state {};
        usize read_pos = 0;
        REQUIRE(CodeState(out_state,
                          CodeStateArguments {
                              .mode = CodeStateArguments::Mode::Decode,
                              .read_or_write_data = [&](void* data, usize bytes) -> ErrorCodeOr<void> {
                                  CHECK(read_pos + bytes <= serialised_data.size);
                                  CopyMemory(data, serialised_data.data + read_pos, bytes);
                                  read_pos += bytes;
                                  return k_success;
                              },
                              .source = source,
                          })
                    .Succeeded());
        CHECK_OP(read_pos, ==, serialised_data.size);
        CheckStateIsValid(tester, out_state);

        CHECK(state == out_state);
        if (source == StateSource::Daw) CHECK(state.param_learned_ccs == out_state.param_learned_ccs);
    }

    return k_success;
}

TEST_CASE(TestBackwardCompat) {
    auto& scratch_arena = tester.scratch_arena;
    StateSnapshot state {};

    SUBCASE("old versions always turn set ping pong crossfade to 0") {
        auto const outcome = DecodeMirageJsonState(
            state,
            scratch_arena,
            TRY(MakeJsonPresetFromParams(scratch_arena,
                                         {1, 0, 0},
                                         ArrayT<JsonPresetParam<f32>>({
                                                                          {"L0LpOn", 1.0f},
                                                                          {"L0LpPP", 1.0f},
                                                                      })
                                             .Items())));
        REQUIRE(outcome.Succeeded());
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopCrossfade), 0.0f, 0.01f);
    }

    SUBCASE("recreate bug behaviour in old versions") {
        SUBCASE("no tuning if keytracking off") {
            auto const outcome =
                DecodeMirageJsonState(state,
                                      scratch_arena,
                                      TRY(MakeJsonPreset(scratch_arena, {1, 0, 0}, "L0KTr", 0.0f)));
            REQUIRE(outcome.Succeeded());
            for (auto const layer_index : Range(3u)) {
                CHECK_APPROX_EQ(ProjectedLayerValue(state, layer_index, LayerParamIndex::TuneCents),
                                0.0f,
                                0.01f);
                CHECK_APPROX_EQ(ProjectedLayerValue(state, layer_index, LayerParamIndex::TuneSemitone),
                                0.0f,
                                0.01f);
            }
        }
        SUBCASE("muted layer if sample offset twice loop end") {
            auto const outcome = DecodeMirageJsonState(
                state,
                scratch_arena,
                TRY(MakeJsonPresetFromParams(scratch_arena,
                                             {1, 0, 0},
                                             ArrayT<JsonPresetParam<f32>>({
                                                                              {"L0LpOn", 1.0f},
                                                                              {"L0LpPP", 1.0f},
                                                                              {"L0Offs", 0.9f},
                                                                              {"L0LpEnd", 0.2f},
                                                                          })
                                                 .Items())));
            REQUIRE(outcome.Succeeded());
            CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Mute), 1.0f, 0.01f);
        }
    }

    return k_success;
}

TEST_CASE(TestFuzzingJsonState) {
    auto seed = RandomSeed();
    StateSnapshot state;

    for (auto const i : Range((u32)k_num_parameters)) {
        auto& scratch_arena = tester.scratch_arena;
        scratch_arena.ResetCursorAndConsolidateRegions();

        auto const param = ParamIndex(i);
        auto const& info = k_param_descriptors[i];
        auto const legacy_id = ParamToLegacyId(param);
        if (!legacy_id) continue;

        if (info.value_type == ParamValueType::Menu) {
            auto const mappings = legacy_mappings::MenuNameMappingsForParam(param);
            for (auto const& mapping : mappings) {
                for (auto const name : mapping.names) {
                    if (!name.size) continue;
                    auto const outcome =
                        DecodeMirageJsonState(state,
                                              scratch_arena,
                                              TRY(MakeJsonPreset(scratch_arena, {2, 0, 0}, *legacy_id, name)),
                                              false);
                    CHECK(outcome.Succeeded());
                    if (outcome.Succeeded()) {
                        CheckStateIsValid(tester, state);
                        CHECK_APPROX_EQ(ProjectedValue(state, param), mapping.value, 0.01f);
                    }
                }
            }
        } else {
            for (auto _ : Range(3)) {
                auto const range = info.projection ? info.projection->range : info.linear_range;
                auto v = RandomFloatInRange(seed, range.min, range.max);
                if (info.value_type == ParamValueType::Bool)
                    v = v > 0.5f ? 1.0f : 0.0f;
                else if (IsAnyOf(info.value_type, Array {ParamValueType::Int, ParamValueType::Menu}))
                    v = Round(v);
                auto const original_v = v;

                if (auto const legacy_projection = legacy_mappings::ParamProjection(param)) {
                    switch (*legacy_projection) {
                        case legacy_mappings::ParamProjection::WasPercentNowFraction: v *= 100.0f; break;
                        case legacy_mappings::ParamProjection::WasDbNowAmp: v = AmpToDb(v); break;
                        case legacy_mappings::ParamProjection::WasOldBoolNowNewBool: break;
                        case legacy_mappings::ParamProjection::WasOldIntNowNewInt: break;
                    }
                }

                auto const outcome =
                    DecodeMirageJsonState(state,
                                          scratch_arena,
                                          TRY(MakeJsonPreset(scratch_arena, {2, 0, 0}, *legacy_id, v)),
                                          false);
                CHECK(outcome.Succeeded());
                if (outcome.Succeeded()) {
                    CheckStateIsValid(tester, state);
                    CAPTURE(*legacy_id);
                    CAPTURE(info.name);
                    CHECK_APPROX_EQ(ProjectedValue(state, param), original_v, 0.01f);
                }
            }
        }
    }

    return k_success;
}

static String TestPresetPath(tests::Tester& tester, String filename) {
    return path::Join(tester.scratch_arena,
                      Array {TestFilesFolder(tester), tests::k_preset_test_files_subdir, filename});
}

TEST_CASE(TestLoadingOldFiles) {
    auto& scratch_arena = tester.scratch_arena;

    auto decode_file = [&](String const filename) -> ErrorCodeOr<StateSnapshot> {
        StateSnapshot state;
        auto const data = TRY_I(ReadEntireFile(TestPresetPath(tester, filename), scratch_arena));
        REQUIRE(DecodeMirageJsonState(state, scratch_arena, data).Succeeded());
        CheckStateIsValid(tester, state);
        return state;
    };

    // Pre-Sv effects
    SUBCASE("stress-test.mirage-phoenix") {
        auto const state = TRY(decode_file("stress-test.mirage-phoenix"));

        CHECK(state.inst_ids[0].tag == InstrumentType::Sampler);
        CHECK(state.inst_ids[1].tag == InstrumentType::Sampler);
        CHECK(state.inst_ids[2].tag == InstrumentType::Sampler);
        if (auto i = state.inst_ids[0].TryGet<sample_lib::InstrumentId>()) {
            CHECK_EQ(i->library, sample_lib::IdForMdataLibraryAlloc("Phoenix"_s, scratch_arena));
            CHECK_EQ(i->inst_id, "Strings"_s);
        }
        if (auto i = state.inst_ids[1].TryGet<sample_lib::InstrumentId>()) {
            CHECK_EQ(i->library, sample_lib::IdForMdataLibraryAlloc("Phoenix"_s, scratch_arena));
            CHECK_EQ(i->inst_id, "Strings"_s);
        }
        if (auto i = state.inst_ids[2].TryGet<sample_lib::InstrumentId>()) {
            CHECK_EQ(i->library, sample_lib::IdForMdataLibraryAlloc("Phoenix"_s, scratch_arena));
            CHECK_EQ(i->inst_id, "Choir"_s);
        }
        CHECK(state.ir_id.HasValue());
        if (state.ir_id.HasValue()) {
            CHECK_EQ(state.ir_id->library, sample_lib::k_mirage_compat_library_id);
            CHECK_EQ(state.ir_id->ir_id, "5s Shimmer"_s);
        }

        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Volume), DbToAmp(-6.0f), 0.01f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::SampleOffset), 0.054875f, 0.005f);
        CHECK_EQ(ParamToInt<param_values::LfoShape>(ProjectedLayerValue(state, 0, LayerParamIndex::LfoShape)),
                 param_values::LfoShape::Sine);
        CHECK_EQ(ParamToInt<param_values::LfoSyncedRate>(
                     ProjectedLayerValue(state, 0, LayerParamIndex::LfoRateTempoSynced)),
                 param_values::LfoSyncedRate::_1_4);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopStart), 0.07196f, 0.005f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopEnd), 0.20306f, 0.005f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolumeSustain),
                        DbToAmp(-17.14738f),
                        0.005f);

        CHECK_APPROX_EQ(ProjectedLayerValue(state, 1, LayerParamIndex::Volume), DbToAmp(-6.0f), 0.01f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 2, LayerParamIndex::Volume), DbToAmp(-6.0f), 0.01f);

        CHECK_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopMode),
                 (f32)param_values::LoopMode::Standard);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopStart), 0.07f, 0.01f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopEnd), 0.20f, 0.01f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::LoopCrossfade), 0.27f, 0.01f);

        // Delay
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayOn)], 1.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncSwitch)], 1.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncedL)],
                 (f32)param_values::DelaySyncedTime::_1_4);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncedR)],
                 (f32)param_values::DelaySyncedTime::_1_8);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::DelayFeedback)], 0.5f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::DelayFilterCutoffSemitones)], 60.0f, 3.0f);

        // Reverb
        CHECK_EQ(state.param_values[ToInt(ParamIndex::ReverbOn)], 1.0f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbSize)], 0.6f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbMix)], 0.25f, 0.2f);
    }

    SUBCASE("Abstract Chord.mirage-abstract") {
        auto const state = TRY(decode_file("Abstract Chord.mirage-abstract"));

        CHECK(state.inst_ids[0].tag == InstrumentType::None);
        CHECK(state.inst_ids[1].tag == InstrumentType::None);
        REQUIRE(state.inst_ids[2].tag == InstrumentType::Sampler);

        {
            auto const i = state.inst_ids[2].Get<sample_lib::InstrumentId>();
            CHECK_EQ(i.library, sample_lib::IdForMdataLibraryAlloc("Abstract Energy"_s, scratch_arena));
            CHECK_EQ(i.inst_id, "Drone 2 Atmos"_s);
        }

        CHECK_EQ(state.param_values[ToInt(ParamIndex::BitCrushOn)], 0.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::ReverbOn)], 0.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayOn)], 0.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::PhaserOn)], 0.0f);

        CHECK_APPROX_EQ(ProjectedLayerValue(state, 2, LayerParamIndex::LoopCrossfade), 0.54f, 0.01f);
    }

    // Pre-Sv effects
    SUBCASE("sine.mirage-wraith") {
        auto const state = TRY(decode_file("sine.mirage-wraith"));

        CHECK(state.inst_ids[0].tag == InstrumentType::WaveformSynth);
        CHECK(state.inst_ids[1].tag == InstrumentType::None);
        CHECK(state.inst_ids[2].tag == InstrumentType::None);

        if (auto w = state.inst_ids[0].TryGet<WaveformType>()) CHECK_EQ(*w, WaveformType::Sine);

        CHECK(!state.ir_id.HasValue());

        CHECK_EQ(state.fx_order[0], EffectType::Distortion);
        CHECK_EQ(state.fx_order[1], EffectType::BitCrush);
        CHECK_EQ(state.fx_order[2], EffectType::Compressor);
        CHECK_EQ(state.fx_order[3], EffectType::FilterEffect);
        CHECK_EQ(state.fx_order[4], EffectType::StereoWiden);
        CHECK_EQ(state.fx_order[5], EffectType::Chorus);
        CHECK_EQ(state.fx_order[6], EffectType::Reverb);
        CHECK_EQ(state.fx_order[7], EffectType::Delay);
        CHECK_EQ(state.fx_order[8], EffectType::Phaser);
        CHECK_EQ(state.fx_order[9], EffectType::ConvolutionReverb);

        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Volume), DbToAmp(-6.0f), 0.01f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Mute), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Solo), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::Pan), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::TuneCents), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::TuneSemitone), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VelocityMapping), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolEnvOn), 1.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolumeAttack), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolumeDecay), 0.0f, 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolumeSustain), DbToAmp(0.0f), 0.1f);
        CHECK_APPROX_EQ(ProjectedLayerValue(state, 0, LayerParamIndex::VolumeRelease), 15.045f, 0.1f);
        CHECK_EQ(ParamToInt<param_values::LayerFilterType>(
                     ProjectedLayerValue(state, 0, LayerParamIndex::FilterType)),
                 param_values::LayerFilterType::Lowpass);
        CHECK_EQ(ParamToInt<param_values::LfoDestination>(
                     ProjectedLayerValue(state, 0, LayerParamIndex::LfoDestination)),
                 param_values::LfoDestination::Volume);

        CHECK_EQ(state.param_values[ToInt(ParamIndex::PhaserOn)], 0.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::ReverbOn)], 0.0f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbSize)], 0.6f, 0.001f);

        CHECK_EQ(ParamToInt<param_values::DistortionType>(ProjectedValue(state, ParamIndex::DistortionType)),
                 param_values::DistortionType::TubeLog);
    }

    // Has Sv effects
    SUBCASE("stress-test.mirage-wraith") {
        auto const state = TRY(decode_file("stress-test.mirage-wraith"));

        // Reverb
        CHECK_EQ(state.param_values[ToInt(ParamIndex::ReverbOn)], 1.0f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbSize)], 0.6f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbDecayTimeMs)], 0.5f, 0.2f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbDelay), 100.0f, 0.01f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbChorusAmount), 0.24f, 0.01f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbChorusFrequency), 0.7f, 0.01f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbPreLowPassCutoff), 64.0f, 1.0f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbPreHighPassCutoff), 0.0f, 1.0f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbHighShelfGain), 0.0f, 1.0f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::ReverbLowShelfGain), 0.0f, 1.0f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::ReverbMix)], 0.3f, 0.02f);

        // Phaser
        CHECK_EQ(state.param_values[ToInt(ParamIndex::PhaserOn)], 1.0f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::PhaserCenterSemitones),
                        FrequencyToMidiNote(3000),
                        0.01f);
        CHECK_APPROX_EQ(ProjectedValue(state, ParamIndex::PhaserModFreqHz), 0.2f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::PhaserModDepth)], 9.6f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::PhaserFeedback)], 0.4f, 0.01f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::PhaserStereoAmount)], 0.0f, 0.01f);
        CHECK_LT(state.param_values[ToInt(ParamIndex::PhaserMix)], 0.5f);

        // Delay
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayOn)], 1.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncSwitch)], 1.0f);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncedL)],
                 (f32)param_values::DelaySyncedTime::_1_4);
        CHECK_EQ(state.param_values[ToInt(ParamIndex::DelayTimeSyncedR)],
                 (f32)param_values::DelaySyncedTime::_1_8);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::DelayFeedback)], 0.8f, 0.2f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::DelayFilterCutoffSemitones)], 60.0f, 3.0f);
        CHECK_APPROX_EQ(state.param_values[ToInt(ParamIndex::DelayMix)], 0.15f, 0.1f);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterStateCodingTests) {
    REGISTER_TEST(TestLoadingOldFiles);
    REGISTER_TEST(TestBackwardCompat);
    REGISTER_TEST(TestFuzzingJsonState);
    REGISTER_TEST(TestNewSerialisation);
    REGISTER_TEST(TestParsersHandleInvalidData);
    REGISTER_TEST(TestAdaptPreAddedLayerVelocityCurvesParams);
}
