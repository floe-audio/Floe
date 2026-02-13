// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui_framework/gui_builder.hpp"
#include "gui_fwd.hpp"

constexpr imgui::Id k_legacy_params_panel_id = HashFnv1a("legacy-params");

void DoLegacyParamsPanel(GuiBuilder& builder, GuiState& g);
