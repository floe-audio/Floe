// Copyright 2024-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_layer_subtabbed.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "gui/controls/gui_curve_map.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/panels/gui_layer_common.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/granular.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

constexpr f32 k_page_row_gap_y = 7;
constexpr f32 k_page_row_gap_x = 7;
constexpr f32 k_knob_width = 36;

static void DoMuteSoloButton(GuiState& g, Box parent, DescribedParamValue const& param, bool is_solo) {
    auto const state = param.BoolValue();
    auto const on_back_col =
        is_solo ? LiveColStruct(UiColMap::SoloButtonBackOn) : LiveColStruct(UiColMap::MuteButtonBackOn);

    auto const btn = DoBox(
        g.builder,
        {
            .parent = parent,
            .id_extra = is_solo,
            .text = is_solo ? "S"_s : "M"_s,
            .text_colours = state ? Colours {ColSet {
                                        .base = LiveColStruct(UiColMap::MuteSoloButtonTextOn),
                                        .hot = LiveColStruct(UiColMap::MuteSoloButtonTextOnHot),
                                        .active = LiveColStruct(UiColMap::MuteSoloButtonTextOnHot),
                                    }}
                                  : Colours {ColSet {
                                        .base = LiveColStruct(UiColMap::MidText),
                                        .hot = LiveColStruct(UiColMap::MidTextHot),
                                        .active = LiveColStruct(UiColMap::MidTextHot),
                                    }},
            .text_justification = TextJustification::Centred,
            .background_fill_colours = state ? Colours {on_back_col} : Colours {Col {.c = Col::None}},
            .round_background_corners = is_solo ? (Corners)0b0110 : (Corners)0b1001,
            .corner_rounding = k_corner_rounding,
            .layout {
                .size = layout::k_fill_parent,
            },
            .tooltip =
                FunctionRef<String()> {[&]() -> String { return ParamTooltipText(param, g.builder.arena); }},
            .button_behaviour = imgui::ButtonConfig {},
        });

    if (btn.button_fired) SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

    AddParamContextMenuBehaviour(g, btn, param);
}

static void DoMixerContainer1(GuiState& g, u8 layer_index, Box root) {
    auto& params = g.engine.processor.main_params;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 14,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Volume knob with peak meter
    auto const& layer_processor = g.engine.processor.layer_processors[layer_index];
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::Volume),
                    {
                        .width = 88,
                        .knob_height_fraction = 0.88f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .peak_meter = &layer_processor.peak_meter,
                    });

    // Mute/Solo buttons
    {
        auto const mute_solo_container = DoBox(g.builder,
                                               {
                                                   .parent = container,
                                                   .layout {
                                                       .size = {k_mid_button_height * 2, k_mid_button_height},
                                                       .contents_direction = layout::Direction::Row,
                                                       .contents_align = layout::Alignment::Start,
                                                   },
                                               });

        // Background and divider
        if (auto const r = BoxRect(g.builder, mute_solo_container)) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
            auto const rounding = WwToPixels(k_corner_rounding);
            g.imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::MidDarkSurface), rounding);
            // Vertical divider in the middle
            g.imgui.draw_list->AddLine({window_r.Centre().x, window_r.y},
                                       {window_r.Centre().x, window_r.Bottom()},
                                       LiveCol(UiColMap::MuteSoloButtonDivider));
        }

        DoMuteSoloButton(g,
                         mute_solo_container,
                         params.DescribedValue(layer_index, LayerParamIndex::Mute),
                         false);
        DoMuteSoloButton(g,
                         mute_solo_container,
                         params.DescribedValue(layer_index, LayerParamIndex::Solo),
                         true);
    }
}

static void DoWhitespace(GuiBuilder& builder, Box parent, f32 height, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = loc_hash,
              .layout {.size = {1, height}},
          });
}

static void DoPageTabs(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_state = g.layer_panel_states[layer_index];

    auto const tabs_row = DoBox(g.builder,
                                {
                                    .parent = parent,
                                    .border_colours = LiveColStruct(UiColMap::MidViewportDivider),
                                    .border_edges = 0b0101,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_direction = layout::Direction::Row,
                                        .contents_align = layout::Alignment::Middle,
                                    },
                                });

    for (auto const i : Range(ToInt(LayerPageType::Count))) {
        auto const page_type = (LayerPageType)i;
        bool const is_selected = page_type == layer_state.selected_page;
        bool const tab_has_active_content = ({
            bool result = false;
            switch (page_type) {
                case LayerPageType::Filter:
                    result = params.BoolValue(layer_index, LayerParamIndex::FilterOn);
                    break;
                case LayerPageType::Lfo:
                    result = params.BoolValue(layer_index, LayerParamIndex::LfoOn);
                    break;
                case LayerPageType::Eq: result = params.BoolValue(layer_index, LayerParamIndex::EqOn); break;
                case LayerPageType::Main:
                case LayerPageType::Engine:
                case LayerPageType::Play:
                case LayerPageType::Count: break;
            }
            result;
        });

        auto const name = [&]() -> String {
            switch (page_type) {
                case LayerPageType::Main: return "Main"_s;
                case LayerPageType::Engine: return "Engine"_s;
                case LayerPageType::Eq: return "EQ"_s;
                case LayerPageType::Play: return "Play"_s;
                case LayerPageType::Lfo: return "LFO"_s;
                case LayerPageType::Filter: return "Filter"_s;
                case LayerPageType::Count: PanicIfReached();
            }
            return {};
        }();

        auto const tab_btn = DoBox(g.builder,
                                   {
                                       .parent = tabs_row,
                                       .id_extra = (u64)i,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                       },
                                       .tooltip = FunctionRef<String()> {[&]() -> String {
                                           return fmt::Format(g.scratch_arena, "Open {} tab", name);
                                       }},
                                       .button_behaviour = imgui::ButtonConfig {},
                                   });

        DoBox(g.builder,
              {
                  .parent = tab_btn,
                  .text = name,
                  .size_from_text = true,
                  .text_colours = is_selected ? Colours {ColSet {
                                                    .base = LiveColStruct(UiColMap::MidTextOn),
                                                    .hot = LiveColStruct(UiColMap::MidTextHot),
                                                    .active = LiveColStruct(UiColMap::MidTextHot),
                                                }}
                                              : Colours {ColSet {
                                                    .base = LiveColStruct(UiColMap::MidText),
                                                    .hot = LiveColStruct(UiColMap::MidTextHot),
                                                    .active = LiveColStruct(UiColMap::MidTextHot),
                                                }},
                  .text_justification = TextJustification::Centred,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .margins {.lr = 2.2f, .tb = 12.7f},
                  },
              });

        if (tab_btn.button_fired) layer_state.selected_page = page_type;

        // Draw active-content dot indicator
        if (tab_has_active_content) {
            if (auto const r = BoxRect(g.builder, tab_btn)) {
                auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
                auto const dot_centre = f32x2 {window_r.CentreX(), window_r.CentreY() + WwToPixels(10.0f)};
                auto const col =
                    is_selected ? LiveCol(UiColMap::MidTextOn) : LiveCol(UiColMap::MidTextDimmed);
                g.imgui.draw_list->AddCircleFilled(dot_centre, WwToPixels(2.0f), col);
            }
        }
    }

    // Auto-switch tab when a macro destination knob is being interacted
    if (g.macros_gui_state.active_destination_knob) {
        auto const param =
            LayerParamIndexAndLayerFor(*g.macros_gui_state.active_destination_knob->dest.param_index);
        if (param && param->layer_num == layer_index) {
            auto const k_desc = ParamDescriptorAt(ParamIndexFromLayerParamIndex(layer_index, param->param));
            Optional<LayerPageType> new_page {};
            if (k_desc.module_parts.size >= 2) {
                switch (k_desc.module_parts[1]) {
                    case ParameterModule::Loop:
                    case ParameterModule::VolEnv: new_page = LayerPageType::Main; break;
                    case ParameterModule::Lfo: new_page = LayerPageType::Lfo; break;
                    case ParameterModule::Filter: new_page = LayerPageType::Filter; break;
                    case ParameterModule::Playback: new_page = LayerPageType::Play; break;
                    case ParameterModule::Eq: new_page = LayerPageType::Eq; break;
                    default: break;
                }
            }
            if (new_page && *new_page != layer_state.selected_page) {
                layer_state.selected_page = *new_page;
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }
        }
    }
}

static void DoMixerContainer2(GuiState& g, u8 layer_index, Box root) {
    auto& params = g.engine.processor.main_params;

    auto const container = DoBox(g.builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = layout::k_hug_contents,
                                         .contents_gap = 18,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    // Tune semitone (int dragger)
    DoIntParameter(g,
                   container,
                   params.DescribedValue(layer_index, LayerParamIndex::TuneSemitone),
                   {
                       .width = 58,
                       .always_show_plus = true,
                   });

    // Tune cents (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::TuneCents),
                    {
                        .width = k_knob_width,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });

    // Pan (bidirectional knob)
    DoKnobParameter(g,
                    container,
                    params.DescribedValue(layer_index, LayerParamIndex::Pan),
                    {
                        .width = k_knob_width,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });
}

static void DoFilterPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;
    bool const filter_on = params.BoolValue(layer_index, LayerParamIndex::FilterOn);
    bool const greyed_out = !filter_on;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Heading row: FilterOn toggle + FilterType menu
    {
        auto const heading_row = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .margins = {.b = 15},
                                               .contents_gap = 12,
                                               .contents_direction = layout::Direction::Row,
                                           },
                                       });

        DoButtonParameter(g,
                          heading_row,
                          params.DescribedValue(layer_index, LayerParamIndex::FilterOn),
                          {.width = layout::k_hug_contents});

        DoMenuParameter(g,
                        heading_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterType),
                        {.width = layout::k_fill_parent, .greyed_out = greyed_out, .label = false});
    }

    // Knobs row: Cutoff, Resonance, EnvAmount
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .margins = {.b = 20},
                                             .contents_gap = 22.6f,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterResonance),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::FilterEnvAmount),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    }

    // Filter envelope
    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, 75},
                                            },
                                        });

        bool const env_greyed_out =
            greyed_out || (params.LinearValue(layer_index, LayerParamIndex::FilterEnvAmount) == 0);
        if (auto const r = BoxRect(g.builder, envelope_box))
            DoEnvelopeGui(g,
                          layer,
                          *r,
                          env_greyed_out,
                          {LayerParamIndex::FilterAttack,
                           LayerParamIndex::FilterDecay,
                           LayerParamIndex::FilterSustain,
                           LayerParamIndex::FilterRelease},
                          GuiEnvelopeType::Filter);
    }
}

static void DoEqPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::EqOn);

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    // EqOn heading
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::EqOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    DoWhitespace(g.builder, page, 8);

    // Container for all EQ band rows with consistent gap
    auto const bands_container = DoBox(g.builder,
                                       {
                                           .parent = page,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_gap = 14,
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                           },
                                       });

    // EQ band helper - adds menu row and knobs row to bands_container
    auto const do_eq_band = [&](LayerParamIndex type_param,
                                LayerParamIndex freq_param,
                                LayerParamIndex reso_param,
                                LayerParamIndex gain_param,
                                u8 band_number) {
        auto const band = DoBox(g.builder,
                                {
                                    .parent = bands_container,
                                    .id_extra = band_number,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_gap = 6,
                                        .contents_direction = layout::Direction::Column,

                                    },
                                });

        // Type menu
        auto const menu_row = DoBox(g.builder,
                                    {
                                        .parent = band,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_gap = 6,
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Middle,
                                        },
                                    });

        DoBox(g.builder,
              {
                  .parent = menu_row,
                  .text = fmt::Format(g.scratch_arena, "Band {}", band_number),
                  .text_colours = LiveColStruct(greyed_out ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
              });
        DoMenuParameter(g,
                        menu_row,
                        params.DescribedValue(layer_index, type_param),
                        {
                            .width = 130,
                            .greyed_out = greyed_out,
                            .label = false,
                        });

        // Knobs row
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = band,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 15,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, freq_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, reso_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, gain_param),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    };

    // Band 1
    do_eq_band(LayerParamIndex::EqType1,
               LayerParamIndex::EqFreq1,
               LayerParamIndex::EqResonance1,
               LayerParamIndex::EqGain1,
               1);

    // Band 2
    do_eq_band(LayerParamIndex::EqType2,
               LayerParamIndex::EqFreq2,
               LayerParamIndex::EqResonance2,
               LayerParamIndex::EqGain2,
               2);
}

static void DoLfoPage(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::LfoOn);

    constexpr f32 k_menu_width = 135;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_page_row_gap_y,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // LfoOn
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::LfoOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    DoWhitespace(g.builder, page, 2);

    // Menu + label rows
    auto const do_menu_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = loc_hash,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(greyed_out ? UiColMap::MidTextDimmed : UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
              });

        DoMenuParameter(g,
                        row,
                        param,
                        {
                            .width = k_menu_width,
                            .greyed_out = greyed_out,
                            .label = false,
                        });
    };

    do_menu_label_row(LayerParamIndex::LfoDestination);
    do_menu_label_row(LayerParamIndex::LfoShape);
    do_menu_label_row(LayerParamIndex::LfoRestart);

    DoWhitespace(g.builder, page, 5);

    // Knobs row: Amount + Rate column
    {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 20,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, LayerParamIndex::LfoAmount),
                        {
                            .width = k_knob_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });

        // Rate column
        auto const rate_col = DoBox(g.builder,
                                    {
                                        .parent = knobs_row,
                                        .layout {
                                            .size = layout::k_hug_contents,
                                            .contents_gap = 3,
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });

        if (params.BoolValue(layer_index, LayerParamIndex::LfoSyncSwitch)) {
            DoMenuParameter(g,
                            rate_col,
                            params.DescribedValue(layer_index, LayerParamIndex::LfoRateTempoSynced),
                            {.greyed_out = greyed_out});
        } else {
            DoKnobParameter(g,
                            rate_col,
                            params.DescribedValue(layer_index, LayerParamIndex::LfoRateHz),
                            {
                                .width = k_knob_width,
                                .style_system = GuiStyleSystem::MidPanel,
                                .greyed_out = greyed_out,
                            });
        }

        DoButtonParameter(g,
                          rate_col,
                          params.DescribedValue(layer_index, LayerParamIndex::LfoSyncSwitch),
                          {.width = layout::k_hug_contents, .greyed_out = greyed_out});
    }
}

static void DoPlayPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    constexpr auto k_control_width = 76;
    constexpr auto k_narrow_control_width = 63;
    constexpr auto k_narrow_control_gap_x = 3;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = 4,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Helper for int dragger + label rows (Transpose, PitchBend)
    auto const do_int_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = loc_hash,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String { return param.info.tooltip; }},
              });

        DoIntParameter(g,
                       row,
                       param,
                       {
                           .width = k_control_width,
                           .label = false,
                       });
    };

    // Transpose
    do_int_label_row(LayerParamIndex::MidiTranspose);

    // PitchBend
    do_int_label_row(LayerParamIndex::PitchBendRange);

    // Keytrack
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::Keytrack);
        bool const state = param.BoolValue();

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .id_extra = (u64)param.info.id,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                                   .tooltip = FunctionRef<String()> {[&]() -> String {
                                       return ParamTooltipText(param, g.builder.arena);
                                   }},
                                   .button_behaviour = imgui::ButtonConfig {},
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours =
                      ColSet {
                          .base = LiveColStruct(UiColMap::MidText),
                          .hot = LiveColStruct(UiColMap::MidTextHot),
                          .active = LiveColStruct(UiColMap::MidTextHot),
                      },
                  .text_justification = TextJustification::CentredRight,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoToggleIcon(g.builder,
                     row,
                     {
                         .state = state,
                         .width = k_control_width,
                         .justify = TextJustification::CentredLeft,
                     });

        if (row.button_fired)
            SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});

        AddParamContextMenuBehaviour(g, row, param);
    }

    // Monophonic mode
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::MonophonicMode);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoMenuParameter(g, row, param, {.width = k_control_width, .label = false});
    }

    // Key Range row
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_narrow_control_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Range"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLow),
                       {
                           .width = k_narrow_control_width,
                           .midi_note_names = true,
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHigh),
                       {
                           .width = k_narrow_control_width,
                           .midi_note_names = true,
                           .label = false,
                       });
    }

    // Key Fade row
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_narrow_control_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Key Fade"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLowFade),
                       {
                           .width = k_narrow_control_width,
                           .label = false,
                       });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHighFade),
                       {
                           .width = k_narrow_control_width,
                           .label = false,
                       });
    }

    // Velocity curve
    {
        auto const col = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = 2,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });
        // Label
        DoBox(g.builder,
              {
                  .parent = col,
                  .text = "Velocity to volume curve"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      return "Curve that maps velocity to volume"_s;
                  }},
              });

        // Element
        auto const velo_box = DoBox(g.builder,
                                    {
                                        .parent = col,
                                        .layout {
                                            .size = {layout::k_fill_parent, 53},
                                        },
                                    });

        if (auto const r = BoxRect(g.builder, velo_box)) {
            auto const window_r = g.imgui.ViewportRectToWindowRect(*r);

            Optional<f32> velocity {};
            if (g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed))
                velocity =
                    g.engine.processor.voice_pool.last_velocity[layer_index].Load(LoadMemoryOrder::Relaxed);

            DoCurveMap(
                g,
                layer.velocity_curve_map,
                window_r,
                velocity,
                "Configures how MIDI velocity maps to volume. X-axis: velocity, Y-axis: volume. Adjust the curve to customize this relationship.");
        }
    }
}

static void DoEnginePage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const control_width = 130;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_page_row_gap_y,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

#if EXPERIMENTAL_GRANULAR
    // Engine type menu
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::PlayMode);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        DoMenuParameter(g, row, param, {.width = control_width, .label = false});
    }

    auto const play_mode = params.IntValue<param_values::PlayMode>(layer_index, LayerParamIndex::PlayMode);
#else
    auto const play_mode = param_values::PlayMode::Standard;
#endif

    // Waveform display
    if (auto const r = BoxRect(g.builder,
                               DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, 70},
                                         },
                                     })))
        DoWaveformElement(g, layer, *r, {.play_mode = play_mode});

    // Reverse toggle
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::Reverse);
        bool const is_waveform_synth = layer.instrument_id.tag == InstrumentType::WaveformSynth;

        DoButtonParameter(g,
                          page,
                          param,
                          {
                              .width = layout::k_fill_parent,
                              .greyed_out = is_waveform_synth,
                          });
    }

    // Loop mode selector (hidden in granular position mode)
    if (play_mode != param_values::PlayMode::GranularFixed) {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = page,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_page_row_gap_x,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = "Loop"_s,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredRight,
                  .layout {
                      .size = layout::k_fill_parent,
                  },
              });

        auto const loop_container = DoBox(g.builder,
                                          {
                                              .parent = row,
                                              .layout {
                                                  .size = {control_width, layout::k_hug_contents},
                                              },
                                          });

        DoLoopModeSelector(g, loop_container, layer);
    }

#if EXPERIMENTAL_GRANULAR
    // Granular controls
    if (IsGranular(play_mode)) {
        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = page,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = {35, 2},
                                             .contents_direction = layout::Direction::Row,
                                             .contents_multiline = true,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        auto const do_knob = [&](LayerParamIndex param) {
            DoKnobParameter(g,
                            knobs_row,
                            params.DescribedValue(layer_index, param),
                            {
                                .width = 20,
                                .style_system = GuiStyleSystem::MidPanel,
                            });
        };

        do_knob((play_mode == param_values::PlayMode::GranularPlayback) ? LayerParamIndex::GranularSpeed
                                                                        : LayerParamIndex::GranularPosition);
        do_knob(LayerParamIndex::GranularSpread);
        do_knob(LayerParamIndex::GranularGrains);
        do_knob(LayerParamIndex::GranularLength);
        do_knob(LayerParamIndex::GranularSmoothing);
        do_knob(LayerParamIndex::GranularRandomPan);
    }
#endif
}

static void DoMainPage(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const page = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    // Waveform display
    {
        auto const waveform_box = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, 70},
                                            },
                                        });
        if (auto const r = BoxRect(g.builder, waveform_box)) DoWaveformElement(g, layer, *r);
    }

    // Instrument info strip
    DoInstrumentInfoStrip(g, layer_index, page, {});

    DoWhitespace(g.builder, page, 16);

    // Volume envelope on button
    DoButtonParameter(g,
                      page,
                      params.DescribedValue(layer_index, LayerParamIndex::VolEnvOn),
                      {
                          .width = layout::k_fill_parent,
                      });

    // Envelope display
    {
        auto const envelope_box = DoBox(g.builder,
                                        {
                                            .parent = page,
                                            .layout {
                                                .size = {layout::k_fill_parent, 80},
                                                .margins {
                                                    .tb = 10,
                                                },
                                            },
                                        });

        bool const env_on = params.BoolValue(layer_index, LayerParamIndex::VolEnvOn) ||
                            layer.instrument.tag == InstrumentType::WaveformSynth;
        if (auto const r = BoxRect(g.builder, envelope_box))
            DoEnvelopeGui(g,
                          layer,
                          *r,
                          !env_on,
                          {LayerParamIndex::VolumeAttack,
                           LayerParamIndex::VolumeDecay,
                           LayerParamIndex::VolumeSustain,
                           LayerParamIndex::VolumeRelease},
                          GuiEnvelopeType::Volume);
    }
}

void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent) {
    auto const root = DoBox(g.builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Top controls: houses the instrument selector and will later contain volume, tune, pan, etc.
    auto const top_controls = DoBox(g.builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding {
                                                .lr = 6.3f,
                                                .t = 6.3f,
                                            },
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    DoInstSelector(g, frame_context, layer_index, top_controls);

    if (g.engine.Layer(layer_index).instrument.tag == InstrumentType::None) return;

    DoWhitespace(g.builder, top_controls, 11);

    DoMixerContainer1(g, layer_index, top_controls);
    DoWhitespace(g.builder, top_controls, 10);
    DoMixerContainer2(g, layer_index, top_controls);

    DoWhitespace(g.builder, top_controls, 10);

    DoPageTabs(g, layer_index, root);

    auto const page_container = DoBox(g.builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = layout::k_fill_parent,
                                              .contents_padding = {.lr = 15, .tb = 8},
                                          },
                                      });

    // Page content
    switch (g.layer_panel_states[layer_index].selected_page) {
        case LayerPageType::Main: DoMainPage(g, layer_index, page_container); break;
        case LayerPageType::Filter: DoFilterPage(g, layer_index, page_container); break;
        case LayerPageType::Engine: DoEnginePage(g, layer_index, page_container); break;
        case LayerPageType::Lfo: DoLfoPage(g, layer_index, page_container); break;
        case LayerPageType::Eq: DoEqPage(g, layer_index, page_container); break;
        case LayerPageType::Play: DoPlayPage(g, layer_index, page_container); break;
        case LayerPageType::Count: PanicIfReached();
    }
}
