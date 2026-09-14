// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/elements/gui_element_drawing.hpp"

#include <IconsFontAwesome6.h>

#include "foundation/foundation.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"

void DrawDropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding_opt, f32 opacity) {
    auto const rounding = rounding_opt ? *rounding_opt : WwToPixels(k_corner_rounding);
    auto const blur = WwToPixels(7.84f);
    imgui.draw_list->AddDropShadow(r.Min(),
                                   r.Max(),
                                   ChangeAlpha(LiveCol(UiColMap::ViewportDropShadow), opacity),
                                   blur,
                                   rounding);
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

void DrawParameterTextInput(imgui::Context const& imgui,
                            Rect r,
                            imgui::TextInputResult const& result,
                            DrawParameterTextInputOptions const& options) {
    auto const font = imgui.draw_list->fonts.Current();

    auto const text_pos = result.text_pos;
    auto const w = Max(r.w, font->CalcTextSize(result.text, {}).x);
    Rect const background_r {.xywh {r.CentreX() - (w / 2), text_pos.y, w, font->font_size}};
    auto const rounding = WwToPixels(k_corner_rounding);

    auto const back_col =
        options.dark_mode ? LiveCol(UiColMap::KnobTextInputBack) : ToU32(Col {.c = Col::Background2});
    auto const border_col =
        options.dark_mode ? LiveCol(UiColMap::KnobTextInputBorder) : ToU32(Col {.c = Col::Overlay1});
    auto const selection_col = options.dark_mode ? LiveCol(UiColMap::TextInputSelection)
                                                 : ToU32(Col {.c = Col::Highlight, .alpha = 128});
    auto const cursor_col =
        options.dark_mode ? LiveCol(UiColMap::TextInputCursor) : ToU32(Col {.c = Col::Text});
    auto const text_col = options.dark_mode ? LiveCol(UiColMap::MidText) : ToU32(Col {.c = Col::Text});

    imgui.draw_list->AddRectFilled(background_r, back_col, rounding);
    if (options.draw_border) imgui.draw_list->AddRect(background_r, border_col, rounding);

    if (result.HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {.imgui = imgui};
        while (auto rect = result.NextSelectionRect(it))
            imgui.draw_list->AddRectFilled(*rect, selection_col);
    }

    if (result.cursor_rect) imgui.draw_list->AddRectFilled(*result.cursor_rect, cursor_col);

    imgui.draw_list->AddText(text_pos, text_col, result.text, {});
}

void DrawTextInput(imgui::Context& imgui,
                   imgui::TextInputResult const& result,
                   DrawTextInputConfig const& config) {
    if (result.clip_rect) imgui.PushRectToCurrentScissorStack(*result.clip_rect);
    DEFER {
        if (result.clip_rect) imgui.PopRectFromCurrentScissorStack();
    };

    if (result.HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {imgui};
        auto const selection_col = ToU32(config.selection_col);
        while (auto const r = result.NextSelectionRect(it))
            imgui.draw_list->AddRectFilled(*r, selection_col);
    }

    if (result.cursor_rect) imgui.draw_list->AddRectFilled(*result.cursor_rect, ToU32(config.cursor_col));

    auto const text_col = WithAlphaU8(ToU32(config.text_col), result.is_placeholder ? 140 : 255);
    if (result.visual_rows.size) {
        auto const font_size = imgui.draw_list->fonts.Current()->font_size;
        for (auto const row_index : Range(result.visual_rows.size))
            imgui.draw_list->AddText(result.text_pos + f32x2 {0, (f32)row_index * font_size},
                                     text_col,
                                     result.visual_rows[row_index],
                                     {});
    } else {
        imgui.draw_list->AddText(result.text_pos,
                                 text_col,
                                 result.text,
                                 {.wrap_width = result.multiline_wrap_width});
    }
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
    if (!options.is_fake && !options.greyed_out &&
        (imgui.IsHot(id) || imgui.IsActive(id, MouseButton::Left))) {
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

    // voice blips: thin radial ticks across the outer arc, same colour as waveform voice markers
    if (!options.is_fake) {
        auto const blip_col = LiveCol(UiColMap::WaveformLoopVoiceMarkers);
        auto const blip_arc_radius = outer_arc_radius_mid - (outer_arc_thickness / 2);
        auto const blip_half_length = outer_arc_thickness / 2;
        for (auto const blip : options.voice_blips_01) {
            auto const blip_angle = start_radians + (Clamp01(blip) * delta);
            f32x2 const direction {Cos(blip_angle), Sin(blip_angle)};
            auto const inner_radius = blip_arc_radius - blip_half_length;
            auto const outer_radius = blip_arc_radius + blip_half_length;
            imgui.draw_list->AddLine(c + (direction * f32x2 {inner_radius, inner_radius}),
                                     c + (direction * f32x2 {outer_radius, outer_radius}),
                                     blip_col,
                                     WwToPixels(1.0f));
        }
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

    // Voice blips: thin horizontal lines across the channel, same colour as waveform voice markers. Drawn
    // before the handle so they pass underneath it. Pixel-snapped filled rects rather than AddLine so that
    // anti-aliasing doesn't feather them past the channel edges.
    if (!options.is_fake) {
        auto const blip_col = LiveCol(UiColMap::WaveformLoopVoiceMarkers);
        auto const blip_thickness = Max(1.0f, Round(WwToPixels(1.0f)));
        auto const blip_left = Round(channel_r.x);
        auto const blip_right = Round(channel_r.Right());
        for (auto const blip : options.voice_blips_01) {
            auto const blip_y = Round(r.y + ((1 - Clamp01(blip)) * usable_height) + (handle_height / 2) -
                                      (blip_thickness / 2));
            imgui.draw_list->AddRectFilled(f32x2 {blip_left, blip_y},
                                           f32x2 {blip_right, blip_y + blip_thickness},
                                           blip_col);
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
                   StereoPeakMeter const* level,
                   DrawPeakMeterOptions const& options) {
    ASSERT(level);

    // Snap origin to pixel boundary. All positions below are origin + integer offset.
    auto const origin_x = Round(r.x);
    auto const origin_y = Round(r.y);
    auto const total_w = (s32)Round(r.w);
    auto const total_h = (s32)Round(r.h);

    auto const k_max_db = options.max_db;
    auto const k_min_db = options.min_db;

    // All layout values as integer pixel offsets.
    auto const marker_w = (s32)WwToPixels(5.7f);
    auto const marker_pad = (s32)WwToPixels(1.8f);
    auto const pad_left = options.show_db_markers ? marker_w : 0;
    auto const pad_right = options.show_db_markers ? marker_w : 0;
    auto const meter_w = total_w - pad_left - pad_right;
    auto const gap = options.gap_px;
    constexpr auto k_num_channels = 2;

    auto const chan_w = (meter_w - gap) / 2;
    s32 const chan_xs[2] = {pad_left, pad_left + chan_w + gap};

    auto const rounding_full = WwToPixels(k_corner_rounding);
    auto const small = chan_w < (s32)(rounding_full * 2);
    auto const rounding = small ? 0.0f : rounding_full;
    auto const saved_aa = imgui.draw_list->renderer.anti_aliased_shapes;
    if (small) imgui.draw_list->renderer.anti_aliased_shapes = false;
    DEFER { imgui.draw_list->renderer.anti_aliased_shapes = saved_aa; };

    // Segment boundaries as integer y-offsets from origin.
    auto const top_seg_y = (s32)((1 - MapTo01(0.0f, k_min_db, k_max_db)) * (f32)total_h);
    auto const mid_seg_y =
        (s32)((1 - MapTo01(options.yellow_zone_min_db, k_min_db, k_max_db)) * (f32)total_h);

    // Background channels. The region above 0dB gets a subtly brighter background to hint at the overload
    // range. The two regions are drawn adjacent (not overlaid) so the translucent background isn't
    // double-blended.
    auto const back_col = LiveCol(UiColMap::PeakMeterBack);
    if (options.show_warning_zones) {
        // The region above 0dB gets a subtly brighter background to hint at the overload range. The two
        // regions are drawn adjacent (not overlaid) so the translucent background isn't double-blended.
        auto const top_back_col = ToU32({.c = Col::Black, .alpha = 44});
        auto const split_y = Clamp(top_seg_y, 0, total_h);
        for (auto const chan_index : Range(k_num_channels)) {
            auto const cx = chan_xs[chan_index];
            auto const x0 = origin_x + (f32)cx;
            auto const x1 = origin_x + (f32)(cx + chan_w);

            // Above 0dB (rounded top corners).
            if (split_y > 0)
                imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y},
                                               f32x2 {x1, origin_y + (f32)split_y},
                                               top_back_col,
                                               rounding,
                                               0b1100);
            // Below 0dB (rounded bottom corners).
            if (split_y < total_h)
                imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + (f32)split_y},
                                               f32x2 {x1, origin_y + (f32)total_h},
                                               back_col,
                                               rounding,
                                               0b0011);
        }
    } else {
        for (auto const chan_index : Range(k_num_channels)) {
            auto const cx = chan_xs[chan_index];
            auto const x0 = origin_x + (f32)cx;
            auto const x1 = origin_x + (f32)(cx + chan_w);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y},
                                           f32x2 {x1, origin_y + (f32)total_h},
                                           back_col,
                                           rounding,
                                           0b1111);
        }
    }

    // dB markers.
    if (options.show_db_markers) {
        auto draw_marker = [&](f32 db, bool bold) {
            auto const y = (s32)((1 - MapTo01(db, k_min_db, k_max_db)) * (f32)total_h);
            auto const col =
                bold ? LiveCol(UiColMap::PeakMeterMarkersBold) : LiveCol(UiColMap::PeakMeterMarkers);
            imgui.draw_list->AddLine(f32x2 {origin_x, origin_y + y},
                                     f32x2 {origin_x + (marker_w - marker_pad), origin_y + y},
                                     col);
            imgui.draw_list->AddLine(f32x2 {origin_x + total_w - (marker_w - marker_pad), origin_y + y},
                                     f32x2 {origin_x + total_w, origin_y + y},
                                     col);
        };

        for (f32 db = 0; db > k_min_db; db -= options.marker_interval_db)
            draw_marker(db, db == 0);

        if (options.show_min_max_markers) {
            draw_marker(k_max_db, false);
            draw_marker(k_min_db, false);
        }
    }

    auto const snapshot = level->GetSnapshot();
    auto const v = snapshot.levels;
    auto const did_clip = options.flash_when_clipping && level->DidClipRecently();

    constexpr f32 k_peak_min_db = -60;
    constexpr f32 k_min_amp = constexpr_math::Powf(10, k_peak_min_db / 20);

    // The processor stops notifying us when it goes silent, so keep animating while there's anything to
    // show; that way we pick up the meter being zeroed instead of leaving a stale level on screen.
    if (Any(v > k_min_amp) || Any(snapshot.hold_levels > k_min_amp))
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);

    // Level positions as integer y-offsets from origin.
    auto const clamped_v = Max(v, f32x2(k_min_amp));
    auto const v_db = 20 * Log10(clamped_v);
    auto const v_perceived = Clamp<f32x2>(MapTo01Unchecked<f32x2>(v_db, k_min_db, k_max_db), 0, 1);
    auto level_y_l = total_h - (s32)(v_perceived[0] * (f32)total_h);
    auto level_y_r = total_h - (s32)(v_perceived[1] * (f32)total_h);

    if (options.low_signal_threshold_db) {
        auto const threshold = *options.low_signal_threshold_db;
        auto const sliver_y = Max(0, total_h - 1);
        if (v_db[0] > threshold) level_y_l = Min(level_y_l, sliver_y);
        if (v_db[1] > threshold) level_y_r = Min(level_y_r, sliver_y);
    }

    // Draw level segments for each channel.
    s32 const level_ys[] = {level_y_l, level_y_r};
    for (s32 i = 0; i < 2; i++) {
        s32 const cx = chan_xs[i];
        s32 const ly = level_ys[i];
        if (ly >= total_h) continue;

        auto const x0 = origin_x + (f32)cx;
        auto const x1 = origin_x + (f32)(cx + chan_w);

        if (!options.show_warning_zones) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightBottom);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + ly},
                                           f32x2 {x1, origin_y + (f32)total_h},
                                           col,
                                           rounding,
                                           0b0011);
            continue;
        }

        // Top segment (above 0dB line).
        if (ly < top_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightTop);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            s32 const bottom = Min(total_h, top_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + ly}, f32x2 {x1, origin_y + bottom}, col);
        }

        // Middle segment (0dB to -12dB).
        if (ly < mid_seg_y && total_h > top_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightMiddle);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            s32 const top = Max(ly, top_seg_y);
            s32 const bottom = Min(total_h, mid_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + top}, f32x2 {x1, origin_y + bottom}, col);
        }

        // Bottom segment (below -12dB).
        if (total_h > mid_seg_y) {
            auto col = LiveCol(UiColMap::PeakMeterHighlightBottom);
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);
            s32 const top = Max(ly, mid_seg_y);
            imgui.draw_list->AddRectFilled(f32x2 {x0, origin_y + top},
                                           f32x2 {x1, origin_y + total_h},
                                           col,
                                           rounding,
                                           0b0011);
        }
    }

    // Peak-hold lines. Positioned with the same rounding as the level bars so a line sits exactly at the
    // top edge of where the level actually reached, never above it, and coloured to match the segment it
    // falls in.
    auto const hold_line_h = Max(1.0f, Round(WwToPixels(1.0f)));
    if ((f32)total_h > hold_line_h) {
        auto const hold_db = 20 * Log10(Max(snapshot.hold_levels, f32x2(k_min_amp)));
        for (auto const chan_index : Range(k_num_channels)) {
            if (hold_db[chan_index] <= k_min_db) continue;
            auto const hold_01 = Clamp01(MapTo01(hold_db[chan_index], k_min_db, k_max_db));
            auto const hold_y = total_h - (s32)(hold_01 * (f32)total_h);
            auto const y = Clamp((f32)hold_y, 0.0f, (f32)total_h - hold_line_h);

            auto col = LiveCol(UiColMap::PeakMeterHighlightBottom);
            if (options.show_warning_zones) {
                if (hold_y < top_seg_y)
                    col = LiveCol(UiColMap::PeakMeterHighlightTop);
                else if (hold_y < mid_seg_y)
                    col = LiveCol(UiColMap::PeakMeterHighlightMiddle);
            }
            if (did_clip) col = LiveCol(UiColMap::PeakMeterClipping);

            auto const cx = chan_xs[chan_index];
            imgui.draw_list->AddRectFilled(f32x2 {origin_x + (f32)cx, origin_y + y},
                                           f32x2 {origin_x + (f32)(cx + chan_w), origin_y + y + hold_line_h},
                                           col);
        }
    }

    if (options.marker_db) {
        auto const marker_y =
            (s32)((1 - MapTo01(Clamp(*options.marker_db, k_min_db, k_max_db), k_min_db, k_max_db)) *
                  (f32)total_h);
        auto const col =
            options.marker_col ? options.marker_col : LiveCol(UiColMap::PeakMeterHighlightMiddle);
        imgui.draw_list->AddLine(f32x2 {origin_x + (f32)chan_xs[0], origin_y + (f32)marker_y},
                                 f32x2 {origin_x + (f32)(chan_xs[1] + chan_w), origin_y + (f32)marker_y},
                                 col,
                                 WwToPixels(1.0f));
    }
}

void DrawGainReductionMeter(imgui::Context& imgui, Rect r, DrawGainReductionMeterOptions const& options) {
    auto const origin_x = Round(r.x);
    auto const origin_y = Round(r.y);
    auto const total_w = (s32)Round(r.w);
    auto const total_h = (s32)Round(r.h);

    auto const meter_w = total_w;
    constexpr auto k_channel_gap = 2; // matches DrawPeakMeterOptions::gap_px default
    auto const chan_w = (meter_w - k_channel_gap) / 2;
    auto const bar_x0 = origin_x + (f32)((meter_w - chan_w) / 2);
    auto const bar_x1 = bar_x0 + (f32)chan_w;

    auto const rounding_full = WwToPixels(k_corner_rounding);
    auto const small = chan_w < (s32)(rounding_full * 2);
    auto const rounding = small ? 0.0f : rounding_full;
    auto const saved_aa = imgui.draw_list->renderer.anti_aliased_shapes;
    if (small) imgui.draw_list->renderer.anti_aliased_shapes = false;
    DEFER { imgui.draw_list->renderer.anti_aliased_shapes = saved_aa; };

    imgui.draw_list->AddRectFilled(f32x2 {bar_x0, origin_y},
                                   f32x2 {bar_x1, origin_y + (f32)total_h},
                                   LiveCol(UiColMap::PeakMeterBack),
                                   rounding);

    auto const reduction_y =
        (s32)(Clamp(options.gain_reduction_db / options.max_reduction_db, 0.0f, 1.0f) * (f32)total_h);
    if (reduction_y > 0)
        imgui.draw_list->AddRectFilled(f32x2 {bar_x0, origin_y},
                                       f32x2 {bar_x1, origin_y + (f32)reduction_y},
                                       options.col,
                                       rounding,
                                       0b1100);
}

void DrawLoudnessMeter(imgui::Context& imgui, Rect r, DrawLoudnessMeterOptions const& options) {
    auto const origin_x = Round(r.x);
    auto const origin_y = Round(r.y);
    auto const total_w = (s32)Round(r.w);
    auto const total_h = (s32)Round(r.h);

    constexpr auto k_channel_gap = 2; // matches DrawPeakMeterOptions::gap_px default
    auto const bar_w = (total_w - k_channel_gap) / 2;
    auto const bar_x0 = origin_x + (f32)((total_w - bar_w) / 2);
    auto const bar_x1 = bar_x0 + (f32)bar_w;

    auto const rounding_full = WwToPixels(k_corner_rounding);
    auto const small = bar_w < (s32)(rounding_full * 2);
    auto const rounding = small ? 0.0f : rounding_full;
    auto const saved_aa = imgui.draw_list->renderer.anti_aliased_shapes;
    if (small) imgui.draw_list->renderer.anti_aliased_shapes = false;
    DEFER { imgui.draw_list->renderer.anti_aliased_shapes = saved_aa; };

    auto const clamp_lufs = [&](f32 lufs) { return Clamp(lufs, options.min_lufs, options.max_lufs); };
    auto const y_for_lufs = [&](f32 lufs) {
        return origin_y +
               (f32)(s32)((1 - MapTo01(clamp_lufs(lufs), options.min_lufs, options.max_lufs)) * (f32)total_h);
    };
    auto const good_col = LiveCol(UiColMap::LoudnessMeterGood);
    auto const col_for_lufs = [&](f32 lufs) {
        if (lufs < options.target_min_lufs)
            return LerpColours(good_col,
                               LiveCol(UiColMap::LoudnessMeterQuiet),
                               Clamp01((options.target_min_lufs - lufs) / options.fade_lu));
        if (lufs > options.target_max_lufs)
            return LerpColours(good_col,
                               LiveCol(UiColMap::LoudnessMeterHot),
                               Clamp01((lufs - options.target_max_lufs) / options.fade_lu));
        return good_col;
    };

    imgui.draw_list->AddRectFilled(f32x2 {bar_x0, origin_y},
                                   f32x2 {bar_x1, origin_y + (f32)total_h},
                                   LiveCol(UiColMap::PeakMeterBack),
                                   rounding);

    // The fill colour is piecewise-linear in LUFS, so a vertical gradient between each pair of breakpoints
    // reproduces it exactly.
    auto const fill_top_lufs = clamp_lufs(options.short_term_lufs);
    f32 const breakpoints_lufs[] = {
        options.min_lufs,
        clamp_lufs(options.target_min_lufs - options.fade_lu),
        clamp_lufs(options.target_min_lufs),
        clamp_lufs(options.target_max_lufs),
        clamp_lufs(options.target_max_lufs + options.fade_lu),
        options.max_lufs,
    };
    for (auto const segment_index : Range(ArraySize(breakpoints_lufs) - 1)) {
        auto const lo_lufs = breakpoints_lufs[segment_index];
        auto const hi_lufs = Min(breakpoints_lufs[segment_index + 1], fill_top_lufs);
        if (hi_lufs <= lo_lufs) break;
        auto const lo_y = y_for_lufs(lo_lufs);
        auto const hi_y = y_for_lufs(hi_lufs);
        if (hi_y >= lo_y) continue;
        auto const lo_col = col_for_lufs(lo_lufs);
        auto const hi_col = col_for_lufs(hi_lufs);
        imgui.draw_list->AddRectFilledMultiColor(f32x2 {bar_x0, hi_y},
                                                 f32x2 {bar_x1, lo_y},
                                                 hi_col,
                                                 hi_col,
                                                 lo_col,
                                                 lo_col);
    }

    if (options.momentary_lufs > options.min_lufs) {
        auto const marker_y = y_for_lufs(options.momentary_lufs);
        imgui.draw_list->AddLine(f32x2 {bar_x0, marker_y},
                                 f32x2 {bar_x1, marker_y},
                                 LiveCol(UiColMap::LoudnessMeterMomentaryMarker),
                                 WwToPixels(1.0f));
    }
}

struct ScrollbarColours {
    u32 channel;
    u32 handle;
    u32 handle_viewport_hovered;
    u32 handle_hot;
    u32 arrow;
    u32 arrow_hot;
    u32 button_back_hot;
};

static void DrawScrollbar(imgui::Context const& imgui,
                          imgui::ViewportScrollbar const& bar,
                          usize bar_index,
                          ScrollbarColours const& colours) {
    auto const rounding = WwToPixels(1.5f);
    auto const inset = Max(1.0f, Round(WwToPixels(1.0f)));

    // Channel.
    {
        auto channel = bar.strip;
        if (bar.buttons)
            channel = Rect::FromMinMax(Min(channel.Min(), (*bar.buttons)[0].rect.Min()),
                                       Max(channel.Max(), (*bar.buttons)[1].rect.Max()));
        imgui.draw_list->AddRectFilled(channel, colours.channel, rounding);
    }

    // Buttons.
    if (bar.buttons) {
        auto const is_vertical = bar_index == 1;
        auto const* font = imgui.draw_list->fonts.atlas[ToInt(FontType::Icons)];
        for (auto const button_index : Range(2uz)) {
            auto const& button = (*bar.buttons)[button_index];
            auto const hot = imgui.IsHotOrActive(button.id, MouseButton::Left);
            if (hot)
                imgui.draw_list->AddRectFilled(button.rect.Reduced(inset), colours.button_back_hot, rounding);

            String const icon = is_vertical ? (button_index == 0 ? ICON_FA_CARET_UP : ICON_FA_CARET_DOWN)
                                            : (button_index == 0 ? ICON_FA_CARET_LEFT : ICON_FA_CARET_RIGHT);
            auto const font_size = Min(button.rect.w, button.rect.h) * 0.8f;
            auto const text_size = font->CalcTextSize(icon, {.font_size = font_size});
            font->RenderText(imgui.draw_list,
                             font_size,
                             button.rect.Centre() - (text_size / 2),
                             hot ? colours.arrow_hot : colours.arrow,
                             imgui.draw_list->clip_rect_stack.Back(),
                             icon);
        }
    }

    // Handle.
    {
        auto handle_col = colours.handle;
        if (imgui.IsHotOrActive(bar.id, MouseButton::Left))
            handle_col = colours.handle_hot;
        else if (imgui.IsViewportHovered(imgui.curr_viewport))
            handle_col = colours.handle_viewport_hovered;
        imgui.draw_list->AddRectFilled(bar.handle.Reduced(inset), handle_col, rounding);
    }
}

void DrawMidPanelScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    ScrollbarColours const colours {
        .channel = LiveCol(UiColMap::ScrollbarBack),
        .handle = LiveCol(UiColMap::ScrollbarHandle),
        .handle_viewport_hovered = LiveCol(UiColMap::ScrollbarHandleViewportHover),
        .handle_hot = LiveCol(UiColMap::ScrollbarHandleHover),
        .arrow = LiveCol(UiColMap::ScrollbarHandle),
        .arrow_hot = LiveCol(UiColMap::ScrollbarHandleHover),
        .button_back_hot = LiveCol(UiColMap::ScrollbarButtonHover),
    };
    for (auto const bar_index : Range(bars.size))
        if (auto const& b = bars[bar_index]) DrawScrollbar(imgui, *b, bar_index, colours);
}

static void DrawModalScrollbarsWithMode(imgui::Context const& imgui,
                                        imgui::ViewportScrollbars const& bars,
                                        bool dark_mode) {
    ScrollbarColours const colours {
        .channel = ToU32({.c = Col::Surface0, .dark_mode = dark_mode}),
        .handle = ToU32({.c = Col::Overlay0, .dark_mode = dark_mode}),
        .handle_viewport_hovered = ToU32({.c = Col::Overlay1, .dark_mode = dark_mode}),
        .handle_hot = ToU32({.c = Col::Overlay2, .dark_mode = dark_mode}),
        .arrow = ToU32({.c = Col::Overlay1, .dark_mode = dark_mode}),
        .arrow_hot = ToU32({.c = Col::Text, .dark_mode = dark_mode}),
        .button_back_hot = ToU32({.c = Col::Surface2, .dark_mode = dark_mode}),
    };
    for (auto const bar_index : Range(bars.size))
        if (auto const& b = bars[bar_index]) DrawScrollbar(imgui, *b, bar_index, colours);
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

struct TooltipText {
    String text;
    String footer {}; // Dimmer, as a separate paragraph beneath the text.
};

static f32 TooltipFooterGap(Fonts& fonts) { return fonts.Current()->font_size; }

static f32x2 TooltipBoxSize(Fonts& fonts,
                            FontType font,
                            TooltipText const& str,
                            f32 max_text_width,
                            f32x2 text_margin,
                            Optional<f32> fixed_text_width = k_nullopt) {
    fonts.Push(ToInt(font));
    DEFER { fonts.Pop(); };
    auto const wrap_width = fixed_text_width.ValueOr(max_text_width);
    auto size = fonts.CalcTextSize(str.text, {.wrap_width = wrap_width});
    if (str.footer.size) {
        auto const footer_size = fonts.CalcTextSize(str.footer, {.wrap_width = wrap_width});
        size = {Max(size.x, footer_size.x), size.y + TooltipFooterGap(fonts) + footer_size.y};
    }
    auto const text_width = fixed_text_width.ValueOr(Min(max_text_width, size.x));
    return f32x2 {text_width, size.y} + (text_margin * 2);
}

static void DrawTooltipBox(imgui::Context const& imgui,
                           Fonts& fonts,
                           FontType font,
                           TooltipText const& str,
                           Rect r,
                           f32x2 text_margin,
                           f32 opacity) {
    fonts.Push(ToInt(font));
    DEFER { fonts.Pop(); };

    DrawDropShadow(imgui, r, k_nullopt, opacity);
    imgui.overlay_draw_list->AddRectFilled(r,
                                           ChangeAlpha(ToU32(Col {.c = Col::Background0}), opacity),
                                           WwToPixels(k_corner_rounding));
    auto const wrap_width = r.w - (text_margin.x * 2) + 1;
    imgui.overlay_draw_list->AddText(r.pos + text_margin,
                                     ChangeAlpha(ToU32(Col {.c = Col::Text}), opacity),
                                     str.text,
                                     {.wrap_width = wrap_width});
    if (str.footer.size) {
        auto const text_height = fonts.CalcTextSize(str.text, {.wrap_width = wrap_width}).y;
        imgui.overlay_draw_list->AddText(r.pos + text_margin +
                                             f32x2 {0, text_height + TooltipFooterGap(fonts)},
                                         ChangeAlpha(ToU32(Col {.c = Col::Subtext0}), opacity),
                                         str.footer,
                                         {.wrap_width = wrap_width});
    }
}

enum class TooltipSide : u8 { Below, Above, Right, Left };

static Array<TooltipSide, 4> TooltipSideOrder(TooltipPlacement placement) {
    switch (placement) {
        case TooltipPlacement::BelowThenAbove:
            return {TooltipSide::Below, TooltipSide::Above, TooltipSide::Right, TooltipSide::Left};
        case TooltipPlacement::AboveThenBelow:
            return {TooltipSide::Above, TooltipSide::Below, TooltipSide::Right, TooltipSide::Left};
        case TooltipPlacement::RightThenLeft:
            return {TooltipSide::Right, TooltipSide::Left, TooltipSide::Below, TooltipSide::Above};
        case TooltipPlacement::LeftThenRight:
            return {TooltipSide::Left, TooltipSide::Right, TooltipSide::Below, TooltipSide::Above};
        case TooltipPlacement::RightThenBelow:
            return {TooltipSide::Right, TooltipSide::Below, TooltipSide::Left, TooltipSide::Above};
    }
    PanicIfReached();
}

struct TooltipPlacementContext {
    Rect element_r;
    Rect avoid_r;
    f32x2 window_size;
    f32x2 text_margin;
};

// Text width available beside avoid_r on the given side, capped at the max tooltip width.
static f32 TooltipTextWidthOnSide(TooltipPlacementContext const& ctx, TooltipSide side) {
    auto const max_text_width = WwToPixels(k_tooltip_max_width);
    switch (side) {
        case TooltipSide::Below:
        case TooltipSide::Above: return max_text_width;
        case TooltipSide::Right:
            return Min(max_text_width, ctx.window_size.x - ctx.avoid_r.Right() - (ctx.text_margin.x * 2));
        case TooltipSide::Left: return Min(max_text_width, ctx.avoid_r.x - (ctx.text_margin.x * 2));
    }
    PanicIfReached();
}

static Rect PlaceTooltipBesideElement(TooltipPlacementContext const& ctx,
                                      Fonts& fonts,
                                      FontType font,
                                      TooltipText const& str,
                                      TooltipPlacement placement,
                                      Optional<f32> fixed_text_width = k_nullopt) {
    auto const min_text_width = fixed_text_width.ValueOr(WwToPixels(k_tooltip_min_width));
    auto const clamp_x = [&](Rect r) {
        r.x = Clamp(r.x, 0.0f, Max(0.0f, ctx.window_size.x - r.w));
        return r;
    };
    auto const clamp_y = [&](Rect r) {
        r.y = Clamp(r.y, 0.0f, Max(0.0f, ctx.window_size.y - r.h));
        return r;
    };

    for (auto const side : TooltipSideOrder(placement)) {
        auto const available_width = TooltipTextWidthOnSide(ctx, side);
        if (available_width < min_text_width) continue;
        auto const text_width = fixed_text_width.ValueOr(available_width);

        Rect r {.size = TooltipBoxSize(fonts, font, str, text_width, ctx.text_margin, fixed_text_width)};
        switch (side) {
            case TooltipSide::Below:
                r.pos = {ctx.element_r.CentreX() - (r.w / 2), ctx.avoid_r.Bottom()};
                if (r.Bottom() > ctx.window_size.y) continue;
                return clamp_x(r);
            case TooltipSide::Above:
                r.pos = {ctx.element_r.CentreX() - (r.w / 2), ctx.avoid_r.y - r.h};
                if (r.y < 0) continue;
                return clamp_x(r);
            case TooltipSide::Right:
                r.pos = {ctx.avoid_r.Right(), ctx.element_r.CentreY() - (r.h / 2)};
                if (r.Right() > ctx.window_size.x) continue;
                return clamp_y(r);
            case TooltipSide::Left:
                r.pos = {ctx.avoid_r.x - r.w, ctx.element_r.CentreY() - (r.h / 2)};
                if (r.x < 0) continue;
                return clamp_y(r);
        }
    }

    Rect r {.pos = ctx.element_r.pos,
            .size = TooltipBoxSize(fonts,
                                   font,
                                   str,
                                   WwToPixels(k_tooltip_max_width),
                                   ctx.text_margin,
                                   fixed_text_width)};
    return clamp_y(clamp_x(r));
}

void DrawOverlayTooltipForRect(imgui::Context const& imgui, Fonts& fonts, DrawTooltipArgs const& args) {
    TooltipPlacementContext const ctx {
        .element_r = args.r,
        .avoid_r = args.avoid_r.Expanded(WwToPixels(k_tooltip_avoid_gap)),
        .window_size = GuiIo().in.window_size.ToFloat2(),
        .text_margin = WwToPixels(k_tooltip_pad),
    };
    auto const& text_margin = ctx.text_margin;
    auto const& window_size = ctx.window_size;
    auto const& avoid_r = ctx.avoid_r;

    Optional<Rect> value_popup_r {};
    if (args.value_popup_opacity > 0) {
        TooltipText const value_popup {.text = args.value_popup};
        auto const fixed_text_width_px =
            args.value_popup_fixed_width.Transform([&](f32 w) { return WwToPixels(w); });
        value_popup_r = PlaceTooltipBesideElement(ctx,
                                                  fonts,
                                                  FontType::Body,
                                                  value_popup,
                                                  args.placement,
                                                  fixed_text_width_px);
        DrawTooltipBox(imgui,
                       fonts,
                       FontType::Body,
                       value_popup,
                       *value_popup_r,
                       text_margin,
                       args.value_popup_opacity);
    }

    if (args.tooltip_opacity > 0) {
        auto const font = FontType::BodyItalic;
        auto const gap = WwToPixels(k_small_gap);
        TooltipText const tooltip {.text = args.tooltip, .footer = args.tooltip_footer};

        // Stacked against the element and value popup, so the value popup never moves when the tooltip
        // appears and the tooltip never covers the element.
        auto const tooltip_r = ({
            Rect r;
            if (!value_popup_r) {
                r = PlaceTooltipBesideElement(ctx, fonts, font, tooltip, args.placement);
            } else if (auto const& v = *value_popup_r; v.x >= avoid_r.Right() || v.Right() <= avoid_r.x) {
                // Value popup sits in the gap beside the element: continue the stack downwards within
                // that same gap, wrapped to the same width.
                auto const side = v.x >= avoid_r.Right() ? TooltipSide::Right : TooltipSide::Left;
                r.size = TooltipBoxSize(fonts, font, tooltip, TooltipTextWidthOnSide(ctx, side), text_margin);
                r.x = side == TooltipSide::Right ? v.x : v.Right() - r.w;
                r.y = v.Bottom() + gap;
                if (r.Bottom() > window_size.y) r.y = v.y - gap - r.h;
                r.y = Clamp(r.y, 0.0f, Max(0.0f, window_size.y - r.h));
            } else {
                auto const block_r =
                    Rect::FromMinMax(Min(v.Min(), avoid_r.Min()), Max(v.Max(), avoid_r.Max())).Expanded(gap);
                r.size = TooltipBoxSize(fonts,
                                        font,
                                        tooltip,
                                        TooltipTextWidthOnSide(ctx, TooltipSide::Below),
                                        text_margin);
                auto const x = v.CentreX() - (r.w / 2);
                r.pos = {x, block_r.Bottom()};
                auto const pos =
                    imgui::BestPopupPos(r, block_r, window_size, imgui::PopupJustification::AboveOrBelow);
                if (pos.x >= 0)
                    r.pos = pos;
                else
                    r.pos = {Clamp(x, 0.0f, Max(0.0f, window_size.x - r.w)),
                             Clamp(block_r.Bottom(), 0.0f, Max(0.0f, window_size.y - r.h))};
            }
            r;
        });

        DrawTooltipBox(imgui,
                       fonts,
                       FontType::BodyItalic,
                       tooltip,
                       tooltip_r,
                       text_margin,
                       args.tooltip_opacity);
    }
}
