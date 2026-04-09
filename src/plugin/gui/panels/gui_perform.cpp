// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_perform.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/auto_description.hpp"
#include "common_infrastructure/constants.hpp"
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
#include "gui/panels/gui_preset_browser.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "preset_server/preset_server.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

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

static void
DoSectionLabel(GuiBuilder& builder, Box parent, String text, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = loc_hash,
              .text = text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = Col {.c = Col::White, .alpha = 120},
          });
}

// Strips leading digit prefixes like "01 ", "02. ", or "01 - " used for ordering folders.
static String StripNumberedPrefix(String s) {
    usize pos = 0;
    while (pos < s.size && IsDigit(s[pos]))
        pos++;
    if (pos == 0) return s;
    if (pos < s.size && s[pos] == '.') pos++;
    if (pos + 1 < s.size && s[pos] == ' ' && s[pos + 1] == '-') pos += 2;
    if (pos < s.size && s[pos] == ' ') pos++;
    return s.SubSpan(pos);
}

static void DoPresetInfo(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const& snapshot = g.engine.last_snapshot;

    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {500, layout::k_hug_contents},
                                         .contents_gap = 5,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });

    // Library name
    {
        Optional<sample_lib::LibraryId> first_lib_id {};
        bool mixed = false;
        for (auto const& layer : g.engine.processor.layer_processors) {
            auto const lib_id = layer.LibId();
            if (!lib_id) continue;
            if (!first_lib_id) {
                first_lib_id = *lib_id;
            } else if (*lib_id != *first_lib_id) {
                mixed = true;
                break;
            }
        }

        String library_name {};
        if (mixed) {
            library_name = "Mixed Libraries"_s;
        } else if (first_lib_id) {
            if (auto const maybe_lib = frame_context.lib_table.Find(*first_lib_id))
                if (*maybe_lib) library_name = (*maybe_lib)->name;
        }

        if (library_name.size) {
            auto const lib_name_row =
                DoBox(builder,
                      {
                          .parent = container,
                          .layout {
                              .size = {layout::k_hug_contents, layout::k_hug_contents},
                              .contents_gap = 6,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Middle,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            if (!mixed && first_lib_id) {
                auto const imgs = GetLibraryImages(g.library_images,
                                                   g.imgui,
                                                   *first_lib_id,
                                                   g.shared_engine_systems.sample_library_server,
                                                   g.engine.instance_index,
                                                   LibraryImagesTypes::Icon);
                if (imgs.icon) {
                    DoBox(builder,
                          {
                              .parent = lib_name_row,
                              .background_tex = imgs.icon.NullableValue(),
                              .layout {
                                  .size = k_library_icon_standard_size,
                              },
                          });
                }
            }

            DoBox(builder,
                  {
                      .parent = lib_name_row,
                      .text = library_name,
                      .size_from_text = true,
                      .font = FontType::Heading1,
                      .text_colours = Col {.c = Col::White},
                      .text_justification = TextJustification::Centred,
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
                      .font = FontType::LargeTitle,
                      .text_colours = Col {.c = Col::White},
                      .text_justification = TextJustification::Centred,
                  });
        }
    }
}

static void DoLayersColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_meter_width = 10;
    constexpr f32 k_vol_slider_width = 12;
    constexpr f32 k_layers_column_width = 420;

    auto& params = g.engine.processor.main_params;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b0010, // right
                                  .layout {
                                      .size = {k_layers_column_width, layout::k_fill_parent},
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
                                         .size = {layout::k_fill_parent, layout::k_fill_parent},
                                         .contents_gap = 3,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

        // Label row
        {
            auto const inst_name = layer.InstName();
            auto const label_text = fmt::Format(g.scratch_arena, "LAYER {}", layer_index + 1);

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

                if (auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
                    auto const& inst = (*sampled_inst)->instrument;
                    if (inst.folder) {
                        auto const raw_folder_name =
                            inst.folder->display_name.size ? inst.folder->display_name : inst.folder->name;
                        auto const folder_name = StripNumberedPrefix(raw_folder_name);
                        DoBox(builder,
                              {
                                  .parent = label_row,
                                  .text = folder_name,
                                  .size_from_text = true,
                                  .font = FontType::Body,
                                  .text_colours = Col {.c = Col::White, .alpha = 120},
                                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                              });
                    }
                }
            }
        }

        auto const controls_row = DoBox(builder,
                                        {
                                            .parent = entry,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_fill_parent},
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
        DoVerticalSliderParameter(g,
                                  controls_row,
                                  params.DescribedValue(layer_index, LayerParamIndex::Volume),
                                  {
                                      .width = k_vol_slider_width,
                                      .height = layout::k_fill_parent,
                                      .style_system = GuiStyleSystem::MidPanel,
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

static Box DoIconButton(GuiBuilder& builder, Box parent, String icon, String tooltip, u64 id_extra) {
    constexpr f32 k_icon_btn_size = 28;
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
                                   .size = {k_icon_btn_size, k_icon_btn_size},
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
              .font_size = k_font_icons_size * 0.85f,
              .text_colours = MidIconButtonColours(false),
              .parent_dictates_hot_and_active = true,
          });

    return btn;
}

static void
DoActionsColumn(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    constexpr f32 k_heading_gap = 10;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {layout::k_hug_contents, layout::k_fill_parent},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 4,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    DoSectionLabel(builder, column, "RANDOMISE"_s);

    // Instruments randomise section
    {
        auto const section = DoBox(builder,
                                   {
                                       .parent = column,
                                       .layout {
                                           .size = {layout::k_hug_contents, layout::k_hug_contents},
                                           .margins = {.t = k_heading_gap},
                                           .contents_gap = 4,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

        DoSectionLabel(builder, section, "INSTRUMENTS"_s);

        auto const btn_row = DoBox(builder,
                                   {
                                       .parent = section,
                                       .layout {
                                           .size = {layout::k_hug_contents, layout::k_hug_contents},
                                           .contents_gap = 4,
                                           .contents_direction = layout::Direction::Row,
                                       },
                                   });

        // Shuffle instruments (same folder)
        {
            auto const btn = DoIconButton(builder,
                                          btn_row,
                                          ICON_FA_FOLDER ""_s,
                                          "Random instruments from same folder"_s,
                                          4);
            if (btn.button_fired) {
                for (auto& layer : g.engine.processor.layer_processors) {
                    if (layer.instrument.tag == InstrumentType::None) continue;

                    auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>();
                    if (!sampled_inst) continue;

                    auto const& inst = (*sampled_inst)->instrument;
                    if (!inst.folder) continue;

                    InstBrowserState ephemeral_state {.id = HashFnv1a("ephemeral-inst-browser")};
                    auto const folder_name =
                        inst.folder->display_name.size ? inst.folder->display_name : inst.folder->name;
                    ephemeral_state.common_state.Filter(BrowserFilter::Folder)
                        .Add(inst.folder->Hash(), folder_name);

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
            auto const btn = DoIconButton(builder,
                                          btn_row,
                                          ICON_FA_BOOK ""_s,
                                          "Random instruments from same library"_s,
                                          3);
            if (btn.button_fired) {
                for (auto& layer : g.engine.processor.layer_processors) {
                    if (layer.instrument.tag == InstrumentType::None) continue;

                    auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>();
                    if (!sampled_inst) continue;

                    auto const& library = (*sampled_inst)->instrument.library;

                    InstBrowserState ephemeral_state {.id = HashFnv1a("ephemeral-inst-browser")};
                    ephemeral_state.common_state.Filter(BrowserFilter::Library)
                        .Add(Hash(library.id), library.name);

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

        // Shuffle instruments (any)
        {
            auto const btn = DoIconButton(builder,
                                          btn_row,
                                          ICON_FA_GLOBE ""_s,
                                          "Random instruments from any library"_s,
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
    }

    // Effects randomise section
    {
        auto const section = DoBox(builder,
                                   {
                                       .parent = column,
                                       .layout {
                                           .size = {layout::k_hug_contents, layout::k_hug_contents},
                                           .contents_gap = 4,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

        DoSectionLabel(builder, section, "EFFECTS"_s);

        auto const btn = DoIconButton(builder, section, ICON_FA_SHUFFLE ""_s, "Randomise all effects"_s, 2);
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

static void DoMacrosColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_macros_column_width = 160;
    constexpr f32 k_macro_knob_width = 32;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b1000, // left
                                  .layout {
                                      .size = {k_macros_column_width, layout::k_fill_parent},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 4,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    DoSectionLabel(builder, column, "MACROS"_s);

    // 2x2 grid
    auto const grid = DoBox(builder,
                            {
                                .parent = column,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .margins = {.t = 14},
                                    .contents_gap = 8,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    for (usize row = 0; row < 2; row++) {
        auto const grid_row = DoBox(builder,
                                    {
                                        .parent = grid,
                                        .id_extra = row,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_gap = 4,
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

        for (usize col = 0; col < 2; col++) {
            auto const macro_index = (row * 2) + col;
            auto const param_index = k_macro_params[macro_index];
            bool const has_destinations = g.engine.processor.main_macro_destinations[macro_index].Size() != 0;

            // Wrapper to give each knob equal space in the row
            auto const cell = DoBox(builder,
                                    {
                                        .parent = grid_row,
                                        .id_extra = col,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });
            DoKnobParameter(g,
                            cell,
                            g.engine.processor.main_params.DescribedValue(param_index),
                            {
                                .width = k_macro_knob_width,
                                .style_system = GuiStyleSystem::MidPanel,
                                .greyed_out = !has_destinations,
                                .override_label = g.engine.macro_names[macro_index],
                            });
        }
    }
}

static void DoDescriptionColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_desc_column_width = 160;

    auto const& snapshot = g.engine.last_snapshot;
    auto const& metadata = snapshot.state.metadata;

    bool const has_desc = metadata.description.size != 0;

    auto const auto_desc = has_desc ? AutoDescriptionString {} : AutoDescription(g.engine);
    bool const has_auto_desc = auto_desc.size != 0;

    if (!has_desc && !has_auto_desc) return;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b0010, // right
                                  .layout {
                                      .size = {k_desc_column_width, layout::k_fill_parent},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 6,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    DoSectionLabel(builder, column, "DESCRIPTION"_s);

    if (has_desc) {
        DoBox(builder,
              {
                  .parent = column,
                  .text = fmt::FormatStringReplace(g.scratch_arena,
                                                   metadata.description,
                                                   ArrayT<fmt::StringReplacement>({{"\n"_s, " "_s}})),
                  .wrap_width = k_desc_column_width - 20,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::White, .alpha = 170},
              });
    } else if (has_auto_desc) {
        DoBox(builder,
              {
                  .parent = column,
                  .text = auto_desc,
                  .wrap_width = k_desc_column_width - 20,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::White, .alpha = 130},
              });
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
                                    .contents_padding = {.lr = 0, .t = 15, .b = 6.08f},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    // Preset info row: [prev] [info] [next]
    {
        constexpr f32 k_nav_btn_size = 32;

        auto const info_row = DoBox(builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

        auto const do_nav_button = [&](Box parent, String icon, String tooltip, u64 id_extra) {
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
                                           .size = {k_nav_btn_size, k_nav_btn_size},
                                           .margins = {.t = 16},
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
                      .text_colours = Col {.c = Col::White, .alpha = 180},
                  });
            return btn;
        };

        auto const load_adjacent = [&](SearchDirection direction) {
            PresetBrowserContext context {
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .preset_server = g.shared_engine_systems.preset_server,
                .library_images = g.library_images,
                .prefs = g.prefs,
                .engine = g.engine,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            context.Init(g.scratch_arena);
            DEFER { context.Deinit(); };
            LoadAdjacentPreset(context, g.preset_browser_state, direction);
        };

        auto const prev_btn = do_nav_button(info_row, ICON_FA_CARET_LEFT ""_s, "Previous preset"_s, 0);
        if (prev_btn.button_fired) load_adjacent(SearchDirection::Backward);
        if (prev_btn.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);

        DoPresetInfo(builder, g, frame_context, info_row);

        auto const next_btn = do_nav_button(info_row, ICON_FA_CARET_RIGHT ""_s, "Next preset"_s, 1);
        if (next_btn.button_fired) load_adjacent(SearchDirection::Forward);
        if (next_btn.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
    }

    // Folder name badge at the bottom of the top section
    {
        auto const& snapshot = g.engine.last_snapshot;
        DynamicArray<char> folder_buf {g.scratch_arena};
        if (auto const preset_path = snapshot.name_or_path.Path()) {
            if (auto const dir = path::Directory(*preset_path))
                fmt::Append(folder_buf, "{}", StripNumberedPrefix(path::Filename(*dir)));
        }
        if (folder_buf.size) {
            auto const badge = DoBox(builder,
                                     {
                                         .parent = root,
                                         .round_background_corners = 0b1111,
                                         .corner_rounding = k_corner_rounding,
                                         .layout {
                                             .size = {layout::k_hug_contents, layout::k_hug_contents},
                                             .margins = {.t = 8},
                                             .contents_padding = {.lr = 8, .tb = 4},
                                         },
                                     });
            if (auto const r = BoxRect(builder, badge))
                DrawMidBlurredPanelSurface(g,
                                           builder.imgui.ViewportRectToWindowRect(*r),
                                           LibraryForOverallBackground(g.engine));
            DoBox(builder,
                  {
                      .parent = badge,
                      .text = folder_buf.ToOwnedSpan(),
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::White, .alpha = 160},
                  });
        }
    }

    // Spacer pushes the central panel to the bottom
    DoBox(builder,
          {
              .parent = root,
              .layout {
                  .size = {0, layout::k_fill_parent},
              },
          });

    {
        auto const central_panel = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_hug_contents, 170},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

        if (auto const r = BoxRect(builder, central_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoDescriptionColumn(builder, g, central_panel);

        DoLayersColumn(builder, g, central_panel);

        DoActionsColumn(builder, g, frame_context, central_panel);

        DoMacrosColumn(builder, g, central_panel);
    }
}
