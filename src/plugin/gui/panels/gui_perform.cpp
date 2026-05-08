// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_perform.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/persistent_store.hpp"
#include "common_infrastructure/state/macros.hpp"

#include "engine/engine.hpp"
#include "engine/random_variation.hpp"
#include "gui/controls/gui_pinned_view_toggle.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_layer_common.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui/panels/gui_preset_browser.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "preset_server/preset_server.hpp"
#include "processor/layer_processor.hpp"
#include "processor/processor.hpp"

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

static void DoPresetInfo(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    auto const& snapshot = g.engine.pinned_snapshot;

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
        auto name = snapshot.state.extras.display_name;
        if (name.size) {
            if (StateModifiedFromPinned(g.engine)) dyn::AppendSpan(name, " (modified)");
            DoBox(builder,
                  {
                      .parent = container,
                      .text = name,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                      .font = FontType::LargeTitle,
                      .text_colours = Col {.c = Col::White},
                      .text_justification = TextJustification::Centred,
                  });
        }
    }
}

constexpr f32 k_inst_name_top_margin = 2;

static void DoLayersColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    auto& params = g.engine.processor.main_params;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_fill_parent},
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    auto const layers_row = DoBox(builder,
                                  {
                                      .parent = column,
                                      .layout {
                                          .size = layout::k_fill_parent,
                                          .contents_padding = {.lr = 10, .tb = 10},
                                          .contents_gap = 8,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                      },
                                  });

    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto& layer = g.engine.processor.layer_processors[layer_index];
        bool const active = layer.instrument.tag != InstrumentType::None;

        auto const cell =
            DoBox(builder,
                  {
                      .parent = layers_row,
                      .id_extra = layer_index,
                      .border_colours = Col {.c = Col::White, .alpha = 14},
                      .border_edges = (Edges)((layer_index < k_num_layers - 1) ? 0b0010 : 0b0000),
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_fill_parent},
                          .contents_padding = {.r = 4},
                          .contents_gap = 2,
                          .contents_direction = layout::Direction::Column,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
                  });

        DoBox(builder,
              {
                  .parent = cell,
                  .text = fmt::Format(g.scratch_arena, "LAYER {}", layer_index + 1),
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = Col {.c = Col::White, .alpha = (u8)(active ? 100 : 60)},
                  .text_justification = TextJustification::CentredLeft,
                  .layout {
                      .margins = {.l = 2},
                  },
              });

        {
            auto const inst_btn = DoBox(
                builder,
                {
                    .parent = cell,
                    .id_extra = layer_index,
                    .background_fill_colours =
                        ColSet {
                            .base = Col {.c = Col::None},
                            .hot = Col {.c = Col::White, .alpha = 10},
                            .active = Col {.c = Col::White, .alpha = 18},
                        },
                    .round_background_corners = 0b1111,
                    .corner_rounding = k_corner_rounding,
                    .layout {
                        .size = {layout::k_fill_parent, (k_font_body_size * 2) + k_inst_name_top_margin},
                        .contents_direction = layout::Direction::Column,
                        .contents_align = layout::Alignment::Start,
                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                    },
                    .tooltip = active ? "Open instrument browser"_s : "Choose an instrument"_s,
                    .button_behaviour = imgui::ButtonConfig {},
                });

            if (inst_btn.button_fired) {
                g.imgui.OpenModalViewport(g.inst_browser_state[layer_index].id);
                if (auto const r = BoxRect(builder, inst_btn))
                    g.inst_browser_state[layer_index].common_state.absolute_button_rect =
                        g.imgui.ViewportRectToWindowRect(*r);
            }

            if (active) {
                auto const inst_name = layer.InstName();

                if (inst_name.size) {
                    DoBox(builder,
                          {
                              .parent = inst_btn,
                              .text = inst_name,
                              .font = FontType::Body,
                              .text_colours =
                                  ColSet {
                                      .base = Col {.c = Col::White, .alpha = 200},
                                      .hot = Col {.c = Col::White, .alpha = 240},
                                      .active = Col {.c = Col::White, .alpha = 255},
                                  },
                              .text_overflow = TextOverflowType::ShowDotsOnRight,
                              .parent_dictates_hot_and_active = true,
                              .layout {
                                  .size = {layout::k_fill_parent, k_font_body_size},
                                  .margins = {.l = 2, .t = k_inst_name_top_margin},
                              },
                          });

                    if (auto sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>()) {
                        auto const& inst = (*sampled_inst)->instrument;
                        if (inst.folder) {
                            auto const raw_folder_name = inst.folder->display_name.size
                                                             ? inst.folder->display_name
                                                             : inst.folder->name;
                            auto const folder_name = StripNumberedPrefix(raw_folder_name);
                            DoBox(builder,
                                  {
                                      .parent = inst_btn,
                                      .text = folder_name,
                                      .font = FontType::Body,
                                      .text_colours = Col {.c = Col::White, .alpha = 120},
                                      .text_overflow = TextOverflowType::ShowDotsOnRight,
                                      .parent_dictates_hot_and_active = true,
                                      .layout {
                                          .size = {layout::k_fill_parent, k_font_body_size},
                                          .margins = {.l = 2},
                                      },
                                  });
                        }
                    }
                }
            } else {
                DoBox(builder,
                      {
                          .parent = inst_btn,
                          .text = "None"_s,
                          .font = FontType::Body,
                          .text_colours =
                              ColSet {
                                  .base = Col {.c = Col::White, .alpha = 60},
                                  .hot = Col {.c = Col::White, .alpha = 120},
                                  .active = Col {.c = Col::White, .alpha = 160},
                              },
                          .parent_dictates_hot_and_active = true,
                          .layout {
                              .size = {layout::k_fill_parent, k_font_body_size},
                              .margins = {.l = 2, .t = k_inst_name_top_margin},
                          },
                      });
            }

            DoInstSelectorRightClickMenu(g, inst_btn, layer_index);
        }

        auto const controls_row = DoBox(builder,
                                        {
                                            .parent = cell,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                .margins = {.t = 6},
                                                .contents_gap = 4,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                            },
                                        });

        constexpr f32 k_inactive_alpha = 0.15f;

        {
            auto const waveform_box = DoBox(builder,
                                            {
                                                .parent = controls_row,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                },
                                            });
            if (auto const r = BoxRect(builder, waveform_box)) {
                if (active) {
                    DoWaveformElement(g, layer, *r);
                } else {
                    g.imgui.draw_list->AddRectFilled(
                        g.imgui.ViewportRectToWindowRect(*r),
                        ChangeAlpha(LiveCol(UiColMap::WaveformLoopBack), k_inactive_alpha));
                }
            }
        }

        {
            auto const meter_box = DoBox(builder,
                                         {
                                             .parent = controls_row,
                                             .layout {
                                                 .size = {6, layout::k_fill_parent},
                                             },
                                         });
            if (auto const r = BoxRect(builder, meter_box)) {
                if (active) {
                    DrawPeakMeter(g.imgui,
                                  g.imgui.ViewportRectToWindowRect(*r),
                                  layer.peak_meter,
                                  {
                                      .flash_when_clipping = false,
                                      .show_db_markers = false,
                                      .gap_px = 1,
                                  });
                } else {
                    g.imgui.draw_list->AddRectFilled(
                        g.imgui.ViewportRectToWindowRect(*r),
                        ChangeAlpha(LiveCol(UiColMap::PeakMeterBack), k_inactive_alpha));
                }
            }
        }

        if (active) {
            DoVerticalSliderParameter(g,
                                      controls_row,
                                      params.DescribedValue(layer_index, LayerParamIndex::Volume),
                                      {
                                          .width = 12,
                                          .height = layout::k_fill_parent,
                                          .style_system = GuiStyleSystem::MidPanel,
                                      });
        } else {
            auto const slider_box = DoBox(builder,
                                          {
                                              .parent = controls_row,
                                              .layout {
                                                  .size = {12, layout::k_fill_parent},
                                              },
                                          });
            if (auto const r = BoxRect(builder, slider_box)) {
                auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
                auto const channel_w = WwToPixels(4.0f);
                Rect const channel_r {.x = window_r.x + ((window_r.w - channel_w) / 2),
                                      .y = window_r.y,
                                      .w = channel_w,
                                      .h = window_r.h};
                g.imgui.draw_list->AddRectFilled(
                    channel_r,
                    ChangeAlpha(LiveCol(UiColMap::SliderMidChannel), k_inactive_alpha),
                    WwToPixels(k_corner_rounding));
            }
        }
    }

    bool any_active = false;
    for (auto const& inst_id : g.engine.pinned_snapshot.state.inst_ids)
        if (inst_id.tag != InstrumentType::None) {
            any_active = true;
            break;
        }

    auto const utility_row = DoBox(builder,
                                   {
                                       .parent = column,
                                       .border_colours = Col {.c = Col::White, .alpha = 14},
                                       .border_edges = 0b0100, // top
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_padding = {.lr = 10},
                                           .contents_gap = 10,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    {
        auto const vary_section = DoBox(builder,
                                        {
                                            .parent = utility_row,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_padding = {.tb = 10},
                                                .contents_gap = 4,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });

        auto const header_row = DoBox(builder,
                                      {
                                          .parent = vary_section,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = 8,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                          },
                                      });

        DoSectionLabel(builder, header_row, "RANDOM VARIATION"_s);

        auto const hover_text_box = DoBox(builder,
                                          {
                                              .parent = header_row,
                                              .layout {
                                                  .size = {layout::k_fill_parent, k_font_body_size},
                                              },
                                          });

        auto const pill = DoBox(builder,
                                {
                                    .parent = vary_section,
                                    .layout {
                                        .size = {layout::k_fill_parent, k_mid_button_height},
                                        .contents_gap = 6,
                                        .contents_direction = layout::Direction::Row,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                    },
                                });

        auto const vary_btn = DoMidPanelIconButton(
            builder,
            pill,
            {
                .icon = MidPanelIcon::Shuffle,
                .tooltip =
                    "Click to load a new random variation using the same randomness as last time.\n\n"
                    "Or, click anywhere on the strip to load a variation: further right means a more varied result, further left stays closer to the current preset."_s,
                .greyed_out = !any_active,
            });

        auto const strip = DoBox(builder,
                                 {
                                     .parent = pill,
                                     .id_extra = 1,
                                     .background_fill_colours = Col {.c = Col::White, .alpha = 12},
                                     .round_background_corners = 0b1111,
                                     .corner_rounding = k_corner_rounding,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_fill_parent},
                                     },
                                     .button_behaviour = imgui::ButtonConfig {},
                                 });

        String hover_label = {};

        if (auto const r = BoxRect(builder, strip)) {
            auto const wr = g.imgui.ViewportRectToWindowRect(*r);

            f32 const flash = g.imgui.GetAnimatedValue(strip.imgui_id, 0.0f);
            if (flash > 0.001f) {
                constexpr f32 k_flash_w = 20;
                f32 const half_inset = (wr.h - 2.0f) * 0.5f;
                f32 const fx =
                    Clamp(g.mid_panel_state.last_strip_fire_x, wr.x + half_inset, wr.x + wr.w - half_inset);
                auto const alpha = (u8)(flash * 70.0f);
                u32 const col = ToU32(Col {.c = Col::White, .alpha = alpha});
                g.imgui.draw_list->AddRectFilled(f32x2 {fx - (k_flash_w * 0.5f), wr.y},
                                                 f32x2 {fx + (k_flash_w * 0.5f), wr.y + wr.h},
                                                 col,
                                                 k_corner_rounding * 0.7f);
            }

            {
                f32 const spacing = WwToPixels(5.0f);
                f32 const inset_x = wr.w * (14.0f / 660.0f);
                f32 const x_start = Round(wr.x + inset_x);
                f32 const x_end = wr.x + wr.w - inset_x;
                int const n_cols = Max(1, (int)((x_end - x_start) / spacing));
                int const n_rows = Max(1, (int)((wr.h - spacing) / spacing));
                f32 const cy = wr.y + (wr.h * 0.5f);
                f32 const grid_h = (f32)(n_rows - 1) * spacing;
                f32 const y0 = Round(cy - (grid_h * 0.5f));
                f32 const x_step = (x_end - x_start) / (f32)Max(1, n_cols - 1);
                for (auto const col_idx : Range(n_cols)) {
                    f32 const x = Round(x_start + ((f32)col_idx * x_step));
                    f32 const frac = n_cols == 1 ? 1.0f : (f32)col_idx / (f32)(n_cols - 1);
                    auto const alpha = (u8)Clamp(8.0f + (frac * 36.0f), 0.0f, 255.0f);
                    u32 const dot_col = ToU32(Col {.c = Col::White, .alpha = alpha});
                    for (auto const row_idx : Range(n_rows)) {
                        f32 const y = y0 + ((f32)row_idx * spacing);
                        g.imgui.draw_list->AddRectFilled(f32x2 {x, y}, f32x2 {x + 1.0f, y + 1.0f}, dot_col);
                    }
                }
            }

            bool const using_last_amount = vary_btn.is_hot || vary_btn.is_active;
            if (strip.is_hot || strip.is_active || using_last_amount) {
                GuiIo().out.wants.mouse_motion_redraw = true;
                f32 const half_inset = (wr.h - 2.0f) * 0.5f;
                f32 const min_x = wr.x + half_inset;
                f32 const max_x = wr.x + wr.w - half_inset;
                f32 const ref_x =
                    using_last_amount
                        ? min_x + (g.mid_panel_state.last_random_variation_amount * (max_x - min_x))
                    : strip.is_active ? GuiIo().in.Mouse(MouseButton::Left).last_press.point.x
                                      : GuiIo().in.cursor_pos.x;
                f32 const tick_x = Clamp(ref_x, min_x, max_x);
                f32 const t_hover = Clamp((ref_x - min_x) / (max_x - min_x), 0.0f, 1.0f);
                bool const armed = strip.is_active || vary_btn.is_active;

                u32 const cursor_col = ToU32(Col {.c = Col::White, .alpha = (u8)(armed ? 90 : 60)});
                constexpr f32 k_cursor_w = 20;
                g.imgui.draw_list->AddRectFilled(f32x2 {tick_x - (k_cursor_w * 0.5f), wr.y},
                                                 f32x2 {tick_x + (k_cursor_w * 0.5f), wr.y + wr.h},
                                                 cursor_col,
                                                 k_corner_rounding * 0.7f);

                auto const pct = (int)(t_hover * 100.0f);
                String word;
                if (pct <= 20)
                    word = "small variation"_s;
                else if (pct <= 40)
                    word = "medium variation"_s;
                else if (pct <= 60)
                    word = "large variation"_s;
                else if (pct <= 80)
                    word = "huge variation"_s;
                else
                    word = "extreme variation"_s;
                hover_label = fmt::Format(g.scratch_arena, "{}% — {}", pct, word);
            }
        }

        if (hover_label.size) {
            if (auto const r = BoxRect(builder, hover_text_box)) {
                auto const wr = g.imgui.ViewportRectToWindowRect(*r);
                builder.fonts.Push(ToInt(FontType::BodyItalic));
                DEFER { builder.fonts.Pop(); };
                u32 const text_col = ToU32(Col {.c = Col::White, .alpha = 120});
                g.imgui.draw_list->AddTextInRect(wr,
                                                 text_col,
                                                 hover_label,
                                                 {
                                                     .justification = TextJustification::CentredRight,
                                                     .overflow_type = TextOverflowType::AllowOverflow,
                                                 });
            }
        }

        if (strip.button_fired || vary_btn.button_fired) {
            if (auto const r = BoxRect(builder, strip)) {
                auto const wr = g.imgui.ViewportRectToWindowRect(*r);
                f32 const half_icon = (wr.h - 2.0f) * 0.5f;
                f32 const min_x = wr.x + half_icon;
                f32 const max_x = wr.x + wr.w - half_icon;
                f32 const fire_x =
                    strip.button_fired
                        ? GuiIo().in.Mouse(MouseButton::Left).last_press.point.x
                        : min_x + (g.mid_panel_state.last_random_variation_amount * (max_x - min_x));
                f32 const t = Clamp((fire_x - min_x) / (max_x - min_x), 0.0f, 1.0f);
                LoadRandomVariation(g.engine, t);
                g.mid_panel_state.last_random_variation_amount = t;
                g.mid_panel_state.last_strip_fire_x = fire_x;
                g.imgui.StartAnimation(strip.imgui_id, 1.0f, 0.45f, true);
            }
        }
    }

    DoBox(builder,
          {
              .parent = utility_row,
              .background_fill_colours = Col {.c = Col::White, .alpha = 20},
              .layout {
                  .size = {1, layout::k_fill_parent},
              },
          });

    auto const compare_section = DoBox(builder,
                                       {
                                           .parent = utility_row,
                                           .layout {
                                               .size = {layout::k_hug_contents, layout::k_hug_contents},
                                               .contents_padding = {.tb = 10},
                                               .contents_gap = 4,
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                       });

    DoSectionLabel(builder, compare_section, "COMPARE"_s);

    DoPinnedViewToggle(g, compare_section);
}

static void DoMacrosColumn(GuiBuilder& builder, GuiState& g, Box parent) {
    constexpr f32 k_macros_column_width = 160;
    constexpr f32 k_macro_knob_width = 32;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b0010, // right
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

    auto const& cache = g.engine.pinned_snapshot.description_cache;

    auto const column = DoBox(builder,
                              {
                                  .parent = parent,
                                  .border_colours = Col {.c = Col::White, .alpha = 20},
                                  .border_edges = 0b1000, // left
                                  .layout {
                                      .size = {k_desc_column_width, layout::k_fill_parent},
                                      .contents_padding = {.lr = 10, .tb = 10},
                                      .contents_gap = 6,
                                      .contents_direction = layout::Direction::Column,
                                      .contents_align = layout::Alignment::Start,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                  },
                              });

    if (!cache.long_text.size) return;

    DoSectionLabel(builder, column, cache.long_is_user_desc ? "DESCRIPTION"_s : "AUTO DESCRIPTION"_s);

    DoBox(builder,
          {
              .parent = column,
              .text = fmt::FormatStringReplace(g.scratch_arena,
                                               cache.long_text,
                                               ArrayT<fmt::StringReplacement>({{"\n"_s, " "_s}})),
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::BodyItalic,
              .text_colours = Col {.c = Col::White, .alpha = 170},
          });
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
                                    .contents_padding = {.lr = 0, .t = 15},
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

        DoPresetInfo(builder, g, frame_context, info_row);

        auto const next_btn = do_nav_button(info_row, ICON_FA_CARET_RIGHT ""_s, "Next preset"_s, 1);
        if (next_btn.button_fired) load_adjacent(SearchDirection::Forward);

        if (prev_btn.is_hot || next_btn.is_hot)
            StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
    }

    // Folder name badge at the bottom of the top section
    if (auto const folder_name = PinnedPresetFolderName(g.engine); folder_name.size) {
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
                  .text = folder_name,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::White, .alpha = 160},
              });
    }

    // Spacer pushes the central panel to the bottom
    DoBox(builder,
          {
              .parent = root,
              .layout {
                  .size = {0, layout::k_fill_parent},
              },
          });

    constexpr u64 k_perform_collapsed_id = HashFnv1a("perform-panel-collapsed");
    bool const collapsed =
        persistent_store::GetFlag(g.shared_engine_systems.persistent_store, k_perform_collapsed_id);

    // Collapse/expand toggle - generous click target with chevron icon at the bottom-centre.
    // The icon is hover-only when expanded, always visible when collapsed.
    {
        auto const toggle_bar =
            DoBox(builder,
                  {
                      .parent = root,
                      .layout {
                          .size = {300, 100},
                          .contents_align = layout::Alignment::Middle,
                          .contents_cross_axis_align = layout::CrossAxisAlign::End,
                      },
                      .tooltip = collapsed ? "Show bottom panel"_s : "Hide bottom panel"_s,
                      .button_behaviour = imgui::ButtonConfig {},
                  });

        DoBox(builder,
              {
                  .parent = toggle_bar,
                  .text = (collapsed ? ICON_FA_CHEVRON_UP ""_s : ICON_FA_CHEVRON_DOWN ""_s),
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.9f,
                  .text_colours =
                      ColSet {
                          .base = Col {.c = Col::White, .alpha = (u8)(collapsed ? 160 : 0)},
                          .hot = Col {.c = Col::White, .alpha = (u8)(collapsed ? 220 : 180)},
                          .active = Col {.c = Col::White, .alpha = 220},
                      },
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .margins = {.b = 4},
                  },
              });

        if (toggle_bar.button_fired)
            persistent_store::SetFlag(g.shared_engine_systems.persistent_store,
                                      k_perform_collapsed_id,
                                      !collapsed);
    }

    if (!collapsed) {
        auto const central_panel = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_fill_parent, 170},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
                                         });

        if (auto const r = BoxRect(builder, central_panel))
            DrawMidBlurredPanelSurface(g,
                                       builder.imgui.ViewportRectToWindowRect(*r),
                                       LibraryForOverallBackground(g.engine));

        DoMacrosColumn(builder, g, central_panel);

        DoLayersColumn(builder, g, central_panel);

        DoDescriptionColumn(builder, g, central_panel);
    }
}
