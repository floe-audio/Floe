// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "gui/core/gui_fwd.hpp"

struct NotificationDisplayInfo {
    enum class IconType : u8 { None, Info, Success, Error };
    String title {};
    String message {};
    bool dismissable = true;
    IconType icon = IconType::None;
};

constexpr usize k_notification_buffer_size = 400;

struct Notification {
    using GetDisplayFunc =
        TrivialFixedSizeFunction<k_notification_buffer_size, NotificationDisplayInfo(ArenaAllocator& arena)>;

    // This function is called every time the notification is displayed. It allows for changing the
    // notification text on-the-fly rather than caching a string once. The function object also has plenty
    // of space if data does need to be cached.
    GetDisplayFunc get_diplay_info;

    // These are set automatically when a notification is added.
    u64 id;
    TimePoint time_added;
};

struct Notifications : BoundedList<Notification, 10> {
    using Super = BoundedList<Notification, 10>;

    Notification* Find(u64 id) {
        for (auto& n : *this)
            if (n.id == id) return &n;
        return nullptr;
    }

    // Returns true if newly added. See Notification::get_display_info.
    bool AddOrUpdate(u64 id, auto&& get_display_info) {
        if (auto* n = Find(id)) {
            n->get_diplay_info = get_display_info;
            return false;
        }

        *Super::AppendUninitalisedOverwrite() = {
            .get_diplay_info = get_display_info,
            .id = id,
            .time_added = TimePoint::Now(),
        };
        return true;
    }

    void Remove(u64 id) { Super::Remove(Find(id)); }
    Super::Iterator Remove(Super::Iterator it) { return Super::Remove(it); }

    // We avoid basic appending because we always want to check IDs to enforce that items with the same ID are
    // not added twice.
    void AppendUninitalisedOverwrite() = delete;
    void AppendUninitialised() = delete;

    TimePoint dismiss_check_counter {};
};

void NotificationsPanel(GuiBuilder& builder, Notifications& notifications);
void DoNotifications(GuiBuilder& builder, Notifications& notifications);
