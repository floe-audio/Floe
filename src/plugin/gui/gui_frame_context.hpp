// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "sample_lib_server/sample_library_server.hpp"

// Ephemeral for a single frame
struct GuiFrameContext {
    sample_lib_server::LibrariesSpan libraries;
    sample_lib_server::LibrariesTable lib_table;
};
