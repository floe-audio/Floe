// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_perform.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/descriptors/effect_descriptors.hpp"
#include "common_infrastructure/state/macros.hpp"
#include "common_infrastructure/tags.hpp"

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

static void
DoSectionLabel(GuiBuilder& builder, Box parent, String text, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = loc_hash,
              .text = text,
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

static void DoPresetInfo(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const& snapshot = g.engine.last_snapshot;
    auto const& metadata = snapshot.state.metadata;

    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {500, layout::k_hug_contents},
                                         .contents_padding = {.l = 20, .r = 20, .t = 12},
                                         .contents_gap = 5,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
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
                      .font = FontType::LargeTitle,
                      .text_colours = Col {.c = Col::White},
                      .text_justification = TextJustification::Centred,
                  });
        }
    }

    // Folder/primary tags and description in a blurred-background box
    {
        DynamicArray<char> tags_buf {g.scratch_arena};

        if (auto const preset_path = snapshot.name_or_path.Path()) {
            if (auto const dir = path::Directory(*preset_path))
                fmt::Append(tags_buf, "{}", StripNumberedPrefix(path::Filename(*dir)));
        }

        auto const folder_len = tags_buf.size;
        metadata.tags.ForEachSetBit([&](usize bit) {
            auto const tag_type = (TagType)bit;
            auto const tag_and_cat = LookupTagName(GetTagInfo(tag_type).name);
            if (tag_and_cat && Tags(tag_and_cat->category).importance == TagCategoryImportance::Primary) {
                if (tags_buf.size)
                    fmt::Append(tags_buf, "{}", tags_buf.size == folder_len ? "  |  "_s : " · "_s);
                fmt::Append(tags_buf, "{}", GetTagInfo(tag_type).name);
            }
        });

        bool const has_tags = tags_buf.size != 0;
        bool const has_desc = metadata.description.size != 0;

        if (has_tags || has_desc) {
            auto const lib_id = LibraryForOverallBackground(g.engine);

            auto const subtitle_box =
                DoBox(builder,
                      {
                          .parent = container,
                          .layout {
                              .size = {layout::k_hug_contents, layout::k_hug_contents},
                              .contents_padding = {.lr = 10, .tb = 6},
                              .contents_gap = 4,
                              .contents_direction = layout::Direction::Column,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });
            if (auto const r = BoxRect(builder, subtitle_box)) {
                auto const wr = builder.imgui.ViewportRectToWindowRect(*r);
                DrawMidBlurredPanelSurface(g, wr, lib_id);
            }

            if (has_tags) {
                DoBox(builder,
                      {
                          .parent = subtitle_box,
                          .text = tags_buf.ToOwnedSpan(),
                          .size_from_text = true,
                          .font = FontType::BodyItalic,
                          .text_colours = Col {.c = Col::White, .alpha = 200},
                          .text_justification = TextJustification::Centred,
                      });
            }

            if (has_desc) {
                DoBox(builder,
                      {
                          .parent = subtitle_box,
                          .text = fmt::FormatStringReplace(g.scratch_arena,
                                                           metadata.description,
                                                           ArrayT<fmt::StringReplacement>({{"\n"_s, ""_s}})),
                          .wrap_width = 440,
                          .size_from_text = true,
                          .font = FontType::BodyItalic,
                          .text_colours = Col {.c = Col::White, .alpha = 220},
                          .text_justification = TextJustification::Centred,
                          .multiline_alignment = MultilineTextAlignment::Centre,
                      });
            }
        }
    }
}

static void DoTagsAndEffectsPills(GuiBuilder& builder, GuiState& g, Box parent) {
    auto const& snapshot = g.engine.last_snapshot;
    auto const& metadata = snapshot.state.metadata;
    auto const& params = g.engine.processor.main_params;
    auto const ordered_effects =
        DecodeEffectsArray(g.engine.processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           g.engine.processor.effects_ordered_by_type);

    bool has_any_effect = false;
    for (auto const fx : ordered_effects) {
        if (EffectIsOn(params, fx)) {
            has_any_effect = true;
            break;
        }
    }

    if (!metadata.tags.AnyValuesSet() && !has_any_effect) return;

    auto const lib_id = LibraryForOverallBackground(g.engine);
    auto const pill_rounding = WwToPixels(k_panel_rounding);

    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .margins = {.b = 8},
                                         .contents_gap = 4,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });

    auto const make_pills_row = [&](u64 id_extra) {
        return DoBox(builder,
                     {
                         .parent = container,
                         .id_extra = id_extra,
                         .layout {
                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                             .contents_gap = 5,
                             .contents_direction = layout::Direction::Row,
                             .contents_multiline = true,
                             .contents_align = layout::Alignment::Middle,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                         },
                     });
    };

    auto const do_tag_pill = [&](Box row, String tag, u64 id_extra, TooltipString tooltip = k_nullopt) {
        auto const pill = DoBox(builder,
                                {
                                    .parent = row,
                                    .id_extra = id_extra,
                                    .layout {
                                        .size = {layout::k_hug_contents, layout::k_hug_contents},
                                        .contents_padding = {.lr = 8, .tb = 2},
                                        .contents_align = layout::Alignment::Middle,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                    },
                                    .tooltip = tooltip,
                                });
        if (auto const r = BoxRect(builder, pill)) {
            auto const wr = builder.imgui.ViewportRectToWindowRect(*r);
            DrawMidBlurredPanelSurface(g, wr, lib_id);
        }
        DoBox(builder,
              {
                  .parent = pill,
                  .text = tag,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .font_size = k_font_body_size * 0.9f,
                  .text_colours = Col {.c = Col::White, .alpha = 180},
              });
    };

    // Secondary tags (capped to avoid visual noise)
    {
        constexpr usize k_max_visible_secondary = 4;

        usize total_secondary = 0;
        metadata.tags.ForEachSetBit([&](usize bit) {
            auto const tag_and_cat = LookupTagName(GetTagInfo((TagType)bit).name);
            if (!tag_and_cat || Tags(tag_and_cat->category).importance == TagCategoryImportance::Secondary)
                total_secondary++;
        });

        if (total_secondary) {
            auto const row = make_pills_row(1);
            usize shown = 0;
            metadata.tags.ForEachSetBit([&](usize bit) {
                if (shown >= k_max_visible_secondary) return;
                auto const tag_type = (TagType)bit;
                auto const tag_and_cat = LookupTagName(GetTagInfo(tag_type).name);
                if (!tag_and_cat ||
                    Tags(tag_and_cat->category).importance == TagCategoryImportance::Secondary) {
                    do_tag_pill(row, GetTagInfo(tag_type).name, 500 + bit);
                    shown++;
                }
            });

            if (total_secondary > k_max_visible_secondary) {
                do_tag_pill(row,
                            fmt::Format(g.scratch_arena, "+{}", total_secondary - k_max_visible_secondary),
                            SourceLocationHash(),
                            FunctionRef<String()> {[&]() -> String {
                                DynamicArray<char> buf {builder.arena};
                                usize skipped = 0;
                                metadata.tags.ForEachSetBit([&](usize bit) {
                                    auto const tag_type = (TagType)bit;
                                    auto const tag_and_cat = LookupTagName(GetTagInfo(tag_type).name);
                                    if (!tag_and_cat || Tags(tag_and_cat->category).importance ==
                                                            TagCategoryImportance::Secondary) {
                                        skipped++;
                                        if (skipped > k_max_visible_secondary) {
                                            if (buf.size) fmt::Append(buf, ", ");
                                            fmt::Append(buf, "{}", GetTagInfo(tag_type).name);
                                        }
                                    }
                                });
                                return buf.ToOwnedSpan();
                            }});
            }
        }
    }

    // Active effects
    if (has_any_effect) {
        auto const row = make_pills_row(2);
        for (auto const fx : ordered_effects) {
            if (!EffectIsOn(params, fx)) continue;

            auto const pill = DoBox(builder,
                                    {
                                        .parent = row,
                                        .id_extra = 1000 + (u64)fx->type,
                                        .round_background_corners = 0b1111,
                                        .corner_rounding = k_panel_rounding,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_hug_contents},
                                            .contents_padding = {.lr = 8, .tb = 2},
                                            .contents_align = layout::Alignment::Middle,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                        },
                                    });

            if (auto const r = BoxRect(builder, pill)) {
                auto const wr = builder.imgui.ViewportRectToWindowRect(*r);
                DrawMidBlurredPanelSurface(g, wr, lib_id);
                auto const cols = GetFxColMap(fx->type);
                g.imgui.draw_list->AddRectFilled(wr, ChangeAlpha(LiveCol(cols.back), 0.7f), pill_rounding);
            }

            DoBox(builder,
                  {
                      .parent = pill,
                      .text = k_effect_info[ToInt(fx->type)].name,
                      .size_from_text = true,
                      .font = FontType::Body,
                      .font_size = k_font_body_size * 0.9f,
                      .text_colours = Col {.c = Col::White, .alpha = 180},
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
                                  .border_edges = 0b1010, // left and right
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
                                  .text_colours = Col {.c = Col::White, .alpha = 100},
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
                                    .contents_padding = {.lr = 100, .t = 15, .b = 20},
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

        DoPresetInfo(builder, g, info_row);

        auto const next_btn = do_nav_button(info_row, ICON_FA_CARET_RIGHT ""_s, "Next preset"_s, 1);
        if (next_btn.button_fired) load_adjacent(SearchDirection::Forward);
        if (next_btn.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
    }

    // Spacer pushes the central panel to the bottom
    DoBox(builder,
          {
              .parent = root,
              .layout {
                  .size = {0, layout::k_fill_parent},
              },
          });

    DoTagsAndEffectsPills(builder, g, root);

    {
        auto const central_panel = DoBox(builder,
                                         {
                                             .parent = root,
                                             .layout {
                                                 .size = {layout::k_hug_contents, 190},
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

        DoActionsColumn(builder, g, frame_context, central_panel);
    }
}
