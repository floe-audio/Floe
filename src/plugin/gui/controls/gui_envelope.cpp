// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_envelope.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"

constexpr usize k_attack_index = 0;
constexpr usize k_decay_index = 1;
constexpr usize k_sustain_index = 2;
constexpr usize k_release_index = 3;

constexpr auto k_max_attack_percent = 0.31f;
constexpr auto k_max_decay_percent = 0.31f;
constexpr auto k_max_release_percent = 0.31f;
constexpr auto k_sustain_point_percent =
    (k_max_attack_percent + k_max_decay_percent) +
    (1 - (k_max_attack_percent + k_max_decay_percent + k_max_release_percent));

static Array<f32x2, k_num_adsr_params> EnvelopePoints(Rect viewport_r,
                                                      Array<f32, k_num_adsr_params> const& linear_values) {
    Array<f32x2, k_num_adsr_params> points;
    points[k_attack_index] = {
        MapFrom01(linear_values[k_attack_index],
                  viewport_r.x,
                  viewport_r.x + (k_max_attack_percent * viewport_r.w)),
        viewport_r.y,
    };
    points[k_decay_index] = {
        MapFrom01(linear_values[k_decay_index],
                  points[k_attack_index].x,
                  points[k_attack_index].x + (k_max_decay_percent * viewport_r.w)),
        MapFrom01(1 - linear_values[k_sustain_index], viewport_r.y, viewport_r.Bottom()),
    };
    points[k_sustain_index] = {viewport_r.x + (k_sustain_point_percent * viewport_r.w),
                               points[k_decay_index].y};
    points[k_release_index] = {
        MapFrom01(linear_values[k_release_index],
                  points[k_sustain_index].x,
                  points[k_sustain_index].x + (k_max_release_percent * viewport_r.w)),
        viewport_r.Bottom(),
    };
    return points;
}

struct EnvelopeXRange {
    f32 min;
    f32 max;
};

static void
DrawEnvelopeRangeLines(imgui::Context& imgui, EnvelopeXRange range, imgui::Id id, f32 top, f32 bottom) {
    if (imgui.IsActive(id, MouseButton::Left)) {
        auto const col = LiveCol(UiColMap::EnvelopeRangeLines);
        imgui.draw_list->AddLine(imgui.ViewportPosToWindowPos({range.min, top}),
                                 imgui.ViewportPosToWindowPos({range.min, bottom}),
                                 col);
        imgui.draw_list->AddLine(imgui.ViewportPosToWindowPos({range.max, top}),
                                 imgui.ViewportPosToWindowPos({range.max, bottom}),
                                 col);
    }
}

static void
DrawEnvelopeHandle(imgui::Context& imgui, f32x2 point, imgui::Id id, f32 handle_size, bool greyed_out) {
    auto const handle_visible_size = WwToPixels(k_graph_handle_radius);
    auto const hover_col = LiveCol(UiColMap::EnvelopeHandleHover);
    auto col = greyed_out ? LiveCol(UiColMap::EnvelopeHandleGreyedOut) : LiveCol(UiColMap::EnvelopeHandle);
    if (imgui.IsHot(id)) {
        auto background_col = FromU32(col);
        background_col.a /= 2;
        imgui.draw_list->AddCircleFilled(point, handle_size / 5, ToU32(background_col));
        col = hover_col;
    }
    if (imgui.IsActive(id, MouseButton::Left)) col = hover_col;
    imgui.draw_list->AddCircleFilled(point, handle_visible_size, col);
}

static void DrawEnvelopeVoiceMarkers(GuiState& g,
                                     GuiEnvelopeType type,
                                     u8 layer_index,
                                     f32x2 bottom_left,
                                     Array<f32x2, k_num_adsr_params> const& adsr_points) {
    auto& imgui = g.imgui;
    auto& voice_markers = type == GuiEnvelopeType::Volume
                              ? g.engine.processor.voice_pool.voice_vol_env_markers_for_gui.Consume().data
                              : g.engine.processor.voice_pool.voice_fil_env_markers_for_gui.Consume().data;

    for (auto const voice_index : ::Range(k_num_voices)) {
        auto const envelope_marker = voice_markers[voice_index];
        if (!envelope_marker.on || envelope_marker.layer_index != layer_index) continue;

        auto const env_pos = envelope_marker.pos / (f32)(UINT16_MAX);
        ASSERT(env_pos >= 0 && env_pos <= 1);
        auto const target_pos = ({
            f32 p = 0;
            switch (envelope_marker.state) {
                case adsr::State::Attack: {
                    p = bottom_left.x + env_pos * (adsr_points[k_attack_index].x - bottom_left.x);
                    break;
                }
                case adsr::State::Decay: {
                    auto const sustain_level = envelope_marker.sustain_level / (f32)UINT16_MAX;
                    ASSERT(sustain_level >= 0 && sustain_level <= 1);
                    auto const pos = 1.0f - MapTo01(env_pos, sustain_level, 1.0f);
                    p = adsr_points[k_attack_index].x +
                        pos * (adsr_points[k_decay_index].x - adsr_points[k_attack_index].x);
                    break;
                }
                case adsr::State::Sustain: {
                    p = adsr_points[k_decay_index].x;
                    break;
                }
                case adsr::State::Release: {
                    auto const pos = 1.0f - env_pos;
                    p = adsr_points[k_sustain_index].x +
                        pos * (adsr_points[k_release_index].x - adsr_points[k_sustain_index].x);
                    break;
                }
                default: PanicIfReached();
            }
            p;
        });

        auto& cursor = g.envelope_voice_cursors[(int)type][voice_index];
        if (cursor.marker_id != envelope_marker.id) {
            cursor.cursor = bottom_left.x;
            cursor.cursor_smoother.Reset();
        }
        cursor.marker_id = envelope_marker.id;

        cursor.cursor = target_pos;
        auto const cursor_x = cursor.cursor_smoother.LowPass(cursor.cursor, 0.5f);

        auto const line = ({
            Line l {};
            if (cursor_x > adsr_points[k_sustain_index].x)
                l = {adsr_points[k_sustain_index], adsr_points[k_release_index]};
            else if (cursor_x > adsr_points[k_decay_index].x)
                l = {adsr_points[k_decay_index], adsr_points[k_sustain_index]};
            else if (cursor_x > adsr_points[k_attack_index].x)
                l = {adsr_points[k_attack_index], adsr_points[k_decay_index]};
            else
                l = {bottom_left, adsr_points[k_attack_index]};
            l;
        });

        auto const cursor_y = ({
            f32 y = adsr_points[k_attack_index].y;
            if (auto p = line.IntersectionWithVerticalLine(cursor_x)) y = p->y;
            y;
        });

        DrawVoiceMarkerLine(imgui,
                            f32x2 {cursor_x, cursor_y},
                            bottom_left.y - cursor_y,
                            bottom_left.x,
                            line,
                            {});

        auto const dot_col = ChangeAlpha(LiveCol(UiColMap::WaveformLoopVoiceMarkers), 0.5f);
        imgui.draw_list->AddCircleFilled(f32x2 {cursor_x, cursor_y}, WwToPixels(2.5f), dot_col);
    }
}

void DoEnvelopeGui(GuiState& g,
                   LayerProcessor& layer,
                   Rect viewport_r,
                   bool greyed_out,
                   Array<LayerParamIndex, k_num_adsr_params> adsr_layer_params,
                   GuiEnvelopeType type) {
    ASSERT_EQ(adsr_layer_params.size, k_num_adsr_params);
    auto& imgui = g.imgui;
    auto& engine = g.engine;

    auto const handle_size = WwToPixels(30.8f);

    auto id = HashInit();
    HashUpdate(id, SourceLocationHash());
    HashUpdate(id, layer.index);
    HashUpdate(id, ToInt(adsr_layer_params[0]));

    imgui.PushId(id);
    DEFER { imgui.PopId(); };

    {
        auto const rounding = WwToPixels(k_corner_rounding);
        imgui.draw_list->AddRectFilled(imgui.ViewportRectToWindowRect(viewport_r),
                                       LiveCol(UiColMap::EnvelopeBack),
                                       rounding);
    }

    auto const padded_x = viewport_r.x;
    auto const padded_y = viewport_r.y;
    auto const padded_height = viewport_r.h;
    auto const padded_width = viewport_r.w;
    auto const padded_bottom = viewport_r.Bottom();
    constexpr auto k_att_rel_slider_sensitivity = 170.0f;

    auto const indices = ({
        Array<ParamIndex, k_num_adsr_params> ids;
        for (auto const i : ::Range(k_num_adsr_params))
            ids[i] = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[i]);
        ids;
    });

    // Background right-click menu. Registered before the grabber regions so they take precedence
    // where they overlap.
    {
        auto const bg_id = imgui.MakeId("envelope-bg");
        auto const popup_id = imgui.MakeId("envelope-bg-popup");
        auto const window_r = imgui.ViewportRectToWindowRect(viewport_r);

        EnvelopeSection const env_target {.layer_index = layer.index,
                                          .kind = type == GuiEnvelopeType::Volume
                                                      ? EnvelopeSection::Kind::Volume
                                                      : EnvelopeSection::Kind::Filter};
        String const env_label = type == GuiEnvelopeType::Volume ? "Volume Envelope"_s : "Filter Envelope"_s;

        DoRightClickMenu(
            g,
            {
                .button_id = bg_id,
                .popup_id = popup_id,
                .interaction_r = window_r,
                .do_menu_items =
                    [&](Box root) {
                        StateSnapshotSection const target_section {env_target};

                        if (MenuItem(g.builder,
                                     root,
                                     {
                                         .text = fmt::Format(g.scratch_arena, "Copy {}"_s, env_label),
                                         .no_icon_gap = true,
                                     })
                                .button_fired) {
                            g.snapshot_clipboard = GuiState::CopiedSection {
                                .snapshot = CurrentStateSnapshot(g.engine),
                                .section = target_section,
                            };
                        }

                        auto const can_paste =
                            g.snapshot_clipboard.HasValue() &&
                            g.snapshot_clipboard->section.tag == StateSnapshotSectionKind::Envelope &&
                            g.snapshot_clipboard->section.Get<EnvelopeSection>().kind == env_target.kind;

                        if (MenuItem(g.builder,
                                     root,
                                     {
                                         .text = fmt::Format(g.scratch_arena, "Paste {}"_s, env_label),
                                         .mode = can_paste ? MenuItemOptions::Mode::Active
                                                           : MenuItemOptions::Mode::Disabled,
                                         .no_icon_gap = true,
                                     })
                                .button_fired &&
                            can_paste) {
                            ApplySectionOfState(g.engine,
                                                g.snapshot_clipboard->snapshot,
                                                g.snapshot_clipboard->section,
                                                target_section);
                        }

                        DoResetSectionMenuItems(g, root, target_section, env_label);
                    },
            });
    }

    auto const attack_imgui_id = imgui.MakeId("attack");
    auto const dec_sus_imgui_id = imgui.MakeId("dec-sus");
    auto const release_imgui_id = imgui.MakeId("release");

    // Handles and grabbers sit at the parameter values; the curve follows the macro-adjusted values with a
    // faint ghost of the unadjusted shape, matching the filter graphs.
    auto const adsr_points = EnvelopePoints(viewport_r, ({
                                                Array<f32, k_num_adsr_params> vals;
                                                for (auto const i : ::Range(k_num_adsr_params))
                                                    vals[i] =
                                                        engine.processor.main_params.LinearValue(indices[i]);
                                                vals;
                                            }));
    auto const adj_adsr_points =
        EnvelopePoints(viewport_r, ({
                           auto const& params = engine.processor.main_params;
                           Array<f32, k_num_adsr_params> vals;
                           for (auto const i : ::Range(k_num_adsr_params))
                               vals[i] = AdjustedLinearValue(params.values,
                                                             engine.processor.main_macro_destinations,
                                                             params.LinearValue(indices[i]),
                                                             indices[i]);
                           vals;
                       }));

    EnvelopeXRange attack_x_range;
    EnvelopeXRange decay_x_range;
    EnvelopeXRange release_x_range;

    // Attack interaction.
    {
        auto const attack_param = engine.processor.main_params.DescribedValue(indices[k_attack_index]);
        auto const norm_attack_val = attack_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = padded_x;
            auto const max_x = min_x + (k_max_attack_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        attack_x_range.min = get_x_coord_at_percent(0);
        attack_x_range.max = get_x_coord_at_percent(1);

        auto const grabber =
            imgui.RegisterAndConvertRect({.xywh {viewport_r.x - (handle_size / 2),
                                                 viewport_r.y - (handle_size / 2),
                                                 adsr_points[k_attack_index].x - viewport_r.x + handle_size,
                                                 viewport_r.h + (handle_size / 2)}});

        auto new_value = norm_attack_val;
        auto const changed = imgui.SliderBehaviourFraction({
            .rect_in_window_coords = grabber,
            .id = attack_imgui_id,
            .fraction = new_value,
            .default_fraction = attack_param.DefaultLinearValue(),
            .cfg =
                {
                    .sensitivity = k_att_rel_slider_sensitivity,
                    .slower_with_shift = true,
                    .default_on_modifer = true,
                },
        });

        if (imgui.ButtonBehaviour(grabber,
                                  attack_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = GuiState::ParamTextEditorRequest {
                .param = indices[k_attack_index],
                .widget_id = ParamTextEditorOverlayId(imgui),
            };

        AddParamContextMenuBehaviour(g, grabber, attack_imgui_id, attack_param);

        if (imgui.IsHotOrActive(attack_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, indices[k_attack_index]);
        if (changed) SetParameterValue(engine.processor, indices[k_attack_index], new_value, {});
        if (imgui.WasJustDeactivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, indices[k_attack_index]);

        ParameterTooltip(g, attack_param, attack_imgui_id, grabber, k_nullopt, k_dragger_tooltip_footer);

        OverlayMacroDestinationRegion(g, grabber, indices[k_attack_index]);
    }

    // Decay and sustain interaction.
    {
        auto const decay_param = engine.processor.main_params.DescribedValue(indices[k_decay_index]);
        auto const sustain_param = engine.processor.main_params.DescribedValue(indices[k_sustain_index]);
        DescribedParamValue const* param_ptrs[] = {&decay_param, &sustain_param};

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = adsr_points[k_attack_index].x;
            auto const max_x = min_x + (k_max_decay_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        auto const get_y_coord_at_percent = [&](f32 percent) {
            auto const min_y = padded_y;
            auto const max_y = min_y + padded_height;
            return MapFrom01(percent, min_y, max_y);
        };

        decay_x_range.min = get_x_coord_at_percent(0);
        decay_x_range.max = get_x_coord_at_percent(1);

        auto const grabber_y = adsr_points[k_decay_index].y - (handle_size / 2);

        f32x2 const grabber_min {Min(adsr_points[k_decay_index].x - (handle_size / 2),
                                     adsr_points[k_attack_index].x + (handle_size / 2)),
                                 grabber_y};
        f32x2 const grabber_max {adsr_points[k_sustain_index].x, viewport_r.Bottom()};
        auto const grabber = imgui.RegisterAndConvertRect(Rect::FromMinMax(grabber_min, grabber_max));

        auto const& frame_input = GuiIo().in;

        // Pixels that a full 0 to 1 move of each parameter covers, so an unmodified drag keeps the handle
        // under the cursor.
        auto const decay_pixels_for_range = get_x_coord_at_percent(1) - get_x_coord_at_percent(0);
        auto const sustain_pixels_for_range = get_y_coord_at_percent(1) - get_y_coord_at_percent(0);
        ASSERT(decay_pixels_for_range > 0);
        ASSERT(sustain_pixels_for_range > 0);

        static f32x2 drag_start_pos;
        static f32 decay_at_drag_start;
        static f32 sustain_at_drag_start;

        auto const anchor_drag = [&]() {
            drag_start_pos = frame_input.cursor_pos;
            decay_at_drag_start = decay_param.LinearValue();
            sustain_at_drag_start = sustain_param.LinearValue();
        };

        if (imgui.ButtonBehaviour(grabber, dec_sus_imgui_id, imgui::SliderConfig::k_activation_cfg)) {
            ParameterJustStartedMoving(engine.processor, indices[k_decay_index]);
            ParameterJustStartedMoving(engine.processor, indices[k_sustain_index]);

            if (frame_input.modifiers.Get(ModifierKey::Modifier)) {
                SetParameterValue(engine.processor,
                                  indices[k_decay_index],
                                  decay_param.DefaultLinearValue(),
                                  {});
                SetParameterValue(engine.processor,
                                  indices[k_sustain_index],
                                  sustain_param.DefaultLinearValue(),
                                  {});
            }

            anchor_drag();
        }

        if (imgui.ButtonBehaviour(grabber,
                                  dec_sus_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = GuiState::ParamTextEditorRequest {
                .param = indices[k_decay_index],
                .widget_id = ParamTextEditorOverlayId(imgui),
            };

        AddParamContextMenuBehaviour(g, grabber, dec_sus_imgui_id, Array {decay_param, sustain_param});

        if (imgui.IsHotOrActive(dec_sus_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        if (imgui.IsActive(dec_sus_imgui_id, MouseButton::Left)) {
            // Re-anchor when fine control is engaged or released so the handle doesn't jump.
            if (frame_input.Key(KeyCode::ShiftL).presses.size ||
                frame_input.Key(KeyCode::ShiftR).presses.size ||
                frame_input.Key(KeyCode::ShiftL).releases.size ||
                frame_input.Key(KeyCode::ShiftR).releases.size)
                anchor_drag();

            if (All(frame_input.cursor_pos != -1)) {
                auto const slower = frame_input.modifiers.Get(ModifierKey::Shift) ? 4.0f : 1.0f;
                auto const delta = frame_input.cursor_pos - drag_start_pos;

                auto const new_decay =
                    Clamp01(decay_at_drag_start + (delta.x / (decay_pixels_for_range * slower)));
                auto const new_sustain =
                    Clamp01(sustain_at_drag_start - (delta.y / (sustain_pixels_for_range * slower)));

                if (new_decay != decay_param.LinearValue())
                    SetParameterValue(engine.processor, indices[k_decay_index], new_decay, {});
                if (new_sustain != sustain_param.LinearValue())
                    SetParameterValue(engine.processor, indices[k_sustain_index], new_sustain, {});
            }
        }

        if (imgui.WasJustDeactivated(dec_sus_imgui_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, indices[k_decay_index]);
            ParameterJustStoppedMoving(engine.processor, indices[k_sustain_index]);
        }

        ParameterTooltip(g, param_ptrs, dec_sus_imgui_id, grabber, k_nullopt, k_dragger_tooltip_footer);

        {
            auto const h = grabber.h / 2;
            auto macro_r = grabber;
            OverlayMacroDestinationRegion(g, rect_cut::CutTop(macro_r, h), indices[k_decay_index]);
            OverlayMacroDestinationRegion(g, rect_cut::CutTop(macro_r, h), indices[k_sustain_index]);
        }
    }

    // Release interaction.
    {
        auto const release_param = engine.processor.main_params.DescribedValue(indices[k_release_index]);
        auto const release_norm_value = release_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = adsr_points[k_sustain_index].x;
            auto const max_x = min_x + (k_max_release_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        release_x_range.min = get_x_coord_at_percent(0);
        release_x_range.max = get_x_coord_at_percent(1);

        auto const grabber =
            imgui.RegisterAndConvertRect({.xywh {adsr_points[k_sustain_index].x - (handle_size / 2),
                                                 viewport_r.y,
                                                 (k_max_release_percent * padded_width) + handle_size,
                                                 viewport_r.h + (handle_size / 2)}});

        AddParamContextMenuBehaviour(g, grabber, release_imgui_id, release_param);

        auto new_value = release_norm_value;
        auto const changed = imgui.SliderBehaviourFraction({
            .rect_in_window_coords = grabber,
            .id = release_imgui_id,
            .fraction = new_value,
            .default_fraction = release_param.DefaultLinearValue(),
            .cfg =
                {
                    .sensitivity = k_att_rel_slider_sensitivity,
                    .slower_with_shift = true,
                    .default_on_modifer = true,
                },
        });

        if (imgui.ButtonBehaviour(grabber,
                                  release_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = GuiState::ParamTextEditorRequest {
                .param = indices[k_release_index],
                .widget_id = ParamTextEditorOverlayId(imgui),
            };

        if (imgui.IsHotOrActive(release_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(release_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, indices[k_release_index]);
        if (changed) SetParameterValue(engine.processor, indices[k_release_index], new_value, {});

        if (imgui.WasJustDeactivated(release_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, indices[k_release_index]);

        ParameterTooltip(g, release_param, release_imgui_id, grabber, k_nullopt, k_dragger_tooltip_footer);

        OverlayMacroDestinationRegion(g, grabber, indices[k_release_index]);
    }

    // Drawing.
    {
        auto const to_window = [&](Array<f32x2, k_num_adsr_params> const& points) {
            Array<f32x2, k_num_adsr_params> pts;
            for (auto const i : ::Range(k_num_adsr_params))
                pts[i] = imgui.ViewportPosToWindowPos(points[i]);
            return pts;
        };
        auto const base_window_points = to_window(adsr_points);
        auto const adsr_window_points = to_window(adj_adsr_points);
        auto const base_differs = ({
            bool differs = false;
            for (auto const i : ::Range(k_num_adsr_params)) {
                if (Abs(adsr_window_points[i].x - base_window_points[i].x) > 0.5f ||
                    Abs(adsr_window_points[i].y - base_window_points[i].y) > 0.5f) {
                    differs = true;
                    break;
                }
            }
            differs;
        });
        auto const bottom_left = imgui.ViewportPosToWindowPos({padded_x, padded_bottom});

        f32x2 const point_below_decay = {adsr_window_points[k_decay_index].x, bottom_left.y};

        auto const area_col = LiveCol(UiColMap::EnvelopeArea);
        auto const greyed_out_line_col = LiveCol(UiColMap::EnvelopeLineGreyedOut);
        auto const line_col = LiveCol(UiColMap::EnvelopeLine);

        // Range lines.
        DrawEnvelopeRangeLines(imgui, attack_x_range, attack_imgui_id, padded_y, padded_bottom);
        DrawEnvelopeRangeLines(imgui, decay_x_range, dec_sus_imgui_id, padded_y, padded_bottom);
        DrawEnvelopeRangeLines(imgui, release_x_range, release_imgui_id, padded_y, padded_bottom);

        // Area fill.
        auto const area_points_a = Array {bottom_left,
                                          adsr_window_points[k_attack_index],
                                          adsr_window_points[k_decay_index],
                                          point_below_decay};
        auto const area_points_b = Array {adsr_window_points[k_decay_index],
                                          adsr_window_points[k_sustain_index],
                                          adsr_window_points[k_release_index],
                                          point_below_decay};
        imgui.draw_list->AddConvexPolyFilled(area_points_a, area_col, false);
        imgui.draw_list->AddConvexPolyFilled(area_points_b, area_col, false);

        if (!greyed_out) DrawEnvelopeVoiceMarkers(g, type, layer.index, bottom_left, adsr_window_points);

        // Lines.
        auto const line_points_for = [&](Array<f32x2, k_num_adsr_params> const& points) {
            return Array {bottom_left,
                          points[k_attack_index],
                          points[k_decay_index],
                          points[k_sustain_index],
                          points[k_release_index]};
        };
        auto const active_line_col = greyed_out ? greyed_out_line_col : line_col;
        if (base_differs) {
            auto base_line_col = FromU32(active_line_col);
            base_line_col.a /= 3;
            imgui.draw_list->AddPolyline(line_points_for(base_window_points),
                                         ToU32(base_line_col),
                                         false,
                                         1,
                                         true);
        }
        imgui.draw_list->AddPolyline(line_points_for(adsr_window_points), active_line_col, false, 1, true);

        // Handles.
        DrawEnvelopeHandle(imgui,
                           base_window_points[k_attack_index],
                           attack_imgui_id,
                           handle_size,
                           greyed_out);
        DrawEnvelopeHandle(imgui,
                           base_window_points[k_decay_index],
                           dec_sus_imgui_id,
                           handle_size,
                           greyed_out);
        DrawEnvelopeHandle(imgui,
                           base_window_points[k_release_index],
                           release_imgui_id,
                           handle_size,
                           greyed_out);
    }

    // Text editor popup.
    if (g.param_text_editor_to_open) {
        auto const cut = viewport_r.w / 3;
        Rect const edit_r {.xywh {viewport_r.x + cut, viewport_r.y, viewport_r.w - (cut * 2), viewport_r.h}};
        HandleShowingTextEditorForParams(g, edit_r, indices);
    }
}
