// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_waveform_images.hpp"

#include "tests/framework.hpp"

#include "common_infrastructure/sample_library/server/sample_library_server.hpp"
#include "common_infrastructure/state/instrument.hpp"

#include "gui_framework/gui_frame.hpp"
#include "gui_framework/image.hpp"
#include "plugin/plugin.hpp"
#include "processor/sample_processing.hpp"

inline Allocator& PixelsAllocator() { return PageAllocator::Instance(); }

static void CreateWaveformImageAsync(WaveformImage::FuturePixels& future,
                                     WaveformAudioSource source,
                                     Instrument const& inst,
                                     UiSize size,
                                     ThreadPool& thread_pool,
                                     FloeInstanceIndex instance_index,
                                     AtomicCountdown& in_flight_instrument_jobs) {
    // We use ValueOr, because we need to have a RefCounted handle, not a _pointer_ to a RefCounted handle
    // since we need to pass the whole handle to the cleanup function.
    auto inst_ref =
        inst.TryGetOpt<sample_lib_server::ResourcePointer<sample_lib::LoadedInstrument>>().ValueOr({});
    if (inst_ref) {
        inst_ref.Retain();
        in_flight_instrument_jobs.Increase();
    }

    thread_pool.Async(
        future,
        [source, size]() -> ImageBytes {
            ArenaAllocator scratch_arena {PageAllocator::Instance()};
            auto const bytes = CreateWaveformImage(source, size, PixelsAllocator(), scratch_arena);
            return {.rgba = bytes.data, .size = size};
        },
        [inst_ref, instance_index, &in_flight_instrument_jobs]() mutable { // Capture the RefCounted handle.
            if (inst_ref) {
                inst_ref.Release();
                in_flight_instrument_jobs.CountDown();
            }
            RequestGuiUpdate(instance_index);
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

static Optional<u64> SourceHashForInstrument(Instrument const& inst, u64 audio_data_hash_override = 0) {
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
            if (audio_data_hash_override) return audio_data_hash_override;
            auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
            auto audio_data = sampled_inst->file_for_gui_waveform;
            if (!audio_data) return k_nullopt;
            return audio_data->hash;
        }
    }
    PanicIfReached();
    return k_nullopt;
}

// Look up an AudioData by hash from the instrument's loaded audio_datas.
// Returns nullptr if not found.
static AudioData const* FindAudioDataByHash(Instrument const& inst, u64 hash) {
    if (inst.tag != InstrumentType::Sampler) return nullptr;
    auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
    for (auto const audio_data : sampled_inst->audio_datas)
        if (audio_data && audio_data->hash == hash) return audio_data;
    return nullptr;
}

Optional<ImageID> GetWaveformImage(WaveformImagesTable& table,
                                   Instrument const& inst,
                                   Renderer& renderer,
                                   ThreadPool& thread_pool,
                                   f32x2 f32_size,
                                   FloeInstanceIndex instance_index,
                                   u64 audio_data_hash_override) {
    auto const size = UiSize::FromFloat2(f32_size);

    auto const opt_source_hash = SourceHashForInstrument(inst, audio_data_hash_override);
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
            AudioData const* audio_data = nullptr;
            if (audio_data_hash_override) audio_data = FindAudioDataByHash(inst, audio_data_hash_override);
            if (!audio_data) {
                auto sampled_inst = inst.GetFromTag<InstrumentType::Sampler>();
                audio_data = sampled_inst->file_for_gui_waveform;
            }
            source = audio_data;
            break;
        }
    }

    auto const key = TableKey(source_hash, size);
    auto e = table.table.FindOrInsertGrowIfNeeded(table.arena, key, {});
    auto& waveform = e.element.data;
    waveform.source_hash = source_hash;
    waveform.last_requested = TimePoint::Now();

    if (!renderer.ImageIdIsValid(waveform.image_id)) {
        bool need_start_loading = false;
        if (!waveform.loading_pixels) {
            waveform.loading_pixels = table.loading_pixels.Prepend(table.arena);
            need_start_loading = true;
        } else if (waveform.loading_pixels->IsInactive()) {
            need_start_loading = true;
        }

        if (need_start_loading)
            CreateWaveformImageAsync(*waveform.loading_pixels,
                                     source,
                                     inst,
                                     size,
                                     thread_pool,
                                     instance_index,
                                     table.in_flight_instrument_jobs);
    }

    return waveform.image_id;
}

void StartFrame(WaveformImagesTable& table, Renderer& renderer) {
    for (auto [_, waveform, _] : table.table) {
        // Consume any finished loading operations.
        if (waveform.loading_pixels) {
            if (auto const result = waveform.loading_pixels->TryReleaseResult()) {
                waveform.image_id = CreateImageIdChecked(renderer, *result); // Create GPU resource.
                result->Free(PixelsAllocator());
            }
        }
    }
}

static void
ReleaseWaveformResources(WaveformImagesTable& table, Renderer& renderer, WaveformImage& waveform) {
    if (waveform.image_id) {
        renderer.DestroyImageID(*waveform.image_id);
        waveform.image_id = k_nullopt;
    }

    auto const future = Exchange(waveform.loading_pixels, nullptr);
    if (!future) return;

    // The job still owns it; the orphan sweep in EndFrame removes it once the job stops.
    if (future->Cancel()) return;

    if (auto const result = future->TryReleaseResult()) result->Free(PixelsAllocator());
    table.loading_pixels.Remove(future);
}

void EndFrame(WaveformImagesTable& table, Renderer& renderer, Span<Instrument const*> possible_instruments) {
    // Clean up orphaned loading futures (cancelled while their job was still in progress).
    table.loading_pixels.RemoveIf([](WaveformImage::FuturePixels& future) {
        auto const status = future.AcquireStatus();
        if (!future.IsInProgress(status) && future.IsCancelled(status)) {
            if (auto const result = future.TryReleaseResult()) result->Free(PixelsAllocator());
            return true;
        }
        return false;
    });

    // Remove entries that are no longer needed. An entry is kept if it matches a currently loaded
    // instrument's static waveform, OR if it was recently requested (which covers dynamically-selected
    // samples via audio_data_hash_override - these won't match any instrument's static waveform but
    // remain alive while actively displayed). The timeout ensures that waveform images for samples
    // that are no longer being played get cleaned up, bounding memory usage even after large
    // polyphonic passages.
    constexpr f64 k_unrequested_timeout_secs = 15.0;
    table.table.RemoveIf([&](u64 const&, WaveformImage& waveform) {
        bool still_needed = false;
        for (auto const inst : possible_instruments) {
            if (auto const h = SourceHashForInstrument(*inst); h && *h == waveform.source_hash) {
                still_needed = true;
                break;
            }
        }

        bool const timed_out =
            waveform.last_requested && waveform.last_requested.SecondsFromNow() > k_unrequested_timeout_secs;

        if (!still_needed && timed_out) {
            ReleaseWaveformResources(table, renderer, waveform);
            return true;
        }
        return false;
    });
}

void InvalidateAllWaveformImages(WaveformImagesTable& table, Renderer& renderer) {
    for (auto [_, waveform, _] : table.table)
        ReleaseWaveformResources(table, renderer, waveform);
}

void Shutdown(WaveformImagesTable& table) {
    for (auto& pixels : table.loading_pixels)
        if (auto image_bytes = pixels.ShutdownAndRelease(10000u)) image_bytes->Free(PixelsAllocator());

    // We must ensure all our Async() cleanup callbacks are completed. Cleanups could happen later and we
    // can't vouch that things like the sample library server is still valid.
    auto const wait_result = table.in_flight_instrument_jobs.WaitUntilZero(10000u);
    ASSERT(wait_result != WaitResult::TimedOut);

    table.loading_pixels.Clear();
    table.table.DeleteAll();
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

struct NullRenderer : Renderer {
    NullRenderer() : Renderer((TextureHandle)0) {}
    ErrorCodeOr<void> Init(UiSize, void*, void*) override { return k_success; }
    void Deinit() override {}
    void OnResize(UiSize, void*) override {}
    ErrorCodeOr<void> CreateFontTexture(FontAtlas&) override { return k_success; }
    void DestroyFontTexture() override {}
    ErrorCodeOr<TextureHandle> CreateTexture(u8 const*, UiSize, u16) override { return (TextureHandle)1; }
    void DestroyTexture(TextureHandle&) override {}
    ErrorCodeOr<void> Render(Span<DrawList*>, UiSize, void*) override { return k_success; }
};

static usize NumLoadingPixels(WaveformImagesTable& table) {
    usize result = 0;
    for (auto& _ : table.loading_pixels)
        ++result;
    return result;
}

static bool EntriesOnlyReferenceLiveFutures(WaveformImagesTable& table) {
    for (auto [_, waveform, _] : table.table) {
        if (!waveform.loading_pixels) continue;
        bool found = false;
        for (auto& future : table.loading_pixels)
            if (&future == waveform.loading_pixels) found = true;
        if (!found) return false;
    }
    return true;
}

TEST_CASE(TestWaveformImages) {
    NullRenderer renderer;
    WaveformImagesTable table;
    DEFER { Shutdown(table); };

    SUBCASE("invalidating while a job is pending doesn't leave a dangling future") {
        auto& waveform = table.table.FindOrInsertGrowIfNeeded(table.arena, 1, {}).element.data;
        waveform.source_hash = 1;
        waveform.last_requested = TimePoint::Now();
        waveform.loading_pixels = table.loading_pixels.Prepend(table.arena);
        auto& future = *waveform.loading_pixels;
        future.SetPending();

        InvalidateAllWaveformImages(table, renderer);
        CHECK(!future.TrySetRunning()); // The job sees the cancellation.

        EndFrame(table, renderer, {});
        CHECK(EntriesOnlyReferenceLiveFutures(table));
        CHECK_EQ(NumLoadingPixels(table), 0u);
    }

    SUBCASE("removing an expired entry frees its inactive future") {
        auto& waveform = table.table.FindOrInsertGrowIfNeeded(table.arena, 2, {}).element.data;
        waveform.source_hash = 2;
        waveform.last_requested = TimePoint::Now() + -60.0;
        waveform.loading_pixels = table.loading_pixels.Prepend(table.arena);

        EndFrame(table, renderer, {});
        CHECK_EQ(table.table.size, 0u);
        CHECK_EQ(NumLoadingPixels(table), 0u);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterWaveformImagesTests) { REGISTER_TEST(TestWaveformImages); }
