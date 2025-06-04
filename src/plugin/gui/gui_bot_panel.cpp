// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui_button_widgets.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_keyboard.hpp"
#include "gui_widget_helpers.hpp"

void BotPanel(Gui* g) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& engine = g->engine;

    auto const button_h = LiveSize(imgui, UiSizeId::MidiKeyboardButtonSize);
    auto const button_ygap = LiveSize(imgui, UiSizeId::MidiKeyboardButtonYGap);

    auto root = layout::CreateItem(lay,
                                   {
                                       .size = imgui.Size(),
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                   });
    auto controls = layout::CreateItem(
        lay,
        {
            .parent = root,
            .size = {LiveSize(imgui, UiSizeId::MidiKeyboardControlWidth), imgui.Height() * 0.9f},
            .contents_direction = layout::Direction::Row,
            .contents_align = layout::Alignment::Start,
        });

    auto oct_container = layout::CreateItem(
        lay,
        {
            .parent = controls,
            .size = {LiveSize(imgui, UiSizeId::MidiKeyboardSlider) * 1.5f, layout::k_fill_parent},
            .contents_direction = layout::Direction::Column,
            .contents_align = layout::Alignment::Middle,
        });

    auto oct_up = layout::CreateItem(lay,
                                     {
                                         .parent = oct_container,
                                         .size = {layout::k_fill_parent, button_h},
                                     });
    auto oct_text = layout::CreateItem(lay,
                                       {
                                           .parent = oct_container,
                                           .size = {layout::k_fill_parent, button_h},
                                           .margins = {.tb = button_ygap},
                                       });
    auto oct_dn = layout::CreateItem(lay,
                                     {
                                         .parent = oct_container,
                                         .size = {layout::k_fill_parent, button_h},
                                     });
    auto keyboard = layout::CreateItem(lay,
                                       {
                                           .parent = root,
                                           .size = layout::k_fill_parent,
                                       });

    layout::RunContext(lay);
    DEFER { layout::ResetContext(lay); };

    Rect const keyboard_r = layout::GetRect(lay, keyboard);
    Rect const oct_up_r = layout::GetRect(lay, oct_up);
    Rect const oct_dn_r = layout::GetRect(lay, oct_dn);
    Rect const oct_text_r = layout::GetRect(lay, oct_text);

    auto up_id = imgui.GetID("Up");
    auto dn_id = imgui.GetID("Dn");
    auto& preferences = g->prefs;
    auto keyboard_octave = (int)Clamp<s64>(
        prefs::LookupInt(preferences, prefs::key::k_gui_keyboard_octave).ValueOr(k_octave_default_offset),
        k_octave_lowest,
        k_octave_highest);
    if (buttons::Button(g, up_id, oct_up_r, ICON_FA_CARET_UP, buttons::IconButton(imgui))) {
        keyboard_octave = Min(keyboard_octave + 1, k_octave_highest);
        prefs::SetValue(preferences, prefs::key::k_gui_keyboard_octave, (s64)keyboard_octave);
    }
    if (buttons::Button(g, dn_id, oct_dn_r, ICON_FA_CARET_DOWN, buttons::IconButton(imgui))) {
        keyboard_octave = Max(keyboard_octave - 1, k_octave_lowest);
        prefs::SetValue(preferences, prefs::key::k_gui_keyboard_octave, (s64)keyboard_octave);
    }
    Tooltip(g, up_id, oct_up_r, "GUI Keyboard Octave Up"_s);
    Tooltip(g, dn_id, oct_dn_r, "GUI Keyboard Octave Down"_s);

    auto oct_text_id = imgui.GetID("Oct");
    if (draggers::Dragger(g,
                          oct_text_id,
                          oct_text_r,
                          k_octave_lowest,
                          k_octave_highest,
                          keyboard_octave,
                          draggers::DefaultStyle(imgui).WithNoBackground().WithSensitivity(500))) {
        ASSERT(keyboard_octave >= k_octave_lowest && keyboard_octave <= k_octave_highest);
        prefs::SetValue(preferences, prefs::key::k_gui_keyboard_octave, (s64)keyboard_octave);
    }
    Tooltip(g, oct_text_id, oct_text_r, "GUI Keyboard Octave - Double Click To Edit"_s);

    if (auto key = KeyboardGui(g, keyboard_r, keyboard_octave)) {
        if (key->is_down)
            engine.processor.events_for_audio_thread.Push(
                GuiNoteClicked {.key = key->note, .velocity = key->velocity});
        else
            engine.processor.events_for_audio_thread.Push(GuiNoteClickReleased {.key = key->note});
        engine.host.request_process(&engine.host);
    }
}
