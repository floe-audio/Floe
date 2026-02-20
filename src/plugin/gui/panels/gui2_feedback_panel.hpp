// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common_infrastructure/error_reporting.hpp"

#include "gui/elements/gui2_modal.hpp"
#include "gui/overlays/gui2_notifications.hpp"
#include "gui/panels/gui2_feedback_panel_state.hpp"

struct FeedbackPanelContext {
    Notifications& notifications;
};

static void FeedbackPanel(GuiBuilder& builder, FeedbackPanelContext& context, FeedbackPanelState& state) {
    auto const root = DoModalRootBox(builder);

    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = "Share Feedback",
                  });

    DoModalDivider(builder, root, {.horizontal = true});

    auto const panel = DoBox(builder,
                             {
                                 .parent = root,
                                 .layout {
                                     .size = {layout::k_fill_parent, layout::k_fill_parent},
                                     .contents_padding = {.lrtb = k_default_spacing},
                                     .contents_gap = k_default_spacing,
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

    DoBox(
        builder,
        {
            .parent = panel,
            .text =
                "Help us improve - share bug reports, feature requests, or any other feedback to make Floe better."_s,
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
            .font = FontType::Body,
        });

    DoBox(builder,
          {
              .parent = panel,
              .text = "Description:"_s,
              .size_from_text = true,
              .font = FontType::Body,
          });

    auto const description_field = TextInput(builder,
                                             panel,
                                             {
                                                 .text = state.description,
                                                 .size = f32x2 {layout::k_fill_parent, 90},
                                                 .multiline = true,
                                             });
    if (description_field.result && description_field.result->buffer_changed)
        dyn::AssignFitInCapacity(state.description, description_field.result->text);

    DoBox(builder,
          {
              .parent = panel,
              .text = "Email (optional):"_s,
              .size_from_text = true,
              .font = FontType::Body,
          });

    auto const email_field = TextInput(builder,
                                       panel,
                                       {
                                           .text = state.email,
                                           .size = f32x2 {layout::k_fill_parent, 30},
                                       });
    if (email_field.result && email_field.result->buffer_changed)
        dyn::AssignFitInCapacity(state.email, email_field.result->text);

    if (CheckboxButton(builder, panel, "Include anonymous diagnostic data"_s, state.send_diagnostic_data))
        state.send_diagnostic_data = !state.send_diagnostic_data;

    if (TextButton(builder, panel, {.text = "Submit"})) {
        auto const return_code = ReportFeedback(state.description,
                                                state.email.size ? Optional<String> {state.email} : k_nullopt,
                                                state.send_diagnostic_data);
        String notification_message = {};
        auto icon = NotificationDisplayInfo::IconType::Success;
        switch (return_code) {
            case ReportFeedbackReturnCode::Success: {
                notification_message = "Feedback submitted successfully"_s;
                dyn::Clear(state.description);
                dyn::Clear(state.email);
                builder.imgui.CloseTopModal();
                break;
            }
            case ReportFeedbackReturnCode::InvalidEmail: {
                notification_message = "Invalid email address"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::Busy: {
                notification_message = "Feedback submission already in progress"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::DescriptionTooLong: {
                notification_message = "Description too long"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
            case ReportFeedbackReturnCode::DescriptionEmpty: {
                notification_message = "Description cannot be empty"_s;
                icon = NotificationDisplayInfo::IconType::Error;
                break;
            }
        }
        *context.notifications.AppendUninitalisedOverwrite() = {
            .get_diplay_info = [icon,
                                title = notification_message](ArenaAllocator&) -> NotificationDisplayInfo {
                return {
                    .title = title,
                    .dismissable = true,
                    .icon = icon,
                };
            },
            .id = SourceLocationHash(),
        };
    }
}

PUBLIC void DoFeedbackPanel(GuiBuilder& builder, FeedbackPanelContext& context, FeedbackPanelState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;
    DoBoxViewport(builder,
                  {
                      .run = [&context, &state](GuiBuilder& b) { FeedbackPanel(b, context, state); },
                      .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                          GuiIo().WwToPixels(f32x2 {400, 443})),
                      .imgui_id = state.k_panel_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
