// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <clap/events.h>
#include <miniaudio.h>
#include <portmidi.h>

#include "foundation/foundation.hpp"
#include "foundation/memory/allocators.hpp"
#include "utils/error_notifications.hpp"

#include "common_infrastructure/persistent_store.hpp"

#include "plugin/plugin/plugin.hpp"

// JACK can change buffer size at runtime and deliver sizes larger than what was reported at init,
// despite miniaudio's noFixedSizedCallback=false. The bound is generous to cover that.
constexpr usize k_max_audio_buffer_frames = 8192;

struct DeviceManagerHostCallbacks {
    void* ctx {};

    // Audio thread.
    bool (*render)(void* ctx,
                   f32* output_interleaved,
                   u32 num_frames,
                   clap_event_midi const* midi_events,
                   u32 num_midi_events) {};

    // Main thread.
    void (*deactivate_for_device_swap)(void* ctx) {};
    void (*activate_after_device_swap)(void* ctx,
                                       f64 sample_rate,
                                       u32 period_frames,
                                       u32 max_block_frames) {};
};

struct DeviceManager {
    struct Audio {
        bool context_initialised = false;
        ma_context context {};

        Span<HostDeviceInfo> devices {};
        Span<ma_device_id> device_ids {};

        DynamicArray<char> selected_name {Malloc::Instance()};

        bool failed = false;

        // Cross-thread data.
        struct Stream {
            Optional<ma_device> device {};

            Atomic<bool> thread_setup_done {false};
            Atomic<u64> thread_id {};

            Atomic<bool> lost {false};
            Atomic<bool> suppress_stop_notification {false};
        } stream;
    } audio;

    struct Midi {
        bool portmidi_initialised = false;

        Span<HostDeviceInfo> devices {};
        Span<PmDeviceID> device_ids {};

        DynamicArray<char> selected_name {Malloc::Instance()};

        bool failed = false;

        // Read by the audio thread; only written before the audio device starts.
        bool force_channel_zero = false;

        struct Stream {
            // `handle` is mutated on the main thread but read on the audio thread, protected by `state`.
            PortMidiStream* handle {};
            enum class State : u8 { Closed, Open, Reading };
            Atomic<State> state {State::Closed};
        } stream;
    } midi;

    DeviceManagerHostCallbacks host_callbacks {};

    ArenaAllocator enum_arena {PageAllocator::Instance()};

    enum class PendingCommand : u8 { None, SelectAudio, SelectMidi, Refresh };
    PendingCommand pending_command = PendingCommand::None;
    DynamicArray<char> pending_name {Malloc::Instance()};

    ThreadsafeErrorNotifications error_notifications {};

    Optional<persistent_store::Store> store;
};

void InitDevices(DeviceManager& dm, ArenaAllocator& arena);
void DeinitDevices(DeviceManager& dm);

void PollDeviceChanges(DeviceManager& dm);
void SetupHostDeviceCallbacks(FloeClapExtensionHost& ext, DeviceManager& dm);
