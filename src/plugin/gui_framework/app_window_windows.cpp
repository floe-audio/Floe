// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
//
#include <shlobj.h>

#include "os/undef_windows_macros.h"
//

#include <pugl/pugl.h>

extern "C" {
#include <win.h>
}

#include "foundation/foundation.hpp"
#include "os/misc_windows.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "app_window.hpp"

namespace native {

struct NativeFilePicker {
    bool running {};
    HANDLE thread {};
    FilePickerDialogOptions args {};
    HWND parent {};
    ArenaAllocator thread_arena {Malloc::Instance(), 256};
    Span<MutableString> result {};
};

constexpr uintptr k_file_picker_message_data = 0xD1A106;

#define HRESULT_TRY(windows_call)                                                                            \
    if (auto hr = windows_call; !SUCCEEDED(hr)) {                                                            \
        return Win32ErrorCode(HresultToWin32(hr), #windows_call);                                            \
    }

f64 DoubleClickTimeMs(AppWindow const&) {
    auto result = GetDoubleClickTime();
    if (result == 0) result = 300;
    return result;
}

// This struct is based on the Visage DpiAwareness.
// Copyright Vital Audio, LLC
// SPDX-License-Identifier: MIT
struct DpiAwareness {
    using GetWindowDpiAwarenessContextFunc = DPI_AWARENESS_CONTEXT(WINAPI*)(HWND);
    using GetThreadDpiAwarenessContextFunc = DPI_AWARENESS_CONTEXT(WINAPI*)();
    using SetThreadDpiAwarenessContextFunc = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);
    using GetDpiForSystemFunc = UINT(WINAPI*)();
    using GetSystemMetricsForDpiFunc = INT(WINAPI*)(INT, UINT);

    DpiAwareness() {
        HMODULE user32 = LoadLibraryA("user32.dll");
        if (!user32) return;

        thread_dpi_awareness_context =
            Procedure<GetThreadDpiAwarenessContextFunc>(user32, "GetThreadDpiAwarenessContext");
        set_thread_dpi_awareness_context =
            Procedure<SetThreadDpiAwarenessContextFunc>(user32, "SetThreadDpiAwarenessContext");
        dpi_for_window = Procedure<GetDpiForWindowFunc>(user32, "GetDpiForWindow");
        dpi_for_system = Procedure<GetDpiForSystemFunc>(user32, "GetDpiForSystem");
        if (!thread_dpi_awareness_context || !set_thread_dpi_awareness_context || !dpi_for_window ||
            !dpi_for_system) {
            LogDebug(ModuleName::Gui, "no DPI functions in user32");
            return;
        }

        previous_dpi_awareness = thread_dpi_awareness_context();
        dpi_awareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
        if (!set_thread_dpi_awareness_context(dpi_awareness)) {
            dpi_awareness = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;
            set_thread_dpi_awareness_context(dpi_awareness);
        }
    }

    ~DpiAwareness() {
        if (set_thread_dpi_awareness_context) set_thread_dpi_awareness_context(previous_dpi_awareness);
    }

    Optional<float> Dpi() const {
        if (!dpi_awareness) return k_nullopt;
        return (f32)dpi_for_system();
    }

    Optional<float> Dpi(HWND hwnd) const {
        if (!dpi_awareness) return k_nullopt;
        return (f32)dpi_for_window(hwnd);
    }

    template <typename T>
    static T Procedure(HMODULE module, LPCSTR proc_name) {
        return (T)(void*)(GetProcAddress(module, proc_name));
    }

    DPI_AWARENESS_CONTEXT dpi_awareness {};
    DPI_AWARENESS_CONTEXT previous_dpi_awareness {};
    GetThreadDpiAwarenessContextFunc thread_dpi_awareness_context {};
    SetThreadDpiAwarenessContextFunc set_thread_dpi_awareness_context {};
    GetDpiForWindowFunc dpi_for_window {};
    GetDpiForSystemFunc dpi_for_system {};
};

UiSize DefaultUiSizeFromDpi(void* native_window) {
    DpiAwareness dpi_awareness {};

    auto const dpi =
        (native_window ? dpi_awareness.Dpi((HWND)native_window) : dpi_awareness.Dpi()).ValueOr(k_default_dpi);

    auto const backing_scale = dpi / 96.0f;

    auto const screen_size = ({
        UiSize sz {};

        if (native_window) {
            auto const monitor = MonitorFromWindow((HWND)native_window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info {};
            info.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoA(monitor, &info)) {
                auto const r = info.rcMonitor;
                sz = {
                    CheckedCast<u16>((f32)(r.right - r.left) * backing_scale),
                    CheckedCast<u16>((f32)(r.bottom - r.top) * backing_scale),
                };
            }
        }

        // Fallback to default screen size.
        if (!sz.width) {
            sz = {CheckedCast<u16>((f32)GetSystemMetrics(SM_CXSCREEN) * backing_scale),
                  CheckedCast<u16>((f32)GetSystemMetrics(SM_CYSCREEN) * backing_scale)};
        }

        sz;
    });

    return DefaultUiSizeInternal(screen_size, dpi);
}

void CloseNativeFilePicker(AppWindow& window) {
    if (!window.native_file_picker) return;
    auto& native = window.native_file_picker->As<NativeFilePicker>();
    if (native.thread) {
        PostThreadMessageW(GetThreadId(native.thread), WM_CLOSE, 0, 0);
        // Blocking wait for the thread to finish.
        auto const wait_result = WaitForSingleObject(native.thread, INFINITE);
        ASSERT_NEQ(wait_result, WAIT_FAILED);
        CloseHandle(native.thread);
    }
    native.~NativeFilePicker();
    window.native_file_picker.Clear();
}

ErrorCodeOr<Span<MutableString>>
RunFilePicker(FilePickerDialogOptions const& args, ArenaAllocator& arena, HWND parent) {
    auto const ids = ({
        struct Ids {
            IID rclsid;
            IID riid;
        };
        Ids i;
        switch (args.type) {
            case FilePickerDialogOptions::Type::SaveFile: {
                i.rclsid = CLSID_FileSaveDialog;
                i.riid = IID_IFileSaveDialog;
                break;
            }
            case FilePickerDialogOptions::Type::OpenFile:
            case FilePickerDialogOptions::Type::SelectFolder: {
                i.rclsid = CLSID_FileOpenDialog;
                i.riid = IID_IFileOpenDialog;
                break;
            }
        }
        i;
    });

    IFileDialog* f {};
    HRESULT_TRY(CoCreateInstance(ids.rclsid, nullptr, CLSCTX_ALL, ids.riid, (void**)&f));
    ASSERT(f);
    DEFER { f->Release(); };

    if (args.default_folder) {
        ASSERT(args.default_folder->size);
        ASSERT(IsValidUtf8(*args.default_folder));
        ASSERT(path::IsAbsolute(*args.default_folder));

        PathArena temp_path_arena {Malloc::Instance()};

        auto wide_dir = WidenAllocNullTerm(temp_path_arena, *args.default_folder).Value();
        Replace(wide_dir, L'/', L'\\');
        IShellItem* item = nullptr;
        // SHCreateItemFromParsingName can fail with ERROR_FILE_NOT_FOUND. We only set the default folder
        // if it succeeds.
        if (auto const hr = SHCreateItemFromParsingName(wide_dir.data, nullptr, IID_PPV_ARGS(&item));
            SUCCEEDED(hr)) {
            ASSERT(item);
            DEFER { item->Release(); };

            constexpr bool k_forced_default_folder = false;
            if constexpr (k_forced_default_folder)
                f->SetFolder(item);
            else
                f->SetDefaultFolder(item);
        }
    }

    if (args.default_filename && args.type == FilePickerDialogOptions::Type::SaveFile) {
        PathArena temp_path_arena {Malloc::Instance()};
        auto wide_filename = WidenAllocNullTerm(temp_path_arena, *args.default_filename).Value();
        f->SetFileName(wide_filename.data);
    }

    if (args.filters.size) {
        PathArena temp_path_arena {Malloc::Instance()};
        DynamicArray<COMDLG_FILTERSPEC> win32_filters {temp_path_arena};
        win32_filters.Reserve(args.filters.size);
        for (auto filter : args.filters) {
            dyn::Append(
                win32_filters,
                COMDLG_FILTERSPEC {
                    .pszName = WidenAllocNullTerm(temp_path_arena, filter.description).Value().data,
                    .pszSpec = WidenAllocNullTerm(temp_path_arena, filter.wildcard_filter).Value().data,
                });
        }
        f->SetFileTypes((UINT)win32_filters.size, win32_filters.data);
    }

    {
        PathArena temp_path_arena {Malloc::Instance()};
        auto wide_title = WidenAllocNullTerm(temp_path_arena, args.title).Value();
        HRESULT_TRY(f->SetTitle(wide_title.data));
    }

    {
        DWORD flags = 0;
        HRESULT_TRY(f->GetOptions(&flags));
        HRESULT_TRY(f->SetOptions(flags | FOS_FORCEFILESYSTEM));
    }

    if (args.type == FilePickerDialogOptions::Type::SelectFolder) {
        DWORD flags = 0;
        HRESULT_TRY(f->GetOptions(&flags));
        HRESULT_TRY(f->SetOptions(flags | FOS_PICKFOLDERS));
    }

    auto const multiple_selection = ids.rclsid == CLSID_FileOpenDialog && args.allow_multiple_selection;
    if (multiple_selection) {
        DWORD flags = 0;
        HRESULT_TRY(f->GetOptions(&flags));
        HRESULT_TRY(f->SetOptions(flags | FOS_ALLOWMULTISELECT));
    }

    if (parent) ASSERT(IsWindow(parent));

    if (auto hr = f->Show(parent); hr != S_OK) {
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return Span<MutableString> {};
        return Win32ErrorCode(HresultToWin32(hr), "Show()");
    }

    auto utf8_path_from_shell_item = [&](IShellItem* p_item) -> ErrorCodeOr<MutableString> {
        PWSTR wide_path = nullptr;
        HRESULT_TRY(p_item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path));
        DEFER { CoTaskMemFree(wide_path); };

        auto narrow_path = Narrow(arena, FromNullTerminated(wide_path)).Value();
        narrow_path.size = path::TrimDirectorySeparatorsEnd(narrow_path).size;
        ASSERT(path::IsAbsolute(narrow_path));
        return narrow_path;
    };

    if (!multiple_selection) {
        IShellItem* p_item = nullptr;
        HRESULT_TRY(f->GetResult(&p_item));
        DEFER { p_item->Release(); };

        auto span = arena.AllocateExactSizeUninitialised<MutableString>(1);
        span[0] = arena.Clone(TRY(utf8_path_from_shell_item(p_item)));
        return span;
    } else {
        IShellItemArray* p_items = nullptr;
        HRESULT_TRY(((IFileOpenDialog*)f)->GetResults(&p_items));
        DEFER { p_items->Release(); };

        DWORD count;
        HRESULT_TRY(p_items->GetCount(&count));
        auto result = arena.AllocateExactSizeUninitialised<MutableString>(CheckedCast<usize>(count));
        for (auto const item_index : Range(count)) {
            IShellItem* p_item = nullptr;
            HRESULT_TRY(p_items->GetItemAt(item_index, &p_item));
            DEFER { p_item->Release(); };

            result[item_index] = arena.Clone(TRY(utf8_path_from_shell_item(p_item)));
        }
        return result;
    }
}

bool NativeFilePickerOnClientMessage(AppWindow& window, uintptr data1, uintptr data2) {
    ASSERT(g_is_logical_main_thread);

    if (data1 != k_file_picker_message_data) return false;
    if (data2 != k_file_picker_message_data) return false;
    if (!window.native_file_picker) return false;

    auto& native_file_picker = window.native_file_picker->As<NativeFilePicker>();

    // The thread should have exited by now so this should be immediate.
    auto const wait_result = WaitForSingleObject(native_file_picker.thread, INFINITE);
    ASSERT_NEQ(wait_result, WAIT_FAILED);
    CloseHandle(native_file_picker.thread);
    native_file_picker.thread = nullptr;

    window.frame_state.file_picker_results.Clear();
    window.file_picker_result_arena.ResetCursorAndConsolidateRegions();
    for (auto const path : native_file_picker.result)
        window.frame_state.file_picker_results.Append(path.Clone(window.file_picker_result_arena),
                                                      window.file_picker_result_arena);
    native_file_picker.running = false;

    return false;
}

// COM initialisation is confusing. To help clear things up:
// - "Apartment" is a term used in COM to describe a threading isolation model.
// - CoInitializeEx sets the apartment model for the calling thread.
// - COINIT_APARTMENTTHREADED (0x2) creates a Single-Threaded Apartment (STA):
//   - Objects can only be accessed by the thread that created them
//   - COM provides message pumping infrastructure
//   - Access from other threads is marshaled through the message queue
// - COINIT_MULTITHREADED (0x0) creates a Multi-Threaded Apartment (MTA):
//   - Objects can be accessed by any thread in the MTA
//   - No automatic message marshaling or pumping
//   - Objects must implement their own thread synchronization
// - UI components like dialogs require a message pump, so they must be used in an STA.
//   Microsoft states:
//     "Note: The multi-threaded apartment is intended for use by non-GUI threads. Threads in
//     multi-threaded apartments should not perform UI actions. This is because UI threads require a
//     message pump, and COM does not pump messages for threads in a multi-threaded apartment."
//   By "multi-threaded apartment" they mean COINIT_MULTITHREADED.
//
// For UI components like IFileDialog, we need COM with COINIT_APARTMENTTHREADED. If the main thread
// thread is already initialised with COINIT_MULTITHREADED, we _cannot_ use UI components because the
// thread does not have a message pump.
//
// As an audio plugin, we can't know for sure the state of COM when we're called. So for robustness, we
// need to create a new thread to handle the file picker where we can guarantee the correct COM.
//
// Some additional information regarding IFileDialog:
// - IFileDialog::Show() will block until the dialog is closed.
// - IFileDialog::Show() will pump it's own messages, but first it _requires_ you to pump messages for the
//   parent HWND that you pass in. You will be sent WM_SHOWWINDOW for example. You must consume this event
//   otherwise IFileDialog::Show() will block forever, and never show it's own dialog.

ErrorCodeOr<void> OpenNativeFilePicker(AppWindow& window, FilePickerDialogOptions const& args) {
    ASSERT(g_is_logical_main_thread);

    NativeFilePicker* native_file_picker = nullptr;

    if (!window.native_file_picker) {
        window.native_file_picker.Emplace(); // Create the OpaqueHandle
        native_file_picker = &window.native_file_picker->As<NativeFilePicker>();
        PLACEMENT_NEW(native_file_picker)
        NativeFilePicker {}; // Initialise the NativeFilePicker in the OpaqueHandle
    } else {
        native_file_picker = &window.native_file_picker->As<NativeFilePicker>();
    }

    if (native_file_picker->running) {
        // Already open. We only allow one at a time.
        return k_success;
    }

    ASSERT(!native_file_picker->thread);
    native_file_picker->running = true;
    native_file_picker->thread_arena.ResetCursorAndConsolidateRegions();
    native_file_picker->args = args.Clone(native_file_picker->thread_arena, CloneType::Deep);
    native_file_picker->parent = (HWND)puglGetNativeView(window.view);
    native_file_picker->thread = CreateThread(
        nullptr,
        0,
        [](void* p) -> DWORD {
            try {
                auto& window = *(AppWindow*)p;
                auto& native_file_picker = window.native_file_picker->As<NativeFilePicker>();

                auto const hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                ASSERT(SUCCEEDED(hr), "new thread couldn't initialise COM");
                DEFER { CoUninitialize(); };

                native_file_picker.result = TRY_OR(
                    RunFilePicker(native_file_picker.args,
                                  native_file_picker.thread_arena,
                                  native_file_picker.parent),
                    {
                        ReportError(ErrorLevel::Error, SourceLocationHash(), "file picker failed: {}", error);
                        return 0;
                    });

                // We have results, now we need to send them back to the main thread.
                PuglEvent const event {
                    .client =
                        {
                            .type = PUGL_CLIENT,
                            .flags = PUGL_IS_SEND_EVENT,
                            .data1 = k_file_picker_message_data,
                            .data2 = k_file_picker_message_data,
                        },
                };
                // This can fail in very rare cases. I don't know the exact reason, but I don't think it is a
                // problem. It just means the file picker result won't be processed. I think this occurs when
                // the GUI is being destroyed - a case where we don't care about the file picker result
                // anyways.
                puglSendEvent(window.view, &event);

                return 0;
            } catch (PanicException) {
            }
            return 0;
        },
        &window,
        0,
        nullptr);
    ASSERT(native_file_picker->thread);

    return k_success;
}

static Optional<KeyCode> WindowsVkToKeyCode(WPARAM vk) {
    switch (vk) {
        case VK_TAB: return KeyCode::Tab;
        case VK_LEFT: return KeyCode::LeftArrow;
        case VK_RIGHT: return KeyCode::RightArrow;
        case VK_UP: return KeyCode::UpArrow;
        case VK_DOWN: return KeyCode::DownArrow;
        case VK_PRIOR: return KeyCode::PageUp; // Page Up
        case VK_NEXT: return KeyCode::PageDown; // Page Down
        case VK_HOME: return KeyCode::Home;
        case VK_END: return KeyCode::End;
        case VK_DELETE: return KeyCode::Delete;
        case VK_BACK: return KeyCode::Backspace;
        case VK_RETURN: return KeyCode::Enter;
        case VK_ESCAPE: return KeyCode::Escape;
        case VK_F1: return KeyCode::F1;
        case VK_F2: return KeyCode::F2;
        case VK_F3: return KeyCode::F3;
        case VK_LSHIFT: return KeyCode::ShiftL;
        case VK_RSHIFT: return KeyCode::ShiftR;
        case 'A': return KeyCode::A;
        case 'C': return KeyCode::C;
        case 'V': return KeyCode::V;
        case 'X': return KeyCode::X;
        case 'Y': return KeyCode::Y;
        case 'Z': return KeyCode::Z;
        case 'F': return KeyCode::F;
    }
    return k_nullopt;
}

static bool HandleMessage(MSG const& msg, int code, WPARAM w_param) {
    if (PanicOccurred()) return false;

    if (!EnterLogicalMainThread()) return false;
    DEFER { LeaveLogicalMainThread(); };

    try {
        // "If code is HC_ACTION, the hook procedure must process the message"
        if (code != HC_ACTION) return false;

        // "The message has been removed from the queue." We only want to process messages that aren't
        // otherwise going to be processed.
        if (w_param != PM_REMOVE) return false;

        if (!msg.hwnd) return false;

        // We only care about keyboard messages.
        {
            constexpr auto k_accepted_messages =
                Array {(UINT)WM_KEYDOWN, WM_SYSKEYDOWN, WM_KEYUP, WM_SYSKEYUP, WM_CHAR, WM_SYSCHAR};
            if (!Contains(k_accepted_messages, msg.message)) return false;
        }

        // We only care about messages to our window.
        {
            constexpr auto k_floe_class_name_len = NullTerminatedSize(AppWindow::k_window_class_name);
            char class_name[k_floe_class_name_len + 1];
            auto const class_name_len = GetClassNameA(msg.hwnd, class_name, sizeof(class_name));
            if (class_name_len == 0) {
                ReportError(ErrorLevel::Warning,
                            SourceLocationHash(),
                            "failed to get class name for hwnd, {}",
                            Win32ErrorCode(GetLastError()));
                return false;
            }

            if (class_name_len != k_floe_class_name_len) return false; // Not our window.
            if (!MemoryIsEqual(class_name, AppWindow::k_window_class_name, k_floe_class_name_len))
                return false; // Not our window.
        }

        ASSERT(g_is_logical_main_thread);

        // WARNING: doing this is not part of Pugl's public API - it might break.
        auto view = (PuglView*)GetWindowLongPtrW(msg.hwnd, GWLP_USERDATA);
        ASSERT(view);
        auto& window = *(AppWindow*)puglGetHandle(view);

        // Determine if we want to consume the original message
        bool const consume_original_message = ({
            bool wants = false;
            switch (msg.message) {
                case WM_CHAR:
                case WM_SYSCHAR: {
                    // Character messages - only consume if we want text input
                    wants = window.last_result.wants.text_input;
                    break;
                }
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    // Key up/down messages - only consume if we want this specific key
                    if (auto const key_code = WindowsVkToKeyCode(msg.wParam))
                        wants = window.last_result.wants.keyboard_keys.Get(ToInt(*key_code));
                    break;
                }
            }
            wants;
        });

        bool consume_char_message = false;
        MSG peeked {};

        // "If the message is translated (that is, a character message is posted to the thread's message
        // queue), the return value is nonzero. If the message is WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, or
        // WM_SYSKEYUP, the return value is nonzero, regardless of the translation."
        if (TranslateMessage(&msg)) {
            // Check if we want the character message that was generated. If we don't want it, leave it in the
            // queue for the host.
            if (window.last_result.wants.text_input) {
                // We check it and, if needed, remove it from queue using PM_REMOVE so host doesn't get it.
                if (PeekMessageW(&peeked, msg.hwnd, WM_CHAR, WM_DEADCHAR, PM_REMOVE) ||
                    PeekMessageW(&peeked, msg.hwnd, WM_SYSCHAR, WM_SYSDEADCHAR, PM_REMOVE)) {
                    consume_char_message = true;
                }
            }
        }

        // Send the messages we want to consume
        if (consume_original_message) SendMessageW(msg.hwnd, msg.message, msg.wParam, msg.lParam);
        if (consume_char_message) SendMessageW(msg.hwnd, peeked.message, peeked.wParam, peeked.lParam);

        // Return true only if we consumed the original message (which will cause MessageHook to scrub it)
        // Character message consumption is handled separately via PeekMessage removal
        return consume_original_message;
    } catch (PanicException) {
        return false;
    }
}

// GetMsgProc
// https://learn.microsoft.com/en-us/windows/win32/winmsg/getmsgproc
static LRESULT CALLBACK MessageHook(int code, WPARAM w_param, LPARAM l_param) {
    auto& msg = *(MSG*)l_param;
    if (HandleMessage(msg, code, w_param)) {
        // "The GetMsgProc hook procedure can examine or modify the message."
        msg = {}; // We scrub it so that no one else gets it.
        return 0;
    }

    return CallNextHookEx(nullptr, code, w_param, l_param);
}

static HHOOK g_keyboard_hook {};
static u32 g_keyboard_hook_ref_count {};

void AddWindowsKeyboardHook(AppWindow& window) {
    ASSERT(g_is_logical_main_thread);

    if (g_keyboard_hook_ref_count++ > 0) return;

    ASSERT(!g_keyboard_hook);

    auto hwnd = (HWND)puglGetNativeView(window.view);
    ASSERT(hwnd);

    HMODULE instance = nullptr;
    bool got_module_handle_from_address = false;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCTSTR)AddWindowsKeyboardHook,
                            &instance)) {
        instance = GetModuleHandleW(nullptr);
    } else {
        got_module_handle_from_address = true;
    }
    ASSERT(instance);

    g_keyboard_hook = SetWindowsHookExW(WH_GETMESSAGE, MessageHook, instance, GetCurrentThreadId());

    if (!g_keyboard_hook) {
        ReportError(ErrorLevel::Warning,
                    SourceLocationHash(),
                    "failed to install keyboard hook (got module handle from address: {}), {}",
                    got_module_handle_from_address,
                    Win32ErrorCode(GetLastError()));
    }
}

void RemoveWindowsKeyboardHook(AppWindow&) {
    ASSERT(g_is_logical_main_thread);

    if (--g_keyboard_hook_ref_count > 0) return;

    // It can be null if it failed.
    if (!g_keyboard_hook) return;

    if (!UnhookWindowsHookEx(g_keyboard_hook)) {
        ReportError(ErrorLevel::Warning,
                    SourceLocationHash(),
                    "failed to remove keyboard hook, {}",
                    Win32ErrorCode(GetLastError()));
    }
    g_keyboard_hook = nullptr;
}

} // namespace native
