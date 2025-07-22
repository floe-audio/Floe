// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui_framework/gui_box_system.hpp"

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

PUBLIC void NotificationsPanel(GuiBoxSystem& box_system, Notifications& notifications) {
    constexpr f64 k_dismiss_seconds = 6;

    auto const root =
        DoBox(box_system,
              {
                  .layout {
                      .size = {box_system.imgui.PixelsToVw(box_system.imgui.Width()), layout::k_hug_contents},
                      .contents_gap = style::k_spacing,
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                  },
              });

    for (auto it = notifications.begin(); it != notifications.end();) {
        auto const& n = *it;
        auto next = it;
        ++next;
        DEFER { it = next; };

        auto const config = n.get_diplay_info(box_system.arena);

        if (config.dismissable && n.time_added.SecondsFromNow() > k_dismiss_seconds) {
            next = notifications.Remove(it);
            continue;
        }

        auto const notification = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .background_fill = style::Colour::Background0,
                                            .drop_shadow = true,
                                            .round_background_corners = 0b1111,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_padding = {.lrtb = style::k_spacing},
                                                .contents_gap = style::k_spacing,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });
        auto const title_container = DoBox(box_system,
                                           {
                                               .parent = notification,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_direction = layout::Direction::Row,
                                                   .contents_align = layout::Alignment::Justify,
                                               },
                                           });

        auto const lhs_container = DoBox(box_system,
                                         {
                                             .parent = title_container,
                                             .layout {
                                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                 .contents_gap = 8,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                             },
                                         });

        if (config.icon != NotificationDisplayInfo::IconType::None) {
            DoBox(box_system,
                  {
                      .parent = lhs_container,
                      .text = ({
                          String str {};
                          switch (config.icon) {
                              case NotificationDisplayInfo::IconType::None: PanicIfReached();
                              case NotificationDisplayInfo::IconType::Info: str = ICON_FA_INFO; break;
                              case NotificationDisplayInfo::IconType::Success: str = ICON_FA_CHECK; break;
                              case NotificationDisplayInfo::IconType::Error:
                                  str = ICON_FA_TRIANGLE_EXCLAMATION;
                                  break;
                          }
                          str;
                      }),
                      .font = FontType::Icons,
                      .text_fill = ({
                          style::Colour c {};
                          switch (config.icon) {
                              case NotificationDisplayInfo::IconType::None: PanicIfReached();
                              case NotificationDisplayInfo::IconType::Info:
                                  c = style::Colour::Subtext1;
                                  break;
                              case NotificationDisplayInfo::IconType::Success:
                                  c = style::Colour::Green;
                                  break;
                              case NotificationDisplayInfo::IconType::Error: c = style::Colour::Red; break;
                          }
                          c;
                      }),
                      .size_from_text = true,
                  });
        }

        DoBox(box_system,
              {
                  .parent = lhs_container,
                  .text = config.title,
                  .font = FontType::Body,
                  .size_from_text = true,
              });

        if (config.dismissable) {
            if (DoBox(box_system,
                      {
                          .parent = title_container,
                          .text = ICON_FA_XMARK,
                          .font = FontType::Icons,
                          .size_from_text = true,
                          .background_fill_auto_hot_active_overlay = true,
                          .round_background_corners = 0b1111,
                          .behaviour =
                              BoxConfig::Button {
                                  .extra_margin_for_mouse_events = 8,
                              },
                      })
                    .button_fired) {
                next = notifications.Remove(it);
            }
        }

        if (config.message.size) {
            DoBox(box_system,
                  {
                      .parent = notification,
                      .text = config.message,
                      .wrap_width = k_wrap_to_parent,
                      .font = FontType::Body,
                      .size_from_text = true,
                  });
        }
    }
}

PUBLIC void DoNotifications(GuiBoxSystem& box_system, Notifications& notifications) {
    if (!notifications.Empty()) {
        auto const width_px = box_system.imgui.VwToPixels(style::k_notification_panel_width);
        auto const spacing = box_system.imgui.PixelsToVw(style::k_spacing);

        RunPanel(box_system,
                 Panel {
                     .run = [&notifications](GuiBoxSystem& b) { NotificationsPanel(b, notifications); },
                     .data =
                         ModalPanel {
                             .r {
                                 .x = box_system.imgui.Width() - width_px - spacing,
                                 .y = spacing,
                                 .w = width_px,
                                 .h = 4,
                             },
                             .imgui_id = box_system.imgui.GetID("notifications"),
                             .on_close = []() {},
                             .close_on_click_outside = false,
                             .darken_background = false,
                             .disable_other_interaction = false,
                             .auto_height = true,
                             .transparent_panel = true,
                         },
                 });

        box_system.imgui.WakeupAtTimedInterval(notifications.dismiss_check_counter, 1);
    }
}
