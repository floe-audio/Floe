// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <clap/process.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/thread_extra/atomic_queue.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "effect_bitcrush.hpp"
#include "effect_chorus.hpp"
#include "effect_compressor_stillwell_majortom.hpp"
#include "effect_convo.hpp"
#include "effect_delay.hpp"
#include "effect_distortion.hpp"
#include "effect_filter_iir.hpp"
#include "effect_phaser.hpp"
#include "effect_reverb.hpp"
#include "effect_stereo_widen.hpp"
#include "layer_processor.hpp"
#include "param.hpp"
#include "plugin/plugin.hpp"
#include "processing_utils/audio_processing_context.hpp"
#include "processing_utils/volume_fade.hpp"
#include "voices.hpp"

enum class EventForAudioThreadType : u8 {
    ParamChanged,
    ParamGestureBegin,
    ParamGestureEnd,
    FxOrderChanged,
    ReloadAllAudioState,
    ConvolutionIRChanged,
    LayerInstrumentChanged,
    StartNote,
    EndNote,
    AppendMacroDestination,
    RemoveMacroDestination,
    MacroDestinationValueChanged,
    RemoveAllMacroDestinations,
};

struct MainThreadChangedParam {
    f32 value;
    ParamIndex param;
    bool host_should_not_record;
};

struct GuiStartedChangingParam {
    ParamIndex param;
};

struct GuiEndedChangingParam {
    ParamIndex param;
};

struct GuiNoteClicked {
    u7 key;
    f32 velocity;
};

struct GuiNoteClickReleased {
    u7 key;
};

struct RemoveMidiLearn {
    ParamIndex param;
    u7 midi_cc;
};

struct LayerInstrumentChanged {
    u32 layer_index;
};

struct AppendMacroDestination {
    f32 value {};
    ParamIndex param;
    u8 macro_index;
};

struct RemoveMacroDestination {
    u8 macro_index;
    u8 destination_index;
};

struct MacroDestinationValueChanged {
    f32 value;
    u8 macro_index;
    u8 destination_index;
};

using EventForAudioThread = TaggedUnion<
    EventForAudioThreadType,
    TypeAndTag<MainThreadChangedParam, EventForAudioThreadType::ParamChanged>,
    TypeAndTag<GuiStartedChangingParam, EventForAudioThreadType::ParamGestureBegin>,
    TypeAndTag<GuiEndedChangingParam, EventForAudioThreadType::ParamGestureEnd>,
    TypeAndTag<GuiNoteClicked, EventForAudioThreadType::StartNote>,
    TypeAndTag<GuiNoteClickReleased, EventForAudioThreadType::EndNote>,
    TypeAndTag<LayerInstrumentChanged, EventForAudioThreadType::LayerInstrumentChanged>,
    TypeAndTag<AppendMacroDestination, EventForAudioThreadType::AppendMacroDestination>,
    TypeAndTag<RemoveMacroDestination, EventForAudioThreadType::RemoveMacroDestination>,
    TypeAndTag<MacroDestinationValueChanged, EventForAudioThreadType::MacroDestinationValueChanged>>;

using EffectsArray = Array<Effect*, k_num_effect_types>;

void MoveEffectToNewSlot(EffectsArray& effects, Effect* effect_to_move, usize slot);
usize FindSlotInEffects(EffectsArray const& effects, Effect* fx);

u64 EncodeEffectsArray(EffectsArray const& arr);
u64 EncodeEffectsArray(Array<EffectType, k_num_effect_types> const& arr);
EffectsArray DecodeEffectsArray(u64 val, EffectsArray const& unordered_effects);

bool EffectIsOn(Parameters const& params, Effect*);

f32 AdjustedLinearValue(Parameters const& params,
                        MacroDestinations const& macro_destinations,
                        f32 linear_value,
                        ParamIndex param_index);

template <usize k_bits>
requires(k_bits != 0)
class AtomicBitset {
  public:
    static constexpr usize k_bits_per_element = sizeof(u64) * 8;
    static constexpr ptrdiff_t k_num_elements =
        (k_bits / k_bits_per_element) + ((k_bits % k_bits_per_element == 0) ? 0 : 1);
    using Bool64 = u64;

    AtomicBitset() {}

    void SetToValue(usize bit, bool value) {
        if (value)
            Set(bit);
        else
            Clear(bit);
    }

    Bool64 Clear(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchAnd(~mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Set(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchOr(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Flip(usize bit) {
        ASSERT(bit < k_bits);
        auto const mask = u64(1) << (bit % k_bits_per_element);
        return m_elements[bit / k_bits_per_element].FetchXor(mask, RmwMemoryOrder::Relaxed) & mask;
    }

    Bool64 Get(usize bit) const {
        ASSERT(bit < k_bits);
        return m_elements[bit / k_bits_per_element].Load(LoadMemoryOrder::Relaxed) &
               (u64(1) << bit % k_bits_per_element);
    }

    // NOTE: these Blockwise methods are not atomic in terms of the _whole_ bitset, but they will be atomic in
    // regard to each 64-bit block - and that might be good enough for some needs

    void AssignBlockwise(Bitset<k_bits> other) {
        auto const other_raw = other.elements;
        for (auto const element_index : Range(m_elements.size))
            m_elements[element_index].Store(other_raw[element_index], StoreMemoryOrder::Relaxed);
    }

    Bitset<k_bits> GetBlockwise() const {
        Bitset<k_bits> result;
        for (auto const element_index : Range(m_elements.size))
            result.elements[element_index] = m_elements[element_index].Load(LoadMemoryOrder::Relaxed);
        return result;
    }

    void SetAllBlockwise() {
        for (auto& block : m_elements)
            block.store(~(u64)0);
    }

    void ClearAllBlockwise() {
        for (auto& block : m_elements)
            block.store(0);
    }

  private:
    Array<Atomic<u64>, k_num_elements> m_elements {};
};

struct ProcessorListener {
    enum : u32 {
        None = 0,
        StatusChanged = 1 << 1,
        InstrumentChanged = 1 << 2,
        NotesChanged = 1 << 3,
        IrChanged = 1 << 4,
        PeakMeterChanged = 1 << 5,
        ParametersChanged = 1 << 6,
    };
    using ChangeFlags = u32;
    virtual void OnProcessorChange(ChangeFlags) = 0; // called from audio thread
    virtual ~ProcessorListener() = default;
};

struct AudioProcessor {
    AudioProcessor(clap_host const& host,
                   ProcessorListener& listener,
                   prefs::PreferencesTable const& preferences);
    ~AudioProcessor();

    clap_host const& host;

    AudioProcessingContext audio_processing_context;

    ProcessorListener& listener;

    Bitset<k_num_layers> restart_voices_for_layer_bitset {};
    bool fx_need_another_frame_of_processing = {};

    // IMPROVE: rather than have atomics here for the ccs, would FIFO communication be better?
    Array<AtomicBitset<128>, k_num_parameters> param_learned_ccs {};
    Array<Atomic<TimePoint>, k_num_parameters> time_when_cc_moved_param {};

    Atomic<OptionalIndex<s32>> midi_learn_param_index {};

    enum class FadeType { None, OutAndIn, OutAndRestartVoices };
    FadeType whole_engine_volume_fade_type {};
    VolumeFade whole_engine_volume_fade {};

    u32 previous_block_size = 0;

    StereoPeakMeter peak_meter = {};

    SharedLayerParams shared_layer_params {};
    Bitset<k_num_layers> solo {};
    Bitset<k_num_layers> mute {};

    AtomicQueue<EventForAudioThread, 128> events_for_audio_thread;
    AtomicQueue<EventForAudioThread, NextPowerOf2(k_num_parameters * 2)> param_events_for_audio_thread;

    Bitset<k_num_parameters> pending_param_changes;

    AtomicBitset<128> notes_currently_held;

    clap_process_status previous_process_status {-1};

    VoicePool voice_pool {};

    Parameters audio_params; // Audio-thread representation of the parameters.
    Parameters main_params; // Main-thread representation of the parameters.

    Parameters audio_macro_adjusted_params;

    // Main-thread. Macro configurations can only be modified from the main thread.
    MacroDestinations main_macro_destinations {};
    MacroDestinations audio_macro_destinations {};

    struct ChangedParam {
        f32 value;
        ParamIndex index;
    };
    AtomicQueue<ChangedParam, NextPowerOf2(k_num_parameters)> param_changes_for_main_thread {};

    Array<LayerProcessor, k_num_layers> layer_processors {
        LayerProcessor {0, host, shared_layer_params},
        LayerProcessor {1, host, shared_layer_params},
        LayerProcessor {2, host, shared_layer_params},
    };
    DynamicArray<sample_lib_server::RefCounted<sample_lib::LoadedInstrument>> lifetime_extended_insts {
        Malloc::Instance()};

    f32 master_vol;
    OnePoleLowPassFilter<f32> master_vol_smoother;

    Distortion distortion;
    BitCrush bit_crush;
    Compressor compressor;
    FilterEffect filter_effect;
    StereoWiden stereo_widen;
    Chorus chorus;
    Reverb reverb;
    Delay delay;
    Phaser phaser;
    ConvolutionReverb convo;

    // The effects indexable by EffectType
    EffectsArray const effects_ordered_by_type;

    Atomic<u64> desired_effects_order {EncodeEffectsArray(effects_ordered_by_type)};
    EffectsArray actual_fx_order {effects_ordered_by_type};

    bool activated = false;
};

extern PluginCallbacks<AudioProcessor> const g_processor_callbacks;

enum class ProcessorSetting {
    DefaultCcParamMappings,
};

prefs::Descriptor SettingDescriptor(ProcessorSetting);

void SetInstrument(AudioProcessor& processor, u32 layer_index, Instrument const& instrument);
void SetConvolutionIrAudioData(AudioProcessor& processor,
                               AudioData const* audio_data,
                               sample_lib::ImpulseResponse::AudioProperties const& audio_props);

// doesn't set instruments or convolution because they require loaded audio data which is often available at a
// later time
void ApplyNewState(AudioProcessor& processor, StateSnapshot const& state, StateSource source);

StateSnapshot MakeStateSnapshot(AudioProcessor const& processor);

void ParameterJustStartedMoving(AudioProcessor& processor, ParamIndex index);
void ParameterJustStoppedMoving(AudioProcessor& processor, ParamIndex index);

struct ParamChangeFlags {
    u8 host_should_not_record : 1;
};

bool SetParameterValue(AudioProcessor& processor, ParamIndex index, f32 value, ParamChangeFlags flags);

bool LayerIsSilent(AudioProcessor const& processor, u32 layer_index);

void SetAllParametersToDefaultValues(AudioProcessor&);
void RandomiseAllParameterValues(AudioProcessor&);
void RandomiseAllEffectParameterValues(AudioProcessor&);

bool IsMidiCCLearnActive(AudioProcessor const& processor);
void LearnMidiCC(AudioProcessor& processor, ParamIndex param);
void CancelMidiCCLearn(AudioProcessor& processor);
void UnlearnMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num_to_remove);
Bitset<128> GetLearnedCCsBitsetForParam(AudioProcessor const& processor, ParamIndex param);
bool CcControllerMovedParamRecently(AudioProcessor const& processor, ParamIndex param);

void AddPersistentCcToParamMapping(prefs::Preferences& preferences, u8 cc_num, u32 param_id);
void RemovePersistentCcToParamMapping(prefs::Preferences& preferences, u8 cc_num, u32 param_id);
Bitset<128> PersistentCcsForParam(prefs::PreferencesTable const& preferences, u32 param_id);

void AppendMacroDestination(AudioProcessor& processor, AppendMacroDestination config);
void RemoveMacroDestination(AudioProcessor& processor, RemoveMacroDestination config);

// Doesn't actually change the value, just sends the event to the audio thread.
void MacroDestinationValueChanged(AudioProcessor& processor, MacroDestinationValueChanged config);
