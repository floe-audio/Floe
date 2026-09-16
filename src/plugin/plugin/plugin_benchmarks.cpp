// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Whole-plugin benchmarks: create a Floe instance in-process, load a preset from disk, and run the CLAP
// process callback as fast as the machine allows. Unlike the standalone's --render, nothing here is paced to
// realtime, so wall-clock time is a measure of DSP cost.

#include "os/threading.hpp"

#include "benchmarks/framework.hpp"
#include "clap/ext/params.h"
#include "clap/ext/thread-check.h"
#include "plugin/plugin.hpp"
#include "plugin/processing_utils/midi.hpp"

struct BenchmarkHost {
    clap_host_params const host_params {
        .rescan = [](clap_host_t const*, clap_param_rescan_flags) {},
        .clear = [](clap_host_t const*, clap_id, clap_param_clear_flags) {},
        .request_flush = [](clap_host_t const*) {},
    };

    clap_host_thread_check const host_thread_check {
        .is_main_thread =
            [](clap_host const* h) {
                auto& benchmark_host = *(BenchmarkHost*)h->host_data;
                return CurrentThreadId() == benchmark_host.main_thread_id;
            },
        .is_audio_thread =
            [](clap_host const* h) {
                auto& benchmark_host = *(BenchmarkHost*)h->host_data;
                return CurrentThreadId() == benchmark_host.audio_thread_id.Load(LoadMemoryOrder::Relaxed);
            },
    };

    clap_host_t const host {
        .clap_version = CLAP_VERSION,
        .host_data = this,
        .name = "Floe Benchmarks",
        .vendor = "Floe",
        .url = "https://floe.audio",
        .version = "1",

        .get_extension = [](clap_host_t const* ch, char const* extension_id) -> void const* {
            auto& benchmark_host = *(BenchmarkHost*)ch->host_data;
            if (NullTermStringsEqual(extension_id, CLAP_EXT_PARAMS)) return &benchmark_host.host_params;
            if (NullTermStringsEqual(extension_id, CLAP_EXT_THREAD_CHECK))
                return &benchmark_host.host_thread_check;
            return nullptr;
        },
        .request_restart = [](clap_host_t const*) {},
        .request_process = [](clap_host_t const*) {},
        .request_callback =
            [](clap_host_t const* h) {
                auto& benchmark_host = *(BenchmarkHost*)h->host_data;
                benchmark_host.callback_requested.Store(true, StoreMemoryOrder::Relaxed);
            },
    };

    Atomic<u64> audio_thread_id {};
    u64 main_thread_id {CurrentThreadId()};
    Atomic<bool> callback_requested {false};
};

constexpr f64 k_sample_rate = 48000;
constexpr u32 k_block_size = 512;
constexpr u32 k_num_channels = 2;

// A repeating pattern of overlapping notes: one starts every interval and is held for the hold duration,
// giving a steady-state polyphony without the benchmark degenerating into a single sustained voice.
constexpr f64 k_note_interval_seconds = 0.25;
constexpr f64 k_note_hold_seconds = 2.0;
constexpr u7 k_notes[] = {48, 55, 52, 59, 60, 64, 57, 62};
constexpr u7 k_velocity = 100;

struct TimedMidiMessage {
    u32 frame;
    MidiMessage message;
};

static Span<TimedMidiMessage> MakeNotePattern(ArenaAllocator& arena, u32 num_frames) {
    auto const interval_frames = (u32)(k_note_interval_seconds * k_sample_rate);
    auto const hold_frames = (u32)(k_note_hold_seconds * k_sample_rate);

    DynamicArray<TimedMidiMessage> result {arena};
    for (u32 note_index = 0;; ++note_index) {
        auto const on_frame = note_index * interval_frames;
        if (on_frame >= num_frames) break;

        auto const note = k_notes[note_index % ArraySize(k_notes)];
        dyn::Append(result, {.frame = on_frame, .message = MidiMessage::NoteOn(note, k_velocity)});

        auto const off_frame = on_frame + hold_frames;
        if (off_frame < num_frames)
            dyn::Append(result, {.frame = off_frame, .message = MidiMessage::NoteOff(note)});
    }

    // Events must be given to the plugin in time order.
    Sort(result, [](TimedMidiMessage const& a, TimedMidiMessage const& b) { return a.frame < b.frame; });

    return result.ToOwnedSpan();
}

struct BlockEvents {
    clap_event_midi events[32];
    u32 size;
};

// Returns the peak of the output so that the caller can tell a real workload from silence.
static f32 ProcessOffline(clap_plugin const& plugin, Span<TimedMidiMessage const> pattern, u32 num_frames) {
    alignas(16) f32 channel_data[k_num_channels][k_block_size];
    f32* channels[k_num_channels];
    for (auto const channel_index : Range(k_num_channels))
        channels[channel_index] = channel_data[channel_index];

    clap_audio_buffer out {
        .data32 = channels,
        .data64 = nullptr,
        .channel_count = k_num_channels,
        .latency = 0,
        .constant_mask = 0,
    };

    usize pattern_pos = 0;
    f32 peak = 0;

    for (u32 frame_pos = 0; frame_pos < num_frames; frame_pos += k_block_size) {
        auto const block_size = Min(k_block_size, num_frames - frame_pos);
        auto const block_end = frame_pos + block_size;

        BlockEvents block_events {};
        while (pattern_pos < pattern.size && pattern[pattern_pos].frame < block_end) {
            ASSERT(block_events.size < ArraySize(block_events.events));
            auto const& timed = pattern[pattern_pos++];
            block_events.events[block_events.size++] = {
                .header =
                    {
                        .size = sizeof(clap_event_midi),
                        .time = timed.frame - frame_pos,
                        .space_id = CLAP_CORE_EVENT_SPACE_ID,
                        .type = CLAP_EVENT_MIDI,
                        .flags = CLAP_EVENT_IS_LIVE,
                    },
                .port_index = 0,
                .data = {timed.message.status, timed.message.data1, timed.message.data2},
            };
        }

        clap_input_events const in_events {
            .ctx = &block_events,
            .size = [](clap_input_events const* e) -> u32 { return ((BlockEvents*)e->ctx)->size; },
            .get = [](clap_input_events const* e, u32 index) -> clap_event_header_t const* {
                return &((BlockEvents*)e->ctx)->events[index].header;
            },
        };

        clap_output_events const out_events {
            .ctx = nullptr,
            .try_push = [](clap_output_events const*, clap_event_header const*) -> bool { return false; },
        };

        clap_process process {
            .steady_time = (s64)frame_pos,
            .frames_count = block_size,
            .transport = nullptr,
            .audio_inputs = nullptr,
            .audio_outputs = &out,
            .audio_inputs_count = 0,
            .audio_outputs_count = 1,
            .in_events = &in_events,
            .out_events = &out_events,
        };

        plugin.process(&plugin, &process);

        for (auto const frame_index : Range(block_size))
            for (auto const channel_index : Range(k_num_channels))
                peak = Max(peak, Abs(channels[channel_index][frame_index]));
    }

    return peak;
}

// Returns false if the preset's sample libraries never finished loading.
static bool WaitForPresetToLoad(BenchmarkHost& host, clap_plugin const& plugin) {
    auto const floe_ext = (FloeClapExtension const*)plugin.get_extension(&plugin, k_floe_clap_extension_id);
    if (!floe_ext) return false;

    constexpr f64 k_timeout_seconds = 60;
    auto const start = TimePoint::Now();
    while (true) {
        if (host.callback_requested.Exchange(false, RmwMemoryOrder::Relaxed)) plugin.on_main_thread(&plugin);

        if (!floe_ext->state_change_is_pending(&plugin)) return true;
        if ((TimePoint::Now() - start) > k_timeout_seconds) return false;

        SleepThisThread(10);
    }
}

// --arg <preset-path> [seconds-of-audio]
BENCHMARK_FN void BenchmarkProcessPreset(benchmarks::BenchmarkContext const& context) {
    if (!context.args.size) {
        StdPrintF(StdStream::Err, "ProcessPreset needs a preset: --arg <preset-path> [seconds-of-audio]\n");
        return;
    }

    auto const seconds = ({
        f64 s = 20;
        if (context.args.size >= 2) {
            auto const parsed = ParseFloat(context.args[1]);
            if (!parsed || *parsed <= 0) {
                StdPrintF(StdStream::Err, "Invalid seconds-of-audio: {}\n", context.args[1]);
                return;
            }
            s = *parsed;
        }
        s;
    });
    auto const num_frames = (u32)(seconds * k_sample_rate);

    BenchmarkHost host {};

    auto const plugin = CreateFloeInstance(&host.host);
    if (!plugin) {
        StdPrintF(StdStream::Err, "Failed to create a Floe instance\n");
        return;
    }
    DEFER { plugin->destroy(plugin); };

    if (!plugin->init(plugin)) {
        StdPrintF(StdStream::Err, "Failed to initialise the Floe instance\n");
        return;
    }

    // Activating discards anything queued for the audio thread, so the preset has to be loaded after it.
    if (!plugin->activate(plugin, k_sample_rate, k_block_size, k_block_size)) {
        StdPrintF(StdStream::Err, "Failed to activate the Floe instance\n");
        return;
    }
    DEFER { plugin->deactivate(plugin); };

    auto const floe_ext = (FloeClapExtension const*)plugin->get_extension(plugin, k_floe_clap_extension_id);
    if (!floe_ext || !floe_ext->load_preset_file(plugin, context.args[0])) {
        StdPrintF(StdStream::Err, "Failed to load the preset\n");
        return;
    }

    if (!WaitForPresetToLoad(host, *plugin)) {
        StdPrintF(StdStream::Err, "Timed out waiting for the preset's sample libraries to load\n");
        return;
    }

    auto const pattern = MakeNotePattern(context.arena, num_frames);

    // The plugin checks that processing happens on the thread the host declares as the audio thread, so we
    // can't just process on this one.
    Thread audio_thread {};
    audio_thread.Start(
        [&] {
            host.audio_thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
            if (!plugin->start_processing(plugin)) {
                StdPrintF(StdStream::Err, "Failed to start processing\n");
                return;
            }
            DEFER { plugin->stop_processing(plugin); };

            if (ProcessOffline(*plugin, pattern, num_frames) == 0)
                StdPrintF(StdStream::Err, "Warning: the preset produced silence\n");
        },
        "audio");
    audio_thread.Join();
}

BENCHMARK_REGISTRATION(RegisterPluginProcessingBenchmarks) {
    REGISTER_BENCHMARK_NAMED(BenchmarkProcessPreset, "ProcessPreset");
}
