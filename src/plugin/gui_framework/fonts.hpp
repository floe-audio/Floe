// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#pragma once

#include "foundation/foundation.hpp"
#include "utils/basic_dynamic_array.hpp"

using Char16 = u16;
constexpr u32 k_max_u16_codepoint = 0xFFFF;

struct DrawList;
struct Font;

struct GlyphRange {
    Char16 start;
    Char16 end; // inclusive
};
using GlyphRanges = DynamicArrayBounded<GlyphRange, 10>;

struct FontConfig {
    bool font_data_reference_only = false;
    void* font_data = nullptr;
    usize font_data_size = 0;
    bool font_data_owned_by_atlas = false;
    int font_no = 0; // Index of font within TTF file
    f32 size_pixels = 0.0f; // Size in pixels for rasterizer

    // Rasterize at higher quality for sub-pixel positioning. We don't use sub-pixel
    int oversample_h = 3;
    int oversample_v = 1;

    // Align every glyph to pixel boundary. Useful e.g. if you are merging a non-pixel aligned font a pixel
    // font. If enabled, you can set oversample_h/v to 1.
    bool pixel_snap_h = false;
    f32x2 glyph_extra_spacing = {}; // Extra spacing (in pixels) between glyphs.
    GlyphRanges glyph_ranges = {};

    // Merge into previous Font, so you can combine multiple inputs font into one Font (e.g. ASCII font +
    // icons + Japanese glyphs).
    bool merge_mode = false;

    // When merging (multiple FontInput for one Font), vertically center new glyphs instead of aligning their
    // baseline
    bool merge_glyph_center_v = false;

    char name[32] {}; // debug only
    Font* dst_font = nullptr;
};

struct FontAtlas {
    ~FontAtlas() { Clear(); }

    Font* AddFont(FontConfig const& font_cfg);

    // Transfer ownership of 'ttf_data' to FontAtlas, unless font_cfg_template->font_data_owned_by_atlas
    // == false. Or, you can specific font_data_reference_only = true in which case you must keep the memory
    // valid until Build(). Owned TTF buffer will be deleted after Build().
    Font* AddFontFromMemoryTTF(void* ttf_data,
                               usize ttf_size,
                               f32 size_pixels,
                               FontConfig const& font_cfg = {},
                               Span<GlyphRange const> glyph_ranges = {});

    // Clear the CPU-side texture data. Saves RAM once the texture has been copied to graphics memory.
    void ClearTexData();

    // Clear the input TTF data (inc sizes, glyph ranges)
    void ClearInputData();

    // Clear the Gui-side font data (glyphs storage, UV coordinates)
    void ClearFonts();

    // Clear all
    void Clear();

    void GetTexDataAsAlpha8(unsigned char** out_pixels,
                            int* out_width,
                            int* out_height,
                            int* out_bytes_per_pixel = nullptr); // 1 byte per-pixel

    void GetTexDataAsRGBA32(unsigned char** out_pixels,
                            int* out_width,
                            int* out_height,
                            int* out_bytes_per_pixel = nullptr); // 4 bytes-per-pixel

    static GlyphRanges GetGlyphRangesDefault(); // Basic Latin, Extended Latin
    static GlyphRanges GetGlyphRangesDefaultAudioPlugin();

    Font* operator[](u32 i) {
        ASSERT(i < fonts.size);
        return fonts[i];
    }
    Font const* operator[](u32 i) const {
        ASSERT(i < fonts.size);
        return fonts[i];
    }

    unsigned char* tex_pixels_alpha8 = nullptr;
    unsigned int* tex_pixels_rgb_a32 = nullptr;

    int tex_width = 0;
    int tex_height = 0;

    // Texture width desired by user before Build(). Must be a power-of-two. If have many glyphs your graphics
    // API have texture size restrictions you may want to increase texture width to decrease height.
    int tex_desired_width = 0;
    f32x2 tex_uv_white_pixel = {}; // Texture coordinates to a white pixel
    BasicDynamicArray<Font*> fonts = {};

    // Private
    BasicDynamicArray<FontConfig> config_data = {};
    bool Build(); // Build pixels data. This is automatically called by the GetTexData*** functions.
    void RenderCustomTexData(int pass, void* rects);
};

// Font runtime data and rendering
struct Font {
    struct Glyph {
        Char16 codepoint;
        f32 x_advance;
        f32 x0, y0, x1, y1;
        f32 u0, v0, u1, v1; // Texture coordinates
    };

    void BuildLookupTable();
    Glyph const* FindGlyph(Char16 c) const;
    f32 GetCharAdvance(Char16 c) const {
        return (c < index_x_advance.size) ? index_x_advance[c] : fallback_x_advance;
    }
    bool IsLoaded() const { return container_atlas != nullptr; }

    struct TextSizeOptions {
        // 0 means use the font's default size.
        f32 font_size = 0;
        // Stops rendering after a certain width. FLT_MAX to disable.
        f32 max_width = FLT_MAX;
        // Automatic word-wrapping across multiple lines to fit into given width; 0.0f to disable.
        f32 wrap_width = 0;
        //
        char const** remaining = nullptr;
    };
    f32x2 CalcTextSize(String str, TextSizeOptions const& options) const;

    char const*
    CalcWordWrapPositionA(f32 scale, char const* text, char const* text_end, f32 wrap_width) const;

    void RenderChar(DrawList* draw_list, f32 size, f32x2 pos, u32 col, Char16 c) const;

    void RenderText(DrawList* draw_list,
                    f32 size,
                    f32x2 pos,
                    u32 col,
                    f32x4 const& clip_rect,
                    String text,
                    f32 wrap_width = 0.0f,
                    bool cpu_fine_clip = false) const;

    f32 LargestStringWidth(f32 pad, void* items, int num, String (*GetStr)(void* items, int index)) const;
    f32 LargestStringWidth(f32 pad, Span<String const> strs) const;

    // Private
    void GrowIndex(u32 new_size);

    // Makes 'dst' character/glyph points to 'src' character/glyph. Currently needs to be called AFTER fonts
    // have been built.
    void AddRemapChar(Char16 dst, Char16 src, bool overwrite_dst = true);

    static constexpr Char16 k_fallback_char = L'?';
    static constexpr Char16 k_invalid_codepoint = (Char16)-1;

    f32 font_size {};

    f32x2 display_offset {0, 1};
    BasicDynamicArray<Glyph> glyphs {};

    // Sparse. Glyphs->XAdvance in a directly indexable way (more cache-friendly, for CalcTextSize functions
    // which are often bottleneck in large UI).
    BasicDynamicArray<f32> index_x_advance {};

    // Sparse. Index glyphs by Unicode code-point.
    BasicDynamicArray<Char16> index_lookup {};

    Glyph const* fallback_glyph {};
    f32 fallback_x_advance {};

    FontConfig* config_data {}; // Pointer within container_atlas->config_data
    FontAtlas* container_atlas {};
    f32 ascent {}; // Ascent: distance from top to bottom of e.g. 'A' [0..font_size]
    f32 descent {};
};

// The user controls what is the 'active font' by pushing/popping fonts to a font stack. This way, all
// draw-list or text size calculation can work on the current active font without having to pass fonts
// everywhere. For example, at the start of a button drawing function, you might:
// fonts.Push(button_font);
// DEFER { fonts.Pop(); };

struct Fonts {
    void Push(Font* font);
    void Push(u32 index) { Push(atlas.fonts[index]); }
    void Pop();

    Font* Current() const {
        if (font_stack.Empty()) return nullptr;
        return font_stack.Back();
    }

    f32x2 CalcTextSize(String str, Font::TextSizeOptions const& options) const {
        return Current()->CalcTextSize(str, options);
    }

    // Pushed/popped during a frame to change the currently active font, always returning to empty at the end
    // of the frame.
    BasicDynamicArray<Font*> font_stack {};

    // The atlas that actually owns the font objects.
    FontAtlas atlas {};
};
