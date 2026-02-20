// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_helpers.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui2_popup_menu.hpp"
#include "gui/elements/gui_drawing_helpers.hpp"
#include "gui/elements/gui_utils.hpp"
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

static void MidiLearnMenu(GuiState& g, imgui::Id id, Span<ParamIndex> params, Rect r) {
    auto& imgui = g.imgui;
    auto& engine = g.engine;
    auto right_clicker_id = id;
    imgui.PushId((uintptr)params[0]);
    auto popup_id = imgui.MakeId("MidiLearnPopup");
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
        imgui::PopupJustification::AboveOrBelow);
    Rect const popup_r {.pos = popup_pos};

    if (imgui.IsPopupMenuOpen(popup_id)) {
        imgui.BeginViewport(k_default_popup_menu_viewport, popup_id, popup_r, __FUNCTION__);
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
                imgui.draw_list->AddRectFilled(div_r, ToU32({.c = Col::Surface0}));
                pos += div_h;
            }
        }
    }
}

static void MidiLearnMenu(GuiState& g, imgui::Id id, ParamIndex param, Rect r) {
    MidiLearnMenu(g, id, {&param, 1}, r);
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

imgui::Id BeginParameterGUI(GuiState& g, DescribedParamValue const& param, Rect r) {
    auto const id = g.imgui.MakeId(param.info.id);
    if (!(param.info.flags.not_automatable)) MidiLearnMenu(g, id, param.info.index, r);
    return id;
}

void EndParameterGUI(GuiState& g,
                     imgui::Id id,
                     DescribedParamValue const& param,
                     Rect viewport_r,
                     Optional<f32> new_val,
                     ParamDisplayFlags flags) {
    if (g.imgui.WasJustActivated(id, MouseButton::Left))
        ParameterJustStartedMoving(g.engine.processor, param.info.index);
    if (new_val) SetParameterValue(g.engine.processor, param.info.index, *new_val, {});
    if (g.imgui.WasJustDeactivated(id, MouseButton::Left))
        ParameterJustStoppedMoving(g.engine.processor, param.info.index);

    if (!(flags & ParamDisplayFlagsNoTooltip) && !g.imgui.TextInputHasFocus(id))
        DoParameterTooltipIfNeeded(g, param, id, g.imgui.ViewportRectToWindowRect(viewport_r));
    if (!(flags & ParamDisplayFlagsNoValuePopup) && param.info.value_type == ParamValueType::Float)
        ParameterValuePopup(g, param, id, g.imgui.ViewportRectToWindowRect(viewport_r));
}
