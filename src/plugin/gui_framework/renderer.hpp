// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Based on code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

struct DrawList;
struct FontAtlas;

struct ImageID {
    bool operator==(ImageID const& other) const = default;
    u64 id;
    UiSize size;
};

constexpr ImageID k_invalid_image_id = {0, {}};

enum class TextureHandle : u64 {};

struct Renderer {
    Renderer(TextureHandle invalid_texture) : invalid_texture(invalid_texture) {}
    virtual ~Renderer() {}

    virtual ErrorCodeOr<void> Init(UiSize size, void* native_window, void* native_display) = 0;
    virtual void Deinit() = 0;

    virtual void OnResize(UiSize size, void* native_display) = 0;

    // The application must build the font atlas first, and then call this.
    virtual ErrorCodeOr<void> CreateFontTexture(FontAtlas& atlas) = 0;
    virtual void DestroyFontTexture() = 0;

    // Never store a TextureHandle longer than a single frame. It can become invalidated between frames.
    // Data should be either RGB or RGBA bytes and bytes_per_pixel set accordingly.
    virtual ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) = 0;
    virtual void DestroyTexture(TextureHandle& id) = 0;

    virtual ErrorCodeOr<void> Render(Span<DrawList*> draw_lists, UiSize window_size, void* native_window) = 0;

    // Texture handles can be invalidated between frames, the application should use this method to check if
    // the ID still has a corresponding texture.
    Optional<TextureHandle> GetTextureFromImage(ImageID id) {
        auto const index = FindIf(textures, [id](IdAndTexture const& i) { return i.id == id; });
        if (!index) return k_nullopt;
        return textures[*index].texture;
    }

    bool ImageIdIsValid(ImageID id) { return GetTextureFromImage(id).HasValue(); }
    bool ImageIdIsValid(Optional<ImageID> id) {
        if (!id) return false;
        return ImageIdIsValid(*id);
    }
    Optional<TextureHandle> GetTextureFromImage(Optional<ImageID> id) {
        if (!id) return k_nullopt;
        return GetTextureFromImage(*id);
    }

    ErrorCodeOr<ImageID> CreateImageID(u8 const* data, UiSize size, u16 bytes_per_pixel) {
        auto tex = TRY(CreateTexture(data, size, bytes_per_pixel));
        ASSERT(tex != invalid_texture);
        auto const id = ImageID {image_id_counter++, size};
        dyn::Append(textures, IdAndTexture {id, tex});
        return id;
    }

    void DestroyImageID(ImageID& id) {
        auto const index = FindIf(textures, [id](IdAndTexture const& i) { return i.id == id; });
        if (!index) return;
        auto item = textures[*index];
        DestroyTexture(item.texture);
        dyn::RemoveSwapLast(textures, *index);
    }

    void DestroyAllTextures() {
        for (auto& i : textures) {
            ASSERT(i.texture != invalid_texture);
            DestroyTexture(i.texture);
        }
        dyn::Clear(textures);
    }

    TextureHandle const invalid_texture;

    u64 image_id_counter {1};

    struct IdAndTexture {
        ImageID id;
        TextureHandle texture;
    };

    DynamicArrayBounded<char, 3000> graphics_device_info {};

    TextureHandle font_texture {invalid_texture};

    DynamicArray<IdAndTexture> textures {Malloc::Instance()};

    bool anti_aliased_lines = true;
    bool anti_aliased_shapes = true;
    f32 curve_tessellation_tol = 1.25f; // increase for better quality
    f32 fill_anti_alias = 1.0f;
    f32 stroke_anti_alias = 1.0f;
};

// Call delete on the result.
Renderer* CreateNewRendererBgfx();
Renderer* CreateNewRendererOpenGl();
Renderer* CreateNewRendererDirect3D9();
enum class RendererBackend : u8 { Bgfx, OpenGl, Direct3D9, Count };
inline Renderer* CreateNewRenderer(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::Bgfx: return CreateNewRendererBgfx();
        case RendererBackend::OpenGl: return CreateNewRendererOpenGl();
        case RendererBackend::Direct3D9: return CreateNewRendererDirect3D9();
        case RendererBackend::Count: PanicIfReached();
    }
    PanicIfReached();
    return nullptr;
}
