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

// =====================================================================================
// Effect colour mapping
// =====================================================================================

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

// =====================================================================================
// Helpers
// =====================================================================================

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
              .font = FontType::Heading3,
              .text_colours = Col {.c = Col::White, .alpha = 90},
          });
}

static void DoHorizontalDivider(GuiBuilder& builder, Box parent) {
    DoBox(builder,
          {
              .parent = parent,
              .background_fill_colours = Col {.c = Col::White, .alpha = 20},
              .layout {
                  .size = {layout::k_fill_parent, 1},
              },
          });
}

// =====================================================================================
// Preset Identity: name, tags, and description
// =====================================================================================

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

    // Tags row
    if (metadata.tags.size) {
        auto const tags_row = DoBox(builder,
                                    {
                                        .parent = container,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
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
                      .font = FontType::Heading3,
                      .text_colours = Col {.c = Col::White},
                  });
        }
    }

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
}

// =====================================================================================
// Layers column: waveforms with labels, peak meters, and effects strip
// =====================================================================================

static void DoLayersColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_waveform_height = 48;
    constexpr f32 k_meter_width = 10;
    constexpr f32 k_layers_column_width = 350;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
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
                          .font = FontType::Heading3,
                          .text_colours = Col {.c = Col::White, .alpha = 200},
                          .text_overflow = TextOverflowType::ShowDotsOnRight,
                      });
            }
        }

        // Waveform + peak meter row
        auto const wave_row = DoBox(builder,
                                    {
                                        .parent = entry,
                                        .layout {
                                            .size = {layout::k_fill_parent, k_waveform_height},
                                            .contents_gap = 4,
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

        {
            auto const waveform_box = DoBox(builder,
                                            {
                                                .parent = wave_row,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                },
                                            });
            if (auto const r = BoxRect(builder, waveform_box)) DoWaveformElement(g, layer, *r);
        }

        {
            auto const meter_box = DoBox(builder,
                                         {
                                             .parent = wave_row,
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

    // Effects strip
    DoHorizontalDivider(builder, column);

    {
        auto const& params = g.engine.processor.main_params;

        auto const strip = DoBox(builder,
                                 {
                                     .parent = column,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 4,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_multiline = true,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });

        DoBox(builder,
              {
                  .parent = strip,
                  .text = "FX"_s,
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = Col {.c = Col::White, .alpha = 100},
              });

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
                                        .parent = strip,
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
                      .font = FontType::Heading3,
                      .text_colours = LiveColStruct(cols.highlight),
                  });
        }

        if (!any_on) {
            DoBox(builder,
                  {
                      .parent = strip,
                      .text = "None"_s,
                      .size_from_text = true,
                      .font = FontType::Heading3,
                      .text_colours = Col {.c = Col::White, .alpha = 60},
                  });
        }
    }
}

// =====================================================================================
// Macros column: knobs stacked vertically
// =====================================================================================

static void DoMacrosColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b1000, // left only
                                  .layout {
                                      .size = {layout::k_hug_contents, layout::k_hug_contents},
                                      .contents_padding = {.lr = 12, .tb = 10},
                                      .contents_gap = 8,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                              });

    DoSectionLabel(builder, column, "Macros"_s);

    bool any_active = false;
    for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
        if (g.engine.processor.main_macro_destinations[macro_index].Size() == 0) continue;
        any_active = true;
        DoKnobParameter(g,
                        column,
                        g.engine.processor.main_params.DescribedValue(param_index),
                        {
                            .width = k_small_knob_width,
                            .override_label = g.engine.macro_names[macro_index],
                        });
    }

    if (!any_active) {
        DoBox(builder,
              {
                  .parent = column,
                  .text = "None assigned"_s,
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = Col {.c = Col::White, .alpha = 60},
              });
    }
}

// =====================================================================================
// Actions column: randomise buttons stacked vertically
// =====================================================================================

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
                                   .contents_align = layout::Alignment::Middle,
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
              .font = FontType::Heading3,
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
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b1000, // left only
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

    // Shuffle instruments
    {
        auto const btn = DoActionButton(builder,
                                        column,
                                        ICON_FA_SHUFFLE ""_s,
                                        "Instruments"_s,
                                        "Load random instruments for all layers"_s,
                                        1);
        if (btn.button_fired) {
            for (auto& layer : g.engine.processor.layer_processors) {
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
                LoadRandomInstrument(context, g.inst_browser_state[layer.index]);
            }
        }
    }

    // Shuffle effects
    {
        auto const btn =
            DoActionButton(builder, column, ICON_FA_SHUFFLE ""_s, "Effects"_s, "Randomise all effects"_s, 2);
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

    // Randomise everything
    {
        auto const btn = DoActionButton(builder,
                                        column,
                                        ICON_FA_DICE ""_s,
                                        "Everything"_s,
                                        "Randomise all parameters, instruments, and effects"_s,
                                        3);
        if (btn.button_fired) {
            RandomiseAllParameterValues(g.engine.processor);
            for (auto& layer : g.engine.processor.layer_processors) {
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
                LoadRandomInstrument(context, g.inst_browser_state[layer.index]);
            }
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

// =====================================================================================
// Main layout
// =====================================================================================

void MidPanelPerformContent(GuiBuilder& builder,
                            GuiState& g,
                            GuiFrameContext const& frame_context,
                            Box parent,
                            Box) {
    // Root fills the entire mid panel area, centres content vertically and horizontally
    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lr = 16, .tb = 8},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Middle,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // Preset info floats above the main panel
    DoPresetInfo(builder, g, root);

    // Central blurred panel containing three columns
    {
        auto const central_panel = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                 .margins = {.t = 8},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

        if (auto const r = BoxRect(builder, central_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        // Column 1: Layers (waveforms + effects)
        DoLayersColumn(builder, g, central_panel);

        // Column 2: Macros (conditional)
        {
            bool has_any_macros = false;
            for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
                if (g.engine.processor.main_macro_destinations[macro_index].Size() != 0) {
                    has_any_macros = true;
                    break;
                }
            }

            if (has_any_macros) DoMacrosColumn(builder, g, central_panel);
        }

        // Column 3: Actions
        DoActionsColumn(builder, g, frame_context, central_panel);
    }
}
