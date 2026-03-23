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

// When audio_data_hash_override is non-zero and the instrument is a sampler, the waveform image is
// generated from the AudioData matching this hash (looked up from the instrument's audio_datas)
// instead of the instrument's statically-chosen file_for_gui_waveform. This is used to display the
// waveform of the last-played sample.
Optional<ImageID> GetWaveformImage(WaveformImagesTable& table,
                                   Instrument const& inst,
                                   Renderer& renderer,
                                   ThreadPool& thread_pool,
                                   f32x2 size,
                                   FloeInstanceIndex instance_index,
                                   u64 audio_data_hash_override = 0);

void StartFrame(WaveformImagesTable& table, Renderer& renderer);
void EndFrame(WaveformImagesTable& table, Renderer& renderer, Span<Instrument const*> possible_instruments);
void Shutdown(WaveformImagesTable& table);
