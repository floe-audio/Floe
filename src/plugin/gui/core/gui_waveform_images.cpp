// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_waveform_images.hpp"

#include "common_infrastructure/state/instrument.hpp"

#include "gui_framework/image.hpp"
#include "processor/sample_processing.hpp"
#include "sample_lib_server/sample_library_server.hpp"

inline Allocator& PixelsAllocator() { return PageAllocator::Instance(); }

static void CreateWaveformImageAsync(WaveformImage::FuturePixels& future,
                                     WaveformAudioSource source,
                                     Instrument const& inst,
                                     UiSize size,
                                     ThreadPool& thread_pool) {
    // We use ValueOr, because we need to have a RefCounted handle, not a _pointer_ to a RefCounted handle
    // since we need to pass the whole handle to the cleanup function.
    auto inst_ref =
        inst.TryGetOpt<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>().ValueOr({});
    if (inst_ref) inst_ref.Retain();

    thread_pool.Async(
        future,
        [source, size]() -> ImageBytes {
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto const bytes = CreateWaveformImage(source, size, PixelsAllocator(), scratch_arena);
            return {.rgba = bytes.data, .size = size};
        },
        [inst_ref]() mutable { // Capture by value the RefCounted handle.
            if (inst_ref) inst_ref.Release();
        },
        JobPriority::High);
}

static u64 TableKey(u64 source_hash, UiSize size) {
    auto hash = HashInit();
    HashUpdate(hash, source_hash);
    HashUpdate(hash, size.width);
    HashUpdate(hash, size.height);
    return hash;
}

static Optional<u64> SourceHashForInstrument(Instrument const& inst) {
    switch (inst.tag) {
        case InstrumentType::None: return k_nullopt;

        case InstrumentType::WaveformSynth: {
            WaveformAudioSource source {WaveformAudioSourceType::Sine};
            switch (inst.Get<WaveformType>()) {
                case WaveformType::Sine: source = WaveformAudioSourceType::Sine; break;
                case WaveformType::WhiteNoiseMono:
                case WaveformType::WhiteNoiseStereo: source = WaveformAudioSourceType::WhiteNoise; break;
                case WaveformType::Count: PanicIfReached();
            }
            return (u64)source.tag + 1;
        }

        case InstrumentType::Sampler: {
            auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
            auto audio_data = sampled_inst->file_for_gui_waveform;
            if (!audio_data) return k_nullopt;
            return audio_data->hash;
        }
    }
    PanicIfReached();
    return k_nullopt;
}

Optional<ImageID> GetWaveformImage(WaveformImagesTable& table,
                                   Instrument const& inst,
                                   Renderer& renderer,
                                   ThreadPool& thread_pool,
                                   f32x2 f32_size) {
    auto const size = UiSize::FromFloat2(f32_size);

    auto const opt_source_hash = SourceHashForInstrument(inst);
    if (!opt_source_hash) return k_nullopt;
    auto const source_hash = *opt_source_hash;

    WaveformAudioSource source {WaveformAudioSourceType::Sine};
    switch (inst.tag) {
        case InstrumentType::None: PanicIfReached(); break;

        case InstrumentType::WaveformSynth: {
            switch (inst.Get<WaveformType>()) {
                case WaveformType::Sine: source = WaveformAudioSourceType::Sine; break;
                case WaveformType::WhiteNoiseMono:
                case WaveformType::WhiteNoiseStereo: source = WaveformAudioSourceType::WhiteNoise; break;
                case WaveformType::Count: PanicIfReached();
            }
            break;
        }

        case InstrumentType::Sampler: {
            auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
            source = sampled_inst->file_for_gui_waveform;
            break;
        }
    }

    auto const key = TableKey(source_hash, size);
    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, key, {});
    auto& waveform = e.element.data;
    waveform.source_hash = source_hash;

    if (!renderer.ImageIdIsValid(waveform.image_id)) {
        bool need_start_loading = false;
        if (!waveform.loading_pixels) {
            waveform.loading_pixels = table.loading_pixels.Prepend(table.arena);
            need_start_loading = true;
        } else if (waveform.loading_pixels->IsInactive()) {
            need_start_loading = true;
        }

        if (need_start_loading)
            CreateWaveformImageAsync(*waveform.loading_pixels, source, inst, size, thread_pool);
    }

    return waveform.image_id;
}

void StartFrame(WaveformImagesTable& table,
                Renderer& renderer,
                Span<Instrument const*> possible_instruments) {
    for (auto [_, waveform, _] : table.table) {
        // Consume any finished loading operations.
        if (waveform.loading_pixels) {
            if (auto const result = waveform.loading_pixels->TryReleaseResult()) {
                waveform.image_id = CreateImageIdChecked(renderer, *result); // Create GPU resource.
                result->Free(PixelsAllocator());
            }
        }
    }

    // Remove entries that don't correspond to any current instrument.
    table.table.RemoveIf([&](u64 const&, WaveformImage& waveform) {
        bool still_needed = false;
        for (auto const inst : possible_instruments) {
            if (auto const h = SourceHashForInstrument(*inst); h && *h == waveform.source_hash) {
                still_needed = true;
                break;
            }
        }

        if (!still_needed) {
            if (waveform.image_id) {
                renderer.DestroyImageID(*waveform.image_id);
                waveform.image_id = k_nullopt;
            }
            if (waveform.loading_pixels) {
                if (auto const result = waveform.loading_pixels->TryReleaseResult()) {
                    result->Free(PixelsAllocator());
                    table.loading_pixels.Remove(waveform.loading_pixels);
                } else {
                    waveform.loading_pixels->Cancel();
                }
            }
            return true;
        }
        return false;
    });
}

void EndFrame(WaveformImagesTable& table) {
    // Clean up orphaned loading futures (cancelled in StartFrame but not yet finished at that time).
    table.loading_pixels.RemoveIf([](WaveformImage::FuturePixels& future) {
        auto const status = future.AcquireStatus();
        if (!future.IsInProgress(status) && future.IsCancelled(status)) {
            if (auto const result = future.TryReleaseResult()) result->Free(PixelsAllocator());
            return true;
        }
        return false;
    });
}

void Shutdown(WaveformImagesTable& table) {
    for (auto& pixels : table.loading_pixels)
        if (auto image_bytes = pixels.ShutdownAndRelease(10000u)) image_bytes->Free(PixelsAllocator());
    table.loading_pixels.Clear();
    table.table.DeleteAll();
}
