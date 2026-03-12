// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_perform.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

struct FXColours {
    UiColMap back;
    UiColMap highlight;
    UiColMap button;
};

static FXColours GetFxColMap(EffectType type) {
    using enum UiColMap;
    switch (type) {
        case EffectType::Distortion: return {DistortionBack, DistortionHighlight, DistortionButton};
        case EffectType::BitCrush: return {BitCrushBack, BitCrushHighlight, BitCrushButton};
        case EffectType::Compressor: return {CompressorBack, CompressorHighlight, CompressorButton};
        case EffectType::FilterEffect: return {FilterBack, FilterHighlight, FilterButton};
        case EffectType::StereoWiden: return {StereoBack, StereoHighlight, StereoButton};
        case EffectType::Chorus: return {ChorusBack, ChorusHighlight, ChorusButton};
        case EffectType::Reverb: return {ReverbBack, ReverbHighlight, ReverbButton};
        case EffectType::Delay: return {DelayBack, DelayHighlight, DelayButton};
        case EffectType::ConvolutionReverb: return {ConvolutionBack, ConvolutionHighlight, ConvolutionButton};
        case EffectType::Phaser: return {PhaserBack, PhaserHighlight, PhaserButton};
        case EffectType::Count: PanicIfReached();
    }
    return {};
}

static Colours MidIconButtonColours(bool greyed_out) {
    if (greyed_out)
        return ColSet {
            .base = LiveColStruct(UiColMap::MidIconDimmed),
        };
    return ColSet {
        .base = LiveColStruct(UiColMap::MidIcon),
        .hot = LiveColStruct(UiColMap::MidTextHot),
        .active = LiveColStruct(UiColMap::MidTextOn),
    };
}

static void DoSectionLabel(GuiBuilder& builder, Box parent, String text) {
    DoBox(builder,
          {
              .parent = parent,
              .text = text,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::White, .alpha = 90},
          });
}

static void DoPresetInfo(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const& snapshot = g.engine.last_snapshot;
    auto const& metadata = snapshot.state.metadata;

    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_padding = {.l = 20, .r = 20, .t = 12},
                                         .contents_gap = 5,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

    // Preset name
    {
        auto name = snapshot.name_or_path.Name();
        if (name.size) {
            DoBox(builder,
                  {
                      .parent = container,
                      .text = name,
                      .size_from_text = true,
                      .font = FontType::Heading1,
                      .text_colours = Col {.c = Col::White},
                  });
        }
    }

    // Description
    if (metadata.description.size) {
        DoBox(builder,
              {
                  .parent = container,
                  .text = metadata.description,
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 180},
              });
    }

    // Tags row
    if (metadata.tags.size) {
        auto const tags_row = DoBox(builder,
                                    {
                                        .parent = container,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .margins = {.t = 4},
                                            .contents_gap = 5,
                                            .contents_direction = layout::Direction::Row,
                                            .contents_multiline = true,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });

        for (auto const [tag_index, tag] : Enumerate(metadata.tags)) {
            auto const pill = DoBox(builder,
                                    {
                                        .parent = tags_row,
                                        .id_extra = tag_index,
                                        .background_fill_colours = Col {.c = Col::Black, .alpha = 130},
                                        .round_background_corners = 0b1111,
                                        .corner_rounding = 10.0f,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_hug_contents},
                                            .contents_padding = {.lr = 8, .tb = 2},
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });
            DoBox(builder,
                  {
                      .parent = pill,
                      .text = tag,
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::White, .alpha = 180},
                  });
        }
    }
}

static void DoLayersColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_waveform_height = 48;
    constexpr f32 k_meter_width = 10;
    constexpr f32 k_vol_slider_width = 12;
    constexpr f32 k_layers_column_width = 350;

    auto& params = g.engine.processor.main_params;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b1010, // left and right
                                  .layout {
                                      .size = {k_layers_column_width, layout::k_hug_contents},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 8,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    // Waveform entries
    bool any_active = false;
    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto& layer = g.engine.processor.layer_processors[layer_index];
        if (layer.instrument.tag == InstrumentType::None) continue;
        any_active = true;

        auto const entry = DoBox(builder,
                                 {
                                     .parent = column,
                                     .id_extra = layer_index,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 3,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

        // Label row
        {
            auto const inst_name = layer.InstName();
            auto const label_text = fmt::Format(g.scratch_arena, "Layer {}", layer_index + 1);

            auto const label_row = DoBox(builder,
                                         {
                                             .parent = entry,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_padding = {.l = 2},
                                                 .contents_gap = 6,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                             },
                                         });

            DoBox(builder,
                  {
                      .parent = label_row,
                      .text = label_text,
                      .size_from_text = true,
                      .font = FontType::Heading3,
                      .text_colours = Col {.c = Col::White, .alpha = 100},
                  });

            if (inst_name.size) {
                DoBox(builder,
                      {
                          .parent = label_row,
                          .text = inst_name,
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = Col {.c = Col::White, .alpha = 200},
                          .text_overflow = TextOverflowType::ShowDotsOnRight,
                      });
            }
        }

        auto const controls_row = DoBox(builder,
                                        {
                                            .parent = entry,
                                            .layout {
                                                .size = {layout::k_fill_parent, k_waveform_height},
                                                .contents_gap = 4,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                            },
                                        });

        // Waveform
        {
            auto const waveform_box = DoBox(builder,
                                            {
                                                .parent = controls_row,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                },
                                            });
            if (auto const r = BoxRect(builder, waveform_box)) DoWaveformElement(g, layer, *r);
        }

        // Peak meter
        {
            auto const meter_box = DoBox(builder,
                                         {
                                             .parent = controls_row,
                                             .layout {
                                                 .size = {k_meter_width, layout::k_fill_parent},
                                             },
                                         });
            if (auto const r = BoxRect(builder, meter_box))
                DrawPeakMeter(g.imgui,
                              g.imgui.ViewportRectToWindowRect(*r),
                              layer.peak_meter,
                              {
                                  .flash_when_clipping = false,
                                  .show_db_markers = false,
                                  .gap = 1,
                              });
        }

        // Volume slider
        DoKnobParameter(g,
                        controls_row,
                        params.DescribedValue(layer_index, LayerParamIndex::Volume),
                        {
                            .width = k_vol_slider_width,
                            .knob_height_fraction = k_waveform_height / k_vol_slider_width,
                            .style_system = GuiStyleSystem::MidPanel,
                            .label = false,
                            .vertical_slider = true,
                        });

        DoMuteSoloButtons(g,
                          controls_row,
                          params.DescribedValue(layer_index, LayerParamIndex::Mute),
                          params.DescribedValue(layer_index, LayerParamIndex::Solo),
                          {.vertical = true});
    }

    if (!any_active) {
        DoBox(builder,
              {
                  .parent = column,
                  .text = "No instruments loaded"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 80},
              });
    }
}

static void DoEffectsColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_effects_column_width = 120;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {k_effects_column_width, layout::k_hug_contents},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 4,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    DoSectionLabel(builder, column, "Effects"_s);

    auto const& params = g.engine.processor.main_params;
    auto const ordered_effects =
        DecodeEffectsArray(g.engine.processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           g.engine.processor.effects_ordered_by_type);

    bool any_on = false;
    for (auto const fx : ordered_effects) {
        if (!EffectIsOn(params, fx)) continue;
        any_on = true;
        auto const cols = GetFxColMap(fx->type);

        auto const pill = DoBox(builder,
                                {
                                    .parent = column,
                                    .id_extra = (u64)fx->type,
                                    .background_fill_colours = LiveColStruct(cols.back),
                                    .round_background_corners = 0b1111,
                                    .corner_rounding = 6.0f,
                                    .layout {
                                        .size = {layout::k_hug_contents, layout::k_hug_contents},
                                        .contents_padding = {.lr = 6, .tb = 2},
                                    },
                                    .tooltip = k_effect_info[ToInt(fx->type)].name,
                                });
        DoBox(builder,
              {
                  .parent = pill,
                  .text = k_effect_info[ToInt(fx->type)].name,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 180},
              });
    }

    if (!any_on) {
        DoBox(builder,
              {
                  .parent = column,
                  .text = "None"_s,
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = Col {.c = Col::White, .alpha = 60},
              });
    }
}

static bool HasAnyActiveMacros(GuiState& g) {
    for (auto const [macro_index, param_index] : Enumerate(k_macro_params))
        if (g.engine.processor.main_macro_destinations[macro_index].Size() != 0) return true;
    return false;
}

static void DoMacrosRow(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .layout {
                                   .size = {layout::k_hug_contents, layout::k_hug_contents},
                                   .contents_padding = {.lr = 24, .tb = 8},
                                   .contents_gap = 40,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    // DoSectionLabel(builder, row, "Macros"_s);

    for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
        if (g.engine.processor.main_macro_destinations[macro_index].Size() == 0) continue;
        DoKnobParameter(g,
                        row,
                        g.engine.processor.main_params.DescribedValue(param_index),
                        {
                            .width = 34,
                            .override_label = g.engine.macro_names[macro_index],
                        });
    }
}

static Box
DoActionButton(GuiBuilder& builder, Box parent, String icon, String label, String tooltip, u64 id_extra) {
    auto const btn = DoBox(builder,
                           {
                               .parent = parent,
                               .id_extra = id_extra,
                               .background_fill_colours =
                                   ColSet {
                                       .base = Col {.c = Col::White, .alpha = 12},
                                       .hot = Col {.c = Col::White, .alpha = 25},
                                       .active = Col {.c = Col::White, .alpha = 35},
                                   },
                               .round_background_corners = 0b1111,
                               .corner_rounding = k_corner_rounding,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_padding = {.lr = 10, .tb = 7},
                                   .contents_gap = 7,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .tooltip = tooltip,
                               .button_behaviour = imgui::ButtonConfig {},
                           });

    DoBox(builder,
          {
              .parent = btn,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .text_colours = MidIconButtonColours(false),
              .parent_dictates_hot_and_active = true,
          });

    DoBox(builder,
          {
              .parent = btn,
              .text = label,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = MidIconButtonColours(false),
              .parent_dictates_hot_and_active = true,
          });

    return btn;
}

static void
DoActionsColumn(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {layout::k_hug_contents, layout::k_hug_contents},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 6,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    DoSectionLabel(builder, column, "Randomise"_s);

    // Shuffle instruments (same folder)
    {
        auto const btn = DoActionButton(
            builder,
            column,
            ICON_FA_SHUFFLE ""_s,
            "Instruments (Folder)"_s,
            "Load random instruments from the same folder as each layer's current instrument"_s,
            4);
        if (btn.button_fired) {
            for (auto& layer : g.engine.processor.layer_processors) {
                if (layer.instrument.tag == InstrumentType::None) continue;

                auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>();
                if (!sampled_inst) continue;

                auto const& inst = (*sampled_inst)->instrument;
                if (!inst.folder) continue;

                // Create an ephemeral browser state filtered to only this folder
                InstBrowserState ephemeral_state {.id = HashFnv1a("ephemeral-inst-browser")};
                auto const folder_name =
                    inst.folder->display_name.size ? inst.folder->display_name : inst.folder->name;
                ephemeral_state.common_state.selected_folder_hashes.Add(inst.folder->Hash(), folder_name);

                InstBrowserContext context {
                    .layer = layer,
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .library_images = g.library_images,
                    .engine = g.engine,
                    .prefs = g.prefs,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                LoadRandomInstrument(context, ephemeral_state);
            }
        }
    }

    // Shuffle instruments (same library)
    {
        auto const btn = DoActionButton(
            builder,
            column,
            ICON_FA_SHUFFLE ""_s,
            "Instruments (Library)"_s,
            "Load random instruments from the same library as each layer's current instrument"_s,
            3);
        if (btn.button_fired) {
            for (auto& layer : g.engine.processor.layer_processors) {
                if (layer.instrument.tag == InstrumentType::None) continue;

                auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>();
                if (!sampled_inst) continue;

                auto const& library = (*sampled_inst)->instrument.library;

                // Create an ephemeral browser state filtered to only this library
                InstBrowserState ephemeral_state {.id = HashFnv1a("ephemeral-inst-browser")};
                ephemeral_state.common_state.selected_library_hashes.Add(Hash(library.id), library.name);

                InstBrowserContext context {
                    .layer = layer,
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .library_images = g.library_images,
                    .engine = g.engine,
                    .prefs = g.prefs,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                LoadRandomInstrument(context, ephemeral_state);
            }
        }
    }

    // Shuffle instruments
    {
        auto const btn = DoActionButton(builder,
                                        column,
                                        ICON_FA_SHUFFLE ""_s,
                                        "Instruments (Any)"_s,
                                        "Load random instruments for all layers"_s,
                                        1);
        if (btn.button_fired) {
            for (auto& layer : g.engine.processor.layer_processors) {
                InstBrowserState ephemeral_state {.id = HashFnv1a("ephemeral-inst-browser")};
                InstBrowserContext context {
                    .layer = layer,
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .library_images = g.library_images,
                    .engine = g.engine,
                    .prefs = g.prefs,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                LoadRandomInstrument(context, ephemeral_state);
            }
        }
    }

    // Shuffle effects
    {
        auto const btn = DoActionButton(builder,
                                        column,
                                        ICON_FA_SHUFFLE ""_s,
                                        "Effects (Chaos)"_s,
                                        "Randomise all effects"_s,
                                        2);
        if (btn.button_fired) {
            RandomiseAllEffectParameterValues(g.engine.processor);
            IrBrowserContext ir_context {
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadRandomIr(ir_context, g.ir_browser_state);
        }
    }
}

void MidPanelPerformContent(GuiBuilder& builder,
                            GuiState& g,
                            GuiFrameContext const& frame_context,
                            Box parent,
                            Box) {
    // Root fills the entire mid panel area
    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lr = 100, .t = 35, .b = 29},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // Preset info at the top
    DoPresetInfo(builder, g, root);

    // Spacer pushes the central panel to the bottom
    DoBox(builder,
          {
              .parent = root,
              .layout {
                  .size = {0, layout::k_fill_parent},
              },
          });

    if (HasAnyActiveMacros(g)) {
        auto const macros_panel = DoBox(builder,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                .margins = {.b = 23},
                                            },
                                        });

        if (auto const r = BoxRect(builder, macros_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoMacrosRow(builder, g, macros_panel);
    }

    {
        auto const central_panel = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

        if (auto const r = BoxRect(builder, central_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoActionsColumn(builder, g, frame_context, central_panel);

        // Layers (waveforms)
        DoLayersColumn(builder, g, central_panel);

        // Effects
        DoEffectsColumn(builder, g, central_panel);
    }
}
