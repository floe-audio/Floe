// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#pragma clang diagnostic pop

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
