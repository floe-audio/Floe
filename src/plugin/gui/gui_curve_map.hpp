// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/gui_drawing_helpers.hpp"
#include "gui/gui_state.hpp"
#include "gui/gui_utils.hpp"
#include "gui2_common_modal_panel.hpp"

static void
DrawCurvedSegment(DrawList& graphics, f32x2 p0, f32x2 p1, float curve_value, int num_samples = 14) {
    if (Abs(curve_value) < 0.01f) {
        // Linear segment
        graphics.PathLineTo(p1);
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

        f32x2 curved_point = {p0.x + ((p1.x - p0.x) * x_t), p0.y + ((p1.y - p0.y) * y_t)};
        graphics.PathLineTo(curved_point);
    }
}

static void DoCurveMap(GuiState& g,
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
    auto const grabber_radius = point_radius * k_extra_grabber_scale;
    auto const tooltip_rect = rect.Expanded(grabber_radius);

    {
        auto const rounding = LiveSize(UiSizeId::CornerRounding);
        imgui.draw_list->AddRectFilled(rect, LiveCol(UiColMap::EnvelopeBack), rounding);
    }

    auto& draw_list = *imgui.draw_list;

    bool changed = false;

    draw_list.PathClear();

    constexpr auto k_remove_all = LargestRepresentableValue<usize>();
    Optional<usize> remove_real_index {};
    Optional<f32x2> new_point_at_window_pos {};

    auto working = CurveMap::CreateWorkingPoints(curve_map.points);

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

        imgui.PushId((uintptr)working_point.unique_id);
        DEFER { imgui.PopId(); };

        f32x2 const pos = {rect_min.x + (working_point.x * width), rect_max.y - (working_point.y * height)};

        // Grabber is 2x bigger than the circle
        Rect grabber_rect = {
            .pos = pos - (grabber_radius),
            .size = grabber_radius * 2.0f,
        };

        if (working_point.real_index < 0 && working_index == 0) {
            auto const& next_working_point = working[working_index + 1];
            f32 next_point_left_edge = rect_min.x + (next_working_point.x * width) -
                                       (grabber_radius * (next_working_point.real_index < 0 ? 0 : 1));

            if (pos.x > next_point_left_edge) continue;

            Rect region_rect = {
                .x = pos.x,
                .y = rect_min.y,
                .w = next_point_left_edge - pos.x,
                .h = height,
            };

            auto const imgui_id = imgui.MakeId(SourceLocationHash());

            Tooltip(g,
                    imgui_id,
                    tooltip_rect,
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
                auto const right_click_id = imgui.MakeId(SourceLocationHash());

                if (imgui.ButtonBehaviour(region_rect,
                                          imgui_id,
                                          {
                                              .mouse_button = MouseButton::Right,
                                              .event = MouseButtonEvent::Up,
                                          })) {
                    imgui.OpenPopupMenu(right_click_id, imgui_id);
                    g.curve_map_add_point_click_pos = GuiIo().in.cursor_pos;
                }

                if (g.imgui.IsPopupMenuOpen(right_click_id))
                    DoBoxViewport(
                        g.builder,
                        {
                            .run =
                                [&](GuiBuilder&) {
                                    auto const root =
                                        DoBox(g.builder,
                                              {
                                                  .layout {
                                                      .size = layout::k_hug_contents,
                                                      .contents_direction = layout::Direction::Column,
                                                      .contents_align = layout::Alignment::Start,
                                                  },
                                              });
                                    if (MenuItem(
                                            g.builder,
                                            root,
                                            {
                                                .text = "Add Point"_s,
                                                .mode = curve_map.points.size == curve_map.points.Capacity()
                                                            ? MenuItemOptions::Mode::Disabled
                                                            : MenuItemOptions::Mode::Active,
                                                .no_icon_gap = true,
                                            })
                                            .button_fired) {
                                        new_point_at_window_pos = g.curve_map_add_point_click_pos;
                                    }
                                },
                            .bounds = Rect {.pos = g.curve_map_add_point_click_pos}.Expanded(grabber_radius),
                            .imgui_id = right_click_id,
                            .viewport_config = k_default_popup_menu_viewport,
                        });
            }

            continue;
        }

        // Curve shape control
        if (working_index + 1 < working.size) {
            ASSERT(working_point.real_index >= 0);
            auto const curve_shaper_imgui_id = imgui.MakeId(SourceLocationHash());

            // We allow the whole rectangle from the grabber to the next grabber to be clicked and dragged.

            auto const this_point_right_edge = grabber_rect.Right();
            auto const& next_working_point = working[working_index + 1];
            f32 next_point_left_edge = rect_min.x + (next_working_point.x * width) - (grabber_radius);

            if (this_point_right_edge < next_point_left_edge) {
                Rect curve_shaper_rect = {
                    .x = grabber_rect.Right(),
                    .y = rect_min.y,
                    .w = next_point_left_edge - this_point_right_edge,
                    .h = height,
                };

                auto const inverted = next_working_point.y > working_point.y;

                f32 percent = MapTo01(working_point.curve * (inverted ? -1.0f : 1.0f), -1.0f, 1.0f);

                if (imgui.SliderBehaviourFraction({
                        .rect_in_window_coords = curve_shaper_rect,
                        .id = curve_shaper_imgui_id,
                        .fraction = percent,
                        .default_fraction = 0.5f,
                        .cfg =
                            {
                                .sensitivity = 500,
                                .slower_with_shift = true,
                                .default_on_modifer = true,
                            },
                    })) {
                    curve_map.points[(usize)working_point.real_index].curve =
                        MapFrom01(percent, -1.0f, 1.0f) * (inverted ? -1.0f : 1.0f);
                    changed = true;
                }

                Tooltip(g,
                        curve_shaper_imgui_id,
                        tooltip_rect,
                        fmt::Format(g.scratch_arena,
                                    "Drag to change curve. Double-click to add point.\n\n{}",
                                    additional_tooltip),
                        {});

                // Double-click to add point
                if (imgui.ButtonBehaviour(curve_shaper_rect,
                                          curve_shaper_imgui_id,
                                          {
                                              .mouse_button = MouseButton::Left,
                                              .event = MouseButtonEvent::DoubleClick,
                                          })) {
                    new_point_at_window_pos = GuiIo().in.Mouse(MouseButton::Left).last_press.point;
                }

                // Right-click
                {
                    auto const right_click_id = imgui.MakeId(SourceLocationHash());

                    if (imgui.ButtonBehaviour(curve_shaper_rect,
                                              curve_shaper_imgui_id,
                                              {
                                                  .mouse_button = MouseButton::Right,
                                                  .event = MouseButtonEvent::Up,
                                              })) {
                        imgui.OpenPopupMenu(right_click_id, curve_shaper_imgui_id);
                        g.curve_map_add_point_click_pos = GuiIo().in.cursor_pos;
                    }

                    if (g.imgui.IsPopupMenuOpen(right_click_id))
                        DoBoxViewport(
                            g.builder,
                            {
                                .run =
                                    [&](GuiBuilder&) {
                                        auto const root =
                                            DoBox(g.builder,
                                                  {
                                                      .layout {
                                                          .size = layout::k_hug_contents,
                                                          .contents_direction = layout::Direction::Column,
                                                          .contents_align = layout::Alignment::Start,
                                                      },
                                                  });
                                        if (MenuItem(g.builder,
                                                     root,
                                                     {
                                                         .text = "Add Point"_s,
                                                         .mode = curve_map.points.size ==
                                                                         curve_map.points.Capacity()
                                                                     ? MenuItemOptions::Mode::Disabled
                                                                     : MenuItemOptions::Mode::Active,
                                                         .no_icon_gap = true,
                                                     })
                                                .button_fired) {
                                            new_point_at_window_pos = g.curve_map_add_point_click_pos;
                                        }
                                    },
                                .bounds =
                                    Rect {.pos = g.curve_map_add_point_click_pos}.Expanded(grabber_radius),
                                .imgui_id = right_click_id,
                                .viewport_config = k_default_popup_menu_viewport,
                            });
                }

                if (imgui.IsHotOrActive(curve_shaper_imgui_id, MouseButton::Left)) {
                    draw_list.AddRectFilled(curve_shaper_rect, LiveCol(UiColMap::CurveMapLineHover));
                    GuiIo().out.wants.cursor_type = CursorType::VerticalArrows;
                }
            }
        }

        // Point handle
        if (working_point.real_index >= 0) {
            auto const imgui_id = imgui.MakeId(SourceLocationHash());

            // Double-click remove
            if (imgui.ButtonBehaviour(grabber_rect,
                                      imgui_id,
                                      {
                                          .mouse_button = MouseButton::Left,
                                          .event = MouseButtonEvent::DoubleClick,
                                      })) {
                remove_real_index = (usize)working_point.real_index;
            }

            // Right-click menu
            {
                auto const right_click_id = imgui.MakeId(SourceLocationHash());

                if (imgui.ButtonBehaviour(grabber_rect,
                                          imgui_id,
                                          {
                                              .mouse_button = MouseButton::Right,
                                              .event = MouseButtonEvent::Up,
                                          })) {
                    imgui.OpenPopupMenu(right_click_id, imgui_id);
                }

                if (g.imgui.IsPopupMenuOpen(right_click_id))
                    DoBoxViewport(g.builder,
                                  {
                                      .run =
                                          [&](GuiBuilder&) {
                                              auto const root = DoBox(
                                                  g.builder,
                                                  {
                                                      .layout {
                                                          .size = layout::k_hug_contents,
                                                          .contents_direction = layout::Direction::Column,
                                                          .contents_align = layout::Alignment::Start,
                                                      },
                                                  });
                                              if (MenuItem(g.builder,
                                                           root,
                                                           {
                                                               .text = "Remove Point"_s,
                                                               .no_icon_gap = true,
                                                           })
                                                      .button_fired) {
                                                  remove_real_index = (usize)working_point.real_index;
                                                  imgui.ClearActive();
                                              }
                                              if (MenuItem(g.builder,
                                                           root,
                                                           {
                                                               .text = "Remove All Points"_s,
                                                               .no_icon_gap = true,
                                                           })
                                                      .button_fired) {
                                                  remove_real_index = k_remove_all;
                                                  imgui.ClearActive();
                                              }
                                          },
                                      .bounds = grabber_rect,
                                      .imgui_id = right_click_id,
                                      .viewport_config = k_default_popup_menu_viewport,
                                  });
            }

            auto drag_activation_cfg = imgui::SliderConfig::k_activation_cfg;
            drag_activation_cfg.cursor_type = CursorType::AllArrows;
            imgui.ButtonBehaviour(grabber_rect, imgui_id, drag_activation_cfg);

            if (imgui.IsActive(imgui_id, drag_activation_cfg.mouse_button)) {
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

                curve_map.points[(usize)working_point.real_index].x = new_pos.x;
                curve_map.points[(usize)working_point.real_index].y = new_pos.y;
                changed = true;
            }

            draw_list.AddCircleFilled(pos,
                                      point_radius,
                                      imgui.IsHotOrActive(imgui_id, drag_activation_cfg.mouse_button)
                                          ? point_hover_color
                                          : point_color,
                                      12);

            Tooltip(g,
                    imgui_id,
                    tooltip_rect,
                    fmt::Format(g.scratch_arena,
                                "Drag to move point. Double-click to remove point.\n\n{}",
                                additional_tooltip),
                    {});
        }
    }

    if (remove_real_index) {
        if (*remove_real_index == k_remove_all)
            curve_map.Clear();
        else
            curve_map.RemoveIndex(*remove_real_index);
    }

    if (new_point_at_window_pos) {
        auto const new_point = Clamp<f32x2>(
            f32x2 {
                (new_point_at_window_pos->x - rect_min.x) / width,
                1.0f - ((new_point_at_window_pos->y - rect_min.y) / height),
            },
            0.0f,
            1.0f);

        curve_map.AddPoint({new_point.x, new_point.y, 0});
    }

    if (changed) curve_map.RenderCurveToLookupTable();

    if (velocity_marker) {
        auto const value = curve_map.ValueAt(working, *velocity_marker);
        DrawVoiceMarkerLine(
            imgui,
            f32x2 {rect_min.x + (*velocity_marker * width), rect_min.y + (height * (1.0f - value))},
            height * value,
            rect_min.x,
            k_nullopt);
    }
}
