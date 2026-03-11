// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/core/gui_fwd.hpp"

void MidPanelPerformContent(GuiBuilder& builder,
                            GuiState& g,
                            GuiFrameContext const& frame_context,
                            Box parent,
                            Optional<Box> tab_extra_buttons_box);
