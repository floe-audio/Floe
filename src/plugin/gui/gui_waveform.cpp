// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_waveform.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/loop_modes.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui/gui_utils.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_state.hpp"
#include "gui_waveform_images.hpp"
#include "processor/layer_processor.hpp"
#include "processor/sample_processing.hpp"

static void DoWaveformControls(GuiState& g, LayerProcessor& layer, Rect r) {
    if (layer.instrument_id.tag == InstrumentType::WaveformSynth) return;

    auto const handle_height = LiveSize(UiSizeId::MainWaveformHandleHeight);
    auto const handle_width = LiveSize(UiSizeId::MainWaveformHandleWidth);
    constexpr auto k_epsilon = 0.001f;
    constexpr auto k_slider_sensitivity = 320.0f;

    auto const& params = g.engine.processor.main_params;

    auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
    auto const desired_loop_mode =
        params.IntValue<param_values::LoopMode>(layer.index, LayerParamIndex::LoopMode);
    auto const mode =
        ActualLoopBehaviour(layer.instrument, desired_loop_mode, layer.VolumeEnvelopeIsOn(params));

    struct SingleBuiltinLoop {
        f32 start;
        f32 end;
        f32 crossfade;
    };

    auto const single_builtin_loop = ({
        Optional<SingleBuiltinLoop> l = {};
        if (IsBuiltinLoop(mode.value.id)) {
            // If it's a single sample with a builtin loop, we can use that.
            if (auto i = layer.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
                if ((*i)->instrument.regions.size == 1) {
                    if (auto const loop = (*i)->instrument.regions[0].loop.builtin_loop) {
                        auto const num_frames = (*i)->audio_datas[0]->num_frames;
                        auto const checked_loop = CreateBoundsCheckedLoop(*loop, num_frames);
                        l = SingleBuiltinLoop {
                            .start = (f32)checked_loop.start / (f32)num_frames,
                            .end = (f32)checked_loop.end / (f32)num_frames,
                            .crossfade = (f32)checked_loop.crossfade / (f32)num_frames,
                        };
                    }
                }
            }
        }
        l;
    });

    auto const extra_grabbing_room_x = handle_width;
    auto const extra_grabbing_room_towards_centre = r.h / 3;
    auto const extra_grabbing_room_away_from_centre = r.h / 6;

    enum class HandleType { LoopStart, LoopEnd, Offset, Xfade };
    enum class HandleDirection { Left, Right };

    // Loop points and crossfade.
    Rect start_line;
    Rect start_handle;
    Rect end_line;
    Rect end_handle;
    Rect xfade_line;
    Rect xfade_handle;
    Rect loop_region_r;

    Rect const& left_line = reverse ? end_line : start_line;
    Rect const& right_line = reverse ? start_line : end_line;

    bool draw_xfade = false;
    bool draw_xfade_as_inactive = false;
    f32 loop_xfade_size {};

    auto const start_id = g.imgui.MakeId("loop start");
    auto const end_id = g.imgui.MakeId("loop end");
    auto const xfade_id = g.imgui.MakeId("loop xfade");
    auto const loop_region_id = g.imgui.MakeId("region");

    auto draw_handle = [&](Rect r, imgui::Id id, HandleType type, bool inactive) {
        auto back_col = LiveCol(!single_builtin_loop ? UiColMap::WaveformLoopHandle
                                                     : UiColMap::WaveformLoopHandleInactive);
        auto back_hover_col = LiveCol(UiColMap::WaveformLoopHandleHover);
        auto text_col = LiveCol(UiColMap::WaveformLoopHandleText);

        String text {};
        HandleDirection handle_direction {HandleDirection::Left};
        switch (type) {
            case HandleType::LoopEnd: {
                handle_direction = reverse ? HandleDirection::Left : HandleDirection::Right;
                text = reverse ? ICON_FA_ROTATE_RIGHT : ICON_FA_ROTATE_LEFT;
                break;
            }
            case HandleType::LoopStart: {
                text = reverse ? ICON_FA_ROTATE_LEFT : ICON_FA_ROTATE_RIGHT;
                handle_direction = reverse ? HandleDirection::Right : HandleDirection::Left;
                break;
            }
            case HandleType::Offset: {
                text = ICON_FA_CARET_RIGHT;
                handle_direction = HandleDirection::Left;
                back_col = LiveCol(UiColMap::WaveformOffsetHandle);
                back_hover_col = LiveCol(UiColMap::WaveformOffsetHandleHover);
                text_col = LiveCol(UiColMap::WaveformOffsetHandleText);
                break;
            }
            case HandleType::Xfade: {
                text = ICON_FA_FIRE;
                handle_direction = mode.value.mode == sample_lib::LoopMode::Standard
                                       ? (reverse ? HandleDirection::Left : HandleDirection::Right)
                                       : HandleDirection::Right;
                back_col = inactive ? LiveCol(UiColMap::WaveformXfadeHandleInactive)
                                    : LiveCol(UiColMap::WaveformXfadeHandle);
                back_hover_col = LiveCol(UiColMap::WaveformXfadeHandleHover);
                text_col = LiveCol(UiColMap::WaveformXfadeHandleText);
                break;
            }
        }

        g.imgui.draw_list
            ->AddRectFilled(r, g.imgui.IsHotOrActive(id, MouseButton::Left) ? back_hover_col : back_col, 6, ({
                                u4 rc = 0;
                                switch (handle_direction) {
                                    case HandleDirection::Left: rc = 0b1001; break;
                                    case HandleDirection::Right: rc = 0b0110; break;
                                }
                                rc;
                            }));
        g.fonts.Push(g.fonts.atlas[ToInt(FontType::Icons)]);
        DEFER { g.fonts.Pop(); };
        g.imgui.draw_list->AddTextInRect(r,
                                         text_col,
                                         text,
                                         {
                                             .justification = TextJustification::Centred,
                                             .overflow_type = TextOverflowType::AllowOverflow,
                                             .font_scaling = 0.5f,
                                         });
    };

    auto do_handle_slider = [&](imgui::Id id,
                                Span<ParamIndex const> params,
                                Optional<ParamIndex> tooltip_param,
                                Rect grabber_unregistered,
                                f32 value,
                                f32 default_val,
                                bool invert_slider,
                                FunctionRef<void(f32)> callback) {
        if (grabber_unregistered.w == 0) return;
        auto const grabber_r = g.imgui.RegisterAndConvertRect(grabber_unregistered);
        if (tooltip_param)
            AddParamContextMenuBehaviour(g,
                                         grabber_r,
                                         id,
                                         g.engine.processor.main_params.DescribedValue(*tooltip_param));

        bool const changed = g.imgui.SliderBehaviourRange({
            .rect_in_window_coords = grabber_r,
            .id = id,
            .min = invert_slider ? 1.0f : 0.0f,
            .max = invert_slider ? 0.0f : 1.0f,
            .value = value,
            .default_value = default_val,
            .cfg =
                {
                    .sensitivity = k_slider_sensitivity,
                    .slower_with_shift = true,
                    .default_on_modifer = true,
                },
        });

        if (g.imgui.ButtonBehaviour(grabber_r,
                                    id,
                                    {
                                        .mouse_button = MouseButton::Left,
                                        .event = MouseButtonEvent::DoubleClick,
                                    })) {
            g.param_text_editor_to_open = params[0];
        }

        if (g.imgui.IsHotOrActive(id, MouseButton::Left))
            GuiIo().out.wants.cursor_type = CursorType::HorizontalArrows;

        if (g.imgui.WasJustActivated(id, MouseButton::Left))
            for (auto p : params)
                ParameterJustStartedMoving(g.engine.processor, p);
        if (changed) callback(value);
        if (g.imgui.WasJustDeactivated(id, MouseButton::Left))
            for (auto p : params)
                ParameterJustStoppedMoving(g.engine.processor, p);

        if (tooltip_param) {
            auto param_obj = g.engine.processor.main_params.DescribedValue(*tooltip_param);
            ParameterValuePopup(g, param_obj, id, grabber_r);
            DoParameterTooltipIfNeeded(g, param_obj, id, grabber_r);
        }
    };

    if (mode.value.editable || single_builtin_loop) {
        auto const loop_start = !single_builtin_loop
                                    ? params.LinearValue(layer.index, LayerParamIndex::LoopStart)
                                    : single_builtin_loop->start;
        auto const loop_end = !single_builtin_loop
                                  ? Max(params.LinearValue(layer.index, LayerParamIndex::LoopEnd), loop_start)
                                  : single_builtin_loop->end;
        auto const raw_crossfade_size = !single_builtin_loop
                                            ? params.LinearValue(layer.index, LayerParamIndex::LoopCrossfade)
                                            : single_builtin_loop->crossfade;
        loop_xfade_size =
            ClampCrossfadeSize<f32>(raw_crossfade_size, loop_start, loop_end, 1.0f, *mode.value.mode) * r.w;
        auto loop_start_pos = loop_start * r.w;
        auto loop_end_pos = loop_end * r.w;
        auto loop_xfade_line_pos = loop_end_pos - loop_xfade_size;
        if (mode.value.mode == sample_lib::LoopMode::Standard) {
            if (reverse) {
                loop_start_pos = r.w - loop_start_pos;
                loop_end_pos = r.w - loop_end_pos;
                loop_xfade_line_pos = loop_end_pos + loop_xfade_size;
            }
        } else if (!reverse) {
            loop_xfade_line_pos = loop_end_pos + loop_xfade_size;
        } else {
            loop_start_pos = r.w - loop_start_pos;
            loop_end_pos = r.w - loop_end_pos;
            loop_xfade_line_pos = loop_start_pos + loop_xfade_size;
        }

        auto const xfade_active = loop_start != 0 && (loop_end - loop_start) != 0;
        draw_xfade_as_inactive = !xfade_active;

        auto const xfade_param_id =
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopCrossfade);
        auto const start_param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopStart);
        auto const end_param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopEnd);

        auto set_xfade_size_if_needed = [&]() {
            auto xfade = g.engine.processor.main_params.LinearValue(xfade_param_id);
            auto clamped_xfade =
                ClampCrossfadeSize(xfade,
                                   loop_start,
                                   Max(params.LinearValue(layer.index, LayerParamIndex::LoopEnd), loop_start),
                                   1.0f,
                                   *mode.value.mode);
            if (xfade > clamped_xfade) {
                SetParameterValue(g.engine.processor,
                                  xfade_param_id,
                                  clamped_xfade,
                                  {.host_should_not_record = true});
            }
        };

        // Start.
        {
            auto const param = g.engine.processor.main_params.DescribedValue(start_param_id);

            start_line = r.WithXW(r.x + loop_start_pos, 1);
            start_handle = {.xywh {start_line.Right() - handle_width, r.y, handle_width, handle_height}};
            if (reverse) start_handle.x += handle_width - start_line.w;

            auto grabber = start_handle;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            grabber.w += extra_grabbing_room_x;
            if (!reverse) grabber.x -= extra_grabbing_room_x;

            if (!single_builtin_loop)
                do_handle_slider(start_id,
                                 Array {start_param_id, xfade_param_id},
                                 start_param_id,
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 reverse,
                                 [&](f32 val) {
                                     val = Max(0.0f, Min(loop_end - k_epsilon, val));
                                     SetParameterValue(g.engine.processor, start_param_id, val, {});
                                     set_xfade_size_if_needed();
                                 });

            start_line = g.imgui.RegisterAndConvertRect(start_line);
            start_handle = g.imgui.RegisterAndConvertRect(start_handle);
        };

        // End.
        {
            auto const param = g.engine.processor.main_params.DescribedValue(end_param_id);

            end_line = r.WithXW(r.x + loop_end_pos, 1);
            end_handle = {.xywh {end_line.x, r.y, handle_width, handle_height}};
            if (reverse) end_handle.x -= handle_width - end_line.w;

            auto grabber = end_handle;
            grabber.w += extra_grabbing_room_x;
            grabber.y -= extra_grabbing_room_away_from_centre;
            grabber.h += extra_grabbing_room_away_from_centre + extra_grabbing_room_towards_centre;
            if (reverse) grabber.x -= extra_grabbing_room_x;

            if (!single_builtin_loop)
                do_handle_slider(end_id,
                                 Array {end_param_id, xfade_param_id},
                                 end_param_id,
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 reverse,
                                 [&](f32 value) {
                                     value = Min(1.0f, Max(loop_start + k_epsilon, value));
                                     SetParameterValue(g.engine.processor, end_param_id, value, {});
                                     set_xfade_size_if_needed();
                                 });

            end_line = g.imgui.RegisterAndConvertRect(end_line);
            end_handle = g.imgui.RegisterAndConvertRect(end_handle);
        };

        // Region.
        {
            loop_region_r = Rect::FromMinMax({r.x + Min(loop_start_pos, loop_end_pos), r.y},
                                             {r.x + Max(loop_start_pos, loop_end_pos), r.Bottom()});

            if (!single_builtin_loop && !(loop_start == 0 && loop_end == 1)) {
                do_handle_slider(loop_region_id,
                                 Array {start_param_id, end_param_id, xfade_param_id},
                                 {},
                                 loop_region_r,
                                 loop_start,
                                 loop_start,
                                 reverse,
                                 [&](f32 value) {
                                     f32 delta = value - loop_start;
                                     if (loop_end + delta > 1.0f) delta = 1.0f - loop_end;

                                     auto new_start = loop_start + delta;
                                     auto new_end = loop_end + delta;

                                     if (new_start != loop_start || new_end != loop_end) {
                                         SetParameterValue(g.engine.processor, start_param_id, new_start, {});
                                         SetParameterValue(g.engine.processor, end_param_id, new_end, {});
                                         set_xfade_size_if_needed();
                                     }
                                 });
            }
            loop_region_r = g.imgui.RegisterAndConvertRect(loop_region_r);
        }

        // Crossfade.
        {
            auto const& param = g.engine.processor.main_params.DescribedValue(xfade_param_id);

            xfade_line = r.WithXW(r.x + loop_xfade_line_pos, 1);
            xfade_handle = {.xywh {xfade_line.x, r.y + handle_height, handle_width, handle_height}};
            if (reverse && mode.value.mode == sample_lib::LoopMode::Standard)
                xfade_handle.x -= handle_width - xfade_line.w;

            auto grabber = xfade_handle;
            grabber.w += extra_grabbing_room_x;
            if (reverse && mode.value.mode == sample_lib::LoopMode::Standard)
                grabber.x -= extra_grabbing_room_x;

            if (xfade_active && !single_builtin_loop) {
                bool const invert = mode.value.mode == sample_lib::LoopMode::Standard ? !reverse : false;

                do_handle_slider(xfade_id,
                                 {&xfade_param_id, 1},
                                 xfade_param_id,
                                 grabber,
                                 param.LinearValue(),
                                 param.DefaultLinearValue(),
                                 invert,
                                 [&](f32 value) {
                                     value = ClampCrossfadeSize<f32>(value,
                                                                     loop_start - k_epsilon,
                                                                     loop_end + k_epsilon,
                                                                     1.0f,
                                                                     *mode.value.mode);
                                     SetParameterValue(g.engine.processor, xfade_param_id, value, {});
                                 });
            }

            xfade_line = g.imgui.RegisterAndConvertRect(xfade_line);
            xfade_handle = g.imgui.RegisterAndConvertRect(xfade_handle);
            draw_xfade = true;
        }
    }

    // Offset.
    Rect offs_handle;
    auto const offs_imgui_id = g.imgui.MakeId("offset");
    {
        auto const sample_offset = params.LinearValue(layer.index, LayerParamIndex::SampleOffset);
        auto const param_id = ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::SampleOffset);
        auto const& param = g.engine.processor.main_params.DescribedValue(param_id);

        auto sample_offset_r = r.WithW(r.w * sample_offset);
        offs_handle = {.xywh {sample_offset_r.Right() - handle_width,
                              r.Bottom() - handle_height,
                              handle_width,
                              handle_height}};
        auto grabber = offs_handle;
        grabber.y -= extra_grabbing_room_towards_centre;
        grabber.h += extra_grabbing_room_towards_centre + extra_grabbing_room_away_from_centre;
        grabber.w += extra_grabbing_room_x;
        grabber.x -= extra_grabbing_room_x;

        do_handle_slider(offs_imgui_id,
                         {&param_id, 1},
                         param_id,
                         grabber,
                         param.LinearValue(),
                         param.DefaultLinearValue(),
                         false,
                         [&](f32 value) { SetParameterValue(g.engine.processor, param_id, value, {}); });

        offs_handle = g.imgui.RegisterAndConvertRect(offs_handle);
        sample_offset_r = g.imgui.RegisterAndConvertRect(sample_offset_r);

        g.imgui.draw_list->AddRectFilled(sample_offset_r, LiveCol(UiColMap::WaveformSampleOffset));
        g.imgui.draw_list->AddRectFilled(f32x2 {sample_offset_r.Right() - 1, sample_offset_r.y},
                                         sample_offset_r.Max(),
                                         g.imgui.IsHotOrActive(offs_imgui_id, MouseButton::Left)
                                             ? LiveCol(UiColMap::WaveformOffsetHandleHover)
                                             : LiveCol(UiColMap::WaveformOffsetHandle));
    }

    // Drawing.
    if (mode.value.editable || single_builtin_loop) {
        auto other_xfade_line = start_line.WithPos(start_line.TopRight() +
                                                   f32x2 {reverse ? loop_xfade_size : -loop_xfade_size, 0});
        if (mode.value.mode == sample_lib::LoopMode::PingPong)
            other_xfade_line = left_line.WithPos(left_line.TopRight() - f32x2 {loop_xfade_size, 0});

        if (draw_xfade && loop_xfade_size > 0.01f) {
            if (mode.value.mode == sample_lib::LoopMode::Standard) {
                g.imgui.draw_list->AddLine(xfade_line.Min(),
                                           end_line.BottomLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
                g.imgui.draw_list->AddLine(other_xfade_line.BottomLeft(),
                                           start_line.TopLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
            } else {
                g.imgui.draw_list->AddLine(other_xfade_line.BottomLeft(),
                                           left_line.TopLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
                g.imgui.draw_list->AddLine(right_line.TopRight(),
                                           xfade_line.BottomLeft(),
                                           LiveCol(UiColMap::WaveformXFade));
            }
        }

        auto const region_active =
            g.imgui.IsHot(loop_region_id) || g.imgui.IsActive(loop_region_id, MouseButton::Left);
        if (!region_active && loop_xfade_size > 0.01f && draw_xfade) {
            if (mode.value.mode == sample_lib::LoopMode::Standard) {
                auto const points = Array {start_line.TopLeft(),
                                           xfade_line.TopLeft(),
                                           end_line.BottomRight(),
                                           start_line.BottomLeft()};
                g.imgui.draw_list->AddConvexPolyFilled(points,
                                                       LiveCol(UiColMap::WaveformRegionOverlay),
                                                       true);
            } else {
                auto const points = Array {other_xfade_line.BottomLeft(),
                                           left_line.TopLeft(),
                                           right_line.TopLeft(),
                                           xfade_line.BottomRight()};
                g.imgui.draw_list->AddConvexPolyFilled(points,
                                                       LiveCol(UiColMap::WaveformRegionOverlay),
                                                       true);
            }
        } else {
            g.imgui.draw_list->AddRectFilled(loop_region_r,
                                             region_active ? LiveCol(UiColMap::WaveformRegionOverlayHover)
                                                           : LiveCol(UiColMap::WaveformRegionOverlay));
        }

        struct LineAndId {
            Rect line;
            imgui::Id id;
        };
        for (auto const [line, id] : Array {
                 LineAndId {start_line, start_id},
                 LineAndId {end_line, end_id},
             }) {
            g.imgui.draw_list->AddRectFilled(
                line,
                LiveCol(g.imgui.IsHotOrActive(id, MouseButton::Left) ? UiColMap::WaveformLoopHandleHover
                        : !single_builtin_loop                       ? UiColMap::WaveformLoopHandle
                                                                     : UiColMap::WaveformLoopHandleInactive));
        }

        if (draw_xfade && loop_xfade_size > 0.01f) {
            g.imgui.draw_list->AddRectFilled(xfade_line,
                                             g.imgui.IsHotOrActive(xfade_id, MouseButton::Left)
                                                 ? LiveCol(UiColMap::WaveformXfadeHandleHover)
                                                 : LiveCol(UiColMap::WaveformXfadeHandle));
        }

        draw_handle(start_handle, start_id, HandleType::LoopStart, false);
        draw_handle(end_handle, end_id, HandleType::LoopEnd, false);
        if (draw_xfade) draw_handle(xfade_handle, xfade_id, HandleType::Xfade, draw_xfade_as_inactive);
    }
    draw_handle(offs_handle, offs_imgui_id, HandleType::Offset, false);

    // Text editor.
    if (g.param_text_editor_to_open) {
        auto const waveform_params = Array {
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopStart),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopEnd),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopCrossfade),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::SampleOffset),
        };
        auto const cut = r.w / 3;
        HandleShowingTextEditorForParams(g, r.CutLeft(cut).CutRight(cut), waveform_params);
    }
}

void DoWaveformElement(GuiState& g, LayerProcessor& layer, Rect viewport_r) {
    g.imgui.PushId(SourceLocationHash() + layer.index);
    DEFER { g.imgui.PopId(); };

    // Fix issue where texture subtly begins to tile when we don't want it
    viewport_r.xywh = Round(viewport_r.xywh);

    auto const window_r = ({
        auto r = g.imgui.RegisterAndConvertRect(viewport_r);
        r.xywh = Round(r.xywh); // As above, fix tiling issue.
        r;
    });

    g.imgui.draw_list->AddRectFilled(window_r,
                                     LiveCol(UiColMap::WaveformLoopBack),
                                     LiveSize(UiSizeId::CornerRounding));

    if (g.engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer.index].Load(
            LoadMemoryOrder::Relaxed) != -1) {
        g.imgui.draw_list->AddTextInRect(window_r,
                                         LiveCol(UiColMap::WaveformLoadingText),
                                         "Loading…",
                                         {
                                             .justification = TextJustification::Centred,
                                             .overflow_type = TextOverflowType::AllowOverflow,
                                             .font_scaling = 1,
                                         });
    } else {
        auto const& params = g.engine.processor.main_params;

        // Waveform image.
        if (layer.instrument_id.tag != InstrumentType::None) {
            auto const offset = layer.instrument_id.tag == InstrumentType::Sampler
                                    ? params.LinearValue(layer.index, LayerParamIndex::SampleOffset)
                                    : 0;
            auto const loop_start = params.LinearValue(layer.index, LayerParamIndex::LoopStart);
            auto const reverse = params.BoolValue(layer.index, LayerParamIndex::Reverse);
            auto const loop_end = Max(params.LinearValue(layer.index, LayerParamIndex::LoopEnd), loop_start);
            auto const loop_mode =
                params.IntValue<param_values::LoopMode>(layer.index, LayerParamIndex::LoopMode);
            bool const loop_points_editable =
                ActualLoopBehaviour(layer.instrument, loop_mode, layer.VolumeEnvelopeIsOn(params))
                    .value.editable;

            struct Range {
                f32x2 lo;
                f32x2 hi;
            };

            Range const whole_section_uv {
                .lo = {reverse ? 1.0f - offset : offset, 0},
                .hi = {reverse ? 0.0f : 1.0f, 1},
            };
            Range const offset_section_uv {
                .lo = {reverse ? 1.0f : 0.0f, 0},
                .hi = {reverse ? 1.0f - offset : offset, 1},
            };
            Range const loop_section_uv {
                .lo = {loop_start, 0},
                .hi = {loop_start + (loop_end - loop_start), 1},
            };

            if (auto const tex = GuiIo().in.renderer->GetTextureFromImage(
                    GetWaveformImage(g.waveform_images,
                                     layer.instrument,
                                     *GuiIo().in.renderer,
                                     g.shared_engine_systems.thread_pool,
                                     viewport_r.size))) {
                g.imgui.draw_list->AddImage(tex.Value(),
                                            window_r.Min() + f32x2 {offset * viewport_r.w, 0},
                                            window_r.Max(),
                                            whole_section_uv.lo,
                                            whole_section_uv.hi,
                                            (!loop_points_editable)
                                                ? LiveCol(UiColMap::WaveformLoopWaveformLoop)
                                                : LiveCol(UiColMap::WaveformLoopWaveform));

                if ((loop_end - loop_start) != 0 && loop_points_editable) {
                    g.imgui.draw_list->AddImage(
                        tex.Value(),
                        window_r.Min() +
                            f32x2 {viewport_r.w * (reverse ? (1.0f - loop_start) : loop_start), 0},
                        window_r.Max() - f32x2 {window_r.w * (reverse ? loop_end : (1.0f - loop_end)), 0},
                        loop_section_uv.lo,
                        loop_section_uv.hi,
                        LiveCol(UiColMap::WaveformLoopWaveformLoop));
                }

                if (offset != 0) {
                    g.imgui.draw_list->AddImage(tex.Value(),
                                                window_r.Min(),
                                                window_r.Max() - f32x2 {viewport_r.w * (1.0f - offset), 0},
                                                offset_section_uv.lo,
                                                offset_section_uv.hi,
                                                LiveCol(UiColMap::WaveformLoopWaveformOffset));
                }
            }
        }

        // Controls.
        DoWaveformControls(g, layer, viewport_r);

        // Voice cursors.
        if (g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)) {
            auto& voice_waveform_markers =
                g.engine.processor.voice_pool.voice_waveform_markers_for_gui.Consume().data;
            for (auto const voice_index : Range(k_num_voices)) {
                auto const marker = voice_waveform_markers[voice_index];
                if (!marker.intensity || marker.layer_index != layer.index) continue;

                f32 position = (f32)marker.position / (f32)UINT16_MAX;
                f32 const intensity = (f32)marker.intensity / (f32)UINT16_MAX;
                if (params.BoolValue(layer.index, LayerParamIndex::Reverse)) position = 1 - position;

                f32x2 cursor_pos {Round(viewport_r.x + (position * viewport_r.w)), viewport_r.y};
                cursor_pos = g.imgui.ViewportPosToWindowPos(cursor_pos);
                DrawVoiceMarkerLine(g.imgui,
                                    cursor_pos,
                                    viewport_r.h,
                                    g.imgui.ViewportPosToWindowPos(viewport_r.pos).x,
                                    {},
                                    intensity);
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
            }
        }
    }

    // Macro destination regions: stacked vertically in the top-right corner.
    {
        auto const cell_size = Min(window_r.w, window_r.h) / 3;
        auto const base_x = window_r.Right() - cell_size;

        auto const macro_params = Array {
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopStart),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopEnd),
            ParamIndexFromLayerParamIndex(layer.index, LayerParamIndex::LoopCrossfade),
        };
        for (auto const [i, param] : Enumerate(macro_params)) {
            auto const r = Rect {.xywh {base_x, window_r.y + (cell_size * (f32)i), cell_size, cell_size}};
            MacroAddDestinationRegion(g, r, param);
        }
    }
}
