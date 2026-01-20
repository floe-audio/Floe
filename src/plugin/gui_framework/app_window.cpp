// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_window.hpp"

#include <clap/ext/posix-fd-support.h>
#include <clap/ext/timer-support.h>
#include <clap/host.h>
#include <pugl/pugl.h>
#include <pugl/stub.h>

#include "foundation/foundation.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "aspect_ratio.hpp"
#include "engine/engine.hpp"
#include "gui/gui.hpp"
#include "gui/gui_prefs.hpp"
#include "gui_frame.hpp"

// #if !IS_LINUX
// int detail::FdFromPuglWorld(PuglWorld*) { return 0; }
// void detail::X11SetParent(PuglView*, uintptr) {}
// #endif

GuiFrameInput* g_frame_input {};
GuiFrameOutput* g_frame_output {};

GuiFrameIo GuiIo() { return {*g_frame_input, *g_frame_output}; }

void SetGuiIo(GuiFrameInput* in, GuiFrameOutput* out) {
    g_frame_input = in;
    g_frame_output = out;
}

Atomic<bool> g_request_gui_update {};

ErrorCodeCategory const app_window_error_code {
    .category_id = "APPW",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((AppWindowErrorCode)code.code) {
            case AppWindowErrorCode::UnknownError: str = "unknown error"; break;
            case AppWindowErrorCode::Unsupported: str = "unsupported"; break;
            case AppWindowErrorCode::BackendFailed: str = "backend init failed"; break;
            case AppWindowErrorCode::RegistrationFailed: str = "registration failed"; break;
            case AppWindowErrorCode::RealizeFailed: str = "realize failed"; break;
            case AppWindowErrorCode::SetFormatFailed: str = "set format failed"; break;
            case AppWindowErrorCode::CreateContextFailed: str = "create context failed"; break;
        }
        return writer.WriteChars(str);
    },
};

static ErrorCodeOr<void> Required(PuglStatus status) {
    switch (status) {
        case PUGL_SUCCESS: return k_success;

        case PUGL_UNSUPPORTED: return ErrorCode {AppWindowErrorCode::Unsupported};
        case PUGL_FAILURE:
        case PUGL_UNKNOWN_ERROR: return ErrorCode {AppWindowErrorCode::UnknownError};
        case PUGL_BACKEND_FAILED: return ErrorCode {AppWindowErrorCode::BackendFailed};
        case PUGL_REGISTRATION_FAILED: return ErrorCode {AppWindowErrorCode::RegistrationFailed};
        case PUGL_REALIZE_FAILED: return ErrorCode {AppWindowErrorCode::RealizeFailed};
        case PUGL_SET_FORMAT_FAILED: return ErrorCode {AppWindowErrorCode::SetFormatFailed};
        case PUGL_CREATE_CONTEXT_FAILED: return ErrorCode {AppWindowErrorCode::CreateContextFailed};

        // Bugs
        case PUGL_BAD_BACKEND: Panic("Invalid or missing backend");
        case PUGL_BAD_CONFIGURATION: Panic("Invalid view configuration");
        case PUGL_BAD_PARAMETER: Panic("Invalid parameter");
        case PUGL_NO_MEMORY: Panic("Failed to allocate memory");
    }
    return k_success;
}

extern "C" PuglBackend const* puglGlBackend(); // NOLINT

enum class SetTimerType : u8 { Start, Stop };
static void SetTimers(AppWindow& window, SetTimerType type) {
    switch (type) {
        case SetTimerType::Start: {
            // Set the timer if not already running.
            if (!window.pugl_timer_running) {
                if (auto const status =
                        puglStartTimer(window.view, window.k_pugl_timer_id, 1.0 / (f64)k_gui_refresh_rate_hz);
                    status == PUGL_SUCCESS) {
                    window.pugl_timer_running = true;
                } else {
                    ReportError(ErrorLevel::Warning,
                                SourceLocationHash(),
                                "Failed to start Pugl timer: {}",
                                ({
                                    String s {};
                                    switch (status) {
                                        case PUGL_FAILURE: s = "timers not supported by system"; break;
                                        case PUGL_UNKNOWN_ERROR: s = "unknown failure"; break;
                                        default: Panic("unexpected pugl status");
                                    }
                                    s;
                                }));
                };
            }

            // Set the CLAP timer/fd if not already done.
            // https://nakst.gitlab.io/tutorial/clap-part-3.html
            if constexpr (IS_LINUX) {
                if (!window.clap_posix_fd) {
                    if (auto const posix_fd_extension =
                            (clap_host_posix_fd_support const*)
                                window.host.get_extension(&window.host, CLAP_EXT_POSIX_FD_SUPPORT);
                        posix_fd_extension && posix_fd_extension->register_fd) {
                        auto const fd = FdFromPuglWorld(window.world);
                        ASSERT(fd != -1);
                        if (posix_fd_extension->register_fd(&window.host, fd, CLAP_POSIX_FD_READ))
                            window.clap_posix_fd = fd;
                        else
                            LogError(ModuleName::Gui, "failed to register fd {}", fd);
                    }
                }

                if (!window.clap_timer_id) {
                    if (auto const timer_support_extension =
                            (clap_host_timer_support const*)window.host.get_extension(&window.host,
                                                                                      CLAP_EXT_TIMER_SUPPORT);
                        timer_support_extension && timer_support_extension->register_timer) {
                        clap_id timer_id;
                        if (timer_support_extension->register_timer(&window.host,
                                                                    (u32)(1000.0 / k_gui_refresh_rate_hz),
                                                                    &timer_id)) {
                            window.clap_timer_id = timer_id;
                        } else
                            LogError(ModuleName::Gui, "failed to register timer");
                    }
                }
            }
            break;
        }
        case SetTimerType::Stop: {
            if constexpr (IS_LINUX) {
                if (window.clap_posix_fd) {
                    auto const ext = (clap_host_posix_fd_support const*)window.host.get_extension(
                        &window.host,
                        CLAP_EXT_POSIX_FD_SUPPORT);
                    if (ext && ext->unregister_fd) {
                        bool const success = ext->unregister_fd(&window.host, *window.clap_posix_fd);
                        if (!success) LogError(ModuleName::Gui, "failed to unregister fd");
                    }
                    window.clap_posix_fd = k_nullopt;
                }

                if (window.clap_timer_id) {
                    auto const ext =
                        (clap_host_timer_support const*)window.host.get_extension(&window.host,
                                                                                  CLAP_EXT_TIMER_SUPPORT);
                    if (ext && ext->unregister_timer) {
                        bool const success = ext->unregister_timer(&window.host, *window.clap_timer_id);
                        if (!success) LogError(ModuleName::Gui, "failed to unregister timer");
                    }
                    window.clap_timer_id = k_nullopt;
                }
            }

            if (window.view) {
                if (window.pugl_timer_running) {
                    puglStopTimer(window.view, window.k_pugl_timer_id);
                    window.pugl_timer_running = false;
                }
            }
            break;
        }
    }
}

inline FloeClapExtensionHost const* CustomFloeHost(clap_host const& host) {
    if constexpr (PRODUCTION_BUILD) return nullptr;
    return (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
}

static void LogIfSlow(Stopwatch& stopwatch, String message) {
    auto const elapsed = stopwatch.MillisecondsElapsed();
    if (elapsed > 10) LogWarning(ModuleName::Gui, "{} took {}ms", message, elapsed);
}

static bool IsUpdateNeeded(AppWindow& window) {
    bool update_needed = false;

    // Until the GUI has been run, we can't know about its requirements and whether we can be more idle or
    // not.
    if (!window.first_update_made) update_needed = true;

    if (g_request_gui_update.Exchange(false, RmwMemoryOrder::Relaxed)) update_needed = true;

    if (window.last_result.wants.update_interval > GuiFrameOutput::UpdateInterval::Sleep)
        update_needed = true;

    for (usize i = 0; i < window.last_result.timed_wakeups.size;) {
        auto& t = window.last_result.timed_wakeups[i];
        if (TimePoint::Now() >= t) {
            update_needed = true;
            dyn::Remove(window.last_result.timed_wakeups, i);
        } else {
            ++i;
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

static bool EventWheel(AppWindow& window, PuglScrollEvent const& scroll_event) {
    window.frame_state.modifiers = CreateModifierFlags(scroll_event.state);

    // IMPROVE: support horizontal scrolling
    if (scroll_event.direction != PUGL_SCROLL_UP && scroll_event.direction != PUGL_SCROLL_DOWN) return false;

    auto const delta_lines = (f32)scroll_event.dy;
    window.frame_state.mouse_scroll_delta_in_lines += delta_lines;
    if (window.last_result.wants.mouse_scroll) return true;
    return false;
}

static bool EventMotion(AppWindow& window, PuglMotionEvent const& motion_event) {
    window.frame_state.modifiers = CreateModifierFlags(motion_event.state);

    auto const new_cursor_pos = f32x2 {(f32)motion_event.x, (f32)motion_event.y};
    bool result = false;
    window.frame_state.cursor_pos = new_cursor_pos;

    for (auto& btn : window.frame_state.mouse_buttons) {
        if (btn.is_down) {
            if (!btn.is_dragging) btn.dragging_started = true;
            btn.is_dragging = true;
        }
    }

    if (window.last_result.mouse_tracked_rects.size == 0 || window.last_result.wants.mouse_capture) {
        result = true;
    } else if (IsUpdateNeeded(window)) {
        return true;
    } else {
        for (auto const i : Range(window.last_result.mouse_tracked_rects.size)) {
            auto& item = window.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(window.frame_state.cursor_pos);
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

static bool EventMouseButton(AppWindow& window, PuglButtonEvent const& button_event, bool is_down) {
    window.frame_state.modifiers = CreateModifierFlags(button_event.state);

    auto const button = RemapMouseButton(button_event.button);
    if (!button) return false;

    auto& btn = window.frame_state.mouse_buttons[ToInt(*button)];

    auto const now = TimePoint::Now();
    auto const point = f32x2 {(f32)button_event.x, (f32)button_event.y};
    GuiFrameInput::MouseButtonState::Event const e {
        .point = point,
        .time = now,
        .modifiers = window.frame_state.modifiers,
        .is_double_click = is_down
                               ? (All(Abs(btn.last_press.point - point) < f32x2(7)) &&
                                  (e.time - btn.last_press.time) <= (window.double_click_time_ms / 1000.0))
                               : btn.last_press.is_double_click,
    };

    if (e.is_double_click)
        LogDebug(ModuleName::Gui, "Mouse button {} double-clicked at {}, {}", *button, e.point.x, e.point.y);

    if (is_down) {
        btn.is_down = e;
        btn.last_press = e;
        btn.presses.Append(e, window.frame_state.event_arena);
    } else {
        btn.is_down = k_nullopt;
        if (btn.is_dragging) btn.dragging_ended = true;
        btn.is_dragging = false;
        btn.releases.Append(e, window.frame_state.event_arena);
    }

    bool result = false;
    if (window.last_result.mouse_tracked_rects.size == 0 || window.last_result.wants.mouse_capture ||
        (window.last_result.wants.all_left_clicks && button == MouseButton::Left) ||
        (window.last_result.wants.all_right_clicks && button == MouseButton::Right) ||
        (window.last_result.wants.all_middle_clicks && button == MouseButton::Middle)) {
        result = true;
    } else {
        for (auto const i : Range(window.last_result.mouse_tracked_rects.size)) {
            auto& item = window.last_result.mouse_tracked_rects[i];
            bool const mouse_over = item.rect.Contains(window.frame_state.cursor_pos);
            if (mouse_over) {
                result = true;
                break;
            }
        }
    }

    return result;
}

static bool EventKeyRegular(AppWindow& window, KeyCode key_code, bool is_down, ModifierFlags modifiers) {
    auto& key = window.frame_state.keys[ToInt(key_code)];
    if (is_down) {
        key.presses_or_repeats.Append({modifiers}, window.frame_state.event_arena);
        if (!key.is_down) key.presses.Append({modifiers}, window.frame_state.event_arena);
    } else {
        key.releases.Append({modifiers}, window.frame_state.event_arena);
    }
    key.is_down = is_down;

    if (window.last_result.wants.text_input) return true;
    if (window.last_result.wants.keyboard_keys.Get(ToInt(key_code))) return true;
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
        case 'f': return KeyCode::F;
    }
    return k_nullopt;
}

static bool EventKey(AppWindow& window, PuglKeyEvent const& key_event, bool is_down) {
    LogDebug(ModuleName::Gui,
             "key event: key: {}, state: {}, is_down: {}",
             key_event.key,
             key_event.state,
             is_down);
    window.frame_state.modifiers = CreateModifierFlags(key_event.state);
    if (auto const key_code = RemapKeyCode(key_event.key))
        return EventKeyRegular(window, *key_code, is_down, CreateModifierFlags(key_event.state));
    return false;
}

static bool EventText(AppWindow& window, PuglTextEvent const& text_event) {
    window.frame_state.modifiers = CreateModifierFlags(text_event.state);
    dyn::Append(window.frame_state.input_utf32_chars, text_event.character);
    if (window.last_result.wants.text_input) return true;
    return false;
}

static void CreateRenderer(AppWindow& window) {
    ZoneScoped;
    auto renderer = graphics::CreateNewRenderer(window.renderer_backend);

    if (auto const outcome = renderer->Init(GetSize(window),
                                            (void*)puglGetNativeView(window.view),
                                            puglGetNativeWorld(puglGetWorld(window.view)));
        outcome.HasError()) {
        LogError(ModuleName::Gui, "Failed to init renderer: {}", outcome.Error());
        delete renderer;
        return;
    }

    window.renderer = renderer;
}

static void DestroyRenderer(AppWindow& window) {
    ZoneScoped;
    if (window.renderer) {
        window.renderer->Deinit();
        delete window.renderer;
        window.renderer = nullptr;
    }
}

// Data offer is where we decide if we want to accept data from the OS.
static bool EventDataOffer(AppWindow& window, PuglDataOfferEvent const& data_offer) {
    bool result = false;
    for (auto const type_index : Range(puglGetNumClipboardTypes(window.view))) {
        auto const type = puglGetClipboardType(window.view, type_index);
        LogDebug(ModuleName::Gui,
                 "clipboard data is being offered, type: {}, time: {}",
                 type,
                 data_offer.time);
        if (NullTermStringsEqual(type, "text/plain")) {
            puglAcceptOffer(window.view, &data_offer, type_index);
            result = true;
        }
    }
    return result;
}

// After we've accepted an offer, we get the data.
static bool EventData(AppWindow& window, PuglDataEvent const& data_event) {
    auto const type_index = data_event.typeIndex;
    auto const type = puglGetClipboardType(window.view, type_index);
    LogDebug(ModuleName::Gui, "clipboard data received, type: {}, time: {}", type, data_event.time);
    if (NullTermStringsEqual(type, "text/plain")) {
        usize size = 0;
        void const* data = puglGetClipboard(window.view, type_index, &size);
        if (data && size) {
            dyn::Assign(window.frame_state.clipboard_text, String {(char const*)data, size});
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

static void ClearImpermanentState(GuiFrameOutput& frame_output) {
    frame_output.wants = {};
    dyn::Clear(frame_output.mouse_tracked_rects);
    dyn::Clear(frame_output.set_clipboard_text);
    frame_output.file_picker_dialog = k_nullopt;
    frame_output.file_picker_options_arena.ResetCursorAndConsolidateRegions();
    dyn::Clear(frame_output.draw_lists);
}

static void HandlePostUpdateRequests(AppWindow& window) {
    if (window.last_result.wants.cursor_type != window.current_cursor) {
        window.current_cursor = window.last_result.wants.cursor_type;
        puglSetCursor(window.view, ({
                          PuglCursor cursor = PUGL_CURSOR_ARROW;
                          switch (window.last_result.wants.cursor_type) {
                              case CursorType::Default: cursor = PUGL_CURSOR_ARROW; break;
                              case CursorType::Hand: cursor = PUGL_CURSOR_HAND; break;
                              case CursorType::IBeam: cursor = PUGL_CURSOR_CARET; break;
                              case CursorType::AllArrows: cursor = PUGL_CURSOR_ALL_SCROLL; break;
                              case CursorType::HorizontalArrows: cursor = PUGL_CURSOR_LEFT_RIGHT; break;
                              case CursorType::VerticalArrows: cursor = PUGL_CURSOR_UP_DOWN; break;
                              case CursorType::UpLeftDownRight:
                                  cursor = PUGL_CURSOR_UP_LEFT_DOWN_RIGHT;
                                  break;
                              case CursorType::Count: break;
                          }
                          cursor;
                      }));
    }

    if (window.last_result.wants.text_input || window.last_result.wants.keyboard_keys.AnyValuesSet()) {
        if (!puglHasFocus(window.view)) {
            auto const result = puglGrabFocus(window.view);
            if (result != PUGL_SUCCESS) LogWarning(ModuleName::Gui, "failed to grab focus: {}", result);
        }
        if constexpr (IS_WINDOWS) {
            if (!window.windows_keyboard_hook_added) {
                AddWindowsKeyboardHook(window);
                window.windows_keyboard_hook_added = true;
            }
        }
    }

    if (window.last_result.wants.clipboard_text_paste) {
        LogDebug(ModuleName::Gui, "requesting OS to give us clipboard");
        // IMPORTANT: this will call into our event handler function right from here rather than queue things
        // up
        puglPaste(window.view);
    }

    if (window.last_result.set_clipboard_text.size) {
        auto& cb = window.last_result.set_clipboard_text;
        LogDebug(ModuleName::Gui, "requesting copy into OS clipboard, size: {}", cb.size);
        puglSetClipboard(window.view, IS_LINUX ? "UTF8_STRING" : "text/plain", cb.data, cb.size);
    }

    if (window.last_result.file_picker_dialog)
        if (auto const o = OpenNativeFilePicker(window, *window.last_result.file_picker_dialog);
            o.HasError()) {
            ReportError(ErrorLevel::Error,
                        SourceLocationHash(),
                        "Failed to open file picker dialog: {}",
                        o.Error());
        }
}

static void UpdateAndRender(AppWindow& window) {
    if (!window.renderer) return;
    if constexpr (!IS_MACOS) // doesn't seem to work on macOS
        if (!puglGetVisible(window.view)) return;

    Stopwatch sw {};
    DEFER {
        auto const elapsed = sw.MillisecondsElapsed();
        if (elapsed > 10) LogWarning(ModuleName::Gui, "GUI update took {}ms", elapsed);
    };

    auto const window_size = GetSize(window);
    if (window_size.width < k_min_gui_width || window_size.width > k_max_gui_width) {
        // Despite our best efforts, the window size might not be ideal for us.
        // We don't want to handle all the edge cases of tiny or huge windows, so we just don't update.
        return;
    }

    if (window.frame_state.window_size != window_size) {
        // When Floe resizes, all graphics scale up. Resizing is really 'rescaling'. Therefore, we delete our
        // textures if the window size changes so fonts/images are more appropriate for the new window size.
        // Our app knows these resources can disappear at any time and will recreate them.
        window.renderer->DestroyAllTextures();
        window.renderer->DestroyFontTexture();

        // We notify the renderer of the resize here because it gives the renderer freer scope in this
        // PUGL_EXPOSE event rather than in a PUGL_CONFIGURE event (where some graphics usage might be
        // invalid).
        window.renderer->OnResize(window_size, (void*)puglGetNativeView(window.view));
    }

    window.frame_state.renderer = window.renderer;
    window.frame_state.native_window = (void*)puglGetNativeView(window.view);
    window.frame_state.window_size = window_size;
    window.frame_state.pugl_view = window.view;

    u32 num_repeats = 0;
    do {
        // Mostly we'd only expect 1 or 2 updates but we set a hard limit of 4 as a fallback.
        if (num_repeats++ >= 4) {
            LogWarning(ModuleName::Gui, "GUI update loop repeated too many times");
            break;
        }

        ZoneNamedN(repeat, "Update", true);

        BeginFrame(window.frame_state);

        {
            ClearImpermanentState(window.last_result);

            SetGuiIo(&window.frame_state, &window.last_result);
            DEFER { SetGuiIo(nullptr, nullptr); };

            GuiUpdate(&*window.gui);
        }

        // clear the state ready for new events, and to ensure they're only processed once
        ClearImpermanentState(window.frame_state);

        // it's important to do this after clearing the impermanent state because this might add new events to
        // the frame
        HandlePostUpdateRequests(window);
    } while (window.last_result.wants.update_interval == GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);

    if (window.last_result.draw_lists.size) {
        ZoneNamedN(render, "render", true);
        auto o = window.renderer->Render(window.last_result.draw_lists,
                                         window_size,
                                         window.frame_state.native_window);
        if (o.HasError()) LogError(ModuleName::Gui, "GUI render failed: {}", o.Error());
    }

    window.first_update_made = true;
}

static PuglStatus EventHandler(PuglView* view, PuglEvent const* event) {
    ZoneScoped;
    ZoneNameF("%s", PuglEventString(event->type));
    if (PanicOccurred()) return PUGL_FAILURE;

    if (!EnterLogicalMainThread()) return PUGL_FAILURE;
    DEFER { LeaveLogicalMainThread(); };

    try {
        auto& window = *(AppWindow*)puglGetHandle(view);

        bool post_redisplay = false;

        switch (event->type) {
            case PUGL_NOTHING: break;

            case PUGL_REALIZE: {
                LogDebug(ModuleName::Gui, "realize: {}", fmt::DumpStruct(event->any));
                CreateRenderer(window);
                window.frame_state.window_size = GetSize(window);
                break;
            }

            case PUGL_UNREALIZE: {
                LogDebug(ModuleName::Gui, "unrealize {}", fmt::DumpStruct(event->any));
                DestroyRenderer(window);
                break;
            }

            // resized or moved
            case PUGL_CONFIGURE: {
                auto const& configure = event->configure;

                // Despite our best efforts, the window size might not be ideal for us. The OS can allow
                // windows to be resized to non-aspect-ratio sizes or tiny sizes. We need to handle this. We
                // save the size in the preferences because it's likely that this size is the user's request.
                // The prefs descriptor will constrain the width to a valid number, we can just pass it
                // anything.
                prefs::SetValue(window.prefs,
                                SettingDescriptor(GuiSetting::WindowWidth),
                                (s64)configure.width,
                                {.dont_send_on_change_event = true});

                break;
            }

            case PUGL_UPDATE: {
                break;
            }

            case PUGL_EXPOSE: {
                // On Windows, this event handler might be called from inside itself.
                if (window.inside_update) return PUGL_SUCCESS;

                window.inside_update = true;
                UpdateAndRender(window);
                window.inside_update = false;
                break;
            }

            case PUGL_CLOSE: {
                // If we support floating windows, we might need to call the host's closed() function here.
                LogDebug(ModuleName::Gui, "close event");
                break;
            }

            case PUGL_FOCUS_IN:
            case PUGL_FOCUS_OUT: {
                window.frame_state.Reset();
                break;
            }

            case PUGL_KEY_PRESS: {
                post_redisplay = EventKey(window, event->key, true);
                break;
            }

            case PUGL_KEY_RELEASE: {
                post_redisplay = EventKey(window, event->key, false);
                break;
            }

            case PUGL_TEXT: {
                post_redisplay = EventText(window, event->text);
                break;
            }

            case PUGL_POINTER_IN: {
                break;
            }
            case PUGL_POINTER_OUT: break;

            case PUGL_BUTTON_PRESS:
            case PUGL_BUTTON_RELEASE: {
                post_redisplay = EventMouseButton(window, event->button, event->type == PUGL_BUTTON_PRESS);
                break;
            }

            case PUGL_MOTION: {
                post_redisplay = EventMotion(window, event->motion);
                break;
            }

            case PUGL_SCROLL: {
                post_redisplay = EventWheel(window, event->scroll);
                break;
            }

            case PUGL_TIMER: {
                if (event->timer.id == window.k_pugl_timer_id) post_redisplay = IsUpdateNeeded(window);
                break;
            }

            case PUGL_DATA_OFFER: {
                post_redisplay = EventDataOffer(window, event->offer);
                break;
            }

            case PUGL_DATA: {
                post_redisplay = EventData(window, event->data);
                break;
            }

            case PUGL_CLIENT: {
                post_redisplay =
                    NativeFilePickerOnClientMessage(window, event->client.data1, event->client.data2);
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

UiSize DefaultUiSize(AppWindow& window) { return DefaultUiSizeFromDpi(window); }

ErrorCodeOr<void> CreateView(AppWindow& window) {
    Trace(ModuleName::Gui);

    ASSERT(window.world == nullptr);
    ASSERT(window.view == nullptr);
    ASSERT(window.renderer == nullptr);
    ASSERT(!window.gui);
    ASSERT(!window.clap_timer_id);
    ASSERT(!window.clap_posix_fd);

    if (auto const floe_custom_host = CustomFloeHost(window.host)) {
        window.world = (PuglWorld*)floe_custom_host->pugl_world;
        ASSERT(window.world != nullptr);
    } else {
        window.world = puglNewWorld(PUGL_MODULE, 0);
        if (window.world == nullptr) return ErrorCode {AppWindowErrorCode::UnknownError};
        puglSetWorldString(window.world, PUGL_CLASS_NAME, AppWindow::k_window_class_name);
        window.world = window.world;
        LogInfo(ModuleName::Gui, "creating new world");
    }

    window.view = puglNewView(window.world);
    if (window.view == nullptr) Panic("out of memory");

    puglSetViewHint(window.view, PUGL_RESIZABLE, true);
    puglSetPositionHint(window.view, PUGL_DEFAULT_POSITION, 0, 0);

    auto window_size = DesiredWindowSize(window.prefs);
    if (!window_size) window_size = DefaultUiSize(window);
    puglSetSizeHint(window.view, PUGL_DEFAULT_SIZE, window_size->width, window_size->height);
    puglSetSizeHint(window.view, PUGL_CURRENT_SIZE, window_size->width, window_size->height);

    auto const min_size = SizeWithAspectRatio(k_min_gui_width, k_gui_aspect_ratio);
    ASSERT(min_size.width >= k_min_gui_width);
    puglSetSizeHint(window.view, PUGL_MIN_SIZE, min_size.width, min_size.height);

    auto const max_size = SizeWithAspectRatio(k_max_gui_width, k_gui_aspect_ratio);
    puglSetSizeHint(window.view, PUGL_MAX_SIZE, max_size.width, max_size.height);

    puglSetSizeHint(window.view, PUGL_FIXED_ASPECT, k_gui_aspect_ratio.width, k_gui_aspect_ratio.height);

    puglSetHandle(window.view, &window);
    TRY(Required(puglSetEventFunc(window.view, EventHandler)));

    window.renderer_backend = ({
        graphics::RendererBackend b {};

        constexpr bool k_use_experimental_bgfx = false;
        if constexpr (k_use_experimental_bgfx) {
            if constexpr (IS_WINDOWS) b = graphics::RendererBackend::Bgfx;
            if constexpr (IS_LINUX) b = graphics::RendererBackend::Bgfx;
            if constexpr (IS_MACOS) {
                // bgfx only supports macOS 13 (Darwin version 22) and above. We use our old OpenGL backend
                // for older systems. We've only ever seen kernel_version in the format x.x.x, so we can
                // reasonably assume that this simple parsing will work. If it doesn't, it's likely something
                // newer.
                auto const darwin_version = ParseInt(GetOsInfo().kernel_version, ParseIntBase::Decimal);
                if (!darwin_version || darwin_version.Value() >= 22)
                    b = graphics::RendererBackend::Bgfx;
                else
                    b = graphics::RendererBackend::OpenGl;
            }
        } else {
            if constexpr (IS_WINDOWS) b = graphics::RendererBackend::Direct3D9;
            if constexpr (IS_MACOS) b = graphics::RendererBackend::OpenGl;
            if constexpr (IS_LINUX) b = graphics::RendererBackend::OpenGl;
        }
        b;
    });

    LogInfo(ModuleName::Gui, "Selected backend {}", EnumToString(window.renderer_backend));

    switch (window.renderer_backend) {
        case graphics::RendererBackend::OpenGl: {
            if constexpr (!IS_WINDOWS) {
                TRY(Required(puglSetBackend(window.view, puglGlBackend())));
                TRY(Required(puglSetViewHint(window.view, PUGL_CONTEXT_VERSION_MAJOR, 3)));
                TRY(Required(puglSetViewHint(window.view, PUGL_CONTEXT_VERSION_MINOR, 3)));
                TRY(Required(
                    puglSetViewHint(window.view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE)));
                puglSetViewHint(window.view, PUGL_CONTEXT_DEBUG, RUNTIME_SAFETY_CHECKS_ON);
            } else {
                Panic("Bug: OpenGL is not supported on Windows");
            }
            break;
        }
        case graphics::RendererBackend::Bgfx:
        case graphics::RendererBackend::Direct3D9: {
            TRY(Required(puglSetBackend(window.view, puglStubBackend())));
            break;
        }
        case graphics::RendererBackend::Count: PanicIfReached();
    }

    return k_success;
}

void DestroyView(AppWindow& window) {
    Trace(ModuleName::Gui);

    if constexpr (IS_WINDOWS) {
        if (window.windows_keyboard_hook_added) RemoveWindowsKeyboardHook(window);
    }

    CloseNativeFilePicker(window);

    if (window.gui) {
        window.gui.Clear();

        // Free memory.
        window.last_result.draw_list_allocator.Clear();
    }

    SetTimers(window, SetTimerType::Stop);

    if (window.view) {
        // We don't need to check if the view is realized, because puglUnrealize will do nothing if it is not.
        puglUnrealize(window.view);

        puglFreeView(window.view);
        window.view = nullptr;
    }

    window.first_update_made = false;

    if (!CustomFloeHost(window.host)) {
        LogInfo(ModuleName::Gui, "freeing world");
        puglFreeWorld(window.world);
        window.world = nullptr;
    }
}

void OnClapTimer(AppWindow& window, clap_id timer_id) {
    Stopwatch stopwatch {};
    if (window.clap_timer_id && *window.clap_timer_id == timer_id) puglUpdate(window.world, 0);
    LogIfSlow(stopwatch, "OnClapTimer");
}

void OnPosixFd(AppWindow& window, int fd) {
    Stopwatch stopwatch {};
    if (window.clap_posix_fd && fd == *window.clap_posix_fd) puglUpdate(window.world, 0);
    LogIfSlow(stopwatch, "OnPosixFd");
}

ErrorCodeOr<void> SetParent(AppWindow& window, clap_window_t const& new_parent) {
    ASSERT(window.view);
    ASSERT(new_parent.ptr);

    auto const parent = puglGetParent(window.view);
    LogDebug(ModuleName::Gui, "SetParent, current: {}, new: {}", parent, new_parent.ptr);

    if (new_parent.ptr == (void*)parent) return k_success;

    if (parent && new_parent.ptr != (void*)parent) {
        // Pluginval tries to re-parent us. I'm not sure if this is a quirk of pluginval or if it's more
        // common than that. Either way, we try to support it.

        DestroyView(window);
        TRY(CreateView(window));
    }

    ASSERT(!puglGetNativeView(window.view), "SetParent called after window realised");
    // NOTE: "This must be called before puglRealize(), re-parenting is not supported"
    TRY(Required(puglSetParent(window.view, (uintptr)new_parent.ptr)));
    return k_success;
}

bool SetSize(AppWindow& window, UiSize new_size) {
    return puglSetSizeHint(window.view, PUGL_CURRENT_SIZE, new_size.width, new_size.height) == PUGL_SUCCESS;
}

UiSize GetSize(AppWindow& window) {
    auto const size = puglGetSizeHint(window.view, PUGL_CURRENT_SIZE);
    return {size.width, size.height};
}

ErrorCodeOr<void> SetVisible(AppWindow& window, bool visible, Engine& engine) {
    ASSERT(window.view);

    if (visible) {
        // Realize if not already done.
        if (!puglGetNativeView(window.view)) {
            TRY(Required(puglRealize(window.view)));
            window.double_click_time_ms = DoubleClickTimeMs(window);
            if constexpr (IS_LINUX) X11SetParent(window.view, puglGetParent(window.view));
        }

        // Start timers if needed.
        SetTimers(window, SetTimerType::Start);

        // Create GUI if not already done.
        if (!window.gui) window.gui.Emplace(engine);

    } else {
        window.frame_state.Reset();
        CloseNativeFilePicker(window);
        SetTimers(window, SetTimerType::Stop);
    }

    if (puglGetVisible(window.view) == visible) {
        LogInfo(ModuleName::Gui, "SetVisible called with same visibility state, ignoring");
        return k_success;
    }

    if (visible)
        TRY(Required(puglShow(window.view, PUGL_SHOW_PASSIVE)));
    else
        TRY(Required(puglHide(window.view)));

    return k_success;
}
