// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file contains modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include <windows.h> // Must be first
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
#include "fonts.hpp"
#include "renderer.hpp"

struct CUSTOMVERTEX {
    f32 pos[3];
    D3DCOLOR col;
    f32 uv[2];
};

static Optional<String> CodeToString(s64 code) {
    switch (code) {
        case D3DERR_WRONGTEXTUREFORMAT: return "WRONGTEXTUREFORMAT"_s;
        case D3DERR_UNSUPPORTEDCOLOROPERATION: return "UNSUPPORTEDCOLOROPERATION"_s;
        case D3DERR_UNSUPPORTEDCOLORARG: return "UNSUPPORTEDCOLORARG"_s;
        case D3DERR_UNSUPPORTEDALPHAOPERATION: return "UNSUPPORTEDALPHAOPERATION"_s;
        case D3DERR_UNSUPPORTEDALPHAARG: return "UNSUPPORTEDALPHAARG"_s;
        case D3DERR_TOOMANYOPERATIONS: return "TOOMANYOPERATIONS"_s;
        case D3DERR_CONFLICTINGTEXTUREFILTER: return "CONFLICTINGTEXTUREFILTER"_s;
        case D3DERR_UNSUPPORTEDFACTORVALUE: return "UNSUPPORTEDFACTORVALUE"_s;
        case D3DERR_CONFLICTINGRENDERSTATE: return "CONFLICTINGRENDERSTATE"_s;
        case D3DERR_UNSUPPORTEDTEXTUREFILTER: return "UNSUPPORTEDTEXTUREFILTER"_s;
        case D3DERR_CONFLICTINGTEXTUREPALETTE: return "CONFLICTINGTEXTUREPALETTE"_s;
        case D3DERR_DRIVERINTERNALERROR: return "DRIVERINTERNALERROR"_s;
        case D3DERR_NOTFOUND: return "NOTFOUND"_s;
        case D3DERR_MOREDATA: return "MOREDATA"_s;
        case D3DERR_DEVICELOST: return "DEVICELOST"_s;
        case D3DERR_DEVICENOTRESET: return "DEVICENOTRESET"_s;
        case D3DERR_NOTAVAILABLE: return "NOTAVAILABLE"_s;
        case D3DERR_OUTOFVIDEOMEMORY: return "OUTOFVIDEOMEMORY"_s;
        case D3DERR_INVALIDDEVICE: return "INVALIDDEVICE"_s;
        case D3DERR_INVALIDCALL: return "INVALIDCALL"_s;
        case D3DERR_DRIVERINVALIDCALL: return "DRIVERINVALIDCALL"_s;
        case D3DERR_WASSTILLDRAWING: return "WASSTILLDRAWING"_s;
    }
    return k_nullopt;
}

static constexpr ErrorCodeCategory k_d3d_error_category = {
    .category_id = "D3",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return writer.WriteChars(CodeToString(code.code).ValueOr("unknown directx error"));
    },
};

#define D3DERR(code, ...)                                                                                    \
    (CodeToString((s64)code) ? ErrorCode(k_d3d_error_category, (s64)code, ##__VA_ARGS__)                     \
                             : HresultErrorCode(code, ##__VA_ARGS__))

#define D3D_TRYV(d3d_call)                                                                                   \
    if (const auto hr_tryv = d3d_call; hr_tryv != D3D_OK) return D3DERR(hr_tryv, #d3d_call);

#define D3D_TRY_LOG_AND_RETURN(return_value, d3d_call)                                                       \
    if (const auto hr_tryv = d3d_call; hr_tryv != D3D_OK) {                                                  \
        LogError("DirectX error: {}", D3DERR(hr_tryv, #d3d_call));                                           \
        return return_value;                                                                                 \
    }

struct Direct3D9Renderer : public Renderer {
    Direct3D9Renderer() : Renderer((TextureHandle)(uintptr) nullptr) {}

    ErrorCodeOr<void> Init(UiSize, void* hwnd, void* hmodule) override {
        Trace(ModuleName::Gui);
        ASSERT(hwnd);
        (void)hmodule;

        render_count = 0;
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) return D3DERR(E_FAIL, "Direct3DCreate9");

        bool success = false;
        DEFER {
            if (!success) {
                d3d->Release();
                d3d = nullptr;
            }
        };

        present_params = {};
        present_params.Windowed = TRUE;
        present_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        present_params.BackBufferFormat = D3DFMT_UNKNOWN;
        present_params.EnableAutoDepthStencil = TRUE;
        present_params.AutoDepthStencilFormat = D3DFMT_D16;
        present_params.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Present with vsync

        D3D_TRYV(d3d->CreateDevice(D3DADAPTER_DEFAULT,
                                   D3DDEVTYPE_HAL,
                                   (HWND)hwnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &present_params,
                                   &device));

        D3DADAPTER_IDENTIFIER9 info = {};
        HRESULT result = d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &info);
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

    void Deinit() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        if (!device) return;
        if (vertex_buf) {
            vertex_buf->Release();
            vertex_buf = nullptr;
        }
        if (index_buf) {
            index_buf->Release();
            index_buf = nullptr;
        }
        DestroyFontTexture();
        DestroyAllTextures();

        if (device) device->Release();
        if (d3d) d3d->Release();
        device = nullptr;
        d3d = nullptr;
    }

    void OnResize(UiSize size, void*) override {
        ASSERT(device);

        present_params.BackBufferWidth = size.width;
        present_params.BackBufferHeight = size.height;

        if (auto const outcome = ResetDevice(); outcome.HasError())
            LogError(ModuleName::Gui, "Failed to reset device: {}", outcome.Error());
    }

    ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        LPDIRECT3DTEXTURE9 texture = nullptr;
        bool success = false;

        D3D_TRYV(device->CreateTexture(size.width,
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
        return TextureHandle {(u64)texture};
    }

    void DestroyTexture(TextureHandle& handle) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        auto texture = (LPDIRECT3DTEXTURE9)handle;
        if (texture) {
            auto const ref_count = texture->Release();
            if (ref_count != 0)
                LogWarning(ModuleName::Gui, "DestroyTexture: unexpected ref count: {}", ref_count);
        }
        texture = nullptr;
        handle = invalid_texture;
    }

    ErrorCodeOr<void> CreateFontTexture(FontAtlas& atlas) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        ASSERT(font_texture == invalid_texture);
        ASSERT(atlas.fonts.size > 0);
        bool success = false;

        // Build texture atlas
        unsigned char* pixels {};
        int width {};
        int height {};
        int bytes_per_pixel {};
        atlas.GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
        ASSERT(pixels != nullptr);
        ASSERT(NumberCastIsSafe<u16>(width));
        ASSERT(NumberCastIsSafe<u16>(height));
        ASSERT(NumberCastIsSafe<u16>(bytes_per_pixel));

        LPDIRECT3DTEXTURE9 tex = nullptr;
        D3D_TRYV(device->CreateTexture((u16)width,
                                       (u16)height,
                                       1,
                                       D3DUSAGE_DYNAMIC,
                                       D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT,
                                       &tex,
                                       nullptr));
        DEFER {
            if (!success && tex) tex->Release();
        };

        D3DLOCKED_RECT tex_locked_rect;
        D3D_TRYV(tex->LockRect(0, &tex_locked_rect, nullptr, 0));
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
        auto r = tex->UnlockRect(0);
        ASSERT_EQ(r, D3D_OK);

        font_texture = (TextureHandle)(uintptr)tex;

        atlas.ClearTexData();

        success = true;
        return k_success;
    }

    void DestroyFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        if (font_texture != invalid_texture) {
            auto tex = (LPDIRECT3DTEXTURE9)font_texture;
            auto const ref_count = tex->Release();
            if (ref_count != 0)
                LogWarning(ModuleName::Gui, "DestroyFontTexture: unexpected ref count: {}", ref_count);
            font_texture = invalid_texture;
        }
    }

    ErrorCodeOr<void> Render(Span<DrawList*> draw_lists, UiSize window_size, void*) override {
        ZoneScoped;
        auto constexpr k_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

        // Setup viewport
        D3DVIEWPORT9 vp;
        vp.X = vp.Y = 0;
        vp.Width = (DWORD)window_size.width;
        vp.Height = (DWORD)window_size.height;
        vp.MinZ = 0.0f;
        vp.MaxZ = 1.0f;
        device->SetViewport(&vp);

        // Rendering
        device->SetRenderState(D3DRS_ZENABLE, false);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, false);

        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(0, 0, 0, 255);
        D3D_TRYV(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0));
        {
            D3D_TRYV(device->BeginScene());
            DEFER { device->EndScene(); };

            // Calculate total vertex and index counts
            int total_vtx_count = 0;
            int total_idx_count = 0;
            for (auto const& draw_list : draw_lists) {
                total_vtx_count += draw_list->vtx_buffer.size;
                total_idx_count += draw_list->idx_buffer.size;
            }

            // Create and grow buffers if needed
            if (!vertex_buf || vertex_buf_size < total_vtx_count) {
                if (vertex_buf) {
                    vertex_buf->Release();
                    vertex_buf = nullptr;
                }
                vertex_buf_size = total_vtx_count + 5000;
                D3D_TRYV(device->CreateVertexBuffer((UINT)vertex_buf_size * (UINT)sizeof(CUSTOMVERTEX),
                                                    D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                                    k_fvf,
                                                    D3DPOOL_DEFAULT,
                                                    &vertex_buf,
                                                    nullptr));
            }
            if (!index_buf || index_buf_size < total_idx_count) {
                if (index_buf) {
                    index_buf->Release();
                    index_buf = nullptr;
                }
                index_buf_size = total_idx_count + 10000;
                D3D_TRYV(device->CreateIndexBuffer((UINT)index_buf_size * (UINT)sizeof(DrawIdx),
                                                   D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                                   sizeof(DrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                                   D3DPOOL_DEFAULT,
                                                   &index_buf,
                                                   nullptr));
            }

            // Copy and convert all vertices into a single contiguous buffer
            {
                CUSTOMVERTEX* vtx_dst;
                DrawIdx* idx_dst;
                D3D_TRYV(vertex_buf->Lock(0,
                                          (UINT)(total_vtx_count) * (UINT)(sizeof(CUSTOMVERTEX)),
                                          (void**)&vtx_dst,
                                          D3DLOCK_DISCARD));
                DEFER { vertex_buf->Unlock(); };

                D3D_TRYV(index_buf->Lock(0,
                                         (UINT)(total_idx_count) * (UINT)(sizeof(DrawIdx)),
                                         (void**)&idx_dst,
                                         D3DLOCK_DISCARD));
                DEFER { index_buf->Unlock(); };

                for (auto const& draw_list : draw_lists) {
                    DrawVert const* vtx_src = draw_list->vtx_buffer.data;
                    for (u32 i = 0; i < draw_list->vtx_buffer.size; i++) {
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

                device->SetStreamSource(0, vertex_buf, 0, sizeof(CUSTOMVERTEX));
                device->SetIndices(index_buf);
                device->SetFVF(k_fvf);
            }

            // Setup render state: fixed-pipeline, alpha-blending, no face culling, no depth testing
            device->SetPixelShader(nullptr);
            device->SetVertexShader(nullptr);
            device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
            device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
            device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_ZENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
            device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
            device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
            device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            device->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
            device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            device->SetRenderState(D3DRS_CLIPPING, TRUE);
            device->SetRenderState(D3DRS_LIGHTING, FALSE);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

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
                device->SetTransform(D3DTS_WORLD, &mat_identity);
                device->SetTransform(D3DTS_VIEW, &mat_identity);
                device->SetTransform(D3DTS_PROJECTION, &mat_projection);
            }

            // Render command lists
            int vtx_offset = 0;
            int idx_offset = 0;
            for (auto const& draw_list : draw_lists) {
                for (u32 cmd_i = 0; cmd_i < draw_list->cmd_buffer.size; cmd_i++) {
                    DrawCmd const* pcmd = &draw_list->cmd_buffer[cmd_i];
                    const RECT r = {(LONG)pcmd->clip_rect.x,
                                    (LONG)pcmd->clip_rect.y,
                                    (LONG)pcmd->clip_rect.z,
                                    (LONG)pcmd->clip_rect.w};
                    device->SetTexture(0, (LPDIRECT3DTEXTURE9)pcmd->texture_id);
                    device->SetScissorRect(&r);
                    device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,
                                                 vtx_offset,
                                                 0,
                                                 (UINT)draw_list->vtx_buffer.size,
                                                 (UINT)idx_offset,
                                                 pcmd->elem_count / 3);
                    idx_offset += pcmd->elem_count;
                }
                vtx_offset += draw_list->vtx_buffer.size;
            }
        }

        if (auto const r = device->Present(nullptr, nullptr, nullptr, nullptr); r == D3D_OK) {
            if (render_count++ == 0) LogDebug(ModuleName::Gui, "{}: first successful render", __FUNCTION__);
        } else if (r == D3DERR_DEVICELOST) {
            // Device has been lost (driver crash, display sleep, remote desktop, etc.)
            // Microsoft: "Reset is the only method by which an application can change
            // the device from a lost to an operational state."
            auto const cooperative_level = device->TestCooperativeLevel();
            if (cooperative_level == D3DERR_DEVICENOTRESET) {
                // Device is ready to be reset
                LogDebug(ModuleName::Gui, "Device lost but ready to reset - calling ResetDevice()");
                auto const reset_result = ResetDevice();
                if (reset_result.HasError()) {
                    LogError(ModuleName::Gui,
                             "Failed to reset device after device loss: {}",
                             reset_result.Error());
                    return reset_result;
                }
            } else {
                // Device is still lost and not ready to reset yet - wait for next frame
                // Microsoft: "the application queries the device to see if it can be restored"
                LogDebug(ModuleName::Gui,
                         "Device lost, waiting for device to become ready (TestCooperativeLevel: {})",
                         cooperative_level);
            }
        } else {
            return D3DERR(r, "Present");
        }

        return k_success;
    }

    ErrorCodeOr<void> ResetDevice() {
        ZoneScoped;
        Trace(ModuleName::Gui);

        if (!device) return ErrorCode(k_d3d_error_category, E_FAIL, "ResetDevice called with null device");

        if (vertex_buf) {
            vertex_buf->Release();
            vertex_buf = nullptr;
        }
        if (index_buf) {
            index_buf->Release();
            index_buf = nullptr;
        }
        DestroyFontTexture();
        DestroyAllTextures();

        D3D_TRYV(device->Reset(&present_params));

        return k_success;
    }

    int render_count = 0;

    D3DPRESENT_PARAMETERS present_params = {};
    LPDIRECT3D9 d3d = nullptr;

    LPDIRECT3DDEVICE9 device = nullptr;
    LPDIRECT3DVERTEXBUFFER9 vertex_buf = nullptr;
    LPDIRECT3DINDEXBUFFER9 index_buf = nullptr;
    int vertex_buf_size = 5000, index_buf_size = 10000;
};

Renderer* CreateNewRendererDirect3D9() { return new Direct3D9Renderer(); }
