// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer_maximised.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/loop_modes.hpp"
#include "gui/controls/gui_curve_map.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/panels/gui_layer_common.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

// =================================================================================================
// Section heading helper

static Box DoSectionHeading(GuiBuilder& builder, Box parent, String text) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = Hash(text),
                     .text = text,
                     .size_from_text = true,
                     .font = FontType::Heading3,
                     .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                     .text_justification = TextJustification::CentredLeft,
                     .capitalize_text = true,
                     .layout {
                         .margins = {.b = 6},
                     },
                 });
}

// =================================================================================================
// Section container: a rounded box with subtle dark background

static Box DoSectionContainer(GuiState& g, Box parent, f32 width, u64 id_extra = SourceLocationHash()) {
    return DoBox(g.builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
                     .layout {
                         .size = {width, layout::k_hug_contents},
                         .contents_padding = {.lr = 8, .t = 8, .b = 10},
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

// =================================================================================================
// Instrument selector row

static void DrawBlurredBackgroundForBox(GuiState& g, Box box, Optional<sample_lib::LibraryIdRef> lib_id) {
    if (auto const r = BoxRect(g.builder, box))
        DrawMidBlurredPanelSurface(g, g.imgui.ViewportRectToWindowRect(*r), lib_id);
}

static void
DoLayerInstSelector(GuiState& g, GuiFrameContext const& frame_context, u8 layer_index, Box parent) {
    auto const lib_id = g.engine.Layer(layer_index).LibId();
    DoInstSelector(g, frame_context, layer_index, parent, [&g, lib_id](Rect window_r) {
        DrawMidBlurredPanelSurface(g, window_r, lib_id);
    });
}

// =================================================================================================
// Mute/Solo buttons (compact)

static void DoMuteSoloCompact(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;

    auto const container =
        DoBox(g.builder,
              {
                  .parent = parent,
                  .layout {
                      .size = {LiveWw(UiSizeId::LayerMuteSoloW), LiveWw(UiSizeId::LayerMuteSoloH)},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                  },
              });

    if (auto const r = BoxRect(g.builder, container)) {
        DrawBlurredBackgroundForBox(g, container, g.engine.Layer(layer_index).LibId());

        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddLine({window_r.Centre().x, window_r.y},
                                   {window_r.Centre().x, window_r.Bottom()},
                                   LiveCol(UiColMap::MuteSoloButtonDivider));
    }

    auto const do_btn = [&](LayerParamIndex param_index, bool is_solo) {
        auto const param = params.DescribedValue(layer_index, param_index);
        auto const state = param.BoolValue();
        auto const on_back_col =
            is_solo ? LiveColStruct(UiColMap::SoloButtonBackOn) : LiveColStruct(UiColMap::MuteButtonBackOn);

        auto const btn = DoBox(
            g.builder,
            {
                .parent = container,
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
                .corner_rounding = LiveWw(UiSizeId::CornerRounding),
                .layout {
                    .size = layout::k_fill_parent,
                },
                .tooltip = FunctionRef<String()> {[&]() -> String {
                    return ParamTooltipText(param, g.builder.arena);
                }},
                .button_behaviour = imgui::ButtonConfig {},
            });

        if (btn.button_fired)
            SetParameterValue(g.engine.processor, param.info.index, state ? 0.0f : 1.0f, {});
        AddParamContextMenuBehaviour(g, btn, param);
    };

    do_btn(LayerParamIndex::Mute, false);
    do_btn(LayerParamIndex::Solo, true);
}

// =================================================================================================
// Parameter group sections

static void DoEngineSection(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_processor = g.engine.processor.layer_processors[layer_index];

    auto const section = DoSectionContainer(g, parent, 200);

    DoSectionHeading(g.builder, section, "ENGINE"_s);

    auto const col = DoBox(g.builder,
                           {
                               .parent = section,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_gap = 12,
                                   .contents_direction = layout::Direction::Column,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                               },
                           });

    bool const is_waveform_synth = layer_processor.instrument_id.tag == InstrumentType::WaveformSynth;
    DoButtonParameter(g,
                      col,
                      params.DescribedValue(layer_index, LayerParamIndex::Reverse),
                      {.width = 60, .greyed_out = is_waveform_synth});

    DoLoopModeSelector(g, col, layer_processor);
}

static void DoMixerSection(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    auto& layer_processor = g.engine.processor.layer_processors[layer_index];

    auto const section = DoSectionContainer(g, parent, layout::k_hug_contents);

    DoSectionHeading(g.builder, section, "MIXER"_s);

    auto const row = DoBox(g.builder,
                           {
                               .parent = section,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_gap = 12,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    DoKnobParameter(g,
                    row,
                    params.DescribedValue(layer_index, LayerParamIndex::Volume),
                    {
                        .width = 80,
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .peak_meter = &layer_processor.peak_meter,
                    });

    DoKnobParameter(g,
                    row,
                    params.DescribedValue(layer_index, LayerParamIndex::Pan),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });

    auto const col = DoBox(g.builder,
                           {
                               .parent = row,
                               .layout {
                                   .size = {layout::k_hug_contents, layout::k_hug_contents},
                                   .contents_gap = 12,
                                   .contents_direction = layout::Direction::Column,
                                   .contents_align = layout::Alignment::Middle,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    DoKnobParameter(g,
                    col,
                    params.DescribedValue(layer_index, LayerParamIndex::TuneCents),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .knob_height_fraction = 0.96f,
                        .style_system = GuiStyleSystem::MidPanel,
                        .bidirectional = true,
                    });

    DoIntParameter(g,
                   col,
                   params.DescribedValue(layer_index, LayerParamIndex::TuneSemitone),
                   {.width = 80, .always_show_plus = true});
}

static void DoVolEnvSection(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;

    auto const section = DoSectionContainer(g, parent, 290);

    auto const heading_row = DoBox(g.builder,
                                   {
                                       .parent = section,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Justify,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    DoSectionHeading(g.builder, heading_row, "VOLUME ENVELOPE"_s);

    DoButtonParameter(g, heading_row, params.DescribedValue(layer_index, LayerParamIndex::VolEnvOn), {});

    // Envelope display
    auto const envelope_box = DoBox(g.builder,
                                    {
                                        .parent = section,
                                        .layout {
                                            .size = {layout::k_fill_parent, 110},
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

static void DoFilterSection(GuiState& g, u8 layer_index, Box parent) {
    auto& layer = g.engine.Layer(layer_index);
    auto& params = g.engine.processor.main_params;
    bool const filter_on = params.BoolValue(layer_index, LayerParamIndex::FilterOn);
    bool const greyed_out = !filter_on;

    auto const section = DoSectionContainer(g, parent, 230);

    // Heading row
    auto const heading_row = DoBox(g.builder,
                                   {
                                       .parent = section,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.b = 6},
                                           .contents_gap = 10,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    DoSectionHeading(g.builder, heading_row, "FILTER"_s);

    DoButtonParameter(g, heading_row, params.DescribedValue(layer_index, LayerParamIndex::FilterOn), {});

    DoMenuParameter(g,
                    heading_row,
                    params.DescribedValue(layer_index, LayerParamIndex::FilterType),
                    {.width = 90, .greyed_out = greyed_out, .label = false});

    // Knobs row
    auto const knobs_row = DoBox(g.builder,
                                 {
                                     .parent = section,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 8,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    DoKnobParameter(g,
                    knobs_row,
                    params.DescribedValue(layer_index, LayerParamIndex::FilterCutoff),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .style_system = GuiStyleSystem::MidPanel,
                        .greyed_out = greyed_out,
                    });
    DoKnobParameter(g,
                    knobs_row,
                    params.DescribedValue(layer_index, LayerParamIndex::FilterResonance),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .style_system = GuiStyleSystem::MidPanel,
                        .greyed_out = greyed_out,
                    });
    DoKnobParameter(g,
                    knobs_row,
                    params.DescribedValue(layer_index, LayerParamIndex::FilterEnvAmount),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .style_system = GuiStyleSystem::MidPanel,
                        .greyed_out = greyed_out,
                        .bidirectional = true,
                    });

    // Filter envelope
    auto const envelope_box = DoBox(g.builder,
                                    {
                                        .parent = section,
                                        .layout {
                                            .size = {layout::k_fill_parent, 90},
                                            .margins = {.t = 4},
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

static void DoLfoSection(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::LfoOn);

    auto const section = DoSectionContainer(g, parent, 200);

    // Heading
    auto const heading_row = DoBox(g.builder,
                                   {
                                       .parent = section,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.b = 6},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Justify,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    DoSectionHeading(g.builder, heading_row, "LFO"_s);

    DoButtonParameter(g, heading_row, params.DescribedValue(layer_index, LayerParamIndex::LfoOn), {});

    // Menus: Destination, Shape, Restart
    auto const menus_col = DoBox(g.builder,
                                 {
                                     .parent = section,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 3,
                                         .contents_direction = layout::Direction::Column,
                                     },
                                 });

    auto const do_menu_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);

        auto const row = DoBox(g.builder,
                               {
                                   .parent = menus_col,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = 8,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               },
                               loc_hash);

        DoMenuParameter(g, row, param, {.width = 100, .greyed_out = greyed_out, .label = false});

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {.size = layout::k_fill_parent},
              },
              loc_hash);
    };

    do_menu_label_row(LayerParamIndex::LfoDestination);
    do_menu_label_row(LayerParamIndex::LfoShape);
    do_menu_label_row(LayerParamIndex::LfoRestart);

    // Knobs: Amount + Rate
    auto const knobs_row = DoBox(g.builder,
                                 {
                                     .parent = section,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .margins = {.t = 8},
                                         .contents_gap = 15,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                     },
                                 });

    DoKnobParameter(g,
                    knobs_row,
                    params.DescribedValue(layer_index, LayerParamIndex::LfoAmount),
                    {
                        .width = LiveWw(UiSizeId::KnobLargeW),
                        .style_system = GuiStyleSystem::MidPanel,
                        .greyed_out = greyed_out,
                        .bidirectional = true,
                    });

    auto const rate_col = DoBox(g.builder,
                                {
                                    .parent = knobs_row,
                                    .layout {
                                        .size = layout::k_hug_contents,
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
                            .width = LiveWw(UiSizeId::KnobLargeW),
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
    }

    DoButtonParameter(g,
                      rate_col,
                      params.DescribedValue(layer_index, LayerParamIndex::LfoSyncSwitch),
                      {.width = 0});
}

static void DoEqSection(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;
    bool const greyed_out = !params.BoolValue(layer_index, LayerParamIndex::EqOn);

    auto const section = DoSectionContainer(g, parent, 200);

    // Heading
    auto const heading_row = DoBox(g.builder,
                                   {
                                       .parent = section,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.b = 6},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Justify,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    DoSectionHeading(g.builder, heading_row, "EQ"_s);

    DoButtonParameter(g, heading_row, params.DescribedValue(layer_index, LayerParamIndex::EqOn), {});

    // EQ bands
    auto const do_eq_band = [&](LayerParamIndex type_param,
                                LayerParamIndex freq_param,
                                LayerParamIndex reso_param,
                                LayerParamIndex gain_param,
                                u64 id_extra) {
        DoMenuParameter(g,
                        section,
                        params.DescribedValue(layer_index, type_param),
                        {.width = layout::k_fill_parent, .greyed_out = greyed_out, .label = false});

        auto const knobs_row = DoBox(g.builder,
                                     {
                                         .parent = section,
                                         .id_extra = id_extra,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = 4,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Middle,
                                         },
                                     });

        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, freq_param),
                        {
                            .width = LiveWw(UiSizeId::ParamComponentExtraSmallWidth),
                            .knob_height_fraction = 0.96f,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, reso_param),
                        {
                            .width = LiveWw(UiSizeId::ParamComponentExtraSmallWidth),
                            .knob_height_fraction = 0.96f,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                        });
        DoKnobParameter(g,
                        knobs_row,
                        params.DescribedValue(layer_index, gain_param),
                        {
                            .width = LiveWw(UiSizeId::ParamComponentExtraSmallWidth),
                            .knob_height_fraction = 0.96f,
                            .style_system = GuiStyleSystem::MidPanel,
                            .greyed_out = greyed_out,
                            .bidirectional = true,
                        });
    };

    do_eq_band(LayerParamIndex::EqType1,
               LayerParamIndex::EqFreq1,
               LayerParamIndex::EqResonance1,
               LayerParamIndex::EqGain1,
               1);

    // Small gap between bands
    DoBox(g.builder,
          {
              .parent = section,
              .layout {.size = {1, 4}},
          });

    do_eq_band(LayerParamIndex::EqType2,
               LayerParamIndex::EqFreq2,
               LayerParamIndex::EqResonance2,
               LayerParamIndex::EqGain2,
               2);
}

static void DoPlaySection(GuiState& g, u8 layer_index, Box parent) {
    auto& params = g.engine.processor.main_params;

    auto const section = DoSectionContainer(g, parent, 200);

    DoSectionHeading(g.builder, section, "PLAY"_s);

    auto const row_height = 18.0f;
    auto const item_gap = 4.0f;
    auto const label_gap = 8.0f;
    auto const control_w = 64.0f;

    auto const container1 = DoBox(g.builder,
                                  {
                                      .parent = section,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_gap = 8,
                                          .contents_direction = layout::Direction::Column,
                                      },
                                  });

    // Helper for int dragger + label row
    auto const do_int_label_row = [&](LayerParamIndex param_index, u64 loc_hash = SourceLocationHash()) {
        auto const param = params.DescribedValue(layer_index, param_index);
        auto const row = DoBox(g.builder,
                               {
                                   .parent = container1,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_gap = label_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               },
                               loc_hash);

        DoIntParameter(g, row, param, {.width = control_w, .label = false});

        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {.size = layout::k_fill_parent},
              },
              loc_hash);
    };

    do_int_label_row(LayerParamIndex::MidiTranspose);
    do_int_label_row(LayerParamIndex::PitchBendRange);

    // Monophonic mode
    {
        auto const param = params.DescribedValue(layer_index, LayerParamIndex::MonophonicMode);
        auto const row = DoBox(g.builder,
                               {
                                   .parent = container1,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_gap = label_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });
        DoMenuParameter(g, row, param, {.width = control_w, .label = false});
        DoBox(g.builder,
              {
                  .parent = row,
                  .text = param.info.gui_label,
                  .text_colours = LiveColStruct(UiColMap::MidText),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {.size = layout::k_fill_parent},
              });
    }

    // Key Range
    {
        auto const row = DoBox(g.builder,
                               {
                                   .parent = container1,
                                   .layout {
                                       .size = {layout::k_fill_parent, row_height},
                                       .contents_gap = item_gap,
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
                  .layout {.size = layout::k_fill_parent},
              });

        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeLow),
                       {.width = 68, .midi_note_names = true, .label = false});
        DoIntParameter(g,
                       row,
                       params.DescribedValue(layer_index, LayerParamIndex::KeyRangeHigh),
                       {.width = 68, .midi_note_names = true, .label = false});
    }

    // Velocity curve
    {
        auto& layer = g.engine.Layer(layer_index);

        DoBox(g.builder,
              {
                  .parent = section,
                  .text = "Velocity curve"_s,
                  .text_colours = LiveColStruct(UiColMap::MidTextDimmed),
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .size = {layout::k_fill_parent, row_height},
                      .margins = {.t = 4},
                  },
              });

        auto const velo_box = DoBox(g.builder,
                                    {
                                        .parent = section,
                                        .layout {
                                            .size = {layout::k_fill_parent, 60},
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

static void VerticalDivider(GuiState& g, Box parent, u64 loc_hash = SourceLocationHash()) {
    auto const box = DoBox(g.builder,
                           {
                               .parent = parent,
                               .id_extra = loc_hash,
                               .layout {
                                   .size = {1, layout::k_fill_parent},
                                   .margins = {.lr = 3},
                               },
                           });
    if (auto const viewport_r = BoxRect(g.builder, box)) {
        auto r = g.imgui.ViewportRectToWindowRect(*viewport_r);
        r.x = Round(r.x);
        g.imgui.draw_list->AddLine(r.TopLeft(), r.BottomLeft(), ToU32(Col {.c = Col::White, .alpha = 30}));
    }
}

// =================================================================================================
// Main entry point

void MidPanelSingleLayerContent(GuiBuilder& builder,
                                GuiState& g,
                                GuiFrameContext const& frame_context,
                                u8 layer_index,
                                Box parent) {
    {
        auto& layer = g.engine.Layer(layer_index);

        auto const root = DoBox(builder,
                                {
                                    .parent = parent,
                                    .layout {
                                        .size = layout::k_fill_parent,
                                        .contents_padding = {.lr = 12, .tb = 10},
                                        .contents_gap = 8,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Middle,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                    },
                                });

        // === TOP ROW: Instrument selector + Mute/Solo ===
        auto const top_row = DoBox(builder,
                                   {
                                       .parent = root,
                                       .layout {
                                           .size = {300, layout::k_hug_contents},
                                           .contents_gap = 8,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

        DoLayerInstSelector(g, frame_context, layer_index, top_row);
        DoMuteSoloCompact(g, layer_index, top_row);

        bool const has_instrument = layer.instrument.tag != InstrumentType::None;

        // === LARGE WAVEFORM + INFO STRIP ===
        auto const waveform_container = DoBox(builder,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                      .contents_direction = layout::Direction::Column,
                                                  },
                                              });

        if (auto const r = BoxRect(builder, waveform_container))
            DrawBlurredBackgroundForBox(g, waveform_container, layer.LibId());

        auto const waveform_box = DoBox(builder,
                                        {
                                            .parent = waveform_container,
                                            .layout {
                                                .size = {layout::k_fill_parent, 120},
                                            },
                                        });

        if (auto const r = BoxRect(builder, waveform_box)) {
            if (has_instrument) {
                auto const engine_type = g.engine.processor.main_params.IntValue<param_values::EngineType>(
                    layer_index,
                    LayerParamIndex::EngineType);
                DoWaveformElement(g, layer, *r, {.handles_follow_cursor = true, .engine_type = engine_type});
            }
        }

        if (has_instrument) DoInstrumentInfoStrip(g, layer_index, waveform_container);

        // === PARAMETER SECTIONS ROW 1: Mixer | Vol Env | Filter ===
        auto const params_row1 = DoBox(builder,
                                       {
                                           .parent = root,
                                           .layout {
                                               .size = layout::k_hug_contents,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                       });
        DrawBlurredBackgroundForBox(g, params_row1, g.engine.Layer(layer_index).LibId());

        DoEngineSection(g, layer_index, params_row1);
        VerticalDivider(g, params_row1);
        DoMixerSection(g, layer_index, params_row1);
        VerticalDivider(g, params_row1);
        DoVolEnvSection(g, layer_index, params_row1);

        // === PARAMETER SECTIONS ROW 2: LFO | EQ | Play ===
        auto const params_row2 = DoBox(builder,
                                       {
                                           .parent = root,
                                           .layout {
                                               .size = layout::k_hug_contents,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                       });

        DrawBlurredBackgroundForBox(g, params_row2, g.engine.Layer(layer_index).LibId());

        DoFilterSection(g, layer_index, params_row2);
        VerticalDivider(g, params_row2);
        DoLfoSection(g, layer_index, params_row2);
        VerticalDivider(g, params_row2);
        DoEqSection(g, layer_index, params_row2);
        VerticalDivider(g, params_row2);
        DoPlaySection(g, layer_index, params_row2);
    }
}
