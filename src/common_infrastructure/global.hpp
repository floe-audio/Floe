// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct GlobalInitOptions {
    bool init_error_reporting = false;
    bool set_main_thread = false;
};

void GlobalInit(GlobalInitOptions options);

struct GlobalShutdownOptions {
    bool shutdown_error_reporting = false;
};

void GlobalDeinit(GlobalShutdownOptions options);
