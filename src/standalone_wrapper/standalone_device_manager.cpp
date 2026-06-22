// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "standalone_device_manager.hpp"

#include <FLAC/stream_encoder.h>

#include "foundation/memory/allocators.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/error_reporting.hpp"

static bool RenderEnabled(DeviceManager const& dm) { return dm.audio.render_flac_path.HasValue(); }

constexpr persistent_store::Id k_audio_device_store_key = HashFnv1a("standalone-audio-output-device");
constexpr persistent_store::Id k_midi_device_store_key = HashFnv1a("standalone-midi-input-device");
constexpr persistent_store::Id k_audio_backend_store_key = HashFnv1a("standalone-audio-backend");

constexpr u64 k_audio_error_id = HashFnv1a("standalone-audio-error");
constexpr u64 k_midi_error_id = HashFnv1a("standalone-midi-error");

static void ReportAudioError(DeviceManager& dm, String selected_name, String cause) {
    ASSERT(g_is_logical_main_thread);
    dm.audio.failed = true;
    if (auto* item = dm.error_notifications.BeginWriteError(k_audio_error_id)) {
        fmt::Assign(item->title, "Audio device unavailable");
        if (selected_name.size)
            fmt::Assign(item->message, "Could not open audio device \"{}\". {}", selected_name, cause);
        else
            fmt::Assign(item->message, "Could not open the default audio device. {}", cause);
        ThreadsafeErrorNotifications::EndWriteError(*item);
    }
}

static void ClearAudioError(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    dm.audio.failed = false;
    dm.error_notifications.RemoveError(k_audio_error_id);
}

static void ReportMidiError(DeviceManager& dm, String selected_name, String cause) {
    ASSERT(g_is_logical_main_thread);
    dm.midi.failed = true;
    if (auto* item = dm.error_notifications.BeginWriteError(k_midi_error_id)) {
        fmt::Assign(item->title, "No MIDI input");
        if (selected_name.size)
            fmt::Assign(item->message, "Could not open MIDI input \"{}\". {}", selected_name, cause);
        else
            fmt::Assign(item->message, "No MIDI input device available. {}", cause);
        ThreadsafeErrorNotifications::EndWriteError(*item);
    }
}

static void ClearMidiError(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    dm.midi.failed = false;
    dm.error_notifications.RemoveError(k_midi_error_id);
}

static void
AudioCallback(ma_device* device, void* output_buffer, void const* input, ma_uint32 num_buffer_frames) {
    (void)input;
    auto* dm = (DeviceManager*)device->pUserData;
    if (!dm) return;

    using MidiState = DeviceManager::Midi::Stream::State;

    if (!dm->audio.stream.thread_setup_done.Load(LoadMemoryOrder::Relaxed)) {
        dm->audio.stream.thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
        SetThreadName("audio", false);
        dm->audio.stream.thread_setup_done.Store(true, StoreMemoryOrder::Relaxed);
    }
    ASSERT_HOT(CurrentThreadId() == dm->audio.stream.thread_id.Load(LoadMemoryOrder::Relaxed));

    constexpr usize k_max_events = 128;
    PmEvent pm_events[k_max_events];
    clap_event_midi clap_events[k_max_events];
    int num_events = 0;

    auto const push_clap_event = [&](u8 status, u8 d1, u8 d2) {
        if (num_events >= (int)k_max_events) return;
        auto const force_zero = dm->midi.force_channel_zero && status < 0xf0;
        clap_events[num_events] = {
            .header =
                {
                    .size = sizeof(clap_event_midi),
                    .time = 0,
                    .type = CLAP_EVENT_MIDI,
                    .flags = CLAP_EVENT_IS_LIVE,
                },
            .port_index = 0,
            .data = {force_zero ? (u8)(status & 0b11110000) : status, d1, d2},
        };
        ++num_events;
    };

    // Claim the stream for the duration of the read; CloseMidiStream waits for the claim to clear
    // before Pm_Close. CAS failure means the stream is closed (or being closed) — skip.
    auto midi_state = MidiState::Open;
    if (dm->midi.stream.state.CompareExchangeStrong(midi_state,
                                                    MidiState::Reading,
                                                    RmwMemoryOrder::AcquireRelease,
                                                    LoadMemoryOrder::Relaxed)) {
        ASSERT_HOT(dm->midi.stream.handle);
        auto const n = Pm_Read(dm->midi.stream.handle, pm_events, (int)k_max_events);
        dm->midi.stream.state.Store(MidiState::Open, StoreMemoryOrder::Release);
        // Errors are recoverable: pmBufferOverflow is expected when nothing drained the stream for
        // a while (e.g. MIDI played during an audio device swap). Drop the data and carry on.
        for (auto const i : Range(n < 0 ? 0 : n))
            push_clap_event((u8)Pm_MessageStatus(pm_events[i].message),
                            (u8)Pm_MessageData1(pm_events[i].message),
                            (u8)Pm_MessageData2(pm_events[i].message));
    }

    DeviceManager::Midi::RawMessage injected;
    while (num_events < (int)k_max_events && dm->midi.injected.Pop(injected))
        push_clap_event(injected.status, injected.data1, injected.data2);

    if (!dm->host_callbacks.render) return;

    // JACK can deliver more frames than it reported at init (see k_max_audio_buffer_frames), so
    // render in bounded chunks rather than overrun buffers sized for the reported maximum.
    auto* output = (f32*)output_buffer;
    u32 frames_done = 0;
    while (frames_done < num_buffer_frames) {
        auto const chunk_frames = Min(num_buffer_frames - frames_done, (u32)k_max_audio_buffer_frames);
        dm->host_callbacks.render(dm->host_callbacks.ctx,
                                  output + (frames_done * 2),
                                  chunk_frames,
                                  frames_done == 0 ? clap_events : nullptr,
                                  frames_done == 0 ? (u32)num_events : 0);
        frames_done += chunk_frames;
    }

    if (auto* enc = dm->audio.render_encoder) {
        constexpr u32 k_chunk_frames = 512;
        FLAC__int32 buf[k_chunk_frames * 2];
        u32 written = 0;
        while (written < num_buffer_frames) {
            auto const n = Min(num_buffer_frames - written, k_chunk_frames);
            for (u32 i = 0; i < n * 2; ++i)
                buf[i] = (FLAC__int32)(Clamp(output[(written * 2) + i], -1.0f, 1.0f) * 32767.0f);
            FLAC__stream_encoder_process_interleaved(enc, buf, n);
            written += n;
        }
    }
}

static Optional<ma_backend> FindAudioBackend(String name) {
    ASSERT(g_is_logical_main_thread);
    if (name.size == 0) return k_nullopt;
    ma_backend enabled[MA_BACKEND_COUNT];
    size_t count = 0;
    if (ma_get_enabled_backends(enabled, MA_BACKEND_COUNT, &count) != MA_SUCCESS) return k_nullopt;
    for (auto const i : Range(count))
        if (FromNullTerminated(ma_get_backend_name(enabled[i])) == name) return enabled[i];
    return k_nullopt;
}

static void EnumerateAudioBackends(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (RenderEnabled(dm)) {
        dm.audio.backends = {};
        dm.audio.backend_ids = {};
        return;
    }
    ma_backend enabled[MA_BACKEND_COUNT];
    size_t count = 0;
    if (ma_get_enabled_backends(enabled, MA_BACKEND_COUNT, &count) != MA_SUCCESS) {
        dm.audio.backends = {};
        dm.audio.backend_ids = {};
        return;
    }

    // Exclude ma_backend_null (test-only) and ma_backend_custom (no callbacks supplied).
    u32 num_visible = 0;
    for (auto const i : Range(count))
        if (enabled[i] != ma_backend_null && enabled[i] != ma_backend_custom) ++num_visible;

    auto infos = dm.enum_arena.AllocateExactSizeUninitialised<HostDeviceInfo>(num_visible);
    auto ids = dm.enum_arena.AllocateExactSizeUninitialised<ma_backend>(num_visible);
    u32 n = 0;
    for (auto const i : Range(count)) {
        if (enabled[i] == ma_backend_null || enabled[i] == ma_backend_custom) continue;
        auto const name = dm.enum_arena.Clone(FromNullTerminated(ma_get_backend_name(enabled[i])));
        infos[n] = {.id = name, .name = name, .is_default = false};
        ids[n] = enabled[i];
        ++n;
    }
    dm.audio.backends = infos;
    dm.audio.backend_ids = ids;
}

static Optional<ma_device_id> FindAudioDeviceId(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    if (name.size == 0) return k_nullopt;
    for (auto const i : Range(dm.audio.devices.size))
        if (dm.audio.devices[i].name == name) return dm.audio.device_ids[i];
    return k_nullopt;
}

static Optional<PmDeviceID> FindMidiDeviceId(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    if (name.size == 0) return k_nullopt;
    for (auto const i : Range(dm.midi.devices.size))
        if (dm.midi.devices[i].name == name) return dm.midi.device_ids[i];
    return k_nullopt;
}

static void EnumerateAudioDevices(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (!dm.audio.context_initialised) {
        bool ok = false;
        Optional<ma_backend> chosen_backend {};
        if (RenderEnabled(dm)) {
            ma_backend backends[1] = {ma_backend_null};
            ok = ma_context_init(backends, 1, nullptr, &dm.audio.context) == MA_SUCCESS;
            ASSERT(ok, "ma_backend_null must be available in render mode");
        } else {
            chosen_backend = FindAudioBackend(dm.audio.selected_backend_name);
            if (chosen_backend) {
                ma_backend backends[1] = {*chosen_backend};
                ok = ma_context_init(backends, 1, nullptr, &dm.audio.context) == MA_SUCCESS;
            }
            if (!ok) ok = ma_context_init(nullptr, 0, nullptr, &dm.audio.context) == MA_SUCCESS;
        }
        if (!ok) {
            if (chosen_backend)
                ReportAudioError(
                    dm,
                    {},
                    "Failed to initialise the selected audio backend; falling back failed too."_s);
            else
                ReportAudioError(dm, {}, "Failed to initialise the audio backend."_s);
            dm.audio.devices = {};
            dm.audio.device_ids = {};
            return;
        }
        dm.audio.context_initialised = true;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    if (ma_context_get_devices(&dm.audio.context, &playback_infos, &playback_count, nullptr, nullptr) !=
        MA_SUCCESS) {
        ReportAudioError(dm, {}, "Failed to enumerate audio devices."_s);
        dm.audio.devices = {};
        dm.audio.device_ids = {};
        return;
    }

    auto infos = dm.enum_arena.AllocateExactSizeUninitialised<HostDeviceInfo>(playback_count);
    auto ids = dm.enum_arena.AllocateExactSizeUninitialised<ma_device_id>(playback_count);
    for (auto const i : Range(playback_count)) {
        auto const name = dm.enum_arena.Clone(FromNullTerminated(playback_infos[i].name));
        infos[i] = {.id = name, .name = name, .is_default = (bool)playback_infos[i].isDefault};
        ids[i] = playback_infos[i].id;
    }
    dm.audio.devices = infos;
    dm.audio.device_ids = ids;
}

static void EnumerateMidiDevices(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (!dm.midi.portmidi_initialised) {
        dm.midi.devices = {};
        dm.midi.device_ids = {};
        return;
    }
    auto const count = Pm_CountDevices();
    auto const default_id = Pm_GetDefaultInputDeviceID();

    u32 num_inputs = 0;
    for (auto const i : Range(count))
        if (auto const info = Pm_GetDeviceInfo(i); info && info->input) ++num_inputs;

    auto infos = dm.enum_arena.AllocateExactSizeUninitialised<HostDeviceInfo>(num_inputs);
    auto ids = dm.enum_arena.AllocateExactSizeUninitialised<PmDeviceID>(num_inputs);
    u32 n = 0;
    for (auto const i : Range(count)) {
        auto const info = Pm_GetDeviceInfo(i);
        if (!info || !info->input) continue;
        auto const name = dm.enum_arena.Clone(FromNullTerminated(info->name));
        infos[n] = {.id = name, .name = name, .is_default = i == default_id};
        ids[n] = i;
        ++n;
    }
    dm.midi.devices = infos;
    dm.midi.device_ids = ids;
}

static void EnumerateAllDevices(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    dm.enum_arena.ResetCursorAndConsolidateRegions();
    EnumerateAudioBackends(dm);
    EnumerateAudioDevices(dm);
    EnumerateMidiDevices(dm);
}

static void AudioNotificationCallback(ma_device_notification const* notification) {
    if (!notification || !notification->pDevice) return;
    if (notification->type != ma_device_notification_type_stopped) return;
    auto* dm = (DeviceManager*)notification->pDevice->pUserData;
    if (!dm) return;
    if (dm->audio.stream.suppress_stop_notification.Load(LoadMemoryOrder::Acquire)) return;
    dm->audio.stream.lost.Store(true, StoreMemoryOrder::Release);
}

static void FinaliseFlacEncoder(DeviceManager& dm) {
    if (!dm.audio.render_encoder) return;
    FLAC__stream_encoder_finish(dm.audio.render_encoder);
    FLAC__stream_encoder_delete(dm.audio.render_encoder);
    dm.audio.render_encoder = nullptr;
}

static void CloseAudio(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (!dm.audio.stream.device) return;

    dm.audio.stream.suppress_stop_notification.Store(true, StoreMemoryOrder::Release);
    DEFER { dm.audio.stream.suppress_stop_notification.Store(false, StoreMemoryOrder::Release); };
    ma_device_uninit(&*dm.audio.stream.device);
    dm.audio.stream.device = k_nullopt;

    // ma_device_uninit blocks until the audio thread has stopped, so the encoder is no longer in
    // use here.
    FinaliseFlacEncoder(dm);

    dm.audio.stream.thread_setup_done.Store(false, StoreMemoryOrder::Relaxed);
    dm.audio.stream.lost.Store(false, StoreMemoryOrder::Release);
}

static bool InitFlacEncoder(DeviceManager& dm) {
    ASSERT(!dm.audio.render_encoder);
    auto* enc = FLAC__stream_encoder_new();
    if (!enc) return false;
    FLAC__stream_encoder_set_channels(enc, 2);
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, dm.audio.stream.device->sampleRate);
    FLAC__stream_encoder_set_compression_level(enc, 5);

    auto const path = NullTerminated(*dm.audio.render_flac_path, dm.enum_arena);
    auto const status = FLAC__stream_encoder_init_file(enc, path, nullptr, nullptr);
    if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        LogError(ModuleName::Standalone, "FLAC encoder init failed ({})", (int)status);
        FLAC__stream_encoder_delete(enc);
        return false;
    }
    dm.audio.render_encoder = enc;
    return true;
}

static bool InitAudioDevice(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(!dm.audio.stream.device);
    if (!dm.audio.context_initialised) return false;

    auto const chosen_id = FindAudioDeviceId(dm, name);

    dm.audio.stream.device.Emplace();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.playback.pDeviceID = chosen_id ? &*chosen_id : nullptr;
    config.sampleRate = RenderEnabled(dm) ? 44100u : 0u; // 0 = use device default
    config.dataCallback = AudioCallback;
    config.notificationCallback = AudioNotificationCallback;
    config.pUserData = &dm;
    config.periodSizeInFrames = 1024; // only a hint
    config.performanceProfile = ma_performance_profile_low_latency;
    config.noFixedSizedCallback = false;
    config.noClip = true;
    config.noPreSilencedOutputBuffer = false;

    ASSERT(!dm.audio.stream.thread_setup_done.Load(LoadMemoryOrder::Relaxed));
    ASSERT(!dm.audio.stream.lost.Load(LoadMemoryOrder::Relaxed));
    ASSERT(!dm.audio.stream.suppress_stop_notification.Load(LoadMemoryOrder::Relaxed));

    if (ma_device_init(&dm.audio.context, &config, &*dm.audio.stream.device) != MA_SUCCESS) {
        dm.audio.stream.device = k_nullopt;
        return false;
    }
    if (dm.audio.stream.device->playback.internalPeriodSizeInFrames > k_max_audio_buffer_frames) {
        LogError(ModuleName::Standalone,
                 "audio device period size {} exceeds maximum {}",
                 dm.audio.stream.device->playback.internalPeriodSizeInFrames,
                 k_max_audio_buffer_frames);
        CloseAudio(dm);
        return false;
    }

    if (RenderEnabled(dm) && !InitFlacEncoder(dm)) {
        CloseAudio(dm);
        return false;
    }

    return true;
}

static bool OpenAudioDeviceWithFallback(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    bool const preferred_missing = name.size && !FindAudioDeviceId(dm, name);
    auto const first_try = preferred_missing ? String {} : name;
    bool ok = InitAudioDevice(dm, first_try);
    bool open_failed_fallback = false;
    if (!ok && first_try.size) {
        ok = InitAudioDevice(dm, {});
        open_failed_fallback = ok;
    }

    if (!ok)
        ReportAudioError(dm, name, "Could not open the audio device."_s);
    else if (preferred_missing)
        ReportAudioError(dm, name, "Selected audio device is not connected; using system default."_s);
    else if (open_failed_fallback)
        ReportAudioError(dm, name, "Could not open the selected audio device; using system default."_s);
    else
        ClearAudioError(dm);
    return ok;
}

static void ActivatePluginAndStartAudio(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(dm.audio.stream.device);
    if (dm.host_callbacks.activate_after_device_swap)
        dm.host_callbacks.activate_after_device_swap(
            dm.host_callbacks.ctx,
            dm.audio.stream.device->sampleRate,
            dm.audio.stream.device->playback.internalPeriodSizeInFrames,
            (u32)k_max_audio_buffer_frames);
    if (ma_device_start(&*dm.audio.stream.device) != MA_SUCCESS) {
        CloseAudio(dm);
        ReportAudioError(dm, dm.audio.selected_name, "Could not start the audio device."_s);
    }
}

static void OpenMidiStream(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    if (dm.midi.skip_device_input) return;
    if (!dm.midi.portmidi_initialised) return;
    ASSERT(dm.midi.stream.handle == nullptr);
    ASSERT(dm.midi.stream.state.Load(LoadMemoryOrder::Relaxed) == DeviceManager::Midi::Stream::State::Closed);

    Optional<PmDeviceID> id {};
    if (name.size) {
        id = FindMidiDeviceId(dm, name);
        if (!id) {
            ReportMidiError(dm, name, "Selected MIDI device is not connected."_s);
            return;
        }
    } else {
        if (auto const def = Pm_GetDefaultInputDeviceID(); def != pmNoDevice) {
            if (auto const info = Pm_GetDeviceInfo(def); info && info->input) id = def;
        }
        if (!id && dm.midi.device_ids.size) id = dm.midi.device_ids[0];
        if (!id) {
            ReportMidiError(dm, name, "No MIDI input devices were found."_s);
            return;
        }
    }

    if (auto result = Pm_OpenInput(&dm.midi.stream.handle, *id, nullptr, 200, nullptr, nullptr);
        result != pmNoError) {
        dm.midi.stream.handle = nullptr;
        ReportMidiError(dm, name, FromNullTerminated(Pm_GetErrorText(result)));
        return;
    }
    ClearMidiError(dm);
    using MidiState = DeviceManager::Midi::Stream::State;
    dm.midi.stream.state.Store(MidiState::Open, StoreMemoryOrder::Release);
}

static void CloseMidiStream(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (!dm.midi.stream.handle) return;
    using MidiState = DeviceManager::Midi::Stream::State;
    while (true) {
        auto expected = MidiState::Open;
        if (dm.midi.stream.state.CompareExchangeStrong(expected,
                                                       MidiState::Closed,
                                                       RmwMemoryOrder::AcquireRelease,
                                                       LoadMemoryOrder::Relaxed))
            break;
        ASSERT(expected == MidiState::Reading);
        SleepThisThread(1u);
    }
    Pm_Close(dm.midi.stream.handle);
    dm.midi.stream.handle = nullptr;
}

static void SaveDeviceSelection(DeviceManager& dm, persistent_store::Id key, String name) {
    ASSERT(g_is_logical_main_thread);
    if (!dm.store) return;
    persistent_store::RemoveValue(*dm.store, key, k_nullopt);
    if (name.size) persistent_store::AddValue(*dm.store, key, name.ToConstByteSpan());
}

static Optional<String> StorePath(ArenaAllocator& arena) {
    DynamicArrayBounded<char, Kb(1)> error_log;
    auto writer = dyn::WriterFor(error_log);

    auto const result = KnownDirectoryWithSubdirectories(arena,
                                                         KnownDirectoryType::UserData,
                                                         Array {"Floe"_s},
                                                         "standalone_persistent_store",
                                                         {.create = true, .error_log = &writer});
    if (error_log.size) {
        ReportError(ErrorLevel::Warning,
                    HashFnv1a("persistent store path"),
                    "Failed to get standalone persistent store {}\n{}",
                    error_log);
        return k_nullopt;
    }

    return result;
}

static void SetupDeviceStore(DeviceManager& dm, ArenaAllocator& arena) {
    ASSERT(g_is_logical_main_thread);

    auto const path = StorePath(arena);
    if (!path) return;

    PLACEMENT_NEW(&dm.store.storage) persistent_store::Store {.filepath = *path};
    dm.store.has_value = true;

    if (auto const r = persistent_store::Get(*dm.store, k_audio_device_store_key);
        r.tag == persistent_store::GetResult::Found) {
        auto const data = r.Get<persistent_store::Value const*>()->data;
        dyn::Assign(dm.audio.selected_name, String {(char const*)data.data, data.size});
    }
    if (auto const r = persistent_store::Get(*dm.store, k_midi_device_store_key);
        r.tag == persistent_store::GetResult::Found) {
        auto const data = r.Get<persistent_store::Value const*>()->data;
        dyn::Assign(dm.midi.selected_name, String {(char const*)data.data, data.size});
    }
    if (auto const r = persistent_store::Get(*dm.store, k_audio_backend_store_key);
        r.tag == persistent_store::GetResult::Found) {
        auto const data = r.Get<persistent_store::Value const*>()->data;
        dyn::Assign(dm.audio.selected_backend_name, String {(char const*)data.data, data.size});
    }
}

static void InitPortMidi(DeviceManager& dm) {
    if (dm.midi.skip_device_input) return;
    if (auto result = Pm_Initialize(); result != pmNoError)
        ReportMidiError(dm, {}, FromNullTerminated(Pm_GetErrorText(result)));
    else
        dm.midi.portmidi_initialised = true;
}

void InitDevices(DeviceManager& dm, ArenaAllocator& arena) {
    ASSERT(g_is_logical_main_thread);

    SetupDeviceStore(dm, arena);
    InitPortMidi(dm);
    EnumerateAllDevices(dm);

    if (OpenAudioDeviceWithFallback(dm, dm.audio.selected_name)) ActivatePluginAndStartAudio(dm);

    OpenMidiStream(dm, dm.midi.selected_name);
}

void DeinitDevices(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    CloseAudio(dm);
    if (dm.audio.context_initialised) ma_context_uninit(&dm.audio.context);
    CloseMidiStream(dm);
    if (dm.midi.portmidi_initialised) Pm_Terminate();
}

static void ApplyAudioSelection(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    dyn::Assign(dm.audio.selected_name, name);
    SaveDeviceSelection(dm, k_audio_device_store_key, name);

    CloseAudio(dm);
    if (dm.host_callbacks.deactivate_for_device_swap)
        dm.host_callbacks.deactivate_for_device_swap(dm.host_callbacks.ctx);

    if (OpenAudioDeviceWithFallback(dm, name)) ActivatePluginAndStartAudio(dm);
}

static void ApplyMidiSelection(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    if (dm.midi.skip_device_input) return;
    dyn::Assign(dm.midi.selected_name, name);
    SaveDeviceSelection(dm, k_midi_device_store_key, name);
    CloseMidiStream(dm);
    OpenMidiStream(dm, name);
}

static void ApplyBackendSelection(DeviceManager& dm, String name) {
    ASSERT(g_is_logical_main_thread);
    dyn::Assign(dm.audio.selected_backend_name, name);
    SaveDeviceSelection(dm, k_audio_backend_store_key, name);

    CloseAudio(dm);
    if (dm.host_callbacks.deactivate_for_device_swap)
        dm.host_callbacks.deactivate_for_device_swap(dm.host_callbacks.ctx);

    if (dm.audio.context_initialised) {
        ma_context_uninit(&dm.audio.context);
        dm.audio.context_initialised = false;
    }

    EnumerateAllDevices(dm);

    if (dm.audio.context_initialised && OpenAudioDeviceWithFallback(dm, dm.audio.selected_name))
        ActivatePluginAndStartAudio(dm);
}

static void ApplyRefresh(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    CloseAudio(dm);
    if (dm.host_callbacks.deactivate_for_device_swap)
        dm.host_callbacks.deactivate_for_device_swap(dm.host_callbacks.ctx);

    if (dm.audio.context_initialised) {
        ma_context_uninit(&dm.audio.context);
        dm.audio.context_initialised = false;
    }

    if (!dm.midi.skip_device_input) {
        CloseMidiStream(dm);
        ClearMidiError(dm);
        if (dm.midi.portmidi_initialised) {
            Pm_Terminate();
            dm.midi.portmidi_initialised = false;
        }
        InitPortMidi(dm);
    }

    EnumerateAllDevices(dm);

    if (dm.audio.context_initialised && OpenAudioDeviceWithFallback(dm, dm.audio.selected_name))
        ActivatePluginAndStartAudio(dm);

    OpenMidiStream(dm, dm.midi.selected_name);
}

static void HandleAudioDeviceLost(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    auto const selected =
        dm.audio.selected_name.size ? (String)dm.audio.selected_name : "the default audio device"_s;

    CloseAudio(dm);
    if (dm.host_callbacks.deactivate_for_device_swap)
        dm.host_callbacks.deactivate_for_device_swap(dm.host_callbacks.ctx);

    EnumerateAllDevices(dm);

    ReportAudioError(dm, selected, "Audio device disconnected."_s);
}

void PollDeviceChanges(DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    if (dm.audio.stream.lost.Load(LoadMemoryOrder::Acquire)) HandleAudioDeviceLost(dm);
    auto const command = dm.pending_command;
    dm.pending_command = DeviceManager::PendingCommand::None;
    switch (command) {
        case DeviceManager::PendingCommand::None: break;
        case DeviceManager::PendingCommand::Refresh: ApplyRefresh(dm); break;
        case DeviceManager::PendingCommand::SelectAudio:
            ApplyAudioSelection(dm, dm.pending_name);
            dyn::Clear(dm.pending_name);
            break;
        case DeviceManager::PendingCommand::SelectMidi:
            ApplyMidiSelection(dm, dm.pending_name);
            dyn::Clear(dm.pending_name);
            break;
        case DeviceManager::PendingCommand::SelectBackend:
            ApplyBackendSelection(dm, dm.pending_name);
            dyn::Clear(dm.pending_name);
            break;
    }
}

void SetupHostDeviceCallbacks(FloeClapExtensionHost& ext, DeviceManager& dm) {
    ASSERT(g_is_logical_main_thread);
    ext.context = &dm;
    ext.num_devices = [](FloeClapExtensionHost const* host, HostDeviceType type) -> u32 {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        auto& dm = *(DeviceManager*)host->context;
        switch (type) {
            case HostDeviceType::AudioOutput: return (u32)dm.audio.devices.size;
            case HostDeviceType::MidiInput: return (u32)dm.midi.devices.size;
            case HostDeviceType::AudioBackend: return (u32)dm.audio.backends.size;
        }
        return 0;
    };
    ext.device_info =
        [](FloeClapExtensionHost const* host, HostDeviceType type, u32 index, HostDeviceInfo* out) -> bool {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        auto& dm = *(DeviceManager*)host->context;
        Span<HostDeviceInfo> list {};
        switch (type) {
            case HostDeviceType::AudioOutput: list = dm.audio.devices; break;
            case HostDeviceType::MidiInput: list = dm.midi.devices; break;
            case HostDeviceType::AudioBackend: list = dm.audio.backends; break;
        }
        if (index >= list.size) return false;
        *out = list[index];
        return true;
    };
    ext.current_device = [](FloeClapExtensionHost const* host, HostDeviceType type, HostDeviceInfo* out) {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        auto& dm = *(DeviceManager*)host->context;
        DynamicArray<char> const* name = nullptr;
        switch (type) {
            case HostDeviceType::AudioOutput: name = &dm.audio.selected_name; break;
            case HostDeviceType::MidiInput: name = &dm.midi.selected_name; break;
            case HostDeviceType::AudioBackend: name = &dm.audio.selected_backend_name; break;
        }
        *out = {.id = *name, .name = *name, .is_default = name->size == 0};
    };
    ext.device_has_error = [](FloeClapExtensionHost const* host, HostDeviceType type) -> bool {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        auto& dm = *(DeviceManager*)host->context;
        switch (type) {
            case HostDeviceType::AudioOutput: return dm.audio.failed;
            case HostDeviceType::MidiInput: return dm.midi.failed;
            case HostDeviceType::AudioBackend: return false;
        }
        return false;
    };
    ext.set_device = [](FloeClapExtensionHost const* host, HostDeviceType type, String id) {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        auto& dm = *(DeviceManager*)host->context;
        dyn::Assign(dm.pending_name, id);
        switch (type) {
            case HostDeviceType::AudioOutput:
                dm.pending_command = DeviceManager::PendingCommand::SelectAudio;
                break;
            case HostDeviceType::MidiInput:
                dm.pending_command = DeviceManager::PendingCommand::SelectMidi;
                break;
            case HostDeviceType::AudioBackend:
                dm.pending_command = DeviceManager::PendingCommand::SelectBackend;
                break;
        }
    };
    ext.refresh_devices = [](FloeClapExtensionHost const* host) {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        ((DeviceManager*)host->context)->pending_command = DeviceManager::PendingCommand::Refresh;
    };
    ext.get_tempo = [](FloeClapExtensionHost const* host) -> f64 {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        return ((DeviceManager*)host->context)->tempo.Load(LoadMemoryOrder::Relaxed);
    };
    ext.set_tempo = [](FloeClapExtensionHost const* host, f64 tempo_bpm) {
        ASSERT(g_is_logical_main_thread);
        ASSERT(host->context);
        ((DeviceManager*)host->context)->tempo.Store(tempo_bpm, StoreMemoryOrder::Relaxed);
    };
}
