// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui.hpp"
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
                                    .contents_padding = {.l = 0, .r = 4, .tb = 4},
                                    .contents_gap = 0,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    auto& preferences = g->prefs;
    auto const keyboard_octave = Clamp<s64>(
        prefs::LookupInt(preferences, prefs::key::k_gui_keyboard_octave).ValueOr(k_octave_default_offset),
        k_octave_lowest,
        k_octave_highest);

    {
        auto const octave_box = DoBox(box_system,
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
                    g->engine.processor.events_for_audio_thread.Push(GuiNoteClickReleased {.key = key->note});
                g->engine.host.request_process(&g->engine.host);
            }
        }
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
