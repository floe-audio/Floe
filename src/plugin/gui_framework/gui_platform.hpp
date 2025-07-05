// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/timer-support.h>
#include <clap/host.h>
#include <pugl/gl.h> // on windows this includes windows.h
#include <pugl/pugl.h>
//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"

#include "aspect_ratio.hpp"
#include "engine/engine.hpp"
#include "gui/gui.hpp"
#include "gui/gui_prefs.hpp"
#include "gui_frame.hpp"

constexpr bool k_debug_gui_platform = false;

constexpr UiSize k_aspect_ratio_without_keyboard = {100, 63};
constexpr UiSize k_aspect_ratio_with_keyboard = {100, 70};

constexpr u16 k_min_gui_width = k_aspect_ratio_with_keyboard.width * 2;
constexpr u16 k_max_gui_width = k_aspect_ratio_with_keyboard.width * 100;
constexpr u32 k_largest_gui_size = LargestRepresentableValue<u16>();

constexpr u16 k_default_gui_width = SizeWithAspectRatio(910, k_aspect_ratio_without_keyboard).width;

struct GuiPlatform {
    static constexpr uintptr k_pugl_timer_id = 200;
    static constexpr char const* k_window_class_name = "FloeSampler";

    clap_host const& host;
    prefs::Preferences& prefs;
    PuglWorld* world {};
    PuglView* view {};
    CursorType current_cursor {CursorType::Default};
    graphics::DrawContext* graphics_ctx {};
    f64 double_click_time_ms {300.0};
    GuiFrameResult last_result {};
    GuiFrameInput frame_state {};
    Optional<Gui> gui {};
    Optional<clap_id> clap_timer_id {};
    Optional<int> clap_posix_fd {};
    bool inside_update {};
    bool first_update_made {};
    ArenaAllocator file_picker_result_arena {Malloc::Instance()};
    Optional<OpaqueHandle<IS_WINDOWS ? 160 : 16>> native_file_picker {};
    bool windows_keyboard_hook_added {};
};

// Public API
// ==========================================================================================================

enum class GuiPlatformErrorCode {
    UnknownError,
    Unsupported,
    BackendFailed,
    RegistrationFailed,
    RealizeFailed,
    SetFormatFailed,
    CreateContextFailed,
};

static ErrorCodeCategory const gui_platform_error_code {
    .category_id = "GUIP",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((GuiPlatformErrorCode)code.code) {
            case GuiPlatformErrorCode::UnknownError: str = "unknown error"; break;
            case GuiPlatformErrorCode::Unsupported: str = "unsupported"; break;
            case GuiPlatformErrorCode::BackendFailed: str = "backend init failed"; break;
            case GuiPlatformErrorCode::RegistrationFailed: str = "registration failed"; break;
            case GuiPlatformErrorCode::RealizeFailed: str = "realize failed"; break;
            case GuiPlatformErrorCode::SetFormatFailed: str = "set format failed"; break;
            case GuiPlatformErrorCode::CreateContextFailed: str = "create context failed"; break;
        }
        return writer.WriteChars(str);
    },
};
inline ErrorCodeCategory const& ErrorCategoryForEnum(GuiPlatformErrorCode) { return gui_platform_error_code; }

static ErrorCodeOr<void> Required(PuglStatus status) {
    switch (status) {
        case PUGL_SUCCESS: return k_success;

        case PUGL_UNSUPPORTED: return ErrorCode {GuiPlatformErrorCode::Unsupported};
        case PUGL_FAILURE:
        case PUGL_UNKNOWN_ERROR: return ErrorCode {GuiPlatformErrorCode::UnknownError};
        case PUGL_BACKEND_FAILED: return ErrorCode {GuiPlatformErrorCode::BackendFailed};
        case PUGL_REGISTRATION_FAILED: return ErrorCode {GuiPlatformErrorCode::RegistrationFailed};
        case PUGL_REALIZE_FAILED: return ErrorCode {GuiPlatformErrorCode::RealizeFailed};
        case PUGL_SET_FORMAT_FAILED: return ErrorCode {GuiPlatformErrorCode::SetFormatFailed};
        case PUGL_CREATE_CONTEXT_FAILED: return ErrorCode {GuiPlatformErrorCode::CreateContextFailed};

        // Bugs
        case PUGL_BAD_BACKEND: Panic("Invalid or missing backend");
        case PUGL_BAD_CONFIGURATION: Panic("Invalid view configuration");
        case PUGL_BAD_PARAMETER: Panic("Invalid parameter");
        case PUGL_NO_MEMORY: Panic("Failed to allocate memory");
    }
    return k_success;
}

namespace detail {

static PuglStatus EventHandler(PuglView* view, PuglEvent const* event);
static void LogIfSlow(Stopwatch& stopwatch, String message);
inline FloeClapExtensionHost const* CustomFloeHost(clap_host const& host);

// Due to the way Windows, Linux and macOS handle file pickers, we have this design:
// - This function may or may not block, depending on the platform.
// - Either way, it will at some point fill GuiFrameInput::file_picker_results with the selected file paths
//   for the application to consume on its next frame.
ErrorCodeOr<void> OpenNativeFilePicker(GuiPlatform& platform, FilePickerDialogOptions const& options);
void CloseNativeFilePicker(GuiPlatform& platform);

// Returns true to request the platform to update the GUI.
bool NativeFilePickerOnClientMessage(GuiPlatform& platform, uintptr data1, uintptr data2);

// Linux only
int FdFromPuglWorld(PuglWorld* world);
void X11SetParent(PuglView* view, uintptr parent);

// Windows only
void AddWindowsKeyboardHook(GuiPlatform& platform);
void RemoveWindowsKeyboardHook(GuiPlatform& platform);

f64 DoubleClickTimeMs(GuiPlatform const& platform);

} // namespace detail

PUBLIC ErrorCodeOr<void> CreateView(GuiPlatform& platform) {
    Trace(ModuleName::Gui);

    ASSERT(platform.world == nullptr);
    ASSERT(platform.view == nullptr);
    ASSERT(platform.graphics_ctx == nullptr);
    ASSERT(!platform.gui);
    ASSERT(!platform.clap_timer_id);
    ASSERT(!platform.clap_posix_fd);

    if (auto const floe_custom_host = detail::CustomFloeHost(platform.host)) {
        platform.world = (PuglWorld*)floe_custom_host->pugl_world;
        ASSERT(platform.world != nullptr);
    } else {
        platform.world = puglNewWorld(PUGL_MODULE, 0);
        if (platform.world == nullptr) Panic("out of memory");
        puglSetWorldString(platform.world, PUGL_CLASS_NAME, GuiPlatform::k_window_class_name);
        platform.world = platform.world;
        LogInfo(ModuleName::Gui, "creating new world");
    }

    platform.view = puglNewView(platform.world);
    if (platform.view == nullptr) Panic("out of memory");

    puglSetViewHint(platform.view, PUGL_RESIZABLE, true);
    puglSetPositionHint(platform.view, PUGL_DEFAULT_POSITION, 0, 0);

    auto const default_size = DesiredWindowSize(platform.prefs);
    puglSetSizeHint(platform.view, PUGL_DEFAULT_SIZE, default_size.width, default_size.height);
    puglSetSizeHint(platform.view, PUGL_CURRENT_SIZE, default_size.width, default_size.height);

    auto const aspect_ratio = DesiredAspectRatio(platform.prefs);

    auto const min_size = SizeWithAspectRatio(k_min_gui_width, aspect_ratio);
    ASSERT(min_size.width >= k_min_gui_width);
    puglSetSizeHint(platform.view, PUGL_MIN_SIZE, min_size.width, min_size.height);

    auto const max_size = SizeWithAspectRatio(k_max_gui_width, aspect_ratio);
    ASSERT(max_size.width <= k_largest_gui_size);
    puglSetSizeHint(platform.view, PUGL_MAX_SIZE, max_size.width, max_size.height);

    puglSetSizeHint(platform.view, PUGL_FIXED_ASPECT, aspect_ratio.width, aspect_ratio.height);

    return k_success;
}

PUBLIC void DestroyView(GuiPlatform& platform) {
    Trace(ModuleName::Gui);

    if constexpr (IS_WINDOWS) {
        if (platform.windows_keyboard_hook_added) detail::RemoveWindowsKeyboardHook(platform);
    }

    detail::CloseNativeFilePicker(platform);

    if (platform.gui) {
        platform.gui.Clear();

        if constexpr (IS_LINUX) {
            if (platform.clap_posix_fd) {
                auto const ext =
                    (clap_host_posix_fd_support const*)platform.host.get_extension(&platform.host,
                                                                                   CLAP_EXT_POSIX_FD_SUPPORT);
                if (ext && ext->unregister_fd) {
                    bool const success = ext->unregister_fd(&platform.host, *platform.clap_posix_fd);
                    if (!success) LogError(ModuleName::Gui, "failed to unregister fd");
                }
                platform.clap_posix_fd = k_nullopt;
            }

            if (platform.clap_timer_id) {
                auto const ext =
                    (clap_host_timer_support const*)platform.host.get_extension(&platform.host,
                                                                                CLAP_EXT_TIMER_SUPPORT);
                if (ext && ext->unregister_timer) {
                    bool const success = ext->unregister_timer(&platform.host, *platform.clap_timer_id);
                    if (!success) LogError(ModuleName::Gui, "failed to unregister timer");
                }
                platform.clap_timer_id = k_nullopt;
            }
        }

        ASSERT(platform.view);
        puglStopTimer(platform.view, platform.k_pugl_timer_id);
        puglUnrealize(platform.view);
    }

    puglFreeView(platform.view);
    platform.view = nullptr;

    platform.first_update_made = false;

    if (!detail::CustomFloeHost(platform.host)) {
        LogInfo(ModuleName::Gui, "freeing world");
        puglFreeWorld(platform.world);
        platform.world = nullptr;
    }
}

PUBLIC void OnClapTimer(GuiPlatform& platform, clap_id timer_id) {
    Stopwatch stopwatch {};
    if (platform.clap_timer_id && *platform.clap_timer_id == timer_id) puglUpdate(platform.world, 0);
    detail::LogIfSlow(stopwatch, "OnClapTimer");
}

PUBLIC void OnPosixFd(GuiPlatform& platform, int fd) {
    Stopwatch stopwatch {};
    if (platform.clap_posix_fd && fd == *platform.clap_posix_fd) puglUpdate(platform.world, 0);
    detail::LogIfSlow(stopwatch, "OnPosixFd");
}

PUBLIC ErrorCodeOr<void> SetParent(GuiPlatform& platform, clap_window_t const& window) {
    ASSERT(platform.view);
    ASSERT(window.ptr);

    auto const parent = puglGetParent(platform.view);
    LogDebug(ModuleName::Gui, "SetParent, current: {}, new: {}", parent, window.ptr);

    if (window.ptr == (void*)parent) return k_success;

    if (parent && window.ptr != (void*)parent) {
        // Pluginval tries to re-parent us. I'm not sure if this is a quirk of pluginval or if it's more
        // common than that. Either way, we try to support it.

        DestroyView(platform);
        TRY(CreateView(platform));
    }

    ASSERT(!puglGetNativeView(platform.view), "SetParent called after window realised");
    // NOTE: "This must be called before puglRealize(), reparenting is not supported"
    TRY(Required(puglSetParent(platform.view, (uintptr)window.ptr)));
    return k_success;
}

PUBLIC ErrorCodeOr<void> SetVisible(GuiPlatform& platform, bool visible, Engine& engine) {
    ASSERT(platform.view);
    if (visible) {
        if (!platform.gui) {
            puglSetHandle(platform.view, &platform);
            TRY(Required(puglSetEventFunc(platform.view, detail::EventHandler)));

            // IMPROVE: we might want a DirectX backend for Windows
            TRY(Required(puglSetBackend(platform.view, puglGlBackend())));
            TRY(Required(puglSetViewHint(platform.view, PUGL_CONTEXT_VERSION_MAJOR, 3)));
            TRY(Required(puglSetViewHint(platform.view, PUGL_CONTEXT_VERSION_MINOR, 3)));
            TRY(Required(
                puglSetViewHint(platform.view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE)));
            puglSetViewHint(platform.view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON);

            TRY(Required(puglRealize(platform.view)));
            TRY(Required(
                puglStartTimer(platform.view, platform.k_pugl_timer_id, 1.0 / (f64)k_gui_refresh_rate_hz)));

            platform.double_click_time_ms = detail::DoubleClickTimeMs(platform);

            detail::X11SetParent(platform.view, puglGetParent(platform.view));

            platform.gui.Emplace(platform.frame_state, engine);

            if constexpr (IS_LINUX) {
                // https://nakst.gitlab.io/tutorial/clap-part-3.html
                if (auto const posix_fd_extension =
                        (clap_host_posix_fd_support const*)
                            platform.host.get_extension(&platform.host, CLAP_EXT_POSIX_FD_SUPPORT);
                    posix_fd_extension && posix_fd_extension->register_fd) {
                    auto const fd = detail::FdFromPuglWorld(platform.world);
                    ASSERT(fd != -1);
                    if (posix_fd_extension->register_fd(&platform.host, fd, CLAP_POSIX_FD_READ))
                        platform.clap_posix_fd = fd;
                    else
                        LogError(ModuleName::Gui, "failed to register fd {}", fd);
                }

                if (auto const timer_support_extension =
                        (clap_host_timer_support const*)platform.host.get_extension(&platform.host,
                                                                                    CLAP_EXT_TIMER_SUPPORT);
                    timer_support_extension && timer_support_extension->register_timer) {
                    clap_id timer_id;
                    if (timer_support_extension->register_timer(&platform.host,
                                                                (u32)(1000.0 / k_gui_refresh_rate_hz),
                                                                &timer_id)) {
                        platform.clap_timer_id = timer_id;
                    } else
                        LogError(ModuleName::Gui, "failed to register timer");
                }
            }
        }

        TRY(Required(puglShow(platform.view, PUGL_SHOW_PASSIVE)));
    } else {
        platform.frame_state.Reset();
        detail::CloseNativeFilePicker(platform);
        // IMRPOVE: stop update timers, make things more efficient
        TRY(Required(puglHide(platform.view)));
    }
    return k_success;
}

PUBLIC bool SetSize(GuiPlatform& platform, UiSize new_size) {
    return puglSetSizeHint(platform.view, PUGL_CURRENT_SIZE, new_size.width, new_size.height) == PUGL_SUCCESS;
}

PUBLIC UiSize GetSize(GuiPlatform& platform) {
    auto const size = puglGetSizeHint(platform.view, PUGL_CURRENT_SIZE);
    return {size.width, size.height};
}

// Details
// ==========================================================================================================

namespace detail {

inline FloeClapExtensionHost const* CustomFloeHost(clap_host const& host) {
    if constexpr (PRODUCTION_BUILD) return nullptr;
    return (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
}

static void LogIfSlow(Stopwatch& stopwatch, String message) {
    auto const elapsed = stopwatch.MillisecondsElapsed();
    if (elapsed > 10) LogWarning(ModuleName::Gui, "{} took {}ms", message, elapsed);
}

static bool IsUpdateNeeded(GuiPlatform& platform) {
    bool update_needed = false;

    // Until the GUI has been run, we can't know about its requirements and whether we can be more idle or
    // not.
    if (!platform.first_update_made) update_needed = true;

    if (platform.frame_state.request_update.Exchange(false, RmwMemoryOrder::Relaxed)) update_needed = true;

    if (platform.last_result.update_request > GuiFrameResult::UpdateRequest::Sleep) update_needed = true;

    if (platform.last_result.timed_wakeups) {
        for (usize i = 0; i < platform.last_result.timed_wakeups->size;) {
            auto& t = (*platform.last_result.timed_wakeups)[i];
            if (TimePoint::Now() >= t) {
                update_needed = true;
                dyn::Remove(*platform.last_result.timed_wakeups, i);
            } else {
                ++i;
            }
        }
    }

    return update_needed;
}

static ModifierFlags CreateModifierFlags(u32 pugl_mod_flags) {
    ModifierFlags result {};
    if (pugl_mod_flags & PUGL_MOD_SHIFT) result.Set(ModifierKey::Shift);
    if (pugl_mod_flags & PUGL_MOD_CTRL) result.Set(ModifierKey::Ctrl);
    if (pugl_mod_flags & PUGL_MOD_ALT) result.Set(ModifierKey::Alt);
    if (pugl_mod_flags & PUGL_MOD_SUPER) result.Set(ModifierKey::Super);
    return result;
}

static bool EventWheel(GuiPlatform& platform, PuglScrollEvent const& scroll_event) {
    platform.frame_state.modifiers = CreateModifierFlags(scroll_event.state);

    // IMPROVE: support horizontal scrolling
    if (scroll_event.direction != PUGL_SCROLL_UP && scroll_event.direction != PUGL_SCROLL_DOWN) return false;

    auto const delta_lines = (f32)scroll_event.dy;
    platform.frame_state.mouse_scroll_delta_in_lines += delta_lines;
    if (platform.last_result.wants_mouse_scroll) return true;
    return false;
}

static bool EventMotion(GuiPlatform& platform, PuglMotionEvent const& motion_event) {
    platform.frame_state.modifiers = CreateModifierFlags(motion_event.state);

    auto const new_cursor_pos = f32x2 {(f32)motion_event.x, (f32)motion_event.y};
    bool result = false;
    platform.frame_state.cursor_pos = new_cursor_pos;

    for (auto& btn : platform.frame_state.mouse_buttons) {
        if (btn.is_down) {
            if (!btn.is_dragging) btn.dragging_started = true;
            btn.is_dragging = true;
        }
    }

    if (platform.last_result.mouse_tracked_rects.size == 0 || platform.last_result.wants_mouse_capture) {
        result = true;
    } else if (IsUpdateNeeded(platform)) {
        return true;
    } else {
        for (auto const i : Range(platform.last_result.mouse_tracked_rects.size)) {
            auto& item = platform.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(platform.frame_state.cursor_pos);
            if (mouse_over && !item.mouse_over) {
                // cursor just entered
                item.mouse_over = mouse_over;
                result = true;
                break;
            } else if (!mouse_over && item.mouse_over) {
                // cursor just left
                item.mouse_over = mouse_over;
                result = true;
                break;
            }
        }
    }

    return result;
}

static Optional<MouseButton> RemapMouseButton(u32 button) {
    switch (button) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
    }
    return k_nullopt;
}

static bool EventMouseButton(GuiPlatform& platform, PuglButtonEvent const& button_event, bool is_down) {
    platform.frame_state.modifiers = CreateModifierFlags(button_event.state);

    auto const button = RemapMouseButton(button_event.button);
    if (!button) return false;

    auto& btn = platform.frame_state.mouse_buttons[ToInt(*button)];

    auto const now = TimePoint::Now();
    GuiFrameInput::MouseButtonState::Event const e {
        .point = {(f32)button_event.x, (f32)button_event.y},
        .time = now,
        .modifiers = platform.frame_state.modifiers,
        .is_double_click = is_down
                               ? (e.time - btn.last_press.time) <= (platform.double_click_time_ms / 1000.0)
                               : btn.last_press.is_double_click,
    };

    if (e.is_double_click)
        LogDebug(ModuleName::Gui, "Mouse button {} double-clicked at {}, {}", *button, e.point.x, e.point.y);

    if (is_down) {
        btn.is_down = e;
        btn.last_press = e;
        btn.presses.Append(e, platform.frame_state.event_arena);
    } else {
        btn.is_down = k_nullopt;
        if (btn.is_dragging) btn.dragging_ended = true;
        btn.is_dragging = false;
        btn.releases.Append(e, platform.frame_state.event_arena);
    }

    bool result = false;
    if (platform.last_result.mouse_tracked_rects.size == 0 || platform.last_result.wants_mouse_capture ||
        (platform.last_result.wants_all_left_clicks && button == MouseButton::Left) ||
        (platform.last_result.wants_all_right_clicks && button == MouseButton::Right) ||
        (platform.last_result.wants_all_middle_clicks && button == MouseButton::Middle)) {
        result = true;
    } else {
        for (auto const i : Range(platform.last_result.mouse_tracked_rects.size)) {
            auto& item = platform.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(platform.frame_state.cursor_pos);
            if (mouse_over) {
                result = true;
                break;
            }
        }
    }

    return result;
}

static bool EventKeyRegular(GuiPlatform& platform, KeyCode key_code, bool is_down, ModifierFlags modifiers) {
    auto& key = platform.frame_state.keys[ToInt(key_code)];
    if (is_down) {
        key.presses_or_repeats.Append({modifiers}, platform.frame_state.event_arena);
        if (!key.is_down) key.presses.Append({modifiers}, platform.frame_state.event_arena);
    } else {
        key.releases.Append({modifiers}, platform.frame_state.event_arena);
    }
    key.is_down = is_down;

    if (platform.last_result.wants_keyboard_input) return true;
    if (platform.last_result.wants_just_arrow_keys &&
        (key_code == KeyCode::UpArrow || key_code == KeyCode::DownArrow || key_code == KeyCode::LeftArrow ||
         key_code == KeyCode::RightArrow)) {
        return true;
    }
    return false;
}

static Optional<KeyCode> RemapKeyCode(u32 pugl_key) {
    switch (pugl_key) {
        case PUGL_KEY_TAB: return KeyCode::Tab;
        case PUGL_KEY_LEFT: return KeyCode::LeftArrow;
        case PUGL_KEY_RIGHT: return KeyCode::RightArrow;
        case PUGL_KEY_UP: return KeyCode::UpArrow;
        case PUGL_KEY_DOWN: return KeyCode::DownArrow;
        case PUGL_KEY_PAGE_UP: return KeyCode::PageUp;
        case PUGL_KEY_PAGE_DOWN: return KeyCode::PageDown;
        case PUGL_KEY_HOME: return KeyCode::Home;
        case PUGL_KEY_END: return KeyCode::End;
        case PUGL_KEY_DELETE: return KeyCode::Delete;
        case PUGL_KEY_BACKSPACE: return KeyCode::Backspace;
        case PUGL_KEY_ENTER: return KeyCode::Enter;
        case PUGL_KEY_ESCAPE: return KeyCode::Escape;
        case PUGL_KEY_F1: return KeyCode::F1;
        case PUGL_KEY_F2: return KeyCode::F2;
        case PUGL_KEY_F3: return KeyCode::F3;
        case PUGL_KEY_SHIFT_L: return KeyCode::ShiftL;
        case PUGL_KEY_SHIFT_R: return KeyCode::ShiftR;
        case 'a': return KeyCode::A;
        case 'c': return KeyCode::C;
        case 'v': return KeyCode::V;
        case 'x': return KeyCode::X;
        case 'y': return KeyCode::Y;
        case 'z': return KeyCode::Z;
    }
    return k_nullopt;
}

static bool EventKey(GuiPlatform& platform, PuglKeyEvent const& key_event, bool is_down) {
    LogDebug(ModuleName::Gui,
             "key event: key: {}, state: {}, is_down: {}",
             key_event.key,
             key_event.state,
             is_down);
    platform.frame_state.modifiers = CreateModifierFlags(key_event.state);
    if (auto const key_code = RemapKeyCode(key_event.key))
        return EventKeyRegular(platform, *key_code, is_down, CreateModifierFlags(key_event.state));
    return false;
}

static bool EventText(GuiPlatform& platform, PuglTextEvent const& text_event) {
    platform.frame_state.modifiers = CreateModifierFlags(text_event.state);
    dyn::Append(platform.frame_state.input_utf32_chars, text_event.character);
    if (platform.last_result.wants_keyboard_input) return true;
    return false;
}

static void CreateGraphicsContext(GuiPlatform& platform) {
    ZoneScoped;
    auto graphics_ctx = graphics::CreateNewDrawContext();
    auto const outcome = graphics_ctx->CreateDeviceObjects((void*)puglGetNativeView(platform.view));
    if (outcome.HasError()) {
        LogError(ModuleName::Gui, "Failed to create graphics context: {}", outcome.Error());
        delete graphics_ctx;
        return;
    }
    platform.graphics_ctx = graphics_ctx;
}

static void DestroyGraphicsContext(GuiPlatform& platform) {
    ZoneScoped;
    if (platform.graphics_ctx) {
        platform.graphics_ctx->DestroyDeviceObjects();
        delete platform.graphics_ctx;
        platform.graphics_ctx = nullptr;
    }
}

// Data offer is where we decide if we want to accept data from the OS.
static bool EventDataOffer(GuiPlatform& platform, PuglDataOfferEvent const& data_offer) {
    bool result = false;
    for (auto const type_index : Range(puglGetNumClipboardTypes(platform.view))) {
        auto const type = puglGetClipboardType(platform.view, type_index);
        LogDebug(ModuleName::Gui,
                 "clipboard data is being offered, type: {}, time: {}",
                 type,
                 data_offer.time);
        if (NullTermStringsEqual(type, "text/plain")) {
            puglAcceptOffer(platform.view, &data_offer, type_index);
            result = true;
        }
    }
    return result;
}

// After we've accepted an offer, we get the data.
static bool EventData(GuiPlatform& platform, PuglDataEvent const& data_event) {
    auto const type_index = data_event.typeIndex;
    auto const type = puglGetClipboardType(platform.view, type_index);
    LogDebug(ModuleName::Gui, "clipboard data received, type: {}, time: {}", type, data_event.time);
    if (NullTermStringsEqual(type, "text/plain")) {
        usize size = 0;
        void const* data = puglGetClipboard(platform.view, type_index, &size);
        if (data && size) {
            dyn::Assign(platform.frame_state.clipboard_text, String {(char const*)data, size});
            return true;
        }
    }
    return false;
}

static void BeginFrame(GuiFrameInput& frame_state) {
    if (All(frame_state.cursor_pos < f32x2 {0, 0} || frame_state.cursor_pos_prev < f32x2 {0, 0})) {
        // if mouse just appeared or disappeared (negative coordinate) we cancel out movement by setting
        // to zero
        frame_state.cursor_delta = {0, 0};
    } else {
        frame_state.cursor_delta = frame_state.cursor_pos - frame_state.cursor_pos_prev;
    }
    frame_state.cursor_pos_prev = frame_state.cursor_pos;

    frame_state.current_time = TimePoint::Now();

    if (frame_state.time_prev)
        frame_state.delta_time = (f32)(frame_state.current_time - frame_state.time_prev);
    else
        frame_state.delta_time = 0;
    frame_state.time_prev = frame_state.current_time;
}

static void ClearImpermanentState(GuiFrameInput& frame_state) {
    for (auto& btn : frame_state.mouse_buttons) {
        btn.dragging_started = false;
        btn.dragging_ended = false;
        btn.presses.Clear();
        btn.releases.Clear();
    }

    for (auto& key : frame_state.keys) {
        key.presses.Clear();
        key.releases.Clear();
        key.presses_or_repeats.Clear();
    }

    frame_state.file_picker_results.Clear();
    frame_state.input_utf32_chars = {};
    frame_state.mouse_scroll_delta_in_lines = 0;
    dyn::Clear(frame_state.clipboard_text);
    frame_state.event_arena.ResetCursorAndConsolidateRegions();
    ++frame_state.update_count;
}

static void HandlePostUpdateRequests(GuiPlatform& platform) {
    if (platform.last_result.cursor_type != platform.current_cursor) {
        platform.current_cursor = platform.last_result.cursor_type;
        puglSetCursor(platform.view, ({
                          PuglCursor cursor = PUGL_CURSOR_ARROW;
                          switch (platform.last_result.cursor_type) {
                              case CursorType::Default: cursor = PUGL_CURSOR_ARROW; break;
                              case CursorType::Hand: cursor = PUGL_CURSOR_HAND; break;
                              case CursorType::IBeam: cursor = PUGL_CURSOR_CARET; break;
                              case CursorType::AllArrows: cursor = PUGL_CURSOR_ALL_SCROLL; break;
                              case CursorType::HorizontalArrows: cursor = PUGL_CURSOR_LEFT_RIGHT; break;
                              case CursorType::VerticalArrows: cursor = PUGL_CURSOR_UP_DOWN; break;
                              case CursorType::Count: break;
                          }
                          cursor;
                      }));
    }

    if (platform.last_result.wants_keyboard_input) {
        if (!puglHasFocus(platform.view)) {
            auto const result = puglGrabFocus(platform.view);
            if (result != PUGL_SUCCESS) LogWarning(ModuleName::Gui, "failed to grab focus: {}", result);
        }
        if constexpr (IS_WINDOWS) {
            if (!platform.windows_keyboard_hook_added) {
                detail::AddWindowsKeyboardHook(platform);
                platform.windows_keyboard_hook_added = true;
            }
        }
    }

    if (platform.last_result.wants_clipboard_text_paste) {
        LogDebug(ModuleName::Gui, "requesting OS to give us clipboard");
        // IMPORTANT: this will call into our event handler function right from here rather than queue things
        // up
        puglPaste(platform.view);
    }

    if (auto const cb = platform.last_result.set_clipboard_text; cb.size) {
        LogDebug(ModuleName::Gui, "requesting copy into OS clipboard, size: {}", cb.size);
        puglSetClipboard(platform.view, IS_LINUX ? "UTF8_STRING" : "text/plain", cb.data, cb.size);
    }

    if (platform.last_result.file_picker_dialog)
        if (auto const o = OpenNativeFilePicker(platform, *platform.last_result.file_picker_dialog);
            o.HasError()) {
            ReportError(ErrorLevel::Error,
                        SourceLocationHash(),
                        "Failed to open file picker dialog: {}",
                        o.Error());
        }
}

static void UpdateAndRender(GuiPlatform& platform) {
    if (!platform.graphics_ctx) return;
    if constexpr (!IS_MACOS) // doesn't seem to work on macOS
        if (!puglGetVisible(platform.view)) return;

    Stopwatch sw {};
    DEFER {
        auto const elapsed = sw.MillisecondsElapsed();
        if (elapsed > 10) LogWarning(ModuleName::Gui, "GUI update took {}ms", elapsed);
    };

    auto const window_size = GetSize(platform);
    ASSERT(window_size.width >= k_min_gui_width && window_size.width <= k_max_gui_width);

    // We delete our textures if the window size changes because we want to scale up all fonts/images to be
    // more appropriate for the new window size. We could be smarter about this in the future.
    if (platform.frame_state.window_size != window_size) platform.graphics_ctx->DestroyDeviceObjects();

    platform.frame_state.graphics_ctx = platform.graphics_ctx;
    platform.frame_state.native_window = (void*)puglGetNativeView(platform.view);
    platform.frame_state.window_size = window_size;
    platform.frame_state.pugl_view = platform.view;

    u32 num_repeats = 0;
    do {
        // Mostly we'd only expect 1 or 2 updates but we set a hard limit of 4 as a fallback.
        if (num_repeats++ >= 4) {
            LogWarning(ModuleName::Gui, "GUI update loop repeated too many times");
            break;
        }

        ZoneNamedN(repeat, "Update", true);

        BeginFrame(platform.frame_state);

        platform.last_result = GuiUpdate(&*platform.gui);

        // clear the state ready for new events, and to ensure they're only processed once
        ClearImpermanentState(platform.frame_state);

        // it's important to do this after clearing the impermanent state because this might add new events to
        // the frame
        HandlePostUpdateRequests(platform);
    } while (platform.last_result.update_request == GuiFrameResult::UpdateRequest::ImmediatelyUpdate);

    if (platform.last_result.draw_data.draw_lists.size) {
        ZoneNamedN(render, "render", true);
        auto o = platform.graphics_ctx->Render(platform.last_result.draw_data, window_size);
        if (o.HasError()) LogError(ModuleName::Gui, "GUI render failed: {}", o.Error());
    }

    platform.first_update_made = true;
}

static PuglStatus EventHandler(PuglView* view, PuglEvent const* event) {
    ZoneScoped;
    ZoneNameF("%s", PuglEventString(event->type));
    if (PanicOccurred()) return PUGL_FAILURE;

    if (!EnterLogicalMainThread()) return PUGL_FAILURE;
    DEFER { LeaveLogicalMainThread(); };

    try {
        auto& platform = *(GuiPlatform*)puglGetHandle(view);

        bool post_redisplay = false;

        switch (event->type) {
            case PUGL_NOTHING: break;

            case PUGL_REALIZE: {
                LogDebug(ModuleName::Gui, "realize: {}", fmt::DumpStruct(event->any));
                CreateGraphicsContext(platform);
                break;
            }

            case PUGL_UNREALIZE: {
                LogDebug(ModuleName::Gui, "unrealize {}", fmt::DumpStruct(event->any));
                DestroyGraphicsContext(platform);
                break;
            }

            // resized or moved
            case PUGL_CONFIGURE: {
                auto const& configure = event->configure;
                if constexpr (IS_LINUX) {
                    // It seems that at this stage, our view might not have the same size as the configure
                    // struct indicates. Not sure if this is a bug or not.
                    puglSetSizeHint(view, PUGL_CURRENT_SIZE, configure.width, configure.height);
                }
                LogDebug(ModuleName::Gui, "configure {}", fmt::DumpStruct(configure));
                auto const size = NearestAspectRatioSizeInsideSize({configure.width, configure.height},
                                                                   DesiredAspectRatio(platform.prefs));

                if (size && size->width >= k_min_gui_width && size->width <= k_max_gui_width) {
                    prefs::SetValue(platform.prefs,
                                    SettingDescriptor(GuiSetting::WindowWidth),
                                    (s64)size->width);
                } else {
                    LogWarning(ModuleName::Gui,
                               "resized to an invalid size: {} x {}",
                               configure.width,
                               configure.height);
                }

                break;
            }

            case PUGL_UPDATE: {
                break;
            }

            case PUGL_EXPOSE: {
                // On Windows, this event handler might be called from inside itself.
                if (platform.inside_update) return PUGL_SUCCESS;

                platform.inside_update = true;
                UpdateAndRender(platform);
                platform.inside_update = false;
                break;
            }

            case PUGL_CLOSE: {
                // If we support floating windows, we might need to call the host's closed() function here.
                break;
            }

            case PUGL_FOCUS_IN:
            case PUGL_FOCUS_OUT: {
                platform.frame_state.Reset();
                break;
            }

            case PUGL_KEY_PRESS: {
                post_redisplay = EventKey(platform, event->key, true);
                break;
            }

            case PUGL_KEY_RELEASE: {
                post_redisplay = EventKey(platform, event->key, false);
                break;
            }

            case PUGL_TEXT: {
                post_redisplay = EventText(platform, event->text);
                break;
            }

            case PUGL_POINTER_IN: {
                break;
            }
            case PUGL_POINTER_OUT: break;

            case PUGL_BUTTON_PRESS:
            case PUGL_BUTTON_RELEASE: {
                post_redisplay = EventMouseButton(platform, event->button, event->type == PUGL_BUTTON_PRESS);
                break;
            }

            case PUGL_MOTION: {
                post_redisplay = EventMotion(platform, event->motion);
                break;
            }

            case PUGL_SCROLL: {
                post_redisplay = EventWheel(platform, event->scroll);
                break;
            }

            case PUGL_TIMER: {
                if (event->timer.id == platform.k_pugl_timer_id) post_redisplay = IsUpdateNeeded(platform);
                break;
            }

            case PUGL_DATA_OFFER: {
                post_redisplay = EventDataOffer(platform, event->offer);
                break;
            }

            case PUGL_DATA: {
                post_redisplay = EventData(platform, event->data);
                break;
            }

            case PUGL_CLIENT: {
                post_redisplay =
                    NativeFilePickerOnClientMessage(platform, event->client.data1, event->client.data2);
                break;
            }

            case PUGL_LOOP_ENTER: {
                break;
            }
            case PUGL_LOOP_LEAVE: {
                break;
            }
        }

        if (post_redisplay) puglObscureView(view);

        return PUGL_SUCCESS;
    } catch (PanicException) {
        return PUGL_FAILURE;
    }
}

} // namespace detail
