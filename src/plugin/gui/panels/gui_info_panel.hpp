// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/error_notifications.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "engine/check_for_update.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct VoicePool;

struct InfoPanelState {
    enum class Tab : u32 {
        Libraries,
        About,
        Metrics,
        Legal,
        Count,
    };
    static constexpr u64 k_panel_id = SourceLocationHash();
    bool opened_before {};
    Tab tab {Tab::Libraries};
};

struct InfoPanelContext {
    sample_lib_server::Server& server;
    VoicePool& voice_pool;
    ArenaAllocator& scratch_arena;
    check_for_update::State& check_for_update_state;
    prefs::Preferences& prefs;
    Span<sample_lib_server::ResourcePointer<sample_lib::Library>> libraries;
    ThreadsafeErrorNotifications& error_notifications;
    Notifications& notifications;
    ConfirmationDialogState& confirmation_dialog_state;
};

void DoInfoPanel(GuiBuilder& builder, InfoPanelContext& context, InfoPanelState& state);
