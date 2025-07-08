// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect  MacRect
#define Delay MacDelay
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#pragma clang diagnostic pop
#undef Rect
#undef Delay

#include "os/misc_mac.hpp"

#include "gui_platform.hpp"

f64 detail::DoubleClickTimeMs(GuiPlatform const&) {
    auto result = [NSEvent doubleClickInterval] * 1000.0;
    if (result <= 0) result = 300;
    return result;
}

UiSize detail::DefaultUiSizeFromDpi(GuiPlatform const&) {
    auto const main_screen = ({
        auto s = [NSScreen mainScreen];
        if (!s) s = [NSScreen screens][0]; // Fallback to first screen.

        if (!s) return SizeWithAspectRatio(960, k_gui_aspect_ratio); // Default 96 DPI
        s;
    });

    auto const backing_scale = [main_screen backingScaleFactor];

    auto const dpi_x = ({
        CGFloat dpi = 0.0;
        auto const description = [main_screen deviceDescription];
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
                    dpi = (logical_pixel_size.width / (physical_size_mm.width / 25.4)) * backing_scale;
                }
            }
        }

        if (dpi <= 0) dpi = 72.0 * backing_scale;
        dpi;
    });

    auto target_width = (CGFloat)k_default_gui_width_inches * dpi_x;
    CGFloat max_height {};

    // Ensure width is within our max screen percentage bounds.
    {
        auto const screen_frame = [main_screen frame];
        auto const screen_width = screen_frame.size.width * backing_scale;
        auto const screen_height = screen_frame.size.height * backing_scale;

        auto const max_width = screen_width * (CGFloat)k_screen_fit_percentage;
        max_height = screen_height * (CGFloat)k_screen_fit_percentage;

        if (target_width > max_width) target_width = max_width;
    }

    auto result = SizeWithAspectRatio((u16)Min((CGFloat)LargestRepresentableValue<u16>(), target_width),
                                      k_gui_aspect_ratio);

    // Ensure height is within max bounds.
    if (result.height > max_height) {
        auto const width_from_height = max_height * k_gui_aspect_ratio.width / k_gui_aspect_ratio.height;
        auto const width_from_height_u16 =
            (u16)Min((CGFloat)LargestRepresentableValue<u16>(), width_from_height);
        result = SizeWithAspectRatio(width_from_height_u16, k_gui_aspect_ratio);
    }

    // Ensure we stay within the min/max bounds
    if (result.width < k_min_gui_width)
        result = SizeWithAspectRatio(k_min_gui_width, k_gui_aspect_ratio);
    else if (result.width > k_max_gui_width)
        result = SizeWithAspectRatio(k_max_gui_width, k_gui_aspect_ratio);

    return result;
}

#define DIALOG_DELEGATE_CLASS MAKE_UNIQUE_OBJC_NAME(DialogDelegate)

@interface DIALOG_DELEGATE_CLASS : NSObject <NSOpenSavePanelDelegate>
@property Span<FilePickerDialogOptions::FileFilter const> filters; // NOLINT
@end

@implementation DIALOG_DELEGATE_CLASS
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    // Enable directories so that the user can navigate into them.
    BOOL is_directory;
    [[NSFileManager defaultManager] fileExistsAtPath:[url path] isDirectory:&is_directory];
    if (is_directory) return YES;

    // Only enable files that match the filter.
    NSString* ns_filename = [url lastPathComponent];
    auto const filename = NSStringToString(ns_filename);
    for (auto filter : self.filters) {
        LogDebug(ModuleName::Gui, "comparing {}, filter: {}", filename, filter.wildcard_filter);
        if (MatchWildcard(filter.wildcard_filter, filename)) return YES;
    }

    return NO;
}
@end

struct NativeFilePicker {
    NSSavePanel* panel = nullptr;
    DIALOG_DELEGATE_CLASS* delegate = nullptr;
};

constexpr uintptr k_file_picker_completed = 0xD1A106;

bool detail::NativeFilePickerOnClientMessage(GuiPlatform& platform, uintptr data1, uintptr data2) {
    if (!platform.native_file_picker) return false;
    if (data1 != k_file_picker_completed) return false;

    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return false;

    ASSERT([NSThread isMainThread]);
    ASSERT(!native_file_picker.panel.visible, "panel should be closed");
    platform.frame_state.file_picker_results = {};
    platform.file_picker_result_arena.ResetCursorAndConsolidateRegions();

    bool update_gui = false;

    auto const response = (NSModalResponse)data2;
    if (response == NSModalResponseOK) {
        auto const append_result = [&](NSURL* url) {
            NSString* path = [[url path] stringByResolvingSymlinksInPath];
            auto utf8 = FromNullTerminated(path.UTF8String);
            ASSERT(path::IsAbsolute(utf8));
            ASSERT(IsValidUtf8(utf8));

            platform.frame_state.file_picker_results.Append(utf8.Clone(platform.file_picker_result_arena),
                                                            platform.file_picker_result_arena);
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
    platform.native_file_picker.Clear();

    return update_gui;
}

ErrorCodeOr<void> detail::OpenNativeFilePicker(GuiPlatform& platform,
                                               FilePickerDialogOptions const& options) {
    ASSERT(platform.view);
    if (platform.native_file_picker) return k_success;
    platform.native_file_picker.Emplace();
    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();

    @try {
        ASSERT([NSThread isMainThread]);

        switch (options.type) {
            case FilePickerDialogOptions::Type::OpenFile:
            case FilePickerDialogOptions::Type::SelectFolder: {
                NSOpenPanel* open_panel = [NSOpenPanel openPanel];
                ASSERT(!native_file_picker.panel);
                native_file_picker.panel = (NSSavePanel*)open_panel; // retain

                native_file_picker.delegate = [[DIALOG_DELEGATE_CLASS alloc] init];
                native_file_picker.delegate.filters =
                    platform.file_picker_result_arena.Clone(options.filters, CloneType::Deep);
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
                break;
            }
        }

        auto panel = native_file_picker.panel;

        panel.parentWindow = ((__bridge NSView*)(void*)puglGetNativeView(platform.view)).window;
        panel.title = StringToNSString(options.title);
        [panel setLevel:NSModalPanelWindowLevel];
        panel.showsResizeIndicator = YES;
        panel.showsHiddenFiles = NO;
        panel.canCreateDirectories = YES;
        if (options.default_path)
            panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*options.default_path)];

        [panel beginWithCompletionHandler:^(NSInteger response) {
          // I don't think we can assert that this is always the main thread, so let's send it via
          // Pugl's event system which should guarantee main thread.
          PuglEvent const event {.client = {
                                     .type = PUGL_CLIENT,
                                     .flags = PUGL_IS_SEND_EVENT,
                                     .data1 = k_file_picker_completed,
                                     .data2 = (uintptr)response,
                                 }};
          auto const rc = puglSendEvent(platform.view, &event);
          ASSERT(rc == PUGL_SUCCESS);
        }];

    } @catch (NSException* e) {
        LogError(ModuleName::Gui, "error opening native file picker: {}", e.description.UTF8String);
        return ErrorFromNSError([NSError errorWithDomain:@"" code:0 userInfo:nil]);
    }
    return k_success;
}

void detail::CloseNativeFilePicker(GuiPlatform& platform) {
    if (!platform.native_file_picker) return;
    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return;
    [native_file_picker.panel close];
    native_file_picker.panel = nullptr; // release
    native_file_picker.delegate = nullptr; // release
    platform.native_file_picker.Clear();
}
