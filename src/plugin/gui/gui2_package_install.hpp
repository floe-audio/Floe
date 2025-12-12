// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "engine/package_installation.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui_framework/gui_box_system.hpp"

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

PUBLIC void PackageInstallAlertsPanel(GuiBoxSystem& box_system, package::InstallJobs& package_install_jobs) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DoBox(box_system,
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

            //
            auto const container = DoBox(box_system,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = style::k_prefs_medium_gap,
                                                 .contents_direction = layout::Direction::Column,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

            auto const text = InstallationOptionAskUserPretext(component, box_system.arena);

            DoBox(box_system,
                  {
                      .parent = container,
                      .text = text,
                      .wrap_width = -1,
                      .size_from_text = true,
                      .font = FontType::Body,
                  });

            auto const button_row = DoBox(box_system,
                                          {
                                              .parent = container,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_gap = style::k_prefs_medium_gap,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::Start,
                                              },
                                          });

            if (TextButton(box_system, button_row, {.text = "Skip"}))
                component.user_decision = package::InstallJob::UserDecision::Skip;
            if (TextButton(box_system, button_row, {.text = "Overwrite"}))
                component.user_decision = package::InstallJob::UserDecision::Overwrite;
        }
    }
}

PUBLIC void DoPackageInstallNotifications(GuiBoxSystem& box_system,
                                          package::InstallJobs& package_install_jobs,
                                          Notifications& notifications,
                                          ThreadsafeErrorNotifications& error_notifs,
                                          ThreadPool& thread_pool) {
    constexpr u64 k_installing_packages_notif_id = HashComptime("installing packages notification");
    if (!package_install_jobs.Empty()) {
        if (!notifications.Find(k_installing_packages_notif_id)) {
            *notifications.AppendUninitalisedOverwrite() = {
                .get_diplay_info =
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
                },
                .id = k_installing_packages_notif_id,
            };
            box_system.imgui.frame_output.ElevateUpdateRequest(
                GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        }

        bool user_input_needed = false;

        for (auto it = package_install_jobs.begin(); it != package_install_jobs.end();) {
            auto& job = *it;
            auto next = it;
            ++next;
            DEFER { it = next; };

            auto const package_install_error_id = HashMultiple(Array {"package-install"_s, job.job->path});

            auto const state = job.job->state.Load(LoadMemoryOrder::Acquire);
            switch (state) {
                case package::InstallJob::State::Installing: break;

                case package::InstallJob::State::DoneError: {
                    if (auto err = error_notifs.BeginWriteError(package_install_error_id)) {
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
                    error_notifs.RemoveError(package_install_error_id);

                    DynamicArrayBounded<char, k_notification_buffer_size - 24> buffer {};
                    u8 num_truncated = 0;
                    for (auto [index, component] : Enumerate(job.job->components)) {
                        if (!num_truncated) {
                            if (!dyn::AppendSpan(
                                    buffer,
                                    fmt::Format(box_system.arena,
                                                "{} {} {}\n",
                                                path::FilenameWithoutExtension(component.component.path),
                                                package::ComponentTypeString(component.component.type),
                                                package::TypeOfActionTaken(component))))
                                num_truncated = 1;
                        } else if (num_truncated != LargestRepresentableValue<decltype(num_truncated)>())
                            ++num_truncated;
                    }

                    *notifications.AppendUninitalisedOverwrite() = {
                        .get_diplay_info = [buffer, num_truncated](
                                               ArenaAllocator& scratch_arena) -> NotificationDisplayInfo {
                            NotificationDisplayInfo c {};
                            c.icon = NotificationDisplayInfo::IconType::Success;
                            c.dismissable = true;
                            c.title = "Installation Complete";
                            if (num_truncated == 0) {
                                c.message = buffer;
                            } else {
                                c.message =
                                    fmt::Format(scratch_arena, "{}\n... and {} more", buffer, num_truncated);
                            }
                            return c;
                        },
                        .id = HashComptime("package install success"),
                    };
                    box_system.imgui.frame_output.ElevateUpdateRequest(
                        GuiFrameResult::UpdateRequest::ImmediatelyUpdate);

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

        if (user_input_needed) {
            RunPanel(box_system,
                     Panel {
                         .run = [&package_install_jobs](
                                    GuiBoxSystem& b) { PackageInstallAlertsPanel(b, package_install_jobs); },
                         .data =
                             ModalPanel {
                                 .r = CentredRect(
                                     {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                     f32x2 {box_system.imgui.VwToPixels(style::k_install_dialog_width),
                                            box_system.imgui.VwToPixels(style::k_install_dialog_height)}),
                                 .imgui_id = box_system.imgui.GetID("install alerts"),
                                 .on_close = {},
                                 .close_on_click_outside = false,
                                 .darken_background = true,
                                 .disable_other_interaction = true,
                                 .auto_height = false,
                             },
                     });
        }
    } else {
        notifications.Remove(notifications.Find(k_installing_packages_notif_id));
    }
}
