// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui/elements/gui2_modal.hpp"
#include "gui/overlays/gui2_confirmation_dialog_state.hpp"

static void ConfirmationDialog(GuiBuilder& builder, ConfirmationDialogState& state) {
    auto const root = DoModalRootBox(builder);

    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = state.title,
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

    DoBox(builder,
          {
              .parent = panel,
              .text = state.body_text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Body,
          });

    auto const buttons_container = DoBox(builder,
                                         {
                                             .parent = panel,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = k_default_spacing,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::End,
                                             },
                                         });

    if (TextButton(builder, buttons_container, {.text = "Cancel"})) {
        builder.imgui.CloseTopModal();
        if (state.callback) state.callback(ConfirmationDialogResult::Cancel);
    }

    if (TextButton(builder, buttons_container, {.text = "OK"})) {
        builder.imgui.CloseTopModal();
        if (state.callback) state.callback(ConfirmationDialogResult::Ok);
    }
}

PUBLIC void DoConfirmationDialog(GuiBuilder& builder, ConfirmationDialogState& state) {
    if (!builder.imgui.IsModalOpen(state.k_id)) return;
    DoBoxViewport(builder,
                  {
                      .run = [&state](GuiBuilder& b) { ConfirmationDialog(b, state); },
                      .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                          GuiIo().WwToPixels(f32x2 {300.0f, 220.0f})),
                      .imgui_id = state.k_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
