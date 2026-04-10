// Copyright 2018-2026 Sam Windell
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
    auto const rounding = rounding_opt ? *rounding_opt : WwToPixels(k_corner_rounding);
    auto const blur = WwToPixels(7.84f);
    imgui.draw_list->AddDropShadow(r.Min(), r.Max(), LiveCol(UiColMap::ViewportDropShadow), blur, rounding);
}

void DrawVoiceMarkerLine(imgui::Context const& imgui,
                         f32x2 pos,
                         f32 height,
                         f32 left_min,
                         Optional<Line> upper_line_opt,
                         VoiceMarkerLineOptions const& options) {
    {
        f32 const tail_size = Min(pos.x - left_min, WwToPixels(8.0f));

        if (tail_size > 1) {
            auto const aa = imgui.draw_list->renderer.anti_aliased_lines;
            DEFER { imgui.draw_list->renderer.anti_aliased_lines = aa; };
            imgui.draw_list->renderer.anti_aliased_lines = false;

            auto const darkened_col = ChangeBrightness(options.col, 0.7f);
            auto const col = ChangeAlpha(darkened_col, options.opacity * 0.25f);
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
        }
    }

    {
        auto const aa = imgui.draw_list->renderer.anti_aliased_lines;
        DEFER { imgui.draw_list->renderer.anti_aliased_lines = aa; };
        imgui.draw_list->renderer.anti_aliased_lines = false;

        auto const col = ChangeAlpha(options.col, options.opacity);
        imgui.draw_list->AddLine(pos, pos + f32x2 {0, height}, col);
    }
}

void DrawParameterTextInput(imgui::Context const& imgui, Rect r, imgui::TextInputResult const& result) {
    auto const font = imgui.draw_list->fonts.Current();

    auto const text_pos = result.text_pos;
    auto const w = Max(r.w, font->CalcTextSize(result.text, {}).x);
    Rect const background_r {.xywh {r.CentreX() - (w / 2), text_pos.y, w, font->font_size}};
    auto const rounding = WwToPixels(k_corner_rounding);

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
    auto const outer_arc_thickness = WwToPixels(2.6f);
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
    auto const inner_arc_radius_mid = outer_arc_radius_mid - WwToPixels(9.2f);
    auto const inner_arc_thickness = WwToPixels(5.6f);
    imgui.draw_list->PathArcTo(c, inner_arc_radius_mid, start_radians, end_radians, 32);
    imgui.draw_list->PathStroke(inner_arc_col, false, inner_arc_thickness);

    // cursor
    if (!options.is_fake) {
        auto const line_weight = WwToPixels(2.3f);

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

void DrawVerticalSlider(imgui::Context& imgui,
                        imgui::Id id,
                        Rect r,
                        f32 percent,
                        DrawVerticalSliderOptions const& options) {
    ASSERT(percent >= 0 && percent <= 1);

    auto const mid_panel_colours = options.style_system == GuiStyleSystem::MidPanel;
    auto const rounding = WwToPixels(k_corner_rounding);
    auto const is_interacting =
        !options.is_fake && (imgui.IsHot(id) || imgui.IsActive(id, MouseButton::Left));

    // Thin channel background (centred within the full rect)
    auto const channel_col = LiveCol(mid_panel_colours ? UiColMap::SliderMidChannel : UiColMap::KnobInnerArc);
    auto const channel_width = WwToPixels(4.0f);
    auto const channel_x = r.x + ((r.w - channel_width) / 2);
    Rect const channel_r {.x = channel_x, .y = r.y, .w = channel_width, .h = r.h};
    imgui.draw_list->AddRectFilled(channel_r, channel_col, rounding);

    // Handle dimensions
    auto const handle_height = WwToPixels(8.0f);
    auto const handle_pad = WwToPixels(1.0f);
    auto const usable_height = r.h - handle_height;
    auto const handle_y = r.y + ((1 - percent) * usable_height);

    // Highlight fill inside the channel showing the modulated value
    if (!options.is_fake) {
        auto const handle_centre_y = handle_y + (handle_height / 2);
        auto highlight_col = options.highlight_col;
        if (options.greyed_out) highlight_col = WithAlphaU8(highlight_col, 105);

        if (options.modulation_percent) {
            // Modulation: fill from modulated position to bottom of channel
            auto const mod_percent = Clamp(*options.modulation_percent, 0.0f, 1.0f);
            auto const mod_y = r.y + ((1 - mod_percent) * usable_height) + (handle_height / 2);
            auto const fill_top = mod_y;
            auto const fill_bottom = channel_r.Bottom();
            if (fill_bottom > fill_top) {
                Corners const corners = fill_top <= channel_r.y ? 0b1111 : 0b0011;
                imgui.draw_list->AddRectFilled(f32x2 {channel_r.x, Max(fill_top, channel_r.y)},
                                               f32x2 {channel_r.Right(), fill_bottom},
                                               highlight_col,
                                               rounding,
                                               corners);
            }
        } else {
            // No modulation: fill from handle to bottom of channel
            auto const fill_top = handle_centre_y;
            auto const fill_bottom = channel_r.Bottom();
            if (fill_bottom > fill_top) {
                imgui.draw_list->AddRectFilled(f32x2 {channel_r.x, fill_top},
                                               f32x2 {channel_r.Right(), fill_bottom},
                                               highlight_col,
                                               rounding,
                                               0b0011);
            }
        }
    }

    // Handle (wider than the channel)
    if (!options.is_fake) {
        Rect const handle_r {
            .x = r.x + handle_pad,
            .y = handle_y,
            .w = r.w - (handle_pad * 2),
            .h = handle_height,
        };

        auto handle_col = options.line_col;
        if (is_interacting)
            handle_col = LiveCol(mid_panel_colours ? UiColMap::KnobMidLineHover : UiColMap::KnobLineHover);

        // Handle drop shadow
        auto const handle_rounding = WwToPixels(1.0f);
        auto const shadow_offset = WwToPixels(1.0f);
        auto const shadow_col = LiveCol(UiColMap::SliderMidHandleShadow);
        Rect const shadow_r {
            .x = handle_r.x,
            .y = handle_r.y + shadow_offset,
            .w = handle_r.w,
            .h = handle_r.h,
        };
        imgui.draw_list->AddRectFilled(shadow_r, shadow_col, handle_rounding);

        // Handle body
        imgui.draw_list->AddRectFilled(handle_r, handle_col, handle_rounding);

        // Centre groove line on the handle
        auto const groove_y = handle_r.y + (handle_r.h / 2);
        auto const groove_inset = WwToPixels(1.5f);
        auto const groove_col = WithAlphaU8(channel_col, 120);
        imgui.draw_list->AddLine({handle_r.x + groove_inset, groove_y},
                                 {handle_r.Right() - groove_inset, groove_y},
                                 groove_col,
                                 WwToPixels(1.0f));
    }
}

void DrawPeakMeter(imgui::Context& imgui,
                   Rect r,
                   StereoPeakMeter const& level,
                   DrawPeakMeterOptions const& options) {
    // Snap origin to pixel boundary. All positions below are origin + integer offset.
    auto const origin_x = Round(r.x);
    auto const origin_y = Round(r.y);
    auto const total_w = (int)Round(r.w);
    auto const total_h = (int)Round(r.h);

    auto const snapshot = level.GetSnapshot();
    auto const v = snapshot.levels;
    auto const did_clip = options.flash_when_clipping && level.DidClipRecently();

    constexpr f32 k_max_db = 10;
    constexpr f32 k_min_db = -60;
    constexpr f32 k_min_amp = constexpr_math::Powf(10, k_min_db / 20);

    // All layout values as integer pixel offsets.
    int const marker_w = (int)WwToPixels(5.7f);
    int const marker_pad = (int)WwToPixels(1.8f);
    int const pad_left = options.show_db_markers ? marker_w : 0;
    int const pad_right = options.show_db_markers ? marker_w : 0;
    int const meter_w = total_w - pad_left - pad_right;
    int const gap = options.gap_px;
    int const chan_w = (meter_w - gap) / 2;
    int const l_chan_x = pad_left;
    int const r_chan_x = pad_left + chan_w + gap;

    auto const rounding_full = WwToPixels(k_corner_rounding);
    auto const small = chan_w < (int)(rounding_full * 2);
    auto const rounding = small ? 0.0f : rounding_full;
    auto const saved_aa = imgui.draw_list->renderer.anti_aliased_shapes;
    if (small) imgui.draw_list->renderer.anti_aliased_shapes = false;
    DEFER { imgui.draw_list->renderer.anti_aliased_shapes = saved_aa; };

    // Background channels.
    imgui.draw_list->AddRectFilled(f32x2 {origin_x + (f32)l_chan_x, origin_y},
                                   f32x2 {origin_x + (f32)(l_chan_x + chan_w), origin_y + (f32)total_h},
                                   LiveCol(UiColMap::PeakMeterBack),
                                   rounding);
    imgui.draw_list->AddRectFilled(f32x2 {origin_x + (f32)r_chan_x, origin_y},
                                   f32x2 {origin_x + (f32)(r_chan_x + chan_w), origin_y + (f32)total_h},
                                   LiveCol(UiColMap::PeakMeterBack),
                                   rounding);

    // dB markers.
    if (options.show_db_markers) {
        auto draw_marker = [&](f32 db, bool bold) {
            int const y = (int)((1 - MapTo01(db, k_min_db, k_max_db)) * (f32)total_h);
            auto const col =
                bold ? LiveCol(UiColMap::PeakMeterMarkersBold) : LiveCol(UiColMap::PeakMeterMarkers);
            imgui.draw_list->AddLine(f32x2 {origin_x, origin_y + y},
                                     f32x2 {origin_x + (marker_w - marker_pad), origin_y + y},
                                     col);
            imgui.draw_list->AddLine(f32x2 {origin_x + total_w - (marker_w - marker_pad), origin_y + y},
                                     f32x2 {origin_x + total_w, origin_y + y},
                                     col);
        };

        draw_marker(0, true);
        draw_marker(-12, false);
        draw_marker(-24, false);
        draw_marker(-36, false);
        draw_marker(-48, false);
    }

    // Level positions as integer y-offsets from origin.
    auto const clamped_v = Max(v, f32x2(k_min_amp));
    auto const v_db = 20 * Log10(clamped_v);
    auto const v_perceived = Clamp<f32x2>(MapTo01Unchecked<f32x2>(v_db, k_min_db, k_max_db), 0, 1);
    int const level_y_l = total_h - (int)(v_perceived[0] * (f32)total_h);
    int const level_y_r = total_h - (int)(v_perceived[1] * (f32)total_h);

    // Segment boundaries as integer y-offsets from origin.
    int const top_seg_y = (int)((1 - MapTo01(0.0f, k_min_db, k_max_db)) * (f32)total_h);
    int const mid_seg_y = (int)((1 - MapTo01(-12.0f, k_min_db, k_max_db)) * (f32)total_h);

    // Draw level segments for each channel.
    int const chan_xs[] = {l_chan_x, r_chan_x};
    int const level_ys[] = {level_y_l, level_y_r};
    for (int i = 0; i < 2; i++) {
        int const cx = chan_xs[i];
        int const ly = level_ys[i];
        if (ly >= total_h) continue;

        auto const x0 = origin_x + (f32)cx;
        auto const x1 = origin_x + (f32)(cx + chan_w);

        // Top segment (above 0dB line).
        if (ly < top_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightTop);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            int const bottom = Min(total_h, top_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + ly}, f32x2 {x1, origin_y + bottom}, col);
        }

        // Middle segment (0dB to -12dB).
        if (ly < mid_seg_y && total_h > top_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightMiddle);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            int const top = Max(ly, top_seg_y);
            int const bottom = Min(total_h, mid_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + top}, f32x2 {x1, origin_y + bottom}, col);
        }

        // Bottom segment (below -12dB).
        if (total_h > mid_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightBottom);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            int const top = Max(ly, mid_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + top},
                                           f32x2 {x1, origin_y + total_h},
                                           col,
                                           rounding,
                                           0b0011);
        }
    }
}

void DrawMidPanelScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    for (auto const b : bars) {
        if (!b) continue;
        auto const rounding = WwToPixels(k_corner_rounding);
        imgui.draw_list->AddRectFilled(b->strip, LiveCol(UiColMap::ScrollbarBack), rounding);
        u32 handle_col = LiveCol(UiColMap::ScrollbarHandle);
        if (imgui.IsHot(b->id))
            handle_col = LiveCol(UiColMap::ScrollbarHandleHover);
        else if (imgui.IsActive(b->id, MouseButton::Left))
            handle_col = LiveCol(UiColMap::ScrollbarHandleActive);
        imgui.draw_list->AddRectFilled(b->handle, handle_col, rounding);
    }
}

static void DrawModalScrollbarsWithMode(imgui::Context const& imgui,
                                        imgui::ViewportScrollbars const& bars,
                                        bool dark_mode) {
    for (auto const b : bars) {
        if (!b) continue;
        if (imgui.IsViewportHovered(imgui.curr_viewport) || imgui.IsActive(b->id, MouseButton::Left)) {
            auto const hot_or_active = imgui.IsHotOrActive(b->id, MouseButton::Left);
            auto const rounding = WwToPixels(k_panel_rounding);

            // Channel.
            if (hot_or_active) {
                u32 col = ToU32({.c = Col::Background2, .dark_mode = dark_mode});
                imgui.draw_list->AddRectFilled(b->strip, col, rounding);
            }

            // Handle.
            {
                auto handle_rect = b->handle;
                u32 handle_col = ToU32({.c = Col::Surface1, .dark_mode = dark_mode});
                if (hot_or_active) handle_col = ToU32({.c = Col::Overlay0, .dark_mode = dark_mode});
                if (imgui.curr_viewport->cfg.scrollbar_inside_padding) {
                    auto const pad_l = WwToPixels(hot_or_active ? 1 : 3.0f);
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

void DrawModalScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    DrawModalScrollbarsWithMode(imgui, bars, false);
}

void DrawModalScrollbarsDarkMode(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    DrawModalScrollbarsWithMode(imgui, bars, true);
}

void DrawModalViewportBackgroundWithFullscreenDim(imgui::Context const& imgui) {
    imgui.draw_list->PushClipRectFullScreen();
    imgui.draw_list->AddRectFilled(0, GuiIo().in.window_size.ToFloat2(), 0x6c0f0d0d);
    imgui.draw_list->PopClipRect();

    auto const rounding = WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    DrawDropShadow(imgui, r, rounding);
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background0}), rounding);
}

void DrawOverlayViewportBackground(imgui::Context const& imgui) {
    auto const rounding = WwToPixels(k_panel_rounding);
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

    auto const max_width = WwToPixels(244.f);
    auto const text_margin = WwToPixels(k_tooltip_pad);

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
                                           WwToPixels(k_corner_rounding));

    imgui.overlay_draw_list->AddText(popup_r.pos + text_margin,
                                     ToU32(Col {.c = Col::Text}),
                                     str,
                                     {.wrap_width = size + 1});
}
