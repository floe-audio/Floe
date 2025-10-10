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
                                     u64 source_hash,
                                     UiSize size,
                                     ThreadPool& thread_pool) {
    auto const inst_ref = inst.TryGetFromTag<InstrumentType::Sampler>();
    if (inst_ref) {
        inst_ref->Retain();
        LogDebug(ModuleName::Gui,
                 "increased ref count to {}: {}",
                 inst_ref->m_ref_count->Load(LoadMemoryOrder::Acquire),
                 source_hash);
    }

    thread_pool.Async(
        future,
        [source, size, source_hash]() -> ImageBytes {
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto const bytes = CreateWaveformImage(source, size, PixelsAllocator(), scratch_arena);
            LogDebug(ModuleName::Gui, "ending async waveform image load: {}", source_hash);
            return {.rgba = bytes.data, .size = size};
        },
        [inst_ref, source_hash]() {
            LogDebug(ModuleName::Gui, "cleanup for waveform image load: {}", source_hash);
            if (inst_ref) {
                inst_ref->Release();
                LogDebug(ModuleName::Gui,
                         "decreased ref count to {}: {}",
                         inst_ref->m_ref_count->Load(LoadMemoryOrder::Acquire),
                         source_hash);
            }
        });
}

static void FreeWaveform(WaveformImage& waveform, WaveformPixelsFutureAllocator& allocator) {
    waveform.loading_pixels->Shutdown();
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

    WaveformImageKey const key {source_hash, size};

    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, key, {});
    auto& waveform = e.element.data;
    waveform.used = true;
    waveform.hash = source_hash;

    if (e.inserted) waveform.loading_pixels = table.future_allocator.Allocate(table.arena);

    if (!graphics.ImageIdIsValid(waveform.image_id) && waveform.loading_pixels->IsInactive()) {
        LogDebug(ModuleName::Gui, "starting async waveform image load: {}", source_hash);
        CreateWaveformImageAsync(*waveform.loading_pixels, source, inst, source_hash, size, thread_pool);
    }

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
    table.table.RemoveIf([&](WaveformImageKey const&, WaveformImage& waveform) {
        if (!waveform.used) {
            LogDebug(ModuleName::Gui, "removing unused waveform image: {}", waveform.hash);
            if (waveform.image_id) {
                graphics.DestroyImageID(*waveform.image_id);
                waveform.image_id = k_nullopt;
            }
            FreeWaveform(waveform, table.future_allocator);
            // waveform.loading_pixels->Shutdown();
            return true;
        }
        return false;
    });
    LogDebug(ModuleName::Gui, "waveform images in table: {}", table.table.size);
    (void)table;
    (void)graphics;
}

void Shutdown(WaveformImagesTable& table) {
    for (auto [_, waveform, _] : table.table)
        FreeWaveform(waveform, table.future_allocator);
    table.table.DeleteAll();
}
