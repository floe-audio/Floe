// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"

//
#include <X11/Xlib.h>

#include "bgfx_init_window.hpp"

namespace graphics {

void* GetBgfxInitWindowHandle(void* native_display) {
    static ::Window window_handle = 0;
    static Display* g_display = nullptr;

    if (!window_handle && native_display) {
        auto* display = (Display*)native_display;
        auto const screen = DefaultScreen(display);
        auto const root = RootWindow(display, screen);

        window_handle = XCreateSimpleWindow(display, root, -100, -100, 1, 1, 0, 0, 0);
        XSelectInput(display, window_handle, StructureNotifyMask);
        XFlush(display);
    }

    ASSERT_EQ(native_display, g_display);

    return (void*)window_handle;
}

} // namespace graphics
