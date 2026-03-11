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
#include "gui_framework/gui_live_edit.hpp"
#include "processor/layer_processor.hpp"

// Draw the preset metadata (tags, name, description) overlaid on the background image area.
static void DoPresetInfoOverlay(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const& snapshot = g.engine.last_snapshot;
    auto const& metadata = snapshot.state.metadata;

    auto const info_container = DoBox(builder,
                                      {
                                          .parent = parent,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_padding = {.l = 24, .r = 24, .t = 20, .b = 0},
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
                                        .parent = info_container,
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

    // Preset name - large heading
    {
        auto name = snapshot.name_or_path.Name();
        if (name.size) {
            DoBox(builder,
                  {
                      .parent = info_container,
                      .text = name,
                      .size_from_text = true,
                      .font = FontType::Heading1,
                      .text_colours = Col {.c = Col::White},
                  });
        }
    }

    // Description - body text, slightly subdued
    if (metadata.description.size) {
        DoBox(builder,
              {
                  .parent = info_container,
                  .text = metadata.description,
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 180},
              });
    }
}

// Draw small waveform previews for each active layer in a horizontal row.
static void DoLayerWaveformPreviews(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_waveform_height = 48;

    auto const waveforms_row = DoBox(builder,
                                     {
                                         .parent = parent,
                                         .layout {
                                             .size = {layout::k_fill_parent, k_waveform_height},
                                             .contents_padding = {.lr = 24},
                                             .contents_gap = 8,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                         },
                                     });

    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto& layer = g.engine.processor.layer_processors[layer_index];
        if (layer.instrument.tag == InstrumentType::None) continue;

        auto const waveform_box = DoBox(builder,
                                        {
                                            .parent = waveforms_row,
                                            .id_extra = layer_index,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_fill_parent},
                                            },
                                        });
        if (auto const r = BoxRect(builder, waveform_box)) DoWaveformElement(g, layer, *r);
    }
}

// Draw the macro knobs inside the bottom blurred panel. Only shows macros that have destinations.
static void DoPerformMacroKnobs(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const knobs_row = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {layout::k_hug_contents, layout::k_fill_parent},
                                         .contents_padding = {.lr = 16, .tb = 6},
                                         .contents_gap = 32,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Middle,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });

    bool any_active = false;
    for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
        if (g.engine.processor.main_macro_destinations[macro_index].Size() == 0) continue;
        any_active = true;
        DoKnobParameter(g,
                        knobs_row,
                        g.engine.processor.main_params.DescribedValue(param_index),
                        {
                            .width = k_small_knob_width,
                            .override_label = g.engine.macro_names[macro_index],
                        });
    }

    if (!any_active) {
        DoBox(builder,
              {
                  .parent = knobs_row,
                  .text = "No macros assigned"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 100},
              });
    }
}

void MidPanelPerformContent(GuiBuilder& builder, GuiState& g, GuiFrameContext const&, Box parent, Box) {
    // The perform panel fills the entire content area. The background image is drawn by the
    // mid panel's viewport background callback, so we just overlay our content on top.
    auto const root = DoBox(builder,
                            {
                                .parent = parent,
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // Upper area: preset info text overlaid directly on the background image.
    // This fills available space, pushing the bottom panel down.
    auto const upper = DoBox(builder,
                             {
                                 .parent = root,
                                 .layout {
                                     .size = layout::k_fill_parent,
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

    DoPresetInfoOverlay(builder, g, upper);

    // Spacer pushes waveforms toward the middle-lower area
    DoBox(builder,
          {
              .parent = upper,
              .layout {
                  .size = {layout::k_fill_parent, layout::k_fill_parent},
              },
          });

    // Layer waveform previews - positioned above the bottom panel
    DoLayerWaveformPreviews(builder, g, upper);

    DoBox(builder,
          {
              .parent = upper,
              .layout {
                  .size = {layout::k_fill_parent, 8},
              },
          });

    // Bottom blurred panel housing the active macro knobs.
    // Only show if there are any active macros.
    {
        bool has_any_macros = false;
        for (auto const [macro_index, param_index] : Enumerate(k_macro_params)) {
            if (g.engine.processor.main_macro_destinations[macro_index].Size() != 0) {
                has_any_macros = true;
                break;
            }
        }

        if (has_any_macros) {
            auto const bottom_panel =
                DoBox(builder,
                      {
                          .parent = root,
                          .layout {
                              .size = {layout::k_hug_contents, layout::k_hug_contents},
                              .margins = {.lr = 8, .b = 8},
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Middle,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            if (auto const r = BoxRect(builder, bottom_panel))
                DrawMidBlurredPanelSurface(g,
                                           builder.imgui.ViewportRectToWindowRect(*r),
                                           LibraryForOverallBackground(g.engine));

            DoPerformMacroKnobs(builder, g, bottom_panel);
        }
    }
}
