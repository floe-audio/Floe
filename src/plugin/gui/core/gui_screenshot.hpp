// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct GuiState;

bool IsScreenshotRequest(String id_name);
bool IsAnyScreenshotInProgress();

void PrepareSubsystemsForScreenshot(GuiState& g);
void MaybeFireScreenshot(GuiState& g);
