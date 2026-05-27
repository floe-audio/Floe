// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

enum class PluginHost : u8 {
    Unknown,
    ClapValidator,
};

extern Atomic<PluginHost> g_plugin_host;

// Quarantine: panic throws PanicException; PanicOccurred()-guarded code paths early-out so the panicked
// subsystem becomes inert while the surrounding process (e.g. a DAW hosting the plugin) keeps running.
// Abort: panic prints the hook output then calls __builtin_abort(), terminating the process. Preferred for
// CLI tools, tests, and dev runs of the standalone where we want a hard failure with a stacktrace.
enum class PanicResponse : u8 { Quarantine, Abort };

struct GlobalInitOptions {
    bool init_error_reporting = false;
    bool set_main_thread = false;
    PanicResponse panic_response = PanicResponse::Quarantine;
};

void GlobalInit(GlobalInitOptions options);

// Switch panic response after init. Useful for the plugin, which doesn't learn it's running under a
// testing host (clap-validator, pluginval, etc.) until the host name is available at plugin-create time.
void SetPanicResponse(PanicResponse response);
PanicResponse GetPanicResponse();

struct GlobalShutdownOptions {
    bool shutdown_error_reporting = false;
};

void GlobalDeinit(GlobalShutdownOptions options);
