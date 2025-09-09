// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/audio-buffer.h>
#include <clap/entry.h>
#include <clap/events.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/thread-check.h>
#include <clap/factory/plugin-factory.h>
#include <clap/host.h>
#include <clap/plugin.h>
#include <clap/process.h>
#include <miniaudio.h>
#include <portmidi.h>
#include <pugl/pugl.h>
#include <pugl/stub.h>

#include "os/misc.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/global.hpp"

#include "plugin/plugin/plugin.hpp"

// A very simple 'standalone' host for development purposes.

constexpr UiSize32 k_invalid_ui_size = {0, 0};

inline auto Factory() { return (clap_plugin_factory const*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID); }

struct Standalone {
    Standalone() : plugin(*Factory()->create_plugin(Factory(), &host, g_plugin_info.id)) {
        plugin_created = true;
    }

    clap_host_params const host_params {
        .rescan =
            [](clap_host_t const* h, clap_param_rescan_flags) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
            },
        .clear =
            [](clap_host_t const* h, clap_id, clap_param_clear_flags) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
            },
        .request_flush =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
            },
    };

    clap_host_gui const host_gui {
        .resize_hints_changed =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                standalone.resize_hints_changed.Store(true, StoreMemoryOrder::Relaxed);
            },
        .request_resize =
            [](clap_host_t const* h, uint32_t width, uint32_t height) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                standalone.requested_resize.Exchange({width, height}, RmwMemoryOrder::AcquireRelease);
                return true;
            },

        .request_show = [](clap_host_t const*) { return false; },
        .request_hide = [](clap_host_t const*) { return false; },
        .closed = [](clap_host_t const*, bool) { Panic("floating windows are not supported"); },
    };

    clap_host_thread_check const host_thread_check {
        .is_main_thread =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                return CurrentThreadId() == standalone.main_thread_id;
            },
        .is_audio_thread =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                return CurrentThreadId() == standalone.audio_thread_id.Load(LoadMemoryOrder::Relaxed);
            },
    };

    clap_host_t const host {
        .clap_version = CLAP_VERSION,
        .host_data = this,
        .name = k_floe_standalone_host_name,
        .vendor = FLOE_VENDOR,
        .url = FLOE_HOMEPAGE_URL,
        .version = "1",

        .get_extension = [](clap_host_t const* ch, char const* extension_id) -> void const* {
            auto& standalone = *(Standalone*)ch->host_data;
            ASSERT(standalone.plugin_created);

            if (NullTermStringsEqual(extension_id, CLAP_EXT_PARAMS))
                return &standalone.host_params;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_GUI))
                return &standalone.host_gui;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_THREAD_CHECK))
                return &standalone.host_thread_check;
            else if (NullTermStringsEqual(extension_id, k_floe_clap_extension_id))
                return &standalone.floe_host_ext;

            return nullptr;
        },
        .request_restart = [](clap_host_t const*) { PanicIfReached(); },
        .request_process =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                // Don't think we need to do anything here because we always call process() regardless
            },
        .request_callback =
            [](clap_host_t const* h) {
                auto& standalone = *(Standalone*)h->host_data;
                ASSERT(standalone.plugin_created);
                standalone.callback_requested.Store(true, StoreMemoryOrder::Relaxed);
            },
    };

    u64 main_thread_id {CurrentThreadId()};
    Atomic<u64> audio_thread_id {};
    Atomic<bool> callback_requested {false};
    FloeClapExtensionHost floe_host_ext {};

    Array<Span<f32>, 2> audio_buffers {};
    enum class AudioStreamState { Closed, Open, CloseRequested };
    Atomic<AudioStreamState> audio_stream_state {AudioStreamState::Closed};
    PortMidiStream* midi_stream {};
    Optional<ma_device> audio_device {};

    PuglWorld* gui_world {};
    PuglView* gui_view {};
    Atomic<UiSize32> requested_resize {k_invalid_ui_size};
    Atomic<bool> resize_hints_changed {false};

    bool quit = false;
    bool plugin_created = false; // plugins are forbidden to call host APIs while creating
    clap_plugin const& plugin;
};

static void
AudioCallback(ma_device* device, void* output_buffer, void const* input, ma_uint32 num_buffer_frames) {
    (void)input;
    auto standalone = (Standalone*)device->pUserData;
    if (!standalone) return;

    static bool called_before {false};
    if (!called_before) {
        called_before = true;
        standalone->audio_thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
        SetThreadName("audio", false);
        standalone->plugin.start_processing(&standalone->plugin);
        standalone->audio_stream_state.Store(Standalone::AudioStreamState::Open, StoreMemoryOrder::Release);
    }

    if (standalone->audio_stream_state.Load(LoadMemoryOrder::Acquire) ==
        Standalone::AudioStreamState::CloseRequested) {
        standalone->plugin.stop_processing(&standalone->plugin);
        standalone->audio_stream_state.Store(Standalone::AudioStreamState::Closed, StoreMemoryOrder::Release);
        return;
    }

    if (standalone->audio_stream_state.Load(LoadMemoryOrder::Acquire) != Standalone::AudioStreamState::Open)
        return;

    f32* channels[2];
    channels[0] = standalone->audio_buffers[0].data;
    channels[1] = standalone->audio_buffers[1].data;

    // Fill memory with garbage.
    for (auto& chan : channels)
        for (auto const i : Range(num_buffer_frames))
            chan[i] = -1000.0f;

    clap_process_t process {};
    process.frames_count = num_buffer_frames;
    process.steady_time = -1;
    process.transport = nullptr;

    clap_audio_buffer_t buffer {};
    buffer.channel_count = 2;
    buffer.data32 = channels;

    process.audio_outputs = &buffer;
    process.audio_outputs_count = 1;

    static constexpr usize k_max_events = 128;
    struct Events {
        PmEvent events[k_max_events];
        clap_event_midi clap_events[k_max_events];
        int num_events;
    };
    Events events {};
    if (standalone->midi_stream) {
        events.num_events = Pm_Read(standalone->midi_stream, events.events, (int)k_max_events);
        if (events.num_events >= 0) {
            for (auto const i : Range(events.num_events)) {
                events.clap_events[i] = {
                    .header =
                        {
                            .size = sizeof(clap_event_midi),
                            .time = 20,
                            .type = CLAP_EVENT_MIDI,
                            .flags = CLAP_EVENT_IS_LIVE,
                        },
                    .port_index = 0,
                    .data =
                        {
                            (u8)Pm_MessageStatus(events.events[i].message),
                            (u8)Pm_MessageData1(events.events[i].message),
                            (u8)Pm_MessageData2(events.events[i].message),
                        },
                };
            }
        } else {
            auto const error_str = Pm_GetErrorText((PmError)events.num_events);
            (void)error_str;
            events.num_events = 0;
            PanicIfReached();
        }
    }

    clap_input_events const in_events {
        .ctx = (void*)&events,
        .size = [](const struct clap_input_events* list) -> u32 {
            auto& events = *(Events*)list->ctx;
            return CheckedCast<u32>(events.num_events);
        },
        .get = [](const struct clap_input_events* list, uint32_t index) -> clap_event_header_t const* {
            auto& events = *(Events*)list->ctx;
            return &events.clap_events[index].header;
        },
    };

    clap_output_events const out_events {
        .ctx = nullptr,
        .try_push = [](clap_output_events const*, clap_event_header const*) -> bool { return false; },
    };

    process.in_events = &in_events;
    process.out_events = &out_events;

    standalone->plugin.process(&standalone->plugin, &process);

    for (auto const chan : Range(2))
        for (auto const i : Range(num_buffer_frames)) {
            constexpr f32 k_hard_limit = 3.0f;
            if (channels[chan][i] > k_hard_limit)
                channels[chan][i] = k_hard_limit;
            else if (channels[chan][i] < -k_hard_limit)
                channels[chan][i] = -k_hard_limit;
        }

    CopySeparateChannelsToInterleaved((f32*)output_buffer, channels[0], channels[1], num_buffer_frames);
}

static bool OpenMidi(Standalone& standalone) {
    ASSERT(standalone.midi_stream == nullptr);

    if (auto result = Pm_Initialize(); result != pmNoError) {
        LogError(ModuleName::Standalone, "Pm_Initialize: {}", FromNullTerminated(Pm_GetErrorText(result)));
        return false;
    }

    auto const num_devices = Pm_CountDevices();
    if (num_devices == 0) return true;

    Optional<PmDeviceID> id_to_use = {};
    for (auto const i : Range(num_devices)) {
        auto const info = Pm_GetDeviceInfo(i);
        if (info->input) {
            if (!id_to_use) id_to_use = i;
            if (ContainsSpan(FromNullTerminated(info->name), "USB Keystation 61es"_s) ||
                ContainsSpan(FromNullTerminated(info->name), "Keystation Mini"_s) ||
                ContainsSpan(FromNullTerminated(info->name), "Seaboard"_s)) {
                id_to_use = i;
                break;
            }
        }
    }

    if (auto result = Pm_OpenInput(&standalone.midi_stream, *id_to_use, nullptr, 200, nullptr, nullptr);
        result != pmNoError) {
        standalone.floe_host_ext.standalone_midi_device_error = true;
        LogError(ModuleName::Standalone, "Pm_OpenInput: {}", FromNullTerminated(Pm_GetErrorText(result)));
        return false;
    }

    return true;
}

static void CloseMidi(Standalone& standalone) {
    if (standalone.midi_stream) Pm_Close(standalone.midi_stream);
    Pm_Terminate();
}

static bool OpenAudio(Standalone& standalone) {
    ASSERT(!standalone.audio_device);
    standalone.audio_device.Emplace();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0; // use default
    config.dataCallback = AudioCallback;
    config.pUserData = &standalone;
    config.periodSizeInFrames = 1024; // only a hint
    config.performanceProfile = ma_performance_profile_low_latency;
    config.noClip = true;
    config.noPreSilencedOutputBuffer = true;

    if (ma_device_init(nullptr, &config, &*standalone.audio_device) != MA_SUCCESS) {
        standalone.floe_host_ext.standalone_audio_device_error = true;
        return false;
    }

    standalone.plugin.activate(&standalone.plugin,
                               standalone.audio_device->sampleRate,
                               config.periodSizeInFrames / 2,
                               config.periodSizeInFrames * 2);

    constexpr usize k_max_frames = 2096;
    auto alloc = PageAllocator::Instance().AllocateExactSizeUninitialised<f32>(k_max_frames * 2);
    standalone.audio_buffers[0] = alloc.SubSpan(0, k_max_frames);
    standalone.audio_buffers[1] = alloc.SubSpan(k_max_frames, k_max_frames);

    ma_device_start(&*standalone.audio_device);

    return true;
}

static void CloseAudio(Standalone& standalone) {
    ASSERT(standalone.audio_device);
    if (standalone.audio_stream_state.Exchange(Standalone::AudioStreamState::CloseRequested,
                                               RmwMemoryOrder::Acquire) == Standalone::AudioStreamState::Open)
        while (standalone.audio_stream_state.Load(LoadMemoryOrder::Relaxed) !=
               Standalone::AudioStreamState::Closed)
            SleepThisThread(2);

    ma_device_uninit(&*standalone.audio_device);

    standalone.plugin.deactivate(&standalone.plugin);
}

static PuglStatus OnEvent(PuglView* view, PuglEvent const* event) {
    ZoneScoped;
    ZoneNameF("Standalone: %s", PuglEventString(event->type));

    auto& p = *(Standalone*)puglGetHandle(view);
    if (PanicOccurred()) return PUGL_UNKNOWN_ERROR;

    switch (event->type) {
        case PUGL_CLOSE: {
            p.quit = true;
            break;
        }
        case PUGL_CONFIGURE: {
            LogDebug(ModuleName::Standalone,
                     "Parent configure: {}x{}, {}x{}, mapped: {}, resizing: {}",
                     event->configure.x,
                     event->configure.y,
                     event->configure.width,
                     event->configure.height,
                     event->configure.style & PUGL_VIEW_STYLE_MAPPED,
                     event->configure.style & PUGL_VIEW_STYLE_RESIZING);
            if (event->configure.style & PUGL_VIEW_STYLE_MAPPED) {
                auto gui = (clap_plugin_gui const*)p.plugin.get_extension(&p.plugin, CLAP_EXT_GUI);
                ASSERT(gui);
                if (gui->can_resize(&p.plugin)) {
                    // CLAP always wants window sizes in the OS's pixel units.
                    auto const scale_factor = puglGetScaleFactor(view);
                    auto width = (u32)(event->configure.width / scale_factor);
                    auto height = (u32)(event->configure.height / scale_factor);
                    if (gui->adjust_size(&p.plugin, &width, &height)) gui->set_size(&p.plugin, width, height);
                }
            }
            break;
        }
        case PUGL_FOCUS_IN:
        case PUGL_FOCUS_OUT: {
            // TODO: on macOS, we are not getting focus events in the child
            break;
        }
        case PUGL_NOTHING:
        case PUGL_REALIZE:
        case PUGL_UNREALIZE:
        case PUGL_UPDATE:
        case PUGL_EXPOSE:
        case PUGL_KEY_PRESS:
        case PUGL_KEY_RELEASE:
        case PUGL_TEXT:
        case PUGL_POINTER_IN:
        case PUGL_POINTER_OUT:
        case PUGL_BUTTON_PRESS:
        case PUGL_BUTTON_RELEASE:
        case PUGL_MOTION:
        case PUGL_SCROLL:
        case PUGL_CLIENT:
        case PUGL_TIMER:
        case PUGL_LOOP_ENTER:
        case PUGL_LOOP_LEAVE:
        case PUGL_DATA_OFFER:
        case PUGL_DATA: break;
    }
    return PUGL_SUCCESS;
}

ErrorCodeCategory const pugl_error_category {
    .category_id = "PUGL",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((PuglStatus)code.code) {
            case PUGL_SUCCESS: str = "success"; break;
            case PUGL_FAILURE: str = "failure"; break;
            case PUGL_UNKNOWN_ERROR: str = "unknown error"; break;
            case PUGL_BAD_BACKEND: str = "bad backend"; break;
            case PUGL_BAD_CONFIGURATION: str = "bad configuration"; break;
            case PUGL_BAD_PARAMETER: str = "bad parameter"; break;
            case PUGL_BACKEND_FAILED: str = "backend failed"; break;
            case PUGL_REGISTRATION_FAILED: str = "registration failed"; break;
            case PUGL_REALIZE_FAILED: str = "realize failed"; break;
            case PUGL_SET_FORMAT_FAILED: str = "set format failed"; break;
            case PUGL_CREATE_CONTEXT_FAILED: str = "create context failed"; break;
            case PUGL_UNSUPPORTED: str = "unsupported"; break;
            case PUGL_NO_MEMORY: str = "no memory"; break;
        }
        return writer.WriteChars(str);
    },
};
inline ErrorCodeCategory const& ErrorCategoryForEnum(PuglStatus) { return pugl_error_category; }

enum class StandaloneError {
    DeviceError,
    PluginInterfaceError,
};
ErrorCodeCategory const standalone_error_category {
    .category_id = "STND",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((StandaloneError)code.code) {
            case StandaloneError::DeviceError: str = "device error"; break;
            case StandaloneError::PluginInterfaceError: str = "plugin interface error"; break;
        }
        return writer.WriteChars(str);
    },
};
inline ErrorCodeCategory const& ErrorCategoryForEnum(StandaloneError) { return standalone_error_category; }

#define TRY_PUGL(expression)                                                                                 \
    ({                                                                                                       \
        const PuglStatus&& CONCAT(st, __LINE__) = (expression);                                              \
        if (CONCAT(st, __LINE__) != PUGL_SUCCESS) return ErrorCode {CONCAT(st, __LINE__)};                   \
    })

#define TRY_CLAP(expression)                                                                                 \
    ({                                                                                                       \
        const auto CONCAT(st, __LINE__) = (expression);                                                      \
        if (!CONCAT(st, __LINE__)) return ErrorCode {StandaloneError::PluginInterfaceError};                 \
    })

extern clap_plugin_entry const clap_entry;

static ErrorCodeOr<void> Main() {
    FixedSizeAllocator<Kb(1)> allocator {nullptr};

    GlobalInit({
        .init_error_reporting = true,
        .set_main_thread = true,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    clap_entry.init("");
    DEFER { clap_entry.deinit(); };

    Standalone standalone {};

    standalone.plugin.init(&standalone.plugin);
    DEFER { standalone.plugin.destroy(&standalone.plugin); };

    if (!OpenMidi(standalone)) {
        LogError(ModuleName::Standalone, "Could not open Midi");
        return ErrorCode {StandaloneError::DeviceError};
    }
    DEFER { CloseMidi(standalone); };
    if (!OpenAudio(standalone)) {
        LogError(ModuleName::Standalone, "Could not open Audio");
        return ErrorCode {StandaloneError::DeviceError};
    }
    DEFER { CloseAudio(standalone); };

    standalone.gui_world = puglNewWorld(PUGL_PROGRAM, 0);
    DEFER { puglFreeWorld(standalone.gui_world); };
    TRY_PUGL(puglSetWorldString(standalone.gui_world, PUGL_CLASS_NAME, "Floe Standalone"));

    standalone.floe_host_ext.pugl_world = standalone.gui_world;

    standalone.gui_view = puglNewView(standalone.gui_world);
    DEFER { puglFreeView(standalone.gui_view); };
    TRY_PUGL(puglSetViewHint(standalone.gui_view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON));
    TRY_PUGL(puglSetBackend(standalone.gui_view, puglStubBackend()));
    puglSetHandle(standalone.gui_view, &standalone);
    TRY_PUGL(puglSetEventFunc(standalone.gui_view, OnEvent));
    TRY_PUGL(puglSetViewString(standalone.gui_view, PUGL_WINDOW_TITLE, "Floe"));

    auto gui = (clap_plugin_gui const*)standalone.plugin.get_extension(&standalone.plugin, CLAP_EXT_GUI);
    TRY_CLAP(gui);

    TRY_CLAP(gui->create(&standalone.plugin, k_supported_gui_api, false));

    u32 clap_width;
    u32 clap_height;
    TRY_CLAP(gui->get_size(&standalone.plugin, &clap_width, &clap_height));

    {
        auto const original_width = clap_width;
        auto const original_height = clap_height;
        TRY_CLAP(gui->adjust_size(&standalone.plugin, &clap_width, &clap_height));

        // We should have created a view that conforms to our own requirements.
        ASSERT_EQ(original_width, clap_width);
        ASSERT_EQ(original_height, clap_height);
    }

    auto const size = *ClapPixelsToPhysicalPixels(standalone.gui_view, clap_width, clap_height);
    TRY_PUGL(
        puglSetSizeHint(standalone.gui_view, PUGL_DEFAULT_SIZE, (PuglSpan)size.width, (PuglSpan)size.height));
    puglSetSizeHint(standalone.gui_view, PUGL_CURRENT_SIZE, (PuglSpan)size.width, (PuglSpan)size.height);

    {
        clap_gui_resize_hints resize_hints;
        TRY_CLAP(gui->get_resize_hints(&standalone.plugin, &resize_hints));
        if (resize_hints.can_resize_vertically && resize_hints.can_resize_horizontally) {
            TRY_PUGL(
                puglSetViewHint(standalone.gui_view, PUGL_RESIZABLE, gui->can_resize(&standalone.plugin)));
            if (resize_hints.preserve_aspect_ratio)
                TRY_PUGL(puglSetSizeHint(standalone.gui_view,
                                         PUGL_FIXED_ASPECT,
                                         (PuglSpan)resize_hints.aspect_ratio_width,
                                         (PuglSpan)resize_hints.aspect_ratio_height));
        }
    }

    TRY_PUGL(puglRealize(standalone.gui_view));
    DEFER { puglUnrealize(standalone.gui_view); };

    clap_window const clap_window = {
        .api = k_supported_gui_api,
        .ptr = (void*)puglGetNativeView(standalone.gui_view),
    };
    TRY_CLAP(gui->set_parent(&standalone.plugin, &clap_window));

    TRY_PUGL(puglShow(standalone.gui_view, PUGL_SHOW_RAISE));
    TRY_CLAP(gui->show(&standalone.plugin));

    while (!standalone.quit) {
        if (standalone.callback_requested.Exchange(false, RmwMemoryOrder::Relaxed))
            standalone.plugin.on_main_thread(&standalone.plugin);

        if (standalone.resize_hints_changed.Exchange(false, RmwMemoryOrder::Relaxed)) {
            clap_gui_resize_hints resize_hints;
            if (gui->get_resize_hints(&standalone.plugin, &resize_hints)) {
                if (resize_hints.can_resize_vertically && resize_hints.can_resize_horizontally) {
                    if (puglSetViewHint(standalone.gui_view,
                                        PUGL_RESIZABLE,
                                        gui->can_resize(&standalone.plugin)) != PUGL_SUCCESS) {
                        PanicIfReached();
                    };
                    if (resize_hints.preserve_aspect_ratio)
                        if (puglSetSizeHint(standalone.gui_view,
                                            PUGL_FIXED_ASPECT,
                                            (PuglSpan)resize_hints.aspect_ratio_width,
                                            (PuglSpan)resize_hints.aspect_ratio_height) != PUGL_SUCCESS) {
                            PanicIfReached();
                        };
                }
            }
        }

        if (auto const requested_clap_size =
                standalone.requested_resize.Exchange(k_invalid_ui_size, RmwMemoryOrder::AcquireRelease);
            requested_clap_size != k_invalid_ui_size) {
            auto const physical_pixels = *ClapPixelsToPhysicalPixels(standalone.gui_view,
                                                                     requested_clap_size.width,
                                                                     requested_clap_size.height);
            LogDebug(ModuleName::Standalone,
                     "Handling resize request, setting parent window to: {} x {}",
                     physical_pixels.width,
                     physical_pixels.height);
            puglSetSizeHint(standalone.gui_view,
                            PUGL_CURRENT_SIZE,
                            physical_pixels.width,
                            physical_pixels.height);
        }

        auto const st = puglUpdate(standalone.gui_world, 1.0 / 60.0);
        if (st != PUGL_SUCCESS && st != PUGL_FAILURE) return ErrorCode {st};
    }

    gui->destroy(&standalone.plugin);
    return k_success;
}

int main(int, char**) {
    auto _ = EnterLogicalMainThread();
    auto const o = Main();
    if (o.HasError()) {
        LogError(ModuleName::Standalone, "Standalone error: {}", o.Error());
        return 1;
    }
    return 0;
}
