// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/timer-support.h>
#include <clap/host.h>
#include <pugl/pugl.h>

#include "foundation/foundation.hpp"

#include "aspect_ratio.hpp"
#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui_frame.hpp"

constexpr bool k_debug_app_window = false;

constexpr UiSize k_gui_aspect_ratio = {10, 7};

constexpr u16 k_min_gui_width = SizeWithAspectRatio(300, k_gui_aspect_ratio).width;
constexpr u32 k_max_gui_width =
    SizeWithAspectRatio(LargestRepresentableValue<u16>() - k_gui_aspect_ratio.width, k_gui_aspect_ratio)
        .width;

constexpr f32 k_default_dpi = 96.0f;
constexpr f32 k_default_gui_width_inches = 10.0f;
constexpr f32 k_max_gui_size_screen_fraction = 0.6f;

struct NativeAppWindowState;

struct AppWindow {
    static constexpr uintptr k_pugl_timer_id = 200;
    static constexpr char const* k_window_class_name = "FloeSampler";

    clap_host const& host;
    prefs::Preferences& prefs;

    PuglWorld* world {};
    PuglView* view {};
    CursorType current_cursor {CursorType::Default};
    RendererBackend renderer_backend {};
    Renderer* renderer {};
    f64 double_click_time_ms {300.0};
    GuiFrameOutput last_result {};
    GuiFrameInput frame_state {};
    Optional<GuiState> gui {};
    Optional<clap_id> clap_timer_id {};
    Optional<int> clap_posix_fd {};
    bool pugl_timer_running {};
    bool inside_update {};
    bool first_update_made {};
    bool wanted_focus_last_update {};
    ArenaAllocator file_picker_result_arena {Malloc::Instance()};
    NativeAppWindowState* native_state;
};

enum class AppWindowErrorCode {
    UnknownError,
    Unsupported,
    BackendFailed,
    RegistrationFailed,
    RealizeFailed,
    SetFormatFailed,
    CreateContextFailed,
};

extern ErrorCodeCategory const app_window_error_code;

inline ErrorCodeCategory const& ErrorCategoryForEnum(AppWindowErrorCode) { return app_window_error_code; }

UiSize DefaultUiSize(AppWindow& window);

ErrorCodeOr<void> Init(AppWindow& window);
void Deinit(AppWindow& window);

void OnClapTimer(AppWindow& window, clap_id timer_id);

void OnPosixFd(AppWindow& window, int fd);

ErrorCodeOr<void> SetParent(AppWindow& window, clap_window_t const& parent);

// Size is in pixels on all OS.
bool SetSize(AppWindow& window, UiSize new_size);

UiSize GetSize(AppWindow& window);

ErrorCodeOr<void> SetVisible(AppWindow& window, bool visible, Engine& engine);

// We mostly use pugl to abstract away OS-specific windowing, however we sometimes still need some
// particulars. Internal only.
namespace native {

void InitNativeState(AppWindow&);
void DeinitNativeState(AppWindow&);

// Due to the way Windows, Linux and macOS handle file browsers, we have this design:
// - This function may or may not block, depending on the platform.
// - Either way, it will at some point fill GuiFrameInput::file_picker_results with the selected file paths
//   for the application to consume on its next frame.
ErrorCodeOr<void> OpenNativeFilePicker(AppWindow& window, FilePickerDialogOptions const& options);
void CloseNativeFilePicker(AppWindow& window);

// Returns true to request the platform to update the GUI.
bool NativeFilePickerOnClientMessage(AppWindow& window, uintptr data1, uintptr data2);

f64 DoubleClickTimeMs(AppWindow const& window);
UiSize DefaultUiSizeFromDpi(void* native_window);

// Linux only
int FdFromPuglWorld(PuglWorld* world);
void X11SetParent(PuglView* view, uintptr parent);

} // namespace native

// Internal.
UiSize DefaultUiSizeInternal(UiSize screen_size, f32 dpi);
