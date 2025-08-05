// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_dragger_widgets.hpp"

#include <IconsFontAwesome6.h>

#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

namespace draggers {

Style DefaultStyle(imgui::Context const& imgui) {
    Style s {};
    s.sensitivity = 20;
    s.background = LiveCol(imgui, UiColMap::Dragger1Back);
    s.text = LiveCol(imgui, UiColMap::TextInputText);
    s.selection_back = LiveCol(imgui, UiColMap::TextInputSelection);
    s.cursor = LiveCol(imgui, UiColMap::TextInputCursor);
    s.button_style = buttons::IconButton(imgui);
    return s;
}

Style NoteNameStyle(imgui::Context const& imgui) {
    auto s = DefaultStyle(imgui);
    s.midi_note_names = true;
    return s;
}

bool Dragger(Gui* g, imgui::Id id, Rect r, int min, int max, int& value, Style const& style) {
    auto settings = imgui::DefTextInputDraggerInt();
    settings.slider_settings.flags = {.slower_with_shift = true, .default_on_modifer = true};
    settings.slider_settings.sensitivity = 15;
    settings.format = style.always_show_plus ? "{+}"_s : "{}"_s;
    settings.slider_settings.draw = [](IMGUI_DRAW_SLIDER_ARGS) {};
    settings.text_input_settings.draw = [&style](IMGUI_DRAW_TEXT_INPUT_ARGS) {
        if (result->HasSelection()) {
            imgui::TextInputResult::SelectionIterator it {.draw_ctx = *imgui.graphics->context};
            while (auto rect = result->NextSelectionRect(it))
                imgui.graphics->AddRectFilled(*rect, style.selection_back);
        }

        if (result->show_cursor) {
            auto cursor_r = result->GetCursorRect();
            imgui.graphics->AddRectFilled(cursor_r.Min(), cursor_r.Max(), style.cursor);
        }

        imgui.graphics->AddText(result->GetTextPos(), style.text, text);
    };
    settings.text_input_settings.text_flags.centre_align = true;

    if (!style.midi_note_names) {
        return g->imgui.TextInputDraggerInt(settings, r, id, min, max, value);
    } else {
        settings.text_input_settings.text_flags.chars_decimal = false;
        settings.text_input_settings.text_flags.chars_note_names = true;
        auto val = (f32)value;
        auto result = g->imgui.TextInputDraggerCustom(settings,
                                                      r,
                                                      id,
                                                      NoteName(CheckedCast<u7>(value)),
                                                      (f32)min,
                                                      (f32)max,
                                                      val,
                                                      (f32)0);
        if (result.new_string_value) {
            if (auto const midi_note = MidiNoteFromName(*result.new_string_value)) {
                val = (f32)midi_note.Value();
                result.value_changed = true;
            }
        }

        if (result.value_changed) value = (int)val;
        return result.value_changed;
    }
}

bool Dragger(Gui* g, DescribedParamValue const& param, Rect r, Style const& style) {
    auto id = BeginParameterGUI(g, param, r);

    auto& imgui = g->imgui;

    auto result = param.IntValue<int>();

    // draw it around the whole thing, not just the dragger
    if (style.background) {
        auto const converted_r = imgui.GetRegisteredAndConvertedRect(r);
        imgui.graphics->AddRectFilled(converted_r.Min(),
                                      converted_r.Max(),
                                      style.background,
                                      LiveSize(imgui, UiSizeId::CornerRounding));
    }

    auto const btn_w = LiveSize(imgui, UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveSize(g->imgui, UiSizeId::ParamIntButtonMarginR);

    rect_cut::CutRight(r, margin_r);
    auto right_r = rect_cut::CutRight(r, btn_w);
    auto left_r = rect_cut::CutRight(r, btn_w);

    bool changed =
        Dragger(g, id, r, (int)param.info.linear_range.min, (int)param.info.linear_range.max, result, style);

    auto const left_id = id - 4;
    auto const right_id = id + 4;
    if (buttons::Button(g, left_id, left_r, ICON_FA_CARET_LEFT, style.button_style)) {
        result = Max((int)param.info.linear_range.min, result - 1);
        changed = true;
    }
    if (buttons::Button(g, right_id, right_r, ICON_FA_CARET_RIGHT, style.button_style)) {
        result = Min((int)param.info.linear_range.max, result + 1);
        changed = true;
    }
    Tooltip(g, left_id, left_r, "Decrement the value"_s);
    Tooltip(g, right_id, right_r, "Increment the value"_s);

    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    changed ? Optional<f32>((f32)result) : k_nullopt,
                    ParamDisplayFlagsNoValuePopup);

    MacroAddDestinationRegion(g, r, param.info.index);

    return changed;
}

bool Dragger(Gui* g, imgui::Id id, layout::Id lay_id, int min, int max, int& value, Style const& style) {
    return Dragger(g, id, layout::GetRect(g->layout, lay_id), min, max, value, style);
}
bool Dragger(Gui* g, DescribedParamValue const& param, layout::Id lay_id, Style const& style) {
    return Dragger(g, param, layout::GetRect(g->layout, lay_id), style);
}

} // namespace draggers
