// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "installer.hpp"

#include <windows.h>
//
#include <restartmanager.h>
#include <shlobj.h>
#include <stb_image.h>

//
#include "foundation/memory/allocators.hpp"
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "os/misc_windows.hpp"
#include "os/threading.hpp"
#include "utils/debug/debug.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "gui.hpp"
#include "registry.hpp"

enum class Pages : u8 {
    Configuration,
    Installing,
    Summary,
    Count,
};

struct PageInfo {
    String title;
    String label;
};

constexpr auto k_page_infos = Array {
    PageInfo {
        .title = "Configuration",
        .label =
            "Welcome to the installer for Floe"
            ".\n\nPlease close your DAW before clicking install. Plugins are installed to standard locations so that any DAW can find them. Existing installations of Floe will be safely overwritten.",
    },
    PageInfo {
        .title = "Installing",
        .label = "Installing plugins...",
    },
    PageInfo {
        .title = "Summary",
        .label = "",
    },
};

static_assert(k_page_infos.size == ToInt(Pages::Count));

struct Component {
    ComponentInfo info {};
    String install_dir {};
    Span<u8 const> data {};
};

using AppNames = DynamicArrayBounded<char, 250>;

struct InstallResult {
    ErrorCodeOr<void> outcome = k_success;
    AppNames apps_using_file {};
};

using InstallationResults = Array<InstallResult, ToInt(ComponentTypes::Count)>;

enum class FailureCause : u8 { InUse, BlockedByWindows, Unexpected };

static FailureCause CauseOfFailure(InstallResult const& result) {
    ASSERT(result.outcome.HasError());
    if (result.apps_using_file.size) return FailureCause::InUse;
    if (result.outcome.Error() == FilesystemError::UsedByAnotherProcess) return FailureCause::InUse;
    if (result.outcome.Error() == FilesystemError::AccessDenied) return FailureCause::BlockedByWindows;
    return FailureCause::Unexpected;
}

struct FailureGuidance {
    String reason;
    String action;
    bool show_help_link;
};

static FailureGuidance GuidanceForFailure(InstallResult const& result) {
    switch (CauseOfFailure(result)) {
        case FailureCause::InUse: {
            auto const action = ({
                String a {};
                if (!result.apps_using_file.size)
                    a = "Close your DAW and any other music apps, then click Try Again.";
                else if (Contains(result.apps_using_file, '\n'))
                    a = "Close these apps, then click Try Again.";
                else
                    a = "Close this app, then click Try Again.";
                a;
            });
            return {
                .reason = "Floe is open in another app, so it couldn't be replaced.",
                .action = action,
                .show_help_link = false,
            };
        }
        case FailureCause::BlockedByWindows:
            return {
                .reason = "Windows blocked the installer from saving Floe.",
                .action =
                    "Close your DAW, then click Try Again.\n\nIf it still fails, security software may be blocking the installer. Check your antivirus, and if Controlled Folder Access is on in Windows Security, allow the installer through it.",
                .show_help_link = true,
            };
        case FailureCause::Unexpected:
            return {
                .reason = "Something unexpected stopped the installation.",
                .action =
                    "Click Try Again. If it keeps failing, the help page shows other ways to install and where to report a bug.",
                .show_help_link = true,
            };
    }
    PanicIfReached();
    return {};
}

enum class StatusIcon : u8 { Installed, Failed };

// A coloured disc with a white tick or cross, on a white background.
static Span<u8> RenderStatusIconRgba(ArenaAllocator& arena, StatusIcon icon, u16 size) {
    struct Point {
        f32 x, y;
    };
    struct Segment {
        Point a, b;
    };

    auto const segments = ({
        Array<Segment, 2> s {};
        switch (icon) {
            case StatusIcon::Installed:
                s = {Segment {{0.28f, 0.52f}, {0.44f, 0.68f}}, Segment {{0.44f, 0.68f}, {0.72f, 0.36f}}};
                break;
            case StatusIcon::Failed:
                s = {Segment {{0.33f, 0.33f}, {0.67f, 0.67f}}, Segment {{0.67f, 0.33f}, {0.33f, 0.67f}}};
                break;
        }
        s;
    });

    auto const disc_colour = ({
        Array<f32, 3> c {};
        switch (icon) {
            case StatusIcon::Installed: c = {15, 123, 15}; break;
            case StatusIcon::Failed: c = {196, 43, 28}; break;
        }
        c;
    });

    auto const distance_squared_to_segment = [](Point p, Segment const& segment) {
        auto const segment_x = segment.b.x - segment.a.x;
        auto const segment_y = segment.b.y - segment.a.y;
        auto const along_01 = Clamp((((p.x - segment.a.x) * segment_x) + ((p.y - segment.a.y) * segment_y)) /
                                        ((segment_x * segment_x) + (segment_y * segment_y)),
                                    0.0f,
                                    1.0f);
        auto const delta_x = p.x - (segment.a.x + (segment_x * along_01));
        auto const delta_y = p.y - (segment.a.y + (segment_y * along_01));
        return (delta_x * delta_x) + (delta_y * delta_y);
    };

    constexpr int k_subsamples = 4;
    constexpr f32 k_stroke_half_width = 0.055f;

    auto const rgba = arena.AllocateExactSizeUninitialised<u8>((usize)size * size * 4);
    for (auto const pixel_y : Range((int)size)) {
        for (auto const pixel_x : Range((int)size)) {
            int disc_samples = 0;
            for (auto const sub_y : Range(k_subsamples)) {
                for (auto const sub_x : Range(k_subsamples)) {
                    Point const p {
                        ((f32)pixel_x + (((f32)sub_x + 0.5f) / k_subsamples)) / (f32)size,
                        ((f32)pixel_y + (((f32)sub_y + 0.5f) / k_subsamples)) / (f32)size,
                    };
                    if ((((p.x - 0.5f) * (p.x - 0.5f)) + ((p.y - 0.5f) * (p.y - 0.5f))) > 0.25f) continue;

                    auto on_stroke = false;
                    for (auto const& segment : segments)
                        if (distance_squared_to_segment(p, segment) <=
                            (k_stroke_half_width * k_stroke_half_width))
                            on_stroke = true;
                    if (!on_stroke) ++disc_samples;
                }
            }

            auto const disc_01 = (f32)disc_samples / (k_subsamples * k_subsamples);
            auto const pixel = rgba.data + ((((usize)pixel_y * size) + (usize)pixel_x) * 4);
            for (auto const channel : Range(3uz))
                pixel[channel] = (u8)(255.0f + ((disc_colour[channel] - 255.0f) * disc_01));
            pixel[3] = 255;
        }
    }
    return rgba;
}

struct SummaryRowWidgets {
    u32 row;
    u32 installed_icon;
    u32 failed_icon;
    u32 status;
    u32 install_dir;
    u32 reason;
    u32 apps_caption;
    u32 apps;
    u32 action;
    u32 help_link;
};

struct SummaryWidgets {
    u32 success_icon;
    u32 failure_icon;
    u32 heading;
    u32 subheading;
    Array<SummaryRowWidgets, ToInt(ComponentTypes::Count)> rows;
    u32 details;
};

struct Application {
    Array<Component, ToInt(ComponentTypes::Count)> components = {};
    u32 page_title;
    u32 page_label;
    u32 next_button;
    u32 back_button;
    u32 cancel_button;
    u32 installing_bar;
    Array<u32, ToInt(Pages::Count)> pages;
    Array<u32, ToInt(Pages::Count)> page_sidebar_labels;
    u32 plugin_checkboxes;
    SummaryWidgets summary;
    Pages current_page = Pages::Configuration;
    Array<bool, ToInt(ComponentTypes::Count)> components_selected;
    bool retrying_failed_components = false;
    Thread installing_thread;
    Atomic<bool> installing_completed {false};
    MutexProtected<InstallationResults> installation_results = {};
    ArenaAllocator arena {PageAllocator::Instance()};
};

ErrorCodeOr<Span<u8 const>> GetResource(int resource_id) {
    auto const res = FindResourceW(GetModuleHandleW(nullptr),
                                   MAKEINTRESOURCEW(resource_id),
                                   MAKEINTRESOURCEW(RAW_DATA_TYPE));
    if (!res) return Win32ErrorCode(GetLastError());
    auto const res_handle = LoadResource(nullptr, res);
    if (!res_handle) return Win32ErrorCode(GetLastError());
    auto const res_data = (u8 const*)LockResource(res_handle);
    auto const res_size = SizeofResource(nullptr, res);

    return Span<u8 const> {res_data, (size_t)res_size};
}

static MutableString TempPathInSameDirAs(String dest_path, Allocator& arena, u64& seed) {
    return path::Join(
        arena,
        Array {path::Directory(dest_path).Value(), UniqueFilename(k_temporary_directory_prefix, "", seed)});
}

static ErrorCodeOr<void>
WriteFileViaRename(String dest_path, Span<u8 const> data, u64& seed, ArenaAllocator& scratch) {
    return TryWithRetries([&]() -> ErrorCodeOr<void> {
        auto const temp_path = TempPathInSameDirAs(dest_path, scratch, seed);
        bool success = false;

        // Always try clean up any failed or partially written temp.
        DEFER {
            if (!success)
                auto _ = Delete(temp_path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false});
        };

        // Write to a temp file. Then rename to file - atomic.
        TRY(WriteFile(temp_path, data));
        TRY(Rename(temp_path, dest_path));

        success = true;
        return k_success;
    });
}

// Error reports don't include paths, so we describe the state of the install location instead.
static String DescribeInstallFolderState(String folder, ArenaAllocator& arena) {
    auto const wide_folder =
        TRY_OR(path::MakePathForWin32(folder, arena, true), return "invalid path"_s).path;
    auto const attributes = GetFileAttributesW(wide_folder.data);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return fmt::Format(arena, "missing: {u}", Win32ErrorCode(GetLastError()));
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) return "not a folder"_s;
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return "exists"_s;

    // A junction or symlink: check that its target can be reached.
    auto const handle = CreateFileW(wide_folder.data,
                                    0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return fmt::Format(arena, "link with unreachable target: {u}", Win32ErrorCode(GetLastError()));
    CloseHandle(handle);
    return "link"_s;
}

static String DescribeExistingFileState(String file, ArenaAllocator& arena) {
    auto const wide_file = TRY_OR(path::MakePathForWin32(file, arena, true), return "invalid path"_s).path;
    auto const attributes = GetFileAttributesW(wide_file.data);
    if (attributes == INVALID_FILE_ATTRIBUTES) return "none"_s;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return "is a folder"_s;
    if (attributes & FILE_ATTRIBUTE_READONLY) return "read-only"_s;
    return "exists"_s;
}

// Friendly names of the apps that have the file open or loaded, one per line.
static ErrorCodeOr<AppNames> AppsUsingFile(String file, ArenaAllocator& arena) {
    DWORD session {};
    WCHAR session_key[CCH_RM_SESSION_KEY + 1] {};
    if (auto const r = RmStartSession(&session, 0, session_key); r != ERROR_SUCCESS)
        return Win32ErrorCode(r, "RmStartSession");
    DEFER { RmEndSession(session); };

    auto const wide_file = WidenAllocNullTerm(arena, file).Value();
    LPCWSTR registered_file = wide_file.data;
    if (auto const r = RmRegisterResources(session, 1, &registered_file, 0, nullptr, 0, nullptr);
        r != ERROR_SUCCESS)
        return Win32ErrorCode(r, "RmRegisterResources");

    UINT num_infos_needed = 0;
    UINT num_infos = 0;
    DWORD reboot_reasons = 0;
    auto get_list_result = RmGetList(session, &num_infos_needed, &num_infos, nullptr, &reboot_reasons);
    if (get_list_result == ERROR_SUCCESS) return AppNames {};
    if (get_list_result != ERROR_MORE_DATA) return Win32ErrorCode(get_list_result, "RmGetList");

    auto const infos = arena.AllocateExactSizeUninitialised<RM_PROCESS_INFO>(num_infos_needed);
    num_infos = (UINT)infos.size;
    get_list_result = RmGetList(session, &num_infos_needed, &num_infos, infos.data, &reboot_reasons);
    if (get_list_result != ERROR_SUCCESS) return Win32ErrorCode(get_list_result, "RmGetList");

    AppNames names {};
    for (auto const info_index : Range((usize)num_infos)) {
        auto const wide_name = FromNullTerminated(infos[info_index].strAppName);

        // An app can have several processes using the file.
        auto already_listed = false;
        for (auto const earlier_index : Range(info_index))
            if (FromNullTerminated(infos[earlier_index].strAppName) == wide_name) already_listed = true;
        if (already_listed) continue;

        auto const name = TRY_OPT_OR(Narrow(arena, wide_name), continue);
        if (!name.size) continue;
        auto const separator = names.size ? "\n"_s : ""_s;
        if (names.size + separator.size + name.size > names.Capacity()) break;
        dyn::AppendSpan(names, separator);
        dyn::AppendSpan(names, name);
    }
    return names;
}

static InstallResult TryInstall(Component const& comp, u64& seed) {
    ASSERT(comp.data.size != 0);
    ASSERT(comp.info.filename.size != 0);
    ASSERT(IsValidUtf8(comp.info.filename));

    ArenaAllocatorWithInlineStorage<3000> arena {Malloc::Instance()};

    auto const path = path::Join(arena, Array {comp.install_dir, comp.info.filename});
    auto const write_outcome = WriteFileViaRename(path, comp.data, seed, arena);
    if (!write_outcome.HasError()) return {};

    InstallResult result {.outcome = write_outcome.Error()};

    auto const apps_using_file = AppsUsingFile(path, arena);
    if (apps_using_file.HasValue()) result.apps_using_file = apps_using_file.Value();
    if (result.apps_using_file.size) return result;

    if (write_outcome.Error() != FilesystemError::UsedByAnotherProcess)
        ReportError(ErrorLevel::Warning,
                    k_nullopt,
                    "Failed to install {}: {} (folder: {}, existing file: {}, apps using file: {})",
                    comp.info.name,
                    write_outcome.Error(),
                    DescribeInstallFolderState(comp.install_dir, arena),
                    DescribeExistingFileState(path, arena),
                    apps_using_file.HasError() ? "unknown"_s : "none"_s);
    return result;
}

static void TryInstallUninstaller(u64& seed) {
    ArenaAllocatorWithInlineStorage<3000> arena {Malloc::Instance()};

    auto const uninstaller_exe_path = TRY_OPT_OR(UninstallerPath(arena, true), return);

    // Write the uninstaller exe
    {
        auto const file_data =
            TRY_OR(GetResource(UNINSTALLER_RESOURCE_ID), Panic("Failed to load uninstaller"));
        TRY_OR(WriteFileViaRename(uninstaller_exe_path, file_data, seed, arena), {
            ReportError(ErrorLevel::Warning, k_nullopt, "Failed to install uninstaller: {}", error);
            return;
        });
    }

    CreateUninstallRegistryKey(arena, uninstaller_exe_path);
}

static void BackgroundInstallingThread(Application& app) {
    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};
    auto const start_time = TimePoint::Now();
    auto seed = RandomSeed();
    auto const previous_results =
        app.installation_results.Use([](InstallationResults const& r) { return r; });
    for (auto const i : Range(ToInt(ComponentTypes::Count))) {
        if (!app.components_selected[i]) continue;
        if (app.retrying_failed_components && !previous_results[i].outcome.HasError()) continue;
        auto const& comp = app.components[i];
        InstallResult result {};
        if (comp.data.size != 0) result = TryInstall(comp, seed);
        app.installation_results.Use([i, &result](InstallationResults& r) { r[i] = result; });
    }

    TryInstallUninstaller(seed);

    // Intentional delay because it feels good
    constexpr auto k_min_seconds = 1.5;
    if (auto const time_taken = start_time.SecondsFromNow(); time_taken < k_min_seconds)
        SleepThisThread((int)((k_min_seconds - time_taken) * 1000));

    app.installing_completed.Store(true, StoreMemoryOrder::Release);
}

static bool AnyInstallFailed(Application& app) {
    auto const results = app.installation_results.Use([](InstallationResults const& r) { return r; });
    for (auto const comp_index : Range(ToInt(ComponentTypes::Count)))
        if (app.components_selected[comp_index] && results[comp_index].outcome.HasError()) return true;
    return false;
}

static void UpdateSummaryPage(Application& app, GuiFramework& framework) {
    auto const results = app.installation_results.Use([](InstallationResults const& r) { return r; });
    auto const& summary = app.summary;

    ArenaAllocatorWithInlineStorage<2000> arena {Malloc::Instance()};

    auto const failed = [&](usize comp_index) {
        return app.components_selected[comp_index] && results[comp_index].outcome.HasError();
    };

    int num_selected = 0;
    int num_failed = 0;
    for (auto const comp_index : Range(ToInt(ComponentTypes::Count))) {
        if (app.components_selected[comp_index]) ++num_selected;
        if (failed(comp_index)) ++num_failed;
    }

    // Header
    {
        EditWidget(framework, summary.success_icon, {.visible = num_failed == 0});
        EditWidget(framework, summary.failure_icon, {.visible = num_failed != 0});

        static_assert(ToInt(ComponentTypes::Count) <= 2, "the wording below assumes at most 2 plugins");
        String heading {};
        String subheading {};
        if (num_failed == 0) {
            heading = "Floe is installed";
            subheading = "Open your DAW and load the Floe plugin.";
        } else if (num_failed != num_selected) {
            heading = "Floe wasn't fully installed";
            subheading =
                fmt::Format(arena, "{} of {} plugins couldn't be installed.", num_failed, num_selected);
        } else {
            heading = "Floe wasn't installed";
            subheading = num_selected == 1 ? "The plugin couldn't be installed."_s
                                           : "Neither plugin could be installed."_s;
        }
        EditWidget(framework, summary.heading, {.text = heading});
        EditWidget(framework, summary.subheading, {.text = subheading});
    }

    DynamicArray<char> details {arena};

    for (auto const comp_index : Range(ToInt(ComponentTypes::Count))) {
        auto const& row = summary.rows[comp_index];
        auto const& comp = app.components[comp_index];
        auto const& result = results[comp_index];

        EditWidget(framework, row.row, {.visible = app.components_selected[comp_index]});
        if (!app.components_selected[comp_index]) continue;

        auto const has_failed = failed(comp_index);

        EditWidget(framework, row.installed_icon, {.visible = !has_failed});
        EditWidget(framework, row.failed_icon, {.visible = has_failed});
        EditWidget(framework,
                   row.status,
                   {
                       .text = has_failed ? "Not installed"_s : "Installed"_s,
                       .label_style = has_failed ? LabelStyle::NegativeBold : LabelStyle::Positive,
                   });
        EditWidget(framework, row.install_dir, {.visible = !has_failed, .text = comp.install_dir});

        // Rows that failed for the same reason share one explanation, shown under the last of them.
        auto const explained_by_later_row = ({
            auto r = false;
            if (has_failed) {
                for (auto const later_index :
                     Range((usize)comp_index + 1, (usize)ToInt(ComponentTypes::Count))) {
                    if (!failed(later_index)) continue;
                    if (CauseOfFailure(results[later_index]) == CauseOfFailure(result) &&
                        results[later_index].apps_using_file == result.apps_using_file)
                        r = true;
                }
            }
            r;
        });
        auto const show_guidance = has_failed && !explained_by_later_row;
        auto const guidance = show_guidance ? GuidanceForFailure(result) : FailureGuidance {};
        auto const show_apps = show_guidance && result.apps_using_file.size != 0;

        EditWidget(framework, row.reason, {.visible = show_guidance, .text = guidance.reason});
        EditWidget(framework, row.apps_caption, {.visible = show_apps});
        EditWidget(framework, row.apps, {.visible = show_apps, .text = String(result.apps_using_file)});
        EditWidget(framework, row.action, {.visible = show_guidance, .text = guidance.action});
        EditWidget(framework, row.help_link, {.visible = show_guidance && guidance.show_help_link});

        if (has_failed) {
            if (details.size) dyn::Append(details, '\n');
            fmt::Append(details,
                        "Details: {} to {}: {u}",
                        comp.info.filename,
                        comp.install_dir,
                        result.outcome.Error());
        }
    }

    EditWidget(framework, summary.details, {.visible = num_failed != 0, .text = String(details)});
}

static void SwitchPage(Application& app, GuiFramework& framework, Pages page) {
    String next_button_text = "Next >";
    String cancel_button_text = "Cancel";

    switch (page) {
        case Pages::Configuration: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = true});
            next_button_text = "Install";
            break;
        }
        case Pages::Installing: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = false});
            break;
        }
        case Pages::Summary: {
            EditWidget(framework, app.back_button, {.enabled = false});
            EditWidget(framework, app.next_button, {.enabled = true});

            UpdateSummaryPage(app, framework);

            if (AnyInstallFailed(app)) {
                next_button_text = "Try Again";
                cancel_button_text = "Close";
            } else {
                next_button_text = "Finish";
            }
            break;
        }
        case Pages::Count: PanicIfReached(); break;
    }

    for (auto const page_index : Range(ToInt(Pages::Count)))
        EditWidget(framework, app.pages[page_index], {.visible = page_index == ToInt(page)});

    // The summary page has its own heading.
    EditWidget(framework,
               app.page_title,
               {.visible = page != Pages::Summary, .text = k_page_infos[ToInt(page)].title});
    EditWidget(framework,
               app.page_label,
               {.visible = page != Pages::Summary, .text = k_page_infos[ToInt(page)].label});
    EditWidget(framework, app.next_button, {.text = next_button_text});
    EditWidget(framework, app.cancel_button, {.text = cancel_button_text});

    for (auto const i : Range(ToInt(Pages::Count))) {
        LabelStyle style = LabelStyle::DullColour;
        if (i == ToInt(page))
            style = LabelStyle::Bold;
        else if (i < ToInt(page))
            style = LabelStyle::Regular;
        EditWidget(framework, app.page_sidebar_labels[i], {.label_style = style});
    }

    app.current_page = page;
    RecalculateLayout(framework);

    switch (page) {
        case Pages::Configuration: break;
        case Pages::Installing: {
            if (app.installing_thread.Joinable()) app.installing_thread.Join();
            app.installing_completed.Store(false, StoreMemoryOrder::Release);
            app.installing_thread.Start([&app]() { BackgroundInstallingThread(app); }, "install");
            break;
        }
        case Pages::Summary: break;
        case Pages::Count: PanicIfReached(); break;
    }

    if (AutorunMode(framework)) {
        switch (page) {
            case Pages::Configuration: {
                EditWidget(framework, app.next_button, {.simulate_button_press = true});
                break;
            }
            case Pages::Installing: break;
            case Pages::Summary: {
                ExitProgram(framework);
                break;
            }
            case Pages::Count: PanicIfReached(); break;
        }
    }
}

Application* CreateApplication(GuiFramework& framework, u32 root_layout_id) {
    auto* app = new Application();

    for (auto& selected : app->components_selected)
        selected = true;

    for (auto const i : Range(k_plugin_infos.size)) {
        auto const& info = k_plugin_infos[i];
        auto data = GetResource(info.resource_id);
        if (data.HasError())
            PanicF(SourceLocation::Current(), "Windows resource failed to load: {}", data.Error());

        DynamicArray<char> error_buffer {PageAllocator::Instance()};
        auto error_writer = dyn::WriterFor(error_buffer);

        app->components[i] = {
            .info = info,
            .install_dir = KnownDirectory(app->arena,
                                          info.install_dir,
                                          {
                                              .create = true,
                                              .error_log = &error_writer,
                                          }),
            .data = data.Value(),
        };

        if (error_buffer.size)
            ReportError(ErrorLevel::Error,
                        k_nullopt,
                        "Failed to get install directory for {}: {}",
                        info.name,
                        error_buffer);

        switch ((ComponentTypes)i) {
            case ComponentTypes::Clap:
                ASSERT(
                    IsEqualToCaseInsensitiveAscii(path::Filename(app->components[i].install_dir), "CLAP"_s));
                break;
#ifdef VST3_PLUGIN_BINARY_NAME
            case ComponentTypes::VST3:
                ASSERT(
                    IsEqualToCaseInsensitiveAscii(path::Filename(app->components[i].install_dir), "VST3"_s));
                break;
#endif

            case ComponentTypes::Count: break;
        }
    }

    constexpr u16 k_margin = 10;

    {
        auto const found_sidebar_image = GetResource(SIDEBAR_IMAGE_RESOURCE_ID);
        if (found_sidebar_image.HasValue()) {
            auto const& bin_data = found_sidebar_image.Value();

            int width;
            int height;
            int channels;
            auto rgba_data = stbi_load_from_memory((stbi_uc const*)bin_data.data,
                                                   (int)bin_data.size,
                                                   &width,
                                                   &height,
                                                   &channels,
                                                   4);
            if (rgba_data == nullptr) Panic("Failed to load image");
            DEFER { stbi_image_free(rgba_data); };
            CreateWidget(framework,
                         root_layout_id,
                         {
                             .margins = {16, 16, 8, 8},
                             .type = WidgetOptions::Image {.rgba_data = rgba_data,
                                                           .size = {CheckedCast<u16>(width),
                                                                    CheckedCast<u16>(height)}},
                         });
        }
    }

    CreateWidget(framework,
                 root_layout_id,
                 {
                     .expand_x = true,
                     .type = WidgetOptions::Divider {.orientation = Orientation::Horizontal},
                 });

    auto const main = CreateStackLayoutWidget(framework,
                                              root_layout_id,
                                              {
                                                  .expand_x = true,
                                                  .expand_y = true,
                                                  .type =
                                                      WidgetOptions::Container {
                                                          .orientation = Orientation::Horizontal,
                                                          .alignment = Alignment::Start,
                                                      },
                                              });

    {
        auto const lhs = CreateStackLayoutWidget(framework,
                                                 main,
                                                 {
                                                     .expand_y = true,
                                                     .debug_name = "LHS",
                                                     .type = WidgetOptions::Container {},
                                                 });

        for (auto const i : Range(k_page_infos.size)) {
            app->page_sidebar_labels[i] =
                CreateWidget(framework,
                             lhs,
                             {
                                 .margins = {k_margin,
                                             k_margin,
                                             (i == 0) ? (u16)8 : (u16)2,
                                             (i == (k_page_infos.size - 1)) ? (u16)8 : (u16)2},
                                 .expand_x = true,
                                 .text = k_page_infos[i].title,
                                 .type = WidgetOptions::Label {.style = LabelStyle::DullColour},
                             });
        }
    }

    CreateWidget(framework,
                 main,
                 {
                     .type = WidgetOptions::Divider {.orientation = Orientation::Vertical},
                 });

    {
        auto const rhs = CreateStackLayoutWidget(framework,
                                                 main,
                                                 {
                                                     .expand_x = true,
                                                     .expand_y = true,
                                                     .debug_name = "RHS",
                                                     .type = WidgetOptions::Container {},
                                                 });
        auto const rhs_inner =
            CreateStackLayoutWidget(framework,
                                    rhs,
                                    {
                                        .margins = {k_margin, k_margin, k_margin, k_margin},
                                        .expand_x = true,
                                        .expand_y = true,
                                        .debug_name = "RHS Inner",
                                        .type =
                                            WidgetOptions::Container {
                                                .spacing = 7,
                                            },
                                    });
        {

            app->page_title = CreateWidget(framework,
                                           rhs_inner,
                                           {
                                               .expand_x = true,
                                               .text = "title",
                                               .type = WidgetOptions::Label {.style = LabelStyle::Heading},
                                           });
            app->page_label = CreateWidget(framework,
                                           rhs_inner,
                                           {
                                               .margins = {0, 0, 2, 8},
                                               .expand_x = true,
                                               .text = "label",
                                               .type = WidgetOptions::Label {.style = LabelStyle::Regular},
                                           });

            auto const page_options = WidgetOptions {
                .expand_x = true,
                .expand_y = true,
                .type =
                    WidgetOptions::Container {
                        .spacing = 5,
                    },
            };

            {
                app->pages[ToInt(Pages::Configuration)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);

                app->plugin_checkboxes =
                    CreateWidget(framework,
                                 app->pages[ToInt(Pages::Configuration)],
                                 {
                                     .expand_x = true,
                                     .expand_y = true,
                                     .type =
                                         WidgetOptions::CheckboxTable {
                                             .columns =
                                                 Array {
                                                     WidgetOptions::CheckboxTable::Column {
                                                         .label = "Component"_s,
                                                         .default_width = 240,
                                                     },
                                                     WidgetOptions::CheckboxTable::Column {
                                                         .label = "Size",
                                                         .default_width = 160,
                                                     },
                                                 },
                                         },
                                 });

                for (auto const& comp : app->components) {
                    FixedSizeAllocator<32> allocator {&Malloc::Instance()};
                    EditWidget(framework,
                               app->plugin_checkboxes,
                               {
                                   .add_checkbox_table_item =
                                       EditWidgetOptions::CheckboxTableItem {
                                           .state = true,
                                           .items = Array {comp.info.name,
                                                           fmt::Format(allocator,
                                                                       "{.2} MB",
                                                                       (f64)comp.data.size / Mb(1))},
                                       },
                               });
                }
            }
            {
                app->pages[ToInt(Pages::Installing)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);

                app->installing_bar = CreateWidget(framework,
                                                   app->pages[ToInt(Pages::Installing)],
                                                   {
                                                       .expand_x = true,
                                                       .type = WidgetType::ProgressBar,
                                                   });
            }

            {
                app->pages[ToInt(Pages::Summary)] =
                    CreateStackLayoutWidget(framework, rhs_inner, page_options);
                auto const page = app->pages[ToInt(Pages::Summary)];
                auto& summary = app->summary;

                auto const create_icon = [&](u32 parent, StatusIcon icon, u16 size) {
                    ArenaAllocatorWithInlineStorage<4200> icon_arena {Malloc::Instance()};
                    return CreateWidget(
                        framework,
                        parent,
                        {
                            .type =
                                WidgetOptions::Image {
                                    .rgba_data = RenderStatusIconRgba(icon_arena, icon, size).data,
                                    .size = {size, size},
                                },
                        });
                };

                constexpr u16 k_row_icon_size = 16;
                constexpr int k_row_icon_gap = 8;
                auto const create_row_detail = [&](u32 parent, LabelStyle style, String text) {
                    return CreateWidget(framework,
                                        parent,
                                        {
                                            .margins = {.l = k_row_icon_size + k_row_icon_gap},
                                            .expand_x = true,
                                            .text = text,
                                            .type = WidgetOptions::Label {.style = style},
                                        });
                };

                {
                    auto const header =
                        CreateStackLayoutWidget(framework,
                                                page,
                                                {
                                                    .expand_x = true,
                                                    .type =
                                                        WidgetOptions::Container {
                                                            .spacing = 10,
                                                            .orientation = Orientation::Horizontal,
                                                        },
                                                });
                    summary.success_icon = create_icon(header, StatusIcon::Installed, 32);
                    summary.failure_icon = create_icon(header, StatusIcon::Failed, 32);
                    auto const header_text =
                        CreateStackLayoutWidget(framework,
                                                header,
                                                {
                                                    .expand_x = true,
                                                    .type = WidgetOptions::Container {.spacing = 3},
                                                });
                    summary.heading =
                        CreateWidget(framework,
                                     header_text,
                                     {
                                         .expand_x = true,
                                         .type = WidgetOptions::Label {.style = LabelStyle::Heading},
                                     });
                    summary.subheading =
                        CreateWidget(framework,
                                     header_text,
                                     {
                                         .expand_x = true,
                                         .type = WidgetOptions::Label {.style = LabelStyle::Regular},
                                     });
                }

                CreateWidget(framework,
                             page,
                             {
                                 .margins = {.t = 6, .b = 6},
                                 .expand_x = true,
                                 .type = WidgetOptions::Divider {.orientation = Orientation::Horizontal},
                             });

                for (auto const comp_index : Range(ToInt(ComponentTypes::Count))) {
                    auto& row = summary.rows[comp_index];
                    row.row = CreateStackLayoutWidget(framework,
                                                      page,
                                                      {
                                                          .margins = {.b = 8},
                                                          .expand_x = true,
                                                          .type = WidgetOptions::Container {.spacing = 6},
                                                      });
                    auto const status_line =
                        CreateStackLayoutWidget(framework,
                                                row.row,
                                                {
                                                    .expand_x = true,
                                                    .type =
                                                        WidgetOptions::Container {
                                                            .spacing = k_row_icon_gap,
                                                            .orientation = Orientation::Horizontal,
                                                        },
                                                });
                    row.installed_icon = create_icon(status_line, StatusIcon::Installed, k_row_icon_size);
                    row.failed_icon = create_icon(status_line, StatusIcon::Failed, k_row_icon_size);
                    CreateWidget(framework,
                                 status_line,
                                 {
                                     .expand_x = true,
                                     .text = app->components[comp_index].info.short_name,
                                     .type = WidgetOptions::Label {.style = LabelStyle::Bold},
                                 });
                    row.status = CreateWidget(framework,
                                              status_line,
                                              {.type = WidgetOptions::Label {.style = LabelStyle::Positive}});

                    row.install_dir = create_row_detail(row.row, LabelStyle::DullColour, {});
                    row.reason = create_row_detail(row.row, LabelStyle::Regular, {});
                    row.apps_caption = create_row_detail(row.row, LabelStyle::DullColour, "Open in:");
                    row.apps = create_row_detail(row.row, LabelStyle::Bold, {});
                    row.action = create_row_detail(row.row, LabelStyle::Regular, {});
                    row.help_link = CreateWidget(
                        framework,
                        row.row,
                        {
                            .fixed_size = UiSize {200, 18},
                            .margins = {.l = k_row_icon_size + k_row_icon_gap},
                            .text = "Installation help",
                            .type =
                                WidgetOptions::Hyperlink {
                                    .url = "https://floe.audio/docs/installation/download-and-install-floe"},
                        });
                }

                CreateStackLayoutWidget(framework,
                                        page,
                                        {.expand_y = true, .type = WidgetOptions::Container {}});
                summary.details =
                    CreateWidget(framework,
                                 page,
                                 {
                                     .expand_x = true,
                                     .type = WidgetOptions::Label {.style = LabelStyle::DullColour},
                                 });
            }
        }

        CreateWidget(framework,
                     rhs,
                     {
                         .type = WidgetOptions::Divider {.orientation = Orientation::Horizontal},
                     });

        auto nav_layout = CreateStackLayoutWidget(framework,
                                                  rhs,
                                                  {
                                                      .margins = {k_margin, k_margin, k_margin, k_margin},
                                                      .expand_x = true,
                                                      .expand_y = false,
                                                      .debug_name = "NavContainer",
                                                      .type =
                                                          WidgetOptions ::Container {
                                                              .spacing = 5,
                                                              .orientation = Orientation::Horizontal,
                                                              .alignment = Alignment::End,
                                                          },
                                                  });
        app->back_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "< Back", .type = WidgetOptions::Button {.is_default = false}});
        app->next_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "Next >", .type = WidgetOptions::Button {.is_default = true}});
        app->cancel_button =
            CreateWidget(framework,
                         nav_layout,
                         {.text = "Cancel", .type = WidgetOptions::Button {.is_default = false}});
    }

    SwitchPage(*app, framework, Pages::Configuration);

    return app;
}

int DestroyApplication(Application& app, GuiFramework&) {
    int result = 0;
    for (auto const& r : app.installation_results.Use([](InstallationResults const& r) { return r; }))
        if (r.outcome.HasError()) {
            result = 1;
            break;
        }

    if (app.installing_thread.Joinable()) app.installing_thread.Join();
    delete &app;
    return result;
}

void OnTimer(Application& app, GuiFramework& framework) {
    if (app.current_page == Pages::Installing) {
        if (app.installing_completed.Load(LoadMemoryOrder::Acquire))
            SwitchPage(app, framework, Pages::Summary);
        else
            EditWidget(framework, app.installing_bar, {.progress_bar_pulse = true});
    }
}

void HandleUserInteraction(Application& app, GuiFramework& framework, UserInteraction const& info) {
    switch (info.type) {
        case UserInteraction::Type::ButtonPressed: {
            if (info.widget_id == app.next_button) {
                auto const next_page = (int)app.current_page + 1;
                if (app.current_page == Pages::Summary && AnyInstallFailed(app)) {
                    app.retrying_failed_components = true;
                    SwitchPage(app, framework, Pages::Installing);
                } else if (next_page == (int)Pages::Count)
                    ExitProgram(framework);
                else
                    SwitchPage(app, framework, (Pages)next_page);
            } else if (info.widget_id == app.back_button) {
                auto const prev_page = (int)(app.current_page) - 1;
                ASSERT(prev_page >= 0);
                SwitchPage(app, framework, (Pages)prev_page);
            } else if (info.widget_id == app.cancel_button) {
                ExitProgram(framework);
            }
            break;
        }
        case UserInteraction::Type::CheckboxTableItemToggled: {
            if (info.widget_id == app.plugin_checkboxes)
                app.components_selected[info.button_index] = info.button_state;
            if (Find(app.components_selected, true))
                EditWidget(framework, app.next_button, {.enabled = true});
            else
                EditWidget(framework, app.next_button, {.enabled = false});
            break;
        }
        case UserInteraction::Type::RadioButtonSelected:
        case UserInteraction::Type::TextInputChanged:
        case UserInteraction::Type::TextInputEnterPressed: break;
    }
}

AppConfig GetAppConfig() {
    return {
        .window_width = 620,
        .window_height = 470,
        .window_title = L"Floe Installer v" WIDEN_STRING_LITERAL(FLOE_VERSION_STRING),
        .description = "Installer Floe plugins",
    };
}
