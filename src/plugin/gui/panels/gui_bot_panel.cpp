// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui/controls/gui_keyboard.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui_framework/gui_live_edit.hpp"

static bool IconButton(GuiBuilder& builder,
                       Box const parent,
                       String icon,
                       TooltipString tooltip,
                       f32 font_scale = 1.0f,
                       u64 id_extra = SourceLocationHash()) {
    auto const button = DoBox(builder,
                              {
                                  .parent = parent,
                                  .id_extra = id_extra,
                                  .layout {
                                      .size = layout::k_hug_contents,
                                      .contents_padding = {.lr = 3, .tb = 2},
                                  },
                                  .tooltip = tooltip,
                                  .button_behaviour = imgui::ButtonConfig {},
                              });

    DoBox(builder,
          {
              .parent = button,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * font_scale,
              .text_colours =
                  ColSet {
                      .base {.c = Col::Subtext1, .dark_mode = true},
                      .hot {.c = Col::Highlight},
                      .active {.c = Col::Highlight},
                  },
              .parent_dictates_hot_and_active = true,
          });
    return button.button_fired;
}

static Optional<s64> OctaveDragger(GuiBuilder& builder,
                                   Box const parent,
                                   s64 value,
                                   s64 oct_lowest,
                                   s64 oct_highest,
                                   u64 id_extra = SourceLocationHash()) {
    auto percent = MapTo01((f32)value, (f32)oct_lowest, (f32)oct_highest);
    auto const box = DoBox(builder,
                           {
                               .parent = parent,
                               .id_extra = id_extra,
                               .layout {
                                   .size = {28, k_font_body_size},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .tooltip = "Keyboard octave offset"_s,
                           });

    Optional<s64> new_value {};

    if (auto const viewport_r = BoxRect(builder, box)) {
        auto constexpr k_fmt_string = "{+}"_s;
        imgui::TextInputConfig constexpr k_input_config = {
            .chars_decimal = true,
            .centre_align = true,
            .escape_unfocuses = true,
            .select_all_when_opening = true,
        };
        DrawTextInputConfig constexpr k_draw_config = {
            .text_col = {.c = Col::Text, .dark_mode = true},
            .cursor_col = {.c = Col::Text, .dark_mode = true},
            .selection_col = {.c = Col::Highlight, .alpha = 128},
        };

        auto const window_r = builder.imgui.RegisterAndConvertRect(*viewport_r);
        auto const text = fmt::Format(builder.arena, k_fmt_string, value);
        auto const dragger_result = builder.imgui.DraggerBehaviour({
            .rect_in_window_coords = window_r,
            .id = box.imgui_id,
            .text = text,
            .min = 0,
            .max = 1,
            .value = percent,
            .default_value = 0,
            .text_input_button_cfg {
                .mouse_button = MouseButton::Left,
                .event = MouseButtonEvent::DoubleClick,
            },
            .text_input_cfg = k_input_config,
            .slider_cfg {
                .sensitivity = 200,
                .slower_with_shift = true,
                .default_on_modifer = true,
            },
        });

        if (dragger_result.new_string_value)
            new_value = ParseInt(*dragger_result.new_string_value, ParseIntBase::Decimal);

        if (dragger_result.value_changed)
            new_value = (s64)MapFrom01(percent, (f32)oct_lowest, (f32)oct_highest);

        if (dragger_result.text_input_result)
            DrawTextInput(builder.imgui, *dragger_result.text_input_result, k_draw_config);
        else {
            auto const draw_text = new_value ? fmt::Format(builder.arena, k_fmt_string, *new_value) : text;
            auto const pos = imgui::TextInputTextPos(draw_text, window_r, k_input_config, builder.fonts);
            builder.imgui.draw_list->AddText(pos, ToU32(k_draw_config.text_col), draw_text, {});
        }
    }

    return new_value;
}

static void DoBotPanel(GuiState& g) {
    auto& builder = g.builder;

    bool const perform_tab_active = g.mid_panel_state.tab == MidPanelTab::Perform;

    auto const root = DoBox(builder,
                            {
                                .background_fill_colours = Col {.c = Col::Background0, .dark_mode = true},
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = 0,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    if (!perform_tab_active) {
        auto const tabs = DoBox(builder,
                                {
                                    .parent = root,
                                    .background_fill_colours = Col {.c = Col::Background0, .dark_mode = true},
                                    .layout {
                                        .size = {55, layout::k_fill_parent},
                                        .contents_padding = {.lr = 3, .tb = 6},
                                        .contents_gap = 2,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Middle,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

        auto const tab_button =
            [&](BottomPanelType type, TooltipString tooltip, u64 id_extra = SourceLocationHash()) {
                auto const name = [type]() -> String {
                    switch (type) {
                        case BottomPanelType::Play: return "PLAY"_s;
                        case BottomPanelType::EditMacros: return "MACROS"_s;
                        case BottomPanelType::Count: PanicIfReached();
                    }
                }();
                return DoTabButton(builder,
                                   tabs,
                                   name,
                                   {
                                       .is_selected = type == g.bottom_panel_state.type,
                                       .width = layout::k_fill_parent,
                                       .tooltip = tooltip,
                                   },
                                   id_extra);
            };

        Optional<BottomPanelType> new_panel {};

        if (tab_button(BottomPanelType::Play, "Play tab: core UI for playing sounds"_s).button_fired)
            new_panel = BottomPanelType::Play;

        if (tab_button(BottomPanelType::EditMacros, "Edit macros tabs: change macro destinations and names"_s)
                .button_fired)
            new_panel = BottomPanelType::EditMacros;

        if (new_panel) g.bottom_panel_state.type = *new_panel;
    }

    if (perform_tab_active) {
        // Full-width keyboard
        constexpr s8 k_perform_num_octaves = 9;
        constexpr int k_perform_starting_octave = -1;

        auto const keyboard = DoBox(builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = layout::k_fill_parent,
                                            .margins = {.lrtb = 3},
                                        },
                                    });
        if (auto const r = BoxRect(builder, keyboard)) {
            if (auto key = KeyboardGui(g, *r, k_perform_starting_octave, k_perform_num_octaves)) {
                g.engine.processor.gui_note_click_state.Store(
                    {
                        .velocity = key->velocity,
                        .key = key->note,
                        .is_held = key->is_down,
                    },
                    StoreMemoryOrder::Release);
                g.engine.host.request_process(&g.engine.host);
            }
        }
    } else if (g.bottom_panel_state.type == BottomPanelType::Play) {
        // Macro knobs.
        {
            auto const macro_box = DoBox(builder,
                                         {
                                             .parent = root,
                                             .background_fill_colours = Col {.c = Col::None},
                                             .round_background_corners = 0b1111,
                                             .layout {
                                                 .size = {layout::k_hug_contents, layout::k_fill_parent},
                                                 .margins = {.lrtb = 3},
                                                 .contents_padding = {.lr = 20},
                                                 .contents_gap = 30,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                             },
                                         });

            for (auto const [macro_index, param_index] : Enumerate(k_macro_params))
                DoKnobParameter(
                    g,
                    macro_box,
                    g.engine.processor.main_params.DescribedValue(param_index),
                    {
                        .width = k_small_knob_width,
                        .greyed_out = g.engine.processor.main_macro_destinations[macro_index].Size() == 0,
                        .override_label = g.engine.macro_names[macro_index],
                    });
        }

        auto const keyboard_octave =
            Clamp<s64>(prefs::LookupInt(g.prefs, prefs::key::k_gui_keyboard_octave).ValueOr(0),
                       k_octave_lowest,
                       k_octave_highest);

        {
            auto const keyboard = DoBox(builder,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = layout::k_fill_parent,
                                                .margins = {.l = 0, .r = 3, .tb = 3},
                                            },
                                        });
            if (auto const r = BoxRect(builder, keyboard)) {
                if (auto key = KeyboardGui(g, *r, (int)keyboard_octave)) {
                    g.engine.processor.gui_note_click_state.Store(
                        {
                            .velocity = key->velocity,
                            .key = key->note,
                            .is_held = key->is_down,
                        },
                        StoreMemoryOrder::Release);
                    g.engine.host.request_process(&g.engine.host);
                }
            }
        }

        {
            auto const octave_box = DoBox(builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Middle,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                              },
                                          });

            Optional<s64> new_octave {};

            if (IconButton(builder, octave_box, ICON_FA_CARET_UP, "GUI Keyboard Octave Up"_s))
                new_octave = Min<s64>(keyboard_octave + 1, k_octave_highest);

            if (auto const v =
                    OctaveDragger(builder, octave_box, keyboard_octave, k_octave_lowest, k_octave_highest))
                new_octave = *v;

            if (IconButton(builder, octave_box, ICON_FA_CARET_DOWN, "GUI Keyboard Octave Down"_s))
                new_octave = Max<s64>(keyboard_octave - 1, k_octave_lowest);

            if (new_octave) prefs::SetValue(g.prefs, prefs::key::k_gui_keyboard_octave, *new_octave);
        }
    } else {
        switch (g.bottom_panel_state.type) {
            case BottomPanelType::EditMacros: {
                DoMacrosEditGui(g, root);
                break;
            }
            case BottomPanelType::Play:
            case BottomPanelType::Count: PanicIfReached();
        }
    }
}

void BotPanel(GuiState& g, Rect const r) {
    DoBoxViewport(g.builder,
                  {
                      .run = [&g](GuiBuilder&) { DoBotPanel(g); },
                      .bounds = r,
                      .imgui_id = g.imgui.MakeId("BotPanel"),
                      .viewport_config = ({
                          auto cfg = k_default_modal_subviewport;
                          cfg.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
                          cfg;
                      }),
                  });
}
