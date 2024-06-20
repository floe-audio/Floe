// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>

#include "os/misc.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"
#include "tracy/TracyC.h"

static clap_plugin_factory_t const factory = {
    .get_plugin_count = [](clap_plugin_factory_t const*) -> uint32_t { return 1; },
    .get_plugin_descriptor = [](clap_plugin_factory_t const*, uint32_t) { return &k_plugin_info; },
    .create_plugin = [](clap_plugin_factory const*,
                        clap_host_t const* host,
                        char const* plugin_id) -> clap_plugin_t const* {
        if (NullTermStringsEqual(plugin_id, k_plugin_info.id)) {
            auto& p = CreatePlugin(host);
            return &p;
        }
        return nullptr;
    },
};

#if __linux__
// NOLINTBEGIN
extern "C" void* __dso_handle;
extern "C" void __cxa_finalize(void*);
__attribute__((destructor)) void onfini() { __cxa_finalize(__dso_handle); }
// NOLINTEND
#endif

bool g_clap_entry_init = false;

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = [](char const*) -> bool {
        g_log_file.DebugLn("init");
        g_panic_handler = [](char const* message, SourceLocation loc) {
            g_log_file.ErrorLn("{}: {}", loc, message);
            DefaultPanicHandler(message, loc);
        };

#ifdef TRACY_ENABLE
        ___tracy_startup_profiler();
#endif
        TracyMessageEx({}, "clap_entry init");
        StartupCrashHandler();
        g_clap_entry_init = true;
        return true;
    },
    .deinit =
        []() {
            if (g_clap_entry_init) {
                g_log_file.DebugLn("deinit");
                ShutdownCrashHandler();
#ifdef TRACY_ENABLE
                ___tracy_shutdown_profiler();
#endif
                g_clap_entry_init = false;
            }
        },
    .get_factory = [](char const* factory_id) -> void const* {
        g_log_file.DebugLn("get_factory");
        if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
        return nullptr;
    },
};
