// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_element_drawing.hpp"

#include "foundation/foundation.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"

void DrawDropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding_opt) {
    auto const rounding = rounding_opt ? *rounding_opt : LivePx(UiSizeId::CornerRounding);
    auto const blur = LivePx(UiSizeId::ViewportDropShadowBlur);
    imgui.draw_list->AddDropShadow(r.Min(), r.Max(), LiveCol(UiColMap::ViewportDropShadow), blur, rounding);
}

void DrawVoiceMarkerLine(imgui::Context const& imgui,
                         f32x2 pos,
                         f32 height,
                         f32 left_min,
                         Optional<Line> upper_line_opt,
                         f32 opacity) {
    {

        constexpr f32 k_tail_size_max = 10;
        f32 const tail_size = Min(pos.x - left_min, k_tail_size_max);

        if (tail_size > 1) {
            auto const aa = imgui.draw_list->renderer.fill_anti_alias;
            imgui.draw_list->renderer.fill_anti_alias = false;
            auto const darkened_col = ChangeBrightness(LiveCol(UiColMap::WaveformLoopVoiceMarkers), 0.7f);
            auto const col = WithAlphaU8(darkened_col, (u8)MapFrom01(opacity, 10, 40));
            auto const transparent_col = WithAlphaU8(darkened_col, 0);

            if (upper_line_opt) {
                auto& upper_line = *upper_line_opt;
                auto p0 = upper_line.IntersectionWithVerticalLine(pos.x - tail_size).ValueOr(upper_line.a);
                auto p1 = pos;
                auto p2 = pos + f32x2 {0, height};
                auto p3 = pos + f32x2 {p0.x - pos.x, height};

                imgui.draw_list
                    ->AddQuadFilledMultiColor(p0, p1, p2, p3, transparent_col, col, col, transparent_col);
            } else {
                auto left = Max(left_min, pos.x - tail_size);

                imgui.draw_list->AddRectFilledMultiColor({left, pos.y},
                                                         pos + f32x2 {0, height},
                                                         transparent_col,
                                                         col,
                                                         col,
                                                         transparent_col);
            }

            imgui.draw_list->renderer.fill_anti_alias = aa;
        }
    }

    {
        auto const aa = imgui.draw_list->renderer.anti_aliased_lines;
        imgui.draw_list->renderer.anti_aliased_lines = false;
        auto const col = WithAlphaU8(LiveCol(UiColMap::WaveformLoopVoiceMarkers), (u8)(opacity * 255.0f));

        imgui.draw_list->AddLine(pos, pos + f32x2 {0, height}, col);
        imgui.draw_list->renderer.anti_aliased_lines = aa;
    }
}

void DrawParameterTextInput(imgui::Context const& imgui, Rect r, imgui::TextInputResult const& result) {
    auto const font = imgui.draw_list->fonts.Current();

    auto const text_pos = result.text_pos;
    auto const w = Max(r.w, font->CalcTextSize(result.text, {}).x);
    Rect const background_r {.xywh {r.CentreX() - (w / 2), text_pos.y, w, font->font_size}};
    auto const rounding = LivePx(UiSizeId::CornerRounding);

    imgui.draw_list->AddRectFilled(background_r, LiveCol(UiColMap::KnobTextInputBack), rounding);
    imgui.draw_list->AddRect(background_r, LiveCol(UiColMap::KnobTextInputBorder), rounding);

    if (result.HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {.imgui = imgui};
        while (auto rect = result.NextSelectionRect(it))
            imgui.draw_list->AddRectFilled(*rect, LiveCol(UiColMap::TextInputSelection));
    }

    if (result.cursor_rect)
        imgui.draw_list->AddRectFilled(*result.cursor_rect, LiveCol(UiColMap::TextInputCursor));

    imgui.draw_list->AddText(text_pos, LiveCol(UiColMap::MidText), result.text, {});
}

void DrawTextInput(imgui::Context const& imgui,
                   imgui::TextInputResult const& result,
                   DrawTextInputConfig const& config) {
    if (result.HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {imgui};
        auto const selection_col = ToU32(config.selection_col);
        while (auto const r = result.NextSelectionRect(it))
            imgui.draw_list->AddRectFilled(*r, selection_col);
    }

    if (result.cursor_rect) imgui.draw_list->AddRectFilled(*result.cursor_rect, ToU32(config.cursor_col));

    imgui.draw_list->AddText(result.text_pos,
                             WithAlphaU8(ToU32(config.text_col), result.is_placeholder ? 140 : 255),
                             result.text,
                             {});
}

// The width is filled by the knob. The height is not actually used.
void DrawKnob(imgui::Context& imgui, imgui::Id id, Rect r, f32 percent, DrawKnobOptions const& options) {
    auto const c = f32x2 {r.CentreX(), r.y + (r.w / 2)};
    auto const outer_arc_percent = options.outer_arc_percent.ValueOr(percent);
    auto const start_radians = (3 * k_pi<>) / 4;
    auto const end_radians = k_tau<> + (k_pi<> / 4);
    auto const delta = end_radians - start_radians;
    auto const angle = start_radians + ((1 - percent) * delta);
    auto const angle2 = start_radians + (outer_arc_percent * delta);
    ASSERT(percent >= 0 && percent <= 1);
    ASSERT(outer_arc_percent >= 0 && outer_arc_percent <= 1);
    ASSERT(angle >= start_radians && angle <= end_radians);

    auto const mid_panel_colours = ({
        bool m;
        switch (options.style_system) {
            case GuiStyleSystem::MidPanel: m = true; break;
            case GuiStyleSystem::TopBottomPanels: m = false; break;
            case GuiStyleSystem::Overlay: Panic("overlay style not supported yet");
        }
        m;
    });

    auto inner_arc_col = LiveCol(mid_panel_colours ? UiColMap::KnobMidInnerArc : UiColMap::KnobInnerArc);
    auto bright_arc_col = options.highlight_col;
    if (options.greyed_out) {
        bright_arc_col = WithAlphaU8(bright_arc_col, 105);
        inner_arc_col =
            LiveCol(mid_panel_colours ? UiColMap::KnobMidInnerArcGreyedOut : UiColMap::KnobInnerArcGreyedOut);
    }
    auto line_col = options.line_col;
    if (!options.is_fake && (imgui.IsHot(id) || imgui.IsActive(id, MouseButton::Left))) {
        inner_arc_col =
            LiveCol(mid_panel_colours ? UiColMap::KnobMidInnerArcHover : UiColMap::KnobInnerArcHover);
        line_col = LiveCol(mid_panel_colours ? UiColMap::KnobMidLineHover : UiColMap::KnobLineHover);
    }

    // outer arc
    auto const outer_arc_thickness = LivePx(UiSizeId::KnobOuterArcWeight);
    auto const outer_arc_radius_mid = r.w * 0.5f;
    auto const empty_outer_arc_col =
        LiveCol(mid_panel_colours ? UiColMap::KnobMidOuterArcEmpty : UiColMap::KnobOuterArcEmpty);
    if (!options.overload_position) {
        imgui.draw_list->PathArcTo(c,
                                   outer_arc_radius_mid - (outer_arc_thickness / 2),
                                   start_radians,
                                   end_radians,
                                   32);
        imgui.draw_list->PathStroke(empty_outer_arc_col, false, outer_arc_thickness);
    } else {
        auto const overload_radians = start_radians + (delta * *options.overload_position);
        auto const radians_per_px = k_tau<> * r.w / 2;
        auto const desired_px_width = 15;
        auto const overload_radians_end = overload_radians + (desired_px_width / radians_per_px);

        {
            imgui.draw_list->PathArcTo(c,
                                       outer_arc_radius_mid - (outer_arc_thickness / 2),
                                       start_radians,
                                       overload_radians,
                                       32);
            imgui.draw_list->PathStroke(empty_outer_arc_col, false, outer_arc_thickness);
        }

        {
            auto const gain_thickness = outer_arc_thickness;
            imgui.draw_list->PathArcTo(c,
                                       outer_arc_radius_mid - (gain_thickness / 2) +
                                           (gain_thickness - outer_arc_thickness),
                                       overload_radians_end,
                                       end_radians,
                                       32);
            imgui.draw_list->PathStroke(LiveCol(mid_panel_colours ? UiColMap::KnobMidOuterArcOverload
                                                                  : UiColMap::KnobOuterArcOverload),
                                        false,
                                        gain_thickness);
        }
    }

    if (!options.is_fake) {
        if (!options.bidirectional) {
            imgui.draw_list->PathArcTo(c,
                                       outer_arc_radius_mid - (outer_arc_thickness / 2),
                                       start_radians,
                                       angle2,
                                       32);
        } else {
            auto const mid_radians = start_radians + (delta / 2);
            imgui.draw_list->PathArcTo(c,
                                       outer_arc_radius_mid - (outer_arc_thickness / 2),
                                       Min(mid_radians, angle2),
                                       Max(mid_radians, angle2),
                                       32);
        }
        imgui.draw_list->PathStroke(bright_arc_col, false, outer_arc_thickness);
    }

    // inner arc
    auto const inner_arc_radius_mid = outer_arc_radius_mid - LivePx(UiSizeId::KnobInnerArc);
    auto const inner_arc_thickness = LivePx(UiSizeId::KnobInnerArcWeight);
    imgui.draw_list->PathArcTo(c, inner_arc_radius_mid, start_radians, end_radians, 32);
    imgui.draw_list->PathStroke(inner_arc_col, false, inner_arc_thickness);

    // cursor
    if (!options.is_fake) {
        auto const line_weight = LivePx(UiSizeId::KnobLineWeight);

        auto const inner_arc_radius_outer = inner_arc_radius_mid + (inner_arc_thickness / 2);
        auto const inner_arc_radius_inner = inner_arc_radius_mid - (inner_arc_thickness / 2);

        f32x2 offset;
        offset.x = Sin(angle - (k_pi<> / 2));
        offset.y = Cos(angle - (k_pi<> / 2));
        auto const outer_point = c + (offset * f32x2 {inner_arc_radius_outer, inner_arc_radius_outer});
        auto const inner_point = c + (offset * f32x2 {inner_arc_radius_inner, inner_arc_radius_inner});

        imgui.draw_list->AddLine(inner_point, outer_point, line_col, line_weight);
    }
}

void DrawPeakMeter(imgui::Context& imgui,
                   Rect r,
                   StereoPeakMeter const& level,
                   DrawPeakMeterOptions const& options) {
    auto const snapshot = level.GetSnapshot();
    auto const v = snapshot.levels;
    auto const did_clip = options.flash_when_clipping && level.DidClipRecently();

    auto const gap = options.gap != 0 ? options.gap : LivePx(UiSizeId::PeakMeterGap);
    auto const marker_w = LivePx(UiSizeId::PeakMeterMarkerWidth);
    auto const marker_pad = LivePx(UiSizeId::PeakMeterMarkerPad);
    auto padded_r = options.show_db_markers
                        ? Rect {.x = r.x + marker_w, .y = r.y, .w = r.w - (marker_w * 2), .h = r.h}
                        : r;
    auto w = (padded_r.w / 2) - (gap / 2);

    constexpr f32 k_max_db = 10;
    constexpr f32 k_min_db = -60;
    constexpr f32 k_min_amp = constexpr_math::Powf(10, k_min_db / 20);

    auto const rounding = LivePx(UiSizeId::CornerRounding);

    {
        // constexpr auto k_channel_col = WithAlphaF(ToU32(ColType::Background0), 0.2f);
        {
            auto l_channel = padded_r;
            l_channel.w = w;
            imgui.draw_list->AddRectFilled(l_channel, LiveCol(UiColMap::PeakMeterBack), rounding);
        }
        {
            auto r_channel = padded_r;
            r_channel.x += w + gap;
            r_channel.w = w;
            imgui.draw_list->AddRectFilled(r_channel, LiveCol(UiColMap::PeakMeterBack), rounding);
        }

        if (options.show_db_markers) {
            auto draw_marker = [&](f32 db, bool bold) {
                f32 const pos = MapTo01(db, k_min_db, k_max_db);
                auto const line_y = padded_r.y + ((1 - pos) * padded_r.h);
                imgui.draw_list->AddLine({r.x, line_y},
                                         {r.x + (marker_w - marker_pad), line_y},
                                         bold ? LiveCol(UiColMap::PeakMeterMarkersBold)
                                              : LiveCol(UiColMap::PeakMeterMarkers));
                imgui.draw_list->AddLine({r.Right() - (marker_w - marker_pad), line_y},
                                         {r.Right(), line_y},
                                         bold ? LiveCol(UiColMap::PeakMeterMarkersBold)
                                              : LiveCol(UiColMap::PeakMeterMarkers));
            };

            draw_marker(0, true);
            draw_marker(-12, false);
            draw_marker(-24, false);
            draw_marker(-36, false);
            draw_marker(-48, false);
        }
    }

    auto const clamped_v = Max(v, f32x2(k_min_amp)); // Ensure we don't Log10 zero.
    auto const v_db = 20 * Log10(clamped_v);
    auto const v_percieved = Clamp<f32x2>(MapTo01Unchecked<f32x2>(v_db, k_min_db, k_max_db), 0, 1);
    auto const pixels = v_percieved * padded_r.h;
    auto const level_y_pos = padded_r.y + (padded_r.h - pixels);

    auto l_r = padded_r;
    l_r.y = level_y_pos[0];
    l_r.w = w;
    l_r.SetBottomByResizing(padded_r.Bottom());

    auto r_r = padded_r;
    r_r.x += w + gap;
    r_r.y = level_y_pos[1];
    r_r.w = w;
    r_r.SetBottomByResizing(padded_r.Bottom());

    Array<Rect, 2> const channel_rs = {l_r, r_r};

    auto const top_segment_line = padded_r.y + ((1 - MapTo01(0.0f, k_min_db, k_max_db)) * padded_r.h);
    auto const mid_segment_line = padded_r.y + ((1 - MapTo01(-12.0f, k_min_db, k_max_db)) * padded_r.h);
    for (auto& chan_r : channel_rs) {
        if (chan_r.h < 1) continue;

        if (chan_r.y < top_segment_line) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightTop);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            imgui.draw_list->AddRectFilled(chan_r, col);
        }

        if (chan_r.y < mid_segment_line) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightMiddle);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            auto const top = Max(chan_r.y, top_segment_line);
            imgui.draw_list->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col);
        }

        auto col = LiveCol(UiColMap::PeakMeterHighlightBottom);
        if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
        auto const top = Max(chan_r.y, mid_segment_line);
        imgui.draw_list->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col, rounding, 0b0011);
    }
}

void DrawMidPanelScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    for (auto const b : bars) {
        if (!b) continue;
        auto const rounding = LivePx(UiSizeId::CornerRounding);
        imgui.draw_list->AddRectFilled(b->strip, LiveCol(UiColMap::ScrollbarBack), rounding);
        u32 handle_col = LiveCol(UiColMap::ScrollbarHandle);
        if (imgui.IsHot(b->id))
            handle_col = LiveCol(UiColMap::ScrollbarHandleHover);
        else if (imgui.IsActive(b->id, MouseButton::Left))
            handle_col = LiveCol(UiColMap::ScrollbarHandleActive);
        imgui.draw_list->AddRectFilled(b->handle, handle_col, rounding);
    }
}

void DrawModalScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    for (auto const b : bars) {
        if (!b) continue;
        if (imgui.IsViewportHovered(imgui.curr_viewport) || imgui.IsActive(b->id, MouseButton::Left)) {
            auto const hot_or_active = imgui.IsHotOrActive(b->id, MouseButton::Left);
            auto const rounding = GuiIo().WwToPixels(k_panel_rounding);

            // Channel.
            if (hot_or_active) {
                u32 col = ToU32({.c = Col::Background2});
                imgui.draw_list->AddRectFilled(b->strip, col, rounding);
            }

            // Handle.
            {
                auto handle_rect = b->handle;
                u32 handle_col = ToU32({.c = Col::Surface1});
                if (hot_or_active) handle_col = ToU32({.c = Col::Overlay0});
                if (imgui.curr_viewport->cfg.scrollbar_inside_padding) {
                    auto const pad_l = GuiIo().WwToPixels(hot_or_active ? 1 : 3.0f);
                    auto const pad_r = 0;
                    auto const total_pad = pad_l + pad_r;
                    if (handle_rect.w > total_pad) {
                        handle_rect.x += pad_l;
                        handle_rect.w -= total_pad;
                    }
                }
                imgui.draw_list->AddRectFilled(handle_rect, handle_col, rounding);
            }
        }
    }
}

void DrawModalViewportBackgroundWithFullscreenDim(imgui::Context const& imgui) {
    imgui.draw_list->PushClipRectFullScreen();
    imgui.draw_list->AddRectFilled(0, GuiIo().in.window_size.ToFloat2(), 0x6c0f0d0d);
    imgui.draw_list->PopClipRect();

    auto const rounding = GuiIo().WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    DrawDropShadow(imgui, r, rounding);
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background0}), rounding);
}

void DrawOverlayViewportBackground(imgui::Context const& imgui) {
    auto const rounding = GuiIo().WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    DrawDropShadow(imgui, r, rounding);
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background0}), rounding);
}

void DrawOverlayTooltipForRect(imgui::Context const& imgui,
                               Fonts& fonts,
                               String str,
                               DrawTooltipArgs const& args) {
    fonts.Push(ToInt(FontType::Body));
    DEFER { fonts.Pop(); };

    auto const max_width = LivePx(UiSizeId::TooltipMaxWidth);
    auto const text_margin = f32x2 {LivePx(UiSizeId::TooltipPadX), LivePx(UiSizeId::TooltipPadY)};

    auto const wrapped_size = fonts.CalcTextSize(str, {.wrap_width = max_width});

    auto const size = Min(max_width, wrapped_size.x);

    Rect popup_r {
        .pos = args.r.pos,
        .size = f32x2 {size, wrapped_size.y} + (text_margin * 2),
    };

    if (args.justification == TooltipJustification::AboveOrBelow) {
        popup_r.y += args.r.h;
        popup_r.x = popup_r.x + ((args.r.w / 2) - (popup_r.w / 2));
    } else {
        popup_r.y = popup_r.y + ((args.r.h / 2) - (popup_r.h / 2));
    }

    popup_r.pos = imgui::BestPopupPos(popup_r,
                                      args.avoid_r,
                                      GuiIo().in.window_size.ToFloat2(),
                                      args.justification == TooltipJustification::LeftOrRight
                                          ? imgui::PopupJustification::LeftOrRight
                                          : imgui::PopupJustification::AboveOrBelow);

    DrawDropShadow(imgui, popup_r);

    imgui.overlay_draw_list->AddRectFilled(popup_r,
                                           ToU32(Col {.c = Col::Background0}),
                                           LivePx(UiSizeId::CornerRounding));

    imgui.overlay_draw_list->AddText(popup_r.pos + text_margin,
                                     ToU32(Col {.c = Col::Text}),
                                     str,
                                     {.wrap_width = size + 1});
}
