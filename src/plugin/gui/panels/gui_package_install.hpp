// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <IconsFontAwesome6.h>

#include "os/filesystem.hpp"

#include "common_infrastructure/persistent_store.hpp"

#include "engine/package_installation.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui_framework/gui_builder.hpp"

struct PackageInstallPanelState {
    enum class LicenseInputMode : u8 { AllKeysAtOnce, PerProduct };
    LicenseInputMode license_input_mode = LicenseInputMode::AllKeysAtOnce;

    struct SuccessfulInstall {
        DynamicArray<char> package_path {Malloc::Instance()};
        DynamicArrayBounded<char, k_notification_buffer_size - 160> components_text {};
        u8 num_truncated {};
        DynamicArrayBounded<char, 128> license_email {};
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

// Renders the per-package "paste a key" blocks. Used by the "Enter each key manually" method.
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
                           "Enter each key manually",
                           panel_state.license_input_mode == Mode::PerProduct,
                           1))
        panel_state.license_input_mode = Mode::PerProduct;

    switch (panel_state.license_input_mode) {
        case Mode::AllKeysAtOnce: {
            DoBox(builder,
                  {
                      .parent = root,
                      .text = "Unlock your package(s) using your license key file. One file unlocks all.",
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

PUBLIC void PackageInstallSuccessPanel(GuiBuilder& builder,
                                       PackageInstallPanelState& panel_state,
                                       Notifications& notifications,
                                       ThreadsafeErrorNotifications& error_notifs) {
    auto& record = panel_state.successful_installs[0];

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

    auto const title_row = DoBox(builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_hug_contents, layout::k_hug_contents},
                                         .contents_gap = k_medium_gap,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });
    DoBox(builder,
          {
              .parent = title_row,
              .text = ICON_FA_CHECK,
              .size_from_text = true,
              .font = FontType::Icons,
              .text_colours = Col {.c = Col::Green},
          });
    DoBox(builder,
          {
              .parent = title_row,
              .text = "Installation Complete",
              .size_from_text = true,
          });

    auto const message = ({
        String m = record.components_text;
        if (record.num_truncated)
            m = fmt::Format(builder.arena, "{}\n... and {} more", m, record.num_truncated);
        if (record.license_email.size)
            m = fmt::Format(builder.arena, "{}Licensed to: {}", m, record.license_email);
        m;
    });
    DoBox(builder,
          {
              .parent = root,
              .text = message,
              .wrap_width = -1,
              .size_from_text = true,
              .font = FontType::Body,
          });

    DoBox(builder,
          {
              .parent = root,
              .text = fmt::Format(
                  builder.arena,
                  "The package file \"{}\" is no longer needed. Would you like to send it to the " TRASH_NAME
                  "?",
                  path::Filename(record.package_path)),
              .wrap_width = -1,
              .size_from_text = true,
              .font = FontType::Body,
          });

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

    auto const keep_clicked = TextButton(builder, button_row, {.text = "Keep File"});
    auto const trash_clicked =
        TextButton(builder, button_row, {.text = "Send to " TRASH_NAME, .is_default = true});

    if (trash_clicked) {
        ArenaAllocatorWithInlineStorage<Kb(1)> scratch_arena {Malloc::Instance()};
        auto const outcome = TrashFileOrDirectory(record.package_path, scratch_arena);
        auto const id = HashMultiple(Array {"package-file-trash"_s, String(record.package_path)});

        if (outcome.HasValue()) {
            error_notifs.RemoveError(id);
            notifications.AddOrUpdate(
                id,
                [f = DynamicArrayBounded<char, 200>(path::Filename(record.package_path))](ArenaAllocator&) {
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

    if (keep_clicked || trash_clicked) dyn::Remove(panel_state.successful_installs, 0);
}

PUBLIC void DoPackageInstallNotifications(GuiBuilder& builder,
                                          package::InstallJobs& package_install_jobs,
                                          Notifications& notifications,
                                          ThreadsafeErrorNotifications& error_notifs,
                                          ThreadPool& thread_pool,
                                          PackageInstallPanelState& panel_state,
                                          FilePickerState& file_picker_state,
                                          persistent_store::Store& persistent_store) {
    constexpr u64 k_installing_packages_notif_id = HashFnv1a("installing packages notification");
    bool user_input_needed = false;
    bool license_key_needed = false;
    if (!package_install_jobs.Empty()) {
        if (notifications.AddOrUpdate(
                k_installing_packages_notif_id,
                [&package_install_jobs](ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                    NotificationDisplayInfo c {};
                    c.icon = NotificationDisplayInfo::IconType::Info;
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
                        fmt::Assign(err->title,
                                    "Failed to install {}",
                                    path::FilenameWithoutExtension(job.job->path));
                        dyn::AssignFitInCapacity(err->message, job.job->error_buffer);
                    }

                    next = package::RemoveJob(package_install_jobs, it);
                    break;
                }

                case package::InstallJob::State::DoneSuccess: {
                    error_notifs.RemoveError(job_id);

                    DynamicArrayBounded<char, k_notification_buffer_size - 160> buffer {};
                    u8 num_truncated = 0;
                    for (auto [index, component] : Enumerate(job.job->components)) {
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

                    if (panel_state.successful_installs.size != panel_state.successful_installs.Capacity()) {
                        PackageInstallPanelState::SuccessfulInstall record {};
                        dyn::Assign(record.package_path, job.job->path);
                        record.components_text = buffer;
                        record.num_truncated = num_truncated;
                        dyn::AssignFitInCapacity(record.license_email, job.job->license_email);
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
        DoBoxViewport(builder,
                      {
                          .run =
                              [&panel_state, &notifications, &error_notifs](GuiBuilder& b) {
                                  PackageInstallSuccessPanel(b, panel_state, notifications, error_notifs);
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
