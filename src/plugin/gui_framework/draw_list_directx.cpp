// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file contains modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

// Must be first
#include <windows.h>
//
#include "os/undef_windows_macros.h"
//
#include <d3d9.h>
#include <d3d9types.h>

#include "foundation/foundation.hpp"
#include "os/misc_windows.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "draw_list.hpp"

namespace graphics {

struct CUSTOMVERTEX {
    f32 pos[3];
    D3DCOLOR col;
    f32 uv[2];
};

static String CodeToString(s64 code) {
    switch (code) {
        case D3DERR_WRONGTEXTUREFORMAT: return "WRONGTEXTUREFORMAT";
        case D3DERR_UNSUPPORTEDCOLOROPERATION: return "UNSUPPORTEDCOLOROPERATION";
        case D3DERR_UNSUPPORTEDCOLORARG: return "UNSUPPORTEDCOLORARG";
        case D3DERR_UNSUPPORTEDALPHAOPERATION: return "UNSUPPORTEDALPHAOPERATION";
        case D3DERR_UNSUPPORTEDALPHAARG: return "UNSUPPORTEDALPHAARG";
        case D3DERR_TOOMANYOPERATIONS: return "TOOMANYOPERATIONS";
        case D3DERR_CONFLICTINGTEXTUREFILTER: return "CONFLICTINGTEXTUREFILTER";
        case D3DERR_UNSUPPORTEDFACTORVALUE: return "UNSUPPORTEDFACTORVALUE";
        case D3DERR_CONFLICTINGRENDERSTATE: return "CONFLICTINGRENDERSTATE";
        case D3DERR_UNSUPPORTEDTEXTUREFILTER: return "UNSUPPORTEDTEXTUREFILTER";
        case D3DERR_CONFLICTINGTEXTUREPALETTE: return "CONFLICTINGTEXTUREPALETTE";
        case D3DERR_DRIVERINTERNALERROR: return "DRIVERINTERNALERROR";
        case D3DERR_NOTFOUND: return "NOTFOUND";
        case D3DERR_MOREDATA: return "MOREDATA";
        case D3DERR_DEVICELOST: return "DEVICELOST";
        case D3DERR_DEVICENOTRESET: return "DEVICENOTRESET";
        case D3DERR_NOTAVAILABLE: return "NOTAVAILABLE";
        case D3DERR_OUTOFVIDEOMEMORY: return "OUTOFVIDEOMEMORY";
        case D3DERR_INVALIDDEVICE: return "INVALIDDEVICE";
        case D3DERR_INVALIDCALL: return "INVALIDCALL";
        case D3DERR_DRIVERINVALIDCALL: return "DRIVERINVALIDCALL";
        case D3DERR_WASSTILLDRAWING: return "WASSTILLDRAWING";
    }
    return "";
}

static constexpr ErrorCodeCategory k_d3d_error_category = {
    .category_id = "D3",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return writer.WriteChars(CodeToString(code.code));
    },
};

#define D3DERR(code, ...)                                                                                    \
    CodeToString((s64)code).size ? ErrorCode(k_d3d_error_category, (s64)code, ##__VA_ARGS__)                 \
                                 : HresultErrorCode(code, ##__VA_ARGS__)

#define D3D_TRYV(d3d_call)                                                                                   \
    if (const auto hr_tryv = d3d_call; hr_tryv != D3D_OK) return D3DERR(hr_tryv, #d3d_call);

#define D3D_TRY_LOG_AND_RETURN(return_value, d3d_call)                                                       \
    if (const auto hr_tryv = d3d_call; hr_tryv != D3D_OK) {                                                  \
        LogError("DirectX error: {}", D3DERR(hr_tryv, #d3d_call));                                           \
        return return_value;                                                                                 \
    }

struct DirectXDrawContext : public DrawContext {
    ErrorCodeOr<void> CreateDeviceObjects(void* hwnd) override {
        Trace(ModuleName::Gui);
        ASSERT(hwnd);

        render_count = 0;
        p_d3_d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!p_d3_d) return D3DERR(E_FAIL, "Direct3DCreate9");

        bool success = false;
        DEFER {
            if (!success && p_d3_d) {
                p_d3_d->Release();
                p_d3_d = nullptr;
            }
        };

        d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
        d3dpp.EnableAutoDepthStencil = TRUE;
        d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Present with vsync

        D3D_TRYV(p_d3_d->CreateDevice(D3DADAPTER_DEFAULT,
                                      D3DDEVTYPE_HAL,
                                      (HWND)hwnd,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                      &d3dpp,
                                      &pd3d_device));

        D3DADAPTER_IDENTIFIER9 info = {};
        HRESULT result = p_d3_d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &info);
        if (result == S_OK) {
            dyn::Clear(graphics_device_info);

            fmt::Append(graphics_device_info, "Driver: {}\n", FromNullTerminated(info.Driver));
            fmt::Append(graphics_device_info, "Description: {}\n", FromNullTerminated(info.Description));
            fmt::Append(graphics_device_info, "DeviceName: {}\n", FromNullTerminated(info.DeviceName));
            fmt::Append(graphics_device_info, "Product {}\n", HIWORD(info.DriverVersion.HighPart));
            fmt::Append(graphics_device_info, "Version {}\n", LOWORD(info.DriverVersion.HighPart));
            fmt::Append(graphics_device_info, "SubVersion {}\n", HIWORD(info.DriverVersion.LowPart));
            fmt::Append(graphics_device_info, "Build {}\n", LOWORD(info.DriverVersion.LowPart));
            fmt::Append(graphics_device_info, "VendorId: {}\n", info.VendorId);
            fmt::Append(graphics_device_info, "DeviceId: {}\n", info.DeviceId);
            fmt::Append(graphics_device_info, "SubSysId: {}\n", info.SubSysId);
            fmt::Append(graphics_device_info, "Revision: {}\n", info.Revision);
            fmt::Append(graphics_device_info, "WHQLLevel: {}\n", info.WHQLLevel);
        }

        success = true;
        return k_success;
    }

    void DestroyDeviceObjects() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        if (!pd3d_device) return;
        if (p_vb) {
            p_vb->Release();
            p_vb = nullptr;
        }
        if (p_ib) {
            p_ib->Release();
            p_ib = nullptr;
        }
        DestroyFontTexture();
        DestroyAllTextures();

        if (pd3d_device) pd3d_device->Release();
        if (p_d3_d) p_d3_d->Release();
        pd3d_device = nullptr;
        p_d3_d = nullptr;
    }

    ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        LPDIRECT3DTEXTURE9 texture = nullptr;
        bool success = false;

        D3D_TRYV(pd3d_device->CreateTexture(size.width,
                                            size.height,
                                            1,
                                            D3DUSAGE_DYNAMIC,
                                            D3DFMT_A8R8G8B8,
                                            D3DPOOL_DEFAULT,
                                            &texture,
                                            nullptr));
        DEFER {
            if (!success && texture) {
                texture->Release();
                texture = nullptr;
            }
        };

        D3DLOCKED_RECT locked_rect;
        D3D_TRYV(texture->LockRect(0, &locked_rect, nullptr, 0));
        DEFER { texture->UnlockRect(0); };

        if (bytes_per_pixel == 4) {
            // Convert RGBA to BGRA for D3DFMT_A8R8G8B8
            for (u16 y = 0; y < size.height; y++) {
                for (auto const w : Range(size.width)) {
                    auto write_index = (y * locked_rect.Pitch) + (w * 4);
                    auto read_index = (y * (bytes_per_pixel * size.width)) + (w * bytes_per_pixel);

                    ((unsigned char*)locked_rect.pBits)[write_index + 0] = data[read_index + 2]; // B
                    ((unsigned char*)locked_rect.pBits)[write_index + 1] = data[read_index + 1]; // G
                    ((unsigned char*)locked_rect.pBits)[write_index + 2] = data[read_index + 0]; // R
                    ((unsigned char*)locked_rect.pBits)[write_index + 3] = data[read_index + 3]; // A
                }
            }
        } else if (bytes_per_pixel == 3) {
            // Convert RGB to BGRA for D3DFMT_A8R8G8B8
            for (u16 y = 0; y < size.height; y++) {
                for (auto const w : Range(size.width)) {
                    auto write_index = (y * locked_rect.Pitch) + (w * 4);
                    auto read_index = (y * (bytes_per_pixel * size.width)) + (w * bytes_per_pixel);

                    ((unsigned char*)locked_rect.pBits)[write_index + 0] = data[read_index + 2]; // B
                    ((unsigned char*)locked_rect.pBits)[write_index + 1] = data[read_index + 1]; // G
                    ((unsigned char*)locked_rect.pBits)[write_index + 2] = data[read_index + 0]; // R
                    ((unsigned char*)locked_rect.pBits)[write_index + 3] = 255;
                }
            }
        } else {
            PanicIfReached();
        }

        success = true;
        return (TextureHandle)texture;
    }

    void DestroyTexture(TextureHandle& id) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        auto texture = (LPDIRECT3DTEXTURE9)id;
        if (texture) {
            auto const ref_count = texture->Release();
            if (ref_count != 0)
                LogWarning(ModuleName::Gui, "DestroyTexture: unexpected ref count: {}", ref_count);
        }
        texture = nullptr;
        id = nullptr;
    }

    ErrorCodeOr<void> CreateFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        ASSERT(font_texture == nullptr);
        ASSERT(fonts.fonts.size > 0);
        bool success = false;

        // Build texture atlas
        unsigned char* pixels {};
        int width {};
        int height {};
        int bytes_per_pixel {};
        fonts.GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
        ASSERT(pixels != nullptr);
        ASSERT(NumberCastIsSafe<u16>(width));
        ASSERT(NumberCastIsSafe<u16>(height));
        ASSERT(NumberCastIsSafe<u16>(bytes_per_pixel));

        D3D_TRYV(pd3d_device->CreateTexture((u16)width,
                                            (u16)height,
                                            1,
                                            D3DUSAGE_DYNAMIC,
                                            D3DFMT_A8R8G8B8,
                                            D3DPOOL_DEFAULT,
                                            &font_texture,
                                            nullptr));
        DEFER {
            if (!success && font_texture) {
                font_texture->Release();
                font_texture = nullptr;
            }
        };

        D3DLOCKED_RECT tex_locked_rect;
        D3D_TRYV(font_texture->LockRect(0, &tex_locked_rect, nullptr, 0));
        // Convert RGBA to BGRA for D3DFMT_A8R8G8B8
        for (u16 y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                auto write_index = (y * tex_locked_rect.Pitch) + (x * 4);
                auto read_index = (y * width * bytes_per_pixel) + (x * bytes_per_pixel);

                ((unsigned char*)tex_locked_rect.pBits)[write_index + 0] = pixels[read_index + 2]; // B
                ((unsigned char*)tex_locked_rect.pBits)[write_index + 1] = pixels[read_index + 1]; // G
                ((unsigned char*)tex_locked_rect.pBits)[write_index + 2] = pixels[read_index + 0]; // R
                ((unsigned char*)tex_locked_rect.pBits)[write_index + 3] = pixels[read_index + 3]; // A
            }
        }
        auto r = font_texture->UnlockRect(0);
        ASSERT_EQ(r, D3D_OK);

        // Store our identifier
        fonts.tex_id = (void*)font_texture;

        fonts.ClearTexData();

        success = true;
        return k_success;
    }

    void DestroyFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        if (font_texture) {
            auto const ref_count = font_texture->Release();
            if (ref_count != 0)
                LogWarning(ModuleName::Gui, "DestroyFontTexture: unexpected ref count: {}", ref_count);
            fonts.tex_id = nullptr;
            font_texture = nullptr;
        }
        fonts.Clear();
    }

    ErrorCodeOr<void> Render(DrawData draw_data, UiSize window_size) override {
        ZoneScoped;
        auto constexpr k_d3_dfvf_customvertex = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

        // Rendering
        pd3d_device->SetRenderState(D3DRS_ZENABLE, false);
        pd3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
        pd3d_device->SetRenderState(D3DRS_SCISSORTESTENABLE, false);

        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(0, 0, 0, 255);
        D3D_TRYV(pd3d_device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0));
        {
            D3D_TRYV(pd3d_device->BeginScene());
            DEFER { pd3d_device->EndScene(); };

            // Calculate total vertex and index counts
            int total_vtx_count = 0;
            int total_idx_count = 0;
            for (auto const& draw_list : draw_data.draw_lists) {
                total_vtx_count += draw_list->vtx_buffer.size;
                total_idx_count += draw_list->idx_buffer.size;
            }

            // Create and grow buffers if needed
            if (!p_vb || vertex_buffer_size < total_vtx_count) {
                if (p_vb) {
                    p_vb->Release();
                    p_vb = nullptr;
                }
                vertex_buffer_size = total_vtx_count + 5000;
                D3D_TRYV(
                    pd3d_device->CreateVertexBuffer((UINT)vertex_buffer_size * (UINT)sizeof(CUSTOMVERTEX),
                                                    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                                    k_d3_dfvf_customvertex,
                                                    D3DPOOL_DEFAULT,
                                                    &p_vb,
                                                    nullptr));
            }
            if (!p_ib || index_buffer_size < total_idx_count) {
                if (p_ib) {
                    p_ib->Release();
                    p_ib = nullptr;
                }
                index_buffer_size = total_idx_count + 10000;
                D3D_TRYV(
                    pd3d_device->CreateIndexBuffer((UINT)index_buffer_size * (UINT)sizeof(DrawIdx),
                                                   D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                                   sizeof(DrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                                   D3DPOOL_DEFAULT,
                                                   &p_ib,
                                                   nullptr));
            }

            // Backup the DX9 state
            IDirect3DStateBlock9* d3d9_state_block = nullptr;
            D3D_TRYV(pd3d_device->CreateStateBlock(D3DSBT_ALL, &d3d9_state_block));
            DEFER {
                d3d9_state_block->Apply();
                d3d9_state_block->Release();
            };

            // Copy and convert all vertices into a single contiguous buffer
            {
                CUSTOMVERTEX* vtx_dst;
                DrawIdx* idx_dst;
                D3D_TRYV(p_vb->Lock(0,
                                    (UINT)(total_vtx_count) * (UINT)(sizeof(CUSTOMVERTEX)),
                                    (void**)&vtx_dst,
                                    D3DLOCK_DISCARD));
                DEFER { p_vb->Unlock(); };

                D3D_TRYV(p_ib->Lock(0,
                                    (UINT)(total_idx_count) * (UINT)(sizeof(DrawIdx)),
                                    (void**)&idx_dst,
                                    D3DLOCK_DISCARD));
                DEFER { p_ib->Unlock(); };

                for (auto const& draw_list : draw_data.draw_lists) {
                    DrawVert const* vtx_src = draw_list->vtx_buffer.data;
                    for (int i = 0; i < draw_list->vtx_buffer.size; i++) {
                        vtx_dst->pos[0] = vtx_src->pos.x;
                        vtx_dst->pos[1] = vtx_src->pos.y;
                        vtx_dst->pos[2] = 0.0f;
                        vtx_dst->col = (vtx_src->col & 0xFF00FF00) | ((vtx_src->col & 0xFF0000) >> 16) |
                                       ((vtx_src->col & 0xFF) << 16); // RGBA --> ARGB for DirectX9
                        vtx_dst->uv[0] = vtx_src->uv.x;
                        vtx_dst->uv[1] = vtx_src->uv.y;
                        vtx_dst++;
                        vtx_src++;
                    }
                    memcpy(idx_dst,
                           draw_list->idx_buffer.data,
                           (usize)draw_list->idx_buffer.size * sizeof(DrawIdx));
                    idx_dst += draw_list->idx_buffer.size;
                }

                pd3d_device->SetStreamSource(0, p_vb, 0, sizeof(CUSTOMVERTEX));
                pd3d_device->SetIndices(p_ib);
                pd3d_device->SetFVF(k_d3_dfvf_customvertex);
            }

            // Setup render state: fixed-pipeline, alpha-blending, no face culling, no depth testing
            pd3d_device->SetPixelShader(nullptr);
            pd3d_device->SetVertexShader(nullptr);
            pd3d_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            pd3d_device->SetRenderState(D3DRS_LIGHTING, false);
            pd3d_device->SetRenderState(D3DRS_ZENABLE, false);
            pd3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, true);
            pd3d_device->SetRenderState(D3DRS_ALPHATESTENABLE, false);
            pd3d_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            pd3d_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            pd3d_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            pd3d_device->SetRenderState(D3DRS_SCISSORTESTENABLE, true);
            pd3d_device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
            pd3d_device->SetRenderState(D3DRS_FOGENABLE, false);
            pd3d_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            pd3d_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            pd3d_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            pd3d_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            pd3d_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            pd3d_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            pd3d_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            pd3d_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

            // Setup orthographic projection matrix
            // Being agnostic of whether <d3dx9.h> or <DirectXMath.h> can be used, we aren't relying on
            // D3DXMatrixIdentity()/D3DXMatrixOrthoOffCenterLH() or
            // DirectX::XMMatrixIdentity()/DirectX::XMMatrixOrthographicOffCenterLH()
            {
                f32 const l = 0.5f;
                f32 const r = (f32)window_size.width + 0.5f;
                f32 const t = 0.5f;
                f32 const b = (f32)window_size.height + 0.5f;
                D3DMATRIX mat_identity = {{{1.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            1.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            1.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            1.0f}}};
                D3DMATRIX mat_projection = {{{
                    2.0f / (r - l),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    2.0f / (t - b),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.5f,
                    0.0f,
                    (l + r) / (l - r),
                    (t + b) / (b - t),
                    0.5f,
                    1.0f,
                }}};
                pd3d_device->SetTransform(D3DTS_WORLD, &mat_identity);
                pd3d_device->SetTransform(D3DTS_VIEW, &mat_identity);
                pd3d_device->SetTransform(D3DTS_PROJECTION, &mat_projection);
            }

            // Render command lists
            int vtx_offset = 0;
            int idx_offset = 0;
            for (auto const& draw_list : draw_data.draw_lists) {
                for (int cmd_i = 0; cmd_i < draw_list->cmd_buffer.size; cmd_i++) {
                    DrawCmd const* pcmd = &draw_list->cmd_buffer[cmd_i];
                    if (pcmd->user_callback) {
                        pcmd->user_callback(draw_list, pcmd);
                    } else {
                        const RECT r = {(LONG)pcmd->clip_rect.x,
                                        (LONG)pcmd->clip_rect.y,
                                        (LONG)pcmd->clip_rect.z,
                                        (LONG)pcmd->clip_rect.w};
                        pd3d_device->SetTexture(0, (LPDIRECT3DTEXTURE9)pcmd->texture_id);
                        pd3d_device->SetScissorRect(&r);
                        pd3d_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
                                                          vtx_offset,
                                                          0,
                                                          (UINT)draw_list->vtx_buffer.size,
                                                          (UINT)idx_offset,
                                                          pcmd->elem_count / 3);
                    }
                    idx_offset += pcmd->elem_count;
                }
                vtx_offset += draw_list->vtx_buffer.size;
            }
        }

        if (auto const r = pd3d_device->Present(nullptr, nullptr, nullptr, nullptr); r == D3D_OK) {
            if (render_count++ == 0) LogDebug(ModuleName::Gui, "{}: first successful render", __FUNCTION__);
        } else if (r == D3DERR_DEVICELOST && pd3d_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            LogDebug(ModuleName::Gui,
                     "pd3d_device->Present returned D3DERR_DEVICELOST, we will destroy the device objects "
                     "and try again next time");
            DestroyDeviceObjects();
        } else {
            return D3DERR(r, "Present");
        }

        return k_success;
    }

    int render_count = 0;

    D3DPRESENT_PARAMETERS d3dpp = {};
    LPDIRECT3D9 p_d3_d = nullptr;

    LPDIRECT3DDEVICE9 pd3d_device = nullptr;
    LPDIRECT3DVERTEXBUFFER9 p_vb = nullptr;
    LPDIRECT3DINDEXBUFFER9 p_ib = nullptr;
    LPDIRECT3DTEXTURE9 font_texture = nullptr;
    int vertex_buffer_size = 5000, index_buffer_size = 10000;
};

DrawContext* CreateNewDrawContext() { return new DirectXDrawContext(); }

} // namespace graphics
