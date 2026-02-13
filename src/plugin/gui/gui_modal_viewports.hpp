// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

struct GuiState;

constexpr u64 k_legacy_params_panel_id = HashFnv1a("legacy-params");

void DoModalViewports(GuiState& g);
