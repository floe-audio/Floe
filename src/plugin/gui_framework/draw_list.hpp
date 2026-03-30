// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Based on code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "utils/basic_dynamic_array.hpp"

#include "renderer.hpp"

// We ubiquitously use ABGR format colours stored as u32.

enum class TextOverflowType : u8 { AllowOverflow, ShowDotsOnRight, ShowDotsOnLeft, Count };

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

BITWISE_OPERATORS(TextJustification)

struct Font;
struct Fonts;

// 4 bits, clockwise as written when using 0bXXXX syntax:
// 0b1000 = top-left
// 0b0100 = top-right
// 0b0010 = bottom-right
// 0b0001 = bottom-left
using Corners = u4;

// 4 bits, clockwise from left: left, top, right, bottom.
// 0b1000 = left
// 0b0100 = top
// 0b0010 = right
// 0b0001 = bottom
using Edges = u4;

struct AddTextOptions {
    // Wrap to a new line if the text exceeds this width. Works the same as Font's text size calculation
    // wrap_width argument.
    f32 wrap_width = 0.0f;

    // The final font size is the font_size * font_scaling.
    f32 font_scaling = 1;
    f32 font_size = 0; // 0 means use default font size.
};

// Same as AddTextOptions, but with additional Rect-related options.
struct AddTextInRectOptions {
    TextJustification justification = TextJustification::Left;
    TextOverflowType overflow_type = TextOverflowType::AllowOverflow;
    f32 wrap_width = 0.0f; // ONLY valid if overflow_type is AllowOverflow.
    f32 font_scaling = 1;
    f32 font_size = 0;
};

#pragma pack(push, 1)
struct DrawVert {
    f32x2 pos;
    f32x2 uv;
    u32 col;
};
#pragma pack(pop)

using DrawIdx = u16;

struct DrawCmd {
    f32x4 clip_rect {-8192, -8192, 8192, 8192};
    TextureHandle texture_id {};
    // Number of indices (multiple of 3) to be rendered as triangles. Vertices are stored in the callee
    // DrawList's vtx_buffer[] array, indices in idx_buffer[].
    u32 elem_count = 0;
};

struct DrawList {
    //
    // Shapes
    //

    void AddRect(Rect r,
                 u32 colour,
                 f32 rounding = 0.0f,
                 Corners corners_to_round = 0b1111,
                 f32 thickness = 1.0f) {
        AddRect(r.Min(), r.Max(), colour, rounding, corners_to_round, thickness);
    }

    void AddRect(f32x2 top_left,
                 f32x2 bottom_right,
                 u32 colour,
                 f32 rounding = 0.0f,
                 Corners corners_to_round = 0b1111,
                 f32 thickness = 1.0f);

    void AddRectFilled(Rect const r, u32 colour, f32 rounding = 0.0f, Corners corners_to_round = 0b1111) {
        AddRectFilled(r.Min(), r.Max(), colour, rounding, corners_to_round);
    }

    void AddRectFilled(f32x2 top_left,
                       f32x2 bottom_right,
                       u32 colour,
                       f32 rounding = 0.0f,
                       Corners corner_to_round = 0b1111);

    void AddRectFilledMultiColor(f32x2 top_left,
                                 f32x2 bottom_right,
                                 u32 col_top_left,
                                 u32 col_top_right,
                                 u32 col_bot_right,
                                 u32 col_bot_left);

    void AddNonAABox(f32x2 a, f32x2 b, u32 colour, f32 thickness);

    void AddQuad(f32x2 a, f32x2 b, f32x2 c, f32x2 d, u32 col, f32 thickness = 1.0f);

    void AddQuadFilled(f32x2 a, f32x2 b, f32x2 c, f32x2 d, u32 col);

    void AddQuadFilledMultiColor(f32x2 top_left,
                                 f32x2 top_right,
                                 f32x2 bot_right,
                                 f32x2 bot_left,
                                 u32 col_top_left,
                                 u32 col_top_right,
                                 u32 col_bot_right,
                                 u32 col_bot_left);

    void AddTriangle(f32x2 a, f32x2 b, f32x2 c, u32 col, f32 thickness = 1.0f);

    void AddTriangleFilled(f32x2 a, f32x2 b, f32x2 c, u32 col);

    void AddCircle(f32x2 centre, f32 radius, u32 col, u32 num_segments = 12, f32 thickness = 1.0f);

    void AddCircleFilled(f32x2 centre, f32 radius, u32 col, u32 num_segments = 12);

    // Draws a vignette effect: darkening edges that fade toward the centre. colour is the edge colour
    // (typically black with some alpha). inner_radius_fraction: 0-1, how far from centre the darkening
    // starts. subdivisions: grid resolution per axis for smooth radial falloff.
    void AddVignetteRect(Rect r, u32 colour, f32 inner_radius_fraction = 0.3f, u32 subdivisions = 16);

    void AddDropShadow(f32x2 a,
                       f32x2 b,
                       u32 col,
                       f32 blur_size,
                       f32 rounding = 0.0f,
                       Corners corners_to_round = 0b1111) {
        PushClipRectFullScreen();
        auto const aa = renderer.fill_anti_alias;
        renderer.fill_anti_alias = blur_size;
        auto const offs = f32x2 {blur_size} / f32x2 {7.0f, 5.0f};
        AddRectFilled(a + offs, b + offs, col, rounding, corners_to_round);
        renderer.fill_anti_alias = aa;
        PopClipRect();
    }

    void AddLine(f32x2 a, f32x2 b, u32 colour, f32 thickness = 1.0f);

    // Draws inside the rect.
    void AddBorderEdges(Rect r, u32 colour, Edges edges, f32 thickness = 1.0f) {
        f32 const l = r.x + 0.5f;
        f32 const t = r.y + 0.5f;
        f32 const ri = r.x + r.w - thickness + 0.5f;
        f32 const b = r.y + r.h - thickness + 0.5f;
        // Clockwise corners: TL, TR, BR, BL. Each edge connects corner[i] to corner[i+1].
        f32x2 const corners[] = {{l, t}, {ri, t}, {ri, b}, {l, b}};
        Edges const edge_bits[] = {0b0100, 0b0010, 0b0001, 0b1000};
        bool in_path = false;
        for (int i = 0; i < 4; i++) {
            if (edges & edge_bits[i]) {
                if (!in_path) {
                    PathLineTo(corners[i]);
                    in_path = true;
                }
                PathLineTo(corners[(i + 1) % 4]);
            } else if (in_path) {
                PathStroke(colour, false, thickness);
                in_path = false;
            }
        }
        if (in_path) PathStroke(colour, false, thickness);
    }

    void AddPolyline(Span<f32x2 const> points, u32 col, bool closed, f32 thickness, bool anti_aliased);

    void AddConvexPolyFilled(Span<f32x2 const> points, u32 col, bool anti_aliased);

    void AddBezierCurve(f32x2 pos0,
                        f32x2 cp0,
                        f32x2 cp1,
                        f32x2 pos1,
                        u32 col,
                        f32 thickness,
                        u32 num_segments = 0);
    //
    // Text
    //

    // Render text using the current font. The current font is pushed/popped by the user using the Fonts
    // object. This draw list maintains a const reference to the Fonts.

    void AddText(f32x2 pos, u32 col, String str, AddTextOptions const& options = {});

    void AddTextInRect(Rect r, u32 col, String str, AddTextInRectOptions const& options = {});

    //
    // Images
    //

    void AddImageRect(TextureHandle user_texture_id,
                      Rect r,
                      f32x2 uv0 = f32x2 {0, 0},
                      f32x2 uv1 = f32x2 {1, 1},
                      u32 col = 0xFFFFFFFF) {
        AddImage(user_texture_id, r.Min(), r.Max(), uv0, uv1, col);
    }

    void AddImage(TextureHandle user_texture_id,
                  f32x2 top_left,
                  f32x2 bottom_right,
                  f32x2 uv0 = f32x2 {0, 0},
                  f32x2 uv1 = f32x2 {1, 1},
                  u32 col = 0xFFFFFFFF);

    void AddImageRounded(TextureHandle user_texture_id,
                         f32x2 top_left,
                         f32x2 bottom_right,
                         f32x2 uv_min,
                         f32x2 uv_max,
                         u32 col,
                         f32 rounding,
                         Corners corners_to_rounding = 0b1111);

    //
    // Render-level scissoring.
    //

    // This is passed down to the render function but not used for CPU-side coarse clipping.

    void SetClipRect(f32x2 min, f32x2 max);
    void SetClipRect(Rect r) { SetClipRect(r.Min(), r.Max()); }
    void SetClipRectFullscreen();

    void
    PushClipRect(f32x2 clip_rect_min, f32x2 clip_rect_max, bool intersect_with_current_clip_rect = false);
    void PushClipRect(Rect clip_rect, bool intersect_with_current_clip_rect = false) {
        PushClipRect(clip_rect.Min(), clip_rect.Max(), intersect_with_current_clip_rect);
    }
    void PushClipRectFullScreen();
    void PopClipRect();

    void PushTextureHandle(TextureHandle const& texture_id);
    void PopTextureHandle();

    //
    // Stateful path API
    //

    // Add points then finish with PathFill() or PathStroke().

    void PathClear() { path.Resize(0); }

    void PathLineTo(f32x2 pos) { path.PushBack(pos); }

    void PathLineToMergeDuplicate(f32x2 pos) {
        if (path.size == 0 || !MemoryIsEqual(&path[path.size - 1], &pos, 8)) path.PushBack(pos);
    }

    void PathFill(u32 col) {
        AddConvexPolyFilled(path, col, true);
        PathClear();
    }

    void PathStroke(u32 col, bool closed, f32 thickness = 1.0f) {
        AddPolyline(path, col, closed, thickness, true);
        PathClear();
    }

    void PathArcTo(f32x2 centre, f32 radius, f32 a_min, f32 a_max, u32 num_segments = 10);

    // Uses precomputed angles for a 12 steps circle.
    void PathArcToFast(f32x2 centre, f32 radius, u32 a_min_of_12, u32 a_max_of_12);

    void PathBezierCurveTo(f32x2 p1, f32x2 p2, f32x2 p3, u32 num_segments = 0);

    void PathRect(f32x2 rect_min, f32x2 rect_max, f32 rounding = 0.0f, Corners corners_to_round = 0b1111);

    // Channels
    // - Use to simulate layers. By switching channels to can render out-of-order (e.g. submit foreground
    //   primitives before background primitives).
    // - Use to minimize draw calls (e.g. if going back-and-forth between multiple non-overlapping clipping
    //   rectangles, prefer to append into separate channels then merge at the end).
    void ChannelsSplit(u32 channels_count);
    void ChannelsMerge();
    void ChannelsSetCurrent(u32 channel_index);

    //
    // Internal helpers.
    //
    // NOTE: all primitives needs to be reserved via PrimReserve() beforehand.

    // This is useful if you need to forcefully create a new draw call (to allow for dependent
    // rendering/blending). Otherwise, primitives are merged into the same draw-call as much as possible.
    void AddDrawCmd();
    void Clear();
    void ClearFreeMemory();
    void PrimReserve(u32 idx_count, u32 vtx_count);
    void PrimRect(f32x2 a, f32x2 b,
                  u32 col); // Axis aligned rectangle (composed of two triangles)
    void PrimRectUV(f32x2 a, f32x2 b, f32x2 uv_a, f32x2 uv_b, u32 col);
    void
    PrimQuadUV(f32x2 a, f32x2 b, f32x2 c, f32x2 d, f32x2 uv_a, f32x2 uv_b, f32x2 uv_c, f32x2 uv_d, u32 col);
    void PrimWriteVtx(f32x2 pos, f32x2 uv, u32 col) {
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
    void PrimVtx(f32x2 pos, f32x2 uv, u32 col) {
        PrimWriteIdx((DrawIdx)vtx_current_idx);
        PrimWriteVtx(pos, uv, col);
    }
    void PathFillConvex(u32 col) { PathFill(col); }

    static void ShadeVertsLinearColorGradientSetAlpha(DrawList* draw_list,
                                                      u32 vert_start_idx,
                                                      u32 vert_end_idx,
                                                      f32x2 gradient_p0,
                                                      f32x2 gradient_p1,
                                                      u32 col0,
                                                      u32 col1);

    void UpdateClipRect();
    void UpdateTexturePtr();

    //
    // Setup and frames
    //

    DrawList(Renderer& renderer, Fonts const& fonts) : renderer(renderer), fonts(fonts) { Clear(); }
    ~DrawList() { ClearFreeMemory(); }
    void BeginDraw() {
        Clear();
        PushClipRectFullScreen();
        PushTextureHandle(renderer.font_texture);
    }
    void EndDraw() {
        PopTextureHandle();
        PopClipRect();
    }

    //
    //
    //
    struct DrawChannel {
        using MemcpySafe = void; // Safe: members are just {pointer, size, capacity}
        BasicDynamicArray<DrawCmd> cmd_buffer;
        BasicDynamicArray<DrawIdx> idx_buffer;
    };

    Renderer& renderer;
    Fonts const& fonts;

    BasicDynamicArray<DrawCmd> cmd_buffer;
    BasicDynamicArray<DrawIdx> idx_buffer;
    BasicDynamicArray<DrawVert> vtx_buffer;

    unsigned int vtx_current_idx;

    // Helpers to avoid using the Vector<> operators too much
    DrawVert* vtx_write_ptr;
    DrawIdx* idx_write_ptr;

    BasicDynamicArray<f32x4> clip_rect_stack;
    BasicDynamicArray<TextureHandle> texture_id_stack;
    BasicDynamicArray<f32x2> path;

    u32 channels_current;
    u32 channels_count;
    BasicDynamicArray<DrawChannel> channels;
};

struct DrawListAllocator {
    void Clear() {
        lists.Clear();
        arena.FreeAll();
        lists = {}; // Arena is freed, we must not retain pointers into it.
    }

    DrawList* Allocate(Renderer& r, Fonts const& f) { return lists.Prepend(arena, r, f); }

    ArenaAllocator arena {PageAllocator::Instance()};
    ArenaList<DrawList> lists {};
};

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
