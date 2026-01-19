// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bgfx_init_window.hpp"

namespace graphics {

void* GetBgfxInitWindowHandle(void*) {
    static CAMetalLayer* metal_layer = nullptr;

    if (!metal_layer) {
        metal_layer = [CAMetalLayer layer];
        metal_layer.colorspace = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
    }

    return (__bridge void*)metal_layer;
}

} // namespace graphics
