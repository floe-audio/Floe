// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_button_widgets.hpp"

#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "processor/param.hpp"

namespace buttons {

static u32 GetCol(Gui* g, Style const& style, ColourSet const& colours, imgui::Id id, bool state) {
    auto col = state ? colours.on : colours.reg;
    if (colours.grey_out_aware && style.greyed_out) col = state ? colours.greyed_out_on : colours.greyed_out;
    if (g->imgui.IsHot(id)) col = state ? colours.hot_on : colours.hot_off;
    if (g->imgui.IsActive(id)) col = state ? colours.active_on : colours.active_off;
    return col;
}

static bool DrawBackground(Gui* g, Style const& style, Rect r, imgui::Id id, bool state) {
    if (auto col = GetCol(g, style, style.back_cols, id, state)) {
        auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
        g->imgui.graphics->AddRectFilled(r.Min(), r.Max(), col, rounding, style.corner_rounding_flags);
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

static void DrawKeyboardIcon(Gui* g, Style const& style, Rect r, imgui::Id id, bool state) {
    auto& im = g->imgui;
    DrawBackground(g, style, r, id, state);

    auto const white_width = LiveSize(im, UiSizeId::KeyboardIconWhiteWidth) / 100.0f * r.w;
    auto const white_height = LiveSize(im, UiSizeId::KeyboardIconWhiteHeight) / 100.0f * r.w;
    auto const rounding = LiveSize(im, UiSizeId::KeyboardIconRounding) / 100.0f * r.w;
    auto const black_width = LiveSize(im, UiSizeId::KeyboardIconBlackWidth) / 100.0f * r.w;
    auto const black_height = LiveSize(im, UiSizeId::KeyboardIconBlackHeight) / 100.0f * r.w;
    auto const gap = Max(1.0f, LiveSize(im, UiSizeId::KeyboardIconGap) / 100.0f * r.w);

    auto const total_width = (white_width * 3) + (gap * 2);
    auto const total_height = white_height;

    f32x2 const start_pos {r.CentreX() - (total_width / 2), r.CentreY() - (total_height / 2)};

    auto col = GetCol(g, style, style.main_cols, id, state);

    {
        {
            Rect kr {.pos = start_pos + f32x2 {0, black_height},
                     .size = f32x2 {white_width, white_height - black_height}};
            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 4 | 8);

            kr.x += white_width + gap;
            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col);

            kr.x += white_width + gap;
            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 1 | 2);
        }

        {
            auto const white_top_width = (total_width - (black_width * 2 + gap * 4)) / 3;
            Rect kr {.pos = start_pos, .size = f32x2 {white_top_width, white_height}};

            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 4 | 8);

            kr.x += white_top_width + gap + black_width + gap;
            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col);

            kr.x = start_pos.x + total_width - white_top_width;
            im.graphics->AddRectFilled(kr.Min(), kr.Max(), col, rounding, 1 | 2);
        }
    }
}

static void DrawIconOrText(Gui* g,
                           Style const& style,
                           Rect r,
                           imgui::Id id,
                           String str,
                           bool state,
                           bool using_icon_font) {
    auto& im = g->imgui;
    DrawBackground(g, style, r, id, state);

    if (style.icon_or_text.justification & TextJustification::Left) {
        if (style.icon_or_text.add_margin_x) r = r.CutLeft(LiveSize(im, UiSizeId::MenuButtonTextMarginL));
    } else if (style.icon_or_text.justification & TextJustification::Right) {
        if (style.icon_or_text.add_margin_x) r = r.CutRight(LiveSize(im, UiSizeId::MenuButtonTextMarginL));
    }

    if (style.icon_or_text.capitalise) str = GetTempCapitalisedString(str);
    im.graphics->AddTextJustified(r,
                                  str,
                                  GetCol(g, style, style.main_cols, id, state),
                                  style.icon_or_text.justification,
                                  style.icon_or_text.overflow_type,
                                  using_icon_font ? style.icon_scaling : style.text_scaling);
}

static void DrawIconAndTextButton(Gui* g, Style const& style, Rect r, imgui::Id id, String str, bool state) {
    auto& im = g->imgui;

    auto const icon_col = GetCol(g, style, style.main_cols, id, state);
    auto const text_col = GetCol(g, style, style.text_cols, id, state);

    DrawBackground(g, style, r, id, state);

    if (style.type != LayoutAndSizeType::IconAndTextInstSelector) {
        im.graphics->context->PushFont(g->fonts[ToInt(FontType::Icons)]);
        DEFER { im.graphics->context->PopFont(); };
        auto just = TextJustification::CentredLeft;
        auto btn_r = r;
        if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
            btn_r = r.WithW(LiveSize(im, UiSizeId::LayerParamsGroupTabsIconW));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
            btn_r = r.WithW(LiveSize(im, UiSizeId::MIDI_ItemWidth));
            just = TextJustification::CentredRight;
        } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem) {
            btn_r = r.WithW(LiveSize(im, UiSizeId::MenuItem_TickWidth))
                        .CutLeft(LiveSize(im, UiSizeId::MenuItem_IconMarginX));
        } else if (style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
            btn_r = r.CutLeft(r.w - LiveSize(im, UiSizeId::MenuItem_SubMenuArrowWidth))
                        .CutRight(LiveSize(im, UiSizeId::MenuItem_IconMarginX));
            just = TextJustification::CentredRight;
        }
        im.graphics->AddTextJustified(btn_r,
                                      state ? style.icon_and_text.on_icon : style.icon_and_text.off_icon,
                                      icon_col,
                                      just,
                                      TextOverflowType::AllowOverflow,
                                      style.icon_scaling);
    } else if (style.icon_and_text.icon_texture) {
        auto const icon_r = Rect {.x = r.x, .y = r.y, .w = r.h, .h = r.h}.Reduced(r.h / 10);
        im.graphics->AddImage(*style.icon_and_text.icon_texture, icon_r.Min(), icon_r.Max());
    }

    if (style.icon_and_text.capitalise) str = GetTempCapitalisedString(str);

    auto just = TextJustification::CentredLeft;
    auto text_offset = LiveSize(im, UiSizeId::Page_HeadingTextOffset);
    auto overflow = TextOverflowType::AllowOverflow;
    if (style.type == LayoutAndSizeType::IconAndTextMidiButton) {
        text_offset = LiveSize(im, UiSizeId::MIDI_ItemWidth) + LiveSize(im, UiSizeId::MIDI_ItemMarginLR);
    } else if (style.type == LayoutAndSizeType::IconAndTextMenuItem ||
               style.type == LayoutAndSizeType::IconAndTextSubMenuItem) {
        text_offset = LiveSize(im, UiSizeId::MenuItem_TickWidth);
    } else if (style.type == LayoutAndSizeType::IconAndTextLayerTab) {
        text_offset = 0;
        just = TextJustification::Centred;
    } else if (style.type == LayoutAndSizeType::IconAndTextInstSelector) {
        overflow = TextOverflowType::ShowDotsOnRight;
        if (style.icon_and_text.icon_texture)
            text_offset = r.h + r.h / 5;
        else
            text_offset = LiveSize(im, UiSizeId::MenuButtonTextMarginL);
    }
    im.graphics->AddTextJustified(r.CutLeft(text_offset), str, text_col, just, overflow, style.text_scaling);
}

static bool ButtonInternal(Gui* g,
                           Style const& style,
                           Optional<imgui::Id> id,
                           Optional<imgui::Id> popup_id,
                           Rect r,
                           bool& state,
                           String str) {
    auto im = imgui::DefButton();
    im.window = PopupWindowSettings(g->imgui);
    im.flags.closes_popups = false;
    if (!popup_id && style.closes_popups) im.flags.closes_popups = true;

    im.draw = [g, &style](IMGUI_DRAW_BUTTON_ARGS) {
        switch (style.type) {
            case LayoutAndSizeType::None: PanicIfReached(); break;
            case LayoutAndSizeType::IconOrTextKeyboardIcon: {
                DrawKeyboardIcon(g, style, r, id, state);
                break;
            }
            case LayoutAndSizeType::IconOrText: {
                if (!str.size) str = style.icon_or_text.default_icon;
                auto& ctx = *g->imgui.graphics->context;
                auto const using_icon_font = str.size && (str[0] & 0x80);
                if (using_icon_font) ctx.PushFont(g->fonts[ToInt(FontType::Icons)]);
                DEFER {
                    if (using_icon_font) ctx.PopFont();
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
        return g->imgui.PopupButton(im, r, *id, *popup_id, str);
    } else if (id) {
        ASSERT(!style.draw_with_overlay_graphics);
        return g->imgui.ToggleButton(im, r, *id, state, str);
    } else {
        if (!style.draw_with_overlay_graphics) g->imgui.RegisterAndConvertRect(&r);
        imgui::Id const fake_id = 99;
        auto graphics = g->imgui.graphics;
        if (style.draw_with_overlay_graphics) g->imgui.graphics = &g->imgui.overlay_graphics;
        im.draw(g->imgui, r, fake_id, str, state);
        g->imgui.graphics = graphics;
        return false;
    }
}

bool Toggle(Gui* g, imgui::Id id, Rect r, bool& state, String str, Style const& style) {
    return ButtonInternal(g, style, id, {}, r, state, str);
}

bool Popup(Gui* g, imgui::Id button_id, imgui::Id popup_id, Rect r, String str, Style const& style) {
    bool state = false;
    return ButtonInternal(g, style, button_id, popup_id, r, state, str);
}

bool Button(Gui* g, imgui::Id id, Rect r, String str, Style const& style) {
    bool state = false;
    return Toggle(g, id, r, state, str, style);
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, String str, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);
    Optional<f32> val {};
    bool state = param.ValueAsBool();
    if (Toggle(g, id, r, state, str, style)) val = state ? 1.0f : 0.0f;
    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, Rect r, Style const& style) {
    return Toggle(g, param, r, param.info.gui_label, style);
}

ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, Rect r, Style const& style) {
    auto const id = BeginParameterGUI(g, param, r);

    // draw it around the whole thing, not just the menu
    if (style.back_cols.reg) {
        auto const converted_r = g->imgui.GetRegisteredAndConvertedRect(r);
        g->imgui.graphics->AddRectFilled(converted_r.Min(),
                                         converted_r.Max(),
                                         style.back_cols.reg,
                                         LiveSize(g->imgui, UiSizeId::CornerRounding));
    }

    auto const btn_w = LiveSize(g->imgui, UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveSize(g->imgui, UiSizeId::ParamIntButtonMarginR);
    rect_cut::CutRight(r, margin_r);
    auto rect_r = rect_cut::CutRight(r, btn_w);
    auto rect_l = rect_cut::CutRight(r, btn_w);

    Optional<f32> val {};
    auto popup_style = style;
    popup_style.back_cols = {};
    if (Popup(g, id, id + 1, r, ParamMenuText(param.info.index, param.LinearValue()), popup_style)) {
        auto current = param.ValueAsInt<int>();
        if (DoMultipleMenuItems(g, ParameterMenuItems(param.info.index), current)) val = (f32)current;
        g->imgui.EndWindow();
    }

    {
        auto current = param.LinearValue();
        if (g->imgui.SliderRange(
                {
                    .flags = imgui::DefSlider().flags,
                    .sensitivity = 100 + (5000 * 1.0f / param.info.linear_range.Delta()),
                    .draw = [](IMGUI_DRAW_SLIDER_ARGS) {},
                },
                r,
                id,
                param.info.linear_range.min,
                param.info.linear_range.max,
                current,
                param.info.default_linear_value)) {
            val = current;
        }
    }

    auto button_style = IconButton(g->imgui);
    button_style.greyed_out = style.greyed_out;
    auto const left_id = id - 4;
    auto const right_id = id + 4;
    if (buttons::Button(g, left_id, rect_l, ICON_FA_CARET_LEFT, button_style)) {
        auto new_val = (f32)param.ValueAsInt<int>() - 1;
        if (new_val < param.info.linear_range.min) new_val = param.info.linear_range.max;
        val = new_val;
    }
    if (buttons::Button(g, right_id, rect_r, ICON_FA_CARET_RIGHT, button_style)) {
        auto new_val = (f32)param.ValueAsInt<int>() + 1;
        if (new_val > param.info.linear_range.max) new_val = param.info.linear_range.min;
        val = new_val;
    }
    Tooltip(g, left_id, rect_l, "Previous"_s);
    Tooltip(g, right_id, rect_r, "Next"_s);

    EndParameterGUI(g,
                    id,
                    param,
                    r,
                    val,
                    style.no_tooltips ? ParamDisplayFlagsNoTooltip : ParamDisplayFlagsDefault);
    return {val.HasValue(), id};
}

bool Button(Gui* g, Rect r, String str, Style const& style) {
    return Button(g, g->imgui.GetID(str), r, str, style);
}

bool Toggle(Gui* g, Rect r, bool& state, String str, Style const& style) {
    return Toggle(g, g->imgui.GetID(str), r, state, str, style);
}

bool Popup(Gui* g, imgui::Id popup_id, Rect r, String str, Style const& style) {
    return Popup(g, g->imgui.GetID(str), popup_id, r, str, style);
}

void FakeButton(Gui* g, Rect r, String str, Style const& style) { FakeButton(g, r, str, false, style); }

void FakeButton(Gui* g, Rect r, String str, bool state, Style const& style) {
    ButtonInternal(g, style, {}, {}, r, state, str);
}

bool Button(Gui* g, imgui::Id id, layout::Id lay_id, String str, Style const& style) {
    return Button(g, id, layout::GetRect(g->layout, lay_id), str, style);
}
bool Toggle(Gui* g, imgui::Id id, layout::Id lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, id, layout::GetRect(g->layout, lay_id), state, str, style);
}
bool Popup(Gui* g,
           imgui::Id button_id,
           imgui::Id popup_id,
           layout::Id lay_id,
           String str,
           Style const& style) {
    return Popup(g, button_id, popup_id, layout::GetRect(g->layout, lay_id), str, style);
}

bool Button(Gui* g, layout::Id lay_id, String str, Style const& style) {
    return Button(g, layout::GetRect(g->layout, lay_id), str, style);
}
bool Toggle(Gui* g, layout::Id lay_id, bool& state, String str, Style const& style) {
    return Toggle(g, layout::GetRect(g->layout, lay_id), state, str, style);
}
bool Popup(Gui* g, imgui::Id popup_id, layout::Id lay_id, String str, Style const& style) {
    return Popup(g, popup_id, layout::GetRect(g->layout, lay_id), str, style);
}

ButtonReturnObject Toggle(Gui* g, Parameter const& param, layout::Id lay_id, String str, Style const& style) {
    return Toggle(g, param, layout::GetRect(g->layout, lay_id), str, style);
}
ButtonReturnObject Toggle(Gui* g, Parameter const& param, layout::Id lay_id, Style const& style) {
    return Toggle(g, param, layout::GetRect(g->layout, lay_id), style);
}
ButtonReturnObject PopupWithItems(Gui* g, Parameter const& param, layout::Id lay_id, Style const& style) {
    return PopupWithItems(g, param, layout::GetRect(g->layout, lay_id), style);
}

void FakeButton(Gui* g, layout::Id lay_id, String str, Style const& style) {
    FakeButton(g, layout::GetRect(g->layout, lay_id), str, style);
}
} // namespace buttons
