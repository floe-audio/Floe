// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <IconsFontAwesome6.h>

#include "os/filesystem.hpp"

#include "common_infrastructure/persistent_store.hpp"

#include "engine/default_preset.hpp"
#include "engine/engine.hpp"
#include "engine/package_installation.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui/panels/gui_prefs_panel.hpp"
#include "gui_framework/gui_builder.hpp"

struct PackageInstallPanelState {
    enum class LicenseInputMode : u8 { AllKeysAtOnce, PerProduct };
    LicenseInputMode license_input_mode = LicenseInputMode::AllKeysAtOnce;

    struct SuccessfulInstall {
        struct ComponentResult {
            DynamicArrayBounded<char, 100> name {};
            String action_taken {}; // Static string.
            bool installed {};
        };

        DynamicArray<char> package_path {Malloc::Instance()};
        DynamicArrayBounded<ComponentResult, 8> components {};
        u8 num_truncated {};
        DynamicArrayBounded<char, 128> license_email {};

        // Empty if the package's preset bank doesn't name a usable default preset.
        DynamicArray<char> default_preset_path {Malloc::Instance()};
        bool default_preset_auto_loaded {};

        // Applied when the dialogue is closed with Done.
        bool load_default_preset {};
        bool trash_package_file {};
    };
    DynamicArrayBounded<SuccessfulInstall, 16> successful_installs {};
};

PUBLIC String InstallationOptionAskUserPretext(package::InstallJob::Component const& comp,
                                               ArenaAllocator& arena) {
    auto const status = comp.existing_installation_status;
    ASSERT(package::UserInputIsRequired(status));

    String format {};
    if (status.modified_since_installed == package::ModifiedSinceInstalled::Modified) {
        switch (status.version_difference) {
            case package::VersionDifference::InstalledIsNewer:
                format =
                    "A newer version of {} {} is already installed but its files have been modified since it was installed.";
                break;
            case package::VersionDifference::InstalledIsOlder:
                format =
                    "An older version of {} {} is already installed but its files have been modified since it was installed.";
                break;
            case package::VersionDifference::Equal:
                format =
                    "{} {} is already installed but its files have been modified since it was installed.";
                break;
            case package::VersionDifference::Count: PanicIfReached();
        }
    } else if (status.modified_since_installed == package::ModifiedSinceInstalled::UnmodifiedButFilesAdded) {
        switch (status.version_difference) {
            case package::VersionDifference::InstalledIsNewer:
                format =
                    "A newer version of {} {} is already fully installed but unrelated files have been added to the folder since it was installed.";
                break;
            case package::VersionDifference::InstalledIsOlder:
                format =
                    "An older version of {} {} is already fully installed but unrelated files have been added to the folder since it was installed.";
                break;
            case package::VersionDifference::Equal:
                format =
                    "{} {} is already fully installed but unrelated files have been added to the folder since it was installed.";
                break;
            case package::VersionDifference::Count: PanicIfReached();
        }

    } else {
        // We don't know if the package has been modified or not so we just ask the user what to do without
        // any explanation.
        switch (status.version_difference) {
            case package::VersionDifference::InstalledIsNewer:
                format = "A newer version of {} {} is already installed.";
                break;
            case package::VersionDifference::InstalledIsOlder:
                format = "An older version of {} {} is already installed.";
                break;
            case package::VersionDifference::Equal: format = "{} {} is already installed."; break;
            case package::VersionDifference::Count: PanicIfReached();
        }
    }

    return fmt::Format(arena,
                       format,
                       path::Filename(comp.component.path),
                       package::ComponentTypeString(comp.component.type));
}

// A modal-styled radio row: circular indicator + label, the whole row clickable. Returns true when clicked.
static bool LicenseMethodRadio(GuiBuilder& builder, Box parent, String label, bool selected, u64 id_extra) {
    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .id_extra = id_extra,
                               .layout {
                                   .size = {layout::k_hug_contents, layout::k_hug_contents},
                                   .contents_gap = k_medium_gap,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .button_behaviour = imgui::ButtonConfig {},
                           });

    auto const indicator = DoBox(builder,
                                 {
                                     .parent = row,
                                     .background_fill_colours = Col {.c = Col::Background2},
                                     .background_fill_auto_hot_active_overlay = true,
                                     .border_colours = Col {.c = Col::Overlay0},
                                     .border_auto_hot_active_overlay = true,
                                     .parent_dictates_hot_and_active = true,
                                     .round_background_corners = 0b1111,
                                     .corner_rounding = k_icon_button_size / 2.0f,
                                     .layout {
                                         .size = k_icon_button_size,
                                         .contents_align = layout::Alignment::Middle,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });
    if (selected)
        DoBox(builder,
              {
                  .parent = indicator,
                  .background_fill_colours = Col {.c = Col::Text},
                  .parent_dictates_hot_and_active = true,
                  .round_background_corners = 0b1111,
                  .corner_rounding = k_icon_button_size / 4.0f,
                  .layout {.size = k_icon_button_size / 2.0f},
              });

    DoBox(builder,
          {
              .parent = row,
              .text = label,
              .size_from_text = true,
              .text_colours = Col {.c = Col::Text},
          });

    return row.button_fired;
}

// Renders the per-package "paste a key" blocks. Used by the manual key entry method.
static void PackageLicensePerProductInputs(GuiBuilder& builder,
                                           Box root,
                                           package::InstallJobs& package_install_jobs,
                                           ThreadPool& thread_pool) {
    for (auto& job : package_install_jobs) {
        auto const state = job.job->state.Load(LoadMemoryOrder::Acquire);
        if (state != package::InstallJob::State::AwaitingLicenseKey) continue;

        builder.imgui.PushId((uintptr)(void*)job.job);
        DEFER { builder.imgui.PopId(); };

        auto const container = DoBox(builder,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = k_medium_gap,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });

        auto const desc = fmt::Format(builder.arena,
                                      "Paste the license key for {}",
                                      path::FilenameWithoutExtension(job.job->path));
        DoBox(builder,
              {
                  .parent = container,
                  .text = desc,
                  .wrap_width = -1,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        auto const text_input = TextInput(builder,
                                          container,
                                          {
                                              .text = job.job->pasted_license_text,
                                              .size = {layout::k_fill_parent, 140.0f},
                                              .border = true,
                                              .background = true,
                                              .multiline = true,
                                          });
        if (text_input.result && text_input.result->buffer_changed)
            dyn::Assign(job.job->pasted_license_text, text_input.result->text);

        // Show error if previous attempt failed
        if (job.job->error_buffer.size) {
            DoBox(builder,
                  {
                      .parent = container,
                      .text = job.job->error_buffer,
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::Red},
                  });
        }

        auto const button_row = DoBox(builder,
                                      {
                                          .parent = container,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = k_medium_gap,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::End,
                                          },
                                      });

        if (TextButton(builder, button_row, {.text = "Cancel"})) {
            dyn::Assign(job.job->error_buffer, "Cancelled"_s);
            job.job->state.Store(package::InstallJob::State::DoneError, StoreMemoryOrder::Release);
        }
        if (TextButton(builder, button_row, {.text = "Paste"})) {
            builder.imgui.SetTextInputFocus(text_input.box.imgui_id, job.job->pasted_license_text, true);
            GuiIo().out.wants.clipboard_text_paste = true;
        }
        if (TextButton(builder,
                       button_row,
                       {
                           .text = "Activate",
                           .disabled = job.job->pasted_license_text.size == 0,
                           .is_default = true,
                       })) {
            dyn::Clear(job.job->error_buffer);
            if (!package::OnLicenseKeyReceived(*job.job, thread_pool)) {
                // Error is already in error_buffer, will be shown next frame
            }
        }
    }
}

PUBLIC void PackageLicenseInputPanel(GuiBuilder& builder,
                                     package::InstallJobs& package_install_jobs,
                                     ThreadPool& thread_pool,
                                     PackageInstallPanelState& panel_state,
                                     FilePickerState& file_picker_state,
                                     persistent_store::Store& persistent_store) {
    using Mode = PackageInstallPanelState::LicenseInputMode;

    auto const num_awaiting_key = ({
        usize n = 0;
        for (auto& job : package_install_jobs)
            if (job.job->state.Load(LoadMemoryOrder::Acquire) ==
                package::InstallJob::State::AwaitingLicenseKey)
                ++n;
        n;
    });

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoBox(builder,
          {
              .parent = root,
              .text = "License Key Required",
              .size_from_text = true,
          });

    auto const radio_group = DoBox(builder,
                                   {
                                       .parent = root,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = k_small_gap,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });
    if (LicenseMethodRadio(builder,
                           radio_group,
                           "Load a license file",
                           panel_state.license_input_mode == Mode::AllKeysAtOnce,
                           0))
        panel_state.license_input_mode = Mode::AllKeysAtOnce;
    if (LicenseMethodRadio(builder,
                           radio_group,
                           num_awaiting_key > 1 ? "Enter each key manually"_s : "Enter the key manually"_s,
                           panel_state.license_input_mode == Mode::PerProduct,
                           1))
        panel_state.license_input_mode = Mode::PerProduct;

    switch (panel_state.license_input_mode) {
        case Mode::AllKeysAtOnce: {
            DoBox(
                builder,
                {
                    .parent = root,
                    .text = num_awaiting_key > 1
                                ? "Unlock your packages using your license key file. One file unlocks all."_s
                                : "Unlock your package using your license key file."_s,
                    .wrap_width = -1,
                    .size_from_text = true,
                    .font = FontType::Body,
                });

            for (auto& job : package_install_jobs) {
                if (job.job->state.Load(LoadMemoryOrder::Acquire) !=
                    package::InstallJob::State::AwaitingLicenseKey)
                    continue;
                if (!job.job->error_buffer.size) continue;
                DoBox(builder,
                      {
                          .parent = root,
                          .text = fmt::Format(builder.arena,
                                              "{}: {}",
                                              path::FilenameWithoutExtension(job.job->path),
                                              job.job->error_buffer),
                          .wrap_width = -1,
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = Col {.c = Col::Red},
                      });
            }

            auto const button_row = DoBox(builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_gap = k_medium_gap,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::End,
                                              },
                                          });
            if (TextButton(builder, button_row, {.text = "Cancel"})) {
                for (auto& job : package_install_jobs)
                    if (job.job->state.Load(LoadMemoryOrder::Acquire) ==
                        package::InstallJob::State::AwaitingLicenseKey) {
                        dyn::Assign(job.job->error_buffer, "Cancelled"_s);
                        job.job->state.Store(package::InstallJob::State::DoneError,
                                             StoreMemoryOrder::Release);
                    }
            }
            if (TextButton(builder, button_row, {.text = "Load license file…", .is_default = true}))
                OpenFilePickerLoadLicenseFile(file_picker_state, persistent_store);
            break;
        }
        case Mode::PerProduct:
            PackageLicensePerProductInputs(builder, root, package_install_jobs, thread_pool);
            break;
    }
}

PUBLIC void PackageInstallAlertsPanel(GuiBuilder& builder, package::InstallJobs& package_install_jobs) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoBox(builder,
          {
              .parent = root,
              .text = "File Conflict",
              .size_from_text = true,
          });

    for (auto& job : package_install_jobs) {
        auto const state = job.job->state.Load(LoadMemoryOrder::Acquire);
        if (state != package::InstallJob::State::AwaitingUserInput) continue;

        for (auto& component : job.job->components) {
            if (!package::UserInputIsRequired(component.existing_installation_status)) continue;

            builder.imgui.PushId((uintptr)(void*)&component);
            DEFER { builder.imgui.PopId(); };

            auto const container = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = k_medium_gap,
                                                 .contents_direction = layout::Direction::Column,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

            auto const text = InstallationOptionAskUserPretext(component, builder.arena);

            DoBox(builder,
                  {
                      .parent = container,
                      .text = text,
                      .wrap_width = -1,
                      .size_from_text = true,
                      .font = FontType::Body,
                  });

            auto const button_row = DoBox(builder,
                                          {
                                              .parent = container,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_gap = k_medium_gap,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::Start,
                                              },
                                          });

            if (TextButton(builder, button_row, {.text = "Skip"}))
                component.user_decision = package::InstallJob::UserDecision::Skip;
            if (component.install_config.allow_overwrite &&
                TextButton(builder, button_row, {.text = "Overwrite"}))
                component.user_decision = package::InstallJob::UserDecision::Overwrite;
            if (component.component.type == package::ComponentType::Presets &&
                TextButton(builder, button_row, {.text = "Keep Both"}))
                component.user_decision = package::InstallJob::UserDecision::InstallCopy;
        }
    }
}

static void TrashPackageFile(String package_path,
                             Notifications& notifications,
                             ThreadsafeErrorNotifications& error_notifs) {
    ArenaAllocatorWithInlineStorage<Kb(1)> scratch_arena {Malloc::Instance()};
    auto const outcome = TrashFileOrDirectory(package_path, scratch_arena);
    auto const id = HashMultiple(Array {"package-file-trash"_s, package_path});

    if (outcome.HasValue()) {
        error_notifs.RemoveError(id);
        notifications.AddOrUpdate(
            id,
            [f = DynamicArrayBounded<char, 200>(path::Filename(package_path))](ArenaAllocator&) {
                return NotificationDisplayInfo {
                    .title = "Package File Sent to " TRASH_NAME,
                    .message = f,
                    .dismissable = true,
                    .icon = NotificationDisplayInfo::IconType::Success,
                };
            });
    } else if (auto item = error_notifs.BeginWriteError(id)) {
        DEFER { error_notifs.EndWriteError(*item); };
        item->title = "Failed to send package file to " TRASH_NAME ""_s;
        item->error_code = outcome.Error();
    }
}

PUBLIC void PackageInstallSuccessPanel(GuiBuilder& builder,
                                       Engine& engine,
                                       PackageInstallPanelState& panel_state,
                                       Notifications& notifications,
                                       ThreadsafeErrorNotifications& error_notifs) {
    auto& record = panel_state.successful_installs[0];

    auto const anything_installed = ({
        bool result = false;
        for (auto const& c : record.components)
            if (c.installed) result = true;
        result;
    });

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = {500, layout::k_hug_contents},
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Heading: title, then the licence as a subtitle.
    {
        auto const heading = DoBox(builder,
                                   {
                                       .parent = root,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = k_medium_gap,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });
        DoBox(builder,
              {
                  .parent = heading,
                  .text = anything_installed ? ICON_FA_CHECK : ICON_FA_CIRCLE_INFO,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = anything_installed ? Col::Green : Col::Subtext0},
              });
        auto const heading_text = DoBox(builder,
                                        {
                                            .parent = heading,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = k_small_gap,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });
        DoBox(builder,
              {
                  .parent = heading_text,
                  .text = anything_installed ? "Installation complete"_s : "Nothing new to install"_s,
                  .size_from_text = true,
              });
        if (record.license_email.size)
            DoBox(builder,
                  {
                      .parent = heading_text,
                      .text = fmt::Format(builder.arena, "Licensed to {}", record.license_email),
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                      .font = FontType::Body,
                  });
    }

    // What was installed: one row per component.
    {
        auto const list = DoBox(builder,
                                {
                                    .parent = root,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_gap = k_medium_gap,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });
        for (auto [index, component] : Enumerate<u64>(record.components)) {
            auto const row = DoBox(builder,
                                   {
                                       .parent = list,
                                       .id_extra = index,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = k_medium_gap,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });
            DoBox(builder,
                  {
                      .parent = row,
                      .text = ICON_FA_CIRCLE,
                      .font = FontType::Icons,
                      .font_size = k_font_body_size * 0.3f,
                      .text_colours = Col {.c = Col::Subtext0},
                      .text_justification = TextJustification::Centred,
                      .layout {.size = k_icon_button_size},
                  });
            // Fills the row's leftover width so the result stays on the right.
            DoBox(builder,
                  {
                      .parent = row,
                      .text = component.name,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                      .font = FontType::Body,
                  });
            DoBox(builder,
                  {
                      .parent = row,
                      .text = ({
                          auto action = builder.arena.Clone(component.action_taken);
                          if (action.size) action[0] = ToUppercaseAscii(action[0]);
                          String {action};
                      }),
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = component.installed ? Col::Green : Col::Subtext0},
                  });
        }
        if (record.num_truncated)
            DoBox(builder,
                  {
                      .parent = list,
                      .text = fmt::Format(builder.arena, "... and {} more", record.num_truncated),
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::Subtext0},
                  });
    }

    DoModalDivider(builder, root, {.horizontal = true, .subtle = true});

    // Actions, applied on Done.
    {
        auto const options = DoBox(builder,
                                   {
                                       .parent = root,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = k_medium_gap,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });
        if (record.default_preset_path.size && !record.default_preset_auto_loaded) {
            if (CheckboxButton(builder,
                               options,
                               fmt::Format(builder.arena,
                                           "Load preset {}",
                                           path::FilenameWithoutExtension(record.default_preset_path)),
                               record.load_default_preset,
                               "Replaces your current sound (undoable)."_s))
                record.load_default_preset = !record.load_default_preset;
        }
        if (CheckboxButton(builder,
                           options,
                           "Send package file to " TRASH_NAME ""_s,
                           record.trash_package_file,
                           TooltipString {String {path::Filename(record.package_path)}}))
            record.trash_package_file = !record.trash_package_file;
    }

    auto const button_row = DoBox(builder,
                                  {
                                      .parent = root,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_gap = k_medium_gap,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::End,
                                      },
                                  });

    if (TextButton(builder, button_row, {.text = "Done", .is_default = true})) {
        if (record.load_default_preset) LoadPresetFromFile(engine, record.default_preset_path);
        if (record.trash_package_file) TrashPackageFile(record.package_path, notifications, error_notifs);
        dyn::Remove(panel_state.successful_installs, 0);
        builder.imgui.CloseModal(PreferencesPanelState::k_panel_id);
    }
}

PUBLIC void DoPackageInstallNotifications(GuiBuilder& builder,
                                          Engine& engine,
                                          Notifications& notifications,
                                          ThreadPool& thread_pool,
                                          PackageInstallPanelState& panel_state,
                                          FilePickerState& file_picker_state,
                                          persistent_store::Store& persistent_store,
                                          prefs::Preferences& prefs) {
    auto& package_install_jobs = engine.package_install_jobs;
    auto& error_notifs = engine.error_notifications;

    constexpr u64 k_installing_packages_notif_id = HashFnv1a("installing packages notification");
    bool user_input_needed = false;
    bool license_key_needed = false;
    if (!package_install_jobs.Empty()) {
        if (notifications.AddOrUpdate(
                k_installing_packages_notif_id,
                [&package_install_jobs](ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                    NotificationDisplayInfo c {};
                    c.icon = NotificationDisplayInfo::IconType::Busy;
                    c.dismissable = false;
                    if (!package_install_jobs.Empty())
                        c.title = fmt::Format(
                            scratch_arena,
                            "Installing {}{}",
                            path::FilenameWithoutExtension(package_install_jobs.First().job->path),
                            package_install_jobs.ContainsMoreThanOne() ? " and others" : "");
                    return c;
                })) {
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }

        for (auto it = package_install_jobs.begin(); it != package_install_jobs.end();) {
            auto& job = *it;
            auto next = it;
            ++next;
            DEFER { it = next; };

            auto const job_id = HashMultiple(Array {"package-install"_s, job.job->path});

            auto const state = job.job->state.Load(LoadMemoryOrder::Acquire);
            switch (state) {
                case package::InstallJob::State::Installing: break;

                case package::InstallJob::State::AwaitingLicenseKey: {
                    license_key_needed = true;
                    break;
                }

                case package::InstallJob::State::DoneError: {
                    if (auto err = error_notifs.BeginWriteError(job_id)) {
                        DEFER { error_notifs.EndWriteError(*err); };
                        dyn::AssignFitInCapacity(err->title, "Failed to install package"_s);
                        fmt::Assign(err->message,
                                    "\"{}\": {}",
                                    path::FilenameWithoutExtension(job.job->path),
                                    job.job->error_buffer);
                    }

                    next = package::RemoveJob(package_install_jobs, it);
                    break;
                }

                case package::InstallJob::State::DoneSuccess: {
                    error_notifs.RemoveError(job_id);

                    DynamicArrayBounded<char, k_notification_buffer_size - 160> buffer {};
                    u8 num_truncated = 0;
                    auto const user_has_own_default = ({
                        auto const d = ResolveDefaultPreset(prefs);
                        d && d->is_user_set;
                    });
                    String suggested_default_preset {};
                    for (auto [index, component] : Enumerate(job.job->components)) {
                        // Latest install wins, so later components overwrite earlier ones.
                        if (component.suggested_default_preset_path.size)
                            suggested_default_preset = component.suggested_default_preset_path;
                        if (!num_truncated) {
                            if (!dyn::AppendSpan(
                                    buffer,
                                    fmt::Format(builder.arena,
                                                "{} {} {}\n",
                                                path::FilenameWithoutExtension(component.component.path),
                                                package::ComponentTypeString(component.component.type),
                                                package::TypeOfActionTaken(component))))
                                num_truncated = 1;
                        } else if (num_truncated != LargestRepresentableValue<decltype(num_truncated)>())
                            ++num_truncated;
                    }

                    if (suggested_default_preset.size) {
                        SetSuggestedDefaultPreset(prefs, suggested_default_preset);
                        if (!user_has_own_default && !num_truncated) {
                            if (!dyn::AppendSpan(
                                    buffer,
                                    fmt::Format(builder.arena,
                                                "Default preset set to {}\n",
                                                path::FilenameWithoutExtension(suggested_default_preset))))
                                num_truncated = 1;
                        }
                    }

                    // A blank instance is effectively still fresh, so open it on the bank's default preset
                    // right away rather than only affecting the next new instance.
                    bool auto_loaded = false;
                    if (suggested_default_preset.size && !user_has_own_default && IsBlankState(engine)) {
                        LoadPresetFromFile(engine, suggested_default_preset);
                        auto_loaded = true;
                    }

                    if (panel_state.successful_installs.size != panel_state.successful_installs.Capacity()) {
                        PackageInstallPanelState::SuccessfulInstall record {};
                        dyn::Assign(record.package_path, job.job->path);
                        for (auto const& component : job.job->components) {
                            if (record.components.size == record.components.Capacity()) {
                                if (record.num_truncated !=
                                    LargestRepresentableValue<decltype(record.num_truncated)>())
                                    ++record.num_truncated;
                                continue;
                            }
                            PackageInstallPanelState::SuccessfulInstall::ComponentResult result {
                                .action_taken = package::TypeOfActionTaken(component),
                                .installed = component.installed_path.size != 0,
                            };
                            switch (component.component.type) {
                                case package::ComponentType::Library:
                                    dyn::AssignFitInCapacity(result.name,
                                                             fmt::Format(builder.arena,
                                                                         "{} - Sample Library",
                                                                         component.component.library->name));
                                    break;
                                case package::ComponentType::Presets:
                                    dyn::AssignFitInCapacity(
                                        result.name,
                                        path::FilenameWithoutExtension(component.component.path));
                                    break;
                                case package::ComponentType::Count: PanicIfReached();
                            }
                            dyn::Append(record.components, result);
                        }
                        dyn::AssignFitInCapacity(record.license_email, job.job->license_email);
                        dyn::Assign(record.default_preset_path, suggested_default_preset);
                        record.default_preset_auto_loaded = auto_loaded;
                        dyn::Append(panel_state.successful_installs, Move(record));
                    } else {
                        DynamicArrayBounded<char, 128> license_email {};
                        dyn::AssignFitInCapacity(license_email, job.job->license_email);

                        notifications.AddOrUpdate(
                            job_id,
                            [buffer, num_truncated, license_email](
                                ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                                NotificationDisplayInfo c {};
                                c.icon = NotificationDisplayInfo::IconType::Success;
                                c.dismissable = true;
                                c.title = "Installation Complete";
                                if (num_truncated == 0) {
                                    c.message = buffer;
                                } else {
                                    c.message = fmt::Format(scratch_arena,
                                                            "{}\n... and {} more",
                                                            buffer,
                                                            num_truncated);
                                }
                                if (license_email.size)
                                    c.message = fmt::Format(scratch_arena,
                                                            "{}Licensed to: {}",
                                                            c.message,
                                                            license_email);
                                return c;
                            });
                    }
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);

                    next = package::RemoveJob(package_install_jobs, it);
                    break;
                }

                case package::InstallJob::State::AwaitingUserInput: {
                    bool all_descisions_made = true;
                    for (auto& component : job.job->components) {
                        if (package::UserInputIsRequired(component.existing_installation_status) &&
                            component.user_decision == package::InstallJob::UserDecision::Unknown) {
                            all_descisions_made = false;
                            break;
                        }
                    }

                    if (all_descisions_made)
                        package::OnAllUserInputReceived(*job.job, thread_pool);
                    else
                        user_input_needed = true;

                    break;
                }
            }
        }

        if (!license_key_needed)
            panel_state.license_input_mode = PackageInstallPanelState::LicenseInputMode::AllKeysAtOnce;

        if (license_key_needed) {
            DoBoxViewport(builder,
                          {
                              .run =
                                  [&package_install_jobs,
                                   &thread_pool,
                                   &panel_state,
                                   &file_picker_state,
                                   &persistent_store](GuiBuilder& b) {
                                      PackageLicenseInputPanel(b,
                                                               package_install_jobs,
                                                               thread_pool,
                                                               panel_state,
                                                               file_picker_state,
                                                               persistent_store);
                                  },
                              .bounds = CentredModalRect(f32x2 {500, 400}),
                              .imgui_id = builder.imgui.MakeId("license input"),
                              .viewport_config = ({
                                  auto cfg = k_default_modal_viewport;
                                  cfg.mode = imgui::ViewportMode::Floating;
                                  cfg.exclusive_focus = true;
                                  cfg.z_order = 200;
                                  cfg;
                              }),
                              .debug_name = "pkg-license-input-dialog",
                          });
        }

        if (user_input_needed) {
            DoBoxViewport(
                builder,
                {
                    .run = [&package_install_jobs](
                               GuiBuilder& b) { PackageInstallAlertsPanel(b, package_install_jobs); },
                    .bounds = CentredModalRect(f32x2 {400, 300}),
                    .imgui_id = builder.imgui.MakeId("install alerts"),
                    .viewport_config = ({
                        auto cfg = k_default_modal_viewport;
                        cfg.mode = imgui::ViewportMode::Floating;
                        cfg.exclusive_focus = true;
                        cfg.z_order = 200;
                        cfg;
                    }),
                    .debug_name = "pkg-user-input-dialog",
                });
        }
    } else {
        notifications.Remove(k_installing_packages_notif_id);
        panel_state.license_input_mode = PackageInstallPanelState::LicenseInputMode::AllKeysAtOnce;
    }

    // Never show this at the same time as the license or file-conflict dialogs: they're all
    // exclusive-focus viewports at the same z-order, so two at once fight over focus and can
    // deadlock the whole GUI.
    if (panel_state.successful_installs.size && !license_key_needed && !user_input_needed) {
        DoBoxViewport(
            builder,
            {
                .run =
                    [&engine, &panel_state, &notifications, &error_notifs](GuiBuilder& b) {
                        PackageInstallSuccessPanel(b, engine, panel_state, notifications, error_notifs);
                    },
                .bounds = Rect {},
                .imgui_id = builder.imgui.MakeId("install success"),
                .viewport_config = ({
                    auto cfg = k_default_modal_viewport;
                    cfg.mode = imgui::ViewportMode::Floating;
                    cfg.positioning = imgui::ViewportPositioning::WindowCentred;
                    cfg.auto_size = true;
                    cfg.exclusive_focus = true;
                    cfg.z_order = 200;
                    cfg;
                }),
                .debug_name = "pkg-install-success-dialog",
            });
    }
}
