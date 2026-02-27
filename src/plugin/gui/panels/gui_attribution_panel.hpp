// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"

struct AttributionPanelContext {
    static constexpr u64 k_panel_id = HashFnv1a("attribution-panel");
    String attribution_text;
};

static void AttributionPanel(GuiBuilder& builder, AttributionPanelContext& context) {
    auto const root = DoModalRootBox(builder);

    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = "Attribution requirements",
                  });
    DoModalDivider(builder, root, {.horizontal = true});

    auto const main_container = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = layout::k_fill_parent,
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
            .parent = main_container,
            .text =
                "Floe is currently using sounds that require crediting the authors. If you publish your work, make the text below available alongside your work in a manner reasonable for the medium (description box, album notes, credits roll, etc.).",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    DoBox(
        builder,
        {
            .parent = main_container,
            .text =
                "This text is generated based on the sounds you have loaded in any instance of Floe. This window will disappear if there's no attribution required.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    auto const button_container = DoBox(builder,
                                        {
                                            .parent = main_container,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = 8,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

    if (TextButton(builder, button_container, {.text = "Copy to clipboard"}))
        dyn::Assign(GuiIo().out.set_clipboard_text, context.attribution_text);

    DoBox(builder,
          {
              .parent = main_container,
              .text = context.attribution_text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
          });
}

PUBLIC void DoAttributionPanel(GuiBuilder& builder, AttributionPanelContext& context) {
    if (!builder.imgui.IsModalOpen(context.k_panel_id)) return;
    if (context.attribution_text.size == 0) {
        builder.imgui.CloseModal(context.k_panel_id);
        return;
    }
    DoBoxViewport(builder,
                  {
                      .run = [&context](GuiBuilder& b) { AttributionPanel(b, context); },
                      .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                          WwToPixels(f32x2 {625, 443})),
                      .imgui_id = context.k_panel_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
