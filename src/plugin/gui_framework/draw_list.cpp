// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "draw_list.hpp"

#include "fonts.hpp"
#include "gui_framework/colours.hpp"

constexpr f32x4 k_null_clip_rect {-8192.0f, -8192.0f, +8192.0f, +8192.0f};

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
    for (u32 i = 0; i < channels.size; i++) {
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
    return l.texture_id_stack.size ? l.texture_id_stack.data[l.texture_id_stack.size - 1]
                                   : l.renderer.invalid_texture;
}

void DrawList::AddDrawCmd() {
    DrawCmd draw_cmd;
    draw_cmd.clip_rect = GetCurrentClipRect(*this);
    draw_cmd.texture_id = GetCurrentTextureId(*this);

    ASSERT(draw_cmd.clip_rect.x <= draw_cmd.clip_rect.z && draw_cmd.clip_rect.y <= draw_cmd.clip_rect.w);
    cmd_buffer.PushBack(draw_cmd);
}

// Our scheme may appears a bit unusual, basically we want the most-common calls AddLine AddRect etc. to not
// have to perform any check so we always have a command ready in the stack. The cost of figuring out if a new
// command has to be added or if we can merge is paid in those Update** functions only.
void DrawList::UpdateClipRect() {
    // If current command is used with different settings we need to add a new command
    f32x4 const curr_clip_rect = GetCurrentClipRect(*this);
    DrawCmd* curr_cmd = cmd_buffer.size > 0 ? &cmd_buffer.data[cmd_buffer.size - 1] : nullptr;
    if (!curr_cmd ||
        (curr_cmd->elem_count != 0 && !MemoryIsEqual(&curr_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)))) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    if (curr_cmd->elem_count == 0 && prev_cmd &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)) &&
        prev_cmd->texture_id == GetCurrentTextureId(*this))
        cmd_buffer.PopBack();
    else
        curr_cmd->clip_rect = curr_clip_rect;
}

void DrawList::UpdateTexturePtr() {
    // If current command is used with different settings we need to add a new command
    TextureHandle const curr_texture_id = GetCurrentTextureId(*this);
    DrawCmd* curr_cmd = cmd_buffer.size ? &cmd_buffer.Back() : nullptr;
    if (!curr_cmd || (curr_cmd->elem_count != 0 && curr_cmd->texture_id != curr_texture_id)) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    auto const curr_clip_rect = GetCurrentClipRect(*this);
    if (prev_cmd && prev_cmd->texture_id == curr_texture_id &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)))
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

void DrawList::ChannelsSplit(u32 chans) {
    ASSERT(channels_current == 0 && chans == 1);
    auto const old_channels_count = channels.size;
    if (old_channels_count < chans) channels.Resize(chans);
    channels_count = chans;

    // _Channels[] (24 bytes each) hold storage that we'll swap with this->_CmdBuffer/_IdxBuffer
    // The content of _Channels[0] at this point doesn't matter. We clear it to make state tidy in a debugger
    // but we don't strictly need to. When we switch to the next channel, we'll copy _CmdBuffer/_IdxBuffer
    // into _Channels[0] and then _Channels[1] into _CmdBuffer/_IdxBuffer
    ZeroMemory(&channels[0], sizeof(DrawChannel));
    for (u32 i = 1; i < channels_count; i++) {
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

    u32 new_cmd_buffer_count = 0;
    u32 new_idx_buffer_count = 0;
    for (u32 i = 1; i < channels_count; i++) {
        auto& ch = channels[i];
        if (ch.cmd_buffer.size && ch.cmd_buffer.Back().elem_count == 0) ch.cmd_buffer.PopBack();
        new_cmd_buffer_count += ch.cmd_buffer.size;
        new_idx_buffer_count += ch.idx_buffer.size;
    }
    cmd_buffer.Resize(cmd_buffer.size + new_cmd_buffer_count);
    idx_buffer.Resize(idx_buffer.size + new_idx_buffer_count);

    auto cmd_write = cmd_buffer.data + cmd_buffer.size - new_cmd_buffer_count;
    idx_write_ptr = idx_buffer.data + idx_buffer.size - new_idx_buffer_count;
    for (u32 i = 1; i < channels_count; i++) {
        DrawChannel const& ch = channels[i];
        if (auto const sz = ch.cmd_buffer.size) {
            CopyMemory(cmd_write, ch.cmd_buffer.data, (usize)sz * sizeof(DrawCmd));
            cmd_write += sz;
        }
        if (auto const sz = ch.idx_buffer.size) {
            CopyMemory(idx_write_ptr, ch.idx_buffer.data, (usize)sz * sizeof(DrawIdx));
            idx_write_ptr += sz;
        }
    }
    AddDrawCmd();
    channels_count = 1;
}

void DrawList::ChannelsSetCurrent(u32 idx) {
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
void DrawList::PrimReserve(u32 idx_count, u32 vtx_count) {
    auto& draw_cmd = cmd_buffer.data[cmd_buffer.size - 1];
    draw_cmd.elem_count += (unsigned)idx_count;

    auto const vtx_buffer_size = vtx_buffer.size;
    vtx_buffer.Resize(vtx_buffer_size + vtx_count);
    vtx_write_ptr = vtx_buffer.data + vtx_buffer_size;

    auto const idx_buffer_size = idx_buffer.size;
    idx_buffer.Resize(idx_buffer_size + idx_count);
    idx_write_ptr = idx_buffer.data + idx_buffer_size;
}

// Fully unrolled with inline call to keep our debug builds decently fast.
void DrawList::PrimRect(f32x2 a, f32x2 c, u32 col) {
    f32x2 const b {c.x, a.y};
    f32x2 const d {a.x, c.y};
    f32x2 const uv(fonts.atlas.tex_uv_white_pixel);
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

void DrawList::PrimRectUV(f32x2 a, f32x2 c, f32x2 uv_a, f32x2 uv_c, u32 col) {
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

void DrawList::PrimQuadUV(f32x2 a,
                          f32x2 b,
                          f32x2 c,
                          f32x2 d,
                          f32x2 uv_a,
                          f32x2 uv_b,
                          f32x2 uv_c,
                          f32x2 uv_d,
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

static inline f32 InvLength(f32x2 lhs, f32 fail_value) {
    f32 const d = (lhs.x * lhs.x) + (lhs.y * lhs.y);
    if (d > 0.0f) return 1.0f / Sqrt(d);
    return fail_value;
}

// IMPROVE: Thickness anti-aliased lines cap are missing their AA fringe.
void DrawList::AddPolyline(Span<f32x2 const> points_span,
                           u32 col,
                           bool closed,
                           f32 thickness,
                           bool anti_aliased) {
    auto const points = points_span.data;
    auto const points_count = (u32)points_span.size;
    if (points_count < 2) return;

    f32x2 const uv = fonts.atlas.tex_uv_white_pixel;
    anti_aliased &= renderer.anti_aliased_lines;

    auto count = points_count;
    if (!closed) count = points_count - 1;

    bool const thick_line = thickness > 1.0f;
    if (anti_aliased) {
        // Anti-aliased stroke
        // const f32 AA_SIZE = 1.0f;
        f32 const aa_size = renderer.stroke_anti_alias;
        u32 const col_trans = col & Rgba(255, 255, 255, 0);

        auto const idx_count = thick_line ? count * 18 : count * 12;
        auto const vtx_count = thick_line ? points_count * 4 : points_count * 3;
        PrimReserve(idx_count, vtx_count);

        // Temporary buffer
        auto* temp_normals =
            (f32x2*)__builtin_alloca((unsigned)points_count * (thick_line ? 5 : 3) * sizeof(f32x2));
        f32x2* temp_points = temp_normals + points_count;

        for (u32 i1 = 0; i1 < count; i1++) {
            auto const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
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
            auto idx1 = vtx_current_idx;
            for (u32 i1 = 0; i1 < count; i1++) {
                auto const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                auto const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 3;

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
            for (u32 i = 0; i < points_count; i++) {
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
            auto idx1 = vtx_current_idx;
            for (u32 i1 = 0; i1 < count; i1++) {
                auto const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                auto const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 4;

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
            for (u32 i = 0; i < points_count; i++) {
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
        auto const idx_count = count * 6;
        auto const vtx_count = count * 4; // FIXME-OPT: Not sharing edges
        PrimReserve(idx_count, vtx_count);

        for (u32 i1 = 0; i1 < count; i1++) {
            auto const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            f32x2 p1 = points[i1];
            f32x2 p2 = points[i2];
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

void DrawList::AddConvexPolyFilled(Span<f32x2 const> points_span, u32 col, bool anti_aliased) {
    auto const points = points_span.data;
    auto const points_count = (u32)points_span.size;
    f32x2 const uv = fonts.atlas.tex_uv_white_pixel;
    anti_aliased &= renderer.anti_aliased_shapes;

    if (anti_aliased) {
        // Anti-aliased Fill
        auto const aa_size = renderer.fill_anti_alias;
        auto const col_trans = col & Rgba(255, 255, 255, 0);
        auto const idx_count = ((points_count - 2) * 3) + (points_count * 6);
        auto const vtx_count = (points_count * 2);
        PrimReserve(idx_count, vtx_count);

        // Add indexes for fill
        auto const vtx_inner_idx = vtx_current_idx;
        auto const vtx_outer_idx = vtx_current_idx + 1;
        for (u32 i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_inner_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_inner_idx + (((unsigned)i - 1) << 1));
            idx_write_ptr[2] = (DrawIdx)(vtx_inner_idx + ((unsigned)i << 1));
            idx_write_ptr += 3;
        }

        // Compute normals
        auto* temp_normals = (f32x2*)__builtin_alloca((unsigned)points_count * sizeof(f32x2));
        for (u32 i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            f32x2 p0 = points[i0];
            f32x2 p1 = points[i1];
            f32x2 diff = p1 - p0;
            diff *= InvLength(diff, 1.0f);
            temp_normals[i0].x = diff.y;
            temp_normals[i0].y = -diff.x;
        }

        for (u32 i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            // Average normals
            f32x2 n0 = temp_normals[i0];
            f32x2 n1 = temp_normals[i1];
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
        auto const idx_count = (points_count - 2) * 3;
        auto const vtx_count = points_count;
        PrimReserve(idx_count, vtx_count);
        for (u32 i = 0; i < vtx_count; i++) {
            vtx_write_ptr[0].pos = points[i];
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col;
            vtx_write_ptr++;
        }
        for (u32 i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + (unsigned)i - 1);
            idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + (unsigned)i);
            idx_write_ptr += 3;
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    }
}

void DrawList::PathArcToFast(f32x2 centre, f32 radius, u32 amin, u32 amax) {
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
        for (auto a = amin; a <= amax; a++) {
            f32x2 c = circle_vtx[a % circle_vtx_count];
            path.PushBack(f32x2 {centre.x + (c.x * radius), centre.y + (c.y * radius)});
        }
    }
}

void DrawList::PathArcTo(f32x2 centre, f32 radius, f32 amin, f32 amax, u32 num_segments) {
    if (radius == 0.0f) path.PushBack(centre);
    path.Reserve(path.size + (num_segments + 1));
    for (u32 i = 0; i <= num_segments; i++) {
        f32 const a = amin + (((f32)i / (f32)num_segments) * (amax - amin));
        path.PushBack(f32x2 {centre.x + (Cos(a) * radius), centre.y + (Sin(a) * radius)});
    }
}

static void PathBezierToCasteljau(BasicDynamicArray<f32x2>* path,
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

void DrawList::PathBezierCurveTo(f32x2 p2, f32x2 p3, f32x2 p4, u32 num_segments) {
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
                              renderer.curve_tessellation_tol,
                              0);
    } else {
        f32 const t_step = 1.0f / (f32)num_segments;
        for (u32 i_step = 1; i_step <= num_segments; i_step++) {
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

void DrawList::PathRect(f32x2 a, f32x2 b, f32 rounding, u4 rounding_corners) {
    constexpr u4 k_bottom_left = 8;
    constexpr u4 k_bottom_right = 4;
    constexpr u4 k_top_right = 2;
    constexpr u4 k_top_left = 1;

    f32 r = rounding;
    r = Min(r,
            (Fabs(b.x - a.x) *
             (((rounding_corners & (k_bottom_left | k_bottom_right)) == (k_bottom_left | k_bottom_right)) ||
                      ((rounding_corners & (k_top_right | k_top_left)) == (k_top_right | k_top_left))
                  ? 0.5f
                  : 1.0f)) -
                1.0f);
    r = Min(r,
            (Fabs(b.y - a.y) *
             (((rounding_corners & (k_bottom_left | k_top_left)) == (k_bottom_left | k_top_left)) ||
                      ((rounding_corners & (k_bottom_right | k_top_right)) == (k_bottom_right | k_top_right))
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
        f32 const r0 = (rounding_corners & k_bottom_left) ? r : 0.0f;
        f32 const r1 = (rounding_corners & k_bottom_right) ? r : 0.0f;
        f32 const r2 = (rounding_corners & k_top_right) ? r : 0.0f;
        f32 const r3 = (rounding_corners & k_top_left) ? r : 0.0f;
        PathArcToFast(f32x2 {a.x + r0, a.y + r0}, r0, 6, 9);
        PathArcToFast(f32x2 {b.x - r1, a.y + r1}, r1, 9, 12);
        PathArcToFast(f32x2 {b.x - r2, b.y - r2}, r2, 0, 3);
        PathArcToFast(f32x2 {a.x + r3, b.y - r3}, r3, 3, 6);
    }
}

void DrawList::AddLine(f32x2 a, f32x2 b, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathLineTo(a + f32x2 {0.5f, 0.5f});
    PathLineTo(b + f32x2 {0.5f, 0.5f});
    PathStroke(col, false, thickness);
}

void DrawList::AddNonAABox(f32x2 a, f32x2 b, u32 col, f32 thickness) {
    f32x2 p1 = a;
    auto const p2 = f32x2 {b.x, a.y};
    f32x2 p3 = b;
    auto const p4 = f32x2 {a.x, b.y};

    f32x2 const uv = fonts.atlas.tex_uv_white_pixel;

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
void DrawList::AddRect(f32x2 a, f32x2 b, u32 col, f32 rounding, u4 rounding_corners_flags, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathRect(a + f32x2 {0.5f, 0.5f}, b - f32x2 {0.5f, 0.5f}, rounding, rounding_corners_flags);
    PathStroke(col, true, thickness);
}

void DrawList::AddRectFilled(f32x2 a, f32x2 b, u32 col, f32 rounding, u4 rounding_corners_flags) {
    if ((col & k_alpha_mask) == 0) return;
    if (rounding > 0.0f) {
        PathRect(a, b, rounding, rounding_corners_flags);
        PathFill(col);
    } else {
        PrimReserve(6, 4);
        PrimRect(a, b, col);
    }
}

void DrawList::AddRectFilledMultiColor(f32x2 a,
                                       f32x2 c,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = fonts.atlas.tex_uv_white_pixel;
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

void DrawList::AddQuadFilledMultiColor(f32x2 upr_left,
                                       f32x2 upr_right,
                                       f32x2 bot_right,
                                       f32x2 bot_left,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = fonts.atlas.tex_uv_white_pixel;
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

void DrawList::AddVignetteRect(Rect r, u32 colour, f32 inner_radius_fraction, u32 subdivisions) {
    if ((colour & k_alpha_mask) == 0) return;
    if (subdivisions == 0) return;

    auto const base_alpha = (f32)((colour >> k_alpha_shift) & 0xFF);
    auto const colour_no_alpha = colour & k_alpha_mask_inv;

    // Compute vignette alpha for a point based on its elliptical distance from centre
    auto const inv_range = 1.0f / Max(1.0f - inner_radius_fraction, 0.001f);
    auto vignette_colour = [&](f32 nx, f32 ny) -> u32 {
        // Elliptical distance normalised to 0-1 (0 = centre, 1 = edge)
        auto const dist = Sqrt((nx * nx) + (ny * ny));
        // Smoothstep falloff
        auto t = Clamp((dist - inner_radius_fraction) * inv_range, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        auto const a = (u8)(base_alpha * t);
        return colour_no_alpha | ((u32)a << k_alpha_shift);
    };

    // Build a grid of positions and colours. Each vertex gets alpha based on its actual
    // distance from centre, so there are no seam artifacts at diagonals.
    auto const n = subdivisions + 1;
    auto const total_verts = n * n;
    auto* cols = (u32*)__builtin_alloca(total_verts * sizeof(u32));
    auto* positions = (f32x2*)__builtin_alloca(total_verts * sizeof(f32x2));

    for (u32 yi = 0; yi < n; ++yi) {
        auto const fy = (f32)yi / (f32)(n - 1);
        auto const ny = (fy * 2.0f) - 1.0f; // -1 to 1
        auto const py = r.y + (r.h * fy);

        for (u32 xi = 0; xi < n; ++xi) {
            auto const fx = (f32)xi / (f32)(n - 1);
            auto const nx = (fx * 2.0f) - 1.0f; // -1 to 1
            auto const idx = (yi * n) + xi;
            positions[idx] = {r.x + (r.w * fx), py};
            cols[idx] = vignette_colour(nx, ny);
        }
    }

    // Draw quads between grid points
    for (u32 yi = 0; yi < subdivisions; ++yi) {
        for (u32 xi = 0; xi < subdivisions; ++xi) {
            auto const tl = (yi * n) + xi;
            auto const tr = tl + 1;
            auto const bl = tl + n;
            auto const br = bl + 1;

            // Skip fully transparent quads
            if (((cols[tl] | cols[tr] | cols[bl] | cols[br]) & k_alpha_mask) == 0) continue;

            AddQuadFilledMultiColor(positions[tl],
                                    positions[tr],
                                    positions[br],
                                    positions[bl],
                                    cols[tl],
                                    cols[tr],
                                    cols[br],
                                    cols[bl]);
        }
    }
}

void DrawList::AddQuad(f32x2 a, f32x2 b, f32x2 c, f32x2 d, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathStroke(col, true, thickness);
}

void DrawList::AddQuadFilled(f32x2 a, f32x2 b, f32x2 c, f32x2 d, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathFill(col);
}

void DrawList::AddTriangle(f32x2 a, f32x2 b, f32x2 c, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathStroke(col, true, thickness);
}

void DrawList::AddTriangleFilled(f32x2 a, f32x2 b, f32x2 c, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathFill(col);
}

void DrawList::AddCircle(f32x2 centre, f32 radius, u32 col, u32 num_segments, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius - 0.5f, 0.0f, a_max, num_segments);
    PathStroke(col, true, thickness);
}

void DrawList::AddCircleFilled(f32x2 centre, f32 radius, u32 col, u32 num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius, 0.0f, a_max, num_segments);
    PathFill(col);
}

void DrawList::AddBezierCurve(f32x2 pos0,
                              f32x2 cp0,
                              f32x2 cp1,
                              f32x2 pos1,
                              u32 col,
                              f32 thickness,
                              u32 num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(pos0);
    PathBezierCurveTo(cp0, cp1, pos1, num_segments);
    PathStroke(col, false, thickness);
}

static f32x2 CalcTextPosition(Font* font,
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
            auto const width = font->CalcTextSize(text, {.font_size = font_size}).x;
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
    auto constexpr k_epsilon = 1.0f;

    if (args.overflow_type != TextOverflowType::AllowOverflow) {
        auto const text_width = args.text_size
                                    ? args.text_size->x
                                    : args.font->CalcTextSize(args.str, {.font_size = args.font_size}).x;
        if (text_width > (args.r.w + k_epsilon)) {
            f32 const dots_size = args.font->CalcTextSize(k_dots, {.font_size = args.font_size}).x;
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
                        (c < args.font->index_x_advance.size ? args.font->index_x_advance[c]
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
                        (c < args.font->index_x_advance.size ? args.font->index_x_advance[c]
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

void DrawList::AddTextInRect(Rect r, u32 col, String str, AddTextInRectOptions const& options) {
    auto const font = fonts.Current();
    auto const font_size =
        (options.font_size != 0 ? options.font_size : font->font_size) * options.font_scaling;

    ArenaAllocatorWithInlineStorage<1000> temp_allocator {Malloc::Instance()};

    f32x2 text_size {-1, -1};
    auto text_pos =
        CalcTextPosition(font, font_size, r.Min(), r.Max(), options.justification, str, &text_size);

    f32 wrap_width = options.wrap_width;

    if (options.overflow_type != TextOverflowType::AllowOverflow) {
        wrap_width = 0;
        str = OverflowText({
            .font = font,
            .font_size = font_size,
            .r = r,
            .str = str,
            .overflow_type = options.overflow_type,
            .font_scaling = options.font_scaling,
            .text_size = (text_size.x != -1) ? Optional<f32x2> {text_size} : k_nullopt,
            .allocator = temp_allocator,
            .text_pos = text_pos,
        });
    }

    font->RenderText(this, font_size, text_pos, col, clip_rect_stack.Back(), str, wrap_width, false);
}

void DrawList::AddText(f32x2 pos, u32 col, String str, AddTextOptions const& options) {
    if ((col & k_alpha_mask) == 0) return;
    if (str.size == 0) return;

    auto const clip_rect = clip_rect_stack.Back();
    auto const font = fonts.Current();
    auto const font_size =
        (options.font_size != 0 ? options.font_size : font->font_size) * options.font_scaling;
    font->RenderText(this,
                      font_size,
                      pos,
                      col,
                      clip_rect,
                      str,
                      options.wrap_width,
                      false,
                      options.multiline_alignment,
                      options.multiline_alignment_width);
}

static inline f32x2 Mul(f32x2 lhs, f32x2 rhs) { return f32x2 {lhs.x * rhs.x, lhs.y * rhs.y}; }
static inline f32 LengthSqr(f32x2 lhs) { return (lhs.x * lhs.x) + (lhs.y * lhs.y); }
static inline f32 Dot(f32x2 a, f32x2 b) { return (a.x * b.x) + (a.y * b.y); }

static void ShadeVertsLinearUV(DrawList* draw_list,
                               u32 vert_start_idx,
                               u32 vert_end_idx,
                               f32x2 a,
                               f32x2 b,
                               f32x2 uv_a,
                               f32x2 uv_b,
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
                                                     u32 vert_start_idx,
                                                     u32 vert_end_idx,
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

void DrawList::AddImage(TextureHandle user_texture_id, f32x2 a, f32x2 b, f32x2 uv0, f32x2 uv1, u32 col) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == renderer.invalid_texture) return;

    // FIXME-OPT: This is wasting draw calls.
    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    PrimReserve(6, 4);
    PrimRectUV(a, b, uv0, uv1, col);

    if (push_texture_id) PopTextureHandle();
}

void DrawList::AddImageRounded(TextureHandle user_texture_id,
                               f32x2 p_min,
                               f32x2 p_max,
                               f32x2 uv_min,
                               f32x2 uv_max,
                               u32 col,
                               f32 rounding,
                               u4 rounding_corners) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == renderer.invalid_texture) return;

    if (rounding <= 0.0f || (u8)rounding_corners == 0) {
        AddImage(user_texture_id, p_min, p_max, uv_min, uv_max, col);
        return;
    }

    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    auto const vert_start_idx = vtx_buffer.size;
    PathRect(p_min, p_max, rounding, rounding_corners);
    PathFillConvex(col);
    auto const vert_end_idx = vtx_buffer.size;
    ShadeVertsLinearUV(this, vert_start_idx, vert_end_idx, p_min, p_max, uv_min, uv_max, true);

    if (push_texture_id) PopTextureHandle();
}
