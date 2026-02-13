// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_helpers.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "../gui_drawing_helpers.hpp"
#include "../gui_prefs.hpp"
#include "../gui_state.hpp"
#include "../gui_viewport_utils.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"

// IMPORTANT: This file is considered technical debt. It's due to be superseded by new code that uses better
// techniques for the same result by leaning into the GuiBuilder system amongst other things.
// The problems with this file:
// - Messy functions
// - Many function arguments rather an options structs with designated initialiser syntax
// - Overuse of function default arguments
// - Use of monolithic 'GuiState' struct rather than specific arguments

void StartFloeMenu(GuiState& g) { g.fonts.Push(ToInt(FontType::Body)); }

void EndFloeMenu(GuiState& g) { g.fonts.Pop(); }

f32 MaxStringLength(GuiState& g, Span<String const> strs) {
    return g.imgui.draw_list->fonts.Current()->LargestStringWidth(0, strs);
}

static f32 MenuItemWidth(GuiState& g, void* items, int num, String (*GetStr)(void* items, int index)) {
    auto const w = g.imgui.draw_list->fonts.Current()->LargestStringWidth(0, items, num, GetStr);
    return w + LiveSize(UiSizeId::MenuItemPadX);
}

f32 MenuItemWidth(GuiState& g, Span<String const> strs) {
    return MaxStringLength(g, strs) + LiveSize(UiSizeId::MenuItemPadX);
}

//
//
//

bool Tooltip(GuiState& g, imgui::Id id, Rect window_r, String str, TooltipOptions const& options) {
    if (!options.ignore_show_tooltips_preference &&
        !prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::ShowTooltips)))
        return false;

    constexpr auto k_delay_secs = 0.5;

    if (g.imgui.WasJustMadeHot(id))
        GuiIo().out.AddTimedWakeup(GuiIo().in.current_time + k_delay_secs, "Tooltip");

    if (g.imgui.IsHot(id) && g.imgui.SecondsSpentHot() >= k_delay_secs) {
        DrawOverlayTooltipForRect(g.imgui, g.fonts, str, window_r);
        return true;
    }

    return false;
}

void ParameterValuePopup(GuiState& g, DescribedParamValue const& param, imgui::Id id, Rect window_r) {
    auto param_ptr = &param;
    ParameterValuePopup(g, {&param_ptr, 1}, id, window_r);
}

void ParameterValuePopup(GuiState& g, Span<DescribedParamValue const*> params, imgui::Id id, Rect window_r) {
    auto& imgui = g.imgui;

    if (imgui.IsActive(id)) {
        if (params.size == 1) {
            auto const str = params[0]->info.LinearValueToString(params[0]->LinearValue());
            ASSERT(str);

            DrawOverlayTooltipForRect(g.imgui, g.fonts, str.Value(), window_r);
        } else {
            DynamicArray<char> buf {g.scratch_arena};
            for (auto param : params) {
                auto const str = param->info.LinearValueToString(param->LinearValue());
                ASSERT(str.HasValue());

                fmt::Append(buf, "{}: {}", param->info.gui_label, str.Value());
                if (param != Last(params)) dyn::Append(buf, '\n');
            }
            DrawOverlayTooltipForRect(g.imgui, g.fonts, buf, window_r);
        }
    }
}

void MidiLearnMenu(GuiState& g, ParamIndex param, Rect r) { MidiLearnMenu(g, {&param, 1}, r); }

// TODO: replace this with the much nicer version in gui2_parameter_component.cpp.
void MidiLearnMenu(GuiState& g, Span<ParamIndex> params, Rect r) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    imgui.PushId((uintptr)params[0]);
    auto popup_id = imgui.MakeId("MidiLearnPopup");
    auto right_clicker_id = imgui.MakeId("MidiLearnClicker");
    imgui.PopId();

    r = imgui.RegisterAndConvertRect(r);
    imgui.PopupMenuButtonBehaviour(r,
                                   right_clicker_id,
                                   popup_id,
                                   {.mouse_button = MouseButton::Right, .event = MouseButtonEvent::Up});

    if (!imgui.IsPopupMenuOpen(popup_id)) return;

    auto item_height = g.fonts.Current()->font_size * 1.5f;
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

        auto const persistent_ccs = PersistentCcsForParam(g.prefs, ParamIndexToId(param));

        auto param_ccs = GetLearnedCCsBitsetForParam(engine.processor, param);
        auto const num_ccs_for_param = (int)param_ccs.NumSet();
        num_items += num_ccs_for_param == 0 ? 1 : num_ccs_for_param + 2;
        for (auto const cc_num : Range(128uz)) {
            if (!param_ccs.Get(cc_num)) continue;

            check_max_size(fmt::Format(g.scratch_arena, k_remove_fmt, cc_num));

            if (!persistent_ccs.Get(cc_num))
                check_max_size(fmt::Format(g.scratch_arena, k_always_set_fmt, cc_num));
        }

        for (auto const cc_num : Range(128uz))
            if (persistent_ccs.Get(cc_num))
                check_max_size(fmt::Format(g.scratch_arena, k_always_set_fmt, cc_num));
    }

    auto centred_x = r.x + ((r.w / 2) - (item_width / 2));

    auto popup_pos = imgui::BestPopupPos(
        Rect {.x = centred_x, .y = r.y, .w = item_width, .h = item_height * (f32)num_items},
        r,
        GuiIo().in.window_size.ToFloat2(),
        false);
    Rect const popup_r {.pos = popup_pos};

    if (imgui.IsPopupMenuOpen(popup_id)) {
        imgui.BeginViewport(FloeMenuConfig(g.imgui), popup_id, popup_r, __FUNCTION__);
        DEFER { imgui.EndViewport(); };

        StartFloeMenu(g);
        DEFER { EndFloeMenu(g); };
        f32 pos = 0;

        for (auto param : params) {
            imgui.PushId((uintptr)param);
            DEFER { imgui.PopId(); };

            if (params.size != 1) {
                labels::Label(
                    g,
                    {.xywh {0, pos, item_width, item_height}},
                    fmt::Format(g.scratch_arena, "{}: ", k_param_descriptors[ToInt(param)].gui_label),
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
                    g.param_text_editor_to_open = param;
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

            auto const persistent_ccs = PersistentCcsForParam(g.prefs, ParamIndexToId(param));

            auto ccs_bitset = GetLearnedCCsBitsetForParam(engine.processor, param);
            bool closes_popups = false;
            if (ccs_bitset.AnyValuesSet()) closes_popups = true;
            for (auto const cc_num : Range(128uz)) {
                if (!ccs_bitset.Get(cc_num)) continue;
                imgui.PushId((u64)cc_num);

                if (buttons::Button(g,
                                    {.xywh {0, pos, item_width, item_height}},
                                    fmt::Format(g.scratch_arena, k_remove_fmt, cc_num),
                                    buttons::MenuItem(imgui, closes_popups))) {
                    UnlearnMidiCC(engine.processor, param, (u7)cc_num);
                }
                pos += item_height;

                if (!persistent_ccs.Get(cc_num)) {
                    bool state = false;
                    if (buttons::Toggle(g,
                                        {.xywh {0, pos, item_width, item_height}},
                                        state,
                                        fmt::Format(g.scratch_arena, k_always_set_fmt, cc_num),
                                        buttons::MenuItem(imgui, closes_popups))) {
                        AddPersistentCcToParamMapping(g.prefs, (u8)cc_num, ParamIndexToId(param));
                    }
                    pos += item_height;
                }

                imgui.PopId();
            }

            imgui.PushId("always_set");
            for (auto const cc_num : Range((u8)128)) {
                if (persistent_ccs.Get(cc_num)) {
                    imgui.PushId(cc_num);

                    bool state = true;
                    if (buttons::Toggle(g,
                                        {.xywh {0, pos, item_width, item_height}},
                                        state,
                                        fmt::Format(g.scratch_arena, k_always_set_fmt, cc_num),
                                        buttons::MenuItem(imgui, closes_popups))) {
                        RemovePersistentCcToParamMapping(g.prefs, cc_num, ParamIndexToId(param));
                    }
                    pos += item_height;

                    imgui.PopId();
                }
            }
            imgui.PopId();

            if (params.size != 1 && param != Last(params)) {
                auto const div_gap_x = LiveSize(UiSizeId::MenuItemDividerGapX);
                auto const div_h = LiveSize(UiSizeId::MenuItemDividerH);

                Rect div_r = {.xywh {div_gap_x, pos + (div_h / 2), item_width - (2 * div_gap_x), 1}};
                div_r = imgui.RegisterAndConvertRect(div_r);
                imgui.draw_list->AddRectFilled(div_r, LiveCol(UiColMap::PopupItemDivider));
                pos += div_h;
            }
        }
    }
}

bool DoMultipleMenuItems(GuiState& g,
                         void* items,
                         int num_items,
                         int& current,
                         String (*GetStr)(void* items, int index)) {
    StartFloeMenu(g);
    DEFER { EndFloeMenu(g); };

    auto w = MenuItemWidth(g, items, num_items, GetStr);
    auto h = LiveSize(UiSizeId::MenuItemHeight);

    int clicked = -1;
    for (auto const i : Range(num_items)) {
        bool state = i == current;
        if (buttons::Toggle(g,
                            g.imgui.MakeId((uintptr)i),
                            {.xywh {0, h * (f32)i, w, h}},
                            state,
                            GetStr(items, i),
                            buttons::MenuItem(g.imgui, true)))
            clicked = i;
    }
    if (clicked != -1 && current != clicked) {
        current = clicked;
        return true;
    }
    return false;
}

bool DoMultipleMenuItems(GuiState& g, Span<String const> items, int& current) {
    auto str_get = [](void* items, int index) {
        auto strs = *(Span<String>*)items;
        return strs[(usize)index];
    };
    return DoMultipleMenuItems(g, (void*)&items, (int)items.size, current, str_get);
}

void DoParameterTooltipIfNeeded(GuiState& g,
                                DescribedParamValue const& param,
                                imgui::Id imgui_id,
                                Rect param_rect_in_window_coords) {
    auto param_ptr = &param;
    DoParameterTooltipIfNeeded(g, {&param_ptr, 1}, imgui_id, param_rect_in_window_coords);
}

void DoParameterTooltipIfNeeded(GuiState& g,
                                Span<DescribedParamValue const*> params,
                                imgui::Id imgui_id,
                                Rect param_rect_in_window_coords) {
    DynamicArray<char> buf {g.scratch_arena};
    for (auto param : params) {
        auto const str = param->info.LinearValueToString(param->LinearValue());
        ASSERT(str);

        fmt::Append(buf, "{}: {}\n{}", param->info.name, str.Value(), param->info.tooltip);

        if (param->info.value_type == ParamValueType::Int)
            fmt::Append(buf, ". Drag to edit or double-click to type a value");

        if (params.size != 1 && param != Last(params)) fmt::Append(buf, "\n\n");
    }
    Tooltip(g, imgui_id, param_rect_in_window_coords, buf, {});
}

imgui::Id BeginParameterGUI(GuiState& g, DescribedParamValue const& param, Rect r, Optional<imgui::Id> id) {
    if (!(param.info.flags.not_automatable)) MidiLearnMenu(g, (ParamIndex)param.info.index, r);
    return id ? *id : g.imgui.MakeId((u64)param.info.id);
}

void EndParameterGUI(GuiState& g,
                     imgui::Id id,
                     DescribedParamValue const& param,
                     Rect viewport_r,
                     Optional<f32> new_val,
                     ParamDisplayFlags flags) {
    if (g.imgui.WasJustActivated(id)) ParameterJustStartedMoving(g.engine.processor, param.info.index);
    if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});
    if (g.imgui.WasJustDeactivated(id)) ParameterJustStoppedMoving(g.engine.processor, param.info.index);

    if (!(flags & ParamDisplayFlagsNoTooltip) && !g.imgui.TextInputHasFocus(id))
        DoParameterTooltipIfNeeded(g, param, id, g.imgui.ViewportRectToWindowRect(viewport_r));
    if (!(flags & ParamDisplayFlagsNoValuePopup) && param.info.value_type == ParamValueType::Float)
        ParameterValuePopup(g, param, id, g.imgui.ViewportRectToWindowRect(viewport_r));
}

bool DoOverlayClickableBackground(GuiState& g) {
    bool clicked = false;
    auto& imgui = g.imgui;
    imgui.BeginViewport(
        {
            .draw_background =
                [](imgui::Context const& imgui) {
                    auto r = imgui.curr_viewport->unpadded_bounds;
                    imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::SidePanelOverlay));
                },
            .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
        },
        {.xywh {0, 0, imgui.CurrentVpWidth(), imgui.CurrentVpHeight()}},
        "invisible");
    DEFER { imgui.EndViewport(); };

    if (imgui.IsViewportHovered(imgui.curr_viewport)) {
        GuiIo().out.wants.cursor_type = CursorType::Hand;
        if (GuiIo().in.Mouse(MouseButton::Left).presses.size) clicked = true;
    }

    return clicked;
}

void HandleShowingTextEditorForParams(GuiState& g, Rect r, Span<ParamIndex const> params) {
    if (g.param_text_editor_to_open) {
        for (auto const p : params) {
            if (p == *g.param_text_editor_to_open) {
                auto const id = g.imgui.MakeId("text input");

                auto const p_obj = g.engine.processor.main_params.DescribedValue(p);
                auto const str = p_obj.info.LinearValueToString(p_obj.LinearValue());
                ASSERT(str.HasValue());

                g.imgui.SetTextInputFocus(id, *str, false);

                auto const text_input = ({
                    auto const input_r = g.imgui.RegisterAndConvertRect(r);
                    auto const o = g.imgui.TextInputBehaviour({
                        .rect_in_window_coords = input_r,
                        .id = id,
                        .text = *str,
                        .input_cfg = k_param_text_input_flags,
                        .button_cfg = k_param_text_input_button_flags,
                    });
                    DrawParameterTextInput(g.imgui, input_r, o);
                    o;
                });

                if (text_input.enter_pressed || g.imgui.TextInputJustUnfocused(id)) {
                    if (auto val = p_obj.info.StringToLinearValue(text_input.text)) {
                        SetParameterValue(g.engine.processor, p, *val, {});
                        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                    }
                    g.param_text_editor_to_open.Clear();
                }
                break;
            }
        }
    }
}

bool DoBasicTextButton(imgui::Context& imgui, imgui::ButtonConfig flags, Rect r, imgui::Id id, String str) {
    r = imgui.RegisterAndConvertRect(r);
    bool const clicked = imgui.ButtonBehaviour(r, id, flags);

    u32 col = 0xffd5d5d5;
    if (imgui.IsHot(id)) col = 0xfff0f0f0;
    if (imgui.IsActive(id)) col = 0xff808080;
    imgui.draw_list->AddRectFilled(r.Min(), r.Max(), col);

    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    auto const pad = (f32)GuiIo().in.window_size.width / 200.0f;
    imgui.draw_list->AddText(f32x2 {r.x + pad, r.y + (r.h / 2 - font_size / 2)}, 0xff000000, str);

    return clicked;
}

void DoBasicWhiteText(imgui::Context& imgui, Rect r, String str) {
    r = imgui.RegisterAndConvertRect(r);
    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    f32x2 pos;
    pos.x = (f32)(int)r.x;
    pos.y = r.y + ((r.h / 2) - (font_size / 2));
    pos.y = (f32)(int)pos.y;
    imgui.draw_list->AddText(pos, 0xffffffff, str);
}
