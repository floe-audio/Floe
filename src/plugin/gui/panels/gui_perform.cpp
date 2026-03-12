// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_perform.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui_framework/gui_builder.hpp"
#include "processor/layer_processor.hpp"

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
                                         .contents_gap = 6,
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
                                            .size = {layout::k_hug_contents, layout::k_hug_contents},
                                            .contents_gap = 6,
                                            .contents_direction = layout::Direction::Row,
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
// Sound Controls: macro knobs
// =====================================================================================

static void DoMacroKnobsContent(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const knobs_container = DoBox(builder,
                                       {
                                           .parent = parent,
                                           .layout {
                                               .size = {layout::k_hug_contents, layout::k_hug_contents},
                                               .contents_padding = {.lr = 16, .tb = 10},
                                               .contents_gap = 28,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Start,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                           },
                                       });

    bool any_active = false;
    for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
        if (g.engine.processor.main_macro_destinations[macro_index].Size() == 0) continue;
        any_active = true;
        DoKnobParameter(g,
                        knobs_container,
                        g.engine.processor.main_params.DescribedValue(param_index),
                        {
                            .width = k_small_knob_width,
                            .override_label = g.engine.macro_names[macro_index],
                        });
    }

    if (!any_active) {
        DoBox(builder,
              {
                  .parent = knobs_container,
                  .text = "No macros assigned"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 100},
              });
    }
}

// =====================================================================================
// Sound Visualisation: waveform displays in a vertical column
// =====================================================================================

static void DoWaveformColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                      .contents_padding = {.lrtb = 8},
                                      .contents_gap = 8,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    u8 num_active_layers = 0;
    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto& layer = g.engine.processor.layer_processors[layer_index];
        if (layer.instrument.tag != InstrumentType::None) num_active_layers++;
    }

    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto& layer = g.engine.processor.layer_processors[layer_index];
        if (layer.instrument.tag == InstrumentType::None) continue;

        // Each waveform entry: label + fixed-height waveform display
        constexpr f32 k_waveform_height = 48;

        auto const entry = DoBox(builder,
                                 {
                                     .parent = column,
                                     .id_extra = layer_index,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 4,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

        // Layer label: instrument name
        {
            auto const inst_name = layer.InstName();
            auto const label_text = fmt::Format(g.scratch_arena, "Layer {}", layer_index + 1);

            auto const label_row = DoBox(builder,
                                         {
                                             .parent = entry,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_padding = {.l = 4},
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
                      .text_colours = Col {.c = Col::White, .alpha = 130},
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

        // Waveform display
        auto const waveform_box = DoBox(builder,
                                        {
                                            .parent = entry,
                                            .layout {
                                                .size = {layout::k_fill_parent, k_waveform_height},
                                            },
                                        });
        if (auto const r = BoxRect(builder, waveform_box)) DoWaveformElement(g, layer, *r);
    }

    // Empty state when no layers are loaded
    if (num_active_layers == 0) {
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

// =====================================================================================
// Main layout
// =====================================================================================

void MidPanelPerformContent(GuiBuilder& builder, GuiState& g, GuiFrameContext const&, Box parent, Box) {
    // Root: two-column layout
    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = 8},
                                    .contents_gap = 8,
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // ---------------------------------------------------------------------------------
    // Left column: Preset Identity + Sound Controls
    // ---------------------------------------------------------------------------------
    auto const left_col = DoBox(builder,
                                {
                                    .parent = root,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_fill_parent},
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

    // Preset info panel (blurred background)
    {
        auto const info_panel = DoBox(builder,
                                      {
                                          .parent = left_col,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_padding = {.lr = 16, .tb = 14},
                                              .contents_direction = layout::Direction::Column,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                          },
                                      });

        if (auto const r = BoxRect(builder, info_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoPresetInfo(builder, g, info_panel);
    }

    // Spacer pushes macros to the bottom
    DoBox(builder,
          {
              .parent = left_col,
              .layout {
                  .size = {layout::k_fill_parent, layout::k_fill_parent},
              },
          });

    // Macros panel (blurred background)
    {
        bool has_any_macros = false;
        for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
            if (g.engine.processor.main_macro_destinations[macro_index].Size() != 0) {
                has_any_macros = true;
                break;
            }
        }

        if (has_any_macros) {
            auto const macros_panel = DoBox(builder,
                                            {
                                                .parent = left_col,
                                                .layout {
                                                    .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

            if (auto const r = BoxRect(builder, macros_panel))
                DrawMidBlurredPanelSurface(g,
                                           builder.imgui.ViewportRectToWindowRect(*r),
                                           LibraryForOverallBackground(g.engine));

            DoMacroKnobsContent(builder, g, macros_panel);
        }
    }

    // ---------------------------------------------------------------------------------
    // Right column: Waveform Visualisation
    // ---------------------------------------------------------------------------------
    {
        auto const waveform_panel = DoBox(builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              },
                                          });

        if (auto const r = BoxRect(builder, waveform_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoWaveformColumn(builder, g, waveform_panel);
    }
}
