// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

// TODO: replace corner_rounding_flags with a bitfield probably

enum class TextOverflowType : u8 { AllowOverflow, ShowDotsOnRight, ShowDotsOnLeft, Count };

// TODO: use bitfields, one for x, one for y
enum class TextJustification {
    Left = 1,
    Right = 2,
    VerticallyCentred = 4,
    HorizontallyCentred = 8,
    Top = 16,
    Bottom = 32,
    Baseline = 64,
    Centred = VerticallyCentred | HorizontallyCentred,
    CentredLeft = Left | VerticallyCentred,
    CentredRight = Right | VerticallyCentred,
    CentredTop = Top | HorizontallyCentred,
    CentredBottom = Bottom | HorizontallyCentred,
    TopLeft = Top | Left,
    TopRight = Top | Right,
    BottomLeft = Bottom | Left,
    BottomRight = Bottom | Right,
};

inline TextJustification operator|(TextJustification a, TextJustification b) {
    return (TextJustification)(ToInt(a) | ToInt(b));
}
inline int operator&(TextJustification a, TextJustification b) { return ToInt(a) & ToInt(b); }
inline TextJustification& operator|=(TextJustification& a, TextJustification b) { return a = a | b; }

namespace graphics {

using DrawIdx = unsigned short;
using TextureHandle = void*;
using Char16 = u16;

struct ImageID {
    bool operator==(ImageID const& other) const = default;
    u64 id;
    UiSize size;
};

constexpr ImageID k_invalid_image_id = {0, {}};

// TODO: should we use DynamicArray?
template <typename T>
struct Vector {
    using ValueType = T;
    using Iterator = ValueType*;
    using ConstIterator = ValueType const*;

    Vector() {
        size = capacity = 0;
        data = nullptr;
    }
    ~Vector() {
        if (data) GlobalFreeNoSize(data);
    }

    bool Empty() const { return size == 0; }
    int Size() const { return size; }
    int Capacity() const { return capacity; }

    ValueType& operator[](int i) {
        ASSERT(i < size);
        return data[i];
    }
    ValueType const& operator[](int i) const {
        ASSERT(i < size);
        return data[i];
    }

    void Clear() {
        if (data) {
            size = capacity = 0;
            GlobalFreeNoSize(data);
            data = nullptr;
        }
    }
    Iterator begin() { return data; }
    ConstIterator begin() const { return data; }
    Iterator end() { return data + size; }
    ConstIterator end() const { return data + size; }
    ValueType& Front() {
        ASSERT(size > 0);
        return data[0];
    }
    ValueType const& Front() const {
        ASSERT(size > 0);
        return data[0];
    }
    ValueType& Back() {
        ASSERT(size > 0);
        return data[size - 1];
    }
    ValueType const& Back() const {
        ASSERT(size > 0);
        return data[size - 1];
    }

    int GrowCapacity(int new_size) {
        int const new_capacity = capacity ? (capacity + (capacity / 2)) : 8;
        return new_capacity > new_size ? new_capacity : new_size;
    }

    void Resize(int new_size) {
        if (new_size > capacity) Reserve(GrowCapacity(new_size));
        size = new_size;
    }
    void Reserve(int new_capacity) {
        if (new_capacity <= capacity) return;
        auto* new_data = (ValueType*)GlobalAlloc({.size = (usize)new_capacity * sizeof(ValueType)}).data;
        if (data) {
            CopyMemory(new_data, data, (usize)size * sizeof(ValueType));
            GlobalFreeNoSize(data);
        }
        data = new_data;
        capacity = new_capacity;
    }

    void PushBack(ValueType const& v) {
        if (size == capacity) Reserve(GrowCapacity(size + 1));
        data[size++] = v;
    }
    void PopBack() {
        ASSERT(size > 0);
        size--;
    }

    int size;
    int capacity;
    T* data;
};

struct DrawVert {
    f32x2 pos;
    f32x2 uv;
    u32 col;
};

struct DrawCmd;
struct DrawList;

using DrawCallback = void (*)(DrawList const*, DrawCmd const*);

struct DrawCmd {
    // Number of indices (multiple of 3) to be rendered as triangles. Vertices are stored in the callee
    // DrawList's vtx_buffer[] array, indices in idx_buffer[].
    unsigned int elem_count = 0;

    f32x4 clip_rect {-8192, -8192, 8192, 8192};
    TextureHandle texture_id {};
    DrawCallback user_callback {};
    void* user_callback_data {};
};

struct DrawChannel {
    Vector<DrawCmd> cmd_buffer;
    Vector<DrawIdx> idx_buffer;
};

struct Font;

struct GlyphRange {
    Char16 start;
    Char16 end; // inclusive
};
using GlyphRanges = DynamicArrayBounded<GlyphRange, 10>;

struct FontConfig {
    bool font_data_reference_only = false;
    void* font_data = nullptr;
    int font_data_size = 0;
    bool font_data_owned_by_atlas = false;
    int font_no = 0; // Index of font within TTF file
    f32 size_pixels = 0.0f; // Size in pixels for rasterizer

    // Rasterize at higher quality for sub-pixel positioning. We don't use sub-pixel
    int oversample_h = 3;
    int oversample_v = 1;

    // Align every glyph to pixel boundary. Useful e.g. if you are merging a non-pixel aligned font with the
    // default font. If enabled, you can set OversampleH/V to 1.
    bool pixel_snap_h = false;
    f32x2 glyph_extra_spacing = {}; // Extra spacing (in pixels) between glyphs
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

    Font* AddFont(FontConfig const* font_cfg);
    Font* AddFontFromMemoryTTF(void* ttf_data,
                               int ttf_size,
                               f32 size_pixels,
                               FontConfig const* font_cfg = nullptr,
                               Span<GlyphRange const> glyph_ranges = {});

    // Clear the CPU-side texture data. Saves RAM once the texture has been copied to graphics memory.
    void ClearTexData();
    void ClearInputData(); // Clear the input TTF data (inc sizes, glyph ranges)
    void ClearFonts(); // Clear the Gui-side font data (glyphs storage, UV coordinates)
    void Clear(); // Clear all

    // Retrieve texture data
    // User is in charge of copying the pixels into graphics memory, then call SetTextureUserID() After
    // loading the texture into your graphic system, store your texture handle in 'TexID' (ignore if you
    // aren't using multiple fonts nor images) RGBA32 format is provided for convenience and high
    // compatibility, but note that all RGB pixels are white, so 75% of the memory is wasted. Pitch = Width *
    // BytesPerPixels
    void GetTexDataAsAlpha8(unsigned char** out_pixels,
                            int* out_width,
                            int* out_height,
                            int* out_bytes_per_pixel = nullptr); // 1 byte per-pixel
    void GetTexDataAsRGBA32(unsigned char** out_pixels,
                            int* out_width,
                            int* out_height,
                            int* out_bytes_per_pixel = nullptr); // 4 bytes-per-pixel
    void SetTexID(void* id) { tex_id = id; }

    static GlyphRanges GetGlyphRangesDefault(); // Basic Latin, Extended Latin
    static GlyphRanges GetGlyphRangesDefaultAudioPlugin();

    // (Access texture data via GetTexData*() calls which will setup a default font for you.)
    // User data to refer to the texture once it has been uploaded to user's graphic systems. It ia passed
    // back to you during rendering.
    void* tex_id = nullptr;

    // 1 component per pixel, each component is unsigned 8-bit. Total size = TexWidth * TexHeight
    unsigned char* tex_pixels_alpha8 = nullptr;

    // 4 component per pixel, each component is unsigned 8-bit. Total size = TexWidth * TexHeight * 4
    unsigned int* tex_pixels_rgb_a32 = nullptr;

    int tex_width = 0;
    int tex_height = 0;

    // Texture width desired by user before Build(). Must be a power-of-two. If have many glyphs your graphics
    // API have texture size restrictions you may want to increase texture width to decrease height.
    int tex_desired_width = 0;
    f32x2 tex_uv_white_pixel = {}; // Texture coordinates to a white pixel
    Vector<Font*> fonts = {};

    // Private
    Vector<FontConfig> config_data = {};
    bool Build(); // Build pixels data. This is automatically called by the GetTexData*** functions.
    void RenderCustomTexData(int pass, void* rects);
};

// Font runtime data and rendering
// FontAtlas automatically loads a default embedded font for you when you call GetTexDataAsAlpha8() or
// GetTexDataAsRGBA32().
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
        return ((int)c < index_x_advance.size) ? index_x_advance[(int)c] : fallback_x_advance;
    }
    bool IsLoaded() const { return container_atlas != nullptr; }

    // 'max_width' stops rendering after a certain width (could be turned into a 2d size). FLT_MAX to disable.
    // 'wrap_width' enable automatic word-wrapping across multiple lines to fit into given width. 0.0f to
    // disable.
    f32x2 CalcTextSizeA(f32 size,
                        f32 max_width,
                        f32 wrap_width,
                        String str,
                        char const** remaining = nullptr) const; // utf8

    char const*
    CalcWordWrapPositionA(f32 scale, char const* text, char const* text_end, f32 wrap_width) const;
    void RenderChar(DrawList* draw_list, f32 size, f32x2 pos, u32 col, Char16 c) const;
    void RenderText(DrawList* draw_list,
                    f32 size,
                    f32x2 pos,
                    u32 col,
                    f32x4 const& clip_rect,
                    char const* text_begin,
                    char const* text_end,
                    f32 wrap_width = 0.0f,
                    bool cpu_fine_clip = false) const;

    // Private
    void GrowIndex(int new_size);

    // Makes 'dst' character/glyph points to 'src' character/glyph. Currently needs to be called AFTER fonts
    // have been built.
    void AddRemapChar(Char16 dst, Char16 src, bool overwrite_dst = true);

    static constexpr Char16 k_fallback_char = L'?';
    static constexpr Char16 k_invalid_codepoint = (Char16)-1;

    f32 font_size {};

    f32x2 display_offset {0, 1};
    Vector<Glyph> glyphs {};

    // Sparse. Glyphs->XAdvance in a directly indexable way (more cache-friendly, for CalcTextSize functions
    // which are often bottleneck in large UI).
    Vector<f32> index_x_advance {};

    // Sparse. Index glyphs by Unicode code-point.
    Vector<Char16> index_lookup {};

    Glyph const* fallback_glyph {};
    f32 fallback_x_advance {};

    FontConfig* config_data {}; // Pointer within ContainerAtlas->ConfigData
    FontAtlas* container_atlas {}; // What we has been loaded into
    f32 ascent {}; // Ascent: distance from top to bottom of e.g. 'A' [0..FontSize]
    f32 descent {};
};

struct DrawList;

struct DrawData {
    Span<DrawList*> draw_lists;
    int total_vtx_count; // For convenience, sum of all draw_lists vtx_buffer.size
    int total_idx_count; // For convenience, sum of all draw_lists idx_buffer.size
};

struct DrawContext {
    virtual ~DrawContext() {}
    virtual ErrorCodeOr<void> CreateDeviceObjects(void* hwnd) = 0;
    virtual void DestroyDeviceObjects() = 0;

    virtual ErrorCodeOr<void> CreateFontTexture() = 0;
    virtual void DestroyFontTexture() = 0;

    // Never store a TextureHandle longer than a single frame. It can become invalidated between frames.
    virtual ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) = 0;
    virtual void DestroyTexture(TextureHandle& id) = 0;

    virtual ErrorCodeOr<void> Render(DrawData draw_data, UiSize window_size) = 0;

    void
    RequestScreenshotImage(TrivialFixedSizeFunction<8, void(u8 const*, int width, int height)>&& callback) {
        screenshot_callback = Move(callback);
    }

    static void ScaleClipRects(DrawData draw_data, f32 display_ratio);

    // void SetCurrentFont(Font *font); // use push pop instead
    void PushFont(Font* font);
    void PopFont();

    Font* CurrentFont() {
        if (font_stack.Empty()) return nullptr;
        return font_stack.Back();
    }
    f32 CurrentFontSize() {
        if (font_stack.Empty()) return 0;
        return font_stack.Back()->font_size;
    }

    // TextureHandles can be invalidated between frames, use this method to check if your ID still has a
    // corresponding texture.
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
        ASSERT(tex != nullptr);
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
            ASSERT(i.texture != nullptr);
            DestroyTexture(i.texture);
        }
        dyn::Clear(textures);
    }

    Vector<Font*> font_stack;
    u64 image_id_counter {1};

    struct IdAndTexture {
        ImageID id;
        TextureHandle texture;
    };

    DynamicArrayBounded<char, 3000> graphics_device_info {};

    DynamicArray<IdAndTexture> textures {Malloc::Instance()};

    TrivialFixedSizeFunction<8, void(u8 const*, int width, int height)> screenshot_callback {};

    bool anti_aliased_lines = true;
    bool anti_aliased_shapes = true;
    f32 curve_tessellation_tol = 1.25f; // increase for better quality
    f32 fill_anti_alias = 1.0f;
    f32 stroke_anti_alias = 1.0f;
    FontAtlas fonts; // Load and assemble one or more fonts into a single tightly packed texture. Output to
                     // Fonts array.
};

// Defined ether draw_list_opengl or draw_list_directx, call delete on result
DrawContext* CreateNewDrawContext();

struct OverflowTextArgs {
    Font* font;
    f32 font_size;
    Rect r;
    String str;
    TextOverflowType overflow_type;
    f32 font_scaling;
    Optional<f32x2> text_size;
    Allocator& allocator;
    f32x2& text_pos;
};

String OverflowText(OverflowTextArgs const& args);

struct DrawList {
    DrawList() {
        owner_name = nullptr;
        Clear();
    }
    ~DrawList() { ClearFreeMemory(); }
    void BeginDraw() {
        Clear();
        PushClipRectFullScreen();
        PushTextureHandle(context->fonts.tex_id); // IMPROVE: better font system
    }
    void EndDraw() {
        PopTextureHandle();
        PopClipRect();
    }

    void SetClipRect(f32x2 cr_min, f32x2 cr_max);
    void SetClipRectFullscreen();

    // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse
    // clipping. Prefer using higher-level Gui::PushClipRect() to affect logic (hit-testing and widget
    // culling)
    void
    PushClipRect(f32x2 clip_rect_min, f32x2 clip_rect_max, bool intersect_with_current_clip_rect = false);
    void PushClipRectFullScreen();
    void PopClipRect();
    void PushTextureHandle(TextureHandle const& texture_id);
    void PopTextureHandle();

    // Primitives
    void AddNonAABox(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness);

    void AddLine(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness = 1.0f);
    void AddRect(f32x2 const& a,
                 f32x2 const& b,
                 u32 col,
                 f32 rounding = 0.0f,
                 int rounding_corners_flags = ~0,
                 f32 thickness = 1.0f); // a: upper-left, b: lower-right, rounding_corners_flags: 4-bits
                                        // corresponding to which corner to round: 0b0001 bottom-left, 0b0010
                                        // bottom-right, 0b0100 top-right, 0b1000 top-left

    void
    AddRect(Rect r, u32 col, f32 rounding = 0.0f, int rounding_corners_flags = ~0, f32 thickness = 1.0f) {
        AddRect(r.Min(), r.Max(), col, rounding, rounding_corners_flags, thickness);
    }

    void AddRectFilled(f32x2 const& a,
                       f32x2 const& b,
                       u32 col,
                       f32 rounding = 0.0f,
                       int rounding_corners_flags = ~0); // a: upper-left, b: lower-right
    void AddRectFilled(Rect const r, u32 col, f32 rounding = 0.0f, int rounding_corner_flags = ~0) {
        AddRectFilled(r.Min(), r.Max(), col, rounding, rounding_corner_flags);
    }

    void AddRectFilledMultiColor(f32x2 const& a,
                                 f32x2 const& b,
                                 u32 col_upr_left,
                                 u32 col_upr_right,
                                 u32 col_bot_right,
                                 u32 col_bot_left);

    void AddQuadFilledMultiColor(f32x2 const& upr_left,
                                 f32x2 const& upr_right,
                                 f32x2 const& bot_right,
                                 f32x2 const& bot_left,
                                 u32 col_upr_left,
                                 u32 col_upr_right,
                                 u32 col_bot_right,
                                 u32 col_bot_left);

    void
    AddQuad(f32x2 const& a, f32x2 const& b, f32x2 const& c, f32x2 const& d, u32 col, f32 thickness = 1.0f);
    void AddQuadFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, f32x2 const& d, u32 col);
    void AddTriangle(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col, f32 thickness = 1.0f);
    void AddTriangleFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col);
    void AddCircle(f32x2 const& centre, f32 radius, u32 col, int num_segments = 12, f32 thickness = 1.0f);
    void AddCircleFilled(f32x2 const& centre, f32 radius, u32 col, int num_segments = 12);

    void AddDropShadow(f32x2 const& a,
                       f32x2 const& b,
                       u32 col,
                       f32 blur_size,
                       f32 rounding = 0.0f,
                       int rounding_corners_flags = ~0) {
        PushClipRectFullScreen();
        auto const aa = context->fill_anti_alias;
        context->fill_anti_alias = blur_size;
        auto const offs = f32x2 {blur_size} / f32x2 {7.0f, 5.0f};
        AddRectFilled(a + offs, b + offs, col, rounding, rounding_corners_flags);
        context->fill_anti_alias = aa;
        PopClipRect();
    }

    void AddText(f32x2 const& pos, u32 col, String str);
    void AddText(Font const* font,
                 f32 font_size,
                 f32x2 const& pos,
                 u32 col,
                 String str,
                 f32 wrap_width = 0.0f,
                 f32x4 const* cpu_fine_clip_rect = nullptr);

    void AddTextJustified(Rect r,
                          String str,
                          u32 col,
                          TextJustification justification,
                          TextOverflowType overflow_type = TextOverflowType::AllowOverflow,
                          f32 font_scaling = 1);

    void AddImage(TextureHandle user_texture_id,
                  f32x2 const& a,
                  f32x2 const& b,
                  f32x2 const& uv0 = f32x2 {0, 0},
                  f32x2 const& uv1 = f32x2 {1, 1},
                  u32 col = 0xFFFFFFFF);

    void AddImageRounded(TextureHandle user_texture_id,
                         f32x2 const& p_min,
                         f32x2 const& p_max,
                         f32x2 const& uv_min,
                         f32x2 const& uv_max,
                         u32 col,
                         f32 rounding,
                         int corner_flags = ~0);

    void AddPolyline(f32x2 const* points,
                     int const num_points,
                     u32 col,
                     bool closed,
                     f32 thickness,
                     bool anti_aliased);
    void AddConvexPolyFilled(f32x2 const* points, int const num_points, u32 col, bool anti_aliased);
    void AddBezierCurve(f32x2 const& pos0,
                        f32x2 const& cp0,
                        f32x2 const& cp1,
                        f32x2 const& pos1,
                        u32 col,
                        f32 thickness,
                        int num_segments = 0);

    // Stateful path API, add points then finish with PathFill() or PathStroke()
    void PathClear() { path.Resize(0); }
    void PathLineTo(f32x2 const& pos) { path.PushBack(pos); }
    void PathLineToMergeDuplicate(f32x2 const& pos) {
        if (path.size == 0 || !MemoryIsEqual(&path[path.size - 1], &pos, 8)) path.PushBack(pos);
    }
    void PathFill(u32 col) {
        AddConvexPolyFilled(path.data, path.size, col, true);
        PathClear();
    }
    void PathStroke(u32 col, bool closed, f32 thickness = 1.0f) {
        AddPolyline(path.data, path.size, col, closed, thickness, true);
        PathClear();
    }
    void PathArcTo(f32x2 const& centre, f32 radius, f32 a_min, f32 a_max, int num_segments = 10);
    void PathArcToFast(f32x2 const& centre,
                       f32 radius,
                       int a_min_of_12,
                       int a_max_of_12); // Use precomputed angles for a 12 steps circle
    void PathBezierCurveTo(f32x2 const& p1, f32x2 const& p2, f32x2 const& p3, int num_segments = 0);
    void PathRect(f32x2 const& rect_min,
                  f32x2 const& rect_max,
                  f32 rounding = 0.0f,
                  int rounding_corners_flags =
                      ~0); // rounding_corners_flags: 4-bits corresponding to which corner to round

    // Channels
    // - Use to simulate layers. By switching channels to can render out-of-order (e.g. submit foreground
    // primitives before background primitives)
    // - Use to minimize draw calls (e.g. if going back-and-forth between multiple non-overlapping clipping
    // rectangles, prefer to append into separate channels then merge at the end)
    void ChannelsSplit(int channels_count);
    void ChannelsMerge();
    void ChannelsSetCurrent(int channel_index);

    // Advanced

    // Your rendering function must check for 'UserCallback' in DrawCmd and call the function instead of
    // rendering triangles.
    void AddCallback(DrawCallback callback, void* callback_data);

    // This is useful if you need to forcefully create a new draw call (to allow for dependent rendering /
    // blending). Otherwise primitives are merged into the same draw-call as much as possible
    void AddDrawCmd();

    // Internal helpers
    // NB: all primitives needs to be reserved via PrimReserve() beforehand!
    void Clear();
    void ClearFreeMemory();
    void PrimReserve(int idx_count, int vtx_count);
    void PrimRect(f32x2 const& a,
                  f32x2 const& b,
                  u32 col); // Axis aligned rectangle (composed of two triangles)
    void PrimRectUV(f32x2 const& a, f32x2 const& b, f32x2 const& uv_a, f32x2 const& uv_b, u32 col);
    void PrimQuadUV(f32x2 const& a,
                    f32x2 const& b,
                    f32x2 const& c,
                    f32x2 const& d,
                    f32x2 const& uv_a,
                    f32x2 const& uv_b,
                    f32x2 const& uv_c,
                    f32x2 const& uv_d,
                    u32 col);
    void PrimWriteVtx(f32x2 const& pos, f32x2 const& uv, u32 col) {
        vtx_write_ptr->pos = pos;
        vtx_write_ptr->uv = uv;
        vtx_write_ptr->col = col;
        vtx_write_ptr++;
        vtx_current_idx++;
    }
    void PrimWriteIdx(DrawIdx idx) {
        *idx_write_ptr = idx;
        idx_write_ptr++;
    }
    void PrimVtx(f32x2 const& pos, f32x2 const& uv, u32 col) {
        PrimWriteIdx((DrawIdx)vtx_current_idx);
        PrimWriteVtx(pos, uv, col);
    }
    void PathFillConvex(u32 col) { PathFill(col); }

    static void ShadeVertsLinearColorGradientSetAlpha(DrawList* draw_list,
                                                      int vert_start_idx,
                                                      int vert_end_idx,
                                                      f32x2 gradient_p0,
                                                      f32x2 gradient_p1,
                                                      u32 col0,
                                                      u32 col1);

    void UpdateClipRect();
    void UpdateTexturePtr();

    DrawContext* context;

    // This is what you have to render
    Vector<DrawCmd> cmd_buffer; // Commands. Typically 1 command = 1 gpu draw call.
    Vector<DrawIdx> idx_buffer; // Index buffer. Each command consume DrawCmd::ElemCount of those
    Vector<DrawVert> vtx_buffer; // Vertex buffer.

    // [Internal, used while building lists]
    char const* owner_name; // Pointer to owner window's name (if any) for debugging
    unsigned int vtx_current_idx; // [Internal] == vtx_buffer.Size

    // [Internal] point within VtxBuffer.Data after each add command (to avoid using the Vector<> operators
    // too much)
    DrawVert* vtx_write_ptr;

    // [Internal] point within IdxBuffer.Data after each add command (to avoid using the Vector<> operators
    // too much)
    DrawIdx* idx_write_ptr;

    Vector<f32x4> clip_rect_stack;
    Vector<TextureHandle> texture_id_stack;
    Vector<f32x2> path; // [Internal] current path building
                        //
    int channels_current;
    int channels_count;

    // [Internal] draw channels for columns API (not resized down so _ChannelsCount may be smaller than
    // _Channels.Size)
    Vector<DrawChannel> channels;
};

} // namespace graphics
