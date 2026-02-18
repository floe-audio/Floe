// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_envelope.hpp"

#include "gui/gui2_parameter_component.hpp"
#include "gui/gui_utils.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_state.hpp"

void DoEnvelopeGui(GuiState& g,
                   LayerProcessor& layer,
                   Rect viewport_r,
                   bool greyed_out,
                   Array<LayerParamIndex, 4> adsr_layer_params,
                   GuiEnvelopeType type) {
    ASSERT_EQ(adsr_layer_params.size, 4u);
    auto& imgui = g.imgui;
    auto& engine = g.engine;

    auto const max_attack_percent = 0.31f;
    auto const max_decay_percent = 0.31f;
    auto const max_release_percent = 0.31f;
    auto const sustain_point_percent = (max_attack_percent + max_decay_percent) +
                                       (1 - (max_attack_percent + max_decay_percent + max_release_percent));

    auto const handle_size = viewport_r.w * 0.15f;
    auto const att_rel_slider_sensitivity = 170.0f;

    imgui.PushId(SourceLocationHash() + layer.index);
    DEFER { imgui.PopId(); };

    imgui.BeginViewport(
        {
            .draw_background =
                [&handle_size](imgui::Context const& imgui) {
                    auto const& r = imgui.curr_viewport->bounds.Reduced(handle_size / 2);
                    auto const rounding = LiveSize(UiSizeId::CornerRounding);
                    imgui.draw_list->AddRectFilled(r, LiveCol(UiColMap::EnvelopeBack), rounding);
                },
            .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
        },
        viewport_r,
        "env-container");
    DEFER { imgui.EndViewport(); };

    auto const padded_x = handle_size / 2;
    auto const padded_y = handle_size / 2;
    auto const padded_height = imgui.CurrentVpHeight() - handle_size;
    auto const padded_width = imgui.CurrentVpWidth() - handle_size;
    auto const padded_bottom = imgui.CurrentVpHeight() - (handle_size / 2);

    auto const attack_imgui_id = imgui.MakeId("attack");
    auto const dec_sus_imgui_id = imgui.MakeId("dec-sus");
    auto const release_imgui_id = imgui.MakeId("release");

    f32x2 attack_point;
    f32x2 decay_point;
    f32x2 sustain_point;
    f32x2 release_point;

    struct Range {
        f32 min;
        f32 max;
    };

    Range attack_x_range;
    Range decay_x_range;
    Range release_x_range;

    {
        auto attack_param_id = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[0]);
        auto attack_param = engine.processor.main_params.DescribedValue(attack_param_id);
        auto norm_attack_val = attack_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = padded_x;
            auto const max_x = min_x + (max_attack_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        attack_point = {get_x_coord_at_percent(norm_attack_val), padded_y};
        attack_x_range.min = get_x_coord_at_percent(0);
        attack_x_range.max = get_x_coord_at_percent(1);

        auto const grabber = imgui.RegisterAndConvertRect(
            {.xywh {0, 0, attack_point.x + (handle_size / 2), imgui.CurrentVpHeight()}});

        f32 new_value = norm_attack_val;
        bool changed = false;
        if (imgui.SliderBehaviourFraction({
                .rect_in_window_coords = grabber,
                .id = attack_imgui_id,
                .fraction = new_value,
                .default_fraction = attack_param.DefaultLinearValue(),
                .cfg =
                    {
                        .sensitivity = att_rel_slider_sensitivity,
                        .slower_with_shift = true,
                        .default_on_modifer = true,
                    },
            })) {
            changed = true;
        }

        if (imgui.ButtonBehaviour(grabber,
                                  attack_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = attack_param_id;

        AddMidiLearnRightClickBehaviour(g, grabber, attack_imgui_id, attack_param);

        if (imgui.IsHotOrActive(attack_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, attack_param_id);
        if (changed) SetParameterValue(engine.processor, attack_param_id, new_value, {});
        if (imgui.WasJustDeactivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, attack_param_id);

        ParameterValuePopup(g, attack_param, attack_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, attack_param, attack_imgui_id, grabber);

        MacroAddDestinationRegion(g, grabber, attack_param_id);
    }

    {
        auto decay_id = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[1]);
        auto sustain_id = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[2]);
        auto decay_param = engine.processor.main_params.DescribedValue(decay_id);
        auto sustain_param = engine.processor.main_params.DescribedValue(sustain_id);
        DescribedParamValue const* param_ptrs[] = {&decay_param, &sustain_param};
        auto const decay_norm_value = decay_param.LinearValue();
        auto const sustain_norm_value = sustain_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = attack_point.x;
            auto const max_x = min_x + (max_decay_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        auto const get_y_coord_at_percent = [&](f32 percent) {
            auto const min_x = padded_x;
            auto const max_x = min_x + padded_height;
            return MapFrom01(percent, min_x, max_x);
        };

        decay_point = {get_x_coord_at_percent(decay_norm_value),
                       get_y_coord_at_percent(1 - sustain_norm_value)};
        sustain_point = {padded_x + (sustain_point_percent * padded_width), decay_point.y};

        decay_x_range.min = get_x_coord_at_percent(0);
        decay_x_range.max = get_x_coord_at_percent(1);

        auto const grabber_y = decay_point.y - (handle_size / 2);

        f32x2 const grabber_min {Min(decay_point.x - (handle_size / 2), attack_point.x + (handle_size / 2)),
                                 grabber_y};
        f32x2 const grabber_max {sustain_point.x, imgui.CurrentVpHeight()};
        auto grabber = Rect::FromMinMax(grabber_min, grabber_max);

        grabber = imgui.RegisterAndConvertRect(grabber);

        static f32x2 rel_click_pos;
        if (imgui.ButtonBehaviour(grabber, dec_sus_imgui_id, imgui::SliderConfig::k_activation_cfg))
            rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(decay_point);

        if (imgui.ButtonBehaviour(grabber,
                                  dec_sus_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = decay_id;

        AddMidiLearnRightClickBehaviour(g, grabber, dec_sus_imgui_id, Array {decay_param, sustain_param});

        if (imgui.IsHotOrActive(dec_sus_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        if (imgui.WasJustActivated(dec_sus_imgui_id, MouseButton::Left)) {
            ParameterJustStartedMoving(engine.processor, decay_id);
            ParameterJustStartedMoving(engine.processor, sustain_id);
        }
        if (imgui.IsActive(dec_sus_imgui_id, MouseButton::Left)) {
            {
                auto const min_pixels_pos = imgui.ViewportPosToWindowPos({get_x_coord_at_percent(0), 0}).x;
                auto const max_pixels_pos = imgui.ViewportPosToWindowPos({get_x_coord_at_percent(1), 0}).x;
                auto curr_pos = GuiIo().in.cursor_pos.x - rel_click_pos.x;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, decay_id, curr_pos_percent, {});
            }
            {
                auto const min_pixels_pos = imgui.ViewportPosToWindowPos({0, get_y_coord_at_percent(0)}).y;
                auto const max_pixels_pos = imgui.ViewportPosToWindowPos({0, get_y_coord_at_percent(1)}).y;
                auto curr_pos = GuiIo().in.cursor_pos.y - rel_click_pos.y;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, sustain_id, 1 - curr_pos_percent, {});
            }
        }

        if (imgui.WasJustDeactivated(dec_sus_imgui_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, decay_id);
            ParameterJustStoppedMoving(engine.processor, sustain_id);
        }

        ParameterValuePopup(g, param_ptrs, dec_sus_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, param_ptrs, dec_sus_imgui_id, grabber);

        {
            auto const h = grabber.h / 2;
            auto macro_r = grabber;
            MacroAddDestinationRegion(g, rect_cut::CutTop(macro_r, h), decay_id);
            MacroAddDestinationRegion(g, rect_cut::CutTop(macro_r, h), sustain_id);
        }
    }

    {
        auto release_param_id = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[3]);
        auto release_param = engine.processor.main_params.DescribedValue(release_param_id);
        auto const release_norm_value = release_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = sustain_point.x;
            auto const max_x = min_x + (max_release_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        release_point = {get_x_coord_at_percent(release_norm_value), padded_bottom};

        release_x_range.min = get_x_coord_at_percent(0);
        release_x_range.max = get_x_coord_at_percent(1);

        auto const grabber =
            imgui.RegisterAndConvertRect({.xywh {sustain_point.x,
                                                 0,
                                                 (max_release_percent * padded_width) + (handle_size / 2),
                                                 imgui.CurrentVpHeight()}});

        AddMidiLearnRightClickBehaviour(g, grabber, release_imgui_id, release_param);

        f32 new_value = release_norm_value;
        bool changed = false;
        if (imgui.SliderBehaviourFraction({
                .rect_in_window_coords = grabber,
                .id = release_imgui_id,
                .fraction = new_value,
                .default_fraction = release_param.DefaultLinearValue(),
                .cfg =
                    {
                        .sensitivity = att_rel_slider_sensitivity,
                        .slower_with_shift = true,
                        .default_on_modifer = true,
                    },
            })) {
            changed = true;
        }

        if (imgui.ButtonBehaviour(grabber,
                                  release_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = release_param_id;

        if (imgui.IsHotOrActive(release_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(release_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, release_param_id);
        if (changed) SetParameterValue(engine.processor, release_param_id, new_value, {});

        if (imgui.WasJustDeactivated(release_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, release_param_id);

        ParameterValuePopup(g, release_param, release_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, release_param, release_imgui_id, grabber);

        MacroAddDestinationRegion(g, grabber, release_param_id);
    }

    {

        auto const attack_point_window = imgui.ViewportPosToWindowPos(attack_point);
        auto const decay_point_window = imgui.ViewportPosToWindowPos(decay_point);
        auto const sustain_point_window = imgui.ViewportPosToWindowPos(sustain_point);
        auto const release_point_window = imgui.ViewportPosToWindowPos(release_point);
        auto const bottom_left = imgui.ViewportPosToWindowPos({padded_x, padded_bottom});

        f32x2 const point_below_decay = {decay_point_window.x, bottom_left.y};

        auto const area_col = LiveCol(UiColMap::EnvelopeArea);
        auto const range_lines_col = LiveCol(UiColMap::EnvelopeRangeLines);
        auto const hover_col = LiveCol(UiColMap::EnvelopeHandleHover);
        auto const greyed_out_line_col = LiveCol(UiColMap::EnvelopeLineGreyedOut);
        auto const greyed_out_handle_col = LiveCol(UiColMap::EnvelopeHandleGreyedOut);
        auto line_col = LiveCol(UiColMap::EnvelopeLine);
        auto handle_col = LiveCol(UiColMap::EnvelopeHandle);

        auto const handle_visible_size = handle_size / 10;

        // range lines
        auto const do_range_lines = [&](Range range, imgui::Id id) {
            if (imgui.IsActive(id, MouseButton::Left)) {
                imgui.draw_list->AddLine(imgui.ViewportPosToWindowPos({range.min, padded_x}),
                                         imgui.ViewportPosToWindowPos({range.min, padded_bottom}),
                                         range_lines_col);
                imgui.draw_list->AddLine(imgui.ViewportPosToWindowPos({range.max, padded_x}),
                                         imgui.ViewportPosToWindowPos({range.max, padded_bottom}),
                                         range_lines_col);
            }
        };

        do_range_lines(attack_x_range, attack_imgui_id);
        do_range_lines(decay_x_range, dec_sus_imgui_id);
        do_range_lines(release_x_range, release_imgui_id);

        // Area under line, done with poly fill instead of a series of triangles/rects because it gives
        // better results
        auto const area_points_a =
            Array {bottom_left, attack_point_window, decay_point_window, point_below_decay};
        auto const area_points_b =
            Array {decay_point_window, sustain_point_window, release_point_window, point_below_decay};
        imgui.draw_list->AddConvexPolyFilled(area_points_a, area_col, false);
        imgui.draw_list->AddConvexPolyFilled(area_points_b, area_col, false);

        if (!greyed_out) {
            auto& voice_markers =
                type == GuiEnvelopeType::Volume
                    ? engine.processor.voice_pool.voice_vol_env_markers_for_gui.Consume().data
                    : engine.processor.voice_pool.voice_fil_env_markers_for_gui.Consume().data;

            for (auto const voice_index : ::Range(k_num_voices)) {
                auto const envelope_marker = voice_markers[voice_index];
                if (envelope_marker.on && envelope_marker.layer_index == layer.index) {
                    f32 target_pos = 0;
                    f32 const env_pos = envelope_marker.pos / (f32)(UINT16_MAX);
                    ASSERT(env_pos >= 0 && env_pos <= 1);
                    switch (envelope_marker.state) {
                        case adsr::State::Attack: {
                            target_pos = bottom_left.x + env_pos * (attack_point_window.x - bottom_left.x);
                            break;
                        }
                        case adsr::State::Decay: {
                            auto const sustain_level = envelope_marker.sustain_level / (f32)UINT16_MAX;
                            ASSERT(sustain_level >= 0 && sustain_level <= 1);
                            auto const pos = 1.0f - MapTo01(env_pos, sustain_level, 1.0f);
                            target_pos =
                                attack_point_window.x + pos * (decay_point_window.x - attack_point_window.x);
                            break;
                        }
                        case adsr::State::Sustain: {
                            target_pos = decay_point_window.x;
                            break;
                        }
                        case adsr::State::Release: {
                            auto const pos = 1.0f - env_pos;
                            target_pos = sustain_point_window.x +
                                         pos * (release_point_window.x - sustain_point_window.x);
                            break;
                        }
                        default: PanicIfReached();
                    }

                    auto& cursor = g.envelope_voice_cursors[(int)type][voice_index];
                    if (cursor.marker_id != envelope_marker.id) {
                        cursor.cursor = bottom_left.x;
                        cursor.cursor_smoother.Reset();
                    }
                    cursor.marker_id = envelope_marker.id;

                    cursor.cursor = target_pos;
                    f32 const cursor_x = cursor.cursor_smoother.LowPass(cursor.cursor, 0.5f);

                    Line line {};
                    if (cursor_x > sustain_point_window.x)
                        line = {sustain_point_window, release_point_window};
                    else if (cursor_x > decay_point_window.x)
                        line = {decay_point_window, sustain_point_window};
                    else if (cursor_x > attack_point_window.x)
                        line = {attack_point_window, decay_point_window};
                    else
                        line = {bottom_left, attack_point_window};

                    f32 cursor_y = attack_point_window.y;
                    if (auto p = line.IntersectionWithVerticalLine(cursor_x)) cursor_y = p->y;

                    DrawVoiceMarkerLine(imgui,
                                        f32x2 {cursor_x, cursor_y},
                                        bottom_left.y - cursor_y,
                                        bottom_left.x,
                                        line);
                }
            }
        }

        // lines
        auto const line_points = Array {bottom_left,
                                        attack_point_window,
                                        decay_point_window,
                                        sustain_point_window,
                                        release_point_window};
        imgui.draw_list->AddPolyline(line_points,
                                     greyed_out ? greyed_out_line_col : line_col,
                                     false,
                                     1,
                                     true);

        // handles
        auto do_handle = [&](f32x2 point, imgui::Id id) {
            auto col = greyed_out ? greyed_out_handle_col : handle_col;
            if (imgui.IsHot(id)) {
                auto background_col = FromU32(col);
                background_col.a /= 2;
                imgui.draw_list->AddCircleFilled(point, handle_size / 5, ToU32(background_col));
                col = hover_col;
            }
            if (imgui.IsActive(id, MouseButton::Left)) col = hover_col;
            imgui.draw_list->AddCircleFilled(point, handle_visible_size, col);
        };
        do_handle(attack_point_window, attack_imgui_id);
        do_handle(decay_point_window, dec_sus_imgui_id);
        do_handle(release_point_window, release_imgui_id);
    }

    if (g.param_text_editor_to_open) {
        ParamIndex const params[] = {
            ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[0]),
            ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[1]),
            ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[2]),
            ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[3]),
        };

        auto const cut = imgui.CurrentVpWidth() / 3;
        Rect const edit_r {.xywh {cut, 0, imgui.CurrentVpWidth() - (cut * 2), imgui.CurrentVpHeight()}};
        HandleShowingTextEditorForParams(g, edit_r, params);
    }
}
