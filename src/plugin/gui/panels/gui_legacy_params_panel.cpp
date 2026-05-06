// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_legacy_params_panel.hpp"

#include <IconsFontAwesome6.h>

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

// The legacy velocity system is a per-layer mode + a global master strength that combine into the
// modern per-layer velocity curve points. Modernising it touches all layers + the master at
// once, regardless of which row the user clicked.
static void ModerniseLegacyVelocity(AudioProcessor& processor) {
    auto const strength = processor.main_params.LinearValue(ParamIndex::MasterVelocity);

    for (auto const layer_index : Range<u8>(k_num_layers)) {
        auto const legacy_pi =
            ParamIndexFromLayerParamIndex(layer_index, LayerParamIndex::LegacyVelocityMapping);
        auto const mode =
            (param_values::VelocityMappingMode)Round(processor.main_params.LinearValue(legacy_pi));
        auto const points = ModerniseVelocityToCurve(mode, strength);
        processor.layer_processors[layer_index].velocity_curve_map.SetNewPoints(points);
        SetParameterValue(processor, legacy_pi, (f32)param_values::VelocityMappingMode::None, {});
    }

    SetParameterValue(processor, ParamIndex::MasterVelocity, 0, {});
}

static void ModerniseLegacyWetDryEffect(AudioProcessor& processor, WetDryEffectGroup const& g) {
    auto const wet_lin = processor.main_params.LinearValue(g.legacy_wet);
    auto const dry_lin = processor.main_params.LinearValue(g.legacy_dry);
    auto const r = ConvertWetDryLinearToMixOutput(g, wet_lin, dry_lin);

    SetParameterValue(processor, g.modern_mix, r.mix_linear, {});
    SetParameterValue(processor, g.modern_output, r.output_linear, {});

    SetParameterValue(processor,
                      g.legacy_wet,
                      k_param_descriptors[ToInt(g.legacy_wet)].default_linear_value,
                      {});
    SetParameterValue(processor,
                      g.legacy_dry,
                      k_param_descriptors[ToInt(g.legacy_dry)].default_linear_value,
                      {});

    RetargetMacroDestinations(processor, g.legacy_wet, g.modern_mix);
    RetargetMacroDestinations(processor, g.legacy_dry, g.modern_output);
}

static void ModerniseLegacyParam(AudioProcessor& processor, ParamIndex legacy) {
    if (IsVelocityLegacyParam(legacy)) {
        ModerniseLegacyVelocity(processor);
        return;
    }

    if (auto const g = WetDryGroupContaining(legacy);
        g && (legacy == g->legacy_wet || legacy == g->legacy_dry)) {
        ModerniseLegacyWetDryEffect(processor, *g);
        return;
    }

    // Walk to the ultimate modern at the end of `legacy`'s chain.
    auto const chain_end =
        TopmostSuccessorOfLegacyValue(legacy, k_param_descriptors[ToInt(legacy)].default_linear_value);
    if (!chain_end) return;
    auto const modern = chain_end->successor_param;

    // Resolve the audibly-active value for `modern` (oldest overriding ancestor wins). This
    // is what the user is currently hearing — preserve it across the modernisation.
    auto const resolved_linear = ResolveLegacyAware(modern, processor.main_params.values);
    SetParameterValue(processor, modern, resolved_linear, {});

    // Clear every legacy ancestor in the chain and retarget any macros pointing at them to
    // the modern. Without this, an older non-default legacy would still override the modern
    // we just wrote, defeating the modernisation.
    auto current = LegacyPredecessor(modern);
    while (current) {
        SetParameterValue(processor, *current, k_param_descriptors[ToInt(*current)].default_linear_value, {});
        RetargetMacroDestinations(processor, *current, modern);
        current = LegacyPredecessor(*current);
    }
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
        ModerniseLegacyParam(g.engine.processor, desc.index);
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
        auto const tldr_box =
            DoBox(builder,
                  {
                      .parent = panel,
                      .background_fill_colours = Col {.c = Col::Surface0, .dark_mode = true},
                      .round_background_corners = 0b1111,
                      .corner_rounding = k_corner_rounding,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.lrtb = 10},
                          .contents_gap = 4,
                          .contents_direction = layout::Direction::Column,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
                  });

        DoBox(builder,
              {
                  .parent = tldr_box,
                  .text = "At a glance",
                  .size_from_text = true,
                  .font = FontType::Heading2,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
              });

        DoBox(
            builder,
            {
                .parent = tldr_box,
                .text =
                    any_overriding
                        ? "Older versions of some parameters are currently driving Floe instead of the modern controls on the main UI. Your project sounds exactly as it did when saved. If you don't automate these parameters in your DAW, click 'Modernise all' to switch over to the modern controls — your sound won't change. If you do automate them in your DAW, leave them alone, or remove the automation first."_s
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
                  .text = "Modernise all"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        if (modernise_btn.button_fired) {
            BeginUndoableStep(g.engine, "Modernise all legacy parameters"_s);
            DEFER { EndUndoableStep(g.engine); };
            for (auto const& desc : k_param_descriptors) {
                if (!desc.flags.legacy) continue;
                if (!IsLegacyParamOverridingModern(desc,
                                                   g.engine.processor.main_params.LinearValue(desc.index)))
                    continue;
                ModerniseLegacyParam(g.engine.processor, desc.index);
            }
        }
    }

    // Full explanation (collapsible).
    {
        static bool more_info_open = false;

        auto const more_info_btn = DoBox(builder,
                                         {
                                             .parent = panel,
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
                  .parent = more_info_btn,
                  .text = more_info_open ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT,
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
                  .parent = more_info_btn,
                  .text = "More info"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::Text, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        if (more_info_btn.button_fired) more_info_open = !more_info_open;

        if (more_info_open) {
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
