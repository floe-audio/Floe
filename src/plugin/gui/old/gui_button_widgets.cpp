// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_button_widgets.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/elements/gui_utils.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"
#include "processor/param.hpp"

namespace buttons {

static u32 GetCol(GuiState& g, Style const& style, ColourSet const& colours, imgui::Id id, bool state) {
    auto col = state ? colours.on : colours.reg;
    if (colours.grey_out_aware && style.greyed_out) col = state ? colours.greyed_out_on : colours.greyed_out;
    if (g.imgui.IsHot(id)) col = state ? colours.hot_on : colours.hot_off;
    if (g.imgui.IsActive(id, MouseButton::Left)) col = state ? colours.active_on : colours.active_off;
    return col;
}

static bool DrawBackground(GuiState& g, Style const& style, Rect r, imgui::Id id, bool state) {
    if (auto col = GetCol(g, style, style.back_cols, id, state)) {
        auto const rounding = LivePx(UiSizeId::CornerRounding);
        g.imgui.draw_list->AddRectFilled(r, col, rounding, style.corner_rounding_flags);
        return true;
    }
    return false;
}

static String GetTempCapitalisedString(String str) {
    static char buffer[64];
    auto const caps_str_size = Min(str.size, ArraySize(buffer));
    for (auto const i : Range(caps_str_size))
        buffer[i] = ToUppercaseAscii(str[i]);
    return {buffer, caps_str_size};
}

static void DrawKeyboardIcon(GuiState& g, Style const& style, Rect r, imgui::Id id, bool state) {
    auto& im = g.imgui;
    DrawBackground(g, style, r, id, state);

    auto const white_width = LiveRaw(UiSizeId::KeyboardIconWhiteWidth1) / 100.0f * r.w;
    auto const white_height = LiveRaw(UiSizeId::KeyboardIconWhiteHeight1) / 100.0f * r.w;
    auto const rounding = LiveRaw(UiSizeId::KeyboardIconRounding1) / 100.0f * r.w;
    auto const black_width = LiveRaw(UiSizeId::KeyboardIconBlackWidth1) / 100.0f * r.w;
    auto const black_height = LiveRaw(UiSizeId::KeyboardIconBlackHeight1) / 100.0f * r.w;
    auto const gap = Max(1.0f, LiveRaw(UiSizeId::KeyboardIconGap1) / 100.0f * r.w);

    auto const total_width = (white_width * 3) + (gap * 2);
    auto const total_height = white_height;

    f32x2 const start_pos {r.CentreX() - (total_width / 2), r.CentreY() - (total_height / 2)};

    auto col = GetCol(g, style, style.main_cols, id, state);

    {
        {
            Rect kr {.pos = start_pos + f32x2 {0, black_height},
                     .size = f32x2 {white_width, white_height - black_height}};
            im.draw_list->AddRectFilled(kr, col, rounding, 0b0011);

            kr.x += white_width + gap;
            im.draw_list->AddRectFilled(kr, col);

            kr.x += white_width + gap;
            im.draw_list->AddRectFilled(kr, col, rounding, 0b1100);
        }

        {
            auto const white_top_width = (total_width - (black_width * 2 + gap * 4)) / 3;
            Rect kr {.pos = start_pos, .size = f32x2 {white_top_width, white_height}};

            im.draw_list->AddRectFilled(kr, col, rounding, 0b0011);

            kr.x += white_top_width + gap + black_width + gap;
            im.draw_list->AddRectFilled(kr, col);

            kr.x = start_pos.x + total_width - white_top_width;
            im.draw_list->AddRectFilled(kr, col, rounding, 0b1100);
        }
    }
}

static void DrawIconOrText(GuiState& g,
                           Style const& style,
                           Rect r,
                           imgui::Id id,
                           String str,
                           bool state,
                           bool using_icon_font) {
    auto& im = g.imgui;
    DrawBackground(g, style, r, id, state);

    if (style.icon_or_text.justification & TextJustification::Left) {
        if (style.icon_or_text.add_margin_x) r = r.CutLeft(LivePx(UiSizeId::MenuButtonTextMarginL));
    } else if (style.icon_or_text.justification & TextJustification::Right) {
        if (style.icon_or_text.add_margin_x) r = r.CutRight(LivePx(UiSizeId::MenuButtonTextMarginL));
    }

    if (style.icon_or_text.capitalise) str = GetTempCapitalisedString(str);
    im.draw_list->AddTextInRect(r,
                                GetCol(g, style, style.main_cols, id, state),
                                str,
                                {
                                    .justification = style.icon_or_text.justification,
                                    .overflow_type = style.icon_or_text.overflow_type,
                                    .font_scaling = using_icon_font ? style.icon_scaling : style.text_scaling,
                                });
}

static void
DrawIconAndTextButton(GuiState& g, Style const& style, Rect r, imgui::Id id, String str, bool state) {
    auto& im = g.imgui;

    auto const icon_col = GetCol(g, style, style.main_cols, id, state);
    auto const text_col = GetCol(g, style, style.text_cols, id, state);

    DrawBackground(g, style, r, id, state);

    if (style.type != LayoutAndSizeType::IconAndTextInstSelector) {
        g.fonts.Push(ToInt(FontType::Icons));
        DEFER { g.fonts.Pop(); };
        auto just = TextJustification::CentredLeft;
        auto btn_r = r;
        if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
            btn_r = r.WithW(LivePx(UiSizeId::LayerParamsGroupTabsIconW));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
            btn_r = r.WithW(LivePx(UiSizeId::MidiItemWidth));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem) {
            btn_r =
                r.WithW(LivePx(UiSizeId::MenuItemTickWidth)).CutLeft(LivePx(UiSizeId::MenuItemIconMarginX));
        } else if (style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
            btn_r = r.CutLeft(r.w - LivePx(UiSizeId::MenuItemSubMenuArrowWidth))
                        .CutRight(LivePx(UiSizeId::MenuItemIconMarginX));
            just = TextJustification::CentredRight;
        }
        im.draw_list->AddTextInRect(btn_r,
                                    icon_col,
                                    state ? style.icon_and_text.on_icon : style.icon_and_text.off_icon,
                                    {
                                        .justification = just,
                                        .overflow_type = TextOverflowType::AllowOverflow,
                                        .font_scaling = style.icon_scaling,
                                    });
    } else if (style.icon_and_text.icon_texture) {
        auto const icon_r = Rect {.x = r.x, .y = r.y, .w = r.h, .h = r.h}.Reduced(r.h / 10);
        im.draw_list->AddImageRect(*style.icon_and_text.icon_texture, icon_r);
    }

    if (style.icon_and_text.capitalise) str = GetTempCapitalisedString(str);

    auto just = TextJustification::CentredLeft;
    auto text_offset = LivePx(UiSizeId::PageHeadingTextOffset);
    auto overflow = TextOverflowType::AllowOverflow;
    if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
        text_offset = LivePx(UiSizeId::MidiItemWidth) + LivePx(UiSizeId::MidiItemMarginLR);
    } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem ||
               style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
        text_offset = LivePx(UiSizeId::MenuItemTickWidth);
    } else if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
        text_offset = 0;
        just = TextJustification::Centred;
    } else if (style.type == LayoutAndSizeType::IconAndTextInstSelector) {
        overflow = TextOverflowType::ShowDotsOnRight;
        if (style.icon_and_text.icon_texture)
            text_offset = r.h + r.h / 5;
        else
            text_offset = LivePx(UiSizeId::MenuButtonTextMarginL);
    }
    im.draw_list->AddTextInRect(r.CutLeft(text_offset),
                                text_col,
                                str,
                                {
                                    .justification = just,
                                    .overflow_type = overflow,
                                    .font_scaling = style.text_scaling,
                                });
}

static bool ButtonInternal(GuiState& g,
                           Style const& style,
                           Optional<imgui::Id> id,
                           Optional<imgui::Id> popup_id,
                           Rect r,
                           bool& state,
                           String str) {
    auto flags = imgui::ButtonConfig {};
    flags.closes_popup_or_modal = false;
    if (!popup_id && style.closes_popups) flags.closes_popup_or_modal = true;

    // TODO: this does not need to be a lamba
    auto const draw = [&g, &style](Rect r, imgui::Id id, String str, bool state) {
        switch (style.type) {
            case LayoutAndSizeType::None: PanicIfReached(); break;
            case LayoutAndSizeType::IconOrTextKeyboardIcon: {
                DrawKeyboardIcon(g, style, r, id, state);
                break;
            }
            case LayoutAndSizeType::IconOrText: {
                if (!str.size) str = style.icon_or_text.default_icon;
                auto const using_icon_font = str.size && (str[0] & 0x80);
                if (using_icon_font) g.fonts.Push(ToInt(FontType::Icons));
                DEFER {
                    if (using_icon_font) g.fonts.Pop();
                };
                DrawIconOrText(g, style, r, id, str, state, using_icon_font);
                break;
            }
            case LayoutAndSizeType::IconAndTextMenuItem:
            case LayoutAndSizeType::IconAndTextSubMenuItem:
            case LayoutAndSizeType::IconAndTextMidiButton:
            case LayoutAndSizeType::IconAndTextLayerTab:
            case LayoutAndSizeType::IconAndTextInstSelector:
            case LayoutAndSizeType::IconAndText: {
                DrawIconAndTextButton(g, style, r, id, str, state);
                break;
            }
        }
    };

    if (popup_id) {
        ASSERT(id.HasValue());
        ASSERT(!style.draw_with_overlay_graphics);
        r = g.imgui.RegisterAndConvertRect(r);

        auto const o = g.imgui.PopupMenuButtonBehaviour(r, *id, *popup_id, flags);
        draw(r, *id, str, o.show_as_active);
        if (g.imgui.IsPopupMenuOpen(*popup_id)) {
            g.imgui.BeginViewport(k_default_popup_menu_viewport, *popup_id, r);
            return true;
        }
        return false;
    } else if (id) {
        ASSERT(!style.draw_with_overlay_graphics);
        r = g.imgui.RegisterAndConvertRect(r);
        bool const clicked = g.imgui.ToggleButtonBehaviour(r, *id, flags, state);
        draw(r, *id, str, state);
        return clicked;
    } else {
        if (!style.draw_with_overlay_graphics) r = g.imgui.RegisterAndConvertRect(r);
        imgui::Id const fake_id = 99;
        auto graphics = g.imgui.draw_list;
        if (style.draw_with_overlay_graphics) g.imgui.draw_list = g.imgui.overlay_draw_list;
        draw(r, fake_id, str, state);
        g.imgui.draw_list = graphics;
        return false;
    }
}

bool Toggle(GuiState& g, imgui::Id id, Rect r, bool& state, String str, Style const& style) {
    return ButtonInternal(g, style, id, {}, r, state, str);
}

bool Popup(GuiState& g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style) {
    bool state = false;
    return ButtonInternal(g, style, button_id, popup_id, r, state, str);
}

bool Button(GuiState& g, imgui::Id id, Rect r, String str, Style const& style) {
    bool state = false;
    return Toggle(g, id, r, state, str, style);
}

ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, Rect r, String str, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);
    Optional<f32> val {};
    bool state = param.BoolValue();
    if (Toggle(g, id, r, state, str, style)) val = state ? 1.0f : 0.0f;
    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

ButtonReturnObject Toggle(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style) {
    return Toggle(g, param, r, param.info.gui_label, style);
}

ButtonReturnObject PopupWithItems(GuiState& g, DescribedParamValue const& param, Rect r, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);

    auto const converted_r = g.imgui.RegisterAndConvertRect(r);

    // draw it around the whole thing, not just the menu
    if (style.back_cols.reg)
        g.imgui.draw_list->AddRectFilled(converted_r, style.back_cols.reg, LivePx(UiSizeId::CornerRounding));

    auto const btn_w = LivePx(UiSizeId::NextPrevButtonSize);
    auto const margin_r = LivePx(UiSizeId::ParamIntButtonMarginR);
    rect_cut::CutRight(r, margin_r);
    auto rect_r = rect_cut::CutRight(r, btn_w);
    auto rect_l = rect_cut::CutRight(r, btn_w);

    Optional<f32> val {};
    auto popup_style = style;
    popup_style.back_cols = {};
    if (Popup(g, id, id + 1, r, ParamMenuText(param.info.index, param.LinearValue()), popup_style)) {
        auto current = param.IntValue<int>();
        if (DoMultipleMenuItems(g, ParameterMenuItems(param.info.index), current)) val = (f32)current;
        g.imgui.EndViewport();
    }

    {
        auto current = param.LinearValue();
        if (g.imgui.SliderBehaviourRange({
                .rect_in_window_coords = converted_r,
                .id = id,
                .min = param.info.linear_range.min,
                .max = param.info.linear_range.max,
                .value = current,
                .default_value = param.info.default_linear_value,
                .cfg = {.sensitivity = 20},
            })) {
            val = current;
        }
    }

    auto button_style = IconButton(g.imgui);
    button_style.greyed_out = style.greyed_out;
    auto const left_id = id - 4;
    auto const right_id = id + 4;
    if (buttons::Button(g, left_id, rect_l, ICON_FA_CARET_LEFT, button_style)) {
        auto new_val = (f32)param.IntValue<int>() - 1;
        if (new_val < param.info.linear_range.min) new_val = param.info.linear_range.max;
        val = new_val;
    }
    if (buttons::Button(g, right_id, rect_r, ICON_FA_CARET_RIGHT, button_style)) {
        auto new_val = (f32)param.IntValue<int>() + 1;
        if (new_val > param.info.linear_range.max) new_val = param.info.linear_range.min;
        val = new_val;
    }
    Tooltip(g, left_id, g.imgui.ViewportRectToWindowRect(rect_l), "Previous"_s, {});
    Tooltip(g, right_id, g.imgui.ViewportRectToWindowRect(rect_r), "Next"_s, {});

    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

bool Button(GuiState& g, Rect r, String str, Style const& style) {
    return Button(g, g.imgui.MakeId(str), r, str, style);
}

bool Toggle(GuiState& g, Rect r, bool& state, String str, Style const& style) {
    return Toggle(g, g.imgui.MakeId(str), r, state, str, style);
}

bool Popup(GuiState& g, imgui::Id popup_id, Rect r, String str, Style const& style) {
    return Popup(g, g.imgui.MakeId(str), popup_id, r, str, style);
}

void FakeButton(GuiState& g, Rect r, String str, Style const& style) { FakeButton(g, r, str, false, style); }

void FakeButton(GuiState& g, Rect r, String str, bool state, Style const& style) {
    ButtonInternal(g, style, {}, {}, r, state, str);
}

bool Button(GuiState& g, imgui::Id id, layout::Id lay_id, String str, Style const& style) {
    return Button(g, id, layout::GetRect(g.layout, lay_id), str, style);
}
bool Toggle(GuiState& g, imgui::Id id, layout::Id lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, id, layout::GetRect(g.layout, lay_id), state, str, style);
}
bool Popup(GuiState& g,
           imgui::Id button_id,
           imgui::Id popup_id,
           layout::Id lay_id,
           String str,
           Style const& style) {
    return Popup(g, button_id, popup_id, layout::GetRect(g.layout, lay_id), str, style);
}

bool Button(GuiState& g, layout::Id lay_id, String str, Style const& style) {
    return Button(g, layout::GetRect(g.layout, lay_id), str, style);
}
bool Toggle(GuiState& g, layout::Id lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, layout::GetRect(g.layout, lay_id), state, str, style);
}
bool Popup(GuiState& g, imgui::Id popup_id, layout::Id lay_id, String str, Style const& style) {
    return Popup(g, popup_id, layout::GetRect(g.layout, lay_id), str, style);
}

ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, String str, Style const& style) {
    return Toggle(g, param, layout::GetRect(g.layout, lay_id), str, style);
}
ButtonReturnObject
Toggle(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style) {
    return Toggle(g, param, layout::GetRect(g.layout, lay_id), style);
}
ButtonReturnObject
PopupWithItems(GuiState& g, DescribedParamValue const& param, layout::Id lay_id, Style const& style) {
    return PopupWithItems(g, param, layout::GetRect(g.layout, lay_id), style);
}

void FakeButton(GuiState& g, layout::Id lay_id, String str, Style const& style) {
    FakeButton(g, layout::GetRect(g.layout, lay_id), str, style);
}
} // namespace buttons
