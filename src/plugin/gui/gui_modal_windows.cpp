// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_modal_windows.hpp"

#include <IconsFontAwesome6.h>

#include "foundation/foundation.hpp"

#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui/gui_button_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"

PUBLIC Rect ModalRect(imgui::Context const& imgui, f32 width, f32 height) {
    auto const size = f32x2 {width, height};
    Rect r;
    r.pos = imgui.frame_input.window_size.ToFloat2() / 2 - size / 2; // centre
    r.size = size;
    return r;
}

PUBLIC Rect ModalRect(imgui::Context const& imgui, UiSizeId width_id, UiSizeId height_id) {
    return ModalRect(imgui, LiveSize(imgui, width_id), LiveSize(imgui, height_id));
}

static imgui::Id IdForModal(ModalWindowType type) { return (imgui::Id)(ToInt(type) + 1000); }

struct IncrementingY {
    f32& y;
};

struct DoButtonArgs {
    Optional<IncrementingY> incrementing_y;
    Optional<f32> y;
    f32 x_offset;
    bool centre_vertically;
    bool auto_width;
    f32 width;
    String tooltip;
    bool greyed_out;
    String icon;
    bool significant;
    bool insignificant;
    bool white_background;
};

static bool DoButton(Gui* g, String button_text, DoButtonArgs args) {
    auto& imgui = g->imgui;

    auto const line_height = imgui.graphics->context->CurrentFontSize();
    auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
    auto const icon_scaling = 0.8f;
    auto const icon_size = line_height * icon_scaling;
    auto const box_padding = line_height * 0.4f;
    auto const gap_between_icon_and_text = box_padding;

    auto const y_pos = args.incrementing_y ? args.incrementing_y->y : args.y.ValueOr(0);

    auto const text_width =
        draw::GetTextSize(imgui.graphics->context->CurrentFont(), button_text, imgui.Width()).x;

    auto const content_width = text_width + (args.icon.size ? icon_size + gap_between_icon_and_text : 0);

    auto const box_width = (args.auto_width) ? (content_width + (box_padding * 2)) : args.width;
    auto const box_height = line_height * 1.5f;

    auto x_pos = args.x_offset;
    if (args.centre_vertically) x_pos = (imgui.Width() - box_width) / 2;

    auto button_r = imgui.GetRegisteredAndConvertedRect({.xywh {x_pos, y_pos, box_width, box_height}});
    auto const id = imgui.GetID(button_text);

    bool result = false;
    if (!args.greyed_out)
        result = imgui.ButtonBehavior(button_r, id, {.left_mouse = true, .triggers_on_mouse_up = true});

    imgui.graphics->AddRectFilled(
        button_r,
        LiveCol(imgui,
                !imgui.IsHot(id)
                    ? (args.white_background ? UiColMap::PopupWindowBack : UiColMap::ModalWindowButtonBack)
                    : UiColMap::ModalWindowButtonBackHover),
        rounding);

    if (!args.greyed_out)
        imgui.graphics->AddRect(button_r,
                                LiveCol(imgui,
                                        args.significant ? UiColMap::ModalWindowButtonOutlineSignificant
                                                         : UiColMap::ModalWindowButtonOutline),
                                rounding);

    auto const required_padding = (box_width - content_width) / 2;
    rect_cut::CutLeft(button_r, required_padding);
    rect_cut::CutRight(button_r, required_padding);

    if (args.icon.size) {
        imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Icons)]);
        DEFER { imgui.graphics->context->PopFont(); };

        auto const icon_r = rect_cut::CutLeft(button_r, icon_size);
        rect_cut::CutLeft(button_r, gap_between_icon_and_text);

        imgui.graphics->AddTextJustified(icon_r,
                                         args.icon,
                                         LiveCol(imgui,
                                                 args.greyed_out ? UiColMap::ModalWindowButtonTextInactive
                                                                 : UiColMap::ModalWindowButtonIcon),
                                         TextJustification::CentredLeft,
                                         TextOverflowType::AllowOverflow,
                                         icon_scaling);
    }

    imgui.graphics->AddTextJustified(
        button_r,
        button_text,
        LiveCol(imgui,
                args.greyed_out ? UiColMap::ModalWindowButtonTextInactive
                                : (args.insignificant ? UiColMap::ModalWindowInsignificantText
                                                      : UiColMap::ModalWindowButtonText)),
        TextJustification::CentredLeft);

    if (args.tooltip.size) Tooltip(g, id, button_r, args.tooltip, true);

    if (args.incrementing_y) args.incrementing_y->y += box_height;
    return result;
}

static bool DoButton(Gui* g, String button_text, f32& y_pos, f32 x_offset) {
    return DoButton(g,
                    button_text,
                    {
                        .incrementing_y = IncrementingY {y_pos},
                        .x_offset = x_offset,
                        .auto_width = true,
                    });
}

static void DoHeading(Gui* g,
                      f32& y_pos,
                      String str,
                      TextJustification justification = TextJustification::CentredLeft,
                      UiColMap col = UiColMap::PopupItemText) {
    auto& imgui = g->imgui;
    auto const window_title_h = LiveSize(imgui, UiSizeId::ModalWindowTitleH);
    auto const window_title_gap_y = LiveSize(imgui, UiSizeId::ModalWindowTitleGapY);

    imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Heading1)]);
    DEFER { imgui.graphics->context->PopFont(); };
    auto const r = imgui.GetRegisteredAndConvertedRect({.xywh {0, y_pos, imgui.Width(), window_title_h}});
    g->imgui.graphics->AddTextJustified(r, str, LiveCol(imgui, col), justification);

    y_pos += window_title_h + window_title_gap_y;
}

bool DoCloseButtonForCurrentWindow(Gui* g, String tooltip_text, buttons::Style const& style) {
    auto& imgui = g->imgui;
    f32 const pad = LiveSize(imgui, UiSizeId::SidePanelCloseButtonPad);
    f32 const size = LiveSize(imgui, UiSizeId::SidePanelCloseButtonSize);

    auto const x = imgui.Width() - (pad + size);
    Rect const btn_r = {.xywh {x, pad, size, size}};

    auto const btn_id = imgui.GetID("close");
    bool const button_clicked = buttons::Button(g, btn_id, btn_r, ICON_FA_XMARK, style);

    Tooltip(g, btn_id, btn_r, tooltip_text);
    return button_clicked;
}

static void DoLegacyParamsModal(Gui* g) {
    if (!g->legacy_params_window_open) return;

    auto body_font = g->fonts[ToInt(FontType::Body)];
    g->frame_input.graphics_ctx->PushFont(body_font);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::LegacyParamsWindowWidth, UiSizeId::LegacyParamsWindowHeight);
    auto settings = FloeWindowSettings(g->imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
        imgui.graphics->AddRectFilled(r, LiveCol(imgui, UiColMap::TopPanelBackTop), rounding);
    });
    settings.pad_top_left = {LiveSize(imgui, UiSizeId::ModalWindowPadL),
                             LiveSize(imgui, UiSizeId::ModalWindowPadT)};
    settings.pad_bottom_right = {LiveSize(imgui, UiSizeId::ModalWindowPadR),
                                 LiveSize(imgui, UiSizeId::ModalWindowPadB)};

    imgui.BeginWindow(settings, r, "LegacyParamsWindow");
    DEFER { imgui.EndWindow(); };

    f32 y_pos = 0;
    DoHeading(g, y_pos, "Legacy Parameters", TextJustification::CentredLeft, UiColMap::TopPanelTitleText);
    if (DoCloseButtonForCurrentWindow(g,
                                      "Close this window",
                                      buttons::BrowserIconButton(g->imgui).WithLargeIcon())) {
        g->legacy_params_window_open = false;
    }

    // Sub-window
    auto const sub_rect = Rect {
        .x = 0,
        .y = y_pos,
        .w = imgui.Width(),
        .h = imgui.Height() - y_pos,
    };
    auto sub_settings = FloeWindowSettings(g->imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {});
    imgui.BeginWindow(sub_settings, imgui.GetID("LegacyParamsSubWindow"), sub_rect);
    DEFER { imgui.EndWindow(); };

    auto root = layout::CreateItem(g->layout,
                                   g->scratch_arena,
                                   {
                                       .size = g->imgui.Size(),
                                       .contents_gap = {0, 10},
                                       .contents_direction = layout::Direction::Row,
                                       .contents_multiline = true,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   });

    struct ParamData {
        ParamIndex index;
        LayIDPair pair;
        layout::Id extra_label;
    };

    DynamicArray<ParamData> hidden_params {g->scratch_arena};
    for (auto const& desc : k_param_descriptors)
        if (desc.flags.hidden) dyn::Append(hidden_params, {.index = desc.index});

    for (auto& p : hidden_params) {
        auto const container = layout::CreateItem(g->layout,
                                                  g->scratch_arena,
                                                  {
                                                      .parent = root,
                                                      .size = layout::k_hug_contents,
                                                      .contents_direction = layout::Direction::Column,
                                                      .contents_align = layout::Alignment::Start,
                                                  });
        LayoutParameterComponent(g,
                                 container,
                                 p.pair,
                                 g->engine.processor.params[ToInt(p.index)],
                                 UiSizeId::Top2KnobsGapX);
        p.extra_label = layout::CreateItem(g->layout,
                                           g->scratch_arena,
                                           {
                                               .parent = container,
                                               .size = {layout::k_fill_parent, body_font->font_size},
                                           });
    }

    layout::RunContext(g->layout);
    DEFER { layout::ResetContext(g->layout); };

    for (auto& p : hidden_params) {
        auto const& desc = k_param_descriptors[ToInt(p.index)];
        auto const& param = g->engine.processor.params[ToInt(p.index)];
        switch (desc.value_type) {
            case ParamValueType::Float: KnobAndLabel(g, param, p.pair, knobs::DefaultKnob(g->imgui)); break;
            case ParamValueType::Menu:
                buttons::PopupWithItems(g,
                                        param,
                                        p.pair.control,
                                        buttons::ParameterPopupButton(g->imgui, false));
                labels::Label(g, param, p.pair.label, labels::ParameterCentred(g->imgui, false));
                break;
            case ParamValueType::Bool:
            case ParamValueType::Int: PanicIfReached(); break;
        }

        auto const label_r = imgui.GetRegisteredAndConvertedRect(layout::GetRect(g->layout, p.extra_label));
        imgui.graphics->AddTextJustified(label_r,
                                         desc.ModuleString(),
                                         LiveCol(g->imgui, UiColMap::TopPanelTitleText),
                                         TextJustification::Centred);
    }
}

static void DoErrorsModal(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::ErrorWindowWidth, UiSizeId::ErrorWindowHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    auto font = imgui.graphics->context->CurrentFont();

    if (imgui.BeginWindowPopup(settings, IdForModal(ModalWindowType::LoadError), r, "ErrorModal")) {
        DEFER { imgui.EndWindow(); };

        f32 y_pos = 0;
        auto text_style = labels::ErrorWindowLabel(imgui);

        auto const error_window_gap_after_desc = LiveSize(imgui, UiSizeId::ErrorWindowGapAfterDesc);
        auto const error_window_divider_spacing_y = LiveSize(imgui, UiSizeId::ErrorWindowDividerSpacingY);

        // title
        DoHeading(g, y_pos, "Errors");

        // new error list
        int num_errors = 0;
        {

            for (auto& errors :
                 Array {&g->engine.error_notifications, &g->shared_engine_systems.error_notifications}) {
                errors->ForEach([&](ThreadsafeErrorNotifications::Item const& e)
                                    -> ThreadsafeErrorNotifications::ItemIterationResult {
                    imgui.PushID((uintptr)e.id.Load(LoadMemoryOrder::Acquire));
                    DEFER { imgui.PopID(); };

                    // divider line
                    if (num_errors > 0) {
                        y_pos += (f32)error_window_gap_after_desc;
                        auto line_r = Rect {.x = 0, .y = y_pos, .w = imgui.Width(), .h = 1};
                        imgui.RegisterAndConvertRect(&line_r);
                        imgui.graphics->AddLine(line_r.Min(), line_r.Max(), text_style.main_cols.reg);
                        y_pos += (f32)error_window_divider_spacing_y;
                    }

                    // title
                    {
                        imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Heading2)]);
                        auto const error_window_item_h = LiveSize(imgui, UiSizeId::ErrorWindowItemH);
                        labels::Label(g,
                                      {.xywh {0, y_pos, imgui.Width(), (f32)error_window_item_h}},
                                      e.title,
                                      text_style);
                        imgui.graphics->context->PopFont();

                        y_pos += (f32)error_window_item_h;
                    }

                    // desc
                    {
                        DynamicArray<char> error_text {g->scratch_arena};
                        if (e.message.size) dyn::AppendSpan(error_text, e.message);
                        if (e.error_code) {
                            if (error_text.size) dyn::Append(error_text, '\n');
                            fmt::Append(error_text, "{u}.", *e.error_code);
                        }

                        auto const max_width = imgui.Width() * 0.95f;
                        auto const size = draw::GetTextSize(font, error_text, max_width);
                        auto desc_r = Rect {.x = 0, .y = y_pos, .w = size.x, .h = size.y};
                        imgui.RegisterAndConvertRect(&desc_r);
                        imgui.graphics->AddText(font,
                                                font->font_size,
                                                desc_r.pos,
                                                text_style.main_cols.reg,
                                                error_text,
                                                max_width);
                        y_pos += size.y + (f32)error_window_gap_after_desc;
                    }

                    // buttons
                    if (DoButton(g, "Dismiss", y_pos, 0))
                        return ThreadsafeErrorNotifications::ItemIterationResult::Remove;

                    ++num_errors;
                    return ThreadsafeErrorNotifications::ItemIterationResult::Continue;
                });
            }
        }

        // Add space to the bottom of the scroll window
        imgui.GetRegisteredAndConvertedRect(
            {.xywh {0, y_pos, 1, imgui.graphics->context->CurrentFontSize()}});

        if (!num_errors) imgui.ClosePopupToLevel(0);
    }
}

static void DoLoadingOverlay(Gui* g) {
    g->frame_input.graphics_ctx->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;

    auto const r = ModalRect(imgui, UiSizeId::LoadingOverlayBoxWidth, UiSizeId::LoadingOverlayBoxHeight);
    auto const settings = ModalWindowSettings(g->imgui);

    if (g->engine.pending_state_change) {
        imgui.BeginWindow(settings, r, "LoadingModal");
        DEFER { imgui.EndWindow(); };

        f32 y_pos = 0;
        DoHeading(g, y_pos, "Loading...", TextJustification::Centred);
    }
}

// ===============================================================================================================

static bool AnyModalOpen(imgui::Context& imgui) {
    for (auto const i : Range(ToInt(ModalWindowType::Count)))
        if (imgui.IsPopupOpen(IdForModal((ModalWindowType)i))) return true;
    return false;
}

// ===============================================================================================================

void OpenModalIfNotAlready(imgui::Context& imgui, ModalWindowType type) {
    if (!imgui.IsPopupOpen(IdForModal(type))) {
        imgui.ClosePopupToLevel(0);
        imgui.OpenPopup(IdForModal(type));
    }
}

void DoModalWindows(Gui* g) {
    if (AnyModalOpen(g->imgui)) DoOverlayClickableBackground(g);
    DoErrorsModal(g);
    DoLoadingOverlay(g);
    DoLegacyParamsModal(g);
}
