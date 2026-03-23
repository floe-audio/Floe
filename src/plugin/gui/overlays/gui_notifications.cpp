// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_notifications.hpp"

#include <IconsFontAwesome6.h>

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"

void NotificationsPanel(GuiBuilder& builder, Notifications& notifications) {
    constexpr f64 k_dismiss_seconds = 10;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto it = notifications.begin(); it != notifications.end();) {
        auto const& n = *it;
        auto next = it;
        ++next;
        DEFER { it = next; };

        auto const config = n.get_diplay_info(builder.arena);

        if (config.dismissable && n.time_added.SecondsFromNow() > k_dismiss_seconds) {
            next = notifications.Remove(it);
            continue;
        }

        builder.imgui.PushId(n.id);
        DEFER { builder.imgui.PopId(); };

        auto const notification = DoBox(builder,
                                        {
                                            .parent = root,
                                            .background_fill_colours = Col {.c = Col::Background0},
                                            .drop_shadow = true,
                                            .round_background_corners = 0b1111,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_padding = {.lrtb = k_default_spacing},
                                                .contents_gap = k_default_spacing,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });
        auto const title_container = DoBox(builder,
                                           {
                                               .parent = notification,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_direction = layout::Direction::Row,
                                                   .contents_align = layout::Alignment::Justify,
                                               },
                                           });

        auto const lhs_container = DoBox(builder,
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
            DoBox(builder,
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
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .text_colours = {({
                          Col::Id c {};
                          switch (config.icon) {
                              case NotificationDisplayInfo::IconType::None: PanicIfReached();
                              case NotificationDisplayInfo::IconType::Info: c = Col::Subtext1; break;
                              case NotificationDisplayInfo::IconType::Success: c = Col::Green; break;
                              case NotificationDisplayInfo::IconType::Error: c = Col::Red; break;
                          }
                          Col {.c = c};
                      })},
                  });
        }

        DoBox(builder,
              {
                  .parent = lhs_container,
                  .text = config.title,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        if (config.dismissable) {
            if (DoBox(builder,
                      {
                          .parent = title_container,
                          .text = ICON_FA_XMARK,
                          .size_from_text = true,
                          .font = FontType::Icons,
                          .background_fill_auto_hot_active_overlay = true,
                          .round_background_corners = 0b1111,
                          .button_behaviour = imgui::ButtonConfig {},
                          .extra_margin_for_mouse_events = 8,
                      })
                    .button_fired) {
                next = notifications.Remove(it);
            }
        }

        if (config.message.size) {
            DoBox(builder,
                  {
                      .parent = notification,
                      .text = config.message,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                      .font = FontType::Body,
                  });
        }
    }
}

void DoNotifications(GuiBuilder& builder, Notifications& notifications) {
    if (!notifications.Empty()) {
        auto const width_px = WwToPixels(400.0f);
        auto const spacing = PixelsToWw(k_default_spacing);

        DoBoxViewport(builder,
                      {
                          .run = [&notifications](GuiBuilder& b) { NotificationsPanel(b, notifications); },
                          .bounds =
                              Rect {
                                  .x = builder.imgui.CurrentVpWidth() - width_px - spacing,
                                  .y = spacing,
                                  .w = width_px,
                                  .h = 4,
                              },
                          .imgui_id = builder.imgui.MakeId("notifications"),
                          .viewport_config = ({
                              auto cfg = k_default_modal_viewport;
                              cfg.mode = imgui::ViewportMode::Floating;
                              cfg.auto_size = {false, true};
                              cfg.exclusive_focus = false;
                              cfg.ignore_exclusive_focus = true;
                              cfg.close_on_click_outside = false;
                              cfg.close_on_escape = false;
                              cfg.draw_background = {};
                              cfg.z_order = 120;
                              cfg;
                          }),
                          .debug_name = "notifications",
                      });

        GuiIo().WakeupAtTimedInterval(notifications.dismiss_check_counter, 1, SourceLocationHash());
    }
}
