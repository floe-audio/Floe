// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/misc.hpp"

#include "gui_framework/image.hpp"
#include "gui_framework/renderer.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct WaveformImage {
    using FuturePixels = Future<ImageBytes>;
    Optional<ImageID> image_id {};
    FuturePixels* loading_pixels {};
    u64 source_hash {};
    TimePoint last_requested {};
};

struct WaveformImagesTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaList<WaveformImage::FuturePixels> loading_pixels;
    HashTable<u64, WaveformImage> table;
};

Optional<ImageID> GetWaveformImage(WaveformImagesTable& table,
                                   Instrument const& inst,
                                   Renderer& renderer,
                                   ThreadPool& thread_pool,
                                   f32x2 size);

void StartFrame(WaveformImagesTable& table, Renderer& renderer, Span<Instrument const*> possible_instruments);
void EndFrame(WaveformImagesTable& table);
void Shutdown(WaveformImagesTable& table);
