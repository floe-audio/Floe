// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#pragma clang diagnostic pop

#include "renderer_bgfx_init_window.hpp"

void* GetBgfxInitWindowHandle(void*) {
    static CAMetalLayer* metal_layer = nullptr;

    if (!metal_layer) {
        metal_layer = [CAMetalLayer layer];
        metal_layer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    }

    return (__bridge void*)metal_layer;
}
