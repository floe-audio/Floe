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

static Array<Atomic<bool>, k_max_num_floe_instances> g_request_gui_update {};

void RequestGuiUpdate(FloeInstanceIndex index) {
    g_request_gui_update[index].Store(true, StoreMemoryOrder::Release);
}

bool ConsumeGuiUpdateRequest(FloeInstanceIndex index) {
    return g_request_gui_update[index].Exchange(false, RmwMemoryOrder::Acquire);
}
