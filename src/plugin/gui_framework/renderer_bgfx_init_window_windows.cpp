// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "renderer_bgfx_init_window.hpp"

BgfxPlatformHandles CreateBgfxPlatformHandles() { return {.init_window = nullptr, .display = nullptr}; }

void DestroyBgfxPlatformHandles(BgfxPlatformHandles& handles) {
    handles = {.init_window = nullptr, .display = nullptr};
}

void SyncWindowDisplayForBgfx(void*) {}

void* BgfxSwapChainWindowHandle(void* native_window) { return native_window; }
