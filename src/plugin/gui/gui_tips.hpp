// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/persistent_store.hpp"

#include "gui2_notifications.hpp"

static constexpr u64 k_tips_persistent_store_id = 3209482352034;

// IMPORTANT: tip must be a string literal.
PUBLIC void ShowTipIfNeeded(Notifications& notifications, persistent_store::Store& store, String tip) {
    auto const r = persistent_store::Get(store, k_tips_persistent_store_id);

    auto const tip_hash = HashFnv1a(tip);

    switch (r.tag) {
        case persistent_store::GetResult::StoreInaccessible: return;
        case persistent_store::GetResult::NotFound: {
            break;
        }
        case persistent_store::GetResult::Found: {
            auto const& values = r.Get<persistent_store::Value const*>();
            if (values->Contains(tip_hash)) {
                // Already shown.
                return;
            }
            break;
        }
    }

    // Not found or not already shown.

    *notifications.AppendUninitialised() = {
        .get_diplay_info = [tip](ArenaAllocator&) -> NotificationDisplayInfo {
            return {
                .title = "Tip"_s,
                .message = tip,
                .dismissable = true,
                .icon = NotificationDisplayInfo::IconType::Info,
            };
        },
        .id = tip_hash,
    };

    persistent_store::AddValue(store, k_tips_persistent_store_id, tip_hash);
}
