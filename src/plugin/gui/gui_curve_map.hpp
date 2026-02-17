// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/gui_drawing_helpers.hpp"
#include "gui/gui_state.hpp"
#include "gui/gui_utils.hpp"
#include "gui/old/gui_menu.hpp"
#include "gui/old/gui_widget_helpers.hpp"

static void DrawCurvedSegment(DrawList& graphics,
                              f32x2 screen_p0,
                              f32x2 screen_p1,
                              float curve_value,
                              int num_samples = 14) {
    if (Abs(curve_value) < 0.01f) {
        // Linear segment
        graphics.PathLineTo(screen_p1);
        return;
    }

    for (int i = 1; i <= num_samples; ++i) {
        float x_t = (float)i / (f32)num_samples; // Linear progression in X
        float y_t = x_t; // Start with linear, then apply curve

        // Apply the same curve math as your lookup table
        if (curve_value > 0.0f)
            y_t = Pow(y_t, 1.0f + (curve_value * CurveMap::k_curve_exponent_multiplier)); // Exponential
        else if (curve_value < 0.0f)
            y_t = 1.0f - Pow(1.0f - y_t,
                             1.0f - (curve_value * CurveMap::k_curve_exponent_multiplier)); // Logarithmic

        f32x2 curved_point = {screen_p0.x + ((screen_p1.x - screen_p0.x) * x_t),
                              screen_p0.y + ((screen_p1.y - screen_p0.y) * y_t)};
        graphics.PathLineTo(curved_point);
    }
}

static bool DoCurveMap(GuiState& g,
                       CurveMap& curve_map,
                       f32x2 rect_min,
                       f32x2 rect_max,
                       Optional<f32> velocity_marker,
                       String additional_tooltip) {
    auto& imgui = g.imgui;
    float width = rect_max.x - rect_min.x;
    float height = rect_max.y - rect_min.y;
    auto const rect = Rect::FromMinMax(rect_min, rect_max);
    auto const point_radius = (rect_max.x - rect_min.x) * 0.02f;
    constexpr f32 k_extra_grabber_scale = 3.0f;

    {
        auto const rounding = LiveSize(UiSizeId::CornerRounding);
        imgui.draw_list->AddRectFilled(rect, LiveCol(UiColMap::EnvelopeBack), rounding);
    }

    auto& draw_list = *imgui.draw_list;
    auto& points = curve_map.points;

    bool changed = false;

    draw_list.PathClear();

    constexpr auto k_remove_all = LargestRepresentableValue<usize>();
    Optional<usize> remove_working_index {};
    Optional<f32x2> new_point_at_window_pos {};

    auto working = curve_map.CreateWorkingPoints(points);

    for (auto const i : Range(working.size - 1)) {
        auto const& p0 = working[i];
        auto const& p1 = working[i + 1];
        f32x2 screen_p0 = {rect_min.x + (p0.x * width), rect_max.y - (p0.y * height)};
        f32x2 screen_p1 = {rect_min.x + (p1.x * width), rect_max.y - (p1.y * height)};

        if (i == 0) draw_list.PathLineTo(screen_p0);
        DrawCurvedSegment(draw_list, screen_p0, screen_p1, p0.curve);
    }

    constexpr f32 k_curve_thickness = 1.0f;
    auto const curve_color = LiveCol(UiColMap::CurveMapLine);
    auto const point_color = LiveCol(UiColMap::CurveMapPoint);
    auto const point_hover_color = LiveCol(UiColMap::CurveMapPointHover);

    draw_list.PathStroke(curve_color, false, k_curve_thickness);

    // Draw control points

    imgui.PushId("curve-map-points");
    DEFER { imgui.PopId(); };

    for (auto const working_index : Range(working.size)) {
        auto& working_point = working[working_index];

        imgui.PushId((uintptr)working_point.real_index);
        DEFER { imgui.PopId(); };

        f32x2 const pos = {rect_min.x + (working_point.x * width), rect_max.y - (working_point.y * height)};

        // Grabber is 2x bigger than the circle
        Rect grabber_rect = {
            .pos = pos - (point_radius * k_extra_grabber_scale),
            .size = point_radius * k_extra_grabber_scale * 2.0f,
        };

        if (working_point.is_virtual && working_index == 0) {
            auto const& next_working_point = working[working_index + 1];
            f32 next_point_left_edge =
                rect_min.x + (next_working_point.x * width) -
                (point_radius * k_extra_grabber_scale * (next_working_point.is_virtual ? 0 : 1));

            if (pos.x > next_point_left_edge) continue;

            Rect region_rect = {
                .x = pos.x,
                .y = rect_min.y,
                .w = next_point_left_edge - pos.x,
                .h = height,
            };

            auto const imgui_id = imgui.MakeId("unused-space");

            Tooltip(g,
                    imgui_id,
                    region_rect,
                    fmt::Format(g.scratch_arena, "Double-click to add point.\n\n{}", additional_tooltip),
                    {});

            // Double-click to add point
            if (imgui.ButtonBehaviour(region_rect,
                                      imgui_id,
                                      {
                                          .mouse_button = MouseButton::Left,
                                          .event = MouseButtonEvent::DoubleClick,
                                      })) {
                new_point_at_window_pos = GuiIo().in.Mouse(MouseButton::Left).last_press.point;
            }

            // Right-click menu
            {
                auto const right_click_id = imgui_id + 1;

                if (imgui.ButtonBehaviour(region_rect,
                                          imgui_id,
                                          {
                                              .mouse_button = MouseButton::Right,
                                              .event = MouseButtonEvent::Up,
                                          })) {
                    imgui.OpenPopupMenu(right_click_id, imgui_id);
                }

                if (g.imgui.IsPopupMenuOpen(right_click_id)) {
                    g.imgui.BeginViewport(
                        k_default_popup_menu_viewport,
                        right_click_id,
                        {.pos = GuiIo().in.Mouse(MouseButton::Right).last_press.point, .size = {}});
                    DEFER { imgui.EndViewport(); };

                    StartFloeMenu(g);
                    DEFER { EndFloeMenu(g); };

                    auto const k_items = Array {"Add Point"_s};

                    PopupMenuItems menu(g, k_items);
                    if (points.size != points.Capacity()) {
                        if (menu.DoButton(0))
                            new_point_at_window_pos = GuiIo().in.Mouse(MouseButton::Right).last_press.point;
                    } else {
                        menu.DoFakeButton(0);
                    }
                }
            }

            continue;
        }

        // Point curve grabber - show handle if this point has a next segment
        if (working_index + 1 < working.size) {
            auto const curve_handle_imgui_id = imgui.MakeId("curve handle");

            // We the whole rectangle from the grabber to the next grabber to be clicked and dragged.

            auto const this_point_right_edge = grabber_rect.Right();
            auto const& next_working_point = working[working_index + 1];
            f32 next_point_left_edge =
                rect_min.x + (next_working_point.x * width) - (point_radius * k_extra_grabber_scale);

            if (this_point_right_edge < next_point_left_edge) {
                Rect curve_handle_rect = {
                    .x = grabber_rect.Right(),
                    .y = rect_min.y,
                    .w = next_point_left_edge - this_point_right_edge,
                    .h = height,
                };

                auto const inverted = next_working_point.y > working_point.y;

                f32 percent = MapTo01(working_point.curve * (inverted ? -1.0f : 1.0f), -1.0f, 1.0f);

                if (imgui.SliderBehaviourFraction({
                        .rect_in_window_coords = curve_handle_rect,
                        .id = curve_handle_imgui_id,
                        .fraction = percent,
                        .default_fraction = 0.5f,
                        .cfg =
                            {
                                .sensitivity = 500,
                                .slower_with_shift = true,
                                .default_on_modifer = true,
                            },
                    })) {
                    working_point.curve = MapFrom01(percent, -1.0f, 1.0f) * (inverted ? -1.0f : 1.0f);
                    changed = true;
                }

                if (imgui.IsHotOrActive(curve_handle_imgui_id, imgui::SliderConfig::k_activation_cfg)) {
                    draw_list.AddRectFilled(curve_handle_rect, LiveCol(UiColMap::CurveMapLineHover));
                    GuiIo().out.wants.cursor_type = CursorType::VerticalArrows;
                }

                if (imgui.IsHot(curve_handle_imgui_id))
                    Tooltip(g,
                            curve_handle_imgui_id,
                            curve_handle_rect,
                            fmt::Format(g.scratch_arena,
                                        "Drag to change curve. Double-click to add point.\n\n{}",
                                        additional_tooltip),
                            {});

                // Double-click to add point
                if (imgui.ButtonBehaviour(curve_handle_rect,
                                          curve_handle_imgui_id,
                                          {
                                              .mouse_button = MouseButton::Left,
                                              .event = MouseButtonEvent::DoubleClick,
                                          })) {
                    new_point_at_window_pos = GuiIo().in.Mouse(MouseButton::Left).last_press.point;
                }

                // Right-click
                {
                    auto const right_click_id = curve_handle_imgui_id + 1;

                    if (imgui.ButtonBehaviour(curve_handle_rect,
                                              curve_handle_imgui_id,
                                              {
                                                  .mouse_button = MouseButton::Right,
                                                  .event = MouseButtonEvent::Up,
                                              })) {
                        imgui.OpenPopupMenu(right_click_id, curve_handle_imgui_id);
                    }

                    if (g.imgui.IsPopupMenuOpen(right_click_id)) {
                        g.imgui.BeginViewport(
                            k_default_popup_menu_viewport,
                            right_click_id,
                            {.pos = GuiIo().in.Mouse(MouseButton::Right).last_press.point, .size = {}});
                        DEFER { imgui.EndViewport(); };

                        StartFloeMenu(g);
                        DEFER { EndFloeMenu(g); };

                        auto const k_items = Array {"Add Point"_s};

                        PopupMenuItems menu(g, k_items);
                        if (points.size != points.Capacity()) {
                            if (menu.DoButton(0))
                                new_point_at_window_pos =
                                    GuiIo().in.Mouse(MouseButton::Right).last_press.point;
                        } else {
                            menu.DoFakeButton(0);
                        }
                    }
                }
            }
        }

        // Point handle
        if (!working_point.is_virtual) {
            auto const imgui_id = imgui.MakeId("point handle");
            imgui.ButtonBehaviour(grabber_rect, imgui_id, imgui::SliderConfig::k_activation_cfg);

            if (imgui.IsActive(imgui_id, imgui::SliderConfig::k_activation_cfg)) {
                // Dragging point
                f32x2 mouse_pos = GuiIo().in.cursor_pos;
                f32x2 new_pos = {
                    (mouse_pos.x - rect_min.x) / width,
                    1.0f - ((mouse_pos.y - rect_min.y) / height),
                };

                // Don't allow going past the next point.
                if (working_index + 1 < working.size) {
                    auto const& next_point = working[working_index + 1];
                    if (new_pos.x > next_point.x) new_pos.x = next_point.x;
                }

                // Don't allow going past the previous point.
                if (working_index > 0) {
                    auto const& prev_point = working[working_index - 1];
                    if (new_pos.x < prev_point.x) new_pos.x = prev_point.x;
                }

                new_pos = Clamp(new_pos, f32x2(0.0f), f32x2(1.0f));

                working_point.x = new_pos.x;
                working_point.y = new_pos.y;
                changed = true;
            }

            if (imgui.ButtonBehaviour(grabber_rect,
                                      imgui_id,
                                      {
                                          .mouse_button = MouseButton::Left,
                                          .event = MouseButtonEvent::DoubleClick,
                                      })) {
                remove_working_index = working_index;
            }

            if (imgui.IsHotOrActive(imgui_id, imgui::SliderConfig::k_activation_cfg))
                GuiIo().out.wants.cursor_type = CursorType::AllArrows;

            if (imgui.IsHot(imgui_id))
                Tooltip(g,
                        imgui_id,
                        grabber_rect,
                        fmt::Format(g.scratch_arena,
                                    "Drag to move point. Double-click to remove point.\n\n{}",
                                    additional_tooltip),
                        {});

            // Right-click menu
            {
                auto const right_click_id = imgui_id + 1;

                if (imgui.ButtonBehaviour(grabber_rect,
                                          imgui_id,
                                          {
                                              .mouse_button = MouseButton::Right,
                                              .event = MouseButtonEvent::Up,
                                          })) {
                    imgui.OpenPopupMenu(right_click_id, imgui_id);
                }

                if (g.imgui.IsPopupMenuOpen(right_click_id)) {
                    g.imgui.BeginViewport(k_default_popup_menu_viewport, right_click_id, grabber_rect);
                    DEFER { imgui.EndViewport(); };

                    StartFloeMenu(g);
                    DEFER { EndFloeMenu(g); };

                    auto const k_items = Array {"Remove Point"_s, "Remove All Points"_s};

                    PopupMenuItems menu(g, k_items);
                    if (menu.DoButton(0)) {
                        remove_working_index = working_index;
                        imgui.ClearActive();
                    }

                    if (menu.DoButton(1)) {
                        remove_working_index = k_remove_all; // Remove all points
                        imgui.ClearActive();
                    }
                }
            }

            draw_list.AddCircleFilled(pos,
                                      point_radius,
                                      imgui.IsHotOrActive(imgui_id, imgui::SliderConfig::k_activation_cfg)
                                          ? point_hover_color
                                          : point_color,
                                      12);
        }
    }

    if (remove_working_index) {
        if (*remove_working_index == k_remove_all)
            dyn::Clear(working);
        else
            dyn::Remove(working, *remove_working_index);
        changed = true;
    }

    // Convert working points back to user points if any changes were made
    if (changed) {
        dyn::Clear(curve_map.points);
        for (auto const& wp : working)
            if (!wp.is_virtual) dyn::Append(curve_map.points, {wp.x, wp.y, wp.curve});
    }

    if (new_point_at_window_pos) {
        f32x2 new_point = {
            (new_point_at_window_pos->x - rect_min.x) / width,
            1.0f - ((new_point_at_window_pos->y - rect_min.y) / height),
        };
        new_point = Clamp(new_point, f32x2(0.0f), f32x2(1.0f));

        dyn::Append(curve_map.points, {new_point.x, new_point.y, 0.0f});
        Sort(curve_map.points, [](auto const& a, auto const& b) { return a.x < b.x; });
        changed = true;
    }

    if (velocity_marker) {
        auto const value = curve_map.ValueAt(working, *velocity_marker);
        DrawVoiceMarkerLine(
            imgui,
            f32x2 {rect_min.x + (*velocity_marker * width), rect_min.y + (height * (1.0f - value))},
            height * value,
            rect_min.x,
            k_nullopt);
    }

    return changed;
}
