// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_frame.hpp"

GuiFrameInput* g_frame_input {};
GuiFrameOutput* g_frame_output {};

GuiFrameIo GuiIo() { return {*g_frame_input, *g_frame_output}; }

void SetGuiIo(GuiFrameInput* in, GuiFrameOutput* out) {
    g_frame_input = in;
    g_frame_output = out;
}

Atomic<bool> g_request_gui_update {};
