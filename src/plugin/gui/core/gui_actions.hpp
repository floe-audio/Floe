// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "utils/error_notifications.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/overlays/gui_notifications.hpp"

void UninstallSampleLibrary(imgui::Context& imgui,
                            sample_lib::Library const& lib,
                            ConfirmationDialogState& confirmation_dialog_state,
                            ThreadsafeErrorNotifications& error_notifications,
                            Notifications& notifications);
