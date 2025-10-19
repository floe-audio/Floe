// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui2_common_modal_panel.hpp"

static void ConfirmationDialog(GuiBoxSystem& box_system, ConfirmationDialogState& state) {
    auto const root = DoModalRootBox(box_system);

    DoModalHeader(box_system,
                  {
                      .parent = root,
                      .title = state.title,
                      .on_close = [&state]() { state.open = false; },
                  });

    DoModalDivider(box_system, root, {.horizontal = true});

    auto const panel = DoBox(box_system,
                             {
                                 .parent = root,
                                 .layout {
                                     .size = {layout::k_fill_parent, layout::k_fill_parent},
                                     .contents_padding = {.lrtb = style::k_spacing},
                                     .contents_gap = style::k_spacing,
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

    DoBox(box_system,
          {
              .parent = panel,
              .text = state.body_text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Body,
          });

    auto const buttons_container = DoBox(box_system,
                                         {
                                             .parent = panel,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = style::k_spacing,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::End,
                                             },
                                         });

    if (TextButton(box_system, buttons_container, {.text = "Cancel"})) {
        state.open = false;
        if (state.callback) state.callback(ConfirmationDialogResult::Cancel);
    }

    if (TextButton(box_system, buttons_container, {.text = "OK"})) {
        state.open = false;
        if (state.callback) state.callback(ConfirmationDialogResult::Ok);
    }
}

PUBLIC void DoConfirmationDialog(GuiBoxSystem& box_system, ConfirmationDialogState& state) {
    if (!state.open) return;
    RunPanel(box_system,
             Panel {
                 .run = [&state](GuiBoxSystem& b) { ConfirmationDialog(b, state); },
                 .data =
                     ModalPanel {
                         .r = CentredRect(
                             {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                             f32x2 {box_system.imgui.VwToPixels(300), box_system.imgui.VwToPixels(220)}),
                         .imgui_id = box_system.imgui.GetID("confirmation"),
                         .on_close = [&state]() { state.open = false; },
                         .close_on_click_outside = true,
                         .darken_background = true,
                         .disable_other_interaction = true,
                     },
             });
}
