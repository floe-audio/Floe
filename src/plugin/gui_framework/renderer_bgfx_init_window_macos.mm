// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect  MacRect
#define Delay MacDelay
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#pragma clang diagnostic pop
#undef Rect
#undef Delay

#include "foundation/foundation.hpp"

#include "renderer_bgfx_init_window.hpp"

BgfxPlatformHandles CreateBgfxPlatformHandles() {
    CAMetalLayer* metal_layer = [CAMetalLayer layer];
    auto colour_space = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    metal_layer.colorspace = colour_space;
    CGColorSpaceRelease(colour_space);
    return {.init_window = (__bridge_retained void*)metal_layer, .display = nullptr};
}

void DestroyBgfxPlatformHandles(BgfxPlatformHandles& handles) {
    if (handles.init_window) CFRelease(handles.init_window);
    handles = {.init_window = nullptr, .display = nullptr};
}

void SyncWindowDisplayForBgfx(void*) {}

// Given an NSView, bgfx's render thread installs a CAMetalLayer on it by queueing a block onto the main
// run loop and blocking until it runs. If our main thread is meanwhile waiting in bgfx::frame() for the
// render thread, that deadlocks. So we install the layer here, on the main thread, and hand bgfx the layer.
void* BgfxSwapChainWindowHandle(void* native_window) {
    ASSERT([NSThread isMainThread]);
    auto view = (__bridge NSView*)native_window;
    if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
        [view setWantsLayer:YES];
        [view setLayer:[CAMetalLayer layer]];
    }
    return (__bridge void*)view.layer;
}
