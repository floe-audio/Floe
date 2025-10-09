// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common_infrastructure/common_errors.hpp"

#include "gui_framework/draw_list.hpp"
#include "processor/sample_processing.hpp"

struct FloeWaveformImages {
    ErrorCodeOr<graphics::TextureHandle> FetchOrCreate(graphics::DrawContext& graphics,
                                                       ArenaAllocator& scratch_arena,
                                                       WaveformAudioSource source,
                                                       f32 unscaled_width,
                                                       f32 unscaled_height) {
        UiSize const size {CheckedCast<u16>(unscaled_width), CheckedCast<u16>(unscaled_height)};

        u64 source_hash = 0;
        switch (source.tag) {
            case WaveformAudioSourceType::AudioData: {
                auto const audio_data = source.Get<AudioData const*>();
                if (!audio_data) return ErrorCode {CommonError::NotFound};
                source_hash = audio_data->hash;
                break;
            }
            case WaveformAudioSourceType::Sine:
            case WaveformAudioSourceType::WhiteNoise: {
                source_hash = (u64)source.tag + 1;
                break;
            }
        }

        for (auto& waveform : m_waveforms) {
            if (waveform.source_hash == source_hash && waveform.image_id.size == size) {
                auto tex = graphics.GetTextureFromImage(waveform.image_id);
                if (tex) {
                    waveform.used = true;
                    return *tex;
                }
            }
        }

        Waveform waveform {};
        auto pixels = CreateWaveformImage(source, size, scratch_arena, scratch_arena);
        waveform.source_hash = source_hash;
        waveform.image_id = TRY(graphics.CreateImageID(pixels.data, size, 4));
        waveform.used = true;

        dyn::Append(m_waveforms, waveform);
        auto tex = graphics.GetTextureFromImage(waveform.image_id);
        ASSERT(tex);
        return *tex;
    }

    void StartFrame() {
        for (auto& waveform : m_waveforms)
            waveform.used = false;
    }

    void EndFrame(graphics::DrawContext& graphics) {
        dyn::RemoveValueIf(m_waveforms, [&graphics](Waveform& w) {
            if (!w.used) {
                graphics.DestroyImageID(w.image_id);
                return true;
            }
            return false;
        });
    }

    void Clear() { dyn::Clear(m_waveforms); }

    struct Waveform {
        u64 source_hash {};
        graphics::ImageID image_id = graphics::k_invalid_image_id;
        bool used {};
    };

    ArenaAllocator arena {PageAllocator::Instance()};
    DynamicArray<Waveform> m_waveforms {arena};
};
