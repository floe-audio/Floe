// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "fonts.hpp"

#include "colours.hpp"
#include "draw_list.hpp"

void Fonts::Push(Font* font) {
    ASSERT(font != nullptr);
    font_stack.PushBack(font);
}

void Fonts::Pop() { font_stack.PopBack(); }

#define STBTT_ifloor(x)    ((int)Floor(x))
#define STBTT_iceil(x)     ((int)Ceil(x))
#define STBTT_sqrt(x)      Sqrt(x)
#define STBTT_pow(x, y)    Pow(x, y)
#define STBTT_fabs(x)      Fabs(x)
#define STBTT_fmod(x, y)   Fmod(x, y)
#define STBTT_cos(x)       Cos(x)
#define STBTT_acos(x)      Acos(x)
#define STBTT_fabs(x)      Fabs(x)
#define STBTT_malloc(x, u) ((void)(u), GlobalAllocOversizeAllowed({(usize)x}).data)
#define STBTT_free(x, u)   ((void)(u), GlobalFreeNoSize(x))
#define STBTT_assert(x)    ASSERT(x)
#define STBTT_strlen(x)    NullTerminatedSize(x)
#define STBTT_memcpy       CopyMemory
#define STBTT_memset       FillMemory

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#pragma clang diagnostic pop

void FontAtlas::ClearInputData() {
    for (u32 i = 0; i < config_data.size; i++)
        if (!config_data[i].font_data_reference_only && config_data[i].font_data &&
            config_data[i].font_data_owned_by_atlas) {
            GlobalFreeNoSize(config_data[i].font_data);
            config_data[i].font_data = nullptr;
        }

    // When clearing this we lose access to the font name and other information used to build the font.
    for (u32 i = 0; i < fonts.size; i++)
        if (fonts[i]->config_data >= config_data.data &&
            fonts[i]->config_data < config_data.data + config_data.size) {
            fonts[i]->config_data = nullptr;
        }
    config_data.Clear();
}

void FontAtlas::ClearTexData() {
    if (tex_pixels_alpha8) GlobalFreeNoSize(tex_pixels_alpha8);
    if (tex_pixels_rgb_a32) GlobalFreeNoSize(tex_pixels_rgb_a32);
    tex_pixels_alpha8 = nullptr;
    tex_pixels_rgb_a32 = nullptr;
}

void FontAtlas::ClearFonts() {
    for (u32 i = 0; i < fonts.size; i++) {
        fonts[i]->~Font();
        GlobalFreeNoSize(fonts[i]);
    }
    fonts.Clear();
}

void FontAtlas::Clear() {
    ClearInputData();
    ClearTexData();
    ClearFonts();
}

void FontAtlas::GetTexDataAsAlpha8(unsigned char** out_pixels,
                                   int* out_width,
                                   int* out_height,
                                   int* out_bytes_per_pixel) {
    // Build atlas on demand
    if (tex_pixels_alpha8 == nullptr) {
        auto const built = Build();
        ASSERT(built);
    }

    *out_pixels = tex_pixels_alpha8;
    if (out_width) *out_width = tex_width;
    if (out_height) *out_height = tex_height;
    if (out_bytes_per_pixel) *out_bytes_per_pixel = 1;
}

void FontAtlas::GetTexDataAsRGBA32(unsigned char** out_pixels,
                                   int* out_width,
                                   int* out_height,
                                   int* out_bytes_per_pixel) {
    // Convert to RGBA32 format on demand
    // Although it is likely to be the most commonly used format, our font rendering is 1 channel / 8 bpp
    if (!tex_pixels_rgb_a32) {
        unsigned char* pixels;
        GetTexDataAsAlpha8(&pixels, nullptr, nullptr);
        tex_pixels_rgb_a32 =
            (unsigned int*)GlobalAllocOversizeAllowed({(usize)(tex_width * tex_height * 4)}).data;
        unsigned char const* src = pixels;
        unsigned int* dst = tex_pixels_rgb_a32;
        for (int n = tex_width * tex_height; n > 0; n--)
            *dst++ = Rgba(255, 255, 255, (u8)(*src++) / 255.0f);
    }

    *out_pixels = (unsigned char*)tex_pixels_rgb_a32;
    if (out_width) *out_width = tex_width;
    if (out_height) *out_height = tex_height;
    if (out_bytes_per_pixel) *out_bytes_per_pixel = 4;
}

Font* FontAtlas::AddFont(FontConfig const& font_cfg) {
    ASSERT(font_cfg.font_data != nullptr && font_cfg.font_data_size > 0);
    ASSERT(font_cfg.size_pixels > 0.0f);

    // Create new font
    if (!font_cfg.merge_mode) {
        auto* font = (Font*)GlobalAllocOversizeAllowed({sizeof(Font)}).data;
        PLACEMENT_NEW(font) Font();
        fonts.PushBack(font);
    }

    config_data.PushBack(font_cfg);
    FontConfig& new_font_cfg = config_data.Back();
    if (!new_font_cfg.dst_font) new_font_cfg.dst_font = fonts.Back();
    if (!new_font_cfg.font_data_reference_only && !new_font_cfg.font_data_owned_by_atlas) {
        new_font_cfg.font_data = GlobalAllocOversizeAllowed({new_font_cfg.font_data_size}).data;
        new_font_cfg.font_data_owned_by_atlas = true;
        CopyMemory(new_font_cfg.font_data, font_cfg.font_data, new_font_cfg.font_data_size);
    }

    // Invalidate texture
    ClearTexData();
    return new_font_cfg.dst_font;
}

Font* FontAtlas::AddFontFromMemoryTTF(void* ttf_data,
                                      usize ttf_size,
                                      f32 size_pixels,
                                      FontConfig const& font_cfg_arg,
                                      Span<GlyphRange const> glyph_ranges) {
    FontConfig font_cfg = font_cfg_arg;
    ASSERT(glyph_ranges.size < font_cfg.glyph_ranges.Capacity());
    ASSERT(font_cfg.font_data == nullptr);
    font_cfg.font_data = ttf_data;
    font_cfg.font_data_size = ttf_size;
    font_cfg.size_pixels = size_pixels;
    if (glyph_ranges.size) font_cfg.glyph_ranges = glyph_ranges;
    return AddFont(font_cfg);
}

bool FontAtlas::Build() {
    ASSERT(config_data.size > 0);

    tex_width = tex_height = 0;
    tex_uv_white_pixel = f32x2 {0, 0};
    ClearTexData();

    struct FontTempBuildData {
        stbtt_fontinfo font_info;
        stbrp_rect* rects;
        stbtt_pack_range* ranges;
        u32 ranges_count;
    };
    auto* tmp_array =
        (FontTempBuildData*)GlobalAllocOversizeAllowed({(usize)config_data.size * sizeof(FontTempBuildData)})
            .data;

    // Initialize font information early (so we can error without any cleanup) + count glyphs
    u32 total_glyph_count = 0;
    u32 total_glyph_range_count = 0;
    for (u32 input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];

        ASSERT(cfg.dst_font && (!cfg.dst_font->IsLoaded() || cfg.dst_font->container_atlas == this));
        int const font_offset = stbtt_GetFontOffsetForIndex((unsigned char*)cfg.font_data, cfg.font_no);
        ASSERT(font_offset >= 0);
        if (!stbtt_InitFont(&tmp.font_info, (unsigned char*)cfg.font_data, font_offset)) return false;

        // Count glyphs
        if (!cfg.glyph_ranges.size) cfg.glyph_ranges = GetGlyphRangesDefault();
        for (auto const& glyph_range : cfg.glyph_ranges) {
            total_glyph_count += (glyph_range.end - glyph_range.start) + 1;
            total_glyph_range_count++;
        }
    }

    // Start packing. We need a known width for the skyline algorithm. Using a cheap heuristic here to decide
    // of width. User can override TexDesiredWidth if they wish. After packing is done, width shouldn't matter
    // much, but some API/GPU have texture size limitations and increasing width can decrease height.
    tex_width = (tex_desired_width > 0)      ? tex_desired_width
                : (total_glyph_count > 4000) ? 4096
                : (total_glyph_count > 2000) ? 2048
                : (total_glyph_count > 1000) ? 1024
                                             : 512;
    tex_height = 0;
    int const max_tex_height = 1024 * 32;
    stbtt_pack_context spc {};
    stbtt_PackBegin(&spc, nullptr, tex_width, max_tex_height, 0, 1, nullptr);

    // Pack our extra data rectangles first, so it will be on the upper-left corner of our texture (UV will
    // have small values).
    BasicDynamicArray<stbrp_rect> extra_rects;
    RenderCustomTexData(0, &extra_rects);
    stbtt_PackSetOversampling(&spc, 1, 1);
    stbrp_pack_rects((stbrp_context*)spc.pack_info, &extra_rects[0], (int)extra_rects.size);
    for (u32 i = 0; i < extra_rects.size; i++)
        if (extra_rects[i].was_packed) tex_height = Max(tex_height, extra_rects[i].y + extra_rects[i].h);

    // Allocate packing character data and flag packed characters buffer as non-packed (x0=y0=x1=y1=0)
    u32 buf_packedchars_n = 0;
    u32 buf_rects_n = 0;
    u32 buf_ranges_n = 0;
    auto* buf_packedchars =
        (stbtt_packedchar*)GlobalAllocOversizeAllowed({total_glyph_count * sizeof(stbtt_packedchar)}).data;
    auto* buf_rects = (stbrp_rect*)GlobalAllocOversizeAllowed({total_glyph_count * sizeof(stbrp_rect)}).data;
    auto* buf_ranges =
        (stbtt_pack_range*)GlobalAllocOversizeAllowed({total_glyph_range_count * sizeof(stbtt_pack_range)})
            .data;
    ZeroMemory(buf_packedchars, total_glyph_count * sizeof(stbtt_packedchar));
    ZeroMemory(buf_rects,
               total_glyph_count *
                   sizeof(stbrp_rect)); // Unnecessary but let's clear this for the sake of sanity.
    ZeroMemory(buf_ranges, total_glyph_range_count * sizeof(stbtt_pack_range));

    // First font pass: pack all glyphs (no rendering at this point, we are working with rectangles in an
    // infinitely tall texture at this point)
    for (u32 input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];

        // Setup ranges
        u32 glyph_count = 0;
        u32 glyph_ranges_count = 0;
        for (auto const& glyph_range : cfg.glyph_ranges) {
            glyph_count += (glyph_range.end - glyph_range.start) + 1;
            glyph_ranges_count++;
        }
        tmp.ranges = buf_ranges + buf_ranges_n;
        tmp.ranges_count = glyph_ranges_count;
        buf_ranges_n += glyph_ranges_count;
        for (u32 i = 0; i < glyph_ranges_count; i++) {
            auto const in_range = cfg.glyph_ranges[i];
            stbtt_pack_range& range = tmp.ranges[i];
            range.font_size = cfg.size_pixels;
            range.first_unicode_codepoint_in_range = in_range.start;
            range.num_chars = (in_range.end - in_range.start) + 1;
            range.chardata_for_range = buf_packedchars + buf_packedchars_n;
            buf_packedchars_n += (u32)range.num_chars;
        }

        // Pack
        tmp.rects = buf_rects + buf_rects_n;
        buf_rects_n += glyph_count;
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        int const n = stbtt_PackFontRangesGatherRects(&spc,
                                                      &tmp.font_info,
                                                      tmp.ranges,
                                                      (int)tmp.ranges_count,
                                                      tmp.rects);
        stbrp_pack_rects((stbrp_context*)spc.pack_info, tmp.rects, n);

        // Extend texture height
        for (int i = 0; i < n; i++)
            if (tmp.rects[i].was_packed) tex_height = Max(tex_height, tmp.rects[i].y + tmp.rects[i].h);
    }
    ASSERT_EQ(buf_rects_n, total_glyph_count);
    ASSERT_EQ(buf_packedchars_n, total_glyph_count);
    ASSERT_EQ(buf_ranges_n, total_glyph_range_count);

    // Create texture
    tex_height = (int)NextPowerOf2((u32)tex_height);
    tex_pixels_alpha8 = (unsigned char*)GlobalAllocOversizeAllowed({(usize)(tex_width * tex_height)}).data;
    ZeroMemory(tex_pixels_alpha8, (usize)(tex_width * tex_height));
    spc.pixels = tex_pixels_alpha8;
    spc.height = tex_height;

    // Second pass: render characters
    for (u32 input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        stbtt_PackFontRangesRenderIntoRects(&spc,
                                            &tmp.font_info,
                                            tmp.ranges,
                                            (int)tmp.ranges_count,
                                            tmp.rects);
        tmp.rects = nullptr;
    }

    // End packing
    stbtt_PackEnd(&spc);
    GlobalFreeNoSize(buf_rects);
    buf_rects = nullptr;

    // Third pass: setup Font and glyphs for runtime
    for (u32 input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig& cfg = config_data[input_i];
        FontTempBuildData const& tmp = tmp_array[input_i];
        Font* dst_font = cfg.dst_font;

        f32 const font_scale = stbtt_ScaleForPixelHeight(&tmp.font_info, cfg.size_pixels);
        int unscaled_ascent;
        int unscaled_descent;
        int unscaled_line_gap;
        stbtt_GetFontVMetrics(&tmp.font_info, &unscaled_ascent, &unscaled_descent, &unscaled_line_gap);

        f32 const ascent = (f32)unscaled_ascent * font_scale;
        f32 const descent = (f32)unscaled_descent * font_scale;
        if (!cfg.merge_mode) {
            dst_font->container_atlas = this;
            dst_font->config_data = &cfg;
            dst_font->font_size = cfg.size_pixels;
            dst_font->ascent = ascent;
            dst_font->descent = descent;
            dst_font->glyphs.Resize(0);
        }
        f32 const off_y =
            (cfg.merge_mode && cfg.merge_glyph_center_v) ? (ascent - dst_font->ascent) * 0.5f : 0.0f;

        dst_font->fallback_glyph = nullptr; // Always clear fallback so FindGlyph can return null. It will
                                            // be set again in BuildLookupTable()
        for (u32 i = 0; i < tmp.ranges_count; i++) {
            stbtt_pack_range const& range = tmp.ranges[i];
            for (int char_idx = 0; char_idx < range.num_chars; char_idx += 1) {
                stbtt_packedchar const& pc = range.chardata_for_range[char_idx];
                if (!pc.x0 && !pc.x1 && !pc.y0 && !pc.y1) continue;

                int const codepoint = range.first_unicode_codepoint_in_range + char_idx;
                if (cfg.merge_mode && dst_font->FindGlyph((Char16)codepoint)) continue;

                stbtt_aligned_quad q;
                f32 dummy_x = 0.0f;
                f32 dummy_y = 0.0f;
                stbtt_GetPackedQuad(range.chardata_for_range,
                                    tex_width,
                                    tex_height,
                                    char_idx,
                                    &dummy_x,
                                    &dummy_y,
                                    &q,
                                    0);

                dst_font->glyphs.Resize(dst_font->glyphs.size + 1);
                Font::Glyph& glyph = dst_font->glyphs.Back();
                glyph.codepoint = (Char16)codepoint;
                glyph.x0 = q.x0;
                glyph.y0 = q.y0;
                glyph.x1 = q.x1;
                glyph.y1 = q.y1;
                glyph.u0 = q.s0;
                glyph.v0 = q.t0;
                glyph.u1 = q.s1;
                glyph.v1 = q.t1;
                glyph.y0 += (f32)(int)(dst_font->ascent + off_y + 0.5f);
                glyph.y1 += (f32)(int)(dst_font->ascent + off_y + 0.5f);
                glyph.x_advance = (pc.xadvance + cfg.glyph_extra_spacing.x); // Bake spacing into XAdvance
                if (cfg.pixel_snap_h) glyph.x_advance = (f32)(int)(glyph.x_advance + 0.5f);
            }
        }
        cfg.dst_font->BuildLookupTable();
    }

    // Cleanup temporaries
    GlobalFreeNoSize(buf_packedchars);
    GlobalFreeNoSize(buf_ranges);
    GlobalFreeNoSize(tmp_array);

    // Render into our custom data block
    RenderCustomTexData(1, &extra_rects);

    return true;
}

void FontAtlas::RenderCustomTexData(int pass, void* p_rects) {
    // The white texels on the top left are the ones we'll use everywhere in Gui to render filled shapes.
    int const tex_data_w = 2;
    int const tex_data_h = 2;
    char const texture_data[(tex_data_w * tex_data_h) + 1] = {".."
                                                              ".."};

    BasicDynamicArray<stbrp_rect>& rects = *(BasicDynamicArray<stbrp_rect>*)p_rects;
    if (pass == 0) {
        // Request rectangles
        stbrp_rect r;
        ZeroMemory(&r, sizeof(r));
        r.w = (tex_data_w * 2) + 1;
        r.h = tex_data_h + 1;
        rects.PushBack(r);
    } else if (pass == 1) {
        // Render/copy pixels
        stbrp_rect const& r = rects[0];
        for (int y = 0, n = 0; y < tex_data_h; y++)
            for (int x = 0; x < tex_data_w; x++, n++) {
                int const offset0 = (int)(r.x + x) + ((int)(r.y + y) * tex_width);
                int const offset1 = offset0 + 1 + tex_data_w;
                tex_pixels_alpha8[offset0] = texture_data[n] == '.' ? 0xFF : 0x00;
                tex_pixels_alpha8[offset1] = texture_data[n] == 'X' ? 0xFF : 0x00;
            }
        f32x2 const tex_uv_scale {1.0f / (f32)tex_width, 1.0f / (f32)tex_height};
        tex_uv_white_pixel = f32x2 {((f32)r.x + 0.5f) * tex_uv_scale.x, ((f32)r.y + 0.5f) * tex_uv_scale.y};
    }
}

GlyphRanges FontAtlas::GetGlyphRangesDefaultAudioPlugin() {
    GlyphRanges ranges;
    dyn::Assign(ranges,
                Array {
                    GlyphRange {0x0020, 0x00FF}, // Basic Latin + Latin Supplement
                    GlyphRange {0x221E, 0x221E}, // Infinity
                    GlyphRange {0x2019, 0x2019}, // Apostrophe
                    GlyphRange {0x2026, 0x2026}, // Ellipsis
                    GlyphRange {0x203A, 0x203A}, // Single Right-Pointing Angle Quotation Mark
                });
    return ranges;
}

// Retrieve list of range (2 int per range, values are inclusive)
GlyphRanges FontAtlas::GetGlyphRangesDefault() {
    GlyphRanges ranges;
    dyn::Assign(ranges,
                Array {
                    GlyphRange {0x0020, 0x00FF}, // Basic Latin + Latin Supplement
                });
    return ranges;
}

//-----------------------------------------------------------------------------
// Font
//-----------------------------------------------------------------------------

void Font::BuildLookupTable() {
    Char16 max_codepoint = 0;
    for (u32 i = 0; i != glyphs.size; i++)
        max_codepoint = Max(max_codepoint, glyphs[i].codepoint);

    ASSERT(glyphs.size < 0xFFFF);
    index_x_advance.Clear();
    index_lookup.Clear();
    GrowIndex(max_codepoint + 1);
    for (u32 i = 0; i < glyphs.size; i++) {
        auto const codepoint = glyphs[i].codepoint;
        index_x_advance[codepoint] = glyphs[i].x_advance;
        index_lookup[codepoint] = (Char16)i;
    }

    // Create a glyph to handle TAB
    // FIXME: Needs proper TAB handling but it needs to be contextualized (or we could arbitrary say that each
    // string starts at "column 0" ?)
    if (FindGlyph(L' ')) {
        if (glyphs.Back().codepoint != '\t') // So we can call this function multiple times
            glyphs.Resize(glyphs.size + 1);
        Font::Glyph& tab_glyph = glyphs.Back();
        tab_glyph = *FindGlyph(L' ');
        tab_glyph.codepoint = '\t';
        tab_glyph.x_advance *= 4;
        index_x_advance[tab_glyph.codepoint] = tab_glyph.x_advance;
        index_lookup[tab_glyph.codepoint] = (Char16)(glyphs.size - 1);
    }

    fallback_glyph = nullptr;
    fallback_glyph = FindGlyph(k_fallback_char);
    fallback_x_advance = fallback_glyph ? fallback_glyph->x_advance : 0.0f;
    for (Char16 i = 0; i < max_codepoint + 1; i++)
        if (index_x_advance[i] < 0.0f) index_x_advance[i] = fallback_x_advance;
}

void Font::GrowIndex(u32 new_size) {
    ASSERT_EQ(index_x_advance.size, index_lookup.size);
    auto const old_size = index_lookup.size;
    if (new_size <= old_size) return;
    index_x_advance.Resize(new_size);
    index_lookup.Resize(new_size);
    for (auto i = old_size; i < new_size; i++) {
        index_x_advance[i] = -1.0f;
        index_lookup[i] = k_invalid_codepoint;
    }
}

void Font::AddRemapChar(Char16 dst, Char16 src, bool overwrite_dst) {
    ASSERT(index_lookup.size > 0); // Currently this can only be called AFTER the font has been built, aka
                                   // after calling FontAtlas::GetTexDataAs*() function.
    auto const index_size = index_lookup.size;

    if (dst < index_size && index_lookup.data[dst] == k_invalid_codepoint &&
        !overwrite_dst) // 'dst' already exists
        return;
    if (src >= index_size && dst >= index_size) // both 'dst' and 'src' don't exist -> no-op
        return;

    GrowIndex(dst + 1);
    index_lookup[dst] = (src < index_size) ? index_lookup.data[src] : k_invalid_codepoint;
    index_x_advance[dst] = (src < index_size) ? index_x_advance.data[src] : 1.0f;
}

Font::Glyph const* Font::FindGlyph(Char16 c) const {
    if (c < index_lookup.size) {
        auto const i = index_lookup[c];
        if (i != k_invalid_codepoint) return &glyphs.data[i];
    }
    return fallback_glyph;
}

char const*
Font::CalcWordWrapPositionA(f32 scale, char const* text, char const* text_end, f32 wrap_width) const {
    // Simple word-wrapping for English, not full-featured.
    // FIXME: Much possible improvements (don't cut things like "word !", "word!!!" but cut within "word,,,,",
    // more sensible support for punctuations, support for Unicode punctuations, etc.)

    // For references, possible wrap point marked with ^
    //  "aaa bbb, ccc,ddd. eee   fff. ggg!"
    //      ^    ^    ^   ^   ^__    ^    ^

    // List of hardcoded separators: .,;!?'"

    // Skip extra blanks after a line returns (that includes not counting them in width computation)
    // e.g. "Hello    world" --> "Hello" "World"

    // Cut words that cannot possibly fit within one line.
    // e.g.: "The tropical fish" with ~5 characters worth of width --> "The tr" "opical" "fish"

    f32 line_width = 0.0f;
    f32 word_width = 0.0f;
    f32 blank_width = 0.0f;

    char const* word_end = text;
    char const* prev_word_end = nullptr;
    bool inside_word = true;

    char const* s = text;
    while (s < text_end) {
        auto c = (unsigned int)*s;
        char const* next_s;
        if (c < 0x80)
            next_s = s + 1;
        else
            next_s = s + Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
        if (c == 0) break;

        if (c < 32) {
            if (c == '\n') {
                line_width = word_width = blank_width = 0.0f;
                inside_word = true;
                s = next_s;
                continue;
            }
            if (c == '\r') {
                s = next_s;
                continue;
            }
        }

        f32 const char_width = (c < index_x_advance.size ? index_x_advance[c] : fallback_x_advance) * scale;
        if (IsSpaceU32(c)) {
            if (inside_word) {
                line_width += blank_width;
                blank_width = 0.0f;
            }
            blank_width += char_width;
            inside_word = false;
        } else {
            word_width += char_width;
            if (inside_word) {
                word_end = next_s;
            } else {
                prev_word_end = word_end;
                line_width += word_width + blank_width;
                word_width = blank_width = 0.0f;
            }

            // Allow wrapping after punctuation.
            inside_word = !(c == '.' || c == ',' || c == ';' || c == '!' || c == '?' || c == '\"');
        }

        // We ignore blank width at the end of the line (they can be skipped)
        if (line_width + word_width >= wrap_width) {
            // Words that cannot possibly fit within an entire line will be cut anywhere.
            if (word_width < wrap_width) s = prev_word_end ? prev_word_end : word_end;
            break;
        }

        s = next_s;
    }

    return s;
}

f32x2 Font::CalcTextSize(String str, TextSizeOptions const& options) const {
    auto text_begin = str.data;
    auto text_end = End(str);

    auto size = options.font_size == 0 ? font_size : options.font_size;

    f32 const line_height = size;
    f32 const scale = size / font_size;

    auto text_size = f32x2 {0, 0};
    f32 line_width = 0.0f;

    bool const word_wrap_enabled = (options.wrap_width > 0.0f);
    char const* word_wrap_eol = nullptr;

    char const* s = text_begin;
    while (s < text_end) {
        if (word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code
            // simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, options.wrap_width - line_width);
                if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1
                                        // character to minimize the height discontinuity.
                    word_wrap_eol++; // +1 may not be a character start point in UTF-8 but it's ok because we
                                     // use s >= word_wrap_eol below
            }

            if (s >= word_wrap_eol) {
                if (text_size.x < line_width) text_size.x = line_width;
                text_size.y += line_height;
                line_width = 0.0f;
                word_wrap_eol = nullptr;

                // Wrapping skips upcoming blanks
                while (s < text_end) {
                    char const c = *s;
                    if (IsSpaceU32((unsigned)c)) {
                        s++;
                    } else if (c == '\n') {
                        s++;
                        break;
                    } else {
                        break;
                    }
                }
                continue;
            }
        }

        // Decode and advance source
        char const* prev_s = s;
        auto c = (unsigned int)*s;
        if (c < 0x80) {
            s += 1;
        } else {
            s += Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
            if (c == 0) break;
        }

        if (c < 32) {
            if (c == '\n') {
                text_size.x = Max(text_size.x, line_width);
                text_size.y += line_height;
                line_width = 0.0f;
                continue;
            }
            if (c == '\r') continue;
        }

        f32 const char_width = (c < index_x_advance.size ? index_x_advance[c] : fallback_x_advance) * scale;
        if (line_width + char_width >= options.max_width) {
            s = prev_s;
            break;
        }

        line_width += char_width;
    }

    if (text_size.x < line_width) text_size.x = line_width;

    if (line_width > 0 || text_size.y == 0.0f) text_size.y += line_height;

    if (options.remaining) *options.remaining = s;

    return text_size;
}

f32 Font::LargestStringWidth(f32 pad, void* items, int num, String (*GetStr)(void* items, int index)) const {
    f32 result = 0;
    for (auto const i : Range(num)) {
        auto str = GetStr(items, i);
        auto len = CalcTextSize(str, {.font_size = font_size}).x;
        if (len > result) result = len;
    }
    return (f32)(int)(result + (pad * 2));
}

f32 Font::LargestStringWidth(f32 pad, Span<String const> strs) const {
    auto str_get = [](void* items, int index) -> String {
        auto strs = (String const*)items;
        return strs[index];
    };

    return LargestStringWidth(pad, (void*)strs.data, (int)strs.size, str_get);
}

void Font::RenderChar(DrawList* draw_list, f32 size, f32x2 pos, u32 col, Char16 c) const {
    if (c == ' ' || c == '\t' || c == '\n' ||
        c == '\r') // Match behavior of RenderText(), those 4 codepoints are hard-coded.
        return;
    if (Glyph const* glyph = FindGlyph(c)) {
        f32 const scale = (size >= 0.0f) ? (size / font_size) : 1.0f;
        pos.x = (f32)(int)pos.x + display_offset.x;
        pos.y = (f32)(int)pos.y + display_offset.y;
        f32x2 const pos_tl {pos.x + (glyph->x0 * scale), pos.y + (glyph->y0 * scale)};
        f32x2 const pos_br {pos.x + (glyph->x1 * scale), pos.y + (glyph->y1 * scale)};
        draw_list->PrimReserve(6, 4);
        draw_list->PrimRectUV(pos_tl,
                              pos_br,
                              f32x2 {glyph->u0, glyph->v0},
                              f32x2 {glyph->u1, glyph->v1},
                              col);
    }
}

void Font::RenderText(DrawList* draw_list,
                      f32 size,
                      f32x2 pos,
                      u32 col,
                      f32x4 const& clip_rect,
                      String text,
                      f32 wrap_width,
                      bool cpu_fine_clip) const {
    // Align to be pixel perfect
    pos.x = (f32)(int)pos.x + display_offset.x;
    pos.y = (f32)(int)pos.y + display_offset.y;
    f32 x = pos.x;
    f32 y = pos.y;
    if (y > clip_rect.w) return;

    f32 const scale = size / font_size;
    f32 const line_height = font_size * scale;
    bool const word_wrap_enabled = (wrap_width > 0.0f);
    char const* word_wrap_eol = nullptr;

    // Skip non-visible lines
    auto s = text.data;
    auto text_end = text.data + text.size;
    if (!word_wrap_enabled && y + line_height < clip_rect.y)
        while (s < text_end && *s != '\n') // Fast-forward to next line
            s++;

    // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
    auto const vtx_count_max = (u32)(text_end - s) * 4;
    auto const idx_count_max = (u32)(text_end - s) * 6;
    auto const idx_expected_size = draw_list->idx_buffer.size + idx_count_max;
    draw_list->PrimReserve(idx_count_max, vtx_count_max);

    DrawVert* vtx_write = draw_list->vtx_write_ptr;
    DrawIdx* idx_write = draw_list->idx_write_ptr;
    unsigned int vtx_current_idx = draw_list->vtx_current_idx;

    while (s < text_end) {
        if (word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code
            // simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, wrap_width - (x - pos.x));
                if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1
                                        // character to minimize the height discontinuity.
                    word_wrap_eol++; // +1 may not be a character start point in UTF-8 but it's ok because we
                                     // use s >= word_wrap_eol below
            }

            if (s >= word_wrap_eol) {
                x = pos.x;
                y += line_height;
                word_wrap_eol = nullptr;

                // Wrapping skips upcoming blanks
                while (s < text_end) {
                    char const c = *s;
                    if (IsSpaceU32((unsigned)c)) {
                        s++;
                    } else if (c == '\n') {
                        s++;
                        break;
                    } else {
                        break;
                    }
                }
                continue;
            }
        }

        // Decode and advance source
        auto c = (unsigned int)*s;
        if (c < 0x80) {
            s += 1;
        } else {
            s += Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
            if (c == 0) break;
        }

        if (c < 32) {
            if (c == '\n') {
                x = pos.x;
                y += line_height;

                if (y > clip_rect.w) break;
                if (!word_wrap_enabled && y + line_height < clip_rect.y)
                    while (s < text_end && *s != '\n') // Fast-forward to next line
                        s++;
                continue;
            }
            if (c == '\r') continue;
        }

        f32 char_width = 0.0f;
        if (Glyph const* glyph = FindGlyph((Char16)c)) {
            char_width = glyph->x_advance * scale;

            // Arbitrarily assume that both space and tabs are empty glyphs as an optimization
            if (c != ' ' && c != '\t') {
                // We don't do a second finer clipping test on the Y axis as we've already skipped anything
                // before clip_rect.y and exit once we pass clip_rect.w
                f32 x1 = x + (glyph->x0 * scale);
                f32 x2 = x + (glyph->x1 * scale);
                f32 y1 = y + (glyph->y0 * scale);
                f32 y2 = y + (glyph->y1 * scale);
                if (x1 <= clip_rect.z && x2 >= clip_rect.x) {
                    // Render a character
                    f32 u1 = glyph->u0;
                    f32 v1 = glyph->v0;
                    f32 u2 = glyph->u1;
                    f32 v2 = glyph->v1;

                    // CPU side clipping used to fit text in their frame when the frame is too small. Only
                    // does clipping for axis aligned quads.
                    if (cpu_fine_clip) {
                        if (x1 < clip_rect.x) {
                            u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                            x1 = clip_rect.x;
                        }
                        if (y1 < clip_rect.y) {
                            v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                            y1 = clip_rect.y;
                        }
                        if (x2 > clip_rect.z) {
                            u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                            x2 = clip_rect.z;
                        }
                        if (y2 > clip_rect.w) {
                            v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                            y2 = clip_rect.w;
                        }
                        if (y1 >= y2) {
                            x += char_width;
                            continue;
                        }
                    }

                    // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a
                    // debug build. Inlined here:
                    {
                        idx_write[0] = (DrawIdx)(vtx_current_idx);
                        idx_write[1] = (DrawIdx)(vtx_current_idx + 1);
                        idx_write[2] = (DrawIdx)(vtx_current_idx + 2);
                        idx_write[3] = (DrawIdx)(vtx_current_idx);
                        idx_write[4] = (DrawIdx)(vtx_current_idx + 2);
                        idx_write[5] = (DrawIdx)(vtx_current_idx + 3);
                        vtx_write[0].pos.x = x1;
                        vtx_write[0].pos.y = y1;
                        vtx_write[0].col = col;
                        vtx_write[0].uv.x = u1;
                        vtx_write[0].uv.y = v1;
                        vtx_write[1].pos.x = x2;
                        vtx_write[1].pos.y = y1;
                        vtx_write[1].col = col;
                        vtx_write[1].uv.x = u2;
                        vtx_write[1].uv.y = v1;
                        vtx_write[2].pos.x = x2;
                        vtx_write[2].pos.y = y2;
                        vtx_write[2].col = col;
                        vtx_write[2].uv.x = u2;
                        vtx_write[2].uv.y = v2;
                        vtx_write[3].pos.x = x1;
                        vtx_write[3].pos.y = y2;
                        vtx_write[3].col = col;
                        vtx_write[3].uv.x = u1;
                        vtx_write[3].uv.y = v2;
                        vtx_write += 4;
                        vtx_current_idx += 4;
                        idx_write += 6;
                    }
                }
            }
        }

        x += char_width;
    }

    // Give back unused vertices
    draw_list->vtx_buffer.Resize((u32)(vtx_write - draw_list->vtx_buffer.data));
    draw_list->idx_buffer.Resize((u32)(idx_write - draw_list->idx_buffer.data));
    draw_list->cmd_buffer[draw_list->cmd_buffer.size - 1].elem_count -=
        (unsigned)(idx_expected_size - draw_list->idx_buffer.size);
    draw_list->vtx_write_ptr = vtx_write;
    draw_list->idx_write_ptr = idx_write;
    draw_list->vtx_current_idx = (unsigned int)draw_list->vtx_buffer.size;
}
