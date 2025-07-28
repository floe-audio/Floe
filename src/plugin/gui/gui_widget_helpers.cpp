// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_helpers.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_prefs.hpp"
#include "gui_window.hpp"

void StartFloeMenu(Gui* g) { g->imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Body)]); }

void EndFloeMenu(Gui* g) { g->imgui.graphics->context->PopFont(); }

f32 MaxStringLength(Gui* g, void* items, int num, String (*GetStr)(void* items, int index)) {
    return g->imgui.LargestStringWidth(0, items, num, GetStr);
}

f32 MaxStringLength(Gui* g, Span<String const> strs) { return g->imgui.LargestStringWidth(0, strs); }

f32 MenuItemWidth(Gui* g, void* items, int num, String (*GetStr)(void* items, int index)) {
    return MaxStringLength(g, items, num, GetStr) + LiveSize(g->imgui, UiSizeId::MenuItemPadX);
}
f32 MenuItemWidth(Gui* g, Span<String const> strs) {
    return MaxStringLength(g, strs) + LiveSize(g->imgui, UiSizeId::MenuItemPadX);
}

//
//
//

void DoTooltipText(Gui* g, String str, Rect r, bool rect_is_window_pos) {
    g->imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { g->imgui.graphics->context->PopFont(); };

    auto& imgui = g->imgui;
    auto font = imgui.overlay_graphics.context->CurrentFont();
    auto size = LiveSize(imgui, UiSizeId::TooltipMaxWidth);
    auto pad_x = LiveSize(imgui, UiSizeId::TooltipPadX);
    auto pad_y = LiveSize(imgui, UiSizeId::TooltipPadY);

    auto wrapped_size = draw::GetTextSize(font, str, size);
    size = Min(size, wrapped_size.x);

    auto abs_pos = !rect_is_window_pos ? imgui.WindowPosToScreenPos(r.pos) : r.pos;

    Rect popup_r;
    popup_r.x = abs_pos.x;
    popup_r.y = abs_pos.y + r.h;
    popup_r.w = size + pad_x * 2;
    popup_r.h = wrapped_size.y + pad_y * 2;

    popup_r.x = popup_r.x + ((r.w / 2) - (popup_r.w / 2));

    popup_r.pos = imgui::BestPopupPos(popup_r,
                                      {.pos = abs_pos, .size = r.size},
                                      g->frame_input.window_size.ToFloat2(),
                                      false);

    f32x2 text_start;
    text_start.x = popup_r.x + pad_x;
    text_start.y = popup_r.y + pad_y;

    draw::DropShadow(imgui, popup_r);
    imgui.overlay_graphics.AddRectFilled(popup_r.Min(),
                                         popup_r.Max(),
                                         LiveCol(imgui, UiColMap::TooltipBack),
                                         LiveSize(imgui, UiSizeId::CornerRounding));
    imgui.overlay_graphics
        .AddText(font, font->font_size, text_start, LiveCol(imgui, UiColMap::TooltipText), str, size + 1);
}

bool Tooltip(Gui* g, imgui::Id id, Rect r, String str, bool rect_is_window_pos) {
    if (!prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::ShowTooltips))) return false;

    auto& imgui = g->imgui;
    f64 const delay {0.5};
    if (imgui.WasJustMadeHot(id)) imgui.AddTimedWakeup(g->frame_input.current_time + delay, "Tooltip");
    auto hot_seconds = imgui.SecondsSpentHot();
    if (imgui.IsHot(id) && hot_seconds >= delay) {
        DoTooltipText(g, str, r, rect_is_window_pos);
        return true;
    }
    return false;
}

void ParameterValuePopup(Gui* g, DescribedParamValue const& param, imgui::Id id, Rect r) {
    auto param_ptr = &param;
    ParameterValuePopup(g, {&param_ptr, 1}, id, r);
}

void ParameterValuePopup(Gui* g, Span<DescribedParamValue const*> params, imgui::Id id, Rect r) {
    auto& imgui = g->imgui;

    bool cc_just_moved_param = false;
    for (auto param : params) {
        if (CcControllerMovedParamRecently(g->engine.processor, param->info.index)) {
            cc_just_moved_param = true;
            break;
        }
    }
    if (cc_just_moved_param) imgui.WakeupAtTimedInterval(g->redraw_counter, 0.04);

    if (imgui.IsActive(id) || cc_just_moved_param) {
        if (params.size == 1) {
            auto const str = params[0]->info.LinearValueToString(params[0]->LinearValue());
            ASSERT(str);

            DoTooltipText(g, str.Value(), r);
        } else {
            DynamicArray<char> buf {g->scratch_arena};
            for (auto param : params) {
                auto const str = param->info.LinearValueToString(param->LinearValue());
                ASSERT(str.HasValue());

                fmt::Append(buf, "{}: {}", param->info.gui_label, str.Value());
                if (param != Last(params)) dyn::Append(buf, '\n');
            }
            DoTooltipText(g, buf, r);
        }
    }
}

void MidiLearnMenu(Gui* g, ParamIndex param, Rect r) { MidiLearnMenu(g, {&param, 1}, r); }

// TODO: replace this with the much nicer version in gui2_parameter_component.cpp.
void MidiLearnMenu(Gui* g, Span<ParamIndex> params, Rect r) {
    auto& imgui = g->imgui;
    auto& engine = g->engine;
    imgui.PushID((uintptr)params[0]);
    auto popup_id = imgui.GetID("MidiLearnPopup");
    auto right_clicker_id = imgui.GetID("MidiLearnClicker");
    imgui.PopID();

    imgui.RegisterAndConvertRect(&r);
    imgui.PopupButtonBehavior(r,
                              right_clicker_id,
                              popup_id,
                              {.right_mouse = true, .triggers_on_mouse_up = true});

    if (!imgui.IsPopupOpen(popup_id)) return;

    auto item_height = imgui.graphics->context->CurrentFontSize() * 1.5f;
    static constexpr String k_reset_text = "Set To Default Value";
    static constexpr String k_enter_text = "Enter Value";
    static constexpr String k_learn_text = "MIDI CC Learn";
    static constexpr String k_cancel_text = "Cancel MIDI CC Learn";
    static constexpr String k_remove_fmt = "Remove MIDI CC {}";
    static constexpr String k_always_set_fmt = "Always set MIDI CC {} to this when Floe opens";

    f32 item_width = 0;
    int num_items = 0;

    for (auto param : params) {
        auto check_max_size = [&](String str) { item_width = Max(item_width, MenuItemWidth(g, {&str, 1})); };

        check_max_size(k_reset_text);
        if (k_param_descriptors[ToInt(param)].value_type == ParamValueType::Float)
            check_max_size(k_enter_text);
        if (IsMidiCCLearnActive(engine.processor))
            check_max_size(k_cancel_text);
        else
            check_max_size(k_learn_text);

        auto const persistent_ccs = PersistentCcsForParam(g->prefs, ParamIndexToId(param));

        auto param_ccs = GetLearnedCCsBitsetForParam(engine.processor, param);
        auto const num_ccs_for_param = (int)param_ccs.NumSet();
        num_items += num_ccs_for_param == 0 ? 1 : num_ccs_for_param + 2;
        for (auto const cc_num : Range(128uz)) {
            if (!param_ccs.Get(cc_num)) continue;

            check_max_size(fmt::Format(g->scratch_arena, k_remove_fmt, cc_num));

            if (!persistent_ccs.Get(cc_num))
                check_max_size(fmt::Format(g->scratch_arena, k_always_set_fmt, cc_num));
        }

        for (auto const cc_num : Range(128uz))
            if (persistent_ccs.Get(cc_num))
                check_max_size(fmt::Format(g->scratch_arena, k_always_set_fmt, cc_num));
    }

    auto centred_x = r.x + ((r.w / 2) - (item_width / 2));

    auto popup_pos = imgui::BestPopupPos(
        Rect {.x = centred_x, .y = r.y, .w = item_width, .h = item_height * (f32)num_items},
        r,
        g->frame_input.window_size.ToFloat2(),
        false);
    Rect const popup_r {.pos = popup_pos};

    auto settings = PopupWindowSettings(imgui);
    settings.flags =
        imgui::WindowFlags_AutoWidth | imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoPosition;
    if (imgui.BeginWindowPopup(settings, popup_id, popup_r)) {
        StartFloeMenu(g);
        DEFER { EndFloeMenu(g); };
        f32 pos = 0;

        for (auto param : params) {
            imgui.PushID((uintptr)param);
            DEFER { imgui.PopID(); };

            if (params.size != 1) {
                labels::Label(
                    g,
                    {.xywh {0, pos, item_width, item_height}},
                    fmt::Format(g->scratch_arena, "{}: ", k_param_descriptors[ToInt(param)].gui_label),
                    labels::FakeMenuItem(imgui));
                pos += item_height;
            }

            {
                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    k_reset_text,
                                    buttons::MenuItem(imgui, false))) {
                    SetParameterValue(engine.processor,
                                      param,
                                      k_param_descriptors[ToInt(param)].default_linear_value,
                                      {});
                    imgui.ClosePopupToLevel(0);
                }
                pos += item_height;
            }

            if (k_param_descriptors[ToInt(param)].value_type == ParamValueType::Float) {
                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    k_enter_text,
                                    buttons::MenuItem(imgui, false))) {
                    imgui.ClosePopupToLevel(0);
                    g->param_text_editor_to_open = param;
                }
                pos += item_height;
            }

            if (IsMidiCCLearnActive(engine.processor)) {
                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    k_cancel_text,
                                    buttons::MenuItem(imgui, false))) {
                    CancelMidiCCLearn(engine.processor);
                }
                pos += item_height;
            } else {
                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    k_learn_text,
                                    buttons::MenuItem(imgui, false))) {
                    LearnMidiCC(engine.processor, param);
                }
                pos += item_height;
            }

            auto const persistent_ccs = PersistentCcsForParam(g->prefs, ParamIndexToId(param));

            auto ccs_bitset = GetLearnedCCsBitsetForParam(engine.processor, param);
            bool closes_popups = false;
            if (ccs_bitset.AnyValuesSet()) closes_popups = true;
            for (auto const cc_num : Range(128uz)) {
                if (!ccs_bitset.Get(cc_num)) continue;
                imgui.PushID((u64)cc_num);

                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    fmt::Format(g->scratch_arena, k_remove_fmt, cc_num),
                                    buttons::MenuItem(imgui, closes_popups))) {
                    UnlearnMidiCC(engine.processor, param, (u7)cc_num);
                }
                pos += item_height;

                if (!persistent_ccs.Get(cc_num)) {
                    bool state = false;
                    if (buttons::Toggle(g,
                                        {.xywh {0, pos, item_width, item_height}},
                                        state,
                                        fmt::Format(g->scratch_arena, k_always_set_fmt, cc_num),
                                        buttons::MenuItem(imgui, closes_popups))) {
                        AddPersistentCcToParamMapping(g->prefs, (u8)cc_num, ParamIndexToId(param));
                    }
                    pos += item_height;
                }

                imgui.PopID();
            }

            imgui.PushID("always_set");
            for (auto const cc_num : Range((u8)128)) {
                if (persistent_ccs.Get(cc_num)) {
                    imgui.PushID(cc_num);

                    bool state = true;
                    if (buttons::Toggle(g,
                                        {.xywh {0, pos, item_width, item_height}},
                                        state,
                                        fmt::Format(g->scratch_arena, k_always_set_fmt, cc_num),
                                        buttons::MenuItem(imgui, closes_popups))) {
                        RemovePersistentCcToParamMapping(g->prefs, cc_num, ParamIndexToId(param));
                    }
                    pos += item_height;

                    imgui.PopID();
                }
            }
            imgui.PopID();

            if (params.size != 1 && param != Last(params)) {
                auto const div_gap_x = LiveSize(imgui, UiSizeId::MenuItemDividerGapX);
                auto const div_h = LiveSize(imgui, UiSizeId::MenuItemDividerH);

                Rect div_r = {.xywh {div_gap_x, pos + (div_h / 2), item_width - (2 * div_gap_x), 1}};
                imgui.RegisterAndConvertRect(&div_r);
                imgui.graphics->AddRectFilled(div_r.Min(),
                                              div_r.Max(),
                                              LiveCol(imgui, UiColMap::PopupItemDivider));
                pos += div_h;
            }
        }
        imgui.EndWindow();
    }
}

bool DoMultipleMenuItems(Gui* g,
                         void* items,
                         int num_items,
                         int& current,
                         String (*GetStr)(void* items, int index)) {
    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    auto w = MenuItemWidth(g, items, num_items, GetStr);
    auto h = LiveSize(g->imgui, UiSizeId::MenuItemHeight);

    int clicked = -1;
    for (auto const i : Range(num_items)) {
        bool state = i == current;
        if (buttons::Toggle(g,
                            g->imgui.GetID((uintptr)i),
                            {.xywh {0, h * (f32)i, w, h}},
                            state,
                            GetStr(items, i),
                            buttons::MenuItem(g->imgui, true)))
            clicked = i;
    }
    if (clicked != -1 && current != clicked) {
        current = clicked;
        return true;
    }
    return false;
}

bool DoMultipleMenuItems(Gui* g, Span<String const> items, int& current) {
    auto str_get = [](void* items, int index) {
        auto strs = *(Span<String>*)items;
        return strs[(usize)index];
    };
    return DoMultipleMenuItems(g, (void*)&items, (int)items.size, current, str_get);
}

void DoParameterTooltipIfNeeded(Gui* g,
                                DescribedParamValue const& param,
                                imgui::Id imgui_id,
                                Rect param_rect) {
    auto param_ptr = &param;
    DoParameterTooltipIfNeeded(g, {&param_ptr, 1}, imgui_id, param_rect);
}

void DoParameterTooltipIfNeeded(Gui* g,
                                Span<DescribedParamValue const*> params,
                                imgui::Id imgui_id,
                                Rect param_rect) {
    DynamicArray<char> buf {g->scratch_arena};
    for (auto param : params) {
        auto const str = param->info.LinearValueToString(param->LinearValue());
        ASSERT(str);

        fmt::Append(buf, "{}: {}\n{}", param->info.name, str.Value(), param->info.tooltip);

        if (param->info.value_type == ParamValueType::Int)
            fmt::Append(buf, ". Drag to edit or double-click to type a value");

        if (params.size != 1 && param != Last(params)) fmt::Append(buf, "\n\n");
    }
    Tooltip(g, imgui_id, param_rect, buf);
}

imgui::Id BeginParameterGUI(Gui* g, DescribedParamValue const& param, Rect r, Optional<imgui::Id> id) {
    if (!(param.info.flags.not_automatable)) MidiLearnMenu(g, (ParamIndex)param.info.index, r);
    return id ? *id : g->imgui.GetID((u64)param.info.id);
}

void EndParameterGUI(Gui* g,
                     imgui::Id id,
                     DescribedParamValue const& param,
                     Rect r,
                     Optional<f32> new_val,
                     ParamDisplayFlags flags) {
    if (g->imgui.WasJustActivated(id)) ParameterJustStartedMoving(g->engine.processor, param.info.index);
    if (new_val) SetParameterValue(g->engine.processor, param.info.index, *new_val, {});
    if (g->imgui.WasJustDeactivated(id)) ParameterJustStoppedMoving(g->engine.processor, param.info.index);

    if (!(flags & ParamDisplayFlagsNoTooltip) && !g->imgui.TextInputHasFocus(id))
        DoParameterTooltipIfNeeded(g, param, id, r);
    if (!(flags & ParamDisplayFlagsNoValuePopup) && param.info.value_type == ParamValueType::Float)
        ParameterValuePopup(g, param, id, r);
}

bool DoOverlayClickableBackground(Gui* g) {
    bool clicked = false;
    auto& imgui = g->imgui;
    auto invis_sets = FloeWindowSettings(imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::SidePanelOverlay));
    });
    imgui.BeginWindow(invis_sets, {.xywh {0, 0, imgui.Width(), imgui.Height()}}, "invisible");
    auto invis_window = imgui.CurrentWindow();

    if (imgui.IsWindowHovered(invis_window)) {
        imgui.frame_output.cursor_type = CursorType::Hand;
        if (imgui.frame_input.Mouse(MouseButton::Left).presses.size) clicked = true;
    }

    imgui.EndWindow();
    return clicked;
}

imgui::TextInputSettings GetParameterTextInputSettings() {
    imgui::TextInputSettings settings = imgui::DefTextInputDraggerInt().text_input_settings;
    settings.text_flags = {.centre_align = true};
    settings.draw = [](IMGUI_DRAW_TEXT_INPUT_ARGS) {
        if (!imgui.TextInputHasFocus(id)) return;

        auto const text_pos = result->GetTextPos();
        auto const w = Max(r.w, draw::GetTextWidth(imgui.graphics->context->CurrentFont(), text));
        Rect const background_r {
            .xywh {r.CentreX() - (w / 2), text_pos.y, w, imgui.graphics->context->CurrentFontSize()}};
        auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);

        imgui.graphics->AddRectFilled(background_r.Min(),
                                      background_r.Max(),
                                      LiveCol(imgui, UiColMap::KnobTextInputBack),
                                      rounding);
        imgui.graphics->AddRect(background_r.Min(),
                                background_r.Max(),
                                LiveCol(imgui, UiColMap::KnobTextInputBorder),
                                rounding);

        if (result->HasSelection()) {
            imgui::TextInputResult::SelectionIterator it {.draw_ctx = *imgui.graphics->context};
            while (auto rect = result->NextSelectionRect(it))
                imgui.graphics->AddRectFilled(*rect, LiveCol(imgui, UiColMap::TextInputSelection));
        }

        if (result->show_cursor) {
            auto cursor_r = result->GetCursorRect();
            imgui.graphics->AddRectFilled(cursor_r.Min(),
                                          cursor_r.Max(),
                                          LiveCol(imgui, UiColMap::TextInputCursor));
        }

        imgui.graphics->AddText(text_pos, LiveCol(imgui, UiColMap::TextInputText), text);
    };

    return settings;
}

void HandleShowingTextEditorForParams(Gui* g, Rect r, Span<ParamIndex const> params) {
    if (g->param_text_editor_to_open) {
        for (auto const p : params) {
            if (p == *g->param_text_editor_to_open) {
                auto const id = g->imgui.GetID("text input");

                auto const p_obj = g->engine.processor.main_params.DescribedValue(p);
                auto const str = p_obj.info.LinearValueToString(p_obj.LinearValue());
                ASSERT(str.HasValue());

                g->imgui.SetTextInputFocus(id, *str, false);
                auto const text_input = g->imgui.TextInput(GetParameterTextInputSettings(), r, id, *str);

                if (text_input.enter_pressed || g->imgui.TextInputJustUnfocused(id)) {
                    if (auto val = p_obj.info.StringToLinearValue(text_input.text)) {
                        SetParameterValue(g->engine.processor, p, *val, {});
                        g->imgui.frame_output.ElevateUpdateRequest(
                            GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
                    }
                    g->param_text_editor_to_open.Clear();
                }
                break;
            }
        }
    }
}
