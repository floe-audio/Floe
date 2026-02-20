// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_macros.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui_framework/gui_builder.hpp"

static void DrawLinkLine(GuiState& g, f32x2 p1, f32x2 p2) {
    auto const padding_radius_p1 = g.fonts.atlas[ToInt(FontType::Icons)]->font_size * 0.5f;
    auto const padding_radius_p2 = padding_radius_p1;

    // Move points inward by their padding radii
    auto const direction = p2 - p1;
    auto const length = Sqrt((direction.x * direction.x) + (direction.y * direction.y));
    auto const unit_direction = direction / length;
    p1 = p1 + unit_direction * padding_radius_p1;
    p2 = p2 - unit_direction * padding_radius_p2;

    g.imgui.overlay_draw_list->AddLine(p1,
                                       p2,
                                       ChangeAlpha(ToU32({.c = Col::Blue}), 0.7f),
                                       Max(1.0f, GuiIo().WwToPixels(2.0f)));
}

static void DrawPopupTextbox(GuiState& g, String str, Rect r) {
    auto const size = g.fonts.CalcTextSize(str, {});
    auto const pad_x = LivePx(UiSizeId::TooltipPadX);
    auto const pad_y = LivePx(UiSizeId::TooltipPadY);

    r = r.Expanded(GuiIo().WwToPixels(4.0f));

    Rect popup_r;
    popup_r.x = r.x + (r.w / 2) - (size.x / 2 + pad_x);
    popup_r.y = r.y + r.h;
    popup_r.w = size.x + pad_x * 2;
    popup_r.h = size.y + pad_y * 2;

    popup_r.pos = imgui::BestPopupPos(popup_r,
                                      r,
                                      GuiIo().in.window_size.ToFloat2(),
                                      imgui::PopupJustification::AboveOrBelow);

    f32x2 text_start;
    text_start.x = popup_r.x + pad_x;
    text_start.y = popup_r.y + pad_y;

    DrawDropShadow(g.builder.imgui, popup_r);
    g.builder.imgui.overlay_draw_list->AddRectFilled(popup_r,
                                                     ToU32({.c = Col::Background0}),
                                                     LivePx(UiSizeId::CornerRounding));
    g.builder.imgui.overlay_draw_list->AddText(text_start, ToU32({.c = Col::Text}), str);
}

void DoMacrosEditGui(GuiState& g, Box const& parent) {
    auto& builder = g.builder;

    auto const initial_active_destination_knob = g.macros_gui_state.active_destination_knob;
    if (g.builder.IsInputAndRenderPass()) g.macros_gui_state.active_destination_knob = {};
    DEFER {
        if (g.builder.IsInputAndRenderPass()) {
            auto const& a = initial_active_destination_knob;
            auto const& b = g.macros_gui_state.active_destination_knob;
            if (a.HasValue() != b.HasValue() || (b.HasValue() && a->dest.param_index != b->dest.param_index))
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }
    };

    auto const macro_box = DoBox(builder,
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
        builder.imgui.PushId(macro_index);
        DEFER { builder.imgui.PopId(); };

        auto& dests = g.engine.processor.main_macro_destinations[macro_index];

        auto const container = DoBox(builder,
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

        auto const knobs_box = DoBox(builder,
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
        auto const knob = DoKnobParameter(g,
                                          knobs_box,
                                          g.engine.processor.main_params.DescribedValue(param_index),
                                          {
                                              .width = LiveWw(UiSizeId::ParamComponentExtraSmallWidth),
                                              .knob_height_fraction = LiveRaw(UiSizeId::ParamComponentKnobHeightPercent) / 100.0f,
                                              .label = false,
                                          });

        constexpr f32 k_dest_knob_size = 25;
        constexpr f32 k_dest_knob_gap_x = 1;
        auto const dest_knob_size_px = GuiIo().WwToPixels(k_dest_knob_size);
        auto const dest_knob_gap_x_px = GuiIo().WwToPixels(k_dest_knob_gap_x);

        auto const destination_box =
            DoBox(builder,
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
                RemoveMacroDestination(g.engine.processor,
                                       {
                                           .macro_index = macro_index,
                                           .destination_index = *remove_destination_index,
                                       });

                // Another annoying hack. When the we remove the value we are shifting the memory in the
                // contiguous array. The next time we run this code the IMGUI ID is still active, and because
                // the memory is the same for the next element it incorrectly thinks it's the same element
                // and is still active and needs its value updated; the knob value of the next knob is
                // changed by SliderBehaviour. We work-around this by clearing the active ID.
                builder.imgui.ClearActive();
            }
        };

        struct RemoveButton {
            Rect r;
            imgui::Id id;
            u8 dest_index;
        };
        Optional<RemoveButton> remove_button {};

        if (auto const rel_r = BoxRect(builder, destination_box)) {
            auto const r = builder.imgui.RegisterAndConvertRect(*rel_r);
            builder.imgui.RegisterRectForMouseTracking(r, false);

            for (auto const dest_knob_index : Range(CheckedCast<u8>(dests.Size()))) {
                auto& dest = dests.items[dest_knob_index];

                auto const knob_r = Rect {
                    .x = r.x + (dest_knob_index * (dest_knob_size_px + dest_knob_gap_x_px)),
                    .y = r.y,
                    .w = dest_knob_size_px,
                    .h = dest_knob_size_px,
                };

                builder.imgui.PushId(dest_knob_index);
                DEFER { builder.imgui.PopId(); };
                auto const imgui_id = builder.imgui.MakeId("destination-knob"_s);

                auto norm_value = MapTo01(dest.value, -1, 1);
                if (builder.imgui.SliderBehaviourFraction({
                        .rect_in_window_coords = knob_r,
                        .id = imgui_id,
                        .fraction = norm_value,
                        .default_fraction = MapTo01(0, -1, 1),
                        .cfg =
                            {
                                .sensitivity = 400,
                                .slower_with_shift = true,
                                .default_on_modifer = true,
                            },
                    })) {
                    dest.value = MapFrom01(norm_value, -1, 1);
                    MacroDestinationValueChanged(g.engine.processor,
                                                 {
                                                     .value = dest.value,
                                                     .macro_index = macro_index,
                                                     .destination_index = dest_knob_index,
                                                 });
                }

                auto const centre = knob_r.Centre();
                auto const radius = knob_r.w * 0.5f;

                auto const arc_thickness = 5;

                if (builder.imgui.IsHotOrActive(imgui_id, MouseButton::Left)) {
                    builder.imgui.draw_list->AddCircleFilled(centre,
                                                             radius - arc_thickness,
                                                             ToU32({.c = Col::Blue}),
                                                             12);
                }

                if (builder.imgui.WasJustMadeHot(imgui_id))
                    GuiIo().out.AddTimedWakeup(TimePoint::Now() + 0.5, "macros_destination_knob_hot");

                if (builder.imgui.IsActive(imgui_id, MouseButton::Left) ||
                    (builder.imgui.IsHot(imgui_id) && builder.imgui.SecondsSpentHot() > 0.5)) {
                    g.macros_gui_state.active_destination_knob = MacrosGuiState::DestinationKnob {
                        .dest = dest,
                        .r = knob_r,
                    };
                }

                DrawKnob(builder.imgui,
                         imgui_id,
                         knob_r,
                         MapTo01(dest.value, -1, 1),
                         {
                             .highlight_col = ToU32({.c = Col::Blue}),
                             .line_col = ToU32({.c = Col::Blue}),
                             .bidirectional = true,
                         });

                if (builder.imgui.IsHotOrActive(imgui_id, MouseButton::Left)) {
                    dyn::Append(g.macros_gui_state.draw_overlays, [&dest, r = knob_r](GuiState& g) {
                        auto const& descriptor = k_param_descriptors[ToInt(*dest.param_index)];
                        auto const str = fmt::Format(g.builder.arena,
                                                     "{}\n{}\n{.0}%",
                                                     descriptor.gui_label,
                                                     descriptor.ModuleString(" › "_s),
                                                     dest.ProjectedValue() * 100);
                        DrawPopupTextbox(g, str, r);
                    });
                }

                {
                    auto const remove_button_id = builder.imgui.MakeId("remove-destination-button"_s);
                    auto const remove_r = Rect {
                        .x = knob_r.x,
                        .y = knob_r.y + knob_r.h,
                        .w = dest_knob_size_px,
                        .h = dest_knob_size_px * 0.6f,
                    };

                    if (builder.imgui.IsHot(imgui_id) ||
                        (builder.imgui.WasJustMadeUnhot(imgui_id) &&
                         remove_r.Contains(GuiIo().in.cursor_pos)) ||
                        builder.imgui.IsHotOrActive(remove_button_id, MouseButton::Left) ||
                        builder.imgui.WasJustDeactivated(remove_button_id, MouseButton::Left)) {
                        remove_button = RemoveButton {
                            .r = remove_r,
                            .id = remove_button_id,
                            .dest_index = dest_knob_index,
                        };
                    }
                }
            }

            if (!knob.is_active && dests.Size() < k_max_macro_destinations) {
                auto const visual_position = dests.Size();

                auto const knob_r = Rect {
                    .x = r.x + (visual_position * (dest_knob_size_px + dest_knob_gap_x_px)),
                    .y = r.y,
                    .w = dest_knob_size_px,
                    .h = dest_knob_size_px,
                };

                auto const imgui_id = builder.imgui.MakeId("add-destination-button"_s);

                if (builder.imgui.ButtonBehaviour(knob_r,
                                                  imgui_id,
                                                  {
                                                      .mouse_button = MouseButton::Left,
                                                      .event = MouseButtonEvent::Up,
                                                  })) {
                    auto& mode = g.macros_gui_state.macro_destination_select_mode;
                    if (!mode || *mode != macro_index)
                        mode = macro_index;
                    else
                        mode.Clear();
                }

                builder.fonts.Push(ToInt(FontType::Icons));
                DEFER { builder.fonts.Pop(); };

                builder.imgui.draw_list->AddTextInRect(
                    knob_r,
                    ({
                        u32 c = ToU32({.c = Col::Blue});
                        if (g.macros_gui_state.macro_destination_select_mode) {
                            if (*g.macros_gui_state.macro_destination_select_mode == macro_index)
                                c = ChangeBrightness(c, 1.3f);
                            else
                                c = ChangeAlpha(c, 0.6f);
                        }
                        if (builder.imgui.IsHotOrActive(imgui_id, MouseButton::Left))
                            c = ChangeBrightness(c, 1.3f);
                        c;
                    }),
                    ICON_FA_CIRCLE_PLUS,
                    {
                        .justification = TextJustification::Centred,
                        .overflow_type = TextOverflowType::AllowOverflow,
                        .font_scaling = 0.9f,
                    });

                if (g.macros_gui_state.hot_destination_param &&
                    g.macros_gui_state.macro_destination_select_mode == macro_index) {
                    auto const& hot_param = *g.macros_gui_state.hot_destination_param;
                    dyn::Append(
                        g.macros_gui_state.draw_overlays,
                        [hot_param, p2 = knob_r.Centre(), macro_param = param_index](GuiState& g) {
                            DrawLinkLine(g, hot_param.r.Centre(), p2);

                            auto const custom_macro_name =
                                g.engine.macro_names[*g.macros_gui_state.macro_destination_select_mode];

                            DynamicArray<char> text(g.scratch_arena);
                            fmt::Assign(text,
                                        "Connect {} to {}"_s,
                                        k_param_descriptors[ToInt(hot_param.param_index)].gui_label,
                                        custom_macro_name);
                            if (auto const default_macro_name =
                                    k_param_descriptors[ToInt(macro_param)].gui_label;
                                custom_macro_name != default_macro_name)
                                fmt::Append(text, " ({})", default_macro_name);

                            auto const window_r = g.imgui.ViewportRectToWindowRect(hot_param.r);

                            DrawOverlayTooltipForRect(g.imgui,
                                                      g.fonts,
                                                      text,
                                                      {
                                                          .r = window_r,
                                                          .avoid_r = window_r,
                                                          .justification = TooltipJustification::AboveOrBelow,
                                                      });
                        });
                }
            }
        }

        auto const name_input = DoBox(builder,
                                      {
                                          .parent = container,
                                          .background_fill_colours =
                                              ColSet {
                                                  .base = {},
                                                  .hot = {.c = Col::Background0, .dark_mode = true},
                                                  .active = {.c = Col::Background0, .dark_mode = true},
                                              },
                                          .border_colours =
                                              ColSet {
                                                  .base = {},
                                                  .hot = {.c = Col::Overlay1, .dark_mode = true},
                                                  .active = {.c = Col::Subtext0, .dark_mode = true},
                                              },
                                          .round_background_corners = 0b1111,
                                          .layout {
                                              .size = {100, k_font_body_size},
                                          },
                                      });

        if (auto const r = BoxRect(builder, name_input)) {
            auto const window_r = builder.imgui.RegisterAndConvertRect(*r);
            auto const result = builder.imgui.TextInputBehaviour({
                .rect_in_window_coords = window_r,
                .id = name_input.imgui_id,
                .text = g.engine.macro_names[macro_index],
                .input_cfg =
                    {
                        .x_padding = GuiIo().WwToPixels(k_text_input_x_padding),
                        .centre_align = false,
                        .escape_unfocuses = true,
                        .select_all_when_opening = true,
                        .multiline = false,
                        .multiline_wordwrap_hack = false,
                    },
                .button_cfg =
                    {
                        .mouse_button = MouseButton::Left,
                        .event = MouseButtonEvent::Up,
                    },
            });

            DrawTextInput(builder.imgui,
                          result,
                          {
                              .text_col = {.c = Col::Text, .dark_mode = true},
                              .cursor_col = {.c = Col::Text, .dark_mode = true},
                              .selection_col = {.c = Col::Highlight, .alpha = 128},
                          });

            if (result.enter_pressed || result.buffer_changed)
                dyn::AssignFitInCapacity(g.engine.macro_names[macro_index], result.text);
        }

        if (remove_button) {
            auto const r = remove_button->r;

            if (builder.imgui.ButtonBehaviour(remove_button->r,
                                              remove_button->id,
                                              {
                                                  .mouse_button = MouseButton::Left,
                                                  .event = MouseButtonEvent::Up,
                                              })) {
                remove_destination_index = remove_button->dest_index;
            }

            // Draw a dark circle with a circle-minus icon inside it.
            g.fonts.Push(ToInt(FontType::Icons));
            DEFER { g.fonts.Pop(); };
            g.builder.imgui.overlay_draw_list->AddCircleFilled(
                r.Centre(),
                r.w * 0.5f,
                ToU32(Col {.c = Col::Background0, .dark_mode = true}),
                12);
            g.builder.imgui.overlay_draw_list->AddTextInRect(
                r,
                ({
                    u32 c = ToU32({.c = Col::Red});
                    if (builder.imgui.IsHot(remove_button->id)) c = ChangeBrightness(c, 1.3f);
                    c;
                }),
                ICON_FA_CIRCLE_MINUS,
                {
                    .justification = TextJustification::Centred,
                    .overflow_type = TextOverflowType::AllowOverflow,
                    .font_scaling = 0.9f,
                });
        }
    }
}

void MacroAddDestinationRegion(GuiState& g, Rect window_r, ParamIndex param_index) {
    if (k_param_descriptors[ToInt(param_index)].module_parts[0] == ParameterModule::Macro) return;

    auto const active_dest_knob_linked =
        g.macros_gui_state.active_destination_knob &&
        g.macros_gui_state.active_destination_knob->dest.param_index == param_index;

    if (!g.macros_gui_state.macro_destination_select_mode) {
        if (active_dest_knob_linked) {
            dyn::Append(g.macros_gui_state.draw_overlays,
                        [p1 = window_r.Centre(), p2 = g.macros_gui_state.active_destination_knob->r.Centre()](
                            GuiState& g) { DrawLinkLine(g, p1, p2); });

            g.imgui.ScrollViewportToShowRectangle(g.imgui.WindowRectToViewportRect(window_r));
        }

        return;
    }

    auto const imgui_id = (imgui::Id)(SourceLocationHash() + g.imgui.MakeId((usize)param_index));

    // Behaviour.
    {
        if (g.imgui.ButtonBehaviour(window_r,
                                    imgui_id,
                                    {
                                        .mouse_button = MouseButton::Left,
                                        .event = MouseButtonEvent::Up,
                                    })) {
            AppendMacroDestination(g.engine.processor,
                                   {
                                       .param = param_index,
                                       .macro_index = *g.macros_gui_state.macro_destination_select_mode,
                                   });
            g.macros_gui_state.macro_destination_select_mode.Clear();
        }

        if (g.imgui.IsHot(imgui_id)) {
            g.macros_gui_state.hot_destination_param = MacrosGuiState::HotDestinationParam {
                .r = window_r,
                .param_index = param_index,
            };
        }
    }

    // Draw.
    {
        auto const clip_rect = g.imgui.draw_list->clip_rect_stack.Back();
        g.imgui.overlay_draw_list->PushClipRect(clip_rect.xy, clip_rect.zw);
        DEFER { g.imgui.overlay_draw_list->PopClipRect(); };

        g.fonts.Push(ToInt(FontType::Icons));
        DEFER { g.fonts.Pop(); };

        g.imgui.overlay_draw_list->AddCircleFilled(window_r.Centre(),
                                                   g.fonts.Current()->font_size * 0.4f,
                                                   ToU32(Col {.c = Col::Background0, .dark_mode = true}));

        g.imgui.overlay_draw_list->AddTextInRect(window_r,
                                                 g.imgui.IsHotOrActive(imgui_id, MouseButton::Left)
                                                     ? ChangeBrightness(ToU32({.c = Col::Blue}), 1.3f)
                                                     : ToU32({.c = Col::Blue}),
                                                 ICON_FA_CIRCLE_PLUS,
                                                 {
                                                     .justification = TextJustification::Centred,
                                                     .overflow_type = TextOverflowType::AllowOverflow,
                                                     .font_scaling = 0.9f,
                                                 });
    }
}

void MacroGuiBeginFrame(GuiState& g) {
    g.macros_gui_state.hot_destination_param.Clear();
    dyn::Clear(g.macros_gui_state.draw_overlays);
}

void MacroGuiEndFrame(GuiState& g) {
    for (auto const& draw_overlay : g.macros_gui_state.draw_overlays)
        draw_overlay(g);

    // Check if we should exit macro destination select mode.
    if (g.macros_gui_state.macro_destination_select_mode) {
        GuiIo().out.wants.keyboard_keys.Set(ToInt(KeyCode::Escape));
        auto const& left_mouse = GuiIo().in.Mouse(MouseButton::Left);
        if ((left_mouse.presses.size && !g.imgui.AnItemIsHot()) ||
            GuiIo().in.Key(KeyCode::Escape).presses.size || g.imgui.IsAnyPopupMenuOpen()) {
            g.macros_gui_state.macro_destination_select_mode.Clear();
        }
    }
}
