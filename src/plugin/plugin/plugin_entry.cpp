// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <clap/entry.h>
#include <clap/plugin.h>
extern "C" {
#include <clapwrapper/vst3.h>
}

#include "utils/logger/logger.hpp"

#include "common_infrastructure/final_binary_type.hpp"
#include "common_infrastructure/global.hpp"

#include "clap/factory/plugin-factory.h"
#include "plugin.hpp"

#ifdef TRACY_ENABLE

#include "utils/debug/tracy_wrapped.hpp"

void* operator new(std ::size_t count) {
    auto ptr = malloc(count);
    TracySecureAlloc(ptr, count, 8);
    return ptr;
}
void operator delete(void* ptr) noexcept {
    TracySecureFree(ptr, 8);
    free(ptr);
}

#endif

static constexpr char const* k_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                             CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                             CLAP_PLUGIN_FEATURE_STEREO,
                                             nullptr};

clap_plugin_descriptor const g_plugin_info {
    .clap_version = CLAP_VERSION,
    .id = FLOE_CLAP_ID,
    .name = "Floe",
    .vendor = FLOE_VENDOR,
    .url = FLOE_HOMEPAGE_URL,
    .manual_url = FLOE_MANUAL_URL,
    .support_url = FLOE_MANUAL_URL,
    .version = FLOE_VERSION_STRING,
    .description = FLOE_DESCRIPTION,
    .features = (char const**)k_features,
};

static u32 ClapFactoryGetPluginCount(clap_plugin_factory const* factory) {
    if (!factory) return 0;
    if (PanicOccurred()) return 0;
    return 1;
}

static clap_plugin_descriptor const* ClapFactoryGetPluginDescriptor(clap_plugin_factory const* factory,
                                                                    uint32_t index) {
    if (!factory) return nullptr;
    if (PanicOccurred()) return nullptr;
    if (index != 0) return nullptr;
    return &g_plugin_info;
}

static clap_plugin const*
ClapFactoryCreatePlugin(clap_plugin_factory const* factory, clap_host_t const* host, char const* plugin_id) {
    if (PanicOccurred()) return nullptr;
    if (!factory || !host || !plugin_id) return nullptr;

    try {
        if (NullTermStringsEqual(plugin_id, g_plugin_info.id)) return CreateFloeInstance(host);
    } catch (PanicException) {
    }
    return nullptr;
}

static clap_plugin_factory const factory = {
    .get_plugin_count = ClapFactoryGetPluginCount,
    .get_plugin_descriptor = ClapFactoryGetPluginDescriptor,
    .create_plugin = ClapFactoryCreatePlugin,
};

static clap_plugin_factory_as_vst3 const floe_plugin_factory_as_vst3 {
    .vendor = FLOE_VENDOR,
    .vendor_url = FLOE_HOMEPAGE_URL,
    .email_contact = "sam@frozenplain.com",
    .get_vst3_info = nullptr,
};

#if __linux__
// https://github.com/ziglang/zig/issues/17908
// NOLINTBEGIN
extern "C" void* __dso_handle;
extern "C" void __cxa_finalize(void*);
__attribute__((destructor)) void ZigBugWorkaround() { __cxa_finalize(__dso_handle); }
// NOLINTEND
#endif

static bool g_init = false;

// We check the host conforms to CLAP spec: "it is forbidden to call ... simultaneously from multiple threads"
static Atomic<u32> g_inside_call {0};

// init and deinit are never called at the same time as any other clap function, including itself.
// Might be called more than once. See the clap docs for full details.
static bool ClapEntryInit(char const*) {
    if (PanicOccurred()) return false;

    try {
        auto const inside_init = g_inside_call.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
        DEFER { g_inside_call.FetchSub(1, RmwMemoryOrder::AcquireRelease); };
        if (inside_init) return false; // The host is misbehaving

        if (Exchange(g_init, true)) return true; // already initialised

        GlobalInit({
            .init_error_reporting = false,
            .set_main_thread = false,
        });

        LogInfo(ModuleName::Clap,
                "entry.init: ver: " FLOE_VERSION_STRING ", os: " OS_DISPLAY_NAME
                ", arch: " ARCH_DISPLAY_NAME);

        return true;
    } catch (PanicException) {
        return false;
    }
}

static void ClapEntryDeinit() {
    if (PanicOccurred()) return;

    try {
        auto const inside_deinit = g_inside_call.FetchAdd(1, RmwMemoryOrder::AcquireRelease);
        DEFER { g_inside_call.FetchSub(1, RmwMemoryOrder::AcquireRelease); };
        if (inside_deinit) return; // The host is misbehaving

        if (!Exchange(g_init, false)) return; // already deinitialised

        LogInfo(ModuleName::Clap, "entry.deinit");

        GlobalDeinit({
            .shutdown_error_reporting = false,
        });
    } catch (PanicException) {
    }
}

static void const* ClapEntryGetFactory(char const* factory_id) {
    if (!factory_id) return nullptr;
    if (PanicOccurred()) return nullptr;
    LogInfo(ModuleName::Clap, "entry.get_factory");
    if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &factory;
    if (NullTermStringsEqual(factory_id, CLAP_PLUGIN_FACTORY_INFO_VST3)) return &floe_plugin_factory_as_vst3;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = ClapEntryInit,
    .deinit = ClapEntryDeinit,
    .get_factory = ClapEntryGetFactory,
};
