// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "utils/error_notifications.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"

PUBLIC void DoErrorsPanel(GuiBuilder& builder,
                          Span<ThreadsafeErrorNotifications* const> error_notifications) {
    bool has_any = false;
    for (auto* notifs : error_notifications) {
        if (notifs->HasErrors()) {
            has_any = true;
            break;
        }
    }
    if (!has_any) return;

    constexpr imgui::Id k_errors_panel_id = SourceLocationHash();

    auto viewport_config = k_default_modal_viewport;
    viewport_config.mode = imgui::ViewportMode::Floating;
    viewport_config.positioning = imgui::ViewportPositioning::WindowCentred;
    viewport_config.z_order = 100;
    viewport_config.auto_size = true;

    DoBoxViewport(
        builder,
        {
            .run =
                [error_notifications](GuiBuilder& builder) {
                    auto const root =
                        DoBox(builder,
                              {
                                  .layout {
                                      .size = {400, layout::k_hug_contents},
                                      .contents_padding = {.lrtb = k_default_spacing},
                                      .contents_gap = k_default_spacing,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

                    DoBox(builder,
                          {
                              .parent = root,
                              .text = "Error",
                              .size_from_text = true,
                              .font = FontType::Heading1,
                          });

                    DoModalDivider(builder, root, {.horizontal = true});

                    u8 num_errors = 0;
                    for (auto* notifs : error_notifications) {
                        notifs->ForEach([&](ThreadsafeErrorNotifications::Item const& e)
                                            -> ThreadsafeErrorNotifications::ItemIterationResult {
                            builder.imgui.PushId((uintptr)e.id.Load(LoadMemoryOrder::Acquire));
                            DEFER { builder.imgui.PopId(); };

                            if (num_errors > 0)
                                DoModalDivider(builder, root, {.horizontal = true, .subtle = true});

                            // Title
                            DoBox(builder,
                                  {
                                      .parent = root,
                                      .text = e.title,
                                      .wrap_width = k_wrap_to_parent,
                                      .size_from_text = true,
                                      .font = FontType::Heading2,
                                  });

                            // Description
                            {
                                DynamicArray<char> error_text {builder.arena};
                                if (e.message.size) dyn::AppendSpan(error_text, e.message);
                                if (e.error_code) {
                                    if (error_text.size) dyn::Append(error_text, '\n');
                                    fmt::Append(error_text, "{u}.", *e.error_code);
                                }

                                if (error_text.size)
                                    DoBox(builder,
                                          {
                                              .parent = root,
                                              .text = error_text,
                                              .wrap_width = k_wrap_to_parent,
                                              .size_from_text = true,
                                              .font = FontType::Body,
                                          });
                            }

                            // Buttons
                            auto const button_container =
                                DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = 8,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                          },
                                      });

                            if (TextButton(builder, button_container, {.text = "Copy to clipboard"_s})) {
                                auto& clipboard = GuiIo().out.set_clipboard_text;
                                dyn::Assign(clipboard, e.title);
                                if (e.message.size) {
                                    dyn::Append(clipboard, '\n');
                                    dyn::AppendSpan(clipboard, e.message);
                                }
                                if (e.error_code) {
                                    dyn::Append(clipboard, '\n');
                                    fmt::Append(clipboard, "{u}.", *e.error_code);
                                }
                            }

                            if (TextButton(builder, button_container, {.text = "Dismiss"_s}))
                                return ThreadsafeErrorNotifications::ItemIterationResult::Remove;

                            ++num_errors;
                            return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
                        });
                    }
                },
            .bounds = Rect {},
            .imgui_id = k_errors_panel_id,
            .viewport_config = viewport_config,
            .debug_name = "errors-panel",
        });
}
