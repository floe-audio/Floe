// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_arp_step_sequencer.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/constants.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"

// ArpStep is a small atomic; user edits are Load/modify/Store so we don't lose neighbouring fields.
template <typename Mutate>
static void ModifyStep(ArpeggiatorState& s, u32 i, Mutate&& mutate) {
    auto step = s.steps[i].Load(LoadMemoryOrder::Relaxed);
    mutate(step);
    s.steps[i].Store(step, StoreMemoryOrder::Relaxed);
}

void DoArpStepSequencer(GuiState& g,
                        ArpeggiatorState& arp_state,
                        Rect rect,
                        ArpGuiSnapshot const& snapshot,
                        u32 playing_step,
                        bool& show_all) {
    auto& imgui = g.imgui;

    imgui.PushId((uintptr)&arp_state);
    DEFER { imgui.PopId(); };

    auto const& edit = snapshot.edit;
    auto const active_steps = snapshot.length;
    auto const arp_type = snapshot.type;
    bool const is_sliced = snapshot.activation == ArpGuiSnapshot::Activation::ForcedBySlicing;

    // Visibility rules: non-editable aspects are HIDDEN in slice mode (where they're irrelevant), but SHOWN
    // greyed when the arp is simply user-off (so the user can see what's there).
    bool const show_tie_row = edit.step_tie || !is_sliced;
    bool const show_note_row = edit.step_note || !is_sliced;

    // Global "dim" for visuals. True when no step-level editing is possible (user-off or slice-mode locked).
    bool const anything_editable = edit.step_velocity || edit.step_gate || edit.step_on;
    auto const dim = [&](u32 col) -> u32 { return anything_editable ? col : WithAlphaU8(col, 60); };

    // Background drawn on the parent viewport, so corner rounding covers the whole widget.
    imgui.draw_list->AddRectFilled(rect, LiveCol(UiColMap::EnvelopeBack), WwToPixels(k_corner_rounding));

    constexpr f32 k_gap = 2.0f;
    constexpr u32 k_default_visible_steps = 16;
    bool const needs_show_all = active_steps > k_default_visible_steps;
    if (!needs_show_all) show_all = false;

    auto const step_width =
        show_all ? (rect.w - (k_gap * ((f32)active_steps - 1))) / (f32)active_steps
                 : (rect.w - (k_gap * (k_default_visible_steps - 1))) / (f32)k_default_visible_steps;
    auto const step_stride = step_width + k_gap;

    // Open a horizontally scrolling viewport. Steps beyond the 16 that fit in `rect.w` overflow and scroll.
    imgui.BeginViewport(
        {
            .positioning = imgui::ViewportPositioning::WindowAbsolute,
            .draw_scrollbars = DrawMidPanelScrollbars,
            .scrollbar_width = WwToPixels(6.0f),
            .scrollbar_visibility = {show_all ? imgui::ViewportScrollbarVisibility::Never
                                              : imgui::ViewportScrollbarVisibility::Auto,
                                     imgui::ViewportScrollbarVisibility::Never},
        },
        rect,
        "arp-steps");

    auto& draw_list = *imgui.draw_list;

    auto const label_pad = WwToPixels(2.0f);
    auto const row_height = WwToPixels(10.0f) + (label_pad * 2);
    auto const knob_size = WwToPixels(12.0f);
    auto const content_h = imgui.CurrentVpHeight();

    f32 bar_area_height;
    if (show_all) {
        bar_area_height = content_h - row_height;
    } else {
        u32 num_footer_rows = 1;
        if (show_note_row) num_footer_rows++;
        if (show_tie_row) num_footer_rows++;
        auto const footer_rows_height = row_height * (f32)num_footer_rows;
        auto const footer_height = footer_rows_height + knob_size + (label_pad * 2);
        bar_area_height = content_h - footer_height;
    }

    auto const total_content_w = (step_stride * (f32)active_steps) - k_gap;

    // Auto-scroll to keep the currently playing/recording step in view. While recording, audio updates
    // current_step_for_gui together with its internal current_step (see ProcessLayerChanges).
    {
        auto const current_step = arp_state.recording.Load(LoadMemoryOrder::Relaxed)
                                      ? arp_state.current_step_for_gui.Load(LoadMemoryOrder::Relaxed)
                                      : playing_step;
        if (current_step < active_steps)
            imgui.ScrollViewportToShowRectangle(
                {.x = (f32)current_step * step_stride, .y = 0, .w = step_width, .h = content_h});
    }

    // Left-click drag sets velocity (walks back to root for tied steps). The registration here also
    // tells the viewport about the full content width so it can show a scrollbar when needed.
    auto const bar_rect =
        imgui.RegisterAndConvertRect(Rect {.x = 0, .y = 0, .w = total_content_w, .h = bar_area_height});
    if (edit.step_velocity) {
        auto const drag_id = imgui.MakeId(SourceLocationHash());
        imgui.ButtonBehaviour(bar_rect,
                              drag_id,
                              {
                                  .mouse_button = MouseButton::Left,
                                  .event = MouseButtonEvent::Down,
                              });

        static f32 prev_drag_x = 0;
        static f32 prev_drag_y = 0;
        static bool has_prev_drag = false;

        if (imgui.IsActive(drag_id, MouseButton::Left)) {
            auto const mouse_pos = GuiIo().in.cursor_pos;
            auto const pos_to_step = [&](f32 x) {
                return Clamp((x - bar_rect.x) / step_stride, 0.0f, (f32)active_steps - 1.0f);
            };
            auto const y_to_vel = [&](f32 y) {
                return Clamp(1.0f - ((y - bar_rect.y) / bar_area_height), 0.0f, 1.0f);
            };
            auto const set_vel_at = [&](u32 step_index, f32 vel) {
                while (step_index > 0 && snapshot.StepAt(step_index).tie)
                    --step_index;
                ModifyStep(arp_state, step_index, [vel](ArpStep& s) { s.velocity = ArpStep::From01(vel); });
            };

            if (!has_prev_drag) {
                set_vel_at((u32)pos_to_step(mouse_pos.x), y_to_vel(mouse_pos.y));
                prev_drag_x = mouse_pos.x;
                prev_drag_y = mouse_pos.y;
                has_prev_drag = true;
            } else {
                auto const prev_sf = pos_to_step(prev_drag_x);
                auto const curr_sf = pos_to_step(mouse_pos.x);
                auto const lo = (u32)Min(prev_sf, curr_sf);
                auto const hi = (u32)Max(prev_sf, curr_sf);

                for (u32 s = lo; s <= hi; ++s) {
                    f32 vel;
                    if (lo == hi) {
                        vel = y_to_vel(mouse_pos.y);
                    } else {
                        auto const t = (f32)(s - lo) / (f32)(hi - lo);
                        f32 lo_y;
                        f32 hi_y;
                        if (prev_sf <= curr_sf) {
                            lo_y = prev_drag_y;
                            hi_y = mouse_pos.y;
                        } else {
                            lo_y = mouse_pos.y;
                            hi_y = prev_drag_y;
                        }
                        vel = y_to_vel(lo_y + (t * (hi_y - lo_y)));
                    }
                    set_vel_at(s, vel);
                }

                prev_drag_x = mouse_pos.x;
                prev_drag_y = mouse_pos.y;
            }
        } else {
            has_prev_drag = false;
        }
    }

    u32 last_overview_label = 0;
    for (u32 i = 0; i < k_arp_max_steps; ++i) {
        if (i >= active_steps) continue;

        auto const x_vp = (f32)i * step_stride;
        auto const step = snapshot.StepAt(i);
        auto const is_recording = arp_state.recording.Load(LoadMemoryOrder::Relaxed);
        auto const playing = !is_recording && i == playing_step;
        auto const recording =
            is_recording && i == arp_state.current_step_for_gui.Load(LoadMemoryOrder::Relaxed);
        auto const step_off = !step.on;
        auto const is_tied = step.tie && i > 0;

        // At each tie-chain root, draw a single BG + bar spanning the whole chain (covering the
        // intermediate gaps so the chain reads as continuous). Then draw the per-step
        // playing/recording highlight on top so it stays visible regardless of the gate width.
        {
            if (!is_tied) {
                u32 num_tied_following = 0;
                for (u32 j = i + 1; j < active_steps; ++j) {
                    if (!snapshot.StepAt(j).tie) break;
                    ++num_tied_following;
                }
                auto const chain_width = step_width + ((f32)num_tied_following * step_stride);

                auto const bg_rect = imgui.ViewportRectToWindowRect({
                    .x = x_vp,
                    .y = 0,
                    .w = chain_width,
                    .h = bar_area_height,
                });
                auto const bg_col = dim(step_off ? WithAlphaU8(LiveCol(UiColMap::EnvelopeArea), 15)
                                                 : LiveCol(UiColMap::EnvelopeArea));
                draw_list.AddRectFilled(bg_rect, bg_col);

                auto const bar_height = step.Velocity01() * bar_area_height;
                auto const gated_bar_w = chain_width * step.Gate01();
                auto const bar_rect_draw = imgui.ViewportRectToWindowRect({
                    .x = x_vp,
                    .y = bar_area_height - bar_height,
                    .w = gated_bar_w,
                    .h = bar_height,
                });
                auto const col = dim(step_off ? WithAlphaU8(LiveCol(UiColMap::CurveMapLine), 30)
                                              : LiveCol(UiColMap::CurveMapLine));
                draw_list.AddRectFilled(bar_rect_draw, col);
            }

            if (playing || recording) {
                auto const hl_rect = imgui.ViewportRectToWindowRect({
                    .x = x_vp,
                    .y = 0,
                    .w = step_width,
                    .h = bar_area_height,
                });
                auto const hl_col = dim(recording ? WithAlphaU8(LiveCol(UiColMap::MidTextHot), 40)
                                                  : WithAlphaU8(LiveCol(UiColMap::CurveMapPointHover), 40));
                draw_list.AddRectFilled(hl_rect, hl_col);
            }
        }

        if (show_all) {
            // Overview mode: same step number row as edit mode but without interaction,
            // only at non-tied steps spaced at least 4 apart.
            if (!is_tied && (i == 0 || (i - last_overview_label) >= 4)) {
                last_overview_label = i;
                auto const label_rect = imgui.ViewportRectToWindowRect({
                    .x = x_vp,
                    .y = bar_area_height + label_pad,
                    .w = step_width,
                    .h = row_height - (label_pad * 2),
                });
                auto const text_col = dim(step_off ? WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 60)
                                                   : LiveCol(UiColMap::MidTextDimmed));
                draw_list.AddTextInRect(label_rect,
                                        text_col,
                                        fmt::Format(g.scratch_arena, "{}", i + 1),
                                        {
                                            .justification = TextJustification::Centred,
                                            .font_scaling = 0.85f,
                                        });
            }
            continue;
        }

        auto const footer_y_vp = bar_area_height;
        f32 row_y_vp = footer_y_vp;

        // Row: Step number + on/off toggle (always shown)
        if (!is_tied) {
            auto const label_click_rect =
                imgui.RegisterAndConvertRect({.x = x_vp, .y = row_y_vp, .w = step_width, .h = row_height});
            auto const label_rect = imgui.ViewportRectToWindowRect({
                .x = x_vp,
                .y = row_y_vp + label_pad,
                .w = step_width,
                .h = row_height - (label_pad * 2),
            });

            bool label_hot = false;
            if (edit.step_on) {
                imgui.PushId((u64)i);
                auto const toggle_id = imgui.MakeId(SourceLocationHash());
                if (imgui.ButtonBehaviour(label_click_rect, toggle_id, {}))
                    ModifyStep(arp_state, i, [](ArpStep& s) { s.on = !s.on; });
                label_hot = imgui.IsHot(toggle_id);
                imgui.PopId();
            }

            auto const text_col = dim(label_hot  ? LiveCol(UiColMap::MidTextHot)
                                      : step_off ? WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 60)
                                                 : LiveCol(UiColMap::MidTextDimmed));
            draw_list.AddTextInRect(label_rect,
                                    text_col,
                                    fmt::Format(g.scratch_arena, "{}", i + 1),
                                    {
                                        .justification = TextJustification::Centred,
                                        .font_scaling = 0.85f,
                                    });
        }
        row_y_vp += row_height;

        // Row: Note/interval display + drag to edit (hidden when show_note_row is false, skipped for tied
        // steps)
        if (show_note_row && !is_tied) {
            auto const note_click_rect = imgui.RegisterAndConvertRect({
                .x = x_vp,
                .y = row_y_vp,
                .w = step_width,
                .h = row_height,
            });
            auto const note_rect = imgui.ViewportRectToWindowRect({
                .x = x_vp,
                .y = row_y_vp + label_pad,
                .w = step_width,
                .h = row_height - (label_pad * 2),
            });

            bool note_hot = false;
            if (edit.step_note) {
                imgui.PushId((u64)(i + k_arp_max_steps));
                auto const note_id = imgui.MakeId(SourceLocationHash());

                f32 frac = arp_type == param_values::ArpMode::Fixed ? (f32)step.note / 127.0f
                                                                    : (f32)(step.interval + 48) / 96.0f;
                if (imgui.SliderBehaviourFraction({
                        .rect_in_window_coords = note_click_rect,
                        .id = note_id,
                        .fraction = frac,
                        .default_fraction = arp_type == param_values::ArpMode::Fixed ? 60.0f / 127.0f : 0.5f,
                        .cfg =
                            {
                                .sensitivity = 300,
                                .slower_with_shift = true,
                                .default_on_modifer = true,
                            },
                    })) {
                    if (arp_type == param_values::ArpMode::Fixed)
                        ModifyStep(arp_state, i, [frac](ArpStep& s) {
                            s.note = (u7)Clamp((int)((frac * 127.0f) + 0.5f), 0, 127);
                        });
                    else
                        ModifyStep(arp_state, i, [frac](ArpStep& s) {
                            s.interval = (s8)Clamp((int)((frac * 96.0f) - 48.0f + 0.5f), -48, 48);
                        });
                }

                note_hot = imgui.IsHot(note_id);
                imgui.PopId();
            }

            String note_str;
            if (arp_type == param_values::ArpMode::Fixed)
                note_str = g.scratch_arena.Clone(NoteName(step.note));
            else if (step.interval == 0)
                note_str = "0"_s;
            else if (step.interval > 0)
                note_str = fmt::Format(g.scratch_arena, "+{}", step.interval);
            else
                note_str = fmt::Format(g.scratch_arena, "{}", step.interval);

            auto const note_col = dim(note_hot   ? LiveCol(UiColMap::MidTextHot)
                                      : step_off ? WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 60)
                                                 : LiveCol(UiColMap::MidText));
            draw_list.AddTextInRect(note_rect,
                                    note_col,
                                    note_str,
                                    {
                                        .justification = TextJustification::Centred,
                                        .font_scaling = 0.85f,
                                    });
        }
        if (show_note_row) row_y_vp += row_height;

        // Row: Tie toggle (hidden when show_tie_row is false, not on first step)
        if (show_tie_row && i > 0) {
            auto const tie_click_rect = imgui.RegisterAndConvertRect({
                .x = x_vp,
                .y = row_y_vp,
                .w = step_width,
                .h = row_height,
            });
            auto const tie_rect = imgui.ViewportRectToWindowRect({
                .x = x_vp,
                .y = row_y_vp + label_pad,
                .w = step_width,
                .h = row_height - (label_pad * 2),
            });

            bool tie_hot = false;
            if (edit.step_tie) {
                imgui.PushId((u64)(i + (k_arp_max_steps * 2)));
                auto const tie_id = imgui.MakeId(SourceLocationHash());
                if (imgui.ButtonBehaviour(tie_click_rect, tie_id, {}))
                    ModifyStep(arp_state, i, [](ArpStep& s) { s.tie = !s.tie; });
                tie_hot = imgui.IsHot(tie_id);
                imgui.PopId();
            }

            auto const tie_col = dim(tie_hot    ? LiveCol(UiColMap::MidTextHot)
                                     : step.tie ? LiveCol(UiColMap::MidTextOn)
                                                : WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 60));
            g.fonts.Push(g.fonts.atlas[ToInt(FontType::Icons)]);
            draw_list.AddTextInRect(tie_rect,
                                    tie_col,
                                    ICON_FA_LINK,
                                    {
                                        .justification = TextJustification::Centred,
                                        .font_scaling = 0.75f,
                                    });
            g.fonts.Pop();
        }
        if (show_tie_row) row_y_vp += row_height;

        // Row: Gate knob (always shown, skipped for tied steps)
        if (!is_tied) {
            auto const knob_rect = imgui.RegisterAndConvertRect({
                .x = x_vp + ((step_width - knob_size) / 2.0f),
                .y = row_y_vp + label_pad,
                .w = knob_size,
                .h = knob_size,
            });

            bool knob_hot = false;
            if (edit.step_gate) {
                imgui.PushId((u64)(i + (k_arp_max_steps * 3)));
                auto const knob_id = imgui.MakeId(SourceLocationHash());

                f32 gate_frac = step.Gate01();
                if (imgui.SliderBehaviourFraction({
                        .rect_in_window_coords = knob_rect,
                        .id = knob_id,
                        .fraction = gate_frac,
                        .default_fraction = 1.0f,
                        .cfg =
                            {
                                .sensitivity = 200,
                                .slower_with_shift = true,
                                .default_on_modifer = true,
                            },
                    })) {
                    auto const new_gate = Clamp(gate_frac, 0.05f, 1.0f);
                    ModifyStep(arp_state, i, [new_gate](ArpStep& s) { s.gate = ArpStep::From01(new_gate); });
                }

                knob_hot = imgui.IsHotOrActive(knob_id, MouseButton::Left);
                imgui.PopId();
            }

            // Draw a tiny arc knob.
            auto const center = f32x2 {knob_rect.x + (knob_size / 2), knob_rect.y + (knob_size / 2)};
            auto const radius = (knob_size / 2) - 1;
            constexpr f32 k_start_angle = 2.356f; // ~135 degrees
            constexpr f32 k_end_angle = 7.069f; // ~405 degrees (135 + 270)
            auto const value_angle = k_start_angle + (step.Gate01() * (k_end_angle - k_start_angle));

            auto const track_col = dim(WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 40));
            auto const fill_col = dim(knob_hot   ? LiveCol(UiColMap::MidTextHot)
                                      : step_off ? WithAlphaU8(LiveCol(UiColMap::CurveMapLine), 60)
                                                 : LiveCol(UiColMap::CurveMapLine));

            draw_list.PathClear();
            draw_list.PathArcTo(center, radius, k_start_angle, k_end_angle, 12);
            draw_list.PathStroke(track_col, false, WwToPixels(1.5f));

            draw_list.PathClear();
            draw_list.PathArcTo(center, radius, k_start_angle, value_angle, 12);
            draw_list.PathStroke(fill_col, false, WwToPixels(1.5f));
        }
    }

    // Floating expand/compress toggle inside the viewport but positioned relative to the visible
    // bounds so it doesn't scroll. Uses is_non_viewport_content so SetHot checks hovered_viewport
    // (which includes scrollbar/padding area) rather than hovered_viewport_content.
    if (needs_show_all) {
        auto const btn_margin = WwToPixels(3.0f);
        auto const btn_h = WwToPixels(14.0f);
        auto const icon_size = WwToPixels(k_font_icons_size * 0.75f);
        auto const gap = WwToPixels(2.0f);

        g.fonts.Push(g.fonts.atlas[ToInt(FontType::Heading3)]);
        auto const text_w = g.fonts.CalcTextSize("OVERVIEW"_s, {}).x;
        g.fonts.Pop();
        auto const btn_pad_x = WwToPixels(4.0f);
        auto const btn_w = btn_pad_x + icon_size + gap + text_w + btn_pad_x;

        auto const visible = imgui.curr_viewport->visible_bounds;
        auto const btn_rect = Rect {
            .x = visible.Right() - btn_w - btn_margin,
            .y = visible.y + btn_margin,
            .w = btn_w,
            .h = btn_h,
        };

        auto const btn_id = imgui.MakeId(SourceLocationHash());
        if (imgui.ButtonBehaviour(btn_rect,
                                  btn_id,
                                  {
                                      .is_non_viewport_content = true,
                                  }))
            show_all = !show_all;

        auto const btn_hot = imgui.IsHot(btn_id);
        auto const btn_active = imgui.IsActive(btn_id, MouseButton::Left);

        // Subtle dark background so the button is visible over the step bars.
        draw_list.AddRectFilled(btn_rect, LiveCol(UiColMap::EnvelopeBack), WwToPixels(k_corner_rounding));

        // Toggle icon (same style as DoToggleIcon / DoButtonParameter).
        auto const icon_col = show_all     ? LiveCol(UiColMap::MidTextOn)
                              : btn_hot    ? LiveCol(UiColMap::MidTextHot)
                              : btn_active ? LiveCol(UiColMap::MidTextOn)
                                           : LiveCol(UiColMap::MidIcon);
        auto const icon_rect =
            Rect {.x = btn_rect.x + btn_pad_x, .y = btn_rect.y, .w = icon_size, .h = btn_h};
        g.fonts.Push(g.fonts.atlas[ToInt(FontType::Icons)]);
        draw_list.AddTextInRect(icon_rect,
                                icon_col,
                                show_all ? ICON_FA_TOGGLE_ON : ICON_FA_TOGGLE_OFF,
                                {
                                    .justification = TextJustification::Centred,
                                    .font_scaling = 0.75f,
                                });
        g.fonts.Pop();

        // Text label.
        auto const text_col = btn_hot      ? LiveCol(UiColMap::MidTextHot)
                              : btn_active ? LiveCol(UiColMap::MidTextHot)
                                           : LiveCol(UiColMap::MidText);
        auto const text_rect =
            Rect {.x = btn_rect.x + btn_pad_x + icon_size + gap, .y = btn_rect.y, .w = text_w, .h = btn_h};
        g.fonts.Push(g.fonts.atlas[ToInt(FontType::Heading3)]);
        draw_list.AddTextInRect(text_rect,
                                text_col,
                                "OVERVIEW"_s,
                                {
                                    .justification = TextJustification::CentredLeft,
                                });
        g.fonts.Pop();
    }

    imgui.EndViewport();
}
