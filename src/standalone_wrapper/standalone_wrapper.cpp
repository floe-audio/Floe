// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/audio-buffer.h>
#include <clap/entry.h>
#include <clap/events.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/thread-check.h>
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
#include "utils/cli_arg_parse.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/global.hpp"

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

    PuglWorld* gui_world {};
    PuglView* gui_view {};
    bool view_realized {};

    Optional<UiSize> fixed_window_size {};
    bool quit = false;

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

    clap_process_t process {};
    process.frames_count = num_frames;
    process.steady_time = -1;
    process.transport = nullptr;

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
                if (inst->gui->can_resize(inst->plugin)) {
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
};
ErrorCodeCategory const standalone_error_category {
    .category_id = "STND",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((StandaloneError)code.code) {
            case StandaloneError::DeviceError: str = "device error"; break;
            case StandaloneError::PluginInterfaceError: str = "plugin interface error"; break;
            case StandaloneError::WindowError: str = "window error"; break;
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

    inst->floe_host_ext.pugl_world = standalone.gui_world;
    inst->floe_host_ext.error_notifications = &standalone.devices.error_notifications;
    SetupHostDeviceCallbacks(inst->floe_host_ext, standalone.devices);

    inst->plugin = factory->create_plugin(factory, &inst->host, g_plugin_info.id);
    if (!inst->plugin) return ErrorCode {StandaloneError::PluginInterfaceError};
    inst->plugin_created = true;

    inst->plugin->init(inst->plugin);

    // If the audio device failed to open we still activate the plugin with conservative defaults so
    // the GUI (and its errors panel) come up; the audio thread is silent until a device is selected.
    auto const sample_rate =
        standalone.devices.audio.stream.device ? standalone.devices.audio.stream.device->sampleRate : 48000.0;
    auto const period_size = standalone.devices.audio.stream.device
                                 ? standalone.devices.audio.stream.device->playback.internalPeriodSizeInFrames
                                 : 1024u;
    // InitAudioDevice rejects devices whose period exceeds k_max_audio_buffer_frames, so this is
    // always the max block size.
    inst->plugin->activate(inst->plugin, sample_rate, period_size, (u32)k_max_audio_buffer_frames);

    inst->plugin->start_processing(inst->plugin);

    // Set up GUI
    inst->gui = (clap_plugin_gui const*)inst->plugin->get_extension(inst->plugin, CLAP_EXT_GUI);
    TRY_CLAP(inst->gui);

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

    inst->gui->destroy(inst->plugin);

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

struct RunOptions {
    Optional<String> dso_path;
    bool force_midi_channel_zero;
    Optional<String> screenshot_region;
    Optional<String> screenshot_out_path;
    Optional<String> preset_path;
    Optional<UiSize> fixed_window_size;
    Optional<String> app_id;
};

static ErrorCodeOr<void> Run(RunOptions options, ArenaAllocator& arena) {
    auto const& dso_path = options.dso_path;
    Standalone standalone {};
    standalone.devices.midi.force_channel_zero = options.force_midi_channel_zero;
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

    // Modify detection if given a plugin path.
    bool const is_external_plugin = dso_path.HasValue();
    Optional<DirectoryWatcher> dir_watcher {};
    DirectoryToWatch dso_dir_to_watch {};
    if (is_external_plugin) {
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

    standalone.gui_world = puglNewWorld(PUGL_PROGRAM, 0);
    if (!standalone.gui_world) {
        LogError(ModuleName::Standalone, "Could not setup window state");
        return ErrorCode {StandaloneError::WindowError};
    }
    DEFER { puglFreeWorld(standalone.gui_world); };
    {
        char const* app_id = k_floe_standalone_default_app_id;
        if (options.app_id) app_id = NullTerminated(*options.app_id, arena);
        TRY_PUGL(puglSetWorldString(standalone.gui_world, PUGL_CLASS_NAME, app_id));
    }

    standalone.gui_view = puglNewView(standalone.gui_world);
    DEFER {
        if (standalone.view_realized) puglUnrealize(standalone.gui_view);
        puglFreeView(standalone.gui_view);
    };
    TRY_PUGL(puglSetViewHint(standalone.gui_view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON));
    TRY_PUGL(puglSetBackend(standalone.gui_view, puglStubBackend()));
    puglSetHandle(standalone.gui_view, &standalone);
    TRY_PUGL(puglSetEventFunc(standalone.gui_view, OnEvent));
    TRY_PUGL(puglSetViewString(standalone.gui_view, PUGL_WINDOW_TITLE, "Floe"));

    // Load plugin (first time)
    TRY(LoadPluginInstance(standalone, dso_path, arena));
    DEFER { UnloadPluginInstance(standalone); };

    if (options.preset_path) {
        auto* inst = standalone.plugin_instance;
        auto const floe_ext =
            (FloeClapExtension const*)inst->plugin->get_extension(inst->plugin, k_floe_clap_extension_id);
        if (floe_ext && floe_ext->load_preset_file) {
            if (floe_ext->load_preset_file(inst->plugin, *options.preset_path))
                LogInfo(ModuleName::Standalone, "Loaded preset");
            else
                LogWarning(ModuleName::Standalone, "Failed to load preset");
        }
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

            if (inst->resize_hints_changed.Exchange(false, RmwMemoryOrder::Relaxed) &&
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
                requested_clap_size != k_invalid_ui_size && !standalone.fixed_window_size) {
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

        auto const st = puglUpdate(standalone.gui_world, 1.0 / 60.0);
        if (st != PUGL_SUCCESS && st != PUGL_FAILURE) return ErrorCode {st};

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

    enum class CommandLineArgId : u8 {
        ClapPluginPath,
        ForceMidiChannelZero,
        Screenshot,
        ScreenshotOut,
        Preset,
        FixedWindowSize,
        AppId,
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
                "Request a screenshot of a named region (e.g. 'perform'). Region names are defined by the GUI subsystems. Requires --screenshot-out. The program exits after the screenshot is captured.",
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
                "Force a fixed (non-resizable) window size. The value is the abstract size number from the 'Window size' field in Floe's preferences.",
            .value_type = "size",
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
    });

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const cli_args_outcome = ParseCommandLineArgsStandard(arena, args, k_cli_arg_defs);
    if (cli_args_outcome.HasError()) return cli_args_outcome.Error();
    auto const cli_args = cli_args_outcome.ReleaseValue();

    auto const screenshot_region = cli_args[ToInt(CommandLineArgId::Screenshot)].Value();
    auto const screenshot_out_path = cli_args[ToInt(CommandLineArgId::ScreenshotOut)].Value();
    if (screenshot_region.HasValue() != screenshot_out_path.HasValue()) {
        LogError(ModuleName::Standalone, "--screenshot and --screenshot-out must be provided together");
        return 1;
    }

    Optional<UiSize> fixed_window_size {};
    if (auto const width_str = cli_args[ToInt(CommandLineArgId::FixedWindowSize)].Value()) {
        auto const parsed = ParseInt(*width_str, ParseIntBase::Decimal);
        s64 pixels;
        if (!parsed || __builtin_mul_overflow(*parsed, k_window_width_step, &pixels) ||
            pixels < k_min_gui_width || pixels > (s64)k_max_gui_width) {
            LogError(ModuleName::Standalone,
                     "Invalid --fixed-window-size '{}': value * {} pixels must be in [{}, {}]",
                     *width_str,
                     k_window_width_step,
                     k_min_gui_width,
                     k_max_gui_width);
            return 1;
        }
        fixed_window_size = SizeWithAspectRatio((u16)pixels, k_gui_aspect_ratio);
    }

    auto const o = Run(
        {
            .dso_path = cli_args[ToInt(CommandLineArgId::ClapPluginPath)].Value(),
            .force_midi_channel_zero = cli_args[ToInt(CommandLineArgId::ForceMidiChannelZero)].was_provided,
            .screenshot_region = screenshot_region,
            .screenshot_out_path = screenshot_out_path,
            .preset_path = cli_args[ToInt(CommandLineArgId::Preset)].Value(),
            .fixed_window_size = fixed_window_size,
            .app_id = cli_args[ToInt(CommandLineArgId::AppId)].Value(),
        },
        arena);
    if (o.HasError()) {
        LogError(ModuleName::Standalone, "Standalone error: {}", o.Error());
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    auto _ = EnterLogicalMainThread();
    return Main({argc, argv});
}
