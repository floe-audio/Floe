// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_envelope.hpp"

#include "gui.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_widget_helpers.hpp"

void GUIDoEnvelope(Gui* g,
                   LayerProcessor* layer,
                   Rect r,
                   bool greyed_out,
                   Array<LayerParamIndex, 4> adsr_layer_params,
                   GuiEnvelopeType type) {
    ASSERT_EQ(adsr_layer_params.size, 4u);
    auto& imgui = g->imgui;
    auto& engine = g->engine;

    auto const max_attack_percent = 0.31f;
    auto const max_decay_percent = 0.31f;
    auto const max_release_percent = 0.31f;
    auto const sustain_point_percent = (max_attack_percent + max_decay_percent) +
                                       (1 - (max_attack_percent + max_decay_percent + max_release_percent));

    auto const handle_size = r.w * 0.15f;
    auto const att_rel_slider_sensitivity = 750.0f;

    auto settings = imgui::DefWindow();
    settings.pad_bottom_right = {};
    settings.pad_top_left = {};
    settings.draw_routine_window_background = [&handle_size](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto const& r = window->bounds.Reduced(handle_size / 2);
        auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::Envelope_Back), rounding);
    };
    imgui.PushID(layer->index);
    DEFER { imgui.PopID(); };
    imgui.BeginWindow(settings, imgui.GetID("envelope container"), r);
    DEFER { imgui.EndWindow(); };

    auto const padded_x = handle_size / 2;
    auto const padded_y = handle_size / 2;
    auto const padded_height = imgui.Height() - handle_size;
    auto const padded_width = imgui.Width() - handle_size;
    auto const padded_bottom = imgui.Height() - (handle_size / 2);

    auto const attack_imgui_id = imgui.GetID("attack");
    auto const dec_sus_imgui_id = imgui.GetID("dec-sus");
    auto const release_imgui_id = imgui.GetID("release");

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
        auto attack_param_id = ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[0]);
        auto& attack_param = engine.processor.params[ToInt(attack_param_id)];
        auto norm_attack_val = attack_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = padded_x;
            auto const max_x = min_x + (max_attack_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        attack_point = {get_x_coord_at_percent(norm_attack_val), padded_y};
        attack_x_range.min = get_x_coord_at_percent(0);
        attack_x_range.max = get_x_coord_at_percent(1);

        Rect grabber = {.xywh {0, 0, attack_point.x + (handle_size / 2), imgui.Height()}};
        auto const grabber_unregistered = grabber;
        MidiLearnMenu(g, attack_param_id, grabber_unregistered);
        imgui.RegisterAndConvertRect(&grabber);

        f32 new_value = norm_attack_val;
        bool changed = false;
        if (imgui.SliderBehavior(grabber,
                                 attack_imgui_id,
                                 new_value,
                                 attack_param.DefaultLinearValue(),
                                 att_rel_slider_sensitivity,
                                 {.slower_with_shift = true, .default_on_modifer = true})) {
            changed = true;
        }

        if (imgui.IsHotOrActive(attack_imgui_id)) {
            imgui.frame_output.cursor_type = CursorType::HorizontalArrows;
            if (imgui::ClickCheck(
                    {
                        .left_mouse = true,
                        .double_click = true,
                        .triggers_on_mouse_down = true,
                    },
                    imgui.frame_input))
                g->param_text_editor_to_open = attack_param_id;
        }

        if (imgui.WasJustActivated(attack_imgui_id))
            ParameterJustStartedMoving(engine.processor, attack_param_id);
        if (changed) SetParameterValue(engine.processor, attack_param_id, new_value, {});
        if (imgui.WasJustDeactivated(attack_imgui_id))
            ParameterJustStoppedMoving(engine.processor, attack_param_id);

        ParameterValuePopup(g, attack_param, attack_imgui_id, grabber_unregistered);
        DoParameterTooltipIfNeeded(g, attack_param, attack_imgui_id, grabber_unregistered);
    }

    {
        auto decay_id = ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[1]);
        auto sustain_id = ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[2]);
        auto& decay_param = engine.processor.params[ToInt(decay_id)];
        auto& sustain_param = engine.processor.params[ToInt(sustain_id)];
        ParamIndex params[] = {decay_id, sustain_id};
        Parameter const* param_ptrs[] = {&decay_param, &sustain_param};
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
        f32x2 const grabber_max {sustain_point.x, imgui.Height()};
        auto grabber = Rect::FromMinMax(grabber_min, grabber_max);
        auto const grabber_unregistered = grabber;

        MidiLearnMenu(g, params, grabber);

        imgui.RegisterAndConvertRect(&grabber);
        static f32x2 rel_click_pos;
        if (imgui.ButtonBehavior(grabber,
                                 dec_sus_imgui_id,
                                 {.left_mouse = true, .triggers_on_mouse_down = true})) {
            rel_click_pos = imgui.frame_input.cursor_pos - imgui.WindowPosToScreenPos(decay_point);
        }

        if (imgui.IsHotOrActive(dec_sus_imgui_id)) {
            imgui.frame_output.cursor_type = CursorType::AllArrows;
            if (imgui::ClickCheck(
                    {
                        .left_mouse = true,
                        .double_click = true,
                        .triggers_on_mouse_down = true,
                    },
                    imgui.frame_input))
                g->param_text_editor_to_open = decay_id;
        }

        if (imgui.WasJustActivated(dec_sus_imgui_id)) {
            ParameterJustStartedMoving(engine.processor, decay_id);
            ParameterJustStartedMoving(engine.processor, sustain_id);
        }
        if (imgui.IsActive(dec_sus_imgui_id)) {
            {
                auto const min_pixels_pos = imgui.WindowPosToScreenPos({get_x_coord_at_percent(0), 0}).x;
                auto const max_pixels_pos = imgui.WindowPosToScreenPos({get_x_coord_at_percent(1), 0}).x;
                auto curr_pos = imgui.frame_input.cursor_pos.x - rel_click_pos.x;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, decay_id, curr_pos_percent, {});
            }
            {
                auto const min_pixels_pos = imgui.WindowPosToScreenPos({0, get_y_coord_at_percent(0)}).y;
                auto const max_pixels_pos = imgui.WindowPosToScreenPos({0, get_y_coord_at_percent(1)}).y;
                auto curr_pos = imgui.frame_input.cursor_pos.y - rel_click_pos.y;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, sustain_id, 1 - curr_pos_percent, {});
            }
        }

        if (imgui.WasJustDeactivated(dec_sus_imgui_id)) {
            ParameterJustStoppedMoving(engine.processor, decay_id);
            ParameterJustStoppedMoving(engine.processor, sustain_id);
        }

        ParameterValuePopup(g, param_ptrs, dec_sus_imgui_id, grabber_unregistered);
        DoParameterTooltipIfNeeded(g, param_ptrs, dec_sus_imgui_id, grabber_unregistered);
    }

    {
        auto release_param_id = ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[3]);
        auto& release_param = engine.processor.params[ToInt(release_param_id)];
        auto const release_norm_value = release_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = sustain_point.x;
            auto const max_x = min_x + (max_release_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        release_point = {get_x_coord_at_percent(release_norm_value), padded_bottom};

        release_x_range.min = get_x_coord_at_percent(0);
        release_x_range.max = get_x_coord_at_percent(1);

        Rect grabber {.xywh {sustain_point.x,
                             0,
                             (max_release_percent * padded_width) + (handle_size / 2),
                             imgui.Height()}};
        auto const grabber_unregistered = grabber;

        MidiLearnMenu(g, release_param_id, grabber_unregistered);
        imgui.RegisterAndConvertRect(&grabber);

        f32 new_value = release_norm_value;
        bool changed = false;
        if (imgui.SliderBehavior(grabber,
                                 release_imgui_id,
                                 new_value,
                                 release_param.DefaultLinearValue(),
                                 att_rel_slider_sensitivity,
                                 {.slower_with_shift = true, .default_on_modifer = true})) {
            changed = true;
        }

        if (imgui.IsHotOrActive(release_imgui_id)) {
            imgui.frame_output.cursor_type = CursorType::HorizontalArrows;
            if (imgui::ClickCheck(
                    {
                        .left_mouse = true,
                        .double_click = true,
                        .triggers_on_mouse_down = true,
                    },
                    imgui.frame_input))
                g->param_text_editor_to_open = release_param_id;
        }

        if (imgui.WasJustActivated(release_imgui_id))
            ParameterJustStartedMoving(engine.processor, release_param_id);
        if (changed) SetParameterValue(engine.processor, release_param_id, new_value, {});

        if (imgui.WasJustDeactivated(release_imgui_id))
            ParameterJustStoppedMoving(engine.processor, release_param_id);

        ParameterValuePopup(g, release_param, release_imgui_id, grabber_unregistered);
        DoParameterTooltipIfNeeded(g, release_param, release_imgui_id, grabber_unregistered);
    }

    {

        auto const attack_point_screen = imgui.WindowPosToScreenPos(attack_point);
        auto const decay_point_screen = imgui.WindowPosToScreenPos(decay_point);
        auto const sustain_point_screen = imgui.WindowPosToScreenPos(sustain_point);
        auto const release_point_screen = imgui.WindowPosToScreenPos(release_point);
        auto const bottom_left = imgui.WindowPosToScreenPos({padded_x, padded_bottom});

        f32x2 const point_below_decay = {decay_point_screen.x, bottom_left.y};

        auto const area_col = LiveCol(imgui, UiColMap::Envelope_Area);
        auto const range_lines_col = LiveCol(imgui, UiColMap::Envelope_RangeLines);
        auto const hover_col = LiveCol(imgui, UiColMap::Envelope_HandleHover);
        auto const greyed_out_line_col = LiveCol(imgui, UiColMap::Envelope_LineGreyedOut);
        auto const greyed_out_handle_col = LiveCol(imgui, UiColMap::Envelope_HandleGreyedOut);
        auto line_col = LiveCol(imgui, UiColMap::Envelope_Line);
        auto handle_col = LiveCol(imgui, UiColMap::Envelope_Handle);

        auto const handle_visible_size = handle_size / 10;

        // range lines
        auto const do_range_lines = [&](Range range, imgui::Id id) {
            if (imgui.IsActive(id)) {
                imgui.graphics->AddLine(imgui.WindowPosToScreenPos({range.min, padded_x}),
                                        imgui.WindowPosToScreenPos({range.min, padded_bottom}),
                                        range_lines_col);
                imgui.graphics->AddLine(imgui.WindowPosToScreenPos({range.max, padded_x}),
                                        imgui.WindowPosToScreenPos({range.max, padded_bottom}),
                                        range_lines_col);
            }
        };

        do_range_lines(attack_x_range, attack_imgui_id);
        do_range_lines(decay_x_range, dec_sus_imgui_id);
        do_range_lines(release_x_range, release_imgui_id);

        // Area under line, done with poly fill instead of a series of triangles/rects because it gives
        // better results
        f32x2 area_points_a[4] = {bottom_left, attack_point_screen, decay_point_screen, point_below_decay};
        f32x2 area_points_b[4] = {decay_point_screen,
                                  sustain_point_screen,
                                  release_point_screen,
                                  point_below_decay};
        imgui.graphics->AddConvexPolyFilled(area_points_a, (int)ArraySize(area_points_a), area_col, false);
        imgui.graphics->AddConvexPolyFilled(area_points_b, (int)ArraySize(area_points_b), area_col, false);

        auto& voice_markers = type == GuiEnvelopeType::Volume
                                  ? engine.processor.voice_pool.voice_vol_env_markers_for_gui.Consume().data
                                  : engine.processor.voice_pool.voice_fil_env_markers_for_gui.Consume().data;

        for (auto const voice_index : ::Range(k_num_voices)) {
            auto const envelope_marker = voice_markers[voice_index];
            if (envelope_marker.on && envelope_marker.layer_index == layer->index) {
                f32 target_pos = 0;
                f32 const env_pos = envelope_marker.pos / (f32)(UINT16_MAX);
                ASSERT(env_pos >= 0 && env_pos <= 1);
                switch (envelope_marker.state) {
                    case adsr::State::Attack: {
                        target_pos = bottom_left.x + env_pos * (attack_point_screen.x - bottom_left.x);
                        break;
                    }
                    case adsr::State::Decay: {
                        auto const sustain_level = envelope_marker.sustain_level / (f32)UINT16_MAX;
                        ASSERT(sustain_level >= 0 && sustain_level <= 1);
                        auto const pos = 1.0f - MapTo01(env_pos, sustain_level, 1.0f);
                        target_pos =
                            attack_point_screen.x + pos * (decay_point_screen.x - attack_point_screen.x);
                        break;
                    }
                    case adsr::State::Sustain: {
                        target_pos = decay_point_screen.x;
                        break;
                    }
                    case adsr::State::Release: {
                        auto const pos = 1.0f - env_pos;
                        target_pos =
                            sustain_point_screen.x + pos * (release_point_screen.x - sustain_point_screen.x);
                        break;
                    }
                    default: PanicIfReached();
                }

                auto& cursor = g->envelope_voice_cursors[(int)type][voice_index];
                if (cursor.marker_id != envelope_marker.id) cursor.smoother.ResetWithValue(bottom_left.x);
                cursor.marker_id = envelope_marker.id;

                cursor.smoother.SetValue(target_pos);
                f32 const cursor_x = cursor.smoother.GetValue(0.5f);

                Line line {};
                if (cursor_x > sustain_point_screen.x)
                    line = {sustain_point_screen, release_point_screen};
                else if (cursor_x > decay_point_screen.x)
                    line = {decay_point_screen, sustain_point_screen};
                else if (cursor_x > attack_point_screen.x)
                    line = {attack_point_screen, decay_point_screen};
                else
                    line = {bottom_left, attack_point_screen};

                f32 cursor_y = attack_point_screen.y;
                if (auto p = line.IntersectionWithVerticalLine(cursor_x)) cursor_y = p->y;

                draw::VoiceMarkerLine(imgui,
                                      {cursor_x, cursor_y},
                                      bottom_left.y - cursor_y,
                                      bottom_left.x,
                                      line);
            }
        }

        // lines
        f32x2 line_points[5] = {bottom_left,
                                attack_point_screen,
                                decay_point_screen,
                                sustain_point_screen,
                                release_point_screen};
        imgui.graphics
            ->AddPolyline(line_points, 5, greyed_out ? greyed_out_line_col : line_col, false, 1, true);

        // handles
        auto do_handle = [&](f32x2 point, u32 id) {
            auto col = greyed_out ? greyed_out_handle_col : handle_col;
            if (imgui.IsHot(id)) {
                auto background_col = colours::FromU32(col);
                background_col.a /= 2;
                imgui.graphics->AddCircleFilled(point, handle_size / 5, colours::ToU32(background_col));
                col = hover_col;
            }
            if (imgui.IsActive(id)) col = hover_col;
            imgui.graphics->AddCircleFilled(point, handle_visible_size, col);
        };
        do_handle(attack_point_screen, attack_imgui_id);
        do_handle(decay_point_screen, dec_sus_imgui_id);
        do_handle(release_point_screen, release_imgui_id);
    }

    if (g->param_text_editor_to_open) {
        ParamIndex const params[] = {
            ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[0]),
            ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[1]),
            ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[2]),
            ParamIndexFromLayerParamIndex(layer->index, adsr_layer_params[3]),
        };

        auto const cut = imgui.Width() / 3;
        Rect const edit_r {.xywh {cut, 0, imgui.Width() - (cut * 2), imgui.Height()}};
        HandleShowingTextEditorForParams(g, edit_r, params);
    }
}
