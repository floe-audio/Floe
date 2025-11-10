// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "utils/error_notifications.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui/gui2_notifications.hpp"

PUBLIC void UninstallSampleLibrary(sample_lib::Library const& lib,
                                   ConfirmationDialogState& confirmation_dialog_state,
                                   ThreadsafeErrorNotifications& error_notifications,
                                   Notifications& notifications) {
    auto const library_path =
        lib.file_format_specifics.tag == sample_lib::FileFormat::Lua ? path::Directory(lib.path) : lib.path;
    if (library_path) {
        auto cloned_path = Malloc::Instance().Clone(*library_path);

        dyn::AssignFitInCapacity(confirmation_dialog_state.title, "Delete Library");
        fmt::Assign(
            confirmation_dialog_state.body_text,
            "Are you sure you want to delete the library '{}'?\n\nThis will send the library {} to the {}. You can restore it from there if needed.",
            lib.name,
            lib.file_format_specifics.tag == sample_lib::FileFormat::Lua ? "folder and all its contents"
                                                                         : "file",
            TRASH_NAME);

        confirmation_dialog_state.callback = [&error_notifications = error_notifications,
                                              &gui_notifications = notifications,
                                              cloned_path](ConfirmationDialogResult result) {
            DEFER { Malloc::Instance().Free(cloned_path.ToByteSpan()); };
            if (result == ConfirmationDialogResult::Ok) {
                ArenaAllocatorWithInlineStorage<Kb(1)> scratch_arena {Malloc::Instance()};
                auto const outcome = TrashFileOrDirectory(cloned_path, scratch_arena);
                auto const id = HashMultiple(Array {"library-delete"_s, cloned_path});

                if (outcome.HasValue()) {
                    error_notifications.RemoveError(id);
                    *gui_notifications.FindOrAppendUninitalisedOverwrite(id) = {
                        .get_diplay_info =
                            [p = DynamicArrayBounded<char, 200>(path::Filename(cloned_path))](
                                ArenaAllocator&) {
                                return NotificationDisplayInfo {
                                    .title = "Library Deleted",
                                    .message = p,
                                    .dismissable = true,
                                    .icon = NotificationDisplayInfo::IconType::Success,
                                };
                            },
                        .id = id,
                    };
                } else if (auto item = error_notifications.BeginWriteError(id)) {
                    DEFER { error_notifications.EndWriteError(*item); };
                    item->title = "Failed to send library to trash"_s;
                    item->error_code = outcome.Error();
                }
            }
        };

        confirmation_dialog_state.open = true;
    }
}
