// Copyright 2018-2026 Sam Windell
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

#include "atomic_bitset.hpp"
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

// NOTE: in the audio processor, we use the term 'inbox' to mean a message that the audio thread should
// consume. For example, we put parameter value updates in an atomic 'inbox' that the audio thread consumes
// when it can.

namespace audio_thread_inbox {

struct ParamChange {
    struct Payload {
        f32 value {};
        u32 active : 1 = false;
        u32 value_changed : 1 = false;
        u32 host_should_record : 1 = false;
        u32 send_to_host : 1 = false;
        u32 gui_gesture_begin : 1 = false;
        u32 gui_gesture_end : 1 = false;
    };

    // Audio thread.
    Optional<Payload> Consume() {
        auto const p = payload.Load(LoadMemoryOrder::Relaxed);
        if (!(p.active)) return k_nullopt;

        return payload.Exchange({}, RmwMemoryOrder::Acquire);
    }

    // Audio thread.
    void Clear() { payload.Store({}, StoreMemoryOrder::Relaxed); }

    struct ValueChangedOptions {
        bool send_to_host = false;
        bool host_should_record = false;
    };

    // Main thread.
    // We deliberately take an additive approach as much as possible rather than overwriting fields so that
    // multiple changes can be safely combined.
    void AddValueChanged(f32 value, ValueChangedOptions const& options) {
        auto p = payload.Load(LoadMemoryOrder::Relaxed);
        p.value = value;
        p.active = true;
        p.value_changed = true;
        if (options.send_to_host) p.send_to_host = true;
        if (options.host_should_record) p.host_should_record = true;
        payload.Store(p, StoreMemoryOrder::Release);
    }

    enum class GuiGestureType { Begin, End };

    // Main thread.
    // Additive approach (see above).
    void AddGuiGesture(GuiGestureType type) {
        auto p = payload.Load(LoadMemoryOrder::Relaxed);
        p.active = true;
        switch (type) {
            case GuiGestureType::Begin: p.gui_gesture_begin = true; break;
            case GuiGestureType::End: p.gui_gesture_end = true; break;
        }
        payload.Store(p, StoreMemoryOrder::Release);
    }

    Atomic<Payload> payload {};
};

struct MacroDestinationUpdate {
    struct Payload {
        f32 value {}; // ONLY valid if active and value_changed.
        ParamIndex param_index {}; // ONLY valid if active and param_index_changed.
        u8 active : 1 = false;
        u8 param_index_changed : 1 = false;
        u8 value_changed : 1 = false;
        u8 clear : 1 = false;
    };

    // Audio thread.
    Optional<Payload> Consume() {
        auto const p = payload.Load(LoadMemoryOrder::Relaxed);
        if (!(p.active)) return k_nullopt;

        return payload.Exchange({}, RmwMemoryOrder::Acquire);
    }

    // Audio thread.
    void Clear() { payload.Store({}, StoreMemoryOrder::Relaxed); }

    struct ProduceOptions {
        Optional<f32> new_value;
        Optional<ParamIndex> new_param_index;
        bool clear;
    };

    // Main thread.
    void Produce(ProduceOptions const& options) {
        auto p = payload.Load(LoadMemoryOrder::Relaxed);
        p.active = true;
        if (options.new_value) {
            p.value = *options.new_value;
            p.value_changed = true;
        }
        if (options.new_param_index) {
            p.param_index = *options.new_param_index;
            p.param_index_changed = true;
        }
        p.clear = options.clear;
        payload.Store(p, StoreMemoryOrder::Release);
    }

    Atomic<Payload> payload {};
};

using Flags = u32;

enum : u32 {
    // IMPORTANT: this is actually multiple bits - one for each layer index. Set using
    // LayerInstrumentChanged << layer_index.
    LayerInstrumentChanged = 1 << 0,

    FxOrderChanged = 1 << (k_num_layers + 0),
    ReloadAllAudioState = 1 << (k_num_layers + 1),
    ConvolutionIRChanged = 1 << (k_num_layers + 2),
    ResetAudioProcessing = 1 << (k_num_layers + 3),
};

} // namespace audio_thread_inbox

struct GuiNoteClickState {
    f32 velocity {};
    u7 key {};
    bool is_held;
};

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
    virtual void OnProcessorChange(ChangeFlags) = 0; // Called from EITHER audio or main thread.
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

    Array<audio_thread_inbox::ParamChange, k_num_parameters> param_change_inbox;

    Atomic<GuiNoteClickState> gui_note_click_state {}; // Written by main-thread, read by audio.
    Optional<u7> gui_note_currently_held {}; // Audio-thread

    Bitset<k_num_parameters> pending_param_changes;

    AtomicBitset<128> notes_currently_held;

    Atomic<audio_thread_inbox::Flags> inbox_flags {}; // From main-thread to audio.

    clap_process_status previous_process_status {-1};

    VoicePool voice_pool {};

    Parameters audio_params; // Audio-thread representation of the parameters.
    Parameters main_params; // Main-thread representation of the parameters.

    Parameters audio_macro_adjusted_params;

    // Main-thread. Macro configurations can only be modified from the main thread.
    MacroDestinations main_macro_destinations {};

    // Audio-thread representation of macro dests.
    MacroDestinations audio_macro_destinations {};

    // Atomic communication for macro dests. Set by main-thread, consumed by audio.
    Array<Array<audio_thread_inbox::MacroDestinationUpdate, k_max_macro_destinations>, k_num_macros>
        macro_dest_inbox {};

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
    DynamicArray<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>> lifetime_extended_insts {
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
void ResetAudioProcessing(AudioProcessor&);

bool IsMidiCCLearnActive(AudioProcessor const& processor);
void LearnMidiCC(AudioProcessor& processor, ParamIndex param);
void CancelMidiCCLearn(AudioProcessor& processor);
void UnlearnMidiCC(AudioProcessor& processor, ParamIndex param, u7 cc_num_to_remove);
Bitset<128> GetLearnedCCsBitsetForParam(AudioProcessor const& processor, ParamIndex param);
bool CcControllerMovedParamRecently(AudioProcessor const& processor, ParamIndex param);

void PinCcToParam(prefs::Preferences& preferences, u8 cc_num, u32 param_id);
void UnpinCcFromParam(prefs::Preferences& preferences, u8 cc_num, u32 param_id);
Bitset<128> PinnedCcsForParam(prefs::PreferencesTable const& preferences, u32 param_id);

void UnlearnAndUnpinMidiCC(AudioProcessor& processor, prefs::Preferences& prefs, ParamIndex param, u7 cc_num);

struct AppendMacroDestinationConfig {
    ParamIndex param;
    u8 macro_index;
};
void AppendMacroDestination(AudioProcessor& processor, AppendMacroDestinationConfig config);

struct RemoveMacroDestinationConfig {
    u8 macro_index;
    u8 destination_index;
};
void RemoveMacroDestination(AudioProcessor& processor, RemoveMacroDestinationConfig config);

struct MacroDestinationValueChangedConfig {
    f32 value;
    u8 macro_index;
    u8 destination_index;
};

// Doesn't actually change the value, just sends the event to the audio thread.
void MacroDestinationValueChanged(AudioProcessor& processor, MacroDestinationValueChangedConfig config);
