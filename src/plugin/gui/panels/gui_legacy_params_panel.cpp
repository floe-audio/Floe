// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_legacy_params_panel.hpp"

#include <IconsFontAwesome6.h>

#include "foundation/utils/algorithm.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/legacy_param_logic.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "processor/processor.hpp"

static void DrawDarkModePanelBackground(imgui::Context const& imgui) {
    auto const rounding = WwToPixels(k_panel_rounding);
    auto const r = imgui.curr_viewport->unpadded_bounds;
    DrawDropShadow(imgui, r, rounding);
    imgui.draw_list->AddRectFilled(r, ToU32({.c = Col::Background1, .dark_mode = true}), rounding);
}

static bool IsVelocityLegacyParam(ParamIndex p) {
    if (auto const lp = LayerParamIndexAndLayerFor(p))
        return lp->param == LayerParamIndex::LegacyVelocityMapping;
    return p == ParamIndex::MasterVelocity;
}

static void ModerniseLegacyInSnapshot(StateSnapshot& snapshot, ParamIndex legacy) {
    if (IsVelocityLegacyParam(legacy)) {
        auto const strength = snapshot.LinearParam(ParamIndex::MasterVelocity);
        for (auto const layer_index : Range<u8>(k_num_layers)) {
            auto const legacy_pi =
                ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::LegacyVelocityMapping);
            auto const mode = (param_values::VelocityMappingMode)Round(snapshot.LinearParam(legacy_pi));
            snapshot.velocity_curve_points[layer_index] = ModerniseVelocityToCurve(mode, strength);
            snapshot.LinearParam(legacy_pi) = (f32)param_values::VelocityMappingMode::None;
        }
        snapshot.LinearParam(ParamIndex::MasterVelocity) = 0;
        return;
    }

    if (auto const mapping = WetDryMappingContaining(legacy);
        mapping && (legacy == mapping->legacy_wet || legacy == mapping->legacy_dry)) {
        ModerniseWetDryEffect(snapshot, *mapping, StateSource::PresetFile);
        return;
    }

    ModerniseLegacyParam(snapshot, legacy, StateSource::PresetFile);
}

static void LegacyParamRow(GuiBuilder& builder, GuiState& g, ParamDescriptor const& desc, Box parent) {
    builder.imgui.PushId(ToInt(desc.index));
    DEFER { builder.imgui.PopId(); };

    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_padding = {.lr = 8, .tb = 6},
                                   .contents_gap = 8,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    auto const text_column = DoBox(builder,
                                   {
                                       .parent = row,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 1,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                       },
                                   });

    DoBox(builder,
          {
              .parent = text_column,
              .text = desc.ModuleString(" › "),
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
          });

    DoBox(builder,
          {
              .parent = text_column,
              .text = desc.name,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Text, .dark_mode = true},
          });

    DoBox(builder,
          {
              .parent = row,
              .layout {.size = {layout::k_fill_parent, 0}},
          });

    auto const right_group = DoBox(builder,
                                   {
                                       .parent = row,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_gap = 12,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

    auto const dpv = g.engine.processor.main_params.DescribedValue(desc.index);
    switch (desc.value_type) {
        case ParamValueType::Float: {
            DoKnobParameter(g,
                            right_group,
                            dpv,
                            {
                                .width = 29,
                                .knob_highlight_col = {.c = Col::Highlight},
                                .knob_line_col = {.c = Col::Background0, .dark_mode = true},
                                .label = false,
                            });
            break;
        }
        case ParamValueType::Menu: {
            DoMenuParameter(g, right_group, dpv, {.width = 140, .label = false});
            break;
        }
        case ParamValueType::Bool: {
            DoButtonParameter(g, right_group, dpv, {.override_label = " "_s});
            break;
        }
        case ParamValueType::Int: {
            // TODO: add int parameter component
            break;
        }
    }

    auto const reset_btn = DoBox(
        builder,
        {
            .parent = right_group,
            .text = ICON_FA_WAND_MAGIC_SPARKLES,
            .size_from_text = true,
            .font = FontType::Icons,
            .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
            .background_fill_auto_hot_active_overlay = true,
            .round_background_corners = 0b1111,
            .tooltip =
                "Modernise: hand control over to the modern parameter, copying across the audibly-equivalent value so the sound doesn't change."_s,
            .button_behaviour = imgui::ButtonConfig {},
            .extra_margin_for_mouse_events = 4,
        });

    if (reset_btn.button_fired) {
        BeginUndoableStep(g.engine, "Modernise legacy parameter"_s);
        DEFER { EndUndoableStep(g.engine); };
        auto snapshot = CurrentStateSnapshot(g.engine);
        ModerniseLegacyInSnapshot(snapshot, desc.index);
        ApplyState(g.engine.processor, snapshot, StateSource::InMemorySource);
    }
}

static void LegacyParamsPanelContent(GuiBuilder& builder, GuiState& g) {
    auto const panel = DoBox(builder,
                             {
                                 .layout {
                                     .size = layout::k_fill_parent,
                                     .contents_padding = {.lrtb = k_default_spacing},
                                     .contents_gap = k_default_spacing,
                                     .contents_direction = layout::Direction::Column,
                                     .contents_align = layout::Alignment::Start,
                                     .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                 },
                             });

    bool const any_overriding = ({
        bool found = false;
        for (auto const& desc : k_param_descriptors) {
            if (!desc.flags.legacy) continue;
            if (IsLegacyParamOverridingModern(desc, g.engine.processor.main_params.LinearValue(desc.index))) {
                found = true;
                break;
            }
        }
        found;
    });

    // TL;DR / at-a-glance summary.
    {
        auto const tldr_box = DoBox(builder,
                                    {
                                        .parent = panel,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_gap = 4,
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

        DoBox(
            builder,
            {
                .parent = tldr_box,
                .text =
                    any_overriding
                        ? "This state comes from an older version of Floe that used slightly different parameters. Floe has perfectly retained the original sound. You can update to the latest parameters — perfectly retaining the original sound (except that further tweaks to macros might have a slightly different shape).\n\nIt's perfectly safe to update unless you have DAW automation on any of the legacy parameters. You don't have to update: Floe is stable and safe in the legacy state, except that it's harder to make further tweaks to the sound unless you modernise first."_s
                        : "No legacy parameters are currently overriding modern controls. Nothing for you to do here."_s,
                .wrap_width = k_wrap_to_parent,
                .size_from_text = true,
                .font = FontType::Body,
                .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
            });
    }

    // Modernise all button (only when there's something to modernise).
    if (any_overriding) {
        auto const modernise_btn = DoBox(
            builder,
            {
                .parent = panel,
                .background_fill_colours = Col {.c = Col::Surface1, .dark_mode = true},
                .background_fill_auto_hot_active_overlay = true,
                .round_background_corners = 0b1111,
                .corner_rounding = k_corner_rounding,
                .layout =
                    {
                        .size = layout::k_hug_contents,
                        .contents_padding = {.lr = 10, .tb = 6},
                        .contents_gap = 6,
                        .contents_direction = layout::Direction::Row,
                        .contents_align = layout::Alignment::Start,
                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                    },
                .tooltip =
                    "Modernise every overriding parameter: hand control over to the modern controls, copying across the audibly-equivalent values so the sound doesn't change. Only do this if you've checked your DAW for automation on these parameters."_s,
                .button_behaviour = imgui::ButtonConfig {},
            });

        DoBox(builder,
              {
                  .parent = modernise_btn,
                  .text = ICON_FA_WAND_MAGIC_SPARKLES,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        DoBox(builder,
              {
                  .parent = modernise_btn,
                  .text = "Modernise State"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        if (modernise_btn.button_fired) {
            BeginUndoableStep(g.engine, "Modernise all legacy parameters"_s);
            DEFER { EndUndoableStep(g.engine); };
            auto snapshot = CurrentStateSnapshot(g.engine);
            for (auto const& desc : k_param_descriptors) {
                if (!desc.flags.legacy) continue;
                if (!IsLegacyParamOverridingModern(desc, snapshot.LinearParam(desc.index))) continue;
                ModerniseLegacyInSnapshot(snapshot, desc.index);
            }
            ApplyState(g.engine.processor, snapshot, StateSource::InMemorySource);
        }
    }

    auto const collapsible_btn = [&](String text, bool& state, u64 loc_hash = SourceLocationHash()) {
        auto const btn = DoBox(builder,
                               {
                                   .parent = panel,
                                   .id_extra = loc_hash,
                                   .layout {
                                       .size = {layout::k_hug_contents, layout::k_hug_contents},
                                       .contents_gap = 4,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                                   .button_behaviour = imgui::ButtonConfig {},
                               });
        DoBox(builder,
              {
                  .parent = btn,
                  .text = state ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours =
                      ColSet {
                          .base {.c = Col::Text, .dark_mode = true},
                          .hot {.c = Col::Subtext0, .dark_mode = true},
                          .active {.c = Col::Text, .dark_mode = true},
                      },
                  .parent_dictates_hot_and_active = true,
              });
        DoBox(builder,
              {
                  .parent = btn,
                  .text = text,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        if (btn.button_fired) state = !state;

        return state;
    };

    // Full explanation (collapsible).
    {
        static bool more_info_open = false;

        if (collapsible_btn("More info", more_info_open)) {
            DoBox(
                builder,
                {
                    .parent = panel,
                    .text =
                        "Floe never deletes parameters: when one needs to change, the old version is kept as a 'legacy' parameter so existing DAW automation keeps working exactly as before. Presets are modernised automatically when loaded — but DAW projects can't be, since Floe can't tell which parameters your DAW is automating.\n\nWhile a legacy override is active, the corresponding modern control is greyed out and disabled, marked with a yellow warning badge that opens this panel. Its knob position and any visualisers (filter response, EQ curve, envelope shape, etc.) reflect the modern parameter's underlying value, not the legacy value actually driving the audio — the sound is correct, but the on-screen display is not.\n\nModernising hands control over to the modern parameter and copies across an audibly-equivalent value so the sound doesn't change. Floe decides whether a legacy parameter is overriding purely by checking whether its value is at the default, so if you modernise while DAW automation is still writing to the legacy parameter, the override will re-engage the moment automation moves the value off default. Remove or re-create the automation in your DAW first.",
                    .wrap_width = k_wrap_to_parent,
                    .size_from_text = true,
                    .font = FontType::Body,
                    .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                });
        }
    }

    // Param list.
    if (any_overriding) {
        static bool show_params = false;
        if (collapsible_btn("Show active legacy parameters", show_params)) {
            auto const list = DoBox(builder,
                                    {
                                        .parent = panel,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_gap = 2,
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

            for (auto const& desc : k_param_descriptors) {
                if (!desc.flags.legacy) continue;
                auto const linear_value = g.engine.processor.main_params.LinearValue(desc.index);
                if (!IsLegacyParamOverridingModern(desc, linear_value)) continue;
                LegacyParamRow(builder, g, desc, list);
            }
        }
    }
}

static void LegacyParamsPanel(GuiBuilder& builder, GuiState& g) {
    auto const root = DoModalRootBox(builder);

    {
        auto const title_container = DoBox(builder,
                                           {
                                               .parent = root,
                                               .layout {
                                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                   .contents_padding = {.lrtb = k_default_spacing},
                                                   .contents_gap = k_default_spacing * 1.2f,
                                                   .contents_direction = layout::Direction::Row,
                                                   .contents_align = layout::Alignment::Justify,
                                               },
                                           });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = "Legacy Parameters",
                  .font = FontType::Heading1,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .layout {
                      .size = {layout::k_fill_parent, k_font_heading1_size},
                  },
              });

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = ICON_FA_XMARK,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .button_behaviour =
                      imgui::ButtonConfig {
                          .closes_popup_or_modal = true,
                      },
                  .extra_margin_for_mouse_events = 8,
              });
    }

    DoModalDivider(builder, root, {.horizontal = true, .dark_mode = true});

    DoBoxViewport(builder,
                  {
                      .run = [&g](GuiBuilder& b) { LegacyParamsPanelContent(b, g); },
                      .bounds = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          },
                                      }),
                      .imgui_id = builder.imgui.MakeId("LegacyParamsContent"),
                      .viewport_config = ({
                          auto cfg = k_default_modal_subviewport;
                          cfg.draw_scrollbars = DrawModalScrollbarsDarkMode;
                          cfg;
                      }),
                  });
}

constexpr bool k_debug_randomise_legacy_params_on_open = false;
static_assert(!k_debug_randomise_legacy_params_on_open || !PRODUCTION_BUILD,
              "k_debug_randomise_legacy_params_on_open must be off in production builds");

[[maybe_unused]] static void DebugRandomiseAllLegacyParams(AudioProcessor& processor) {
    auto seed = RandomSeed();
    for (auto const& desc : k_param_descriptors) {
        if (!desc.flags.legacy) continue;
        auto const v = RandomFloatInRange<f32>(seed, desc.linear_range.min, desc.linear_range.max);
        SetParameterValue(processor, desc.index, v, {});
    }
}

void DoLegacyParamsPanel(GuiBuilder& builder, GuiState& g) {
    if (!builder.imgui.IsModalOpen(k_legacy_params_panel_id)) return;

    if constexpr (k_debug_randomise_legacy_params_on_open) {
        static bool s_randomised_once = false;
        if (!s_randomised_once) {
            s_randomised_once = true;
            DebugRandomiseAllLegacyParams(g.engine.processor);
        }
    }

    auto viewport_config = k_default_modal_viewport;
    viewport_config.draw_background = DrawDarkModePanelBackground;
    viewport_config.exclusive_focus = false;
    viewport_config.close_on_click_outside = false;
    viewport_config.close_on_escape = false;

    auto const window_size = GuiIo().in.window_size.ToFloat2();
    auto const panel_size = WwToPixels(f32x2 {520, 480});
    auto const bounds = Rect {.pos = 0, .size = window_size}.CentredRect(panel_size);

    DoBoxViewport(builder,
                  {
                      .run = [&g](GuiBuilder& b) { LegacyParamsPanel(b, g); },
                      .bounds = bounds,
                      .imgui_id = k_legacy_params_panel_id,
                      .viewport_config = viewport_config,
                  });
}
