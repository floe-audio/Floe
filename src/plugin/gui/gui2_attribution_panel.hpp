// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui2_common_modal_panel.hpp"
#include "gui_framework/gui_box_system.hpp"

struct AttributionPanelContext {
    String attribution_text;
};

static void AttributionPanel(GuiBoxSystem& box_system, AttributionPanelContext& context, bool& open) {
    auto const root = DoModalRootBox(box_system);

    DoModalHeader(box_system,
                  {
                      .parent = root,
                      .title = "Attribution requirements",
                      .on_close = [&open]() { open = false; },
                  });
    DoModalDivider(box_system, root, {.horizontal = true});

    auto const main_container = DoBox(box_system,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = layout::k_fill_parent,
                                              .contents_padding = {.lrtb = style::k_spacing},
                                              .contents_gap = style::k_spacing,
                                              .contents_direction = layout::Direction::Column,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                          },
                                      });

    DoBox(
        box_system,
        {
            .parent = main_container,
            .text =
                "Floe is currently using sounds that require crediting the authors. If you publish your work, make the text below available alongside your work in a manner reasonable for the medium (description box, album notes, credits roll, etc.).",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    DoBox(
        box_system,
        {
            .parent = main_container,
            .text =
                "This text is generated based on the sounds you have loaded in any instance of Floe. This window will disappear if there's no attribution required.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    auto const button_container = DoBox(box_system,
                                        {
                                            .parent = main_container,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = 8,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

    if (TextButton(box_system, button_container, {.text = "Copy to clipboard"}))
        dyn::Assign(box_system.imgui.clipboard_for_os, context.attribution_text);

    DoBox(box_system,
          {
              .parent = main_container,
              .text = context.attribution_text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
          });
}

PUBLIC void DoAttributionPanel(GuiBoxSystem& box_system, AttributionPanelContext& context, bool& open) {
    if (context.attribution_text.size == 0) {
        open = false;
        return;
    }
    if (open) {
        RunPanel(box_system,
                 Panel {
                     .run = [&context, &open](GuiBoxSystem& b) { AttributionPanel(b, context, open); },
                     .data =
                         ModalPanel {
                             .r = CentredRect(
                                 {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                 f32x2 {box_system.imgui.VwToPixels(style::k_info_dialog_width),
                                        box_system.imgui.VwToPixels(style::k_info_dialog_height)}),
                             .imgui_id = box_system.imgui.GetID("new info"),
                             .on_close = [&open]() { open = false; },
                             .close_on_click_outside = true,
                             .darken_background = true,
                             .disable_other_interaction = true,
                         },
                 });
    }
}
