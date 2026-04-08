// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_instance_config_panel.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"
#include "processor/processor.hpp"

static void DrawDarkModeModalBackground(imgui::Context const& imgui) {
    imgui.draw_list->PushClipRectFullScreen();
    imgui.draw_list->AddRectFilled(0, GuiIo().in.window_size.ToFloat2(), 0x6c0f0d0d);
    imgui.draw_list->PopClipRect();

    auto const rounding = WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background1, .dark_mode = true}), rounding);
}

static void InstanceConfigPanel(GuiBuilder& builder, InstanceConfigPanelContext& context) {
    auto config = context.processor.instance_config.Load(LoadMemoryOrder::Relaxed);
    auto const initial_config = config;

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Header
    {
        auto const title_container = DoBox(builder,
                                           {
                                               .parent = root,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding = {.lrtb = k_default_spacing},
                                                   .contents_gap = k_default_spacing * 1.2f,
                                                   .contents_direction = layout::Direction::Row,
                                                   .contents_align = layout::Alignment::Justify,
                                               },
                                           });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = "Instance Config",
                  .font = FontType::Heading1,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, k_font_heading1_size},
                  },
              });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = ICON_FA_XMARK,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .button_behaviour =
                      imgui::ButtonConfig {
                          .closes_popup_or_modal = true,
                      },
                  .extra_margin_for_mouse_events = 8,
              });
    }

    // Divider
    {
        auto const one_pixel = PixelsToWw(1.0f);
        DoBox(builder,
              {
                  .parent = root,
                  .background_fill_colours = Col {.c = Col::Surface0, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, one_pixel},
                  },
              });
    }

    // Section heading
    DoBox(builder,
          {
              .parent = root,
              .text = "Reproducibility",
              .font = FontType::Heading2,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
              .layout {
                  .size = {layout::k_fill_parent, k_font_heading2_size},
                  .margins = {.t = k_default_spacing, .lr = k_default_spacing},
              },
          });

    DoBox(
        builder,
        {
            .parent = root,
            .text =
                "Control how fluctuating elements (such round robin or granular) behave. Resetting restores the sequence to a known state so that playback is exactly reproducible.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
            .font = FontType::Body,
            .text_colours = Col {.c = Col::White, .alpha = 120},
            .layout =
                {
                    .margins = {.t = 5, .lr = k_default_spacing},
                },
        });

    // Controls
    auto const controls = DoBox(builder,
                                {
                                    .parent = root,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.lrtb = k_default_spacing},
                                        .contents_gap = 10,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

    constexpr f32 k_field_width = 60;

    // Reset on transport
    {
        if (CheckboxButton(builder,
                           controls,
                           "Reset on transport"_s,
                           config.reset_on_transport,
                           "Reset round robin positions and random sequences when the DAW transport starts playing"_s,
                           GuiStyleSystem::TopBottomPanels))
            config.reset_on_transport = !config.reset_on_transport;
    }

    // Reset keyswitch
    {
        auto const row = DoBox(builder,
                               {
                                   .parent = controls,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_medium_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        bool const keyswitch_enabled = config.reset_keyswitch.HasValue();

        if (CheckboxButton(builder,
                           row,
                           "Reset keyswitch"_s,
                           keyswitch_enabled,
                           "Enable a MIDI note that resets round robin positions and random sequences"_s,
                           GuiStyleSystem::TopBottomPanels)) {
            if (keyswitch_enabled)
                config.reset_keyswitch = k_nullopt;
            else
                config.reset_keyswitch = (u7)0;
        }

        bool const ks_active = config.reset_keyswitch.HasValue();
        auto const keyswitch_value = (s64)config.reset_keyswitch.ValueOr((u7)0);

        if (auto const v =
                IntField(builder,
                         row,
                         {
                             .tooltip = "MIDI note that triggers a reset"_s,
                             .width = k_field_width,
                             .value = keyswitch_value,
                             .constrainer = [](s64 value) { return Clamp(value, (s64)0, (s64)127); },
                             .style = GuiStyleSystem::TopBottomPanels,
                             .midi_note_names = true,
                             .greyed_out = !ks_active,
                         })) {
            config.reset_keyswitch = (u7)*v;
        }
    }

    // Seed
    {
        if (auto const v = IntField(
                builder,
                controls,
                {
                    .label = "Seed",
                    .tooltip =
                        "Different seeds produce different round robin starting positions and random sequences"_s,
                    .width = k_field_width,
                    .value = config.seed,
                    .constrainer = [](s64 value) { return Clamp(value, (s64)0, (s64)99); },
                    .style = GuiStyleSystem::TopBottomPanels,
                })) {
            config.seed = (u8)*v;
        }
    }

    if (config != initial_config) context.processor.instance_config.Store(config, StoreMemoryOrder::Release);
}

void DoInstanceConfigPanel(GuiBuilder& builder,
                           InstanceConfigPanelContext& context,
                           InstanceConfigPanelState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    auto viewport_config = k_default_modal_viewport;
    viewport_config.draw_background = DrawDarkModeModalBackground;

    auto const window_size = GuiIo().in.window_size.ToFloat2();
    auto const panel_size = WwToPixels(f32x2 {400, 300});
    auto const bounds = Rect {.pos = 0, .size = window_size}.CentredRect(panel_size);

    DoBoxViewport(builder,
                  {
                      .run = [&context](GuiBuilder& b) { InstanceConfigPanel(b, context); },
                      .bounds = bounds,
                      .imgui_id = state.k_panel_id,
                      .viewport_config = viewport_config,
                  });
}
