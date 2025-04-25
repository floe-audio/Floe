// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "draw_list.hpp"

#include <float.h>

#include "foundation/foundation.hpp"

namespace graphics {

constexpr u32 k_max_u16_codepoint = 0xFFFF;

static inline f32 InvLength(f32x2 const& lhs, f32 fail_value) {
    f32 const d = (lhs.x * lhs.x) + (lhs.y * lhs.y);
    if (d > 0.0f) return 1.0f / Sqrt(d);
    return fail_value;
}

void DrawContext::ScaleClipRects(DrawData draw_data, f32 display_ratio) {
    for (auto const& list : draw_data.draw_lists) {
        for (int i = 0; i < list->cmd_buffer.size; i++) {
            auto& cmd = list->cmd_buffer[i];
            cmd.clip_rect = f32x4 {cmd.clip_rect.x * display_ratio,
                                   cmd.clip_rect.y * display_ratio,
                                   cmd.clip_rect.z * display_ratio,
                                   cmd.clip_rect.w * display_ratio};
        }
    }
}

void DrawContext::PushFont(Font* font) {
    ASSERT(font != nullptr);
    font_stack.PushBack(font);
}

void DrawContext::PopFont() { font_stack.PopBack(); }

constexpr f32x4 k_null_clip_rect {-8192.0f,
                                  -8192.0f,
                                  +8192.0f,
                                  +8192.0f}; // Large values that are easy to encode in a few bits+shift

void DrawList::Clear() {
    cmd_buffer.Resize(0);
    idx_buffer.Resize(0);
    vtx_buffer.Resize(0);
    vtx_current_idx = 0;
    vtx_write_ptr = nullptr;
    idx_write_ptr = nullptr;
    clip_rect_stack.Resize(0);
    texture_id_stack.Resize(0);
    path.Resize(0);
    channels_current = 0;
    channels_count = 1;
    // NB: Do not clear channels so our allocations are re-used after the first frame.
}

void DrawList::ClearFreeMemory() {
    cmd_buffer.Clear();
    idx_buffer.Clear();
    vtx_buffer.Clear();
    vtx_current_idx = 0;
    vtx_write_ptr = nullptr;
    idx_write_ptr = nullptr;
    clip_rect_stack.Clear();
    texture_id_stack.Clear();
    path.Clear();
    channels_current = 0;
    channels_count = 1;
    for (int i = 0; i < channels.size; i++) {
        if (i == 0)
            ZeroMemory(
                &channels[0],
                sizeof(channels[0])); // channel 0 is a copy of CmdBuffer/IdxBuffer, don't destruct again
        channels[i].cmd_buffer.Clear();
        channels[i].idx_buffer.Clear();
    }
    channels.Clear();
}

ALWAYS_INLINE f32x4 GetCurrentClipRect(DrawList const& l) {
    return l.clip_rect_stack.size ? l.clip_rect_stack.data[l.clip_rect_stack.size - 1] : k_null_clip_rect;
}

ALWAYS_INLINE TextureHandle GetCurrentTextureId(DrawList const& l) {
    return l.texture_id_stack.size ? l.texture_id_stack.data[l.texture_id_stack.size - 1] : nullptr;
}

void DrawList::AddDrawCmd() {
    DrawCmd draw_cmd;
    draw_cmd.clip_rect = GetCurrentClipRect(*this);
    draw_cmd.texture_id = GetCurrentTextureId(*this);

    ASSERT(draw_cmd.clip_rect.x <= draw_cmd.clip_rect.z && draw_cmd.clip_rect.y <= draw_cmd.clip_rect.w);
    cmd_buffer.PushBack(draw_cmd);
}

void DrawList::AddCallback(DrawCallback callback, void* callback_data) {
    DrawCmd* current_cmd = cmd_buffer.size ? &cmd_buffer.Back() : nullptr;
    if (!current_cmd || current_cmd->elem_count != 0 || current_cmd->user_callback != nullptr) {
        AddDrawCmd();
        current_cmd = &cmd_buffer.Back();
    }
    current_cmd->user_callback = callback;
    current_cmd->user_callback_data = callback_data;

    AddDrawCmd(); // Force a new command after us (see comment below)
}

// Our scheme may appears a bit unusual, basically we want the most-common calls AddLine AddRect etc. to not
// have to perform any check so we always have a command ready in the stack. The cost of figuring out if a new
// command has to be added or if we can merge is paid in those Update** functions only.
void DrawList::UpdateClipRect() {
    // If current command is used with different settings we need to add a new command
    f32x4 const curr_clip_rect = GetCurrentClipRect(*this);
    DrawCmd* curr_cmd = cmd_buffer.size > 0 ? &cmd_buffer.data[cmd_buffer.size - 1] : nullptr;
    if (!curr_cmd ||
        (curr_cmd->elem_count != 0 && !MemoryIsEqual(&curr_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4))) ||
        curr_cmd->user_callback != nullptr) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    if (curr_cmd->elem_count == 0 && prev_cmd &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)) &&
        prev_cmd->texture_id == GetCurrentTextureId(*this) && prev_cmd->user_callback == nullptr)
        cmd_buffer.PopBack();
    else
        curr_cmd->clip_rect = curr_clip_rect;
}

void DrawList::UpdateTexturePtr() {
    // If current command is used with different settings we need to add a new command
    TextureHandle const curr_texture_id = GetCurrentTextureId(*this);
    DrawCmd* curr_cmd = cmd_buffer.size ? &cmd_buffer.Back() : nullptr;
    if (!curr_cmd || (curr_cmd->elem_count != 0 && curr_cmd->texture_id != curr_texture_id) ||
        curr_cmd->user_callback != nullptr) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    auto const curr_clip_rect = GetCurrentClipRect(*this);
    if (prev_cmd && prev_cmd->texture_id == curr_texture_id &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)) &&
        prev_cmd->user_callback == nullptr)
        cmd_buffer.PopBack();
    else
        curr_cmd->texture_id = curr_texture_id;
}

// Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse
// clipping. Prefer using higher-level Gui::PushClipRect() to affect logic (hit-testing and widget culling)
void DrawList::PushClipRect(f32x2 cr_min, f32x2 cr_max, bool intersect_with_current_clip_rect) {
    f32x4 cr {cr_min.x, cr_min.y, cr_max.x, cr_max.y};
    if (intersect_with_current_clip_rect && clip_rect_stack.size) {
        f32x4 const current = clip_rect_stack.data[clip_rect_stack.size - 1];
        if (cr.x < current.x) cr.x = current.x;
        if (cr.y < current.y) cr.y = current.y;
        if (cr.z > current.z) cr.z = current.z;
        if (cr.w > current.w) cr.w = current.w;
    }
    cr.z = Max(cr.x, cr.z);
    cr.w = Max(cr.y, cr.w);

    clip_rect_stack.PushBack(cr);
    UpdateClipRect();
}

void DrawList::SetClipRect(f32x2 cr_min, f32x2 cr_max) {
    f32x4 cr {cr_min.x, cr_min.y, cr_max.x, cr_max.y};
    cr.z = Max(cr.x, cr.z);
    cr.w = Max(cr.y, cr.w);

    auto& rect = clip_rect_stack.Back();
    rect = cr;
    UpdateClipRect();
}

void DrawList::SetClipRectFullscreen() {
    SetClipRect(f32x2 {k_null_clip_rect.x, k_null_clip_rect.y},
                f32x2 {k_null_clip_rect.z, k_null_clip_rect.w});
}

void DrawList::PushClipRectFullScreen() {
    PushClipRect(f32x2 {k_null_clip_rect.x, k_null_clip_rect.y},
                 f32x2 {k_null_clip_rect.z, k_null_clip_rect.w});
}

void DrawList::PopClipRect() {
    ASSERT(clip_rect_stack.size > 0);
    clip_rect_stack.PopBack();
    UpdateClipRect();
}

void DrawList::PushTextureHandle(TextureHandle const& texture_id) {
    texture_id_stack.PushBack(texture_id);
    UpdateTexturePtr();
}

void DrawList::PopTextureHandle() {
    ASSERT(texture_id_stack.size > 0);
    texture_id_stack.PopBack();
    UpdateTexturePtr();
}

void DrawList::ChannelsSplit(int chans) {
    ASSERT(channels_current == 0 && chans == 1);
    int const old_channels_count = channels.size;
    if (old_channels_count < chans) channels.Resize(chans);
    channels_count = chans;

    // _Channels[] (24 bytes each) hold storage that we'll swap with this->_CmdBuffer/_IdxBuffer
    // The content of _Channels[0] at this point doesn't matter. We clear it to make state tidy in a debugger
    // but we don't strictly need to. When we switch to the next channel, we'll copy _CmdBuffer/_IdxBuffer
    // into _Channels[0] and then _Channels[1] into _CmdBuffer/_IdxBuffer
    ZeroMemory(&channels[0], sizeof(DrawChannel));
    for (int i = 1; i < channels_count; i++) {
        if (i >= old_channels_count) {
            PLACEMENT_NEW(&channels[i]) DrawChannel();
        } else {
            channels[i].cmd_buffer.Resize(0);
            channels[i].idx_buffer.Resize(0);
        }
        if (channels[i].cmd_buffer.size == 0) {
            DrawCmd draw_cmd;
            draw_cmd.clip_rect = clip_rect_stack.Back();
            draw_cmd.texture_id = texture_id_stack.Back();
            channels[i].cmd_buffer.PushBack(draw_cmd);
        }
    }
}

void DrawList::ChannelsMerge() {
    // Note that we never use or rely on channels.Size because it is merely a buffer that we never shrink back
    // to 0 to keep all sub-buffers ready for use.
    if (channels_count <= 1) return;

    ChannelsSetCurrent(0);
    if (cmd_buffer.size && cmd_buffer.Back().elem_count == 0) cmd_buffer.PopBack();

    int new_cmd_buffer_count = 0;
    int new_idx_buffer_count = 0;
    for (int i = 1; i < channels_count; i++) {
        DrawChannel& ch = channels[i];
        if (ch.cmd_buffer.size && ch.cmd_buffer.Back().elem_count == 0) ch.cmd_buffer.PopBack();
        new_cmd_buffer_count += ch.cmd_buffer.size;
        new_idx_buffer_count += ch.idx_buffer.size;
    }
    cmd_buffer.Resize(cmd_buffer.size + new_cmd_buffer_count);
    idx_buffer.Resize(idx_buffer.size + new_idx_buffer_count);

    DrawCmd* cmd_write = cmd_buffer.data + cmd_buffer.size - new_cmd_buffer_count;
    idx_write_ptr = idx_buffer.data + idx_buffer.size - new_idx_buffer_count;
    for (int i = 1; i < channels_count; i++) {
        DrawChannel const& ch = channels[i];
        if (int const sz = ch.cmd_buffer.size) {
            CopyMemory(cmd_write, ch.cmd_buffer.data, (usize)sz * sizeof(DrawCmd));
            cmd_write += sz;
        }
        if (int const sz = ch.idx_buffer.size) {
            CopyMemory(idx_write_ptr, ch.idx_buffer.data, (usize)sz * sizeof(DrawIdx));
            idx_write_ptr += sz;
        }
    }
    AddDrawCmd();
    channels_count = 1;
}

void DrawList::ChannelsSetCurrent(int idx) {
    ASSERT(idx < channels_count);
    if (channels_current == idx) return;
    CopyMemory(&channels.data[channels_current].cmd_buffer,
               &cmd_buffer,
               sizeof(cmd_buffer)); // copy 12 bytes, four times
    CopyMemory(&channels.data[channels_current].idx_buffer, &idx_buffer, sizeof(idx_buffer));
    channels_current = idx;
    CopyMemory(&cmd_buffer, &channels.data[channels_current].cmd_buffer, sizeof(cmd_buffer));
    CopyMemory(&idx_buffer, &channels.data[channels_current].idx_buffer, sizeof(idx_buffer));
    idx_write_ptr = idx_buffer.data + idx_buffer.size;
}

// NB: this can be called with negative count for removing primitives (as long as the result does not
// underflow)
void DrawList::PrimReserve(int idx_count, int vtx_count) {
    DrawCmd& draw_cmd = cmd_buffer.data[cmd_buffer.size - 1];
    draw_cmd.elem_count += (unsigned)idx_count;

    int const vtx_buffer_size = vtx_buffer.size;
    vtx_buffer.Resize(vtx_buffer_size + vtx_count);
    vtx_write_ptr = vtx_buffer.data + vtx_buffer_size;

    int const idx_buffer_size = idx_buffer.size;
    idx_buffer.Resize(idx_buffer_size + idx_count);
    idx_write_ptr = idx_buffer.data + idx_buffer_size;
}

// Fully unrolled with inline call to keep our debug builds decently fast.
void DrawList::PrimRect(f32x2 const& a, f32x2 const& c, u32 col) {
    f32x2 const b {c.x, a.y};
    f32x2 const d {a.x, c.y};
    f32x2 const uv(context->fonts.tex_uv_white_pixel);
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

void DrawList::PrimRectUV(f32x2 const& a, f32x2 const& c, f32x2 const& uv_a, f32x2 const& uv_c, u32 col) {
    f32x2 const b {c.x, a.y};
    f32x2 const d {a.x, c.y};
    f32x2 const uv_b {uv_c.x, uv_a.y};
    f32x2 const uv_d {uv_a.x, uv_c.y};
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv_a;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv_b;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv_c;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv_d;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

void DrawList::PrimQuadUV(f32x2 const& a,
                          f32x2 const& b,
                          f32x2 const& c,
                          f32x2 const& d,
                          f32x2 const& uv_a,
                          f32x2 const& uv_b,
                          f32x2 const& uv_c,
                          f32x2 const& uv_d,
                          u32 col) {
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv_a;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv_b;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv_c;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv_d;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

constexpr u32 k_red_shift = 0;
constexpr u32 k_green_shift = 8;
constexpr u32 k_blue_shift = 16;
constexpr u32 k_alpha_shift = 24;
constexpr u32 k_alpha_mask = 0xFF000000;
constexpr u32 ColU32(u8 r, u8 g, u8 b, u8 a) {
    return (u32)a << k_alpha_shift | (u32)b << k_blue_shift | (u32)g << k_green_shift | (u32)r << k_red_shift;
}

// IMPROVE: Thickness anti-aliased lines cap are missing their AA fringe.
void DrawList::AddPolyline(f32x2 const* points,
                           int const points_count,
                           u32 col,
                           bool closed,
                           f32 thickness,
                           bool anti_aliased) {
    if (points_count < 2) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    anti_aliased &= context->anti_aliased_lines;

    int count = points_count;
    if (!closed) count = points_count - 1;

    bool const thick_line = thickness > 1.0f;
    if (anti_aliased) {
        // Anti-aliased stroke
        // const f32 AA_SIZE = 1.0f;
        f32 const aa_size = context->stroke_anti_alias;
        u32 const col_trans = col & ColU32(255, 255, 255, 0);

        int const idx_count = thick_line ? count * 18 : count * 12;
        int const vtx_count = thick_line ? points_count * 4 : points_count * 3;
        PrimReserve(idx_count, vtx_count);

        // Temporary buffer
        auto* temp_normals =
            (f32x2*)__builtin_alloca((unsigned)points_count * (thick_line ? 5 : 3) * sizeof(f32x2));
        f32x2* temp_points = temp_normals + points_count;

        for (int i1 = 0; i1 < count; i1++) {
            int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            f32x2 diff = points[i2] - points[i1];
            diff *= InvLength(diff, 1.0f);
            temp_normals[i1].x = diff.y;
            temp_normals[i1].y = -diff.x;
        }
        if (!closed) temp_normals[points_count - 1] = temp_normals[points_count - 2];

        if (!thick_line) {
            if (!closed) {
                temp_points[0] = points[0] + temp_normals[0] * aa_size;
                temp_points[1] = points[0] - temp_normals[0] * aa_size;
                temp_points[((points_count - 1) * 2) + 0] =
                    points[points_count - 1] + temp_normals[points_count - 1] * aa_size;
                temp_points[((points_count - 1) * 2) + 1] =
                    points[points_count - 1] - temp_normals[points_count - 1] * aa_size;
            }

            // FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
            unsigned int idx1 = vtx_current_idx;
            for (int i1 = 0; i1 < count; i1++) {
                int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                unsigned int const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 3;

                // Average normals
                f32x2 dm = (temp_normals[i1] + temp_normals[i2]) * 0.5f;
                f32 const dmr2 = (dm.x * dm.x) + (dm.y * dm.y);
                if (dmr2 > 0.000001f) {
                    f32 scale = 1.0f / dmr2;
                    if (scale > 100.0f) scale = 100.0f;
                    dm *= scale;
                }
                dm *= aa_size;
                temp_points[(i2 * 2) + 0] = points[i2] + dm;
                temp_points[(i2 * 2) + 1] = points[i2] - dm;

                // Add indexes
                idx_write_ptr[0] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[1] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[2] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[3] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[4] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[5] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[6] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[7] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[8] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[9] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[10] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[11] = (DrawIdx)(idx2 + 1);
                idx_write_ptr += 12;

                idx1 = idx2;
            }

            // Add vertices
            for (int i = 0; i < points_count; i++) {
                vtx_write_ptr[0].pos = points[i];
                vtx_write_ptr[0].uv = uv;
                vtx_write_ptr[0].col = col;
                vtx_write_ptr[1].pos = temp_points[(i * 2) + 0];
                vtx_write_ptr[1].uv = uv;
                vtx_write_ptr[1].col = col_trans;
                vtx_write_ptr[2].pos = temp_points[(i * 2) + 1];
                vtx_write_ptr[2].uv = uv;
                vtx_write_ptr[2].col = col_trans;
                vtx_write_ptr += 3;
            }
        } else {
            f32 const half_inner_thickness = (thickness - aa_size) * 0.5f;
            if (!closed) {
                temp_points[0] = points[0] + temp_normals[0] * (half_inner_thickness + aa_size);
                temp_points[1] = points[0] + temp_normals[0] * (half_inner_thickness);
                temp_points[2] = points[0] - temp_normals[0] * (half_inner_thickness);
                temp_points[3] = points[0] - temp_normals[0] * (half_inner_thickness + aa_size);
                temp_points[((points_count - 1) * 4) + 0] =
                    points[points_count - 1] +
                    temp_normals[points_count - 1] * (half_inner_thickness + aa_size);
                temp_points[((points_count - 1) * 4) + 1] =
                    points[points_count - 1] + temp_normals[points_count - 1] * (half_inner_thickness);
                temp_points[((points_count - 1) * 4) + 2] =
                    points[points_count - 1] - temp_normals[points_count - 1] * (half_inner_thickness);
                temp_points[((points_count - 1) * 4) + 3] =
                    points[points_count - 1] -
                    temp_normals[points_count - 1] * (half_inner_thickness + aa_size);
            }

            // FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
            unsigned int idx1 = vtx_current_idx;
            for (int i1 = 0; i1 < count; i1++) {
                int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                unsigned int const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 4;

                // Average normals
                f32x2 dm = (temp_normals[i1] + temp_normals[i2]) * 0.5f;
                f32 const dmr2 = (dm.x * dm.x) + (dm.y * dm.y);
                if (dmr2 > 0.000001f) {
                    f32 scale = 1.0f / dmr2;
                    if (scale > 100.0f) scale = 100.0f;
                    dm *= scale;
                }
                f32x2 const dm_out = dm * (half_inner_thickness + aa_size);
                f32x2 const dm_in = dm * half_inner_thickness;
                temp_points[(i2 * 4) + 0] = points[i2] + dm_out;
                temp_points[(i2 * 4) + 1] = points[i2] + dm_in;
                temp_points[(i2 * 4) + 2] = points[i2] - dm_in;
                temp_points[(i2 * 4) + 3] = points[i2] - dm_out;

                // Add indexes
                idx_write_ptr[0] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[1] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[2] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[3] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[4] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[5] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[6] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[7] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[8] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[9] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[10] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[11] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[12] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[13] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[14] = (DrawIdx)(idx1 + 3);
                idx_write_ptr[15] = (DrawIdx)(idx1 + 3);
                idx_write_ptr[16] = (DrawIdx)(idx2 + 3);
                idx_write_ptr[17] = (DrawIdx)(idx2 + 2);
                idx_write_ptr += 18;

                idx1 = idx2;
            }

            // Add vertices
            for (int i = 0; i < points_count; i++) {
                vtx_write_ptr[0].pos = temp_points[(i * 4) + 0];
                vtx_write_ptr[0].uv = uv;
                vtx_write_ptr[0].col = col_trans;
                vtx_write_ptr[1].pos = temp_points[(i * 4) + 1];
                vtx_write_ptr[1].uv = uv;
                vtx_write_ptr[1].col = col;
                vtx_write_ptr[2].pos = temp_points[(i * 4) + 2];
                vtx_write_ptr[2].uv = uv;
                vtx_write_ptr[2].col = col;
                vtx_write_ptr[3].pos = temp_points[(i * 4) + 3];
                vtx_write_ptr[3].uv = uv;
                vtx_write_ptr[3].col = col_trans;
                vtx_write_ptr += 4;
            }
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    } else {
        // Non Anti-aliased Stroke
        int const idx_count = count * 6;
        int const vtx_count = count * 4; // FIXME-OPT: Not sharing edges
        PrimReserve(idx_count, vtx_count);

        for (int i1 = 0; i1 < count; i1++) {
            int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            f32x2 const& p1 = points[i1];
            f32x2 const& p2 = points[i2];
            f32x2 diff = p2 - p1;
            f32 const inv = InvLength(diff, 1.0f);
            diff *= inv;

            f32 const dx = diff.x * (thickness * 0.5f);
            f32 const dy = diff.y * (thickness * 0.5f);
            vtx_write_ptr[0].pos.x = p1.x + dy;
            vtx_write_ptr[0].pos.y = p1.y - dx;
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col;
            vtx_write_ptr[1].pos.x = p2.x + dy;
            vtx_write_ptr[1].pos.y = p2.y - dx;
            vtx_write_ptr[1].uv = uv;
            vtx_write_ptr[1].col = col;
            vtx_write_ptr[2].pos.x = p2.x - dy;
            vtx_write_ptr[2].pos.y = p2.y + dx;
            vtx_write_ptr[2].uv = uv;
            vtx_write_ptr[2].col = col;
            vtx_write_ptr[3].pos.x = p1.x - dy;
            vtx_write_ptr[3].pos.y = p1.y + dx;
            vtx_write_ptr[3].uv = uv;
            vtx_write_ptr[3].col = col;

            // sw_print("Before: (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)\n",
            //         _VtxWritePtr[0].pos.x, _VtxWritePtr[0].pos.y,
            //         _VtxWritePtr[1].pos.x, _VtxWritePtr[1].pos.y,
            //         _VtxWritePtr[2].pos.x, _VtxWritePtr[2].pos.y,
            //         _VtxWritePtr[3].pos.x, _VtxWritePtr[3].pos.y
            //         );

            // if (_VtxWritePtr[0].pos.x < 50) _VtxWritePtr[0].pos.x = (f32)(int)_VtxWritePtr[0].pos.x;
            // else _VtxWritePtr[0].pos.x = ceilf(_VtxWritePtr[0].pos.x);
            // if (_VtxWritePtr[0].pos.y < 50) _VtxWritePtr[0].pos.y = (f32)(int)_VtxWritePtr[0].pos.y;
            // else _VtxWritePtr[0].pos.y = ceilf(_VtxWritePtr[0].pos.y);

            // if (_VtxWritePtr[1].pos.x < 50) _VtxWritePtr[1].pos.x = (f32)(int)_VtxWritePtr[1].pos.x;
            // else _VtxWritePtr[1].pos.x = ceilf(_VtxWritePtr[1].pos.x);
            // if (_VtxWritePtr[1].pos.y < 50) _VtxWritePtr[1].pos.y = (f32)(int)_VtxWritePtr[1].pos.y;
            // else _VtxWritePtr[1].pos.y = ceilf(_VtxWritePtr[1].pos.y);

            // if (_VtxWritePtr[2].pos.x < 50) _VtxWritePtr[2].pos.x = (f32)(int)_VtxWritePtr[2].pos.x;
            // else _VtxWritePtr[2].pos.x = ceilf(_VtxWritePtr[2].pos.x);
            // if (_VtxWritePtr[2].pos.y < 50) _VtxWritePtr[2].pos.y = (f32)(int)_VtxWritePtr[2].pos.y;
            // else _VtxWritePtr[2].pos.y = ceilf(_VtxWritePtr[2].pos.y);

            // if (_VtxWritePtr[3].pos.x < 50) _VtxWritePtr[3].pos.x = (f32)(int)_VtxWritePtr[3].pos.x;
            // else _VtxWritePtr[3].pos.x = ceilf(_VtxWritePtr[3].pos.x);
            // if (_VtxWritePtr[3].pos.y < 50) _VtxWritePtr[3].pos.y = (f32)(int)_VtxWritePtr[3].pos.y;
            // else _VtxWritePtr[3].pos.y = ceilf(_VtxWritePtr[3].pos.y);

            // _VtxWritePtr[0].pos.x = roundf(_VtxWritePtr[0].pos.x); _VtxWritePtr[0].pos.y =
            // roundf(_VtxWritePtr[0].pos.y); _VtxWritePtr[1].pos.x = roundf(_VtxWritePtr[1].pos.x);
            // _VtxWritePtr[1].pos.y = roundf(_VtxWritePtr[1].pos.y); _VtxWritePtr[2].pos.x =
            // roundf(_VtxWritePtr[2].pos.x); _VtxWritePtr[2].pos.y = roundf(_VtxWritePtr[2].pos.y);
            // _VtxWritePtr[3].pos.x = roundf(_VtxWritePtr[3].pos.x); _VtxWritePtr[3].pos.y =
            // roundf(_VtxWritePtr[3].pos.y);

            // sw_print("After: (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)\n",
            //         _VtxWritePtr[0].pos.x, _VtxWritePtr[0].pos.y,
            //         _VtxWritePtr[1].pos.x, _VtxWritePtr[1].pos.y,
            //         _VtxWritePtr[2].pos.x, _VtxWritePtr[2].pos.y,
            //         _VtxWritePtr[3].pos.x, _VtxWritePtr[3].pos.y
            //         );

            vtx_write_ptr += 4;

            idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
            idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);
            idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
            idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);
            idx_write_ptr += 6;
            vtx_current_idx += 4;
        }
    }
}

void DrawList::AddConvexPolyFilled(f32x2 const* points, int const points_count, u32 col, bool anti_aliased) {
    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    anti_aliased &= context->anti_aliased_shapes;
    // if (Gui::GetIO().KeyCtrl) anti_aliased = false; // Debug

    if (anti_aliased) {
        // Anti-aliased Fill
        f32 const aa_size = context->fill_anti_alias;
        u32 const col_trans = col & ColU32(255, 255, 255, 0);
        int const idx_count = ((points_count - 2) * 3) + (points_count * 6);
        int const vtx_count = (points_count * 2);
        PrimReserve(idx_count, vtx_count);

        // Add indexes for fill
        unsigned int const vtx_inner_idx = vtx_current_idx;
        unsigned int const vtx_outer_idx = vtx_current_idx + 1;
        for (int i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_inner_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_inner_idx + (((unsigned)i - 1) << 1));
            idx_write_ptr[2] = (DrawIdx)(vtx_inner_idx + ((unsigned)i << 1));
            idx_write_ptr += 3;
        }

        // Compute normals
        auto* temp_normals = (f32x2*)__builtin_alloca((unsigned)points_count * sizeof(f32x2));
        for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            f32x2 const& p0 = points[i0];
            f32x2 const& p1 = points[i1];
            f32x2 diff = p1 - p0;
            diff *= InvLength(diff, 1.0f);
            temp_normals[i0].x = diff.y;
            temp_normals[i0].y = -diff.x;
        }

        for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            // Average normals
            f32x2 const& n0 = temp_normals[i0];
            f32x2 const& n1 = temp_normals[i1];
            f32x2 dm = (n0 + n1) * 0.5f;
            f32 const dmr2 = (dm.x * dm.x) + (dm.y * dm.y);
            if (dmr2 > 0.000001f) {
                f32 scale = 1.0f / dmr2;
                if (scale > 100.0f) scale = 100.0f;
                dm *= scale;
            }
            dm *= aa_size * 0.5f;

            // Add vertices
            vtx_write_ptr[0].pos = (points[i1] - dm);
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col; // Inner
            vtx_write_ptr[1].pos = (points[i1] + dm);
            vtx_write_ptr[1].uv = uv;
            vtx_write_ptr[1].col = col_trans; // Outer
            vtx_write_ptr += 2;

            // Add indexes for fringes
            idx_write_ptr[0] = (DrawIdx)(vtx_inner_idx + ((unsigned)i1 << 1));
            idx_write_ptr[1] = (DrawIdx)(vtx_inner_idx + ((unsigned)i0 << 1));
            idx_write_ptr[2] = (DrawIdx)(vtx_outer_idx + ((unsigned)i0 << 1));
            idx_write_ptr[3] = (DrawIdx)(vtx_outer_idx + ((unsigned)i0 << 1));
            idx_write_ptr[4] = (DrawIdx)(vtx_outer_idx + ((unsigned)i1 << 1));
            idx_write_ptr[5] = (DrawIdx)(vtx_inner_idx + ((unsigned)i1 << 1));
            idx_write_ptr += 6;
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    } else {
        // Non Anti-aliased Fill
        int const idx_count = (points_count - 2) * 3;
        int const vtx_count = points_count;
        PrimReserve(idx_count, vtx_count);
        for (int i = 0; i < vtx_count; i++) {
            vtx_write_ptr[0].pos = points[i];
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col;
            vtx_write_ptr++;
        }
        for (int i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + (unsigned)i - 1);
            idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + (unsigned)i);
            idx_write_ptr += 3;
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    }
}

void DrawList::PathArcToFast(f32x2 const& centre, f32 radius, int amin, int amax) {
    static f32x2 circle_vtx[24];
    amin *= 2;
    amax *= 2;
    static bool circle_vtx_builds = false;
    auto const circle_vtx_count = (int)ArraySize(circle_vtx);
    if (!circle_vtx_builds) {
        for (int i = 0; i < circle_vtx_count; i++) {
            f32 const a = ((f32)i / (f32)circle_vtx_count) * 2 * k_pi<>;
            circle_vtx[i].x = Cos(a);
            circle_vtx[i].y = Sin(a);
        }
        circle_vtx_builds = true;
    }

    if (amin > amax) return;
    if (radius == 0.0f) {
        path.PushBack(centre);
    } else {
        path.Reserve(path.size + (amax - amin + 1));
        for (int a = amin; a <= amax; a++) {
            f32x2 const& c = circle_vtx[a % circle_vtx_count];
            path.PushBack(f32x2 {centre.x + (c.x * radius), centre.y + (c.y * radius)});
        }
    }
}

void DrawList::PathArcTo(f32x2 const& centre, f32 radius, f32 amin, f32 amax, int num_segments) {
    if (radius == 0.0f) path.PushBack(centre);
    path.Reserve(path.size + (num_segments + 1));
    for (int i = 0; i <= num_segments; i++) {
        f32 const a = amin + (((f32)i / (f32)num_segments) * (amax - amin));
        path.PushBack(f32x2 {centre.x + (Cos(a) * radius), centre.y + (Sin(a) * radius)});
    }
}

static void PathBezierToCasteljau(Vector<f32x2>* path,
                                  f32 x1,
                                  f32 y1,
                                  f32 x2,
                                  f32 y2,
                                  f32 x3,
                                  f32 y3,
                                  f32 x4,
                                  f32 y4,
                                  f32 tess_tol,
                                  int level) {
    f32 const dx = x4 - x1;
    f32 const dy = y4 - y1;
    f32 d2 = (((x2 - x4) * dy) - ((y2 - y4) * dx));
    f32 d3 = (((x3 - x4) * dy) - ((y3 - y4) * dx));
    d2 = (d2 >= 0) ? d2 : -d2;
    d3 = (d3 >= 0) ? d3 : -d3;
    if ((d2 + d3) * (d2 + d3) < tess_tol * (dx * dx + dy * dy)) {
        path->PushBack(f32x2 {x4, y4});
    } else if (level < 10) {
        f32 const x12 = (x1 + x2) * 0.5f;
        f32 const y12 = (y1 + y2) * 0.5f;
        f32 const x23 = (x2 + x3) * 0.5f;
        f32 const y23 = (y2 + y3) * 0.5f;
        f32 const x34 = (x3 + x4) * 0.5f;
        f32 const y34 = (y3 + y4) * 0.5f;
        f32 const x123 = (x12 + x23) * 0.5f;
        f32 const y123 = (y12 + y23) * 0.5f;
        f32 const x234 = (x23 + x34) * 0.5f;
        f32 const y234 = (y23 + y34) * 0.5f;
        f32 const x1234 = (x123 + x234) * 0.5f;
        f32 const y1234 = (y123 + y234) * 0.5f;

        PathBezierToCasteljau(path, x1, y1, x12, y12, x123, y123, x1234, y1234, tess_tol, level + 1);
        PathBezierToCasteljau(path, x1234, y1234, x234, y234, x34, y34, x4, y4, tess_tol, level + 1);
    }
}

void DrawList::PathBezierCurveTo(f32x2 const& p2, f32x2 const& p3, f32x2 const& p4, int num_segments) {
    f32x2 const p1 = path.Back();
    if (num_segments == 0) {
        // Auto-tessellated
        PathBezierToCasteljau(&path,
                              p1.x,
                              p1.y,
                              p2.x,
                              p2.y,
                              p3.x,
                              p3.y,
                              p4.x,
                              p4.y,
                              context->curve_tessellation_tol,
                              0);
    } else {
        f32 const t_step = 1.0f / (f32)num_segments;
        for (int i_step = 1; i_step <= num_segments; i_step++) {
            f32 const t = t_step * (f32)i_step;
            f32 const u = 1.0f - t;
            f32 const w1 = u * u * u;
            f32 const w2 = 3 * u * u * t;
            f32 const w3 = 3 * u * t * t;
            f32 const w4 = t * t * t;
            path.PushBack(f32x2 {(w1 * p1.x) + (w2 * p2.x) + (w3 * p3.x) + (w4 * p4.x),
                                 (w1 * p1.y) + (w2 * p2.y) + (w3 * p3.y) + (w4 * p4.y)});
        }
    }
}

void DrawList::PathRect(f32x2 const& a, f32x2 const& b, f32 rounding, int rounding_corners) {
    f32 r = rounding;
    r = Min(r,
            (Fabs(b.x - a.x) *
                    (((rounding_corners & (1 | 2)) == (1 | 2)) || ((rounding_corners & (4 | 8)) == (4 | 8))
                         ? 0.5f
                         : 1.0f)) -
                1.0f);
    r = Min(r,
            (Fabs(b.y - a.y) *
                    (((rounding_corners & (1 | 8)) == (1 | 8)) || ((rounding_corners & (2 | 4)) == (2 | 4))
                         ? 0.5f
                         : 1.0f)) -
                1.0f);

    if (r <= 0.0f || rounding_corners == 0) {
        PathLineTo(a);
        PathLineTo(f32x2 {b.x + 1, a.y});
        PathLineTo(f32x2 {b.x, a.y});
        PathLineTo(b);
        PathLineTo(f32x2 {a.x, b.y});
    } else {
        f32 const r0 = (rounding_corners & 1) ? r : 0.0f;
        f32 const r1 = (rounding_corners & 2) ? r : 0.0f;
        f32 const r2 = (rounding_corners & 4) ? r : 0.0f;
        f32 const r3 = (rounding_corners & 8) ? r : 0.0f;
        PathArcToFast(f32x2 {a.x + r0, a.y + r0}, r0, 6, 9);
        PathArcToFast(f32x2 {b.x - r1, a.y + r1}, r1, 9, 12);
        PathArcToFast(f32x2 {b.x - r2, b.y - r2}, r2, 0, 3);
        PathArcToFast(f32x2 {a.x + r3, b.y - r3}, r3, 3, 6);
    }
}

void DrawList::AddLine(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathLineTo(a + f32x2 {0.5f, 0.5f});
    PathLineTo(b + f32x2 {0.5f, 0.5f});
    PathStroke(col, false, thickness);
}

void DrawList::AddNonAABox(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness) {

    f32x2 const& p1 = a;
    auto const p2 = f32x2 {b.x, a.y};
    f32x2 const& p3 = b;
    auto const p4 = f32x2 {a.x, b.y};

    f32x2 const uv = context->fonts.tex_uv_white_pixel;

    // Non Anti-aliased Stroke
    int const idx_count = 4 * 6;
    int const vtx_count = 4 * 4; // FIXME-OPT: Not sharing edges
    PrimReserve(idx_count, vtx_count);

    {
        vtx_write_ptr[0].pos.x = p1.x + thickness;
        vtx_write_ptr[0].pos.y = p1.y;

        vtx_write_ptr[1].pos.x = p2.x;
        vtx_write_ptr[1].pos.y = p2.y;

        vtx_write_ptr[2].pos.x = p2.x;
        vtx_write_ptr[2].pos.y = p2.y + thickness;

        vtx_write_ptr[3].pos.x = p1.x + thickness;
        vtx_write_ptr[3].pos.y = p1.y + thickness;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p2.x;
        vtx_write_ptr[0].pos.y = p2.y + thickness;

        vtx_write_ptr[1].pos.x = p3.x;
        vtx_write_ptr[1].pos.y = p3.y;

        vtx_write_ptr[2].pos.x = p3.x - thickness;
        vtx_write_ptr[2].pos.y = p3.y;

        vtx_write_ptr[3].pos.x = p2.x - thickness;
        vtx_write_ptr[3].pos.y = p2.y + thickness;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p4.x;
        vtx_write_ptr[0].pos.y = p4.y - thickness;

        vtx_write_ptr[1].pos.x = p3.x - thickness;
        vtx_write_ptr[1].pos.y = p3.y - thickness;

        vtx_write_ptr[2].pos.x = p3.x - thickness;
        vtx_write_ptr[2].pos.y = p3.y;

        vtx_write_ptr[3].pos.x = p4.x;
        vtx_write_ptr[3].pos.y = p4.y;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p1.x + thickness;
        vtx_write_ptr[0].pos.y = p1.y;

        vtx_write_ptr[1].pos.x = p4.x + thickness;
        vtx_write_ptr[1].pos.y = p4.y - thickness;

        vtx_write_ptr[2].pos.x = p4.x;
        vtx_write_ptr[2].pos.y = p4.y - thickness;

        vtx_write_ptr[3].pos.x = p1.x;
        vtx_write_ptr[3].pos.y = p1.y;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }
}

// a: upper-left, b: lower-right. we don't render 1 px sized rectangles properly.
void DrawList::AddRect(f32x2 const& a,
                       f32x2 const& b,
                       u32 col,
                       f32 rounding,
                       int rounding_corners_flags,
                       f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathRect(a + f32x2 {0.5f, 0.5f}, b - f32x2 {0.5f, 0.5f}, rounding, rounding_corners_flags);
    PathStroke(col, true, thickness);
}

void DrawList::AddRectFilled(f32x2 const& a,
                             f32x2 const& b,
                             u32 col,
                             f32 rounding,
                             int rounding_corners_flags) {
    if ((col & k_alpha_mask) == 0) return;
    if (rounding > 0.0f) {
        PathRect(a, b, rounding, rounding_corners_flags);
        PathFill(col);
    } else {
        PrimReserve(6, 4);
        PrimRect(a, b, col);
    }
}

void DrawList::AddRectFilledMultiColor(f32x2 const& a,
                                       f32x2 const& c,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    PrimReserve(6, 4);
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 1));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 3));
    PrimWriteVtx(a, uv, col_upr_left);
    PrimWriteVtx(f32x2 {c.x, a.y}, uv, col_upr_right);
    PrimWriteVtx(c, uv, col_bot_right);
    PrimWriteVtx(f32x2 {a.x, c.y}, uv, col_bot_left);
}

void DrawList::AddQuadFilledMultiColor(f32x2 const& upr_left,
                                       f32x2 const& upr_right,
                                       f32x2 const& bot_right,
                                       f32x2 const& bot_left,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    PrimReserve(6, 4);
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 1));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 3));
    PrimWriteVtx(upr_left, uv, col_upr_left);
    PrimWriteVtx(upr_right, uv, col_upr_right);
    PrimWriteVtx(bot_right, uv, col_bot_right);
    PrimWriteVtx(bot_left, uv, col_bot_left);
}

void DrawList::AddQuad(f32x2 const& a,
                       f32x2 const& b,
                       f32x2 const& c,
                       f32x2 const& d,
                       u32 col,
                       f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathStroke(col, true, thickness);
}

void DrawList::AddQuadFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, f32x2 const& d, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathFill(col);
}

void DrawList::AddTriangle(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathStroke(col, true, thickness);
}

void DrawList::AddTriangleFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathFill(col);
}

void DrawList::AddCircle(f32x2 const& centre, f32 radius, u32 col, int num_segments, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius - 0.5f, 0.0f, a_max, num_segments);
    PathStroke(col, true, thickness);
}

void DrawList::AddCircleFilled(f32x2 const& centre, f32 radius, u32 col, int num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius, 0.0f, a_max, num_segments);
    PathFill(col);
}

void DrawList::AddBezierCurve(f32x2 const& pos0,
                              f32x2 const& cp0,
                              f32x2 const& cp1,
                              f32x2 const& pos1,
                              u32 col,
                              f32 thickness,
                              int num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(pos0);
    PathBezierCurveTo(cp0, cp1, pos1, num_segments);
    PathStroke(col, false, thickness);
}

static f32x2 GetTextPosition(Font* font,
                             f32 font_size,
                             f32x2 r_min,
                             f32x2 r_max,
                             TextJustification justification,
                             String text,
                             f32x2* calculated_size) {
    f32x2 pos = r_min;
    if (justification != TextJustification::TopLeft) {
        Optional<f32x2> size = {};
        auto const height = font_size;
        if (justification & TextJustification::Left) {
            pos.x = r_min.x;
        } else {
            auto const width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text).x;
            size = f32x2 {width, height};
            if (justification & TextJustification::Right)
                pos.x = r_max.x - width;
            else if (justification & TextJustification::HorizontallyCentred)
                pos.x = r_min.x + ((r_max.x - r_min.x) / 2) - (width / 2);
        }

        if (justification & TextJustification::Baseline)
            pos.y = r_max.y - height + (-font->descent);
        else if (justification & TextJustification::Top)
            pos.y = r_min.y;
        else if (justification & TextJustification::Bottom)
            pos.y = r_max.y - height;
        else if (justification & TextJustification::VerticallyCentred)
            pos.y = r_min.y + ((r_max.y - r_min.y) / 2) - (height / 2);
        if (calculated_size && size) *calculated_size = *size;
    }
    return pos;
}

String OverflowText(OverflowTextArgs const& args) {
    String constexpr k_dots {".."};
    auto const epsilon = 0.01f;

    if (args.overflow_type != TextOverflowType::AllowOverflow) {
        auto const text_width = args.text_size
                                    ? args.text_size->x
                                    : args.font->CalcTextSizeA(args.font_size, FLT_MAX, 0.0f, args.str).x;
        if (text_width > (args.r.w + epsilon)) {
            f32 const dots_size = args.font->CalcTextSizeA(args.font_size, FLT_MAX, 0.0f, k_dots).x;
            f32 line_width = 0;

            if (args.overflow_type == TextOverflowType::ShowDotsOnRight) {
                char const* s = args.str.data;
                auto const end = End(args.str);
                while (s < end) {
                    auto prev_s = s;
                    auto c = (u32)*s;
                    if (c < 0x80) {
                        s += 1;
                    } else {
                        s += Utf8CharacterToUtf32(&c, s, end, k_max_u16_codepoint);
                        if (c == 0) break;
                    }

                    if (c < 32) {
                        if (c == '\n' || c == '\r') continue;
                    }

                    f32 const char_width =
                        ((int)c < args.font->index_x_advance.size ? args.font->index_x_advance[(int)c]
                                                                  : args.font->fallback_x_advance) *
                        args.font_scaling;

                    line_width += char_width;

                    if ((line_width + dots_size) > args.r.w) {
                        DynamicArray<char> buffer(args.allocator);
                        dyn::Assign(buffer, args.str.SubSpan(0, (usize)(prev_s - args.str.data)));
                        dyn::AppendSpan(buffer, k_dots);
                        args.text_pos.x = args.r.x;
                        return buffer.ToOwnedSpan();
                    }
                }
            } else if (args.overflow_type == TextOverflowType::ShowDotsOnLeft) {
                auto get_char_previous_to_end = [](char const* start, char const* end) {
                    char const* prev_s = start;
                    for (auto s = start; s < end && *s != '\0';) {
                        s = IncrementUTF8Characters(s, 1);
                        if (s >= end) return prev_s;
                        prev_s = s;
                    }
                    return start;
                };

                char const* start = args.str.data;
                char const* end = End(args.str);
                char const* s = get_char_previous_to_end(start, end);
                while (s > start) {
                    auto prev_s = s;
                    auto c = (u32)*s;
                    if (c < 0x80) {
                    } else {
                        Utf8CharacterToUtf32(&c, s, end, k_max_u16_codepoint);
                        if (c == 0) break;
                    }

                    if (c < 32) {
                        if (c == '\n' || c == '\r') continue;
                    }

                    f32 const char_width =
                        ((int)c < args.font->index_x_advance.size ? args.font->index_x_advance[(int)c]
                                                                  : args.font->fallback_x_advance) *
                        args.font_scaling;

                    line_width += char_width;

                    if ((line_width + dots_size) > args.r.w) {
                        DynamicArray<char> buffer(args.allocator);
                        dyn::Assign(buffer, k_dots);
                        dyn::AppendSpan(buffer, String(prev_s, (usize)(end - prev_s)));
                        args.text_pos.x = args.r.Right() - (line_width + dots_size);
                        return buffer.ToOwnedSpan();
                    }

                    s = get_char_previous_to_end(start, s);
                }
            }
        }
    }

    return args.str;
}

void DrawList::AddTextJustified(Rect r,
                                String str,
                                u32 col,
                                TextJustification justification,
                                TextOverflowType overflow_type,
                                f32 font_scaling) {
    auto font = context->CurrentFont();
    auto font_size = context->CurrentFontSize() * font_scaling;

    ArenaAllocatorWithInlineStorage<1000> temp_allocator {Malloc::Instance()};

    f32x2 text_size {-1, -1};
    auto text_pos = GetTextPosition(font, font_size, r.Min(), r.Max(), justification, str, &text_size);

    str = OverflowText({
        .font = font,
        .font_size = font_size,
        .r = r,
        .str = str,
        .overflow_type = overflow_type,
        .font_scaling = font_scaling,
        .text_size = (text_size.x != -1) ? Optional<f32x2> {text_size} : k_nullopt,
        .allocator = temp_allocator,
        .text_pos = text_pos,
    });

    AddText(font, font_size, text_pos, col, str);
}

void DrawList::AddText(Font const* font,
                       f32 font_size,
                       f32x2 const& pos,
                       u32 col,
                       String str,
                       f32 wrap_width,
                       f32x4 const* cpu_fine_clip_rect) {
    if ((col & k_alpha_mask) == 0) return;
    if (str.size == 0) return;

    f32x4 clip_rect = clip_rect_stack.Back();
    if (cpu_fine_clip_rect) {
        clip_rect.x = Max(clip_rect.x, cpu_fine_clip_rect->x);
        clip_rect.y = Max(clip_rect.y, cpu_fine_clip_rect->y);
        clip_rect.z = Min(clip_rect.z, cpu_fine_clip_rect->z);
        clip_rect.w = Min(clip_rect.w, cpu_fine_clip_rect->w);
    }
    font->RenderText(this,
                     font_size,
                     pos,
                     col,
                     clip_rect,
                     str.data,
                     End(str),
                     wrap_width,
                     cpu_fine_clip_rect != nullptr);
}

void DrawList::AddText(f32x2 const& pos, u32 col, String str) {
    AddText(context->CurrentFont(), context->CurrentFont()->font_size, pos, col, str);
}

static inline f32x2 Mul(f32x2 const& lhs, f32x2 const& rhs) { return f32x2 {lhs.x * rhs.x, lhs.y * rhs.y}; }
static inline f32 LengthSqr(f32x2 const& lhs) { return (lhs.x * lhs.x) + (lhs.y * lhs.y); }
static inline f32 Dot(f32x2 const& a, f32x2 const& b) { return (a.x * b.x) + (a.y * b.y); }

static void ShadeVertsLinearUV(DrawList* draw_list,
                               int vert_start_idx,
                               int vert_end_idx,
                               f32x2 const& a,
                               f32x2 const& b,
                               f32x2 const& uv_a,
                               f32x2 const& uv_b,
                               bool clamp) {
    f32x2 const size = b - a;
    f32x2 const uv_size = uv_b - uv_a;
    auto const scale =
        f32x2 {size.x != 0.0f ? (uv_size.x / size.x) : 0.0f, size.y != 0.0f ? (uv_size.y / size.y) : 0.0f};

    DrawVert* vert_start = draw_list->vtx_buffer.data + vert_start_idx;
    DrawVert* vert_end = draw_list->vtx_buffer.data + vert_end_idx;
    if (clamp) {
        f32x2 const min = Min(uv_a, uv_b);
        f32x2 const max = Max(uv_a, uv_b);
        for (DrawVert* vertex = vert_start; vertex < vert_end; ++vertex)
            vertex->uv = Clamp(uv_a + Mul(f32x2 {vertex->pos.x, vertex->pos.y} - a, scale), min, max);
    } else {
        for (DrawVert* vertex = vert_start; vertex < vert_end; ++vertex)
            vertex->uv = uv_a + Mul(f32x2 {vertex->pos.x, vertex->pos.y} - a, scale);
    }
}

void DrawList::ShadeVertsLinearColorGradientSetAlpha(DrawList* draw_list,
                                                     int vert_start_idx,
                                                     int vert_end_idx,
                                                     f32x2 gradient_p0,
                                                     f32x2 gradient_p1,
                                                     u32 col0,
                                                     u32 col1) {
    auto const gradient_extent = gradient_p1 - gradient_p0;
    auto const gradient_inv_length2 = 1.0f / LengthSqr(gradient_extent);
    auto const vert_start = draw_list->vtx_buffer.data + vert_start_idx;
    auto const vert_end = draw_list->vtx_buffer.data + vert_end_idx;
    auto const col0_r = (f32)((int)(col0 >> k_red_shift) & 0xFF);
    auto const col0_g = (f32)((int)(col0 >> k_green_shift) & 0xFF);
    auto const col0_b = (f32)((int)(col0 >> k_blue_shift) & 0xFF);
    auto const col0_a = (f32)((int)(col0 >> k_alpha_shift) & 0xFF);
    auto const col_delta_r = ((f32)((int)(col1 >> k_red_shift) & 0xFF) - col0_r);
    auto const col_delta_g = ((f32)((int)(col1 >> k_green_shift) & 0xFF) - col0_g);
    auto const col_delta_b = ((f32)((int)(col1 >> k_blue_shift) & 0xFF) - col0_b);
    auto const col_delta_a = ((f32)((int)(col1 >> k_alpha_shift) & 0xFF) - col0_a);
    for (auto vert = vert_start; vert < vert_end; vert++) {
        auto const d = Dot(vert->pos - gradient_p0, gradient_extent);
        auto const t = Clamp(d * gradient_inv_length2, 0.0f, 1.0f);
        auto const r = (u32)(col0_r + (col_delta_r * t));
        auto const g = (u32)(col0_g + (col_delta_g * t));
        auto const b = (u32)(col0_b + (col_delta_b * t));
        auto const a = (u32)(col0_a + (col_delta_a * t));
        vert->col = (r << k_red_shift) | (g << k_green_shift) | (b << k_blue_shift) | (a << k_alpha_shift);
    }
}

void DrawList::AddImage(TextureHandle user_texture_id,
                        f32x2 const& a,
                        f32x2 const& b,
                        f32x2 const& uv0,
                        f32x2 const& uv1,
                        u32 col) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == nullptr) return;

    // FIXME-OPT: This is wasting draw calls.
    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    PrimReserve(6, 4);
    PrimRectUV(a, b, uv0, uv1, col);

    if (push_texture_id) PopTextureHandle();
}

void DrawList::AddImageRounded(TextureHandle user_texture_id,
                               f32x2 const& p_min,
                               f32x2 const& p_max,
                               f32x2 const& uv_min,
                               f32x2 const& uv_max,
                               u32 col,
                               f32 rounding,
                               int rounding_corners) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == nullptr) return;

    if (rounding <= 0.0f || (rounding_corners & 0xf) == 0) {
        AddImage(user_texture_id, p_min, p_max, uv_min, uv_max, col);
        return;
    }

    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    int const vert_start_idx = vtx_buffer.size;
    PathRect(p_min, p_max, rounding, rounding_corners);
    PathFillConvex(col);
    int const vert_end_idx = vtx_buffer.size;
    ShadeVertsLinearUV(this, vert_start_idx, vert_end_idx, p_min, p_max, uv_min, uv_max, true);

    if (push_texture_id) PopTextureHandle();
}

#define STBTT_ifloor(x)    ((int)Floor(x))
#define STBTT_iceil(x)     ((int)Ceil(x))
#define STBTT_sqrt(x)      Sqrt(x)
#define STBTT_pow(x, y)    Pow(x, y)
#define STBTT_fabs(x)      Fabs(x)
#define STBTT_fmod(x, y)   Fmod(x, y)
#define STBTT_cos(x)       Cos(x)
#define STBTT_acos(x)      Acos(x)
#define STBTT_fabs(x)      Fabs(x)
#define STBTT_malloc(x, u) ((void)(u), GpaAlloc(x))
#define STBTT_free(x, u)   ((void)(u), GpaFree(x))
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
    for (int i = 0; i < config_data.size; i++)
        if (!config_data[i].font_data_reference_only && config_data[i].font_data &&
            config_data[i].font_data_owned_by_atlas) {
            GpaFree(config_data[i].font_data);
            config_data[i].font_data = nullptr;
        }

    // When clearing this we lose access to the font name and other information used to build the font.
    for (int i = 0; i < fonts.size; i++)
        if (fonts[i]->config_data >= config_data.data &&
            fonts[i]->config_data < config_data.data + config_data.size) {
            fonts[i]->config_data = nullptr;
        }
    config_data.Clear();
}

void FontAtlas::ClearTexData() {
    if (tex_pixels_alpha8) GpaFree(tex_pixels_alpha8);
    if (tex_pixels_rgb_a32) GpaFree(tex_pixels_rgb_a32);
    tex_pixels_alpha8 = nullptr;
    tex_pixels_rgb_a32 = nullptr;
}

void FontAtlas::ClearFonts() {
    for (int i = 0; i < fonts.size; i++) {
        fonts[i]->~Font();
        GpaFree(fonts[i]);
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
        tex_pixels_rgb_a32 = (unsigned int*)GpaAlloc((usize)(tex_width * tex_height * 4));
        unsigned char const* src = pixels;
        unsigned int* dst = tex_pixels_rgb_a32;
        for (int n = tex_width * tex_height; n > 0; n--)
            *dst++ = ColU32(255, 255, 255, (u8)(*src++));
    }

    *out_pixels = (unsigned char*)tex_pixels_rgb_a32;
    if (out_width) *out_width = tex_width;
    if (out_height) *out_height = tex_height;
    if (out_bytes_per_pixel) *out_bytes_per_pixel = 4;
}

Font* FontAtlas::AddFont(FontConfig const* font_cfg) {
    ASSERT(font_cfg->font_data != nullptr && font_cfg->font_data_size > 0);
    ASSERT(font_cfg->size_pixels > 0.0f);

    // Create new font
    if (!font_cfg->merge_mode) {
        auto* font = (Font*)GpaAlloc(sizeof(Font));
        PLACEMENT_NEW(font) Font();
        fonts.PushBack(font);
    }

    config_data.PushBack(*font_cfg);
    FontConfig& new_font_cfg = config_data.Back();
    if (!new_font_cfg.dst_font) new_font_cfg.dst_font = fonts.Back();
    if (!new_font_cfg.font_data_reference_only && !new_font_cfg.font_data_owned_by_atlas) {
        new_font_cfg.font_data = GpaAlloc((usize)new_font_cfg.font_data_size);
        new_font_cfg.font_data_owned_by_atlas = true;
        CopyMemory(new_font_cfg.font_data, font_cfg->font_data, (usize)new_font_cfg.font_data_size);
    }

    // Invalidate texture
    ClearTexData();
    return new_font_cfg.dst_font;
}

// NBM Transfer ownership of 'ttf_data' to FontAtlas, unless font_cfg_template->FontDataOwnedByAtlas == false.
// Or, you can specific FontDataReferenceOnly = true in which case you must keep the memory valid until
// Build(). Owned TTF buffer will be deleted after Build().
Font* FontAtlas::AddFontFromMemoryTTF(void* ttf_data,
                                      int ttf_size,
                                      f32 size_pixels,
                                      FontConfig const* font_cfg_template,
                                      Span<GlyphRange const> glyph_ranges) {
    FontConfig font_cfg = font_cfg_template ? *font_cfg_template : FontConfig();
    ASSERT(glyph_ranges.size < font_cfg.glyph_ranges.Capacity());
    ASSERT(font_cfg.font_data == nullptr);
    font_cfg.font_data = ttf_data;
    font_cfg.font_data_size = ttf_size;
    font_cfg.size_pixels = size_pixels;
    if (glyph_ranges.size) font_cfg.glyph_ranges = glyph_ranges;
    return AddFont(&font_cfg);
}

bool FontAtlas::Build() {
    ASSERT(config_data.size > 0);

    tex_id = nullptr;
    tex_width = tex_height = 0;
    tex_uv_white_pixel = f32x2 {0, 0};
    ClearTexData();

    struct FontTempBuildData {
        stbtt_fontinfo font_info;
        stbrp_rect* rects;
        stbtt_pack_range* ranges;
        int ranges_count;
    };
    auto* tmp_array = (FontTempBuildData*)GpaAlloc((usize)config_data.size * sizeof(FontTempBuildData));

    // Initialize font information early (so we can error without any cleanup) + count glyphs
    int total_glyph_count = 0;
    int total_glyph_range_count = 0;
    for (int input_i = 0; input_i < config_data.size; input_i++) {
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
    Vector<stbrp_rect> extra_rects;
    RenderCustomTexData(0, &extra_rects);
    stbtt_PackSetOversampling(&spc, 1, 1);
    stbrp_pack_rects((stbrp_context*)spc.pack_info, &extra_rects[0], extra_rects.size);
    for (int i = 0; i < extra_rects.size; i++)
        if (extra_rects[i].was_packed) tex_height = Max(tex_height, extra_rects[i].y + extra_rects[i].h);

    // Allocate packing character data and flag packed characters buffer as non-packed (x0=y0=x1=y1=0)
    int buf_packedchars_n = 0;
    int buf_rects_n = 0;
    int buf_ranges_n = 0;
    auto* buf_packedchars = (stbtt_packedchar*)GpaAlloc((usize)total_glyph_count * sizeof(stbtt_packedchar));
    auto* buf_rects = (stbrp_rect*)GpaAlloc((usize)total_glyph_count * sizeof(stbrp_rect));
    auto* buf_ranges = (stbtt_pack_range*)GpaAlloc((usize)total_glyph_range_count * sizeof(stbtt_pack_range));
    ZeroMemory(buf_packedchars, (usize)total_glyph_count * sizeof(stbtt_packedchar));
    ZeroMemory(buf_rects,
               (usize)total_glyph_count *
                   sizeof(stbrp_rect)); // Unnecessary but let's clear this for the sake of sanity.
    ZeroMemory(buf_ranges, (usize)total_glyph_range_count * sizeof(stbtt_pack_range));

    // First font pass: pack all glyphs (no rendering at this point, we are working with rectangles in an
    // infinitely tall texture at this point)
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];

        // Setup ranges
        int glyph_count = 0;
        int glyph_ranges_count = 0;
        for (auto const& glyph_range : cfg.glyph_ranges) {
            glyph_count += (glyph_range.end - glyph_range.start) + 1;
            glyph_ranges_count++;
        }
        tmp.ranges = buf_ranges + buf_ranges_n;
        tmp.ranges_count = glyph_ranges_count;
        buf_ranges_n += glyph_ranges_count;
        for (int i = 0; i < glyph_ranges_count; i++) {
            auto const in_range = cfg.glyph_ranges[(u32)i];
            stbtt_pack_range& range = tmp.ranges[i];
            range.font_size = cfg.size_pixels;
            range.first_unicode_codepoint_in_range = in_range.start;
            range.num_chars = (in_range.end - in_range.start) + 1;
            range.chardata_for_range = buf_packedchars + buf_packedchars_n;
            buf_packedchars_n += range.num_chars;
        }

        // Pack
        tmp.rects = buf_rects + buf_rects_n;
        buf_rects_n += glyph_count;
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        int const n =
            stbtt_PackFontRangesGatherRects(&spc, &tmp.font_info, tmp.ranges, tmp.ranges_count, tmp.rects);
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
    tex_pixels_alpha8 = (unsigned char*)GpaAlloc((usize)(tex_width * tex_height));
    ZeroMemory(tex_pixels_alpha8, (usize)(tex_width * tex_height));
    spc.pixels = tex_pixels_alpha8;
    spc.height = tex_height;

    // Second pass: render characters
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        stbtt_PackFontRangesRenderIntoRects(&spc, &tmp.font_info, tmp.ranges, tmp.ranges_count, tmp.rects);
        tmp.rects = nullptr;
    }

    // End packing
    stbtt_PackEnd(&spc);
    GpaFree(buf_rects);
    buf_rects = nullptr;

    // Third pass: setup Font and glyphs for runtime
    for (int input_i = 0; input_i < config_data.size; input_i++) {
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

        dst_font->fallback_glyph = nullptr; // Always clear fallback so FindGlyph can return nullptr. It will
                                            // be set again in BuildLookupTable()
        for (int i = 0; i < tmp.ranges_count; i++) {
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
    GpaFree(buf_packedchars);
    GpaFree(buf_ranges);
    GpaFree(tmp_array);

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

    Vector<stbrp_rect>& rects = *(Vector<stbrp_rect>*)p_rects;
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
    int max_codepoint = 0;
    for (int i = 0; i != glyphs.size; i++)
        max_codepoint = Max(max_codepoint, (int)glyphs[i].codepoint);

    ASSERT(glyphs.size < 0xFFFF);
    index_x_advance.Clear();
    index_lookup.Clear();
    GrowIndex(max_codepoint + 1);
    for (int i = 0; i < glyphs.size; i++) {
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
        index_x_advance[(int)tab_glyph.codepoint] = tab_glyph.x_advance;
        index_lookup[(int)tab_glyph.codepoint] = (Char16)(glyphs.size - 1);
    }

    fallback_glyph = nullptr;
    fallback_glyph = FindGlyph(k_fallback_char);
    fallback_x_advance = fallback_glyph ? fallback_glyph->x_advance : 0.0f;
    for (int i = 0; i < max_codepoint + 1; i++)
        if (index_x_advance[i] < 0.0f) index_x_advance[i] = fallback_x_advance;
}

void Font::GrowIndex(int new_size) {
    ASSERT_EQ(index_x_advance.size, index_lookup.size);
    int const old_size = index_lookup.size;
    if (new_size <= old_size) return;
    index_x_advance.Resize(new_size);
    index_lookup.Resize(new_size);
    for (int i = old_size; i < new_size; i++) {
        index_x_advance[i] = -1.0f;
        index_lookup[i] = k_invalid_codepoint;
    }
}

void Font::AddRemapChar(Char16 dst, Char16 src, bool overwrite_dst) {
    ASSERT(index_lookup.size > 0); // Currently this can only be called AFTER the font has been built, aka
                                   // after calling FontAtlas::GetTexDataAs*() function.
    int const index_size = index_lookup.size;

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
    // Simple word-wrapping for English, not full-featured. Please submit failing cases!
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

        f32 const char_width =
            ((int)c < index_x_advance.size ? index_x_advance[(int)c] : fallback_x_advance) * scale;
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

f32x2 Font::CalcTextSizeA(f32 size, f32 max_width, f32 wrap_width, String str, char const** remaining) const {
    auto text_begin = str.data;
    auto text_end = End(str);

    f32 const line_height = size;
    f32 const scale = size / font_size;

    auto text_size = f32x2 {0, 0};
    f32 line_width = 0.0f;

    bool const word_wrap_enabled = (wrap_width > 0.0f);
    char const* word_wrap_eol = nullptr;

    char const* s = text_begin;
    while (s < text_end) {
        if (word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code
            // simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, wrap_width - line_width);
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

        f32 const char_width =
            ((int)c < index_x_advance.size ? index_x_advance[(int)c] : fallback_x_advance) * scale;
        if (line_width + char_width >= max_width) {
            s = prev_s;
            break;
        }

        line_width += char_width;
    }

    if (text_size.x < line_width) text_size.x = line_width;

    if (line_width > 0 || text_size.y == 0.0f) text_size.y += line_height;

    if (remaining) *remaining = s;

    return text_size;
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
                      char const* text_begin,
                      char const* text_end,
                      f32 wrap_width,
                      bool cpu_fine_clip) const {
    if (!text_end) text_end = text_begin + NullTerminatedSize(text_begin);

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
    char const* s = text_begin;
    if (!word_wrap_enabled && y + line_height < clip_rect.y)
        while (s < text_end && *s != '\n') // Fast-forward to next line
            s++;

    // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
    int const vtx_count_max = (int)(text_end - s) * 4;
    int const idx_count_max = (int)(text_end - s) * 6;
    int const idx_expected_size = draw_list->idx_buffer.size + idx_count_max;
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
    draw_list->vtx_buffer.Resize((int)(vtx_write - draw_list->vtx_buffer.data));
    draw_list->idx_buffer.Resize((int)(idx_write - draw_list->idx_buffer.data));
    draw_list->cmd_buffer[draw_list->cmd_buffer.size - 1].elem_count -=
        (unsigned)(idx_expected_size - draw_list->idx_buffer.size);
    draw_list->vtx_write_ptr = vtx_write;
    draw_list->idx_write_ptr = idx_write;
    draw_list->vtx_current_idx = (unsigned int)draw_list->vtx_buffer.size;
}

} // namespace graphics
