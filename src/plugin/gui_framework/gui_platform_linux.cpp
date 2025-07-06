// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <stdio.h>

#include "gui_platform.hpp"

//
#define KeyCode XKeyCode
#include <X11/Xlib.h>

void detail::CloseNativeFilePicker(GuiPlatform&) {}
bool detail::NativeFilePickerOnClientMessage(GuiPlatform&, uintptr, uintptr) { return false; }

f64 detail::DoubleClickTimeMs(GuiPlatform const&) {
    // IMPROVE: get this somehow from the machine
    return 300.0;
}

UiSize detail::DefaultUiSizeFromDpi(GuiPlatform const& platform) {
    // On Linux, use a reasonable hardcoded size: 10 inches * 96 DPI = 960 pixels
    auto target_width = (u16)(k_default_gui_width_inches * 96);
    auto aspect_ratio = DesiredAspectRatio(platform.prefs);
    return SizeWithAspectRatio(target_width, aspect_ratio);
}

ErrorCodeOr<void> detail::OpenNativeFilePicker(GuiPlatform& platform, FilePickerDialogOptions const& args) {
    ASSERT(g_is_logical_main_thread);
    if (platform.native_file_picker) return k_success;

    if (args.default_path) ASSERT(path::IsAbsolute(*args.default_path));

    // This implmentation is blocking, so we only need to check for recursion.
    platform.native_file_picker.Emplace();
    DEFER { platform.native_file_picker.Clear(); };

    platform.frame_state.file_picker_results.Clear();
    platform.file_picker_result_arena.ResetCursorAndConsolidateRegions();

    // IMPROVE: use Gtk Dialog directly instead of zenity so that we can associate the dialog with the window
    // so that there is better UX for the dialog appearing on top of the window.

    // IMPROVE: be more considered with buffer size
    DynamicArrayBounded<char, 3000> command {};
    dyn::AppendSpan(command, "zenity --file-selection "_s);
    fmt::Append(command, "--title=\"{}\" ", args.title);
    if (args.default_path) fmt::Append(command, "--filename=\"{}\" ", *args.default_path);
    for (auto f : args.filters)
        fmt::Append(command, "--file-filter=\"{}|{}\" ", f.description, f.wildcard_filter);

    if (args.allow_multiple_selection) dyn::AppendSpan(command, "--multiple "_s);

    switch (args.type) {
        case FilePickerDialogOptions::Type::SelectFolder: {
            dyn::AppendSpan(command, "--directory "_s);
            break;
        }
        case FilePickerDialogOptions::Type::OpenFile: {
            break;
        }
        case FilePickerDialogOptions::Type::SaveFile: {
            dyn::AppendSpan(command, "--save "_s);
            break;
        }
    }

    // IMPROVE: we don't need to block, we could use a thread.
    // IMPROVE: don't use a fixed size buffer
    // IMPROVE: is fgets safe?

    FILE* f = popen(dyn::NullTerminated(command), "r");
    if (!f) return FilesystemErrnoErrorCode(errno);

    char filenames[8000];
    auto _ = fgets(filenames, ArraySize(filenames), f);
    pclose(f);

    auto output = FromNullTerminated(filenames);
    LogDebug({}, "zenity output: {}", output);

    while (output.size && Last(output) == '\n')
        output.RemoveSuffix(1);

    for (auto part : SplitIterator {output, '|'})
        if (path::IsAbsolute(part))
            platform.frame_state.file_picker_results.Append(platform.file_picker_result_arena.Clone(part),
                                                            platform.file_picker_result_arena);
    return k_success;
}

int detail::FdFromPuglWorld(PuglWorld* world) {
    auto display = (Display*)puglGetNativeWorld(world);
    return ConnectionNumber(display);
}

// The CLAP API says that we need to use XEMBED protocol. Pugl doesn't do that so we need to do it ourselves.
void detail::X11SetParent(PuglView* view, uintptr parent) {
    auto display = (Display*)puglGetNativeWorld(puglGetWorld(view));
    auto window = (Window)puglGetNativeView(view);
    ASSERT(window);

    XReparentWindow(display, window, (Window)parent, 0, 0);
    XFlush(display);

    Atom embed_info_atom = XInternAtom(display, "_XEMBED_INFO", 0);
    constexpr u32 k_xembed_protocol_version = 0;
    constexpr u32 k_xembed_flags = 0;
    u32 embed_info_data[2] = {k_xembed_protocol_version, k_xembed_flags};
    XChangeProperty(display,
                    window,
                    embed_info_atom,
                    embed_info_atom,
                    sizeof(embed_info_data[0]) * 8,
                    PropModeReplace,
                    (u8*)embed_info_data,
                    ArraySize(embed_info_data));
}
