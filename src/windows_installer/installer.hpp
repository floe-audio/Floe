// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "resources.h"

enum class ComponentTypes : u32 {
    Clap,
#ifdef VST3_PLUGIN_BINARY_NAME
    VST3,
#endif
    Count,
};

struct ComponentInfo {
    String name;
    KnownDirectoryType install_dir;
    String filename;
    int resource_id;
};

constexpr auto k_plugin_infos = Array {
    ComponentInfo {
        .name = "Floe CLAP Plugin v" FLOE_VERSION_STRING,
        .install_dir = KnownDirectoryType::GlobalClapPlugins,
        .filename = path::Filename(CLAP_PLUGIN_BINARY_NAME),
        .resource_id = CLAP_PLUGIN_RESOURCE_ID,
    },
#ifdef VST3_PLUGIN_BINARY_NAME
    ComponentInfo {
        .name = "Floe VST3 Plugin v" FLOE_VERSION_STRING,
        .install_dir = KnownDirectoryType::GlobalVst3Plugins,
        .filename = path::Filename(VST3_PLUGIN_BINARY_NAME),
        .resource_id = VST3_PLUGIN_RESOURCE_ID,
    },
#endif
};

static_assert(k_plugin_infos.size == ToInt(ComponentTypes::Count));

ErrorCodeOr<Span<u8 const>> GetResource(int resource_id);
