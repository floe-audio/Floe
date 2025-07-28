// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_macros.hpp"

#include "gui.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui/gui_draw_knob.hpp"
#include "gui/gui_widget_helpers.hpp"
#include "gui_framework/gui_box_system.hpp"

static void DrawLinkLine(Gui* g, f32x2 p1, f32x2 p2) {
    auto const padding_radius_p1 = g->fonts[ToInt(FontType::Icons)]->font_size * 0.5f;
    auto const padding_radius_p2 = padding_radius_p1;

    // Move points inward by their padding radii
    auto const direction = p2 - p1;
    auto const length = Sqrt((direction.x * direction.x) + (direction.y * direction.y));
    auto const unit_direction = direction / length;
    p1 = p1 + unit_direction * padding_radius_p1;
    p2 = p2 - unit_direction * padding_radius_p2;

    g->imgui.overlay_graphics.AddLine(p1,
                                      p2,
                                      colours::ChangeAlpha(style::Col(style::Colour::Blue), 0.7f),
                                      Max(1.0f, g->imgui.VwToPixels(2)));
}

static void DrawPopupTextbox(Gui* g, String str, Rect r) {
    auto const font = g->box_system.imgui.graphics->context->CurrentFont();

    auto const size = draw::GetTextSize(font, str);
    auto const pad_x = LiveSize(g->box_system.imgui, UiSizeId::TooltipPadX);
    auto const pad_y = LiveSize(g->box_system.imgui, UiSizeId::TooltipPadY);

    r = r.Expanded(g->imgui.VwToPixels(4));

    Rect popup_r;
    popup_r.x = r.x + (r.w / 2) - (size.x / 2 + pad_x);
    popup_r.y = r.y + r.h;
    popup_r.w = size.x + pad_x * 2;
    popup_r.h = size.y + pad_y * 2;

    popup_r.pos =
        imgui::BestPopupPos(popup_r, r, g->box_system.imgui.frame_input.window_size.ToFloat2(), false);

    f32x2 text_start;
    text_start.x = popup_r.x + pad_x;
    text_start.y = popup_r.y + pad_y;

    draw::DropShadow(g->box_system.imgui, popup_r);
    g->box_system.imgui.overlay_graphics.AddRectFilled(
        popup_r.Min(),
        popup_r.Max(),
        LiveCol(g->box_system.imgui, UiColMap::TooltipBack),
        LiveSize(g->box_system.imgui, UiSizeId::CornerRounding));
    g->box_system.imgui.overlay_graphics.AddText(text_start,
                                                 LiveCol(g->box_system.imgui, UiColMap::TooltipText),
                                                 str);
}

void DoMacrosEditGui(Gui* g, Box const& parent) {
    auto& box_system = g->box_system;

    auto const initial_active_destination_knob = g->macros_gui_state.active_destination_knob;
    if (g->box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender)
        g->macros_gui_state.active_destination_knob = {};
    DEFER {
        if (g->box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender) {
            auto const& a = initial_active_destination_knob;
            auto const& b = g->macros_gui_state.active_destination_knob;
            if (a.HasValue() != b.HasValue() || (b.HasValue() && a->dest.param_index != b->dest.param_index))
                g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        }
    };

    auto const macro_box = DoBox(box_system,
                                 {
                                     .parent = parent,
                                     .round_background_corners = 0b1111,
                                     .layout {
                                         .size = layout::k_fill_parent,
                                         .margins = {.lrtb = 3},
                                         .contents_padding = {.lr = 5},
                                         .contents_gap = 6,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });
    for (auto const [macro_index, param_index] : Enumerate<u8>(k_macro_params)) {
        box_system.imgui.PushID(macro_index);
        DEFER { box_system.imgui.PopID(); };

        auto& dests = g->engine.processor.main_macro_destinations[macro_index];

        auto const container = DoBox(box_system,
                                     {
                                         .parent = macro_box,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 4,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });

        constexpr f32 k_text_input_x_padding = 4;

        auto const knobs_box = DoBox(box_system,
                                     {
                                         .parent = container,
                                         .layout {
                                             .size = layout::k_hug_contents,
                                             .contents_padding = {.l = k_text_input_x_padding},
                                             .contents_gap = 4,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                         },
                                     });
        auto const knob = DoParameterComponent(g,
                                               knobs_box,
                                               g->engine.processor.main_params.DescribedValue(param_index),
                                               {.label = false});

        constexpr f32 k_dest_knob_size = 25;
        constexpr f32 k_dest_knob_gap_x = 1;
        auto const dest_knob_size_px = box_system.imgui.VwToPixels(k_dest_knob_size);
        auto const dest_knob_gap_x_px = box_system.imgui.VwToPixels(k_dest_knob_gap_x);

        auto const destination_box =
            DoBox(box_system,
                  {
                      .parent = knobs_box,
                      .layout {
                          .size = {(k_dest_knob_size * k_max_macro_destinations) +
                                       (k_dest_knob_gap_x * (k_max_macro_destinations - 1)),
                                   k_dest_knob_size},
                      },
                  });

        Optional<u8> remove_destination_index {};
        DEFER {
            if (remove_destination_index) {
                RemoveMacroDestination(g->engine.processor,
                                       {
                                           .macro_index = macro_index,
                                           .destination_index = *remove_destination_index,
                                       });

                // Another annoying hack. When the we remove the value we are shifting the memory in the
                // contiguous array. The next time we run this code the IMGUI ID is still active, and because
                // the memory is the same for the next element it incorrectly thinks it's the same element
                // and is still active and needs its value updated; the knob value of the next knob is
                // changed by SliderBehaviour. We work-around this by clearing the active ID.
                box_system.imgui.SetActiveIDZero();
            }
        };

        if (auto const rel_r = BoxRect(box_system, destination_box)) {
            auto const r = box_system.imgui.GetRegisteredAndConvertedRect(*rel_r);
            box_system.imgui.RegisterRegionForMouseTracking(r, false);

            for (auto const dest_knob_index : Range((u8)dests.size)) {
                auto& dest = dests[dest_knob_index];

                auto const knob_r = Rect {
                    .x = r.x + (dest_knob_index * (dest_knob_size_px + dest_knob_gap_x_px)),
                    .y = r.y,
                    .w = dest_knob_size_px,
                    .h = dest_knob_size_px,
                };

                box_system.imgui.PushID(dest_knob_index);
                DEFER { box_system.imgui.PopID(); };
                auto const imgui_id = box_system.imgui.GetID("destination-knob"_s);

                auto norm_value = MapTo01(dest.value, -1, 1);
                if (box_system.imgui.SliderBehavior(knob_r,
                                                    imgui_id,
                                                    norm_value,
                                                    MapTo01(0, -1, 1),
                                                    {
                                                        .slower_with_shift = true,
                                                        .default_on_modifer = true,
                                                    })) {
                    dest.value = MapFrom01(norm_value, -1, 1);
                    MacroDestinationValueChanged(g->engine.processor,
                                                 {
                                                     .value = dest.value,
                                                     .macro_index = macro_index,
                                                     .destination_index = dest_knob_index,
                                                 });
                }

                auto const centre = knob_r.Centre();
                auto const radius = knob_r.w * 0.5f;

                auto const arc_thickness = 5;

                if (box_system.imgui.IsHotOrActive(imgui_id)) {
                    box_system.imgui.graphics->AddCircleFilled(centre,
                                                               radius - arc_thickness,
                                                               style::Col(style::Colour::Blue),
                                                               12);
                }

                if (box_system.imgui.WasJustMadeHot(imgui_id))
                    g->imgui.AddTimedWakeup(TimePoint::Now() + 0.5, "macros_destination_knob_hot");

                if (box_system.imgui.IsActive(imgui_id) ||
                    (box_system.imgui.IsHot(imgui_id) && box_system.imgui.SecondsSpentHot() > 0.5)) {
                    g->macros_gui_state.active_destination_knob = MacrosGuiState::DestinationKnob {
                        .dest = dest,
                        .r = knob_r,
                    };
                }

                DrawKnob(box_system.imgui,
                         imgui_id,
                         knob_r,
                         MapTo01(dest.value, -1, 1),
                         {
                             .highlight_col = style::Col(style::Colour::Blue),
                             .line_col = style::Col(style::Colour::Blue),
                             .bidirectional = true,
                         });

                if (box_system.imgui.IsHotOrActive(imgui_id)) {
                    dyn::Append(g->macros_gui_state.draw_overlays, [&dest, r = knob_r](Gui* g) {
                        auto const& descriptor = k_param_descriptors[ToInt(dest.param_index)];
                        auto const str = fmt::Format(g->box_system.arena,
                                                     "{}\n{}\n{.0}%",
                                                     descriptor.gui_label,
                                                     descriptor.ModuleString(" › "_s),
                                                     dest.ProjectedValue() * 100);
                        DrawPopupTextbox(g, str, r);
                    });
                }

                {
                    auto const remove_button_id = box_system.imgui.GetID("remove-destination-button"_s);

                    auto const remove_button_r = Rect {
                        .x = knob_r.x,
                        .y = knob_r.y + knob_r.h,
                        .w = dest_knob_size_px,
                        .h = dest_knob_size_px * 0.6f,
                    };

                    if (box_system.imgui.IsHot(imgui_id))
                        g->macros_gui_state.open_remove_destination_button_id = remove_button_id;

                    if (g->macros_gui_state.open_remove_destination_button_id == remove_button_id) {
                        auto const hovering_remove_button =
                            remove_button_r.Contains(box_system.imgui.frame_input.cursor_pos);

                        if (hovering_remove_button) {
                            // We are using overlay graphics; we need to make sure any item underneath this
                            // button is not turned hot.
                            box_system.imgui.active_item.id = imgui::k_imgui_misc_id;
                        } else {
                            g->macros_gui_state.open_remove_destination_button_id = 0;
                        }

                        box_system.imgui.RegisterRegionForMouseTracking(remove_button_r, false);
                        if (imgui::ClickCheck(
                                {
                                    .left_mouse = true,
                                    .triggers_on_mouse_up = true,
                                },
                                box_system.imgui.frame_input)) {
                            remove_destination_index = dest_knob_index;
                        }

                        dyn::Append(g->macros_gui_state.draw_overlays,
                                    [r = remove_button_r, hot = hovering_remove_button](Gui* g) {
                                        // Draw a dark circle with a circle-minus icon inside it.
                                        g->box_system.imgui.overlay_graphics.context->PushFont(
                                            g->fonts[ToInt(FontType::Icons)]);
                                        DEFER { g->box_system.imgui.overlay_graphics.context->PopFont(); };
                                        g->box_system.imgui.overlay_graphics.AddCircleFilled(
                                            r.Centre(),
                                            r.w * 0.5f,
                                            style::Col(style::Colour::DarkModeBackground0),
                                            12);
                                        g->box_system.imgui.overlay_graphics.AddTextJustified(
                                            r,
                                            ICON_FA_CIRCLE_MINUS,
                                            ({
                                                u32 c = style::Col(style::Colour::Red);
                                                if (hot) c = colours::ChangeBrightness(c, 1.3f);
                                                c;
                                            }),
                                            TextJustification::Centred,
                                            TextOverflowType::AllowOverflow,
                                            0.9f);
                                    });
                    }
                }
            }

            if (!knob.is_active && dests.size < k_max_macro_destinations) {
                auto const dest_knob_index = dests.size;

                auto const knob_r = Rect {
                    .x = r.x + (dest_knob_index * (dest_knob_size_px + dest_knob_gap_x_px)),
                    .y = r.y,
                    .w = dest_knob_size_px,
                    .h = dest_knob_size_px,
                };

                auto const imgui_id = box_system.imgui.GetID("add-destination-button"_s);

                if (box_system.imgui.ButtonBehavior(knob_r,
                                                    imgui_id,
                                                    {
                                                        .left_mouse = true,
                                                        .triggers_on_mouse_up = true,
                                                    })) {
                    auto& mode = g->macros_gui_state.macro_destination_select_mode;
                    if (!mode || *mode != macro_index)
                        mode = macro_index;
                    else
                        mode.Clear();
                }

                box_system.imgui.graphics->context->PushFont(g->fonts[ToInt(FontType::Icons)]);
                DEFER { box_system.imgui.graphics->context->PopFont(); };
                box_system.imgui.graphics->AddTextJustified(
                    knob_r,
                    ICON_FA_CIRCLE_PLUS,
                    ({
                        u32 c = style::Col(style::Colour::Blue);
                        if (g->macros_gui_state.macro_destination_select_mode) {
                            if (*g->macros_gui_state.macro_destination_select_mode == macro_index)
                                c = colours::ChangeBrightness(c, 1.3f);
                            else
                                c = colours::ChangeAlpha(c, 0.6f);
                        }
                        if (box_system.imgui.IsHotOrActive(imgui_id)) c = colours::ChangeBrightness(c, 1.3f);
                        c;
                    }),
                    TextJustification::Centred,
                    TextOverflowType::AllowOverflow,
                    0.9f);

                if (g->macros_gui_state.hot_destination_param &&
                    g->macros_gui_state.macro_destination_select_mode == macro_index) {
                    auto const& hot_param = *g->macros_gui_state.hot_destination_param;
                    dyn::Append(
                        g->macros_gui_state.draw_overlays,
                        [hot_param, p2 = knob_r.Centre(), macro_param = param_index](Gui* g) {
                            DrawLinkLine(g, hot_param.r.Centre(), p2);

                            auto const custom_macro_name =
                                g->engine.macro_names[*g->macros_gui_state.macro_destination_select_mode];

                            DynamicArray<char> text(g->scratch_arena);
                            fmt::Assign(text,
                                        "Connect {} to {}"_s,
                                        k_param_descriptors[ToInt(hot_param.param_index)].gui_label,
                                        custom_macro_name);
                            if (auto const default_macro_name =
                                    k_param_descriptors[ToInt(macro_param)].gui_label;
                                custom_macro_name != default_macro_name)
                                fmt::Append(text, " ({})", default_macro_name);

                            DoTooltipText(g, text, hot_param.r, true);
                        });
                }
            }
        }

        auto const label = DoBox(box_system,
                                 {
                                     .parent = container,
                                     .text = g->engine.macro_names[macro_index],
                                     .text_colours = Splat(style::Colour::DarkModeText),
                                     .text_overflow = TextOverflowType::ShowDotsOnRight,
                                     .background_fill_colours {
                                         .base = style::Colour::None,
                                         .hot = style::Colour::DarkModeBackground0,
                                         .active = style::Colour::DarkModeBackground0,
                                     },
                                     .border_colours {
                                         .base = style::Colour::None,
                                         .hot = style::Colour::DarkModeOverlay1,
                                         .active = style::Colour::DarkModeSubtext0,
                                     },
                                     .round_background_corners = 0b1111,
                                     .layout {
                                         .size = {100, style::k_font_body_size},
                                     },
                                     .behaviour = Behaviour::TextInput,
                                     .text_input_x_padding = k_text_input_x_padding,
                                 });
        DrawTextInput(box_system,
                      label,
                      {
                          .text_col = style::Colour::DarkModeText,
                          .cursor_col = style::Colour::DarkModeText,
                          .selection_col = style::Colour::Highlight,
                      });
        if (label.text_input_result &&
            (label.text_input_result->enter_pressed || label.text_input_result->buffer_changed)) {
            dyn::AssignFitInCapacity(g->engine.macro_names[macro_index], label.text_input_result->text);
        }
    }
}

void MacroAddDestinationRegion(Gui* g, Rect rel_r, ParamIndex param_index) {
    if (k_param_descriptors[ToInt(param_index)].module_parts[0] == ParameterModule::Macro) return;

    auto const active_dest_knob_linked =
        g->macros_gui_state.active_destination_knob &&
        g->macros_gui_state.active_destination_knob->dest.param_index == param_index;

    if (!g->macros_gui_state.macro_destination_select_mode) {
        if (active_dest_knob_linked) {
            auto const r = g->imgui.GetRegisteredAndConvertedRect(rel_r);
            dyn::Append(g->macros_gui_state.draw_overlays,
                        [p1 = r.Centre(), p2 = g->macros_gui_state.active_destination_knob->r.Centre()](
                            Gui* g) { DrawLinkLine(g, p1, p2); });

            g->imgui.ScrollWindowToShowRectangle(rel_r);
        }

        return;
    }

    auto const imgui_id = (imgui::Id)(SourceLocationHash() + g->imgui.GetID((usize)param_index));
    auto const r = g->imgui.GetRegisteredAndConvertedRect(rel_r);

    // Behaviour.
    {
        if (g->imgui.ButtonBehavior(r,
                                    imgui_id,
                                    {
                                        .left_mouse = true,
                                        .triggers_on_mouse_up = true,
                                    })) {
            AppendMacroDestination(g->engine.processor,
                                   {
                                       .param = param_index,
                                       .macro_index = *g->macros_gui_state.macro_destination_select_mode,
                                   });
            g->macros_gui_state.macro_destination_select_mode.Clear();
        }

        if (g->imgui.IsHot(imgui_id)) {
            g->macros_gui_state.hot_destination_param = MacrosGuiState::HotDestinationParam {
                .r = r,
                .param_index = param_index,
            };
        }
    }

    // Draw.
    {
        auto const clip_rect = g->imgui.graphics->clip_rect_stack.Back();
        g->imgui.overlay_graphics.PushClipRect(clip_rect.xy, clip_rect.zw);
        DEFER { g->imgui.overlay_graphics.PopClipRect(); };

        g->imgui.overlay_graphics.context->PushFont(g->fonts[ToInt(FontType::Icons)]);
        DEFER { g->imgui.overlay_graphics.context->PopFont(); };

        g->imgui.overlay_graphics.AddCircleFilled(r.Centre(),
                                                  g->imgui.overlay_graphics.context->CurrentFontSize() * 0.4f,
                                                  style::Col(style::Colour::DarkModeBackground0));

        g->imgui.overlay_graphics.AddTextJustified(
            r,
            ICON_FA_CIRCLE_PLUS,
            g->imgui.IsHotOrActive(imgui_id)
                ? colours::ChangeBrightness(style::Col(style::Colour::Blue), 1.3f)
                : style::Col(style::Colour::Blue),
            TextJustification::Centred,
            TextOverflowType::AllowOverflow,
            0.9f);
    }
}

void MacroGuiBeginFrame(Gui* g) {
    g->macros_gui_state.hot_destination_param.Clear();
    dyn::Clear(g->macros_gui_state.draw_overlays);
}

void MacroGuiEndFrame(Gui* g) {
    if (g->macros_gui_state.macro_destination_select_mode) {
        if (imgui::ClickCheck(
                {
                    .left_mouse = true,
                    .triggers_on_mouse_down = true,
                },
                g->imgui.frame_input) &&
            !g->imgui.AnItemIsHot()) {
            g->macros_gui_state.macro_destination_select_mode.Clear();
        }
    }

    for (auto const& draw_overlay : g->macros_gui_state.draw_overlays)
        draw_overlay(g);
}
