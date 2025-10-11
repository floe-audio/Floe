// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_waveform_images.hpp"

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
        [inst_ref]() mutable {
            if (inst_ref) inst_ref.Release();
        });
}

static void FreeWaveform(WaveformImage& waveform, WaveformPixelsFutureAllocator& allocator) {
    waveform.loading_pixels->Shutdown(10000u);
    if (waveform.loading_pixels->HasResult())
        waveform.loading_pixels->ReleaseResult().Free(PixelsAllocator());
    else
        waveform.loading_pixels->Reset();

    allocator.Free(waveform.loading_pixels);
}

Optional<graphics::ImageID> GetWaveformImage(WaveformImagesTable& table,
                                             Instrument const& inst,
                                             graphics::DrawContext& graphics,
                                             ThreadPool& thread_pool,
                                             f32x2 f32_size) {
    auto const size = UiSize::FromFloat2(f32_size);

    u64 source_hash = 0;
    WaveformAudioSource source {WaveformAudioSourceType::Sine};

    switch (inst.tag) {
        case InstrumentType::None: return k_nullopt;

        case InstrumentType::WaveformSynth: {
            switch (inst.Get<WaveformType>()) {
                case WaveformType::Sine: source = WaveformAudioSourceType::Sine; break;
                case WaveformType::WhiteNoiseMono:
                case WaveformType::WhiteNoiseStereo: source = WaveformAudioSourceType::WhiteNoise; break;
                case WaveformType::Count: PanicIfReached();
            }
            source_hash = (u64)source.tag + 1;
            break;
        }

        case InstrumentType::Sampler: {
            auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
            auto audio_data = sampled_inst->file_for_gui_waveform;
            if (!audio_data) return k_nullopt;
            source = audio_data;
            source_hash = audio_data->hash;
            break;
        }
    }

    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, source_hash, {});
    auto& waveform = e.element.data;
    waveform.used = true;

    if (e.inserted) waveform.loading_pixels = table.future_allocator.Allocate(table.arena);

    if (!graphics.ImageIdIsValid(waveform.image_id) && waveform.loading_pixels->IsInactive())
        CreateWaveformImageAsync(*waveform.loading_pixels, source, inst, size, thread_pool);

    return waveform.image_id;
}

void StartFrame(WaveformImagesTable& table, graphics::DrawContext& graphics) {
    for (auto [_, waveform, _] : table.table) {
        waveform.used = false;

        if (auto const result = waveform.loading_pixels->TryReleaseResult()) {
            waveform.image_id = CreateImageIdChecked(graphics, *result);
            result->Free(PixelsAllocator());
        }
    }
}

void EndFrame(WaveformImagesTable& table, graphics::DrawContext& graphics) {
    table.table.RemoveIf([&](u64 const&, WaveformImage& waveform) {
        if (!waveform.used) {
            if (waveform.image_id) {
                graphics.DestroyImageID(*waveform.image_id);
                waveform.image_id = k_nullopt;
            }
            FreeWaveform(waveform, table.future_allocator);
            return true;
        }
        return false;
    });
}

void Shutdown(WaveformImagesTable& table) {
    for (auto [_, waveform, _] : table.table)
        FreeWaveform(waveform, table.future_allocator);
    table.table.DeleteAll();
}
