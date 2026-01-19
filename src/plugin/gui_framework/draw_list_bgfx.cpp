// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// References:
// - BIGG (unlicense): https://github.com/JoshuaBrookover/bigg
// - bgfx imgui (BSD-2-Clause): https://github.com/bkaradzic/bgfx/blob/master/examples/common/imgui/imgui.cpp

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include <bx/math.h>
#pragma clang diagnostic pop

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "bgfx_init_window.hpp"
#include "draw_list.hpp"
#include "shaders/fs_draw_list.bin.h"
#include "shaders/vs_draw_list.bin.h"

static bgfx::EmbeddedShader const g_embedded_shaders[] = {
    BGFX_EMBEDDED_SHADER(fs_draw_list),
    BGFX_EMBEDDED_SHADER(vs_draw_list),
    BGFX_EMBEDDED_SHADER_END(),
};

namespace graphics {

static constexpr ErrorCodeCategory k_bgfx_error_category = {
    .category_id = "BGFX",
    .message = [](Writer const& writer, ErrorCode) -> ErrorCodeOr<void> {
        return writer.WriteChars("bgfx error"_s);
    },
};

static ErrorCode Error(char const* debug_msg) { return ErrorCode(k_bgfx_error_category, 0, debug_msg); }

struct Callback : public bgfx::CallbackI {
    virtual ~Callback() = default;

    virtual void fatal(char const* filePath, u16 line, bgfx::Fatal::Enum code, char const* str) override {
        LogError(ModuleName::Bgfx,
                 "Fatal BGFX error {}, {}. {} ({})",
                 EnumToString(code),
                 str,
                 filePath,
                 line);

        if (bgfx::Fatal::DebugCheck == code)
            __builtin_debugtrap();
        else
            Panic("BGFX fatal error");
    }

    virtual void traceVargs(char const* filepath, u16 line, char const* format, va_list args) override {
        InlineSprintfBuffer buffer;
        buffer.AppendV(format, args);
        if (buffer.AsString().size && EndsWith(buffer.AsString(), '\n')) buffer.size_remaining++;
        LogDebug(ModuleName::Bgfx,
                 "{} ({}): {}",
                 path::Filename(FromNullTerminated(filepath)),
                 line,
                 buffer.AsString());
    }

    virtual void profilerBegin(char const*, u32, char const*, u16) override {}
    virtual void profilerBeginLiteral(char const*, u32, char const*, u16) override {}
    virtual void profilerEnd() override {}

    virtual u32 cacheReadSize(u64) override { return 0; }
    virtual bool cacheRead(u64, void*, u32) override { return false; }
    virtual void cacheWrite(u64, void const*, u32) override {}

    virtual void screenShot(char const* _filePath,
                            u32 _width,
                            u32 _height,
                            u32 _pitch,
                            void const* _data,
                            u32 _size,
                            bool _yflip) override {
        BX_UNUSED(_filePath, _width, _height, _pitch, _data, _size, _yflip);
        TODO();
    }

    virtual void captureBegin(u32, u32, u32, bgfx::TextureFormat::Enum, bool) override {}

    virtual void captureEnd() override {}
    virtual void captureFrame(void const*, u32) override {}
};

struct Renderer {
    ErrorCodeOr<void> InitIfNeeded(void* native_window, void* native_display) {
        if (init) return k_success;
        (void)native_window;

        {
            bx::setAssertHandler(
                [](bx::Location const& location, uint32_t skip, char const* format, va_list args) -> bool {
                    InlineSprintfBuffer buffer;
                    buffer.AppendV(format, args);
                    StdPrintF(StdStream::Err,
                              "{} ({}): {}\n",
                              location.filePath,
                              location.line,
                              buffer.AsString());

                    DynamicArrayBounded<char, 2000> b2 {};
                    auto _ = WriteCurrentStacktrace(dyn::WriterFor(b2),
                                                    {.ansi_colours = true},
                                                    StacktraceFrames {skip});

                    Panic(dyn::NullTerminated(b2),
                          {
                              .file = location.filePath,
                              .line = (int)location.line,
                          });
                });

            bgfx::Init cfg {};

            cfg.platformData.nwh = GetBgfxInitWindowHandle(native_display);
            cfg.platformData.ndt = native_display;
            cfg.platformData.type = bgfx::NativeWindowHandleType::Default;

            if constexpr (FLOE_BGFX_API_VULKAN) cfg.type = bgfx::RendererType::Vulkan;
            if constexpr (FLOE_BGFX_API_DIRECT3D11) cfg.type = bgfx::RendererType::Direct3D11;
            if constexpr (FLOE_BGFX_API_METAL) cfg.type = bgfx::RendererType::Metal;
            if (cfg.type == bgfx::RendererType::Count) PanicIfReached();

            cfg.resolution.numBackBuffers = 1;
            cfg.resolution.width = FLOE_BGFX_API_METAL ? 1 : 0;
            cfg.resolution.height = FLOE_BGFX_API_METAL ? 1 : 0;
            cfg.resolution.reset = BGFX_RESET_VSYNC;
            cfg.callback = &callbacks;

            if (!bgfx::init(cfg)) return Error("bgfx::init failed");
        }

        static_assert(offsetof(DrawVert, pos) == 0);
        static_assert(offsetof(DrawVert, uv) == 8);
        static_assert(offsetof(DrawVert, col) == 16);
        vertex_layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float) // f32x2 pos, offset 0
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float) // f32x2 uv, offset 8
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true) // u32 col, offset 16
            .end();

        LogDebug(ModuleName::Bgfx,
                 "DrawVert size: {}, vertex_layout stride: {}",
                 sizeof(DrawVert),
                 vertex_layout.getStride());

        ASSERT_EQ(vertex_layout.getStride(), sizeof(DrawVert));

        bool success = false;

        texture_uniform = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        if (!bgfx::isValid(texture_uniform)) return Error("failed to create texture uniform");
        DEFER {
            if (!success) {
                bgfx::destroy(texture_uniform);
                texture_uniform = BGFX_INVALID_HANDLE;
            }
        };

        auto const type = bgfx::getRendererType();

        auto const vs = bgfx::createEmbeddedShader(g_embedded_shaders, type, "vs_draw_list");
        if (!bgfx::isValid(vs)) return Error("failed to create vertex shader");
        DEFER {
            if (!success) bgfx::destroy(vs);
        };

        auto const fs = bgfx::createEmbeddedShader(g_embedded_shaders, type, "fs_draw_list");
        if (!bgfx::isValid(fs)) return Error("failed to create fragment shader");
        DEFER {
            if (!success) bgfx::destroy(fs);
        };

        shader_program = bgfx::createProgram(vs, fs, true);
        if (!bgfx::isValid(shader_program)) return Error("failed to create shader program");

        ASSERT(bgfx::getCaps()->supported & BGFX_CAPS_SWAP_CHAIN);

        init = true;
        success = true;
        return k_success;
    }

    void Shutdown() {
        if (bgfx::isValid(texture_uniform)) {
            bgfx::destroy(texture_uniform);
            texture_uniform = BGFX_INVALID_HANDLE;
        }

        if (bgfx::isValid(shader_program)) {
            bgfx::destroy(shader_program);
            shader_program = BGFX_INVALID_HANDLE;
        }
    }

    void Render() {}

    bool init {};
    Callback callbacks {};
    u16 view_id {};

    bgfx::VertexLayout vertex_layout {};
    bgfx::UniformHandle texture_uniform = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shader_program = BGFX_INVALID_HANDLE;
};

[[clang::no_destroy]] Renderer g_renderer {};

struct BgfxDrawContext : public DrawContext {
    ~BgfxDrawContext() override { DestroyDeviceObjects(); }

    ErrorCodeOr<void> CreateDeviceObjects(UiSize size, void* native_window, void* native_display) override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);

        TRY(g_renderer.InitIfNeeded(native_window, native_display));

        ASSERT(!bgfx::isValid(window_framebuffer));
        ASSERT(size.width > 0 && size.height > 0);

        LogDebug(ModuleName::Bgfx,
                 "Creating framebuffer for window: {}x{}, nwh={}",
                 size.width,
                 size.height,
                 native_window);

        window_framebuffer = bgfx::createFrameBuffer(native_window, size.width, size.height);

        if (!bgfx::isValid(window_framebuffer)) return Error("failed to create window framebuffer");

        last_window_size = size;

        {
            auto const caps = bgfx::getCaps();
            dyn::Clear(graphics_device_info);
            fmt::Append(graphics_device_info, "Renderer: {}\n", EnumToString(bgfx::getRendererType()));
            fmt::Append(graphics_device_info, "Vendor ID: 0x{x}\n", caps->vendorId);
            fmt::Append(graphics_device_info, "Device ID: 0x{x}\n", caps->deviceId);
            fmt::Append(graphics_device_info, "Max Texture Size: {}\n", caps->limits.maxTextureSize);
        }

        return k_success;
    }

    void DestroyDeviceObjects() override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);

        DestroyFontTexture();

        if (bgfx::isValid(window_framebuffer)) {
            LogDebug(ModuleName::Bgfx, "Destroying framebuffer");

            bgfx::destroy(window_framebuffer);
            window_framebuffer.idx = bgfx::kInvalidHandle;

            bgfx::frame();
            bgfx::frame();

            LogDebug(ModuleName::Bgfx, "Framebuffer destroyed and flushed");
        }
    }

    ErrorCodeOr<void> CreateFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);
        ASSERT(!bgfx::isValid(font_texture));
        ASSERT(fonts.fonts.size > 0);

        // Build texture atlas
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        int bytes_per_pixel = 0;
        fonts.GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
        ASSERT(pixels != nullptr);
        ASSERT(bytes_per_pixel == 4);

        LogDebug(ModuleName::Bgfx, "Creating font texture: {}x{}", width, height);

        font_texture = bgfx::createTexture2D((u16)width,
                                             (u16)height,
                                             false,
                                             1,
                                             bgfx::TextureFormat::RGBA8,
                                             0,
                                             bgfx::copy(pixels, (u32)(width * height * 4)));

        if (!bgfx::isValid(font_texture)) return Error("failed to create font texture");

        fonts.tex_id = (void*)(uintptr)font_texture.idx;

        fonts.ClearTexData();

        LogDebug(ModuleName::Bgfx, "Font texture created successfully");
        return k_success;
    }

    void DestroyFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);

        if (bgfx::isValid(font_texture)) {
            bgfx::destroy(font_texture);
            font_texture = BGFX_INVALID_HANDLE;
            fonts.tex_id = nullptr;
            LogDebug(ModuleName::Bgfx, "Font texture destroyed");
        }

        fonts.Clear();
    }

    ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);
        ASSERT(bytes_per_pixel == 3 || bytes_per_pixel == 4);
        ASSERT(size.width && size.height);

        auto const format = (bytes_per_pixel == 4) ? bgfx::TextureFormat::RGBA8 : bgfx::TextureFormat::RGB8;

        LogDebug(ModuleName::Bgfx,
                 "Creating texture: {}x{}, bpp={}, format={}",
                 size.width,
                 size.height,
                 bytes_per_pixel,
                 format);

        auto const tex = bgfx::createTexture2D(size.width,
                                               size.height,
                                               false,
                                               1,
                                               format,
                                               0,
                                               bgfx::copy(data, size.width * size.height * bytes_per_pixel));

        if (!bgfx::isValid(tex)) return Error("failed to create texture");
        return (void*)(uintptr_t)tex.idx;
    }

    void DestroyTexture(TextureHandle& id) override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);

        if (id != nullptr) {
            bgfx::TextureHandle const tex {(u16)(uintptr_t)id};
            if (bgfx::isValid(tex)) bgfx::destroy(tex);
            id = nullptr;
        }
    }

    ErrorCodeOr<void> Render(Span<DrawList*> draw_lists, UiSize window_size, void* native_window) override {
        ZoneScoped;
        Trace(ModuleName::Bgfx);

        if (draw_lists.size == 0) return k_success;

        LogDebug(ModuleName::Bgfx,
                 "Render called: window={}x{}, draw_lists={}",
                 window_size.width,
                 window_size.height,
                 draw_lists.size);

        bool const size_changed = last_window_size != window_size;
        bool const needs_recreation = size_changed || !bgfx::isValid(window_framebuffer);

        if (needs_recreation) {
            LogDebug(ModuleName::Bgfx,
                     "Recreating framebuffer: size_changed={}, invalid={}, {}x{} -> {}x{}",
                     size_changed,
                     !bgfx::isValid(window_framebuffer),
                     last_window_size.width,
                     last_window_size.height,
                     window_size.width,
                     window_size.height);

            if (bgfx::isValid(window_framebuffer)) {
                bgfx::destroy(window_framebuffer);
                window_framebuffer.idx = bgfx::kInvalidHandle;
            }

            bgfx::frame();

            window_framebuffer =
                bgfx::createFrameBuffer(native_window, window_size.width, window_size.height);
            if (!bgfx::isValid(window_framebuffer)) return Error("failed to create window framebuffer");

            last_window_size = window_size;
        }

        // Setup view
        bgfx::setViewName(k_view_id, "Floe GUI");
        bgfx::setViewMode(k_view_id, bgfx::ViewMode::Sequential);
        bgfx::setViewFrameBuffer(k_view_id, window_framebuffer);
        bgfx::setViewClear(k_view_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

        // Setup orthographic projection matrix to transform from pixel coordinates to clip space
        auto const caps = bgfx::getCaps();
        f32 ortho[16];
        bx::mtxOrtho(ortho,
                     0.0f,
                     (f32)window_size.width,
                     (f32)window_size.height,
                     0.0f,
                     0.0f,
                     1000.0f,
                     0.0f,
                     caps->homogeneousDepth);
        bgfx::setViewTransform(k_view_id, nullptr, ortho);
        bgfx::setViewRect(k_view_id, 0, 0, window_size.width, window_size.height);

        for (auto const* draw_list : draw_lists) {
            if (draw_list->vtx_buffer.size == 0 || draw_list->idx_buffer.size == 0) continue;

            auto const num_vertices = (u32)draw_list->vtx_buffer.size;
            auto const num_indices = (u32)draw_list->idx_buffer.size;

            if (!bgfx::getAvailTransientVertexBuffer(num_vertices, g_renderer.vertex_layout) ||
                !bgfx::getAvailTransientIndexBuffer(num_indices)) {
                LogWarning(ModuleName::Bgfx, "Transient buffers not available, skipping draw list");
                break;
            }

            bgfx::TransientVertexBuffer tvb;
            bgfx::TransientIndexBuffer tib;
            bgfx::allocTransientVertexBuffer(&tvb, num_vertices, g_renderer.vertex_layout);
            bgfx::allocTransientIndexBuffer(&tib, num_indices);

            // Copy index data directly
            CopyMemory(tib.data, draw_list->idx_buffer.data, num_indices * sizeof(DrawIdx));

            // Copy vertex data, conditionally flipping Y coordinates based on backend
            auto* dst_verts = (DrawVert*)tvb.data;
            if (caps->originBottomLeft) {
                for (u32 i = 0; i < num_vertices; i++) {
                    dst_verts[i] = draw_list->vtx_buffer.data[i];
                    dst_verts[i].pos.y = (f32)window_size.height - dst_verts[i].pos.y;
                }
            } else {
                CopyMemory(dst_verts, draw_list->vtx_buffer.data, num_vertices * sizeof(DrawVert));
            }

            // Render each draw command
            u32 offset = 0;
            for (auto const& cmd : draw_list->cmd_buffer) {
                if (cmd.elem_count == 0) continue;

                auto th = font_texture;
                if (cmd.texture_id != nullptr) th.idx = (u16)(uintptr)cmd.texture_id;

                auto const x = (u16)Max(cmd.clip_rect.x, 0.0f);
                auto const y = (u16)Max(cmd.clip_rect.y, 0.0f);
                auto const w = (u16)(Min(cmd.clip_rect.z, 65535.0f) - x);
                auto const h = (u16)(Min(cmd.clip_rect.w, 65535.0f) - y);
                bgfx::setScissor(x, y, w, h);

                bgfx::setState(
                    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::setTexture(0, g_renderer.texture_uniform, th);
                bgfx::setVertexBuffer(0, &tvb, 0, num_vertices);
                bgfx::setIndexBuffer(&tib, offset, cmd.elem_count);
                bgfx::submit(k_view_id, g_renderer.shader_program);

                offset += cmd.elem_count;
            }
        }

        // Present frame
        bgfx::frame();

        return k_success;
    }

    static inline u16 const k_view_id = 200;

    bgfx::TextureHandle font_texture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle window_framebuffer = BGFX_INVALID_HANDLE;
    UiSize last_window_size {0, 0};
};

DrawContext* CreateNewDrawContextBgfx() { return new BgfxDrawContext(); }

} // namespace graphics
