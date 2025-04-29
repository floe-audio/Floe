// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/filesystem.hpp"

#include "gui.hpp"
#include "windows_installer/registry.hpp"

// NOTE: Lot's of room for improvement in UX here. For now, this gets the job done. Improvements could
// include:
// - Don't block GUI thread while uninstalling
// - Use checkboxes to select what to uninstall
// - Show progress bar
// - Support uninstalling libraries/presets
// - Don't show error messages - the user can't do anything about them anyways.

struct Application {
    u32 uninstall_floe_button;
    u32 uninstall_mirage_buttton;
    u32 finish_button;
    u32 result_textbox;
    bool has_error = false;
    bool uninstall_attempted = false;
};

AppConfig GetAppConfig() {
    return {
        .window_width = 400,
        .window_height = 400,
        .window_title = L"Floe Uninstaller",
        .description = "Uninstall Floe plugins",
    };
}

Application* CreateApplication(GuiFramework& framework, u32 root_layout) {
    auto* app = new Application();

    constexpr u16 k_margin = 10;

    auto const root = CreateStackLayoutWidget(framework,
                                              root_layout,
                                              {
                                                  .margins = {k_margin, k_margin, k_margin, k_margin},
                                                  .expand_x = true,
                                                  .expand_y = true,
                                                  .type = WidgetOptions::Container {},
                                              });

    auto const main = CreateStackLayoutWidget(framework,
                                              root,
                                              {
                                                  .expand_x = true,
                                                  .expand_y = true,
                                                  .type =
                                                      WidgetOptions::Container {
                                                          .spacing = 7,
                                                      },
                                              });

    CreateWidget(framework,
                 main,
                 {
                     .text = "Floe Uninstaller",
                     .type = WidgetOptions::Label {.style = LabelStyle::Heading},
                 });
    CreateWidget(
        framework,
        main,
        {
            .margins = {0, 0, 2, 8},
            .expand_x = true,
            .text =
                "This program will remove Floe from your system. It does not remove libraries or presets. Close your DAW before uninstalling.",
            .type = WidgetOptions::Label {.style = LabelStyle::Regular},
        });

    app->uninstall_floe_button = CreateWidget(framework,
                                              main,
                                              {
                                                  .text = "Uninstall Floe",
                                                  .type = WidgetOptions::Button {.is_default = true},
                                              });
    app->uninstall_mirage_buttton = CreateWidget(framework,
                                                 main,
                                                 {
                                                     .text = "Uninstall Mirage",
                                                     .type = WidgetOptions::Button {.is_default = false},
                                                 });

    app->result_textbox = CreateWidget(framework,
                                       main,
                                       {
                                           .expand_x = true,
                                           .expand_y = true,
                                           .type = WidgetType::ReadOnlyTextbox,
                                       });

    auto bottom_row = CreateStackLayoutWidget(framework,
                                              root,
                                              {
                                                  .expand_x = true,
                                                  .expand_y = false,
                                                  .debug_name = "BottomRow",
                                                  .type =
                                                      WidgetOptions ::Container {
                                                          .orientation = Orientation::Horizontal,
                                                      },
                                              });
    app->finish_button = CreateWidget(framework,
                                      bottom_row,
                                      {
                                          .text = "Finish",
                                          .type = WidgetOptions::Button {.is_default = false},
                                      });

    if (AutorunMode(framework))
        EditWidget(framework, app->uninstall_floe_button, {.simulate_button_press = true});

    RecalculateLayout(framework);

    return app;
}

static void UninstallFloe(ArenaAllocator& scratch, DynamicArray<char>& error_log) {
    // Delete plugins
    struct Plugin {
        KnownDirectoryType type;
        String name;
    };
    for (auto const plugin : Array {
             Plugin {KnownDirectoryType::GlobalClapPlugins, "Floe.clap"},
             Plugin {KnownDirectoryType::GlobalVst3Plugins, "Floe.vst3"},
         }) {

        auto const dir = (String)KnownDirectory(scratch, plugin.type, {.create = false});
        auto const path = path::Join(scratch, Array {dir, plugin.name});

        TRY_OR(Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false}),
               { fmt::Append(error_log, "Failed to delete '{}': {}\n", path, error); });
    }

    // Delete preferences
    {
        auto const path = PreferencesFilepath();
        TRY_OR(Delete(path, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false}),
               { fmt::Append(error_log, "Failed to delete '{}': {}\n", path, error); });

        if (auto const dir = path::Directory(path)) {
            auto _ = Delete(*dir,
                            {.type = DeleteOptions::Type::DirectoryOnlyIfEmpty, .fail_if_not_exists = false});
        }
    }
}

static void UninstallMirage(ArenaAllocator& scratch, DynamicArray<char>& error_log) {
    auto const settings_path_1 =
        KnownDirectoryWithSubdirectories(scratch,
                                         KnownDirectoryType::MirageGlobalPreferences,
                                         Array {"FrozenPlain"_s, "Mirage", "Settings"},
                                         "mirage.json"_s,
                                         {.create = false, .error_log = nullptr});

    auto const settings_path_2 = KnownDirectoryWithSubdirectories(scratch,
                                                                  KnownDirectoryType::MiragePreferences,
                                                                  Array {"FrozenPlain"_s, "Mirage"},
                                                                  "mirage.json"_s,
                                                                  {.create = false, .error_log = nullptr});

    for (auto const p : Array {
             "C:\\Program Files\\VSTPlugins\\mirage64.dll"_s,
             "C:\\Program Files\\Steinberg\\VSTPlugins\\mirage64.dll",
             "C:\\Program Files\\Common Files\\VST2\\mirage64.dll",
             "C:\\Program Files\\Common Files\\Steinberg\\VST2\\mirage64.dll",
             settings_path_1,
             settings_path_2,
         }) {
        TRY_OR(Delete(p, {.type = DeleteOptions::Type::File, .fail_if_not_exists = false}),
               { fmt::Append(error_log, "Failed to delete '{}': {}\n", p, error); });
    }

    for (auto const p : Array {settings_path_1, settings_path_2}) {
        if (auto const dir = path::Directory(p)) {
            auto _ = Delete(*dir,
                            {.type = DeleteOptions::Type::DirectoryOnlyIfEmpty, .fail_if_not_exists = false});
        }
    }
}

void HandleUserInteraction(Application& app, GuiFramework& framework, UserInteraction const& info) {
    switch (info.type) {
        case UserInteraction::Type::ButtonPressed: {
            if (info.widget_id == app.uninstall_floe_button ||
                info.widget_id == app.uninstall_mirage_buttton) {
                // IMPROVE: not great doing this in the main thread because it might be slow.

                ArenaAllocator scratch {PageAllocator::Instance()};
                DynamicArray<char> error_log {scratch};

                String const name = info.widget_id == app.uninstall_floe_button ? "Floe"_s : "Mirage";

                app.has_error = false;
                app.uninstall_attempted = true;
                EditWidget(framework,
                           app.result_textbox,
                           {.text = fmt::Format(scratch, "Uninstalling {}...\n", name)});

                if (info.widget_id == app.uninstall_floe_button) {
                    UninstallFloe(scratch, error_log);

                    if (AutorunMode(framework)) ExitProgram(framework);
                } else if (info.widget_id == app.uninstall_mirage_buttton) {

                    UninstallMirage(scratch, error_log);
                }

                if (error_log.size) {
                    app.has_error = true;
                    ReportError(ErrorLevel::Error, k_nullopt, "Uninstalling {} failed:\n{}", name, error_log);
                }

                if (!app.has_error)
                    EditWidget(framework,
                               app.result_textbox,
                               {.text = fmt::Format(scratch, "{} has been uninstalled.", name)});
                else
                    EditWidget(framework, app.result_textbox, {.text = error_log});

            } else if (info.widget_id == app.finish_button) {
                ExitProgram(framework);
            }
            break;
        }

        case UserInteraction::Type::RadioButtonSelected:
        case UserInteraction::Type::TextInputChanged:
        case UserInteraction::Type::TextInputEnterPressed:
        case UserInteraction::Type::CheckboxTableItemToggled: break;
    }
}

void OnTimer(Application&, GuiFramework&) {}

[[nodiscard]] int DestroyApplication(Application& app, GuiFramework&) {
    if (app.has_error) return 1;

    if (app.uninstall_attempted) {
        ArenaAllocator scratch {PageAllocator::Instance()};
        if (auto const uninstall_path = UninstallerPath(scratch, false)) {
            RemoveFileOnReboot(*uninstall_path, scratch);
            if (auto const dir = path::Directory(*uninstall_path)) RemoveFileOnReboot(*dir, scratch);

            RemoveUninstallRegistryKey();
        }
    }

    return 0;
}
