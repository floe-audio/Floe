// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/graphics.hpp"
#include "gui_framework/image.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct WaveformImage {
    using FuturePixels = Future<ImageBytes>;
    Optional<graphics::ImageID> image_id {};
    bool used {};
    FuturePixels* loading_pixels {};
};

struct WaveformImagesTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaList<WaveformImage::FuturePixels> loading_pixels;
    HashTable<u64, WaveformImage> table;
};

Optional<graphics::ImageID> GetWaveformImage(WaveformImagesTable& table,
                                             Instrument const& inst,
                                             graphics::Renderer& renderer,
                                             ThreadPool& thread_pool,
                                             f32x2 size);

void StartFrame(WaveformImagesTable& table, graphics::Renderer& renderer);
void EndFrame(WaveformImagesTable& table, graphics::Renderer& renderer);
void Shutdown(WaveformImagesTable& table);
