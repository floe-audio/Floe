// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect  MacRect
#define Delay MacDelay
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#pragma clang diagnostic pop
#undef Rect
#undef Delay

#include "os/misc_mac.hpp"

#include "app_window.hpp"

#define DIALOG_DELEGATE_CLASS MAKE_UNIQUE_OBJC_NAME(DialogDelegate)

@interface DIALOG_DELEGATE_CLASS : NSObject <NSOpenSavePanelDelegate>
@property Span<FilePickerDialogOptions::FileFilter const> filters; // NOLINT
@property Mutex* mutex; // NOLINT
@end

@implementation DIALOG_DELEGATE_CLASS
// Not necessarily main-thread.
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    if (!url) return NO;

    self.mutex->Lock();
    DEFER { self.mutex->Unlock(); };

    if (self.filters.size) {
        if (NSString* ns_filename = [url lastPathComponent]) {
            auto const filename = NSStringToString(ns_filename);
            for (auto const filter : self.filters)
                if (MatchWildcard(filter.wildcard_filter, filename)) return YES;
        }

        if ([url isFileURL]) {
            if (auto const p = [url path]) {
                BOOL is_directory {};
                if ([[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&is_directory])
                    // Accept directories since they allow the user to browse in subfolders, but disable files
                    // since they must have already not matched the filters.
                    return is_directory ? YES : NO;
            }
        }
    }

    // There's either no filters, or there's various strange cases or error branches that could get us to
    // this codepath. We enable the file in any of these cases because it's better to err on the side of
    // giving the user more options.
    return YES;
}
@end

namespace native {

struct NativeFilePicker {
    NSSavePanel* panel = nullptr;
    Mutex mutex;
    DIALOG_DELEGATE_CLASS* delegate = nullptr;
};

constexpr uintptr k_file_picker_completed = 0xD1A106;

bool NativeFilePickerOnClientMessage(AppWindow& window, uintptr data1, uintptr data2) {
    ASSERT(g_is_logical_main_thread);
    if (!window.native_file_picker) return false;
    if (data1 != k_file_picker_completed) return false;

    auto& native_file_picker = window.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return false;

    ASSERT([NSThread isMainThread]);
    ASSERT(!native_file_picker.panel.visible, "panel should be closed");
    window.frame_state.file_picker_results = {};
    window.file_picker_result_arena.ResetCursorAndConsolidateRegions();

    bool update_gui = false;

    auto const response = (NSModalResponse)data2;
    if (response == NSModalResponseOK) {
        auto const append_result = [&](NSURL* url) {
            NSString* path = [[url path] stringByResolvingSymlinksInPath];
            auto utf8 = FromNullTerminated(path.UTF8String);
            ASSERT(path::IsAbsolute(utf8));
            ASSERT(IsValidUtf8(utf8));

            window.frame_state.file_picker_results.Append(utf8.Clone(window.file_picker_result_arena),
                                                          window.file_picker_result_arena);
        };

        if ([native_file_picker.panel isKindOfClass:[NSOpenPanel class]]) {
            auto open_panel = (NSOpenPanel*)native_file_picker.panel;

            for (auto const i : Range<NSUInteger>(open_panel.URLs.count))
                append_result(open_panel.URLs[i]);
        } else {
            auto save_panel = native_file_picker.panel;
            append_result(save_panel.URL);
        }
        update_gui = true;
    }

    native_file_picker.panel = nullptr; // release
    native_file_picker.delegate = nullptr; // release
    window.native_file_picker.Clear();

    return update_gui;
}

ErrorCodeOr<void> OpenNativeFilePicker(AppWindow& window, FilePickerDialogOptions const& options) {
    ASSERT(window.view);
    ASSERT(g_is_logical_main_thread);
    if (window.native_file_picker) return k_success;
    window.native_file_picker.Emplace();
    auto& native_file_picker = window.native_file_picker->As<NativeFilePicker>();

    @try {
        ASSERT([NSThread isMainThread]);

        switch (options.type) {
            case FilePickerDialogOptions::Type::OpenFile:
            case FilePickerDialogOptions::Type::SelectFolder: {
                NSOpenPanel* open_panel = [NSOpenPanel openPanel];
                ASSERT(!native_file_picker.panel);
                native_file_picker.panel = (NSSavePanel*)open_panel; // retain

                native_file_picker.delegate = [[DIALOG_DELEGATE_CLASS alloc] init];
                native_file_picker.delegate.mutex = &native_file_picker.mutex;
                native_file_picker.delegate.filters =
                    window.file_picker_result_arena.Clone(options.filters, CloneType::Deep);
                open_panel.delegate = native_file_picker.delegate;

                open_panel.canChooseDirectories = options.type == FilePickerDialogOptions::Type::SelectFolder;
                open_panel.canChooseFiles = options.type == FilePickerDialogOptions::Type::OpenFile;
                open_panel.canCreateDirectories = YES;
                open_panel.allowsMultipleSelection = options.allow_multiple_selection;
                break;
            }
            case FilePickerDialogOptions::Type::SaveFile: {
                NSSavePanel* save_panel = [NSSavePanel savePanel];
                ASSERT(!native_file_picker.panel);
                native_file_picker.panel = (NSSavePanel*)save_panel; // retain

                if (options.default_filename)
                    save_panel.nameFieldStringValue = StringToNSString(*options.default_filename);
                break;
            }
        }

        auto panel = native_file_picker.panel;

        panel.parentWindow = ((__bridge NSView*)(void*)puglGetNativeView(window.view)).window;
        panel.title = StringToNSString(options.title);
        [panel setLevel:NSModalPanelWindowLevel];
        panel.showsResizeIndicator = YES;
        panel.showsHiddenFiles = NO;
        panel.canCreateDirectories = YES;
        if (options.default_folder)
            panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*options.default_folder)];

        [panel beginWithCompletionHandler:^(NSInteger response) {
          // I don't think we can assert that this is always the main thread, so let's send it via
          // Pugl's event system which should guarantee main thread.
          // This triggers NativeFilePickerOnClientMessage eventually.
          PuglEvent const event {.client = {
                                     .type = PUGL_CLIENT,
                                     .flags = PUGL_IS_SEND_EVENT,
                                     .data1 = k_file_picker_completed,
                                     .data2 = (uintptr)response,
                                 }};
          auto const rc = puglSendEvent(window.view, &event);
          ASSERT(rc == PUGL_SUCCESS);
        }];

    } @catch (NSException* e) {
        LogError(ModuleName::Gui, "error opening native file picker: {}", e.description.UTF8String);
        return ErrorFromNSError([NSError errorWithDomain:@"" code:0 userInfo:nil]);
    }
    return k_success;
}

void CloseNativeFilePicker(AppWindow& window) {
    // On rare ocassions in Logic Pro 11.0.1 (CLAP-as-AUv2), we've actually found it's possible that [NSThread
    // isMainThread] is false here. However, this is still workable so long as it's at least the logical main
    // thread.
    ASSERT(g_is_logical_main_thread);

    if (!window.native_file_picker) return;
    auto& native_file_picker = window.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return;
    {
        native_file_picker.mutex.Lock();
        DEFER { native_file_picker.mutex.Unlock(); };
        if ([NSThread isMainThread]) {
            [native_file_picker.panel close];
        } else {
            LogInfo(ModuleName::Gui, "Closing native file picker from non-main thread");
            // We assume here that there's a main thread regularly pumping the run loop.
            [native_file_picker.panel performSelectorOnMainThread:@selector(close)
                                                       withObject:nil
                                                    waitUntilDone:YES];
        }
        native_file_picker.panel = nullptr; // release
        native_file_picker.delegate = nullptr; // release
    }
    window.native_file_picker.Clear();
}

f64 DoubleClickTimeMs(AppWindow const&) {
    auto result = [NSEvent doubleClickInterval] * 1000.0;
    if (result <= 0) result = 300;
    return result;
}

UiSize DefaultUiSizeFromDpi(void* native_window) {
    auto const screen = ({
        NSScreen* s = nil;

        if (native_window) {
            auto const view = (__bridge NSView*)native_window;
            auto const window = [view window];
            if (window) s = [window screen];
        }

        if (!s && NSApp) {
            auto const key_window = [NSApp keyWindow];
            if (key_window) s = [key_window screen];
        }
        if (!s) s = [NSScreen mainScreen];
        if (!s) {
            auto const screens = [NSScreen screens];
            if (screens && screens.count) s = screens[0];
        }

        if (!s) return SizeWithAspectRatio(600, k_gui_aspect_ratio);
        s;
    });

    auto const backing_scale = [screen backingScaleFactor];

    auto const dpi = ({
        CGFloat v = 0.0;
        auto const description = [screen deviceDescription];
        NSNumber* const screen_number = description[@"NSScreenNumber"];

        if (screen_number) {
            auto const display_id = (CGDirectDisplayID)[screen_number unsignedIntValue];
            auto const physical_size_mm = CGDisplayScreenSize(display_id);

            if (physical_size_mm.width > 0 && physical_size_mm.height > 0) {
                auto const logical_pixel_size = [[description objectForKey:NSDeviceSize] sizeValue];
                if (logical_pixel_size.width > 0) {
                    // logical_pixel_size is in logical pixels, physical_size_mm is in mm
                    // Convert mm to inches: divide by 25.4
                    // Calculate logical pixels per inch, then multiply by backing scale for physical pixels
                    // per inch
                    v = (logical_pixel_size.width / (physical_size_mm.width / 25.4)) * backing_scale;
                }
            }
        }

        if (v <= 0) v = 72.0 * backing_scale;
        v;
    });

    auto const screen_size = ({
        auto const s = [screen frame].size;
        UiSize {
            CheckedCast<u16>(s.width * backing_scale),
            CheckedCast<u16>(s.height * backing_scale),
        };
    });

    return DefaultUiSizeInternal(screen_size, (f32)dpi);
}

} // namespace native
