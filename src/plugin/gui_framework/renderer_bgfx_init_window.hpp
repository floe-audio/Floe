// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// bgfx stores the init display and uses it for every swap chain it creates, for as long as bgfx is
// initialised. It must therefore be owned by the renderer, not borrowed from a window that can close first.
struct BgfxPlatformHandles {
    void* init_window;
    void* display;
};

BgfxPlatformHandles CreateBgfxPlatformHandles();
void DestroyBgfxPlatformHandles(BgfxPlatformHandles& handles);

// The window was created on the windowing library's own connection; make sure the server knows about it
// before bgfx's connection creates a surface for it.
void SyncWindowDisplayForBgfx(void* window_display);
