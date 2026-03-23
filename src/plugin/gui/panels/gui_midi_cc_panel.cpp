// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_midi_cc_panel.hpp"

#include <IconsFontAwesome6.h>

#include "common_infrastructure/cc_mapping.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui_framework/gui_builder.hpp"
#include "processor/processor.hpp"

constexpr f32 k_cc_col_width = 60.0f;
constexpr f32 k_module_col_width = 220.0f;
constexpr f32 k_icon_col_width = 25.0f;
constexpr f32 k_table_row_height = 20.0f;

static bool IsDefaultCcMapping(ParamIndex param_index, usize cc_num) {
    for (auto const& mapping : k_default_cc_to_param_mapping)
        if (mapping.param == param_index && mapping.cc == (u7)cc_num) return true;
    return false;
}

static Box TableRow(GuiBuilder& builder, Box parent, u64 id_extra = SourceLocationHash()) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_gap = 0,
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                     },
                 });
}

static void TableHeaderText(GuiBuilder& builder,
                            Box parent,
                            String text,
                            f32 width,
                            TextJustification justify = TextJustification::CentredLeft,
                            u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Subtext0},
              .text_justification = justify,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {width, k_font_body_size},
              },
          });
}

static void
TableCellText(GuiBuilder& builder, Box parent, String text, f32 width, u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .text_justification = TextJustification::CentredLeft,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {width, k_font_body_size},
              },
          });
}

static void MidiCcTableContent(GuiBuilder& builder, MidiCcPanelContext& context) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding =
                                        {
                                            .lr = k_default_spacing,
                                            .tb = 6,
                                        },
                                    .contents_gap = 0,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    auto const& default_cc_pref = SettingDescriptor(ProcessorSetting::DefaultCcParamMappings);
    bool const defaults_enabled = prefs::GetBool(context.prefs, default_cc_pref);

    bool has_any_assignments = false;

    for (auto const param_index_int : Range<u16>(k_num_parameters)) {
        auto const param_index = (ParamIndex)param_index_int;
        auto const& descriptor = k_param_descriptors[param_index_int];

        if (descriptor.flags.not_automatable) continue;

        auto const ccs_bitset = GetLearnedCCsBitsetForParam(context.processor, param_index);
        if (!ccs_bitset.AnyValuesSet()) continue;

        auto const pinned_ccs = PinnedCcsForParam(context.prefs, descriptor.id);

        for (auto const cc_num : Range(128uz)) {
            if (!ccs_bitset.Get(cc_num)) continue;

            has_any_assignments = true;

            builder.imgui.PushId(((u64)param_index_int * 128) + cc_num);
            DEFER { builder.imgui.PopId(); };

            bool const is_default = defaults_enabled && IsDefaultCcMapping(param_index, cc_num);

            auto const row = TableRow(builder, root);

            // CC number column
            {
                auto const cc_container = DoBox(builder,
                                                {
                                                    .parent = row,
                                                    .layout {
                                                        .size = {k_cc_col_width, layout::k_hug_contents},
                                                        .contents_direction = layout::Direction::Row,
                                                        .contents_align = layout::Alignment::Start,
                                                    },
                                                });
                DoBox(builder,
                      {
                          .parent = cc_container,
                          .text = fmt::Format(builder.arena, "CC {}", cc_num),
                          .size_from_text = true,
                          .text_justification = TextJustification::CentredLeft,
                      });
            }

            // Parameter name column (fills remaining space)
            TableCellText(builder, row, descriptor.name, layout::k_fill_parent);

            // Module path column
            TableCellText(builder, row, descriptor.ModuleString(" › "), k_module_col_width);

            bool const is_pinned = pinned_ccs.Get(cc_num);

            // Pin icon - 3 states:
            //   1. Default mapping (from preference): shown with a distinct colour, not
            //      interactive (pinning would be redundant)
            //   2. User-pinned: bright, clickable to unpin
            //   3. Not pinned: dim, clickable to pin
            {
                if (is_default && !is_pinned) {
                    // Default mapping: show pin in a mid-tone to indicate "auto-pinned by
                    // preference". Not interactive since the preference controls it.
                    DoBox(builder,
                          {
                              .parent = row,
                              .text = ICON_FA_THUMBTACK,
                              .font = FontType::Icons,
                              .font_size = k_font_icons_size * 0.8f,
                              .text_colours = Col {.c = Col::Subtext0},
                              .text_justification = TextJustification::Centred,
                              .layout {
                                  .size = {k_icon_col_width, k_table_row_height},
                              },
                              .tooltip = (String)fmt::Format(
                                  builder.arena,
                                  "Pinned by the '{}' option. This mapping is applied each time Floe starts.",
                                  default_cc_pref.gui_label),
                          });
                } else {
                    auto const pin_btn = DoBox(
                        builder,
                        {
                            .parent = row,
                            .text = ICON_FA_THUMBTACK,
                            .font = FontType::Icons,
                            .font_size = k_font_icons_size * 0.8f,
                            .text_colours = is_pinned
                                                ? ColSet {
                                                      .base = Col {.c = Col::Text},
                                                      .hot = Col {.c = Col::Subtext0},
                                                      .active = Col {.c = Col::Subtext0},
                                                  }
                                                : ColSet {
                                                      .base = Col {.c = Col::Overlay0},
                                                      .hot = Col {.c = Col::Text},
                                                      .active = Col {.c = Col::Text},
                                                  },
                            .text_justification = TextJustification::Centred,
                            .background_fill_auto_hot_active_overlay = true,
                            .round_background_corners = 0b1111,
                            .layout {
                                .size = {k_icon_col_width, k_table_row_height},
                            },
                            .tooltip =
                                is_pinned
                                    ? "Pinned: this mapping is applied to all new Floe instances. Click to unpin."_s
                                    : "Not pinned: this mapping only exists in this instance and is saved with your DAW project. Click to pin it so it's applied to all new Floe instances."_s,
                            .button_behaviour = imgui::ButtonConfig {},
                            .extra_margin_for_mouse_events = 2,
                        });

                    if (pin_btn.button_fired) {
                        if (is_pinned)
                            UnpinCcFromParam(context.prefs, (u8)cc_num, descriptor.id);
                        else
                            PinCcToParam(context.prefs, (u8)cc_num, descriptor.id);
                    }
                }
            }

            // Remove button
            {
                auto const remove_btn = DoBox(
                    builder,
                    {
                        .parent = row,
                        .text = ICON_FA_TRASH,
                        .font = FontType::Icons,
                        .font_size = k_font_icons_size * 0.8f,
                        .text_colours =
                            ColSet {
                                .base = Col {.c = Col::Subtext0},
                                .hot = Col {.c = Col::Text},
                                .active = Col {.c = Col::Text},
                            },
                        .text_justification = TextJustification::Centred,
                        .background_fill_auto_hot_active_overlay = true,
                        .round_background_corners = 0b1111,
                        .layout {
                            .size = {k_icon_col_width, k_table_row_height},
                        },
                        .tooltip =
                            is_default
                                ? (String)fmt::Format(
                                      builder.arena,
                                      "Remove this mapping from this instance. It will reappear when Floe restarts because it is pinned by the '{}' option above.",
                                      default_cc_pref.gui_label)
                                : "Remove and unpin this MIDI CC mapping"_s,
                        .button_behaviour = imgui::ButtonConfig {},
                        .extra_margin_for_mouse_events = 2,
                    });

                if (remove_btn.button_fired)
                    UnlearnAndUnpinMidiCC(context.processor, context.prefs, param_index, (u7)cc_num);
            }
        }
    }

    if (!has_any_assignments) {
        DoBox(
            builder,
            {
                .parent = root,
                .text =
                    "No MIDI CC assignments. Right-click a parameter and select 'MIDI CC Learn' to create one.",
                .wrap_width = k_wrap_to_parent,
                .size_from_text = true,
                .font = FontType::BodyItalic,
                .text_colours = Col {.c = Col::Subtext0},
            });
    }
}

static void MidiCcPanel(GuiBuilder& builder, MidiCcPanelContext& context) {
    auto const root = DoModalRootBox(builder);
    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = "MIDI CC Assignments"_s,
                  });

    // Description and default CC toggle
    {
        auto const info_container = DoBox(builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_padding = {.lrtb = k_default_spacing},
                                                  .contents_gap = k_medium_gap,
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Start,
                                              },
                                          });

        DoBox(builder,
              {
                  .parent = info_container,
                  .text = "Right-click any parameter to assign a MIDI CC via 'MIDI CC Learn'. "
                          "Mappings are saved with your DAW project. "
                          "Pin a mapping to apply it to all new Floe instances."_s,
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Subtext0},
              });

        auto const& desc = SettingDescriptor(ProcessorSetting::DefaultCcParamMappings);
        auto const state = prefs::GetBool(context.prefs, desc);
        if (CheckboxButton(builder, info_container, desc.gui_label, state, desc.long_description))
            prefs::SetValue(context.prefs, desc, !state);
    }

    DoModalDivider(builder, root, {.horizontal = true, .subtle = true});

    // Table header
    {
        auto const header_container = DoBox(builder,
                                            {
                                                .parent = root,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                    .contents_padding = {.lr = k_default_spacing, .tb = 8},
                                                },
                                            });

        auto const header_row = TableRow(builder, header_container);
        TableHeaderText(builder, header_row, "CC"_s, k_cc_col_width);
        TableHeaderText(builder, header_row, "Parameter"_s, layout::k_fill_parent);
        TableHeaderText(builder, header_row, "Module"_s, k_module_col_width);
        // Spacer for the pin + remove icon columns
        DoBox(builder,
              {
                  .parent = header_row,
                  .layout {.size = {k_icon_col_width * 2, k_font_body_size}},
              });
    }

    DoModalDivider(builder, root, {.horizontal = true});

    // Scrollable content area
    DoBoxViewport(builder,
                  {
                      .run = [&context](GuiBuilder& builder) { MidiCcTableContent(builder, context); },
                      .bounds = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          },
                                      }),
                      .imgui_id = builder.imgui.MakeId("MidiCcContent"),
                      .viewport_config = ({
                          auto cfg = k_default_modal_subviewport;
                          cfg.scrollbar_inside_padding = true;
                          cfg;
                      }),
                  });
}

void DoMidiCcPanel(GuiBuilder& builder, MidiCcPanelContext& context, MidiCcPanelState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    DoBoxViewport(builder,
                  {
                      .run = [&context](GuiBuilder& b) { MidiCcPanel(b, context); },
                      .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                          WwToPixels(f32x2 {560, 450})),
                      .imgui_id = state.k_panel_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
