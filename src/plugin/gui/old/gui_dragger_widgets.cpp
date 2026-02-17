// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_dragger_widgets.hpp"

#include <IconsFontAwesome6.h>

#include "../gui_state.hpp"
#include "gui/gui_utils.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

namespace draggers {

Style DefaultStyle(imgui::Context const& imgui) {
    Style s {};
    s.sensitivity = 20;
    s.background = LiveCol(UiColMap::MidDarkSurface);
    s.text = LiveCol(UiColMap::MidText);
    s.selection_back = LiveCol(UiColMap::TextInputSelection);
    s.cursor = LiveCol(UiColMap::TextInputCursor);
    s.button_style = buttons::IconButton(imgui);
    return s;
}

Style NoteNameStyle(imgui::Context const& imgui) {
    auto s = DefaultStyle(imgui);
    s.midi_note_names = true;
    return s;
}

bool Dragger(GuiState& g, imgui::Id id, Rect r, int min, int max, int& value, Style const& style) {
    auto const format_string = [&]() {
        if (!style.midi_note_names) {
            auto const format_string = style.always_show_plus ? "{+}"_s : "{}"_s;
            return fmt::Format(g.scratch_arena, format_string, value);
        } else {
            return g.scratch_arena.Clone(NoteName(CheckedCast<u7>(value)));
        }
    };

    auto const parse_string = [&](String str) -> bool {
        if (!style.midi_note_names) {
            if (auto const o = ParseInt(str, ParseIntBase::Decimal)) {
                value = Clamp((int)o.Value(), min, max);
                return true;
            }
        } else if (auto const midi_note = MidiNoteFromName(str)) {
            value = midi_note.Value();
            return true;
        }
        return false;
    };

    auto val = (f32)value;

    auto const window_r = g.imgui.RegisterAndConvertRect(r);

    auto const original_text = format_string();

    imgui::TextInputConfig const text_input_flags = {
        .chars_decimal = !style.midi_note_names,
        .chars_note_names = style.midi_note_names,
        .tab_focuses_next_input = true,
        .centre_align = true,
        .escape_unfocuses = true,
        .select_all_when_opening = true,
    };

    auto const result = g.imgui.DraggerBehaviour({
        .rect_in_window_coords = window_r,
        .id = id,
        .text = original_text,
        .min = (f32)min,
        .max = (f32)max,
        .value = val,
        .default_value = (f32)min,
        .text_input_button_cfg =
            {
                .mouse_button = MouseButton::Left,
                .event = MouseButtonEvent::DoubleClick,
            },
        .text_input_cfg = text_input_flags,
        .slider_cfg =
            {
                .sensitivity = 15,
                .slower_with_shift = true,
                .default_on_modifer = true,
            },
    });

    bool value_changed = false;
    if (result.new_string_value) value_changed = parse_string(*result.new_string_value);
    if (result.value_changed) {
        value = (int)val;
        value_changed = true;
    }

    String text_to_render = original_text;
    f32x2 text_pos {};

    if (auto const o = result.text_input_result) {

        if (o->HasSelection()) {
            imgui::TextInputResult::SelectionIterator it {g.imgui};
            while (auto const rect = o->NextSelectionRect(it))
                g.imgui.draw_list->AddRectFilled(*rect, style.selection_back);
        }

        if (o->cursor_rect) g.imgui.draw_list->AddRectFilled(*o->cursor_rect, style.cursor);

        text_to_render = result.text_input_result->text;
        text_pos = o->text_pos;
    } else {
        if (value_changed) text_to_render = format_string();
        text_pos =
            imgui::TextInputTextPos(text_to_render, window_r, text_input_flags, g.imgui.draw_list->fonts);
    }

    g.imgui.draw_list->AddText(text_pos, style.text, text_to_render);

    return value_changed;
}

bool Dragger(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style) {
    auto id = BeginParameterGUI(g, param, r);

    auto& imgui = g.imgui;

    auto result = param.IntValue<int>();

    // draw it around the whole thing, not just the dragger
    if (style.background) {
        auto const converted_r = imgui.RegisterAndConvertRect(r);
        imgui.draw_list->AddRectFilled(converted_r, style.background, LiveSize(UiSizeId::CornerRounding));
    }

    auto const btn_w = LiveSize(UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveSize(UiSizeId::ParamIntButtonMarginR);

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
    Tooltip(g, left_id, g.imgui.ViewportRectToWindowRect(left_r), "Decrement the value"_s, {});
    Tooltip(g, right_id, g.imgui.ViewportRectToWindowRect(right_r), "Increment the value"_s, {});

    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    changed ? Optional<f32>((f32)result) : k_nullopt,
                    ParamDisplayFlagsNoValuePopup);

    MacroAddDestinationRegion(g, g.imgui.ViewportRectToWindowRect(r), param.info.index);

    return changed;
}

bool Dragger(GuiState& g, imgui::Id id, layout::Id lay_id, int min, int max, int& value, Style const& style) {
    return Dragger(g, id, layout::GetRect(g.layout, lay_id), min, max, value, style);
}
bool Dragger(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style) {
    return Dragger(g, param, layout::GetRect(g.layout, lay_id), style);
}

} // namespace draggers
