// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_arp_step_sequencer.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/audio_utils.hpp"
#include "common_infrastructure/constants.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_live_edit.hpp"

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

    // Global "dim" for visuals. True when the arp is off so step controls read as inactive.
    auto const dim = [&](u32 col) -> u32 { return snapshot.on ? col : WithAlphaU8(col, 60); };

    // Background drawn on the parent viewport, so corner rounding covers the whole widget.
    imgui.draw_list->AddRectFilled(rect, LiveCol(UiColMap::EnvelopeBack), WwToPixels(k_corner_rounding));

    constexpr f32 k_gap = 2.0f;
    auto const k_default_visible_steps =
        snapshot.activation == ArpGuiSnapshot::Activation::ForcedBySlicing ? 16u : 12u;
    bool const needs_show_all = active_steps > k_default_visible_steps;
    if (!needs_show_all) show_all = false;

    auto const step_width =
        show_all ? (rect.w - (k_gap * ((f32)active_steps - 1))) / (f32)active_steps
                 : (rect.w - (k_gap * ((f32)k_default_visible_steps - 1))) / (f32)k_default_visible_steps;
    auto const step_stride = step_width + k_gap;

    // Open a horizontally scrolling viewport. Steps beyond those that fit in `rect.w` overflow and scroll.
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
        constexpr f32 k_row_gap = 1;
        u32 num_footer_rows = 1;
        if (edit.step_note) num_footer_rows++;
        if (edit.step_tie) num_footer_rows++;
        auto const footer_rows_height = row_height * (f32)num_footer_rows;
        auto const num_gaps = num_footer_rows + 1;
        auto const footer_height =
            footer_rows_height + knob_size + (label_pad * 2) + (k_row_gap * (f32)num_gaps);
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
    auto const bar_id = imgui.MakeId(SourceLocationHash());

    // Per-step popup ids for the right-click step context menu. XORed from bar_id + a fixed salt so
    // the right-click handler and the per-step render loop agree on the id without any PushId dance.
    auto const step_popup_id_for = [&](u32 step) -> imgui::Id {
        return bar_id ^ SourceLocationHash() ^ (imgui::Id)step;
    };

    // Right-click anywhere on the bar strip opens a per-step context menu.
    if (imgui.ButtonBehaviour(bar_rect,
                              bar_id,
                              {
                                  .mouse_button = MouseButton::Right,
                                  .event = MouseButtonEvent::Up,
                              })) {
        auto const& io = GuiIo().in;
        auto const clicked_step =
            (u32)Clamp((io.cursor_pos.x - bar_rect.x) / step_stride, 0.0f, (f32)active_steps - 1.0f);
        imgui.OpenPopupMenu(step_popup_id_for(clicked_step), bar_id);
    }

    // Velocity drag + tooltip. Always interactive — edit.step_velocity only controls visual dimming
    // via anything_editable, not interactivity.
    {
        // fired captures the mouse-down on the same frame; IsActive only goes true the frame after.
        auto const fired = imgui.ButtonBehaviour(bar_rect,
                                                 bar_id,
                                                 {
                                                     .mouse_button = MouseButton::Left,
                                                     .event = MouseButtonEvent::Down,
                                                 });

        if (fired) BeginUndoableStep(g.engine, "Arp step velocity"_s);
        if (imgui.WasJustDeactivated(bar_id, MouseButton::Left)) EndUndoableStep(g.engine);

        if (fired || imgui.IsActive(bar_id, MouseButton::Left)) {
            auto const& io = GuiIo().in;
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

            auto const curr_sf = pos_to_step(io.cursor_pos.x);

            if (fired) {
                set_vel_at((u32)curr_sf, y_to_vel(io.cursor_pos.y));
            } else {
                auto const prev_sf = pos_to_step(io.cursor_pos_prev.x);
                auto const lo_sf = Min(prev_sf, curr_sf);
                auto const hi_sf = Max(prev_sf, curr_sf);
                auto const lo_y = (prev_sf <= curr_sf) ? io.cursor_pos_prev.y : io.cursor_pos.y;
                auto const hi_y = (prev_sf <= curr_sf) ? io.cursor_pos.y : io.cursor_pos_prev.y;
                auto const lo = (u32)lo_sf;
                auto const hi = (u32)hi_sf;
                auto const span = hi_sf - lo_sf;

                for (u32 s = lo; s <= hi; ++s) {
                    auto const t = span > 0.0f ? Clamp(((f32)s - lo_sf) / span, 0.0f, 1.0f) : 1.0f;
                    set_vel_at(s, y_to_vel(lo_y + (t * (hi_y - lo_y))));
                }
            }
        }

        Tooltip(
            g,
            bar_id,
            bar_rect,
            "Step velocity. Click and drag to set. How velocity translates to volume is shaped by the curve on the CONFIG tab. Right-click for more options"_s,
            {});
    }

    // Right-click context menu for step draggers (note and gate). Must be called while the dragger's
    // PushId scope is active so MakeId(SourceLocationHash()) gives a unique popup id per step/control.
    auto const do_step_dragger_context_menu = [&](imgui::Id dragger_id,
                                                  Rect dragger_rect,
                                                  String display_text,
                                                  String reset_tooltip,
                                                  String reset_all_tooltip,
                                                  auto&& on_reset,
                                                  auto&& on_apply_to_all,
                                                  auto&& on_reset_all) {
        auto const popup_id = imgui.MakeId(SourceLocationHash());

        DoRightClickMenu(g,
                         {
                             .button_id = dragger_id,
                             .popup_id = popup_id,
                             .interaction_r = dragger_rect,
                             .do_menu_items =
                                 [&](Box root) {
                                     if (MenuItem(g.builder,
                                                  root,
                                                  {
                                                      .text = "Enter Value"_s,
                                                      .tooltip = "Open a text input to enter a value"_s,
                                                      .no_icon_gap = true,
                                                  })
                                             .button_fired)
                                         imgui.SetTextInputFocus(dragger_id, display_text, false);

                                     if (MenuItem(g.builder,
                                                  root,
                                                  {
                                                      .text = "Reset Value to Default"_s,
                                                      .tooltip = reset_tooltip,
                                                      .no_icon_gap = true,
                                                  })
                                             .button_fired)
                                         on_reset();

                                     MenuDivider(g.builder, root);

                                     if (MenuItem(g.builder,
                                                  root,
                                                  {
                                                      .text = "Apply to All Steps"_s,
                                                      .tooltip = "Set every other step to this same value"_s,
                                                      .no_icon_gap = true,
                                                  })
                                             .button_fired)
                                         on_apply_to_all();

                                     if (MenuItem(g.builder,
                                                  root,
                                                  {
                                                      .text = "Reset All Steps to Default"_s,
                                                      .tooltip = reset_all_tooltip,
                                                      .no_icon_gap = true,
                                                  })
                                             .button_fired)
                                         on_reset_all();
                                 },
                         });
    };

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

        constexpr f32 k_row_gap = 1;
        auto const footer_y_vp = bar_area_height + k_row_gap;
        f32 row_y_vp = footer_y_vp;

        // Row: Step number + on/off toggle (always shown, spans tie chain)
        if (!is_tied) {
            u32 num_tied_following = 0;
            for (u32 j = i + 1; j < active_steps; ++j) {
                if (!snapshot.StepAt(j).tie) break;
                ++num_tied_following;
            }
            auto const chain_width = step_width + ((f32)num_tied_following * step_stride);
            auto const btn_height = row_height + label_pad;

            auto const label_click_rect =
                imgui.RegisterAndConvertRect({.x = x_vp, .y = row_y_vp, .w = chain_width, .h = btn_height});
            auto const label_rect = imgui.ViewportRectToWindowRect({
                .x = x_vp,
                .y = row_y_vp,
                .w = chain_width,
                .h = btn_height,
            });

            bool label_hot = false;
            {
                imgui.PushId((u64)i);
                auto const toggle_id = imgui.MakeId(SourceLocationHash());
                if (imgui.ButtonBehaviour(label_click_rect, toggle_id, {})) {
                    ModifyStep(arp_state, i, [](ArpStep& s) { s.on = !s.on; });
                    RecordUndoableStep(g.engine, "Arp step on/off"_s);
                }
                label_hot = imgui.IsHot(toggle_id);
                Tooltip(
                    g,
                    toggle_id,
                    label_click_rect,
                    "Click to enable or disable this step. Disabled steps stay silent but keep their settings. Right-click for more options"_s,
                    {});

                auto const popup_id = imgui.MakeId(SourceLocationHash());
                DoRightClickMenu(
                    g,
                    {
                        .button_id = toggle_id,
                        .popup_id = popup_id,
                        .interaction_r = label_click_rect,
                        .do_menu_items =
                            [&](Box root) {
                                if (MenuItem(g.builder,
                                             root,
                                             {
                                                 .text = "Reset All Steps"_s,
                                                 .tooltip = "Enable every step"_s,
                                                 .no_icon_gap = true,
                                             })
                                        .button_fired) {
                                    for (u32 j = 0; j < active_steps; ++j)
                                        ModifyStep(arp_state, j, [](ArpStep& s) { s.on = true; });
                                    RecordUndoableStep(g.engine, "Reset all arp steps"_s);
                                }
                            },
                    });

                imgui.PopId();
            }

            if (label_hot) {
                draw_list.AddRectFilled(label_rect, WithAlphaU8(LiveCol(UiColMap::MidTextHot), 20));
                draw_list.AddNonAABox(label_rect.Min(),
                                      label_rect.Max(),
                                      WithAlphaU8(LiveCol(UiColMap::MidTextHot), 120),
                                      1);
            } else if (!step_off) {
                draw_list.AddRectFilled(label_rect, WithAlphaU8(LiveCol(UiColMap::CurveMapLine), 15));
                draw_list.AddNonAABox(label_rect.Min(),
                                      label_rect.Max(),
                                      WithAlphaU8(LiveCol(UiColMap::CurveMapLine), 80),
                                      1);
            } else {
                draw_list.AddRectFilled(label_rect, WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 10));
            }

            auto const text_rect = imgui.ViewportRectToWindowRect({
                .x = x_vp,
                .y = row_y_vp,
                .w = step_width,
                .h = btn_height,
            });
            auto const text_col = dim(label_hot  ? LiveCol(UiColMap::MidTextHot)
                                      : step_off ? WithAlphaU8(LiveCol(UiColMap::MidTextDimmed), 60)
                                                 : LiveCol(UiColMap::MidTextDimmed));
            draw_list.AddTextInRect(text_rect,
                                    text_col,
                                    fmt::Format(g.scratch_arena, "{}", i + 1),
                                    {
                                        .justification = TextJustification::Centred,
                                        .font_scaling = 0.85f,
                                    });
        }
        row_y_vp += row_height + k_row_gap;

        // Row: Note/interval display + drag to edit
        if (edit.step_note && !is_tied) {
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

            imgui.PushId((u64)(i + k_arp_max_steps));
            DEFER { imgui.PopId(); };

            auto const note_id = imgui.MakeId(SourceLocationHash());
            auto const is_fixed = arp_type == param_values::ArpMode::Fixed;

            String note_str;
            if (is_fixed)
                note_str = g.scratch_arena.Clone(NoteName(step.note));
            else if (step.interval == 0)
                note_str = "0"_s;
            else if (step.interval > 0)
                note_str = fmt::Format(g.scratch_arena, "+{}", step.interval);
            else
                note_str = fmt::Format(g.scratch_arena, "{}", step.interval);

            constexpr f32 k_px_per_increment = 10.0f;
            auto val = is_fixed ? (f32)step.note : (f32)step.interval;
            auto const dragger_result = imgui.DraggerBehaviour({
                .rect_in_window_coords = note_click_rect,
                .id = note_id,
                .text = note_str,
                .min = is_fixed ? 0.0f : -48.0f,
                .max = is_fixed ? 127.0f : 48.0f,
                .value = val,
                .default_value = is_fixed ? 60.0f : 0.0f,
                .text_input_button_cfg {
                    .mouse_button = MouseButton::Left,
                    .event = MouseButtonEvent::DoubleClick,
                },
                .text_input_cfg {
                    .chars_decimal = !is_fixed,
                    .chars_note_names = is_fixed,
                    .centre_align = true,
                    .escape_unfocuses = true,
                    .select_all_when_opening = true,
                },
                .slider_cfg {
                    .sensitivity = k_px_per_increment,
                    .slower_with_shift = true,
                    .default_on_modifer = true,
                },
            });

            do_step_dragger_context_menu(
                note_id,
                note_click_rect,
                note_str,
                is_fixed ? "Reset this note to C4 (60)"_s : "Reset this pitch offset to 0"_s,
                is_fixed ? "Reset every step's note to C4 (60)"_s : "Reset every step's pitch offset to 0"_s,
                [&]() {
                    if (is_fixed)
                        ModifyStep(arp_state, i, [](ArpStep& s) { s.note = 60; });
                    else
                        ModifyStep(arp_state, i, [](ArpStep& s) { s.interval = 0; });
                    RecordUndoableStep(g.engine, "Reset arp step note"_s);
                },
                [&]() {
                    auto const this_step = snapshot.StepAt(i);
                    for (u32 j = 0; j < active_steps; ++j) {
                        if (j == i) continue;
                        if (is_fixed)
                            ModifyStep(arp_state, j, [n = this_step.note](ArpStep& s) { s.note = n; });
                        else
                            ModifyStep(arp_state, j, [iv = this_step.interval](ArpStep& s) {
                                s.interval = iv;
                            });
                    }
                    RecordUndoableStep(g.engine, "Apply arp step note to all"_s);
                },
                [&]() {
                    for (u32 j = 0; j < active_steps; ++j)
                        if (is_fixed)
                            ModifyStep(arp_state, j, [](ArpStep& s) { s.note = 60; });
                        else
                            ModifyStep(arp_state, j, [](ArpStep& s) { s.interval = 0; });
                    RecordUndoableStep(g.engine, "Reset all arp step notes"_s);
                });

            if (imgui.WasJustActivated(note_id, MouseButton::Left))
                BeginUndoableStep(g.engine, "Arp step note"_s);
            if (imgui.WasJustDeactivated(note_id, MouseButton::Left)) EndUndoableStep(g.engine);

            if (dragger_result.value_changed) {
                if (is_fixed)
                    ModifyStep(arp_state, i, [val](ArpStep& s) {
                        s.note = (u7)Clamp((int)(val + 0.5f), 0, 127);
                    });
                else
                    ModifyStep(arp_state, i, [val](ArpStep& s) {
                        s.interval = (s8)Clamp((int)Round(val), -48, 48);
                    });
            }
            if (dragger_result.new_string_value) {
                if (is_fixed) {
                    if (auto const midi_note = MidiNoteFromName(*dragger_result.new_string_value))
                        ModifyStep(arp_state, i, [n = *midi_note](ArpStep& s) { s.note = n; });
                } else {
                    if (auto const o = ParseInt(*dragger_result.new_string_value, ParseIntBase::Decimal))
                        ModifyStep(arp_state,
                                   i,
                                   [v = (s8)Clamp((s64)o.Value(), (s64)-48, (s64)48)](ArpStep& s) {
                                       s.interval = v;
                                   });
                }
                RecordUndoableStep(g.engine, "Arp step note"_s);
            }

            auto note_hot = imgui.IsHot(note_id);

            Tooltip(
                g,
                note_id,
                note_click_rect,
                is_fixed
                    ? "Note played at this step. Drag to change, double-click to type a note name"_s
                    : "Pitch offset from the incoming note, in semitones. Drag to change, double-click to type a value"_s,
                {});

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

            if (dragger_result.text_input_result)
                DrawParameterTextInput(imgui, note_click_rect, *dragger_result.text_input_result);
        }
        if (edit.step_note) row_y_vp += row_height + k_row_gap;

        // Row: Tie toggle (hidden when show_tie_row is false, not on first step)
        if (edit.step_tie && i > 0) {
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
                if (imgui.ButtonBehaviour(tie_click_rect, tie_id, {})) {
                    ModifyStep(arp_state, i, [](ArpStep& s) { s.tie = !s.tie; });
                    RecordUndoableStep(g.engine, "Arp step tie"_s);
                }
                tie_hot = imgui.IsHot(tie_id);
                Tooltip(g,
                        tie_id,
                        tie_click_rect,
                        "Tie this step to the previous one so they play as a single, longer note"_s,
                        {});
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
        if (edit.step_tie) row_y_vp += row_height + k_row_gap;

        // Row: Gate knob (always shown, skipped for tied steps)
        if (!is_tied) {
            auto const knob_rect = imgui.RegisterAndConvertRect({
                .x = x_vp + ((step_width - knob_size) / 2.0f),
                .y = row_y_vp + label_pad,
                .w = knob_size,
                .h = knob_size,
            });

            bool knob_hot = false;
            Optional<imgui::TextInputResult> gate_text_input_result {};
            {
                imgui.PushId((u64)(i + (k_arp_max_steps * 3)));
                auto const knob_id = imgui.MakeId(SourceLocationHash());

                auto gate_pct = step.Gate01() * 100.0f;
                auto const gate_str = fmt::Format(g.scratch_arena, "{}%", (int)Round(gate_pct));

                auto const dragger_result = imgui.DraggerBehaviour({
                    .rect_in_window_coords = knob_rect,
                    .id = knob_id,
                    .text = gate_str,
                    .min = 5.0f,
                    .max = 100.0f,
                    .value = gate_pct,
                    .default_value = 100.0f,
                    .text_input_button_cfg {
                        .mouse_button = MouseButton::Left,
                        .event = MouseButtonEvent::DoubleClick,
                    },
                    .text_input_cfg {
                        .chars_decimal = true,
                        .centre_align = true,
                        .escape_unfocuses = true,
                        .select_all_when_opening = true,
                    },
                    .slider_cfg {
                        .sensitivity = 200.0f / 95.0f,
                        .slower_with_shift = true,
                        .default_on_modifer = true,
                    },
                });

                do_step_dragger_context_menu(
                    knob_id,
                    knob_rect,
                    gate_str,
                    "Reset gate to 100%"_s,
                    "Reset every step's gate to 100%"_s,
                    [&]() {
                        ModifyStep(arp_state, i, [](ArpStep& s) { s.gate = ArpStep::From01(1.0f); });
                        RecordUndoableStep(g.engine, "Reset arp step gate"_s);
                    },
                    [&]() {
                        auto const this_gate = snapshot.StepAt(i).gate;
                        for (u32 j = 0; j < active_steps; ++j) {
                            if (j == i) continue;
                            ModifyStep(arp_state, j, [g = this_gate](ArpStep& s) { s.gate = g; });
                        }
                        RecordUndoableStep(g.engine, "Apply arp step gate to all"_s);
                    },
                    [&]() {
                        for (u32 j = 0; j < active_steps; ++j)
                            ModifyStep(arp_state, j, [](ArpStep& s) { s.gate = ArpStep::From01(1.0f); });
                        RecordUndoableStep(g.engine, "Reset all arp step gates"_s);
                    });

                if (imgui.WasJustActivated(knob_id, MouseButton::Left))
                    BeginUndoableStep(g.engine, "Arp step gate"_s);
                if (imgui.WasJustDeactivated(knob_id, MouseButton::Left)) EndUndoableStep(g.engine);

                if (dragger_result.value_changed) {
                    auto const new_gate = Clamp(gate_pct / 100.0f, 0.05f, 1.0f);
                    ModifyStep(arp_state, i, [new_gate](ArpStep& s) { s.gate = ArpStep::From01(new_gate); });
                }
                if (dragger_result.new_string_value) {
                    if (auto const o = ParseInt(*dragger_result.new_string_value, ParseIntBase::Decimal)) {
                        auto const new_gate = Clamp((f32)o.Value() / 100.0f, 0.05f, 1.0f);
                        ModifyStep(arp_state, i, [new_gate](ArpStep& s) {
                            s.gate = ArpStep::From01(new_gate);
                        });
                    }
                    RecordUndoableStep(g.engine, "Arp step gate"_s);
                }
                gate_text_input_result = dragger_result.text_input_result;

                knob_hot = imgui.IsHotOrActive(knob_id, MouseButton::Left);
                Tooltip(
                    g,
                    knob_id,
                    knob_rect,
                    "Gate: note length as a percentage of the step. 100% is legato, lower values are staccato. Drag to change, double-click to type a value"_s,
                    {});
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

            if (gate_text_input_result) DrawParameterTextInput(imgui, knob_rect, *gate_text_input_result);
        }

        // Per-step right-click context menu (opened from the global bar right-click handler above).
        {
            auto const step_popup_id = step_popup_id_for(i);
            if (imgui.IsPopupMenuOpen(step_popup_id)) {
                auto const step_window_rect = imgui.ViewportRectToWindowRect({
                    .x = x_vp,
                    .y = 0,
                    .w = step_width,
                    .h = bar_area_height,
                });
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

                                MenuItem(g.builder,
                                         root,
                                         {
                                             .text = fmt::Format(g.scratch_arena, "Step {}", i + 1),
                                             .mode = MenuItemOptions::Mode::Disabled,
                                             .no_icon_gap = true,
                                         });

                                if (MenuItem(g.builder,
                                             root,
                                             {
                                                 .text = "Reset Step"_s,
                                                 .tooltip = "Reset all values of this step to defaults"_s,
                                                 .no_icon_gap = true,
                                             })
                                        .button_fired) {
                                    ModifyStep(arp_state, i, [](ArpStep& s) { s = {}; });
                                    RecordUndoableStep(g.engine, "Reset arp step"_s);
                                }

                                if (MenuItem(
                                        g.builder,
                                        root,
                                        {
                                            .text = "Apply Step to All Steps"_s,
                                            .tooltip =
                                                "Copy every value of this step to all other steps (except tie)"_s,
                                            .no_icon_gap = true,
                                        })
                                        .button_fired) {
                                    auto const this_step = snapshot.StepAt(i);
                                    for (u32 j = 0; j < active_steps; ++j) {
                                        if (j == i) continue;
                                        ModifyStep(arp_state, j, [this_step](ArpStep& s) {
                                            s.velocity = this_step.velocity;
                                            s.gate = this_step.gate;
                                            s.on = this_step.on;
                                            s.interval = this_step.interval;
                                            s.note = this_step.note;
                                        });
                                    }
                                    RecordUndoableStep(g.engine, "Apply arp step to all"_s);
                                }

                                if (MenuItem(g.builder,
                                             root,
                                             {
                                                 .text = "Reset All Steps"_s,
                                                 .tooltip = "Reset every step to its defaults"_s,
                                                 .no_icon_gap = true,
                                             })
                                        .button_fired) {
                                    for (u32 j = 0; j < active_steps; ++j)
                                        ModifyStep(arp_state, j, [](ArpStep& s) { s = {}; });
                                    RecordUndoableStep(g.engine, "Reset all arp steps"_s);
                                }
                            },
                        .bounds = step_window_rect,
                        .imgui_id = step_popup_id,
                        .viewport_config = k_default_popup_menu_viewport,
                    });
            }
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

        Tooltip(g,
                btn_id,
                btn_rect,
                show_all ? "Return to per-step editing"_s : "Show a compact overview of all steps"_s,
                {});

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
