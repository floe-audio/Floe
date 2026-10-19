// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/audio-buffer.h>
#include <clap/entry.h>
#include <clap/events.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/thread-check.h>
#include <clap/ext/timer-support.h>
#include <clap/factory/plugin-factory.h>
#include <clap/host.h>
#include <clap/plugin.h>
#include <clap/process.h>
#include <miniaudio.h>
#include <portmidi.h>
#include <pugl/pugl.h>
#include <pugl/stub.h>

#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/global.hpp"

#include "offline_render.hpp"
#include "plugin/gui_framework/app_window_sizes.hpp"
#include "plugin/plugin/plugin.hpp"
#include "standalone_device_manager.hpp"

// A very simple 'standalone' host for development purposes.
// The wrapper (window, event loop, audio/MIDI devices) persists while the plugin (DSO, CLAP entry,
// plugin instance, GUI) can be loaded and unloaded multiple times to support hot-reloading.

constexpr UiSize32 k_invalid_ui_size = {0, 0};

struct ClapEntrySource {
    clap_plugin_entry const* entry; // always valid after construction
    Optional<LibraryHandle> library_handle; // set only when loaded from DSO
};

struct Standalone;

struct PluginInstance {
    PluginInstance(Standalone& s, bool is_external) : standalone(s), is_external_plugin(is_external) {
        host.host_data = this;
    }

    Standalone& standalone;

    clap_host_params const host_params {
        .rescan =
            [](clap_host_t const* h, clap_param_rescan_flags) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
            },
        .clear =
            [](clap_host_t const* h, clap_id, clap_param_clear_flags) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
            },
        .request_flush =
            [](clap_host_t const* h) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
            },
    };

    clap_host_gui const host_gui {
        .resize_hints_changed =
            [](clap_host_t const* h) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                inst.resize_hints_changed.Store(true, StoreMemoryOrder::Relaxed);
            },
        .request_resize =
            [](clap_host_t const* h, uint32_t width, uint32_t height) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                inst.requested_resize.Exchange({width, height}, RmwMemoryOrder::AcquireRelease);
                return true;
            },

        .request_show = [](clap_host_t const*) { return false; },
        .request_hide = [](clap_host_t const*) { return false; },
        .closed = [](clap_host_t const*, bool) { Panic("floating windows are not supported"); },
    };

    clap_host_thread_check host_thread_check {};

    struct HostTimer {
        clap_id id;
        f64 period_seconds;
        TimePoint next_fire;
    };
    DynamicArrayBounded<HostTimer, 4> timers {};
    clap_id next_timer_id {};

    clap_host_timer_support const host_timer_support {
        .register_timer =
            [](clap_host_t const* h, uint32_t period_ms, clap_id* timer_id) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                ASSERT(timer_id);
                auto const period_seconds = (f64)period_ms / 1000.0;
                if (!dyn::Append(inst.timers,
                                 {
                                     .id = inst.next_timer_id,
                                     .period_seconds = period_seconds,
                                     .next_fire = TimePoint::Now() + period_seconds,
                                 }))
                    return false;
                *timer_id = inst.next_timer_id++;
                return true;
            },
        .unregister_timer =
            [](clap_host_t const* h, clap_id timer_id) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                return dyn::RemoveValueIf(inst.timers,
                                          [&](HostTimer const& t) { return t.id == timer_id; }) != 0;
            },
    };

    clap_host_t host {
        .clap_version = CLAP_VERSION,
        .host_data = this,
        .name = k_floe_standalone_host_name,
        .vendor = FLOE_VENDOR,
        .url = FLOE_HOMEPAGE_URL,
        .version = "1",

        .get_extension = [](clap_host_t const* ch, char const* extension_id) -> void const* {
            auto& inst = *(PluginInstance*)ch->host_data;
            ASSERT(inst.plugin_created);

            if (NullTermStringsEqual(extension_id, CLAP_EXT_PARAMS))
                return &inst.host_params;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_GUI))
                return &inst.host_gui;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_THREAD_CHECK))
                return &inst.host_thread_check;
            // A windowed GUI is driven by our shared pugl world, so timers would double-update it.
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_TIMER_SUPPORT) && inst.offscreen_gui)
                return &inst.host_timer_support;
            else if (NullTermStringsEqual(extension_id, k_floe_clap_extension_id))
                return &inst.floe_host_ext;

            return nullptr;
        },
        .request_restart = [](clap_host_t const*) { PanicIfReached(); },
        .request_process =
            [](clap_host_t const* h) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                // Don't think we need to do anything here because we always call process() regardless
            },
        .request_callback =
            [](clap_host_t const* h) {
                auto& inst = *(PluginInstance*)h->host_data;
                ASSERT(inst.plugin_created);
                inst.callback_requested.Store(true, StoreMemoryOrder::Relaxed);
            },
    };

    bool is_external_plugin;
    Atomic<bool> callback_requested {false};
    FloeClapExtensionHost floe_host_ext {};

    Atomic<UiSize32> requested_resize {k_invalid_ui_size};
    Atomic<bool> resize_hints_changed {false};

    bool plugin_created = false; // plugins are forbidden to call host APIs while creating
    clap_plugin const* plugin {};
    clap_plugin_gui const* gui {};
    bool offscreen_gui = false;

    ClapEntrySource entry_source; // owns the library handle for this load cycle
};

// Controls audio thread access to the plugin instance. The main thread transitions through these
// states to safely hand off and reclaim the plugin instance without races.
enum class PluginInstanceState : u8 {
    // No plugin loaded. Audio thread outputs silence.
    Inactive,
    // Plugin is loaded and active. Audio thread calls process().
    Active,
    // Main thread has requested the audio thread to stop using the plugin.
    // Audio thread sees this, transitions to DeactivateAcknowledged.
    DeactivateRequest,
    // Audio thread has acknowledged it is no longer using the plugin.
    // Main thread sees this and proceeds with teardown.
    DeactivateAcknowledged,
};

// Persistent wrapper state that survives plugin reloads.
struct Standalone {
    u64 main_thread_id {CurrentThreadId()};

    DeviceManager devices {};

    // Scratch L/R channel buffers used to de-interleave miniaudio's output for clap_process() and
    // then re-interleave the result. Allocated once at startup.
    Array<Span<f32>, 2> render_scratch {};

    // Created on first need: the offscreen GUI doesn't use them.
    PuglWorld* gui_world {};
    PuglView* gui_view {};
    bool view_realized {};
    char const* app_id {};

    Optional<UiSize> fixed_window_size {};
    bool warned_fixed_window_size_mismatch {};
    Optional<String> ui_dump_path {};
    Optional<f64> offline_sample_rate {};
    bool headless = false;
    // Attempt to render the GUI without a window; cleared if the plugin doesn't support it.
    bool offscreen_gui = false;
    bool quit = false;

    // Handshake with the stdin MIDI reader thread. Reader sets `eof` on stdin close; main sets
    // `stop` to ask the reader to exit so it can be joined before teardown.
    struct {
        Atomic<bool> stop {false};
        Atomic<bool> eof {false};
    } stdin_midi;

    // Audio thread writes, main thread reads.
    struct {
        f32 threshold_amp {}; // 0 when not tracking
        Atomic<u64> frames_rendered {};
        Atomic<u64> audible_end_frame {};
    } output_level;

    // Plugin instance access is controlled by plugin_instance_state. The pointer is only valid
    // to read when the state protocol allows it (Active for audio thread, Active for main thread
    // since main thread controls transitions).
    Atomic<PluginInstanceState> plugin_instance_state {PluginInstanceState::Inactive};
    PluginInstance* plugin_instance {};
};

// Audio thread. Called from the device manager's audio callback with pre-decoded MIDI events.
// Returns false (silence) when the plugin is not active or is being deactivated.
static bool RenderAudio(void* ctx,
                        f32* output_interleaved,
                        u32 num_frames,
                        clap_event_midi const* midi_events,
                        u32 num_midi_events) {
    auto& standalone = *(Standalone*)ctx;

    auto const plugin_state = standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire);
    if (plugin_state == PluginInstanceState::DeactivateRequest) {
        standalone.plugin_instance_state.Store(PluginInstanceState::DeactivateAcknowledged,
                                               StoreMemoryOrder::Release);
        return false;
    }
    if (plugin_state != PluginInstanceState::Active) return false;

    auto* inst = standalone.plugin_instance;

    f32* channels[2] = {standalone.render_scratch[0].data, standalone.render_scratch[1].data};

    // Fill memory with garbage.
    for (auto& chan : channels)
        for (auto const i : Range(num_frames))
            chan[i] = -1000.0f;

    clap_event_transport transport {};
    transport.header = {
        .size = sizeof(transport),
        .time = 0,
        .space_id = CLAP_CORE_EVENT_SPACE_ID,
        .type = CLAP_EVENT_TRANSPORT,
        .flags = 0,
    };
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
    transport.tempo = standalone.devices.tempo.Load(LoadMemoryOrder::Relaxed);

    clap_process_t process {};
    process.frames_count = num_frames;
    process.steady_time = -1;
    process.transport = &transport;

    clap_audio_buffer_t buffer {};
    buffer.channel_count = 2;
    buffer.data32 = channels;

    process.audio_outputs = &buffer;
    process.audio_outputs_count = 1;

    struct EventCtx {
        clap_event_midi const* events;
        u32 count;
    };
    EventCtx const event_ctx {.events = midi_events, .count = num_midi_events};

    clap_input_events const in_events {
        .ctx = (void*)&event_ctx,
        .size = [](const struct clap_input_events* list) -> u32 {
            return ((EventCtx const*)list->ctx)->count;
        },
        .get = [](const struct clap_input_events* list, uint32_t index) -> clap_event_header_t const* {
            return &((EventCtx const*)list->ctx)->events[index].header;
        },
    };

    clap_output_events const out_events {
        .ctx = nullptr,
        .try_push = [](clap_output_events const*, clap_event_header const*) -> bool { return false; },
    };

    process.in_events = &in_events;
    process.out_events = &out_events;

    inst->plugin->process(inst->plugin, &process);

    for (auto const chan : Range(2))
        for (auto const i : Range(num_frames)) {
            constexpr f32 k_hard_limit = 3.0f;
            if (channels[chan][i] > k_hard_limit)
                channels[chan][i] = k_hard_limit;
            else if (channels[chan][i] < -k_hard_limit)
                channels[chan][i] = -k_hard_limit;
        }

    if (auto const threshold = standalone.output_level.threshold_amp; threshold > 0) {
        auto const frames_rendered = standalone.output_level.frames_rendered.Load(LoadMemoryOrder::Relaxed);
        for (auto frame_index = num_frames; frame_index-- > 0;) {
            if (Abs(channels[0][frame_index]) > threshold || Abs(channels[1][frame_index]) > threshold) {
                standalone.output_level.audible_end_frame.Store(frames_rendered + frame_index + 1,
                                                                StoreMemoryOrder::Relaxed);
                break;
            }
        }
        standalone.output_level.frames_rendered.Store(frames_rendered + num_frames,
                                                      StoreMemoryOrder::Relaxed);
    }

    CopySeparateChannelsToInterleaved(output_interleaved, channels[0], channels[1], num_frames);
    return true;
}

// Main thread. Tear the plugin down around an audio-device swap. No-op when nothing is active.
static void DeactivateForDeviceSwap(void* ctx) {
    auto& standalone = *(Standalone*)ctx;
    if (standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire) != PluginInstanceState::Active)
        return;
    standalone.plugin_instance_state.Store(PluginInstanceState::Inactive, StoreMemoryOrder::Release);
    if (auto* inst = standalone.plugin_instance) {
        inst->plugin->stop_processing(inst->plugin);
        inst->plugin->deactivate(inst->plugin);
    }
}

// Main thread. Bring the plugin back up against the freshly-opened audio device.
static void ActivateAfterDeviceSwap(void* ctx, f64 sample_rate, u32 period_frames, u32 max_block_frames) {
    auto& standalone = *(Standalone*)ctx;
    auto* inst = standalone.plugin_instance;
    if (!inst) return;
    inst->plugin->activate(inst->plugin, sample_rate, period_frames, max_block_frames);
    inst->plugin->start_processing(inst->plugin);
    standalone.plugin_instance_state.Store(PluginInstanceState::Active, StoreMemoryOrder::Release);
}

static PuglStatus OnEvent(PuglView* view, PuglEvent const* event) {
    ZoneScoped;
    ZoneNameF("Standalone: %s", PuglEventString(event->type));

    auto& standalone = *(Standalone*)puglGetHandle(view);
    if (PanicOccurred()) return PUGL_UNKNOWN_ERROR;

    switch (event->type) {
        case PUGL_CLOSE: {
            standalone.quit = true;
            break;
        }
        case PUGL_CONFIGURE: {
            if (standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire) !=
                PluginInstanceState::Active)
                break;
            auto* inst = standalone.plugin_instance;

            LogDebug(ModuleName::Standalone,
                     "Parent configure: {}x{}, {}x{}, mapped: {}, resizing: {}",
                     event->configure.x,
                     event->configure.y,
                     event->configure.width,
                     event->configure.height,
                     event->configure.style & PUGL_VIEW_STYLE_MAPPED,
                     event->configure.style & PUGL_VIEW_STYLE_RESIZING);
            if (event->configure.style & PUGL_VIEW_STYLE_MAPPED) {
                ASSERT(inst->gui);
                if (standalone.fixed_window_size) {
                    // Commit the size as-is: adjust_size would shrink it to fit the screen.
                    auto const fixed = *standalone.fixed_window_size;
                    inst->gui->set_size(inst->plugin, fixed.width, fixed.height);

                    auto const expected = *ClapPixelsToPhysicalPixels(view, fixed.width, fixed.height);
                    if ((event->configure.width != expected.width ||
                         event->configure.height != expected.height) &&
                        !Exchange(standalone.warned_fixed_window_size_mismatch, true)) {
                        StdPrintF(StdStream::Err,
                                  "Warning: --fixed-window-size requested {}x{} but the window is {}x{}; the "
                                  "window manager may have limited it to the screen\n",
                                  expected.width,
                                  expected.height,
                                  event->configure.width,
                                  event->configure.height);
                    }
                } else if (inst->gui->can_resize(inst->plugin)) {
                    // CLAP always wants window sizes in the OS's pixel units.
                    auto const scale_factor = puglGetScaleFactor(view);
                    auto width = (u32)(event->configure.width / scale_factor);
                    auto height = (u32)(event->configure.height / scale_factor);
                    if (inst->gui->adjust_size(inst->plugin, &width, &height))
                        inst->gui->set_size(inst->plugin, width, height);
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

enum class StandaloneError : u8 {
    DeviceError,
    PluginInterfaceError,
    WindowError,
    RenderFileError,
};
ErrorCodeCategory const standalone_error_category {
    .category_id = "STND",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((StandaloneError)code.code) {
            case StandaloneError::DeviceError: str = "device error"; break;
            case StandaloneError::PluginInterfaceError: str = "plugin interface error"; break;
            case StandaloneError::WindowError: str = "window error"; break;
            case StandaloneError::RenderFileError: str = "failed to write render file"; break;
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

static ErrorCodeOr<ClapEntrySource> LoadClapEntry(Optional<String> dso_path, ArenaAllocator& arena) {
    if (!dso_path) {
        return ClapEntrySource {
            .entry = &clap_entry,
            .library_handle = k_nullopt,
        };
    }

    auto const handle = TRY(LoadLibrary(({
        auto p = TRY(AbsolutePath(arena, *dso_path));
        if constexpr (IS_MACOS)
            p = path::JoinAppendResizeAllocation(arena, p, Array {"Contents/MacOS/Floe"_s});
        p;
    })));

    auto const entry = (clap_plugin_entry const*)TRY(SymbolFromLibrary(handle, "clap_entry"));
    ASSERT(entry);

    return ClapEntrySource {
        .entry = entry,
        .library_handle = handle,
    };
}

static ErrorCodeOr<void> CreateGuiWorldAndView(Standalone& standalone) {
    if (standalone.gui_world) return k_success;

    standalone.gui_world = puglNewWorld(PUGL_PROGRAM, 0);
    if (!standalone.gui_world) {
        LogError(ModuleName::Standalone, "Could not setup window state");
        return ErrorCode {StandaloneError::WindowError};
    }
    TRY_PUGL(puglSetWorldString(standalone.gui_world, PUGL_CLASS_NAME, standalone.app_id));

    standalone.gui_view = puglNewView(standalone.gui_world);
    TRY_PUGL(puglSetViewHint(standalone.gui_view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON));
    TRY_PUGL(puglSetBackend(standalone.gui_view, puglStubBackend()));
    puglSetHandle(standalone.gui_view, &standalone);
    TRY_PUGL(puglSetEventFunc(standalone.gui_view, OnEvent));
    TRY_PUGL(puglSetViewString(standalone.gui_view, PUGL_WINDOW_TITLE, "Floe"));
    return k_success;
}

static ErrorCodeOr<void>
LoadPluginInstance(Standalone& standalone, Optional<String> dso_path, ArenaAllocator& arena) {
    ASSERT(standalone.plugin_instance_state.Load(LoadMemoryOrder::Relaxed) == PluginInstanceState::Inactive);

    bool success = false;

    auto entry_source = TRY(LoadClapEntry(dso_path, arena));
    DEFER {
        if (!success && entry_source.library_handle) UnloadLibrary(*entry_source.library_handle);
    };

    entry_source.entry->init(nullptr);
    DEFER {
        if (!success) entry_source.entry->deinit();
    };

    auto const factory = (clap_plugin_factory const*)entry_source.entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory) return ErrorCode {StandaloneError::PluginInterfaceError};

    bool const is_external = entry_source.library_handle.HasValue();

    // Allocate and construct PluginInstance first so we can pass its host to create_plugin.
    // The plugin is set to null initially and assigned after create_plugin succeeds.
    auto inst = arena.New<PluginInstance>(standalone, is_external);
    inst->entry_source = entry_source;

    // Set up thread check callbacks that reference the standalone
    inst->host_thread_check = {
        .is_main_thread =
            [](clap_host_t const* h) {
                auto& pi = *(PluginInstance*)h->host_data;
                ASSERT(pi.plugin_created);
                return CurrentThreadId() == pi.standalone.main_thread_id;
            },
        .is_audio_thread =
            [](clap_host_t const* h) {
                auto& pi = *(PluginInstance*)h->host_data;
                ASSERT(pi.plugin_created);
                return CurrentThreadId() ==
                       pi.standalone.devices.audio.stream.thread_id.Load(LoadMemoryOrder::Relaxed);
            },
    };

    inst->floe_host_ext.error_notifications = &standalone.devices.error_notifications;
    SetupHostDeviceCallbacks(inst->floe_host_ext, standalone.devices);

    inst->plugin = factory->create_plugin(factory, &inst->host, g_plugin_info.id);
    if (!inst->plugin) return ErrorCode {StandaloneError::PluginInterfaceError};
    inst->plugin_created = true;

    inst->plugin->init(inst->plugin);

    // If the audio device failed to open we still activate the plugin with conservative defaults so
    // the GUI (and its errors panel) come up; the audio thread is silent until a device is selected.
    auto const& device = standalone.devices.audio.stream.device;
    auto const sample_rate = standalone.offline_sample_rate ? *standalone.offline_sample_rate
                             : device                       ? device->sampleRate
                                                            : 48000.0;
    auto const min_block_size = standalone.offline_sample_rate ? 1u
                                : device                       ? device->playback.internalPeriodSizeInFrames
                                                               : 1024u;
    // InitAudioDevice rejects devices whose period exceeds k_max_audio_buffer_frames, so this is
    // always the max block size.
    inst->plugin->activate(inst->plugin, sample_rate, min_block_size, (u32)k_max_audio_buffer_frames);

    inst->plugin->start_processing(inst->plugin);

    if (standalone.headless) {
        // Make visible to audio thread last. The Release ensures the plugin_instance pointer write
        // is visible before the state becomes Active.
        standalone.plugin_instance = inst;
        standalone.plugin_instance_state.Store(PluginInstanceState::Active, StoreMemoryOrder::Release);
        success = true;
        return k_success;
    }

    // Set up GUI
    inst->gui = (clap_plugin_gui const*)inst->plugin->get_extension(inst->plugin, CLAP_EXT_GUI);
    TRY_CLAP(inst->gui);

    if (standalone.offscreen_gui &&
        inst->gui->is_api_supported(inst->plugin, k_floe_offscreen_gui_api, false)) {
        inst->offscreen_gui = true;
        TRY_CLAP(inst->gui->create(inst->plugin, k_floe_offscreen_gui_api, false));
        if (standalone.fixed_window_size)
            TRY_CLAP(inst->gui->set_size(inst->plugin,
                                         standalone.fixed_window_size->width,
                                         standalone.fixed_window_size->height));
        TRY_CLAP(inst->gui->show(inst->plugin));

        standalone.plugin_instance = inst;
        standalone.plugin_instance_state.Store(PluginInstanceState::Active, StoreMemoryOrder::Release);
        success = true;
        return k_success;
    }
    if (standalone.offscreen_gui) {
        LogInfo(ModuleName::Standalone, "Offscreen GUI not supported, using a window");
        standalone.offscreen_gui = false;
    }

    TRY(CreateGuiWorldAndView(standalone));
    inst->floe_host_ext.pugl_world = standalone.gui_world;
    inst->floe_host_ext.fixed_window_size = standalone.fixed_window_size.HasValue();

    TRY_CLAP(inst->gui->create(inst->plugin, k_supported_gui_api, false));

    u32 clap_width;
    u32 clap_height;
    TRY_CLAP(inst->gui->get_size(inst->plugin, &clap_width, &clap_height));

    if (standalone.fixed_window_size) {
        // Override the plugin's preferred size with our fixed value. PUGL_CONFIGURE will commit it
        // via set_size once the parent view is mapped at the matching physical size below.
        clap_width = standalone.fixed_window_size->width;
        clap_height = standalone.fixed_window_size->height;
    } else {
        auto const original_width = clap_width;
        auto const original_height = clap_height;
        TRY_CLAP(inst->gui->adjust_size(inst->plugin, &clap_width, &clap_height));

        // We should have created a view that conforms to our own requirements.
        ASSERT_EQ(original_width, clap_width);
        ASSERT_EQ(original_height, clap_height);
    }

    if (!standalone.view_realized) {
        auto const size = *ClapPixelsToPhysicalPixels(standalone.gui_view, clap_width, clap_height);
        TRY_PUGL(puglSetSizeHint(standalone.gui_view,
                                 PUGL_DEFAULT_SIZE,
                                 (PuglSpan)size.width,
                                 (PuglSpan)size.height));
        puglSetSizeHint(standalone.gui_view, PUGL_CURRENT_SIZE, (PuglSpan)size.width, (PuglSpan)size.height);

        if (standalone.fixed_window_size) {
            TRY_PUGL(puglSetViewHint(standalone.gui_view, PUGL_RESIZABLE, false));
            TRY_PUGL(puglSetSizeHint(standalone.gui_view,
                                     PUGL_MIN_SIZE,
                                     (PuglSpan)size.width,
                                     (PuglSpan)size.height));
            TRY_PUGL(puglSetSizeHint(standalone.gui_view,
                                     PUGL_MAX_SIZE,
                                     (PuglSpan)size.width,
                                     (PuglSpan)size.height));
        } else {
            clap_gui_resize_hints resize_hints;
            TRY_CLAP(inst->gui->get_resize_hints(inst->plugin, &resize_hints));
            if (resize_hints.can_resize_vertically && resize_hints.can_resize_horizontally) {
                TRY_PUGL(puglSetViewHint(standalone.gui_view,
                                         PUGL_RESIZABLE,
                                         inst->gui->can_resize(inst->plugin)));
                if (resize_hints.preserve_aspect_ratio)
                    TRY_PUGL(puglSetSizeHint(standalone.gui_view,
                                             PUGL_FIXED_ASPECT,
                                             (PuglSpan)resize_hints.aspect_ratio_width,
                                             (PuglSpan)resize_hints.aspect_ratio_height));
            }
        }

        TRY_PUGL(puglRealize(standalone.gui_view));
        standalone.view_realized = true;
    }

    clap_window const clap_window = {
        .api = k_supported_gui_api,
        .ptr = (void*)puglGetNativeView(standalone.gui_view),
    };
    TRY_CLAP(inst->gui->set_parent(inst->plugin, &clap_window));

    TRY_PUGL(puglShow(standalone.gui_view, PUGL_SHOW_RAISE));
    TRY_CLAP(inst->gui->show(inst->plugin));

    if (standalone.ui_dump_path) {
        auto const floe_ext =
            (FloeClapExtension const*)inst->plugin->get_extension(inst->plugin, k_floe_clap_extension_id);
        if (!floe_ext || !floe_ext->set_ui_dump_path ||
            !floe_ext->set_ui_dump_path(inst->plugin, *standalone.ui_dump_path))
            LogWarning(ModuleName::Standalone, "Failed to set UI dump path");
    }

    // Make visible to audio thread last. The Release ensures the plugin_instance pointer write
    // is visible before the state becomes Active.
    standalone.plugin_instance = inst;
    standalone.plugin_instance_state.Store(PluginInstanceState::Active, StoreMemoryOrder::Release);

    success = true;
    return k_success;
}

static void UnloadPluginInstance(Standalone& standalone) {
    if (standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire) != PluginInstanceState::Active)
        return;

    auto& audio_stream = standalone.devices.audio.stream;
    auto const audio_running =
        audio_stream.device.HasValue() && !audio_stream.lost.Load(LoadMemoryOrder::Acquire);

    if (audio_running) {
        standalone.plugin_instance_state.Store(PluginInstanceState::DeactivateRequest,
                                               StoreMemoryOrder::Release);
        while (standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire) !=
               PluginInstanceState::DeactivateAcknowledged) {
            if (audio_stream.lost.Load(LoadMemoryOrder::Acquire)) break;
            SleepThisThread(1);
        }
    }

    // Now safe: audio thread has committed to not using the instance
    standalone.plugin_instance_state.Store(PluginInstanceState::Inactive, StoreMemoryOrder::Release);

    auto* inst = standalone.plugin_instance;
    standalone.plugin_instance = nullptr;

    inst->plugin->stop_processing(inst->plugin);

    if (inst->gui) inst->gui->destroy(inst->plugin);

    inst->plugin->deactivate(inst->plugin);
    inst->plugin->destroy(inst->plugin);

    inst->entry_source.entry->deinit();
    if (inst->entry_source.library_handle) UnloadLibrary(*inst->entry_source.library_handle);
}

static void HotReloadPlugin(Standalone& standalone, Optional<String> dso_path, ArenaAllocator& arena) {
    ASSERT(standalone.plugin_instance_state.Load(LoadMemoryOrder::Relaxed) == PluginInstanceState::Active);
    auto* inst = standalone.plugin_instance;

    // Save the plugin state before unloading.
    DynamicArray<u8> saved_state {PageAllocator::Instance()};
    bool state_saved = false;
    {
        auto const state_ext =
            (clap_plugin_state const*)inst->plugin->get_extension(inst->plugin, CLAP_EXT_STATE);
        if (state_ext) {
            clap_ostream const stream {
                .ctx = (void*)&saved_state,
                .write = [](clap_ostream const* stream, void const* buffer, uint64_t size) -> s64 {
                    auto& buf = *(DynamicArray<u8>*)stream->ctx;
                    dyn::AppendSpan(buf, Span {(u8 const*)buffer, (usize)size});
                    return (s64)size;
                },
            };
            state_saved = state_ext->save(inst->plugin, &stream);
            if (state_saved)
                LogInfo(ModuleName::Standalone,
                        "Hot-reload: saved plugin state ({} bytes)",
                        saved_state.size);
            else
                LogWarning(ModuleName::Standalone, "Hot-reload: failed to save plugin state");
        }
    }

    DynamicArray<u8> saved_gui_state {PageAllocator::Instance()};
    {
        auto const floe_ext =
            (FloeClapExtension const*)inst->plugin->get_extension(inst->plugin, k_floe_clap_extension_id);
        if (floe_ext && floe_ext->save_gui_state) {
            if (floe_ext->save_gui_state(inst->plugin, dyn::WriterFor(saved_gui_state)))
                LogInfo(ModuleName::Standalone,
                        "Hot-reload: saved gui state ({} bytes)",
                        saved_gui_state.size);
            else
                LogWarning(ModuleName::Standalone, "Hot-reload: failed to save gui state");
        }
    }

    UnloadPluginInstance(standalone);

    auto const reload_outcome = LoadPluginInstance(standalone, dso_path, arena);
    if (reload_outcome.HasError()) {
        LogError(ModuleName::Standalone, "Hot-reload: failed to reload plugin: {}", reload_outcome.Error());
        return;
    }

    // Restore the saved state into the new plugin instance.
    if (state_saved) {
        auto* new_inst = standalone.plugin_instance;
        auto const state_ext =
            (clap_plugin_state const*)new_inst->plugin->get_extension(new_inst->plugin, CLAP_EXT_STATE);
        if (state_ext) {
            auto reader = Reader::FromMemory(saved_state.Items());
            clap_istream const stream {
                .ctx = (void*)&reader,
                .read = [](clap_istream const* stream, void* buffer, uint64_t size) -> s64 {
                    auto& r = *(Reader*)stream->ctx;
                    auto const read = r.Read(Span<u8>((u8*)buffer, size));
                    if (read.HasError()) return -1;
                    return CheckedCast<s64>(read.Value());
                },
            };
            if (state_ext->load(new_inst->plugin, &stream))
                LogInfo(ModuleName::Standalone, "Hot-reload: restored plugin state");
            else
                LogWarning(ModuleName::Standalone, "Hot-reload: failed to restore plugin state");
        }
    }

    if (saved_gui_state.size) {
        auto* new_inst = standalone.plugin_instance;
        auto const floe_ext =
            (FloeClapExtension const*)new_inst->plugin->get_extension(new_inst->plugin,
                                                                      k_floe_clap_extension_id);
        if (floe_ext && floe_ext->load_gui_state) {
            if (floe_ext->load_gui_state(new_inst->plugin,
                                         String {(char const*)saved_gui_state.data, saved_gui_state.size}))
                LogInfo(ModuleName::Standalone, "Hot-reload: restored gui state");
            else
                LogWarning(ModuleName::Standalone, "Hot-reload: failed to restore gui state");
        }
    }

    LogInfo(ModuleName::Standalone, "Hot-reload: plugin reloaded successfully");
}

struct OfflineRenderOptions {
    String out_flac_path;
    String events_path;
    f64 sample_rate;
};

struct RunOptions {
    Optional<OfflineRenderOptions> offline_render;
    RenderTail tail;
    Optional<String> dso_path;
    bool force_midi_channel_zero;
    Optional<String> screenshot_region;
    Optional<String> screenshot_out_path;
    Optional<String> preset_path;
    Optional<UiSize> fixed_window_size;
    Optional<String> app_id;
    Optional<String> ui_dump_path;
    bool headless;
    bool midi_stdin;
};

static void StdinMidiReaderLoop(Standalone& standalone) {
    using RawMessage = DeviceManager::Midi::RawMessage;
    auto const read_byte = [&standalone](u8& out) -> bool {
        while (!standalone.stdin_midi.stop.Load(LoadMemoryOrder::Acquire)) {
            switch (ReadStdinByte(out, 100)) {
                case StdinReadResult::GotByte: return true;
                case StdinReadResult::Timeout: continue;
                case StdinReadResult::Eof: return false;
                case StdinReadResult::Error: return false;
            }
        }
        return false;
    };

    u8 running_status = 0;
    while (true) {
        u8 byte;
        if (!read_byte(byte)) break;

        u8 status;
        u8 data1;
        if (byte & 0x80) {
            // Real-time messages (0xF8-0xFF) don't reset running status and have no data.
            if (byte >= 0xF8) {
                standalone.devices.midi.injected.Push(RawMessage {.status = byte});
                continue;
            }
            // System common messages reset running status; we don't decode them — skip and resync.
            if (byte >= 0xF0) {
                running_status = 0;
                continue;
            }
            status = byte;
            running_status = status;
            if (!read_byte(data1)) break;
        } else {
            // No status byte — must use running status. Drop the byte if there isn't one.
            if (!running_status) continue;
            status = running_status;
            data1 = byte;
        }

        auto const type = status & 0xF0;
        u8 data2 = 0;
        if (type != 0xC0 && type != 0xD0)
            if (!read_byte(data2)) break;
        standalone.devices.midi.injected.Push(RawMessage {.status = status, .data1 = data1, .data2 = data2});
    }

    standalone.stdin_midi.eof.Store(true, StoreMemoryOrder::Release);
}

static FloeClapExtension const* FloeExtension(PluginInstance const& inst) {
    return (FloeClapExtension const*)inst.plugin->get_extension(inst.plugin, k_floe_clap_extension_id);
}

static bool LoadPresetFile(PluginInstance const& inst, String preset_path) {
    auto const floe_ext = FloeExtension(inst);
    if (!floe_ext || !floe_ext->load_preset_file) return false;
    return floe_ext->load_preset_file(inst.plugin, preset_path);
}

static bool UntilSilentTailFinished(Standalone const& standalone,
                                    u64 tail_start_frame,
                                    u64 current_frame,
                                    f64 sample_rate) {
    switch (CheckUntilSilent(tail_start_frame,
                             standalone.output_level.audible_end_frame.Load(LoadMemoryOrder::Relaxed),
                             current_frame,
                             sample_rate)) {
        case UntilSilentStatus::Continue: return false;
        case UntilSilentStatus::WentSilent: return true;
        case UntilSilentStatus::TimedOut:
            LogWarning(ModuleName::Standalone,
                       "Output didn't go silent within {} seconds, stopping",
                       k_until_silent_timeout_seconds);
            return true;
    }
    PanicIfReached();
    return true;
}

// Audio thread. Appends interleaved stereo.
static void RenderOfflineAudio(Standalone& standalone,
                               Span<OfflineRenderEvent const> events,
                               OfflineRenderOptions const& options,
                               RenderTail tail,
                               DynamicArray<f32>& output) {
    constexpr u32 k_block_frames = 512;

    auto const last_event_frame = events.size ? events[events.size - 1].frame : 0;
    auto const fixed_end_frame = ({
        Optional<u64> f {};
        switch (tail.tag) {
            case RenderTailType::Seconds:
                f = last_event_frame + (u64)Round(tail.Get<f64>() * options.sample_rate);
                break;
            case RenderTailType::UntilSilent: break;
        }
        f;
    });
    auto& output_level = standalone.output_level;

    DynamicArray<clap_event_midi> block_events {PageAllocator::Instance()};
    alignas(16) f32 block_output[k_block_frames * 2];

    u64 frame_pos = 0;
    usize event_index = 0;
    while (true) {
        if (fixed_end_frame) {
            if (frame_pos >= *fixed_end_frame) break;
        } else if (UntilSilentTailFinished(standalone, last_event_frame, frame_pos, options.sample_rate)) {
            break;
        }

        auto const block_frames =
            fixed_end_frame ? (u32)Min<u64>(k_block_frames, *fixed_end_frame - frame_pos) : k_block_frames;
        auto const block_end_frame = frame_pos + block_frames;

        dyn::Clear(block_events);
        for (; event_index < events.size && events[event_index].frame < block_end_frame; ++event_index) {
            auto const& event = events[event_index];
            dyn::Append(block_events,
                        {
                            .header =
                                {
                                    .size = sizeof(clap_event_midi),
                                    .time = (u32)(event.frame - frame_pos),
                                    .space_id = CLAP_CORE_EVENT_SPACE_ID,
                                    .type = CLAP_EVENT_MIDI,
                                    .flags = CLAP_EVENT_IS_LIVE,
                                },
                            .port_index = 0,
                            .data = {event.midi[0], event.midi[1], event.midi[2]},
                        });
        }

        auto const rendered =
            RenderAudio(&standalone, block_output, block_frames, block_events.data, (u32)block_events.size);
        ASSERT(rendered);

        dyn::AppendSpan(output, Span<f32 const> {block_output, block_frames * 2});
        frame_pos = block_end_frame;
    }

    if (!fixed_end_frame) {
        auto const audible_end_frame = output_level.audible_end_frame.Load(LoadMemoryOrder::Relaxed);
        dyn::Resize(output, Min(output.size, (usize)(Max(audible_end_frame, last_event_frame) * 2)));
    }
}

struct StdinTailStart {
    TimePoint time;
    u64 frame;
};

// Main thread.
static bool StdinTailFinished(Standalone const& standalone, RenderTail tail, StdinTailStart start) {
    switch (tail.tag) {
        case RenderTailType::Seconds: return (TimePoint::Now() - start.time) >= tail.Get<f64>();
        case RenderTailType::UntilSilent: {
            auto const& device = standalone.devices.audio.stream.device;
            if (!device) return true;
            return UntilSilentTailFinished(
                standalone,
                start.frame,
                standalone.output_level.frames_rendered.Load(LoadMemoryOrder::Relaxed),
                device->sampleRate);
        }
    }
    PanicIfReached();
    return true;
}

// Renders as fast as possible rather than realtime, so event times are sample-accurate and independent of
// how long loading takes. Time 0 is the first sample after the preset and its libraries have loaded.
static ErrorCodeOr<void>
RunOfflineRender(Standalone& standalone, RunOptions const& run_options, ArenaAllocator& arena) {
    auto const& options = *run_options.offline_render;

    auto const events = ({
        auto const text = TRY(ReadEntireFile(options.events_path, arena));
        auto const outcome = ParseOfflineRenderEvents(text, options.sample_rate, arena);
        if (outcome.HasError()) {
            StdPrintF(StdStream::Err,
                      "Error: events file, line {}: {}\n",
                      outcome.Error().line_number,
                      outcome.Error().message);
            return ErrorCode {CommonError::InvalidFileFormat};
        }
        outcome.ReleaseValue();
    });

    TRY(LoadPluginInstance(standalone, run_options.dso_path, arena));
    DEFER { UnloadPluginInstance(standalone); };
    auto& inst = *standalone.plugin_instance;

    auto const service_main_thread_callbacks = [&inst] {
        if (inst.callback_requested.Exchange(false, RmwMemoryOrder::Relaxed))
            inst.plugin->on_main_thread(inst.plugin);
    };

    if (run_options.preset_path && !LoadPresetFile(inst, *run_options.preset_path)) {
        StdPrintF(StdStream::Err, "Error: failed to load preset\n");
        return ErrorCode {StandaloneError::PluginInterfaceError};
    }

    {
        auto const floe_ext = FloeExtension(inst);
        if (!floe_ext || !floe_ext->state_change_is_pending)
            return ErrorCode {StandaloneError::PluginInterfaceError};
        constexpr f64 k_timeout_seconds = 120;
        auto const start = TimePoint::Now();
        while (true) {
            service_main_thread_callbacks();
            if (!floe_ext->state_change_is_pending(inst.plugin)) break;
            if ((TimePoint::Now() - start) > k_timeout_seconds) {
                StdPrintF(StdStream::Err, "Error: timed out waiting for the preset's libraries to load\n");
                return ErrorCode {StandaloneError::PluginInterfaceError};
            }
            SleepThisThread(5);
        }
    }

    auto const render_start = TimePoint::Now();
    DynamicArray<f32> output {PageAllocator::Instance()};
    Atomic<bool> render_done {false};
    Thread render_thread {};
    render_thread.Start(
        [&] {
            // The plugin checks that it's processed on the host's declared audio thread.
            standalone.devices.audio.stream.thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
            RenderOfflineAudio(standalone, events, options, run_options.tail, output);
            render_done.Store(true, StoreMemoryOrder::Release);
        },
        "render"_s);
    while (!render_done.Load(LoadMemoryOrder::Acquire)) {
        service_main_thread_callbacks();
        SleepThisThread(1);
    }
    render_thread.Join();

    if (!WriteFlacFile(options.out_flac_path,
                       {
                           .sample_rate = options.sample_rate,
                           .num_channels = 2,
                           .interleaved_samples = output.Items(),
                       },
                       arena))
        return ErrorCode {StandaloneError::RenderFileError};

    LogInfo(ModuleName::Standalone,
            "Rendered {.2} seconds of audio in {.2} seconds",
            (f64)output.size / 2 / options.sample_rate,
            TimePoint::Now() - render_start);

    return k_success;
}

static ErrorCodeOr<void> Run(RunOptions options, ArenaAllocator& arena) {
    auto const& dso_path = options.dso_path;
    Standalone standalone {};
    standalone.headless = options.headless || options.offline_render;
    standalone.devices.midi.force_channel_zero = options.force_midi_channel_zero;
    standalone.devices.midi.skip_device_input = options.midi_stdin;
    standalone.devices.host_callbacks = {
        .ctx = &standalone,
        .render = RenderAudio,
        .deactivate_for_device_swap = DeactivateForDeviceSwap,
        .activate_after_device_swap = ActivateAfterDeviceSwap,
    };
    {
        auto alloc =
            PageAllocator::Instance().AllocateExactSizeUninitialised<f32>(k_max_audio_buffer_frames * 2);
        standalone.render_scratch[0] = alloc.SubSpan(0, k_max_audio_buffer_frames);
        standalone.render_scratch[1] = alloc.SubSpan(k_max_audio_buffer_frames, k_max_audio_buffer_frames);
    }
    standalone.fixed_window_size = options.fixed_window_size;
    standalone.offscreen_gui = options.screenshot_region.HasValue();
    standalone.ui_dump_path = options.ui_dump_path;

    switch (options.tail.tag) {
        case RenderTailType::Seconds: break;
        case RenderTailType::UntilSilent:
            // Not DbToAmp: it treats -80 dB and below as silence.
            standalone.output_level.threshold_amp =
                Pow(10.0f, options.tail.Get<RenderTailUntilSilent>().threshold_db / 20.0f);
            break;
    }

    if (options.offline_render) {
        standalone.offline_sample_rate = options.offline_render->sample_rate;
        return RunOfflineRender(standalone, options, arena);
    }

    // Modify detection if given a plugin path.
    bool const is_external_plugin = dso_path.HasValue();
    Optional<DirectoryWatcher> dir_watcher {};
    DirectoryToWatch dso_dir_to_watch {};
    if (is_external_plugin && !options.headless) {
        auto outcome = CreateDirectoryWatcher(PageAllocator::Instance());
        if (outcome.HasError()) {
            LogWarning(ModuleName::Standalone,
                       "Failed to create directory watcher for hot-reload: {}",
                       outcome.Error());
        } else {
            dir_watcher.Emplace(outcome.ReleaseValue());
        }

        auto const dso_path_str = ({
            auto p = AbsolutePath(arena, *dso_path);
            p.HasValue() ? String(p.Value()) : *dso_path;
        });
        if constexpr (IS_MACOS) {
            // The plugin_path points to the .clap bundle. Watch it recursively.
            dso_dir_to_watch = {.path = dso_path_str, .recursive = true, .user_data = nullptr};
        } else {
            // Watch the directory containing the DSO file.
            auto const dir = path::Directory(dso_path_str);
            ASSERT(dir.HasValue()); // plugin_path should be an absolute path
            dso_dir_to_watch = {.path = *dir, .recursive = false, .user_data = nullptr};
        }
    }
    DEFER {
        if (dir_watcher) DestoryDirectoryWatcher(*dir_watcher);
    };

    InitDevices(standalone.devices, arena);
    DEFER { DeinitDevices(standalone.devices); };

    standalone.app_id =
        options.app_id ? NullTerminated(*options.app_id, arena) : k_floe_standalone_default_app_id;
    DEFER {
        if (standalone.gui_view && standalone.view_realized) puglUnrealize(standalone.gui_view);
        if (standalone.gui_view) puglFreeView(standalone.gui_view);
        if (standalone.gui_world) puglFreeWorld(standalone.gui_world);
    };

    // Load plugin (first time)
    TRY(LoadPluginInstance(standalone, dso_path, arena));
    DEFER { UnloadPluginInstance(standalone); };

    Thread stdin_midi_thread {};
    if (options.midi_stdin) {
        LogInfo(ModuleName::Standalone, "Reading raw MIDI bytes from stdin (EOF to exit)");
        stdin_midi_thread.Start([&standalone] { StdinMidiReaderLoop(standalone); }, "stdin-midi"_s);
    }
    DEFER {
        if (stdin_midi_thread.Joinable()) {
            standalone.stdin_midi.stop.Store(true, StoreMemoryOrder::Release);
            stdin_midi_thread.Join();
        }
    };

    if (options.preset_path) {
        if (LoadPresetFile(*standalone.plugin_instance, *options.preset_path))
            LogInfo(ModuleName::Standalone, "Loaded preset");
        else
            LogWarning(ModuleName::Standalone, "Failed to load preset");
    }

    if (options.screenshot_region) {
        auto* inst = standalone.plugin_instance;
        auto const floe_ext =
            (FloeClapExtension const*)inst->plugin->get_extension(inst->plugin, k_floe_clap_extension_id);
        if (floe_ext && floe_ext->request_screenshot) {
            if (floe_ext->request_screenshot(inst->plugin,
                                             *options.screenshot_region,
                                             *options.screenshot_out_path))
                LogInfo(ModuleName::Standalone,
                        "Requested screenshot of region '{}'",
                        *options.screenshot_region);
            else
                LogWarning(ModuleName::Standalone,
                           "Failed to request screenshot of region '{}'",
                           *options.screenshot_region);
        }
    }

    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    bool screenshot_request_was_pending = false;
    Optional<StdinTailStart> stdin_tail_start {};

    while (!standalone.quit) {
        auto const has_active_plugin =
            standalone.plugin_instance_state.Load(LoadMemoryOrder::Acquire) == PluginInstanceState::Active;
        if (has_active_plugin) {
            auto* inst = standalone.plugin_instance;
            if (inst->callback_requested.Exchange(false, RmwMemoryOrder::Relaxed))
                inst->plugin->on_main_thread(inst->plugin);

            if (options.screenshot_region) {
                auto const floe_ext =
                    (FloeClapExtension const*)inst->plugin->get_extension(inst->plugin,
                                                                          k_floe_clap_extension_id);
                if (floe_ext && floe_ext->screenshot_request_pending) {
                    bool const pending = floe_ext->screenshot_request_pending(inst->plugin);
                    if (screenshot_request_was_pending && !pending) {
                        LogInfo(ModuleName::Standalone, "Screenshot captured, exiting");
                        standalone.quit = true;
                    }
                    screenshot_request_was_pending = pending;
                }
            }

            if (inst->timers.size) {
                auto const timer_ext =
                    (clap_plugin_timer_support const*)inst->plugin->get_extension(inst->plugin,
                                                                                  CLAP_EXT_TIMER_SUPPORT);
                auto const now = TimePoint::Now();
                // Copy: the plugin may unregister timers from inside on_timer.
                auto const timers = inst->timers;
                for (auto const& timer : timers) {
                    if (now < timer.next_fire) continue;
                    for (auto& t : inst->timers)
                        if (t.id == timer.id) t.next_fire = now + timer.period_seconds;
                    if (timer_ext && timer_ext->on_timer) timer_ext->on_timer(inst->plugin, timer.id);
                }
            }

            if (inst->resize_hints_changed.Exchange(false, RmwMemoryOrder::Relaxed) && standalone.gui_view &&
                !standalone.fixed_window_size) {
                clap_gui_resize_hints resize_hints;
                if (inst->gui->get_resize_hints(inst->plugin, &resize_hints)) {
                    if (resize_hints.can_resize_vertically && resize_hints.can_resize_horizontally) {
                        if (puglSetViewHint(standalone.gui_view,
                                            PUGL_RESIZABLE,
                                            inst->gui->can_resize(inst->plugin)) != PUGL_SUCCESS) {
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
                    inst->requested_resize.Exchange(k_invalid_ui_size, RmwMemoryOrder::AcquireRelease);
                requested_clap_size != k_invalid_ui_size && standalone.gui_view &&
                !standalone.fixed_window_size) {
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
        }

        if (dir_watcher) {
            auto const dirs_to_watch = Array {dso_dir_to_watch};
            auto const outcome = PollDirectoryChanges(*dir_watcher,
                                                      {
                                                          .dirs_to_watch = dirs_to_watch,
                                                          .result_arena = scratch_arena,
                                                          .scratch_arena = scratch_arena,
                                                      });
            if (outcome.HasError()) {
                LogWarning(ModuleName::Standalone, "Failed to poll directory changes: {}", outcome.Error());
            } else {
                for (auto const& dir_changes : outcome.Value()) {
                    if (dir_changes.error) {
                        LogWarning(ModuleName::Standalone, "Directory watcher error: {}", *dir_changes.error);
                        continue;
                    }
                    if (dir_changes.subpath_changesets.size) {
                        for (auto const& changeset : dir_changes.subpath_changesets) {
                            LogInfo(ModuleName::Standalone,
                                    "DSO file change detected: subpath='{}', changes={}",
                                    changeset.subpath,
                                    DirectoryWatcher::ChangeType::ToString(changeset.changes));
                        }

                        HotReloadPlugin(standalone, dso_path, arena);
                    }
                }
            }
        }

        PollDeviceChanges(standalone.devices);

        if (options.midi_stdin && !options.screenshot_region &&
            standalone.stdin_midi.eof.Load(LoadMemoryOrder::Acquire)) {
            if (!stdin_tail_start)
                stdin_tail_start = StdinTailStart {
                    .time = TimePoint::Now(),
                    .frame = standalone.output_level.frames_rendered.Load(LoadMemoryOrder::Relaxed),
                };
            if (StdinTailFinished(standalone, options.tail, *stdin_tail_start)) standalone.quit = true;
        }

        if (!standalone.gui_world) {
            SleepThisThread(16);
        } else {
            auto const st = puglUpdate(standalone.gui_world, 1.0 / 60.0);
            if (st != PUGL_SUCCESS && st != PUGL_FAILURE) return ErrorCode {st};
        }

        scratch_arena.ResetCursorAndConsolidateRegions();
    }

    return k_success;
}

static int Main(ArgsCstr args) {
    GlobalInit({
        .init_error_reporting = true,
        .set_main_thread = true,
        .panic_response = PanicResponse::Abort,
    });
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    // TODO: we should have a OS abstraction for setting the interupt handler (CTRL+C) so we can gracefully
    // exit the program.

    enum class CommandLineArgId : u8 {
        ClapPluginPath,
        ForceMidiChannelZero,
        Screenshot,
        ScreenshotOut,
        Preset,
        FixedWindowSize,
        AppId,
        UiDump,
        Headless,
        MidiStdin,
        Render,
        Events,
        SampleRate,
        Tail,
        Count,
    };

    auto constexpr k_cli_arg_defs = MakeCommandLineArgDefs<CommandLineArgId>({
        {
            .id = (u32)CommandLineArgId::ClapPluginPath,
            .key = "clap-plugin-path",
            .description =
                "Path to an external CLAP plugin to load instead of the built-in one - this file is watched and the plugin is hot-reloaded if it changes.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::ForceMidiChannelZero,
            .key = "force-midi-channel-zero",
            .description = "Rewrite all incoming MIDI messages to channel 0.",
            .value_type = {},
            .required = false,
            .num_values = 0,
        },
        {
            .id = (u32)CommandLineArgId::Screenshot,
            .key = "screenshot",
            .description =
                "Request a screenshot of a named region (e.g. 'perform'). Region names are defined by the GUI subsystems. Append ':full' (e.g. 'perform:full') to prepare GUI state for the region but capture the whole window. Requires --screenshot-out. The program exits after the screenshot is captured.",
            .value_type = "region",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::ScreenshotOut,
            .key = "screenshot-out",
            .description = "Output path for the PNG produced by --screenshot.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::Preset,
            .key = "preset",
            .description = "Path to a Floe preset file (.floe-preset) to load on startup.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::FixedWindowSize,
            .key = "fixed-window-size",
            .description =
                "Force a fixed (non-resizable) window width in pixels; the height follows the GUI's 10:7 aspect ratio. Must be a multiple of 10. Floe doesn't shrink the window to fit the screen, but the window manager may; a warning is printed if the window ends up a different size.",
            .value_type = "width",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::AppId,
            .key = "app-id",
            .description =
                "Override the windowing system class/app ID (Wayland xdg-shell app_id, X11 WM_CLASS, Windows window class). Defaults to 'org.floe-audio.floe'.",
            .value_type = "id",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::UiDump,
            .key = "ui-dump",
            .description =
                "Write a JSON description of the GUI to the given path whenever it changes: window size, whether anything is still loading, and every interactive element's window-pixel rect with its text, name, value and tooltip. For automation tools that drive the GUI.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::Headless,
            .key = "headless",
            .description = "Run without a GUI.",
            .value_type = {},
            .required = false,
            .num_values = 0,
        },
        {
            .id = (u32)CommandLineArgId::MidiStdin,
            .key = "midi-stdin",
            .description =
                "Read raw MIDI bytes (status + data) from stdin and feed them to the plugin instead of opening a PortMidi input device. Exits on stdin EOF.",
            .value_type = {},
            .required = false,
            .num_values = 0,
        },
        {
            .id = (u32)CommandLineArgId::Render,
            .key = "render",
            .description =
                "Render to a 24-bit stereo FLAC file at the given path as fast as possible, then exit. Requires --events. Time 0 is the first sample after the preset has loaded. Runs headless.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::Events,
            .key = "events",
            .description =
                "MIDI events for --render. Text file, one event per line: '<seconds> <status> <data...>' with hex MIDI bytes, e.g. '1.5 90 3C 64'. '#' starts a comment.",
            .value_type = "path",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::SampleRate,
            .key = "sample-rate",
            .description = "Sample rate for --render. Default 48000.",
            .value_type = "hz",
            .required = false,
            .num_values = 1,
        },
        {
            .id = (u32)CommandLineArgId::Tail,
            .key = "tail",
            .description =
                "How long to keep going after the last --render event, or after stdin closes with --midi-stdin. Either seconds, e.g. '2.5', or a threshold, e.g. '-90dB', meaning until the output has stayed below it for 3 seconds (trailing silence is trimmed from --render). Default: -90dB for --render, 0.25 for --midi-stdin.",
            .value_type = "seconds|dB",
            .required = false,
            .num_values = 1,
        },
    });

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const cli_args_outcome = ParseCommandLineArgsStandard(arena, args, k_cli_arg_defs);
    if (cli_args_outcome.HasError()) return cli_args_outcome.Error();
    auto const cli_args = cli_args_outcome.ReleaseValue();

    auto const screenshot_region = cli_args[ToInt(CommandLineArgId::Screenshot)].Value();
    auto const screenshot_out_path = cli_args[ToInt(CommandLineArgId::ScreenshotOut)].Value();
    if (screenshot_region.HasValue() != screenshot_out_path.HasValue()) {
        StdPrintF(StdStream::Err, "Error: --screenshot and --screenshot-out must be provided together\n");
        return 1;
    }

    Optional<UiSize> fixed_window_size {};
    if (auto const width_str = cli_args[ToInt(CommandLineArgId::FixedWindowSize)].Value()) {
        auto const width = ParseInt(*width_str, ParseIntBase::Decimal);
        if (!width || *width < k_min_gui_width || *width > (s64)k_max_gui_width ||
            *width % k_gui_aspect_ratio.width != 0) {
            StdPrintF(
                StdStream::Err,
                "Error: invalid --fixed-window-size '{}': must be a width in pixels that is a multiple of "
                "{}, in [{}, {}]\n",
                *width_str,
                k_gui_aspect_ratio.width,
                k_min_gui_width,
                k_max_gui_width);
            return 1;
        }
        fixed_window_size = SizeWithAspectRatio((u16)*width, k_gui_aspect_ratio);
        ASSERT_EQ(fixed_window_size->width, *width);
    }

    auto const midi_stdin = cli_args[ToInt(CommandLineArgId::MidiStdin)].was_provided;

    auto const offline_render = ({
        Optional<OfflineRenderOptions> result {};
        auto const out_path = cli_args[ToInt(CommandLineArgId::Render)].Value();
        auto const events_path = cli_args[ToInt(CommandLineArgId::Events)].Value();
        auto const sample_rate_str = cli_args[ToInt(CommandLineArgId::SampleRate)].Value();
        if (out_path) {
            if (!events_path) {
                StdPrintF(StdStream::Err, "Error: --render requires --events\n");
                return 1;
            }
            if (midi_stdin || screenshot_region) {
                StdPrintF(StdStream::Err,
                          "Error: --render can't be combined with --midi-stdin or --screenshot\n");
                return 1;
            }
            result = OfflineRenderOptions {
                .out_flac_path = *out_path,
                .events_path = *events_path,
                .sample_rate = 48000,
            };
            if (sample_rate_str) {
                auto const sample_rate = ParseWholeFloat(*sample_rate_str);
                if (!sample_rate || !(*sample_rate >= 8000 && *sample_rate <= 384000)) {
                    StdPrintF(StdStream::Err,
                              "Error: invalid --sample-rate '{}': must be in [8000, 384000]\n",
                              *sample_rate_str);
                    return 1;
                }
                result->sample_rate = *sample_rate;
            }
        } else if (events_path || sample_rate_str) {
            StdPrintF(StdStream::Err, "Error: --events and --sample-rate require --render\n");
            return 1;
        }
        result;
    });

    auto const tail = ({
        RenderTail result =
            offline_render ? RenderTail {RenderTailUntilSilent {.threshold_db = -90}} : RenderTail {0.25};
        if (auto const tail_str = cli_args[ToInt(CommandLineArgId::Tail)].Value()) {
            if (!offline_render && !midi_stdin) {
                StdPrintF(StdStream::Err, "Error: --tail requires --render or --midi-stdin\n");
                return 1;
            }
            auto const parsed = ParseRenderTail(*tail_str);
            if (!parsed) {
                StdPrintF(
                    StdStream::Err,
                    "Error: invalid --tail '{}': expected seconds in [0, 3600] or a threshold like -90dB\n",
                    *tail_str);
                return 1;
            }
            result = *parsed;
        }
        result;
    });

    auto const o = Run(
        {
            .offline_render = offline_render,
            .tail = tail,
            .dso_path = cli_args[ToInt(CommandLineArgId::ClapPluginPath)].Value(),
            .force_midi_channel_zero = cli_args[ToInt(CommandLineArgId::ForceMidiChannelZero)].was_provided,
            .screenshot_region = screenshot_region,
            .screenshot_out_path = screenshot_out_path,
            .preset_path = cli_args[ToInt(CommandLineArgId::Preset)].Value(),
            .fixed_window_size = fixed_window_size,
            .app_id = cli_args[ToInt(CommandLineArgId::AppId)].Value(),
            .ui_dump_path = cli_args[ToInt(CommandLineArgId::UiDump)].Value(),
            .headless = cli_args[ToInt(CommandLineArgId::Headless)].was_provided,
            .midi_stdin = midi_stdin,
        },
        arena);
    if (o.HasError()) {
        StdPrintF(StdStream::Err, "Error: {}\n", o.Error());
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    return Main({argc, argv});
}
