// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_envelope.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui2_parameter_component.hpp"
#include "gui/elements/gui_drawing_helpers.hpp"
#include "gui/elements/gui_utils.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"

constexpr usize k_attack_index = 0;
constexpr usize k_decay_index = 1;
constexpr usize k_sustain_index = 2;
constexpr usize k_release_index = 3;

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
    auto const handle_visible_size = handle_size / 10;
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

        DrawVoiceMarkerLine(imgui, f32x2 {cursor_x, cursor_y}, bottom_left.y - cursor_y, bottom_left.x, line);
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

    auto const handle_size = GuiIo().WwToPixels(30.8f);

    imgui.PushId(SourceLocationHash() + layer.index);
    DEFER { imgui.PopId(); };

    imgui.BeginViewport(
        {
            .draw_background =
                [&handle_size](imgui::Context const& imgui) {
                    auto const& r = imgui.curr_viewport->bounds.Reduced(handle_size / 2);
                    auto const rounding = LivePx(UiSizeId::CornerRounding);
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
    constexpr auto k_max_attack_percent = 0.31f;
    constexpr auto k_max_decay_percent = 0.31f;
    constexpr auto k_max_release_percent = 0.31f;
    constexpr auto k_sustain_point_percent =
        (k_max_attack_percent + k_max_decay_percent) +
        (1 - (k_max_attack_percent + k_max_decay_percent + k_max_release_percent));
    constexpr auto k_att_rel_slider_sensitivity = 170.0f;

    auto const indices = ({
        Array<ParamIndex, k_num_adsr_params> ids;
        for (auto const i : ::Range(k_num_adsr_params))
            ids[i] = ParamIndexFromLayerParamIndex(layer.index, adsr_layer_params[i]);
        ids;
    });

    auto const attack_imgui_id = imgui.MakeId("attack");
    auto const dec_sus_imgui_id = imgui.MakeId("dec-sus");
    auto const release_imgui_id = imgui.MakeId("release");

    Array<f32x2, k_num_adsr_params> adsr_points;

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

        adsr_points[k_attack_index] = {get_x_coord_at_percent(norm_attack_val), padded_y};
        attack_x_range.min = get_x_coord_at_percent(0);
        attack_x_range.max = get_x_coord_at_percent(1);

        auto const grabber = imgui.RegisterAndConvertRect(
            {.xywh {0, 0, adsr_points[k_attack_index].x + (handle_size / 2), imgui.CurrentVpHeight()}});

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
            g.param_text_editor_to_open = indices[k_attack_index];

        AddParamContextMenuBehaviour(g, grabber, attack_imgui_id, attack_param);

        if (imgui.IsHotOrActive(attack_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, indices[k_attack_index]);
        if (changed) SetParameterValue(engine.processor, indices[k_attack_index], new_value, {});
        if (imgui.WasJustDeactivated(attack_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, indices[k_attack_index]);

        ParameterValuePopup(g, attack_param, attack_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, attack_param, attack_imgui_id, grabber);

        MacroAddDestinationRegion(g, grabber, indices[k_attack_index]);
    }

    // Decay and sustain interaction.
    {
        auto const decay_param = engine.processor.main_params.DescribedValue(indices[k_decay_index]);
        auto const sustain_param = engine.processor.main_params.DescribedValue(indices[k_sustain_index]);
        DescribedParamValue const* param_ptrs[] = {&decay_param, &sustain_param};
        auto const decay_norm_value = decay_param.LinearValue();
        auto const sustain_norm_value = sustain_param.LinearValue();

        auto const get_x_coord_at_percent = [&](f32 percent) {
            auto const min_x = adsr_points[k_attack_index].x;
            auto const max_x = min_x + (k_max_decay_percent * padded_width);
            return MapFrom01(percent, min_x, max_x);
        };

        auto const get_y_coord_at_percent = [&](f32 percent) {
            auto const min_x = padded_x;
            auto const max_x = min_x + padded_height;
            return MapFrom01(percent, min_x, max_x);
        };

        adsr_points[k_decay_index] = {get_x_coord_at_percent(decay_norm_value),
                                      get_y_coord_at_percent(1 - sustain_norm_value)};
        adsr_points[k_sustain_index] = {padded_x + (k_sustain_point_percent * padded_width),
                                        adsr_points[k_decay_index].y};

        decay_x_range.min = get_x_coord_at_percent(0);
        decay_x_range.max = get_x_coord_at_percent(1);

        auto const grabber_y = adsr_points[k_decay_index].y - (handle_size / 2);

        f32x2 const grabber_min {Min(adsr_points[k_decay_index].x - (handle_size / 2),
                                     adsr_points[k_attack_index].x + (handle_size / 2)),
                                 grabber_y};
        f32x2 const grabber_max {adsr_points[k_sustain_index].x, imgui.CurrentVpHeight()};
        auto const grabber = imgui.RegisterAndConvertRect(Rect::FromMinMax(grabber_min, grabber_max));

        static f32x2 rel_click_pos;
        if (imgui.ButtonBehaviour(grabber, dec_sus_imgui_id, imgui::SliderConfig::k_activation_cfg))
            rel_click_pos = GuiIo().in.cursor_pos - imgui.ViewportPosToWindowPos(adsr_points[k_decay_index]);

        if (imgui.ButtonBehaviour(grabber,
                                  dec_sus_imgui_id,
                                  {
                                      .mouse_button = MouseButton::Left,
                                      .event = MouseButtonEvent::DoubleClick,
                                  }))
            g.param_text_editor_to_open = indices[k_decay_index];

        AddParamContextMenuBehaviour(g, grabber, dec_sus_imgui_id, Array {decay_param, sustain_param});

        if (imgui.IsHotOrActive(dec_sus_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        if (imgui.WasJustActivated(dec_sus_imgui_id, MouseButton::Left)) {
            ParameterJustStartedMoving(engine.processor, indices[k_decay_index]);
            ParameterJustStartedMoving(engine.processor, indices[k_sustain_index]);
        }
        if (imgui.IsActive(dec_sus_imgui_id, MouseButton::Left)) {
            {
                auto const min_pixels_pos = imgui.ViewportPosToWindowPos({get_x_coord_at_percent(0), 0}).x;
                auto const max_pixels_pos = imgui.ViewportPosToWindowPos({get_x_coord_at_percent(1), 0}).x;
                auto curr_pos = GuiIo().in.cursor_pos.x - rel_click_pos.x;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, indices[k_decay_index], curr_pos_percent, {});
            }
            {
                auto const min_pixels_pos = imgui.ViewportPosToWindowPos({0, get_y_coord_at_percent(0)}).y;
                auto const max_pixels_pos = imgui.ViewportPosToWindowPos({0, get_y_coord_at_percent(1)}).y;
                auto curr_pos = GuiIo().in.cursor_pos.y - rel_click_pos.y;

                curr_pos = Clamp(curr_pos, min_pixels_pos, max_pixels_pos);
                auto const curr_pos_percent = MapTo01(curr_pos, min_pixels_pos, max_pixels_pos);

                SetParameterValue(engine.processor, indices[k_sustain_index], 1 - curr_pos_percent, {});
            }
        }

        if (imgui.WasJustDeactivated(dec_sus_imgui_id, MouseButton::Left)) {
            ParameterJustStoppedMoving(engine.processor, indices[k_decay_index]);
            ParameterJustStoppedMoving(engine.processor, indices[k_sustain_index]);
        }

        ParameterValuePopup(g, param_ptrs, dec_sus_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, param_ptrs, dec_sus_imgui_id, grabber);

        {
            auto const h = grabber.h / 2;
            auto macro_r = grabber;
            MacroAddDestinationRegion(g, rect_cut::CutTop(macro_r, h), indices[k_decay_index]);
            MacroAddDestinationRegion(g, rect_cut::CutTop(macro_r, h), indices[k_sustain_index]);
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

        adsr_points[k_release_index] = {get_x_coord_at_percent(release_norm_value), padded_bottom};

        release_x_range.min = get_x_coord_at_percent(0);
        release_x_range.max = get_x_coord_at_percent(1);

        auto const grabber =
            imgui.RegisterAndConvertRect({.xywh {adsr_points[k_sustain_index].x,
                                                 0,
                                                 (k_max_release_percent * padded_width) + (handle_size / 2),
                                                 imgui.CurrentVpHeight()}});

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
            g.param_text_editor_to_open = indices[k_release_index];

        if (imgui.IsHotOrActive(release_imgui_id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (imgui.WasJustActivated(release_imgui_id, MouseButton::Left))
            ParameterJustStartedMoving(engine.processor, indices[k_release_index]);
        if (changed) SetParameterValue(engine.processor, indices[k_release_index], new_value, {});

        if (imgui.WasJustDeactivated(release_imgui_id, MouseButton::Left))
            ParameterJustStoppedMoving(engine.processor, indices[k_release_index]);

        ParameterValuePopup(g, release_param, release_imgui_id, grabber);
        DoParameterTooltipIfNeeded(g, release_param, release_imgui_id, grabber);

        MacroAddDestinationRegion(g, grabber, indices[k_release_index]);
    }

    // Drawing.
    {
        auto const adsr_window_points = ({
            Array<f32x2, k_num_adsr_params> pts;
            for (auto const i : ::Range(k_num_adsr_params))
                pts[i] = imgui.ViewportPosToWindowPos(adsr_points[i]);
            pts;
        });
        auto const bottom_left = imgui.ViewportPosToWindowPos({padded_x, padded_bottom});

        f32x2 const point_below_decay = {adsr_window_points[k_decay_index].x, bottom_left.y};

        auto const area_col = LiveCol(UiColMap::EnvelopeArea);
        auto const greyed_out_line_col = LiveCol(UiColMap::EnvelopeLineGreyedOut);
        auto const line_col = LiveCol(UiColMap::EnvelopeLine);

        // Range lines.
        DrawEnvelopeRangeLines(imgui, attack_x_range, attack_imgui_id, padded_x, padded_bottom);
        DrawEnvelopeRangeLines(imgui, decay_x_range, dec_sus_imgui_id, padded_x, padded_bottom);
        DrawEnvelopeRangeLines(imgui, release_x_range, release_imgui_id, padded_x, padded_bottom);

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
        auto const line_points = Array {bottom_left,
                                        adsr_window_points[k_attack_index],
                                        adsr_window_points[k_decay_index],
                                        adsr_window_points[k_sustain_index],
                                        adsr_window_points[k_release_index]};
        imgui.draw_list->AddPolyline(line_points,
                                     greyed_out ? greyed_out_line_col : line_col,
                                     false,
                                     1,
                                     true);

        // Handles.
        DrawEnvelopeHandle(imgui,
                           adsr_window_points[k_attack_index],
                           attack_imgui_id,
                           handle_size,
                           greyed_out);
        DrawEnvelopeHandle(imgui,
                           adsr_window_points[k_decay_index],
                           dec_sus_imgui_id,
                           handle_size,
                           greyed_out);
        DrawEnvelopeHandle(imgui,
                           adsr_window_points[k_release_index],
                           release_imgui_id,
                           handle_size,
                           greyed_out);
    }

    // Text editor popup.
    if (g.param_text_editor_to_open) {
        auto const cut = imgui.CurrentVpWidth() / 3;
        Rect const edit_r {.xywh {cut, 0, imgui.CurrentVpWidth() - (cut * 2), imgui.CurrentVpHeight()}};
        HandleShowingTextEditorForParams(g, edit_r, indices);
    }
}
