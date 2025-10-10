// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/draw_list.hpp"
#include "gui_framework/image.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct WaveformImageKey {
    u64 source_hash {};
    UiSize size {};

    constexpr bool operator==(WaveformImageKey const& other) const = default;

    u64 Hash() const { return ::Hash(source_hash) ^ (::Hash(size.width) << 1) ^ (::Hash(size.height) << 2); }
};

struct WaveformImage {
    using FuturePixels = Future<ImageBytes>;
    Optional<graphics::ImageID> image_id {};
    bool used {};
    u64 hash {};
    FuturePixels* loading_pixels {};
};

struct WaveformPixelsFutureAllocator {
    struct Node : WaveformImage::FuturePixels {
        Node* next {nullptr};
    };

    WaveformImage::FuturePixels* Allocate(ArenaAllocator& a) {
        if (free_list) {
            auto* n = free_list;
            free_list = free_list->next;
            return n;
        }
        return a.New<Node>();
    }

    void Free(WaveformImage::FuturePixels* f) {
        auto* n = (Node*)f;
        n->next = free_list;
        free_list = n;
    }

    Node* free_list {};
};

struct WaveformImagesTable {
    ArenaAllocator arena {PageAllocator::Instance()};
    WaveformPixelsFutureAllocator future_allocator {};
    HashTable<WaveformImageKey, WaveformImage> table;
};

Optional<graphics::ImageID> GetWaveformImage(WaveformImagesTable& table,
                                             Instrument const& inst,
                                             graphics::DrawContext& graphics,
                                             ThreadPool& thread_pool,
                                             f32x2 size);

void StartFrame(WaveformImagesTable& table, graphics::DrawContext& graphics);
void EndFrame(WaveformImagesTable& table, graphics::DrawContext& graphics);
void Shutdown(WaveformImagesTable& table);
