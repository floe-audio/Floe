// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/state/state_coding.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "build_resources/embedded_files.h"
#include "clap/entry.h"
#include "clap/ext/gui.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/thread-check.h"
#include "clap/ext/timer-support.h"
#include "clap/factory/plugin-factory.h"
#include "plugin/plugin.hpp"
#include "plugin/processing_utils/midi.hpp"

struct TestHost {
    clap_host_params const host_params {
        .rescan =
            [](clap_host_t const* h, clap_param_rescan_flags) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .clear =
            [](clap_host_t const* h, clap_id, clap_param_clear_flags) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .request_flush =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
    };

    clap_host_gui const host_gui {
        .resize_hints_changed =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .request_resize =
            [](clap_host_t const* h, uint32_t, uint32_t) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return false;
            },

        .request_show = [](clap_host_t const*) { return false; },
        .request_hide = [](clap_host_t const*) { return false; },
        .closed = [](clap_host_t const*, bool) { Panic("floating windows are not supported"); },
    };

    clap_host_thread_check const host_thread_check {
        .is_main_thread =
            [](clap_host const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return CurrentThreadId() == test_host.main_thread_id;
            },
        .is_audio_thread =
            [](clap_host const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return CurrentThreadId() == test_host.audio_thread_id.Load(LoadMemoryOrder::Relaxed);
            },
    };

    clap_host_timer_support const host_timer_support {
        .register_timer =
            [](clap_host_t const* h, uint32_t, clap_id* timer_id) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                ASSERT(timer_id);
                if (!dyn::Append(test_host.timer_ids, test_host.next_timer_id)) return false;
                *timer_id = test_host.next_timer_id++;
                return true;
            },
        .unregister_timer =
            [](clap_host_t const* h, clap_id timer_id) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return dyn::RemoveValue(test_host.timer_ids, timer_id) != 0;
            },
    };

    clap_host_t const host {
        .clap_version = CLAP_VERSION,
        .host_data = this,
        .name = "Test Host",
        .vendor = "Tester",
        .url = "https://example.com",
        .version = "1",

        .get_extension = [](clap_host_t const* ch, char const* extension_id) -> void const* {
            auto& test_host = *(TestHost*)ch->host_data;
            ASSERT(test_host.plugin_created);

            if (NullTermStringsEqual(extension_id, CLAP_EXT_PARAMS))
                return &test_host.host_params;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_GUI))
                return &test_host.host_gui;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_THREAD_CHECK))
                return &test_host.host_thread_check;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_TIMER_SUPPORT))
                return &test_host.host_timer_support;
            else if (NullTermStringsEqual(extension_id, k_floe_clap_extension_id))
                return &test_host.floe_host_ext;

            return nullptr;
        },
        .request_restart = [](clap_host_t const*) { PanicIfReached(); },
        .request_process =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .request_callback =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                test_host.callback_requested.Store(true, StoreMemoryOrder::Relaxed);
            },
    };

    Atomic<u64> audio_thread_id {};
    u64 main_thread_id {CurrentThreadId()};
    Atomic<bool> callback_requested {false};
    FloeClapExtensionHost floe_host_ext {};
    DynamicArrayBounded<clap_id, 4> timer_ids {};
    clap_id next_timer_id {};
    bool plugin_created = false;
};

struct EventQueue {
    struct Node {
        clap_event_header* header;
        Node* prev;
        Node* next;
    };

    template <typename EventType>
    EventType* AppendUninit(ArenaAllocator& arena) {
        auto node = arena.New<Node>();
        auto header = arena.Allocate({.size = sizeof(EventType), .alignment = k_max_alignment});
        node->header = CheckedPointerCast<clap_event_header*>(header.data);
        DoublyLinkedListAppend(*this, node);
        ++size;
        return (EventType*)node->header;
    }

    void AppendMidiMessage(ArenaAllocator& arena, u32 time, MidiMessage msg) {
        *AppendUninit<clap_event_midi>(arena) = {
            .header =
                {
                    .size = sizeof(clap_event_midi),
                    .time = time,
                    .type = CLAP_EVENT_MIDI,
                    .flags = CLAP_EVENT_IS_LIVE,
                },
            .port_index = 0,
            .data = {msg.status, msg.data1, msg.data2},
        };
    }

    void AppendExistingNode(Node* node) {
        DoublyLinkedListAppend(*this, node);
        ++size;
    }

    clap_event_header* AtIndex(u32 index) const {
        ASSERT(first);
        ASSERT(index < size);
        auto node = first;
        for (auto _ : Range(index)) {
            ASSERT(node);
            node = node->next;
        }
        return node->header;
    }

    using Iterator = IntrusiveSinglyLinkedListIterator<Node>;
    auto begin() const { return Iterator {first}; }
    static auto end() { return Iterator {nullptr}; }

    Node* first = nullptr;
    Node* last = nullptr;
    u32 size = 0;
};

struct ProcessTestOptions {
    u64 seed;
    u32 num_frames;
    u32 num_channels;
    f64 sample_rate;
    u32 min_block_size;
    u32 max_block_size;
    u32 constant_block_size; // 0 means random block size
    EventQueue events; // ordered by time, time is in overall frames not per-block
    bool transport_playing = false;
    bool capture_output = false;
};

constexpr usize k_max_test_block_size = 1024;
constexpr usize k_max_test_channels = 3;

static void CheckProcessTestOptions(ProcessTestOptions const& options) {
    if (options.constant_block_size) {
        ASSERT(options.constant_block_size >= options.min_block_size);
        ASSERT(options.constant_block_size <= options.max_block_size);
    }

    ASSERT(options.num_frames > 0);
    ASSERT(options.num_channels > 0);
    ASSERT(options.num_channels <= k_max_test_channels);
    ASSERT(options.min_block_size > 0);
    ASSERT(options.max_block_size > 0);
    ASSERT(options.min_block_size <= options.max_block_size);
    ASSERT(options.max_block_size <= k_max_test_block_size);

    {
        u32 prev_time = 0;
        for (auto n : options.events) {
            auto const time = n.header->time;
            ASSERT(time >= prev_time);
            prev_time = time;
        }
    }
}

static Span<Span<f32>>
Process(tests::Tester& tester, clap_plugin const* plugin, ArenaAllocator& arena, ProcessTestOptions options) {
    CheckProcessTestOptions(options);

    Span<Span<f32>> result {};
    if (options.capture_output) {
        result = arena.AllocateExactSizeUninitialised<Span<f32>>(options.num_channels);
        for (auto c : Range(options.num_channels))
            result[c] = arena.AllocateExactSizeUninitialised<f32>(options.num_frames);
    }

    u32 block_size = options.constant_block_size;
    for (u32 frame_pos = 0; frame_pos < options.num_frames; frame_pos += block_size) {
        if (!options.constant_block_size)
            block_size = RandomIntInRange<u32>(options.seed, options.min_block_size, options.max_block_size);
        block_size = Min(block_size, options.num_frames - frame_pos);

        u32 const frame_end = frame_pos + block_size;

        EventQueue events {};

        // events are ordered by time
        while (options.events.first && options.events.first->header->time < frame_end) {
            auto n = options.events.first;
            DoublyLinkedListRemoveFirst(options.events);
            events.AppendExistingNode(n);
            n->header->time -= frame_pos; // convert to block time
        }

        clap_input_events const in_events {
            .ctx = &events,
            .size = [](clap_input_events const* in_events) -> u32 {
                auto const& events = *(EventQueue*)in_events->ctx;
                return events.size;
            },
            .get = [](clap_input_events const* in_events, uint32_t index) -> clap_event_header_t const* {
                auto const& events = *(EventQueue*)in_events->ctx;
                return events.AtIndex(index);
            },
        };

        clap_output_events const out_events {
            .ctx = nullptr,
            .try_push = [](clap_output_events const*, clap_event_header const*) -> bool { return false; },
        };

        constexpr f32 k_invalid_value = 100.0f;

        Array<f32, k_max_test_block_size * k_max_test_channels> data_blob {};
        for (auto& d : data_blob)
            d = k_invalid_value;

        Array<f32*, k_max_test_channels> data_channels {};
        for (auto c : Range(options.num_channels))
            data_channels[c] = data_blob.data + c * k_max_test_block_size;

        clap_audio_buffer out {
            .data32 = data_channels.data,
            .channel_count = options.num_channels,
            .latency = 0,
            .constant_mask = 0,
        };

        clap_event_transport const transport {
            .header =
                {
                    .size = sizeof(clap_event_transport),
                    .time = 0,
                    .space_id = CLAP_CORE_EVENT_SPACE_ID,
                    .type = CLAP_EVENT_TRANSPORT,
                },
            .flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING,
            .tempo = 120,
        };

        clap_process process {
            .steady_time = -1,
            .frames_count = block_size,
            .transport = options.transport_playing ? &transport : nullptr,
            .audio_inputs = nullptr,
            .audio_outputs = &out,
            .audio_inputs_count = 0,
            .audio_outputs_count = 1,
            .in_events = &in_events,
            .out_events = &out_events,
        };

        tester.log.Debug("processing {} frames with {} events", block_size, events.size);

        auto const status = plugin->process(plugin, &process);
        CHECK(status != CLAP_PROCESS_ERROR);

        for (auto const c : Range(options.num_channels)) {
            for (auto const i : Range(block_size)) {
                auto const value = data_channels[c][i];
                if (value == k_invalid_value) TEST_FAILED("channel {} frame {} is invalid", c, i);
                if (value < -1.0f || value > 1.0f) TEST_FAILED("channel {} frame {} is out of range", c, i);
            }
        }

        if (options.capture_output) {
            for (auto const c : Range(options.num_channels))
                for (auto const i : Range(block_size))
                    result[c][frame_pos + i] = data_channels[c][i];
        }
    }

    return result;
}

struct WaveFileArgs {
    u32 num_channels;
    f64 sample_rate;
    u32 num_frames;
    Span<Span<f32>> data;
};

ErrorCodeOr<void> WriteWaveFile(String filename, ArenaAllocator& scratch_arena, WaveFileArgs args) {
    static_assert(k_endianness == Endianness::Little, "Wave file format is little-endian, we don't convert");

    ASSERT_EQ(args.data.size, args.num_channels);
    ASSERT(args.num_channels);
    ASSERT(args.num_frames);
    ASSERT(args.sample_rate > 0.0);

    auto const sample_rate = (u32)args.sample_rate;

    auto file = TRY(OpenFile(filename, FileMode::Write()));

    auto data_pcm_16_interleaved =
        scratch_arena.AllocateExactSizeUninitialised<s16>(args.num_frames * args.num_channels);
    for (auto const frame : Range(args.num_frames)) {
        for (auto const chan : Range(args.num_channels)) {
            auto const value = args.data[chan][frame];
            auto const clamped = Clamp(value, -1.0f, 1.0f);
            auto const pcm = (s16)(clamped * 32767.0f);
            data_pcm_16_interleaved[(frame * args.num_channels) + chan] = pcm;
        }
    }

    TRY(file.Write("RIFF"));
    TRY(file.WriteBinaryNumber<u32>((u32)36 + (args.num_frames * args.num_channels * sizeof(s16))));
    TRY(file.Write("WAVEfmt "));
    TRY(file.WriteBinaryNumber<u32>(16)); // fmt chunk size
    TRY(file.WriteBinaryNumber<u16>(1)); // PCM mode
    TRY(file.WriteBinaryNumber<u16>((u16)args.num_channels));
    TRY(file.WriteBinaryNumber<u32>(sample_rate));
    TRY(file.WriteBinaryNumber<u32>(sample_rate * args.num_channels * sizeof(s16))); // bytes per second
    TRY(file.WriteBinaryNumber<u16>((u16)(args.num_channels * sizeof(s16)))); // bytes per sample
    TRY(file.WriteBinaryNumber<u16>(16)); // bits per sample
    TRY(file.Write("data"));
    TRY(file.WriteBinaryNumber<u32>(
        (u32)(args.num_frames * args.num_channels * sizeof(s16)))); // data length bytes
    TRY(file.Write(data_pcm_16_interleaved.ToByteSpan()));

    return k_success;
}

enum class StateProperties : u8 {
    Ir = 1 << 0,
    Sine = 1 << 1,
    WhiteNoise = 1 << 2,
    SampleInst = 1 << 3,
    SoundShapersOn = 1 << 4,
    ArpWithRandomness = 1 << 5, // Arpeggiator on the first layer, using random note order and humanise
};

inline StateProperties operator|(StateProperties a, StateProperties b) {
    return (StateProperties)(ToInt(a) | ToInt(b));
}

inline auto operator&(StateProperties a, StateProperties b) { return ToInt(a) & ToInt(b); }

static ErrorCodeOr<Span<u8 const>> MakeState(ArenaAllocator& arena, StateProperties properties) {
    StateSnapshot state;

    state.extras.instance_id = "foo-123"_s;

    for (auto const index : Range(k_num_effect_types))
        state.fx_order[index] = (EffectType)index;

    for (auto const param_index : Range(k_num_parameters)) {
        auto const& descriptor = k_param_descriptors[param_index];
        state.param_values[param_index] = descriptor.default_linear_value;
    }

    if (properties & StateProperties::Ir) {
        auto const ir_id = GetEmbeddedIrs().irs[0].id;
        state.ir_id = sample_lib::IrId {
            .library = sample_lib::k_builtin_library_id,
            .ir_id = String {ir_id.data, ir_id.size},
        };
    }

    u32 layer_assignment_index = 0;

    if (properties & StateProperties::Sine) {
        state.inst_ids[layer_assignment_index] = WaveformType::Sine;
        layer_assignment_index = (layer_assignment_index + 1) % k_num_layers;
    }

    if (properties & StateProperties::WhiteNoise) {
        state.inst_ids[layer_assignment_index] = WaveformType::WhiteNoiseMono;
        layer_assignment_index = (layer_assignment_index + 1) % k_num_layers;
    }

    if (properties & StateProperties::SampleInst) {
        // TODO: we need to somehow get a library that is widely available: e.g. on CI.
        state.inst_ids[layer_assignment_index] = sample_lib::InstrumentId {
            .library = sample_lib::IdForMdataLibrary("Wraith"),
            .inst_id = "Endless Stride"_s,
        };
        layer_assignment_index = (layer_assignment_index + 1) % k_num_layers;
    }

    if (properties & StateProperties::SoundShapersOn) {
        state.param_values[ToInt(ParamIndex::DistortionOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::BitCrushOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::CompressorOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::FilterOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::StereoWidenOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::ChorusOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::ReverbOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::DelayOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::ConvolutionReverbOn)] = 1.0f;
        state.param_values[ToInt(ParamIndex::PhaserOn)] = 1.0f;
        for (auto const layer_index : Range(k_num_layers)) {
            state.param_values[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::FilterOn))] =
                1.0f;
            state.param_values[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::LfoOn))] =
                1.0f;
            state.param_values[ToInt(ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::EqOn))] =
                1.0f;
        }
    }

    if (properties & StateProperties::ArpWithRandomness) {
        auto const param = [&](LayerParamIndex p) -> f32& {
            return state.param_values[ToInt(ParamIndexFromLayerParamIndex(0, p))];
        };
        param(LayerParamIndex::ArpOn) = 1;
        param(LayerParamIndex::ArpNoteOrder) = (f32)param_values::ArpNoteOrder::Random;
        param(LayerParamIndex::ArpRate) = (f32)param_values::ArpSyncedRate::_1_64;
        param(LayerParamIndex::ArpHumanise) = 1;
    }

    DynamicArray<u8> buffer {arena};
    TRY(CodeState(state,
                  CodeStateArguments {
                      .mode = CodeStateArguments::Mode::Encode,
                      .read_or_write_data = [&buffer](void* data, usize bytes) -> ErrorCodeOr<void> {
                          dyn::AppendSpan(buffer, Span {(u8 const*)data, bytes});
                          return k_success;
                      },
                      .source = StateSource::Daw,
                  }));

    return buffer.ToOwnedSpan();
}

static void LoadState(tests::Tester& tester, clap_plugin const* plugin, Span<u8 const> state) {
    auto const state_ext = (clap_plugin_state const*)plugin->get_extension(plugin, CLAP_EXT_STATE);
    REQUIRE(state_ext);

    auto reader = Reader::FromMemory(state);

    clap_istream const stream {
        .ctx = (void*)&reader,

        .read = [](const struct clap_istream* stream, void* buffer, uint64_t size) -> s64 {
            auto& reader = *(Reader*)stream->ctx;
            auto const read = reader.Read(Span<u8>((u8*)buffer, size));
            if (read.HasError()) return -1;
            return CheckedCast<s64>(read.Value());
        },
    };

    REQUIRE(state_ext->load(plugin, &stream));
}

static Span<Span<f32>> ProcessWithState(tests::Tester& tester,
                                        clap_plugin const* plugin,
                                        TestHost& test_host,
                                        StateProperties state_properties,
                                        ProcessTestOptions options) {
    CheckProcessTestOptions(options);

    LoadState(tester, plugin, REQUIRE_UNWRAP(MakeState(tester.scratch_arena, state_properties)));

    // Floe can't always apply state immediately. Sample libraries might need to be loaded before we have the
    // audio data to play. Here, we wait a little while for this to happen otherwise we might get silence.
    {
        auto const floe_custom_ext =
            (FloeClapExtension const*)plugin->get_extension(plugin, k_floe_clap_extension_id);
        REQUIRE(floe_custom_ext);

        auto const start = TimePoint::Now();
        while (true) {
            if (test_host.callback_requested.Exchange(false, RmwMemoryOrder::Relaxed))
                plugin->on_main_thread(plugin);

            if (!floe_custom_ext->state_change_is_pending(plugin)) break;

            constexpr f64 k_timeout_ms = 10000;
            if ((TimePoint::Now() - start) > (k_timeout_ms / 1000.0)) {
                LOG_WARNING("Timeout waiting for state change to complete");
                return {};
            }

            SleepThisThread(10);
        }
    }

    REQUIRE(plugin->activate(plugin, options.sample_rate, options.min_block_size, options.max_block_size));
    DEFER { plugin->deactivate(plugin); };

    // We block until the audio thread is done and we don't run anything concurrently so we don't need to
    // worry about thread safety.
    Thread audio_thread {};
    Span<Span<f32>> captured_output {};
    audio_thread.Start(
        [&] {
            test_host.audio_thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
            REQUIRE(plugin->start_processing(plugin));
            DEFER { plugin->stop_processing(plugin); };

            captured_output = Process(tester, plugin, tester.scratch_arena, options);
        },
        "audio");
    audio_thread.Join();

    if (options.capture_output) {
        auto const out_dir = tests::HumanCheckableOutputFilesFolder(tester);
        auto const audio_file_path =
            fmt::Format(tester.scratch_arena, "{}/{}.wav", out_dir, Last(tester.subcases_stack).name);

        REQUIRE_UNWRAP(WriteWaveFile(audio_file_path,
                                     tester.scratch_arena,
                                     {
                                         .num_channels = options.num_channels,
                                         .sample_rate = options.sample_rate,
                                         .num_frames = options.num_frames,
                                         .data = captured_output,
                                     }));
    }

    return captured_output;
}

TEST_CASE(TestHostingClap) {
    if constexpr (k_running_with_thread_sanitizer) {
        // The CLAP plugin does not have tsan instrumentation since it's a shared library aren't supported by
        // tsan. There's no point in testing it in this environment. Additionally, it might not exist since
        // when tsan is enabled we don't build shared libs.
        return k_success;
    }

    struct Fixture {
        [[maybe_unused]] Fixture(tests::Tester&) {}
        [[maybe_unused]] ~Fixture() {}
        DynamicArrayBounded<char, path::k_max> clap_binary_path {};
        DynamicArrayBounded<char, path::k_max> clap_dso_or_bundle_path {}; // null terminated
        bool initialised = false;
        Optional<LibraryHandle> handle = {}; // there is no need to unload the library
    };

    auto& fixture = CreateOrFetchFixtureObject<Fixture>(tester);

    if (!fixture.initialised) {
        fixture.initialised = true;

        fixture.clap_dso_or_bundle_path = tester.clap_plugin_path;
        dyn::Append(fixture.clap_dso_or_bundle_path, '\0');

        auto const abs_clap_path = (String)TRY(AbsolutePath(tester.scratch_arena, tester.clap_plugin_path));
        if constexpr (IS_MACOS) {
            fixture.clap_binary_path = abs_clap_path;
            path::JoinAppend(fixture.clap_binary_path, "Contents/MacOS/Floe"_s);
        } else {
            fixture.clap_binary_path = abs_clap_path;
        }

        fixture.handle = TRY(LoadLibrary(fixture.clap_binary_path));
    }

    auto const entry = (clap_plugin_entry const*)TRY(SymbolFromLibrary(*fixture.handle, "clap_entry"));
    REQUIRE(entry != nullptr);

    SUBCASE("no init") { entry->deinit(); }

    SUBCASE("reuse init/deinit") {
        REQUIRE(entry->init(fixture.clap_dso_or_bundle_path.data));
        entry->deinit();
        REQUIRE(entry->init(fixture.clap_dso_or_bundle_path.data));
        entry->deinit();

        SUBCASE("no double deinit") {}
        SUBCASE("double deinit") { entry->deinit(); }
    }

    SUBCASE("init") {
        REQUIRE(entry->init(fixture.clap_dso_or_bundle_path.data));
        DEFER { entry->deinit(); };

        SUBCASE("double init") { REQUIRE(entry->init("plugin-path")); }

        SUBCASE("double deinit") { entry->deinit(); }

        SUBCASE("plugin") {
            TestHost test_host {};

            auto factory = (clap_plugin_factory const*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
            REQUIRE(factory);
            CHECK_EQ(factory->get_plugin_count(factory), 1u);
            auto const plugin_id = factory->get_plugin_descriptor(factory, 0)->id;

            auto const plugin = factory->create_plugin(factory, &test_host.host, plugin_id);
            REQUIRE(plugin);
            test_host.plugin_created = true;

            DEFER { plugin->destroy(plugin); };

            SUBCASE("no init") {}

            SUBCASE("init") {
                REQUIRE(plugin->init(plugin));

                SUBCASE("empty") {
                    ProcessWithState(tester,
                                     plugin,
                                     test_host,
                                     {},
                                     {
                                         .seed = 0xca7,
                                         .num_frames = 132,
                                         .num_channels = 2,
                                         .sample_rate = 44100,
                                         .min_block_size = 1,
                                         .max_block_size = 32,
                                         .constant_block_size = 0,
                                         .events = {},
                                         .capture_output = false,
                                     });
                }

                SUBCASE("render does not depend on block size") {
                    // Notes are staggered like a hand-played chord so they land at different points within
                    // the internal sub-blocks. Layer 1 plays them directly; layer 0 arpeggiates them with
                    // random note order and humanise. White noise voices take their seed from the master
                    // random seed, so any change in the order of random draws makes the noise different.
                    auto const make_events = [&]() {
                        EventQueue events {};
                        auto const on = [&](u32 time, u7 note) {
                            events.AppendMidiMessage(tester.scratch_arena,
                                                     time,
                                                     MidiMessage::NoteOn(note, 90));
                        };
                        auto const off = [&](u32 time, u7 note) {
                            events.AppendMidiMessage(tester.scratch_arena, time, MidiMessage::NoteOff(note));
                        };
                        on(1000, 60);
                        on(1003, 64);
                        on(1011, 67);
                        on(1030, 71);
                        off(9000, 60);
                        off(9005, 64);
                        on(9013, 62);
                        off(9020, 67);
                        off(9021, 71);
                        on(14000, 65);
                        on(14002, 69);
                        on(14019, 72);
                        off(22000, 62);
                        off(22010, 65);
                        off(22011, 69);
                        off(22030, 72);
                        return events;
                    };

                    auto const render = [&](u32 constant_block_size, u64 seed) {
                        return ProcessWithState(tester,
                                                plugin,
                                                test_host,
                                                StateProperties::Sine | StateProperties::WhiteNoise |
                                                    StateProperties::ArpWithRandomness,
                                                {
                                                    .seed = seed,
                                                    .num_frames = 30000,
                                                    .num_channels = 2,
                                                    .sample_rate = 44100,
                                                    .min_block_size = 1,
                                                    .max_block_size = 1024,
                                                    .constant_block_size = constant_block_size,
                                                    .events = make_events(),
                                                    .transport_playing = true,
                                                    .capture_output = true,
                                                });
                    };

                    auto const reference = render(512, 0);
                    REQUIRE(reference.size == 2);
                    {
                        f32 peak = 0;
                        for (auto const channel : reference)
                            for (auto const sample : channel)
                                peak = Max(peak, Abs(sample));
                        CHECK_GT(peak, 0.01f);
                    }

                    struct BlockSizes {
                        u32 constant_block_size;
                        u64 seed;
                    };
                    for (auto const block_sizes :
                         ArrayT<BlockSizes>({{512, 0}, {441, 0}, {0, 0xf00d}, {0, 0xbead}})) {
                        auto const other = render(block_sizes.constant_block_size, block_sizes.seed);
                        REQUIRE(other.size == 2);
                        f32 max_difference = 0;
                        for (auto const channel_index : Range(2u))
                            for (auto const frame : Range(30000u))
                                max_difference =
                                    Max(max_difference,
                                        Abs(reference[channel_index][frame] - other[channel_index][frame]));
                        tester.log.Info("block size {} (seed {}): max difference {}",
                                        block_sizes.constant_block_size,
                                        block_sizes.seed,
                                        max_difference);
                        CHECK_LT(max_difference, 0.01f);
                    }
                }

                SUBCASE("note on") {
                    EventQueue events {};
                    events.AppendMidiMessage(tester.scratch_arena, 0, MidiMessage::NoteOn(60, 100, 0));

                    SUBCASE("sine") {
                        ProcessWithState(tester,
                                         plugin,
                                         test_host,
                                         StateProperties::Sine,
                                         {
                                             .seed = 0xbee,
                                             .num_frames = 44100,
                                             .num_channels = 2,
                                             .sample_rate = 20000,
                                             .min_block_size = 1,
                                             .max_block_size = 1024,
                                             .constant_block_size = 0,
                                             .events = events,
                                             .capture_output = true,
                                         });
                    }

                    SUBCASE("white noise") {
                        ProcessWithState(tester,
                                         plugin,
                                         test_host,
                                         StateProperties::WhiteNoise,
                                         {
                                             .seed = 0xd09,
                                             .num_frames = 44100,
                                             .num_channels = 2,
                                             .sample_rate = 96000,
                                             .min_block_size = 1,
                                             .max_block_size = 1024,
                                             .constant_block_size = 0,
                                             .events = events,
                                             .capture_output = true,
                                         });
                    }

                    SUBCASE("sample inst") {
                        ProcessWithState(tester,
                                         plugin,
                                         test_host,
                                         StateProperties::SampleInst,
                                         {
                                             .seed = 0x1ce,
                                             .num_frames = 44100,
                                             .num_channels = 2,
                                             .sample_rate = 44100,
                                             .min_block_size = 1,
                                             .max_block_size = 1024,
                                             .constant_block_size = 0,
                                             .events = events,
                                             .capture_output = true,
                                         });
                    }

                    SUBCASE("everything on") {
                        ProcessWithState(tester,
                                         plugin,
                                         test_host,
                                         StateProperties::Ir | StateProperties::Sine |
                                             StateProperties::WhiteNoise | StateProperties::SampleInst |
                                             StateProperties::SoundShapersOn,
                                         {
                                             .seed = 0xba7,
                                             .num_frames = 44100,
                                             .num_channels = 2,
                                             .sample_rate = 44100,
                                             .min_block_size = 1,
                                             .max_block_size = 1024,
                                             .constant_block_size = 0,
                                             .events = events,
                                             .capture_output = true,
                                         });
                    }

                    SUBCASE("gui basic") {
                        // On CI machines, X11 is rarely available and so creating a GUI will fail. We got
                        // quite far using xvfb and Mesa software renderer setups but never sucessfully got it
                        // working reliably.
                        auto const gui = (clap_plugin_gui const*)plugin->get_extension(plugin, CLAP_EXT_GUI);
                        REQUIRE(gui);
                        if (!(IS_LINUX && !GetEnvironmentVariable("DISPLAY", tester.scratch_arena)) &&
                            gui->is_api_supported(plugin, k_floe_offscreen_gui_api, false)) {
                            // Offscreen so that running the tests doesn't flash a window up.
                            REQUIRE(gui->create(plugin, k_floe_offscreen_gui_api, false));
                            DEFER { gui->destroy(plugin); };

                            u32 clap_width;
                            u32 clap_height;
                            REQUIRE(gui->get_size(plugin, &clap_width, &clap_height));
                            REQUIRE(gui->set_size(plugin, clap_width, clap_height));

                            REQUIRE(gui->show(plugin));
                            REQUIRE(test_host.timer_ids.size);

                            auto const timer_ext = (clap_plugin_timer_support const*)plugin->get_extension(
                                plugin,
                                CLAP_EXT_TIMER_SUPPORT);
                            REQUIRE(timer_ext);

                            for (auto _ : Range(5)) {
                                if (test_host.callback_requested.Exchange(false, RmwMemoryOrder::Relaxed))
                                    plugin->on_main_thread(plugin);

                                // Copy: the plugin may unregister timers from inside on_timer.
                                auto const timer_ids = test_host.timer_ids;
                                for (auto const timer_id : timer_ids)
                                    timer_ext->on_timer(plugin, timer_id);
                                SleepThisThread(10);
                            }

                            REQUIRE(gui->hide(plugin));
                        }
                    }
                }
            }
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterHostingTests) { REGISTER_TEST(TestHostingClap); }
