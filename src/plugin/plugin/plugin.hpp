// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "os/threading.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/final_binary_type.hpp"
#include "common_infrastructure/preferences.hpp"

#include "clap/ext/gui.h"
#include "clap/ext/thread-check.h"
#include "clap/stream.h"
#include "config.h"
#include "pugl/pugl.h"

struct PluginActivateArgs {
    f64 sample_rate;
    u32 min_block_size;
    u32 max_block_size;
};

template <typename UserObject>
struct PluginCallbacks {
    // [main-thread & !active_state]
    bool (*activate)(UserObject&, PluginActivateArgs args) = [](UserObject&, PluginActivateArgs) {
        return true;
    };

    // [main-thread & active_state]
    void (*deactivate)(UserObject&) = [](UserObject&) {};

    // Call start processing before processing.
    // [audio-thread & active_state & !processing_state]
    bool (*start_processing)(UserObject&) = [](UserObject&) { return true; };

    // Call stop processing before sending the plugin to sleep.
    // [audio-thread & active_state & processing_state]
    void (*stop_processing)(UserObject&) = [](UserObject&) {};

    // - Clears all buffers, performs a full reset of the processing state (filters, oscillators,
    //   envelopes, lfo, ...) and kills all voices.
    // - The parameter's value remain unchanged.
    // - clap_process.steady_time may jump backward.
    //
    // [audio-thread & active_state]
    void (*reset)(UserObject&) = [](UserObject&) {};

    // process audio, events, ...
    // All the pointers coming from clap_process_t and its nested attributes,
    // are valid until process() returns.
    // [audio-thread & active_state & processing_state]
    clap_process_status (*process)(UserObject&, clap_process const& process) =
        [](UserObject&, clap_process const&) -> clap_process_status { return CLAP_PROCESS_SLEEP; };

    // Flushes a set of parameter changes.
    // This method must not be called concurrently to clap_plugin->process().
    //
    // Note: if the plugin is processing, then the process() call will already achieve the
    // parameter update (bi-directional), so a call to flush isn't required, also be aware
    // that the plugin may use the sample offset in process(), while this information would be
    // lost within flush().
    //
    // [active ? audio-thread : main-thread]
    void (*flush_parameter_events)(UserObject&, clap_input_events const& in, clap_output_events const& out) =
        [](UserObject&, clap_input_events const&, clap_output_events const&) {};

    // Called by the host on the main thread in response to a previous call to:
    //   host->request_callback(host);
    // [main-thread]
    void (*on_main_thread)(UserObject&) = [](UserObject&) {};

    // [main-thread]
    void (*on_timer)(UserObject&, clap_id timer_id) = [](UserObject&, clap_id) {};

    // [polling-thread]
    void (*on_poll_thread)(UserObject&) = [](UserObject&) {};

    // [main-thread]
    void (*on_preference_changed)(UserObject&, prefs::Key, prefs::Value const*) =
        [](UserObject&, prefs::Key, prefs::Value const*) {};

    // [audio-thread]
    void (*on_thread_pool_exec)(UserObject&, u32 task_index) {};

    // [main-thread]
    bool (*save_state)(UserObject&, clap_ostream const& stream) = [](UserObject&, clap_ostream const&) {
        return true;
    };

    // [main-thread]
    bool (*load_state)(UserObject&, clap_istream const& stream) = [](UserObject&, clap_istream const&) {
        return true;
    };
};

struct PluginInstanceMessages {
    virtual void UpdateGui() = 0;
    virtual ~PluginInstanceMessages() = default;
};

constexpr char const* k_supported_gui_api =
    IS_WINDOWS ? CLAP_WINDOW_API_WIN32 : (IS_MACOS ? CLAP_WINDOW_API_COCOA : CLAP_WINDOW_API_X11);

// CLAP uses logical pixels on macOS or physical pixel on Windows/Linux. We always use physical pixels, and so
// need to convert. See gui.h definitions of CLAP_WINDOW_API_WIN32, CLAP_WINDOW_API_COCOA,
// CLAP_WINDOW_API_X11.
PUBLIC UiSize PhysicalPixelsToClapPixels(PuglView* view, UiSize size) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(view);
    if constexpr (IS_MACOS) {
        auto scale_factor = puglGetScaleFactor(view);
        if (scale_factor > 0) {
            size.width = CheckedCast<u16>(size.width / scale_factor);
            size.height = CheckedCast<u16>(size.height / scale_factor);
        }
    }
    return size;
}
PUBLIC Optional<UiSize> ClapPixelsToPhysicalPixels(PuglView* view, u32 width, u32 height) {
    ASSERT(g_is_logical_main_thread);
    ASSERT(view);
    if constexpr (IS_MACOS) {
        auto scale_factor = puglGetScaleFactor(view);
        if (scale_factor > 0) {
            width = u32(width * scale_factor);
            height = u32(height * scale_factor);
        }
    }
    if (width > LargestRepresentableValue<u16>() || height > LargestRepresentableValue<u16>())
        return k_nullopt;
    return UiSize {(u16)width, (u16)height};
}

// We use the clap extension interface for our plugin and "host" (wrapper) to communicate to each other.
static constexpr char k_floe_clap_extension_id[] = "floe.floe";
static constexpr char k_floe_standalone_host_name[] = "Floe Standalone";

struct FloeClapExtensionHost {
    bool standalone_audio_device_error;
    bool standalone_midi_device_error;
    void* pugl_world;
};

struct FloeClapTestingExtension {
    bool (*state_change_is_pending)(clap_plugin const* plugin) = nullptr;
};

enum class IsThreadResult { No, Yes, Unknown };

inline IsThreadResult IsMainThread(clap_host const& host) {
    // Whilst the CLAP-wrapper does support thread-check, it's untrustworthy. We only trust the value of
    // direct CLAP thread-check.
    if (g_final_binary_type == FinalBinaryType::Clap) {
        if (auto const thread_check =
                (clap_host_thread_check const*)host.get_extension(&host, CLAP_EXT_THREAD_CHECK);
            thread_check)
            return thread_check->is_main_thread(&host) ? IsThreadResult::Yes : IsThreadResult::No;
    }
    return IsThreadResult::Unknown;
}

inline IsThreadResult IsAudioThread(clap_host const& host) {
    if (auto const thread_check =
            (clap_host_thread_check const*)host.get_extension(&host, CLAP_EXT_THREAD_CHECK);
        thread_check)
        return thread_check->is_audio_thread(&host) ? IsThreadResult::Yes : IsThreadResult::No;
    else {
        // We can't know for sure without the host's extension since the CLAP spec allows there to be multiple
        // audio threads.
        return IsThreadResult::Unknown;
    }
}

extern clap_plugin_descriptor const g_plugin_info;

// can return null
clap_plugin const* CreateFloeInstance(clap_host const* clap_host);

void OnPollThread(FloeInstanceIndex index);
void OnPreferenceChanged(FloeInstanceIndex index, prefs::Key const& key, prefs::Value const* value);

char const* PuglEventString(PuglEventType type);
