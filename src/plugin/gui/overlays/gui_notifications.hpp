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
    // This function is called every time the notification is displayed. It allows for changing the
    // notification text on-the-fly rather than caching a string once. The function object also has plenty
    // of space if data does need to be cached.
    TrivialFixedSizeFunction<k_notification_buffer_size, NotificationDisplayInfo(ArenaAllocator& arena)>
        get_diplay_info;
    u64 id;
    TimePoint time_added = TimePoint::Now();
};

struct Notifications : BoundedList<Notification, 10> {
    Notification* Find(u64 id) {
        for (auto& n : *this)
            if (n.id == id) return &n;
        return nullptr;
    }
    Notification* FindOrAppendUninitalisedOverwrite(u64 id) {
        auto* n = Find(id);
        if (n) return n;
        return AppendUninitalisedOverwrite();
    }
    TimePoint dismiss_check_counter {};
};

void NotificationsPanel(GuiBuilder& builder, Notifications& notifications);
void DoNotifications(GuiBuilder& builder, Notifications& notifications);
