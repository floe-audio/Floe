// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui_keyboard.hpp"
#include "gui_widget_helpers.hpp"

static bool IconButton(GuiBoxSystem& box_system,
                       Box const parent,
                       String icon,
                       TooltipString tooltip,
                       f32 font_scale = 1.0f) {
    auto const button = DoBox(box_system,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = layout::k_hug_contents,
                                      .contents_padding = {.lr = 3, .tb = 2},
                                  },
                                  .tooltip = tooltip,
                                  .behaviour = Behaviour::Button,
                              });

    DoBox(box_system,
          {
              .parent = button,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = style::k_font_icons_size * font_scale,
              .text_colours {
                  .base = style::Colour::DarkModeSubtext1,
                  .hot = style::Colour::Highlight,
                  .active = style::Colour::Highlight,
              },
              .parent_dictates_hot_and_active = true,
          });
    return button.button_fired;
}

static Optional<s64> OctaveDragger(GuiBoxSystem& box_system, Box const parent, s64 value) {
    auto percent = MapTo01((f32)value, k_octave_lowest, k_octave_highest);
    auto const box = DoBox(box_system,
                           {
                               .parent = parent,
                               .text = fmt::Format(box_system.arena, "{+}", value),
                               .text_align_x = TextAlignX::Centre,
                               .text_align_y = TextAlignY::Centre,
                               .layout {
                                   .size = {28, style::k_font_body_size},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .behaviour = Behaviour::TextInput | Behaviour::Knob,
                               .activate_on_click_button = MouseButton::Left,
                               .activate_on_double_click = true,
                               .activation_click_event = ActivationClickEvent::Down,
                               .knob_percent = percent,
                               .knob_sensitivity = 20,
                           });

    Optional<s64> new_value {};

    if (box.text_input_result &&
        (box.text_input_result->buffer_changed || box.text_input_result->enter_pressed)) {
        new_value = ParseInt(box.text_input_result->text, ParseIntBase::Decimal);
    }

    if (!__builtin_isnan(box.knob_percent))
        new_value = (s64)MapFrom01(box.knob_percent, k_octave_lowest, k_octave_highest);

    DrawTextInput(box_system,
                  box,
                  {
                      .text_col = style::Colour::DarkModeText,
                      .cursor_col = style::Colour::DarkModeText,
                      .selection_col = style::Colour::Highlight,
                  });

    return new_value;
}

static void DoBotPanel(Gui* g) {
    auto& box_system = g->box_system;
    auto const root_size = box_system.imgui.PixelsToVw(box_system.imgui.Size());

    auto const root = DoBox(box_system,
                            {
                                .background_fill_colours = {style::Colour::DarkModeBackground0},
                                .layout {
                                    .size = root_size,
                                    .contents_gap = 0,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    {
        constexpr auto k_border_col = style::Colour::DarkModeBackground2;
        constexpr auto k_top_bot_margin = 2.0f;

        auto const tabs = DoBox(box_system,
                                {
                                    .parent = root,
                                    .background_fill_colours = {style::Colour::DarkModeBackground1},
                                    .layout {
                                        .size = {layout::k_hug_contents, layout::k_fill_parent},
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

        if (auto const rel_r = BoxRect(box_system, tabs)) {
            auto const r = box_system.imgui.GetRegisteredAndConvertedRect(*rel_r);
            // Draw a divider line on the inside right side of the tab box. We do this here because it creates
            // a nice consistent line - active tabs will draw over it to connect with the main content.
            box_system.imgui.graphics->AddRectFilled(Rect {.xywh {r.x + r.w - 1, r.y, 1, r.h}},
                                                     style::Col(k_border_col));
        }

        auto const tab_button = [&](BottomPanelType type, TooltipString tooltip) {
            auto const btn =
                DoBox(box_system,
                      {
                          .parent = tabs,
                          .background_fill_colours =
                              Splat(type == g->bottom_panel_state.type ? style::Colour::DarkModeBackground0
                                                                       : style::Colour::None),
                          .border_colours = Splat(k_border_col),
                          .border_edges = type == g->bottom_panel_state.type ? (u32)0b0101 : 0b0000,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .margins = {.t = ToInt(type) == 0 ? k_top_bot_margin : 0.0f,
                                          .b = BottomPanelType(ToInt(type) + 1) == BottomPanelType::Count
                                                   ? k_top_bot_margin
                                                   : 0.0f},
                              .contents_padding = {.lr = 5, .tb = 4},
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                          .tooltip = tooltip,
                          .behaviour = Behaviour::Button,
                      });

            DoBox(box_system,
                  {
                      .parent = btn,
                      .text =
                          [type]() {
                              switch (type) {
                                  case BottomPanelType::Play: return "Play"_s;
                                  case BottomPanelType::EditMacros: return "Macros"_s;
                                  case BottomPanelType::Count: PanicIfReached();
                              }
                          }(),
                      .size_from_text = true,
                      .text_colours {
                          .base = type == g->bottom_panel_state.type ? style::Colour::Highlight
                                                                     : style::Colour::DarkModeText,
                          .hot = style::Colour::Highlight,
                          .active = style::Colour::Highlight,
                      },
                      .parent_dictates_hot_and_active = true,
                  });
            return btn;
        };

        Optional<BottomPanelType> new_panel {};

        if (tab_button(BottomPanelType::Play, "Play tab: core UI for playing sounds"_s).button_fired)
            new_panel = BottomPanelType::Play;

        if (tab_button(BottomPanelType::EditMacros, "Edit macros tabs: change macro destinations and names"_s)
                .button_fired)
            new_panel = BottomPanelType::EditMacros;

        if (new_panel)
            dyn::Append(box_system.state->deferred_actions,
                        [t = *new_panel, &state = g->bottom_panel_state]() { state.type = t; });
    }

    switch (g->bottom_panel_state.type) {
        case BottomPanelType::Play: {
            // Macro knobs.
            {
                auto const macro_box =
                    DoBox(box_system,
                          {
                              .parent = root,
                              .background_fill_colours = {style::Colour::None},
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                  .margins = {.lrtb = 3},
                                  .contents_padding = {.lr = 20},
                                  .contents_gap = 30,
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                for (auto const [macro_index, param_index] : Enumerate(k_macro_params))
                    DoParameterComponent(
                        g,
                        macro_box,
                        g->engine.processor.main_params.DescribedValue(param_index),
                        {
                            .greyed_out = g->engine.processor.main_macro_destinations[macro_index].size == 0,
                            .override_label = g->engine.macro_names[macro_index],
                        });
            }

            auto& preferences = g->prefs;
            auto const keyboard_octave =
                Clamp<s64>(prefs::LookupInt(preferences, prefs::key::k_gui_keyboard_octave)
                               .ValueOr(k_octave_default_offset),
                           k_octave_lowest,
                           k_octave_highest);

            {
                auto const keyboard = DoBox(box_system,
                                            {
                                                .parent = root,
                                                .layout {
                                                    .size = layout::k_fill_parent,
                                                    .margins = {.l = 0, .r = 3, .tb = 3},
                                                },
                                            });
                if (auto const r = BoxRect(box_system, keyboard)) {
                    if (auto key = KeyboardGui(g, *r, (int)keyboard_octave)) {
                        if (key->is_down)
                            g->engine.processor.events_for_audio_thread.Push(
                                GuiNoteClicked {.key = key->note, .velocity = key->velocity});
                        else
                            g->engine.processor.events_for_audio_thread.Push(
                                GuiNoteClickReleased {.key = key->note});
                        g->engine.host.request_process(&g->engine.host);
                    }
                }
            }

            {
                auto const octave_box =
                    DoBox(box_system,
                          {
                              .parent = root,
                              .layout {
                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                  .contents_direction = layout::Direction::Column,
                                  .contents_align = layout::Alignment::Middle,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                Optional<s64> new_octave {};

                if (IconButton(box_system, octave_box, ICON_FA_CARET_UP, "GUI Keyboard Octave Up"_s))
                    new_octave = Min<s64>(keyboard_octave + 1, k_octave_highest);

                if (auto const v = OctaveDragger(box_system, octave_box, keyboard_octave)) new_octave = *v;

                if (IconButton(box_system, octave_box, ICON_FA_CARET_DOWN, "GUI Keyboard Octave Down"_s))
                    new_octave = Max<s64>(keyboard_octave - 1, k_octave_lowest);

                if (new_octave) prefs::SetValue(preferences, prefs::key::k_gui_keyboard_octave, *new_octave);
            }
            break;
        }

        case BottomPanelType::EditMacros: {
            DoMacrosEditGui(g, root);
            break;
        }

        case BottomPanelType::Count: PanicIfReached();
    }
}

void BotPanel(Gui* g, Rect const r) {
    RunPanel(g->box_system,
             {
                 .run = [g](GuiBoxSystem&) { DoBotPanel(g); },
                 .data =
                     Subpanel {
                         .rect = r,
                         .imgui_id = g->imgui.GetID("BotPanel"),
                         .flags = imgui::WindowFlags_NoScrollbarX | imgui::WindowFlags_NoScrollbarY,
                     },
             });
}
