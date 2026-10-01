// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "foundation/foundation.hpp"

//
#include <X11/Xlib.h>

#include "renderer_bgfx_init_window.hpp"

BgfxPlatformHandles CreateBgfxPlatformHandles() {
    auto* display = XOpenDisplay(nullptr);
    if (!display) return {.init_window = nullptr, .display = nullptr};

    auto const root = RootWindow(display, DefaultScreen(display));
    auto const window = XCreateSimpleWindow(display, root, -100, -100, 1, 1, 0, 0, 0);
    XSelectInput(display, window, StructureNotifyMask);
    XFlush(display);

    return {.init_window = (void*)window, .display = display};
}

void DestroyBgfxPlatformHandles(BgfxPlatformHandles& handles) {
    if (handles.display) {
        auto* display = (Display*)handles.display;
        if (handles.init_window) XDestroyWindow(display, (::Window)handles.init_window);
        XCloseDisplay(display);
    }
    handles = {.init_window = nullptr, .display = nullptr};
}

void SyncWindowDisplayForBgfx(void* window_display) {
    ASSERT(window_display);
    XSync((Display*)window_display, False);
}
