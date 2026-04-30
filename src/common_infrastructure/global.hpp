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

struct GlobalInitOptions {
    bool init_error_reporting = false;
    bool set_main_thread = false;
};

void GlobalInit(GlobalInitOptions options);

struct GlobalShutdownOptions {
    bool shutdown_error_reporting = false;
};

void GlobalDeinit(GlobalShutdownOptions options);
