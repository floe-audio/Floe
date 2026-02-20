// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_effects.hpp"

#include <IconsFontAwesome6.h>
#include <float.h>

#include "os/threading.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/elements/gui_drawing_helpers.hpp"
#include "gui/old/gui_dragger_widgets.hpp"
#include "gui/old/gui_label_widgets.hpp"
#include "gui/old/gui_menu.hpp"
#include "gui/old/gui_widget_compounds.hpp"
#include "gui/old/gui_widget_helpers.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/effect.hpp"

// TODO: this code needs entirely updating to use GuiBuilder, and no includes of old/* code.

constexpr auto k_reverb_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Reverb},
    .skip = ParamIndex::ReverbOn,
}>();

constexpr auto k_phaser_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Phaser},
    .skip = ParamIndex::PhaserOn,
}>();

struct EffectIDs {
    layout::Id heading;
    layout::Id divider;
    layout::Id close;

    Effect* fx;

    union {
        struct {
            LayIDPair type;
            LayIDPair amount;
        } distortion;

        struct {
            LayIDPair bits;
            LayIDPair sample_rate;
            LayIDPair wet;
            LayIDPair dry;
        } bit_crush;

        struct {
            LayIDPair threshold;
            LayIDPair ratio;
            LayIDPair gain;

            layout::Id auto_gain;
        } compressor;

        struct {
            LayIDPair type;
            LayIDPair cutoff;
            LayIDPair reso;
            bool using_gain;
            LayIDPair gain;
        } filter;

        struct {
            LayIDPair width;
        } stereo;

        struct {
            LayIDPair rate;
            LayIDPair highpass;
            LayIDPair depth;
            LayIDPair wet;
            LayIDPair dry;
        } chorus;

        struct {
            Array<LayIDPair, k_reverb_params.size> ids;
        } reverb;

        struct {
            Array<LayIDPair, k_phaser_params.size> ids;
        } phaser;

        struct {
            LayIDPair feedback;
            LayIDPair left;
            LayIDPair right;
            LayIDPair mix;
            LayIDPair filter_cutoff;
            LayIDPair filter_spread;
            LayIDPair mode;
            layout::Id sync_btn;
        } delay;

        struct {
            LayIDPair ir;
            LayIDPair highpass;
            LayIDPair wet;
            LayIDPair dry;
        } convo;
    };
};

static void DoIrSelectorRightClickMenu(GuiState& g, Rect r, imgui::Id imgui_id) {
    auto& imgui = g.imgui;
    auto const right_click_id = imgui.MakeId("ir-selector-popup");

    r = imgui.RegisterAndConvertRect(r);

    if (imgui.ButtonBehaviour(r,
                              imgui_id,
                              {
                                  .mouse_button = MouseButton::Right,
                                  .event = MouseButtonEvent::Up,
                              })) {
        imgui.OpenPopupMenu(right_click_id, imgui_id);
    }

    if (imgui.IsPopupMenuOpen(right_click_id))
        DoBoxViewport(g.builder,
                      {
                          .run =
                              [&](GuiBuilder&) {
                                  auto const root =
                                      DoBox(g.builder,
                                            {
                                                .layout {
                                                    .size = layout::k_hug_contents,
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });
                                  if (MenuItem(g.builder,
                                               root,
                                               {
                                                   .text = "Unload IR"_s,
                                                   .mode = !g.engine.processor.convo.ir_id
                                                               ? MenuItemOptions::Mode::Disabled
                                                               : MenuItemOptions::Mode::Active,
                                                   .no_icon_gap = true,
                                               })
                                          .button_fired) {
                                      LoadConvolutionIr(g.engine, k_nullopt);
                                  }
                              },
                          .bounds = r,
                          .imgui_id = right_click_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });
}

static void DoImpulseResponseMenu(GuiState& g, GuiFrameContext const& frame_context, layout::Id lay_id) {
    auto r = layout::GetRect(g.layout, lay_id);

    auto const ir_name = IrName(g.engine);

    auto const id = g.imgui.MakeId("Impulse");

    auto const style = buttons::ParameterPopupButton(g.imgui);

    // Draw around the whole thing, not just the menu.
    if (style.back_cols.reg) {
        auto const converted_r = g.imgui.RegisterAndConvertRect(r);
        g.imgui.draw_list->AddRectFilled(converted_r, style.back_cols.reg, LivePx(UiSizeId::CornerRounding));
    }

    auto const arrow_btn_w = LivePx(UiSizeId::NextPrevButtonSize);
    auto const rand_btn_w = LivePx(UiSizeId::ResourceSelectorRandomButtonW);
    auto const margin_r = LivePx(UiSizeId::ParamIntButtonMarginR);
    rect_cut::CutRight(r, margin_r);
    auto rect_rand = rect_cut::CutRight(r, rand_btn_w);
    auto rect_next = rect_cut::CutRight(r, arrow_btn_w);
    auto rect_prev = rect_cut::CutRight(r, arrow_btn_w);

    DoIrSelectorRightClickMenu(g, r, id);

    if (buttons::Button(g, id, r, ir_name, buttons::InstSelectorPopupButton(g.imgui, {}))) {
        g.imgui.OpenModalViewport(g.ir_browser_state.k_panel_id);
        g.ir_browser_state.common_state.absolute_button_rect = g.imgui.ViewportRectToWindowRect(r);
    }

    IrBrowserContext context {
        .sample_library_server = g.shared_engine_systems.sample_library_server,
        .library_images = g.library_images,
        .engine = g.engine,
        .prefs = g.prefs,
        .notifications = g.notifications,
        .persistent_store = g.shared_engine_systems.persistent_store,
        .confirmation_dialog_state = g.confirmation_dialog_state,
        .frame_context = frame_context,
    };

    auto const button_style = buttons::IconButton(g.imgui);
    auto const left_id = id - 4;
    auto const right_id = id + 4;
    auto const random_id = id + 8;
    if (buttons::Button(g, left_id, rect_prev, ICON_FA_CARET_LEFT, button_style))
        LoadAdjacentIr(context, g.ir_browser_state, SearchDirection::Backward);
    if (buttons::Button(g, right_id, rect_next, ICON_FA_CARET_RIGHT, button_style))
        LoadAdjacentIr(context, g.ir_browser_state, SearchDirection::Forward);
    if (buttons::Button(g,
                        random_id,
                        rect_rand,
                        ICON_FA_SHUFFLE,
                        buttons::IconButton(g.imgui).WithRandomiseIconScaling()))
        LoadRandomIr(context, g.ir_browser_state);

    Tooltip(g,
            left_id,
            g.imgui.ViewportRectToWindowRect(rect_prev),
            "Previous IR.\n\nThis is based on the currently selected filters."_s,
            {});
    Tooltip(g,
            right_id,
            g.imgui.ViewportRectToWindowRect(rect_next),
            "Next IR.\n\nThis is based on the currently selected filters."_s,
            {});
    Tooltip(g,
            random_id,
            g.imgui.ViewportRectToWindowRect(rect_rand),
            "Load a random IR.\n\nThis is based on the currently selected filters."_s,
            {});
    Tooltip(g,
            id,
            g.imgui.ViewportRectToWindowRect(r),
            fmt::Format(g.scratch_arena, "Impulse: {}\n{}", ir_name, "The impulse response to use"),
            {});
}

struct FXColours {
    u32 back;
    u32 highlight;
    u32 button;
};

static FXColours GetFxCols(imgui::Context const&, EffectType type) {
    using enum UiColMap;
    switch (type) {
        case EffectType::Distortion:
            return {LiveCol(DistortionBack), LiveCol(DistortionHighlight), LiveCol(DistortionButton)};
        case EffectType::BitCrush:
            return {LiveCol(BitCrushBack), LiveCol(BitCrushHighlight), LiveCol(BitCrushButton)};
        case EffectType::Compressor:
            return {LiveCol(CompressorBack), LiveCol(CompressorHighlight), LiveCol(CompressorButton)};
        case EffectType::FilterEffect:
            return {LiveCol(FilterBack), LiveCol(FilterHighlight), LiveCol(FilterButton)};
        case EffectType::StereoWiden:
            return {LiveCol(StereoBack), LiveCol(StereoHighlight), LiveCol(StereoButton)};
        case EffectType::Chorus:
            return {LiveCol(ChorusBack), LiveCol(ChorusHighlight), LiveCol(ChorusButton)};
        case EffectType::Reverb:
            return {LiveCol(ReverbBack), LiveCol(ReverbHighlight), LiveCol(ReverbButton)};
        case EffectType::Delay: return {LiveCol(DelayBack), LiveCol(DelayHighlight), LiveCol(DelayButton)};
        case EffectType::ConvolutionReverb:
            return {LiveCol(ConvolutionBack), LiveCol(ConvolutionHighlight), LiveCol(ConvolutionButton)};
        case EffectType::Phaser:
            return {LiveCol(PhaserBack), LiveCol(PhaserHighlight), LiveCol(PhaserButton)};
        case EffectType::Count: PanicIfReached();
    }
    return {};
}

void DoEffectsViewport(GuiState& g, GuiFrameContext const& frame_context, Rect r) {
    using enum UiSizeId;
    auto& imgui = g.imgui;
    auto& lay = g.layout;
    auto& engine = g.engine;

    auto const fx_divider_margin_b = LivePx(FXDividerMarginB);
    auto const fx_param_button_height = LivePx(FXParamButtonHeight);
    auto const corner_rounding = LivePx(CornerRounding);

    imgui.BeginViewport(({
                            imgui::ViewportConfig {
                                .draw_scrollbars = DrawMidPanelScrollbars,
                                .padding =
                                    {
                                        .l = LivePx(FXViewportPadL),
                                        .t = LivePx(FXViewportPadT),
                                        .r = LivePx(FXViewportPadR),
                                        .b = LivePx(FXViewportPadB),
                                    },
                                .scrollbar_padding = 4,
                                .scrollbar_width = LivePx(ScrollbarWidth),
                                .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                                         imgui::ViewportScrollbarVisibility::Always},
                            };
                        }),
                        r,
                        "Effects");
    DEFER { imgui.EndViewport(); };
    DEFER { layout::ResetContext(lay); };

    layout::Id switches[k_num_effect_types];
    for (auto& s : switches)
        s = layout::k_invalid_id;
    layout::Id switches_bottom_divider;
    DynamicArrayBounded<EffectIDs, k_num_effect_types> effects;

    auto& dragging_fx_unit = g.dragging_fx_unit;

    //
    //
    //
    auto const root_width = imgui.CurrentVpWidth();
    auto effects_root = layout::CreateItem(lay,
                                           g.scratch_arena,
                                           {
                                               .size = imgui.CurrentVpSize(),
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                           });

    int const switches_left_col_size = (k_num_effect_types / 2) + (k_num_effect_types % 2);

    {
        auto const fx_switch_board_margin_l = LivePx(FXSwitchBoardMarginL);
        auto const fx_switch_board_margin_r = LivePx(FXSwitchBoardMarginR);

        auto switches_container =
            layout::CreateItem(lay,
                               g.scratch_arena,
                               {
                                   .parent = effects_root,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .margins = {.l = fx_switch_board_margin_l,
                                               .r = fx_switch_board_margin_r,
                                               .t = LivePx(FXSwitchBoardMarginT),
                                               .b = LivePx(FXSwitchBoardMarginB)},
                                   .contents_direction = layout::Direction::Row,
                               });

        auto left = layout::CreateItem(lay,
                                       g.scratch_arena,
                                       {
                                           .parent = switches_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Column,

                                       });
        auto right = layout::CreateItem(lay,
                                        g.scratch_arena,
                                        {
                                            .parent = switches_container,
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_direction = layout::Direction::Column,
                                        });

        auto const fx_switch_board_item_height = LivePx(FXSwitchBoardItemHeight);
        for (auto const i : Range(k_num_effect_types)) {
            auto const parent = (i < switches_left_col_size) ? left : right;
            switches[i] = layout::CreateItem(
                lay,
                g.scratch_arena,
                {
                    .parent = parent,
                    .size = {(root_width / 2) - fx_switch_board_margin_l - fx_switch_board_margin_r,
                             fx_switch_board_item_height},
                });
        }
    }

    switches_bottom_divider = layout::CreateItem(lay,
                                                 g.scratch_arena,
                                                 {
                                                     .parent = effects_root,
                                                     .size = {layout::k_fill_parent, 1},
                                                     .margins = {.b = fx_divider_margin_b},
                                                 });

    auto get_heading_size = [&](String name) {
        auto const font = g.fonts.atlas[ToInt(FontType::Heading2)];
        auto size = font->CalcTextSize(
            name,
            {.font_size = font->font_size * buttons::EffectHeading(imgui, 0).text_scaling});
        f32 const epsilon = 2;
        return f32x2 {Round(size.x + epsilon) + LivePx(FXHeadingExtraWidth), LivePx(FXHeadingH)};
    };

    auto create_fx_ids = [&](Effect& fx, layout::Id* heading_container_out) {
        EffectIDs ids;
        ids.fx = &fx;

        auto master_heading_container =
            layout::CreateItem(lay,
                               g.scratch_arena,
                               {
                                   .parent = effects_root,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                               });

        auto const heading_size = get_heading_size(k_effect_info[ToInt(fx.type)].name);
        ids.heading = layout::CreateItem(lay,
                                         g.scratch_arena,
                                         {
                                             .parent = master_heading_container,
                                             .size = {heading_size.x, heading_size.y},
                                             .margins = {.l = LivePx(FXHeadingL), .r = LivePx(FXHeadingR)},
                                             .anchor = layout::Anchor::Left | layout::Anchor::Top,
                                         });

        auto heading_container =
            layout::CreateItem(lay,
                               g.scratch_arena,
                               {
                                   .parent = master_heading_container,
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::End,
                               });

        ids.close = layout::CreateItem(lay,
                                       g.scratch_arena,
                                       {
                                           .parent = master_heading_container,
                                           .size = {LivePx(FXCloseButtonWidth), LivePx(FXCloseButtonHeight)},
                                       });

        if (heading_container_out) *heading_container_out = heading_container;
        return ids;
    };

    auto const divider_options = layout::ItemOptions {
        .parent = effects_root,
        .size = {layout::k_fill_parent, 1},
        .margins = {.t = LivePx(FXDividerMarginT), .b = fx_divider_margin_b},
    };

    auto const param_container_options = layout::ItemOptions {
        .parent = effects_root,
        .size = {layout::k_fill_parent, layout::k_hug_contents},
        .contents_direction = layout::Direction::Row,
        .contents_multiline = true,
        .contents_align = layout::Alignment::Middle,
    };

    auto create_subcontainer = [&](layout::Id parent) {
        return layout::CreateItem(lay,
                                  g.scratch_arena,
                                  {
                                      .parent = parent,
                                      .size = layout::k_hug_contents,
                                      .contents_direction = layout::Direction::Row,
                                  });
    };

    auto layout_all = [&](Span<LayIDPair> ids, Span<ParamIndex const> params) {
        auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

        Optional<layout::Id> container {};
        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_descriptors[ToInt(params[i])];
            layout::Id inner_container = param_container;
            if (info.grouping_within_module != 0) {
                if (!container || info.grouping_within_module != previous_group)
                    container = create_subcontainer(param_container);
                inner_container = *container;
                previous_group = info.grouping_within_module;
            }
            LayoutParameterComponent(g,
                                     inner_container,
                                     ids[i],
                                     engine.processor.main_params.DescribedValue(params[i]));
        }
    };

    auto ordered_effects =
        DecodeEffectsArray(engine.processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                           engine.processor.effects_ordered_by_type);

    for (auto fx : ordered_effects) {
        if (!EffectIsOn(engine.processor.main_params, fx)) continue;

        switch (fx->type) {
            case EffectType::Distortion: {
                auto ids = create_fx_ids(engine.processor.distortion, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.distortion.type,
                    engine.processor.main_params.DescribedValue(ParamIndex::DistortionType));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.distortion.amount,
                    engine.processor.main_params.DescribedValue(ParamIndex::DistortionDrive));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::BitCrush: {
                auto ids = create_fx_ids(engine.processor.bit_crush, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.bit_crush.bits,
                    engine.processor.main_params.DescribedValue(ParamIndex::BitCrushBits));

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.bit_crush.sample_rate,
                    engine.processor.main_params.DescribedValue(ParamIndex::BitCrushBitRate));

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(
                    g,
                    mix_container,
                    ids.bit_crush.wet,
                    engine.processor.main_params.DescribedValue(ParamIndex::BitCrushWet));
                LayoutParameterComponent(
                    g,
                    mix_container,
                    ids.bit_crush.dry,
                    engine.processor.main_params.DescribedValue(ParamIndex::BitCrushDry));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Compressor: {
                layout::Id heading_container;
                auto ids = create_fx_ids(engine.processor.compressor, &heading_container);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                ids.compressor.auto_gain = layout::CreateItem(
                    lay,
                    g.scratch_arena,
                    {
                        .parent = heading_container,
                        .size = {LivePx(FXCompressorAutoGainWidth), fx_param_button_height},
                    });

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.compressor.threshold,
                    engine.processor.main_params.DescribedValue(ParamIndex::CompressorThreshold));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.compressor.ratio,
                    engine.processor.main_params.DescribedValue(ParamIndex::CompressorRatio));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.compressor.gain,
                    engine.processor.main_params.DescribedValue(ParamIndex::CompressorGain));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::FilterEffect: {
                auto ids = create_fx_ids(engine.processor.filter_effect, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.type,
                                         engine.processor.main_params.DescribedValue(ParamIndex::FilterType));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.filter.cutoff,
                    engine.processor.main_params.DescribedValue(ParamIndex::FilterCutoff));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.filter.reso,
                    engine.processor.main_params.DescribedValue(ParamIndex::FilterResonance));
                ids.filter.using_gain =
                    engine.processor.filter_effect.IsUsingGainParam(engine.processor.main_params);
                // Always lay it out so the GUI doesn't jump around.
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.gain,
                                         engine.processor.main_params.DescribedValue(ParamIndex::FilterGain));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::StereoWiden: {
                auto ids = create_fx_ids(engine.processor.stereo_widen, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.stereo.width,
                    engine.processor.main_params.DescribedValue(ParamIndex::StereoWidenWidth));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Chorus: {
                auto ids = create_fx_ids(engine.processor.chorus, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.rate,
                                         engine.processor.main_params.DescribedValue(ParamIndex::ChorusRate));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.chorus.highpass,
                    engine.processor.main_params.DescribedValue(ParamIndex::ChorusHighpass));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.chorus.depth,
                    engine.processor.main_params.DescribedValue(ParamIndex::ChorusDepth));

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.wet,
                                         engine.processor.main_params.DescribedValue(ParamIndex::ChorusWet));
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.dry,
                                         engine.processor.main_params.DescribedValue(ParamIndex::ChorusDry));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Reverb: {
                auto ids = create_fx_ids(engine.processor.reverb, nullptr);

                layout_all(ids.reverb.ids, k_reverb_params);

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Phaser: {
                auto ids = create_fx_ids(engine.processor.phaser, nullptr);

                layout_all(ids.phaser.ids, k_phaser_params);

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Delay: {
                layout::Id heading_container;
                auto ids = create_fx_ids(engine.processor.delay, &heading_container);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                ids.delay.sync_btn =
                    layout::CreateItem(lay,
                                       g.scratch_arena,
                                       {
                                           .parent = heading_container,
                                           .size = {LivePx(FXDelaySyncBtnWidth), fx_param_button_height},
                                       });

                auto left_index = ParamIndex::DelayTimeSyncedL;
                auto right_index = ParamIndex::DelayTimeSyncedR;
                if (!engine.processor.main_params.BoolValue(ParamIndex::DelayTimeSyncSwitch)) {
                    left_index = ParamIndex::DelayTimeLMs;
                    right_index = ParamIndex::DelayTimeRMs;
                }
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.delay.left,
                                         engine.processor.main_params.DescribedValue(left_index),
                                         k_nullopt,
                                         false,
                                         true);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.delay.right,
                                         engine.processor.main_params.DescribedValue(right_index),
                                         k_nullopt,
                                         false,
                                         true);
                {
                    LayoutParameterComponent(
                        g,
                        param_container,
                        ids.delay.feedback,
                        engine.processor.main_params.DescribedValue(ParamIndex::DelayFeedback));
                }
                {
                    auto const id = LayoutParameterComponent(
                        g,
                        param_container,
                        ids.delay.mode,
                        engine.processor.main_params.DescribedValue(ParamIndex::DelayMode));
                    layout::SetBehave(lay, id, layout::flags::LineBreak);
                }
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.delay.filter_cutoff,
                    engine.processor.main_params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones));
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.delay.filter_spread,
                    engine.processor.main_params.DescribedValue(ParamIndex::DelayFilterSpread));
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.delay.mix,
                                         engine.processor.main_params.DescribedValue(ParamIndex::DelayMix));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::ConvolutionReverb: {
                auto ids = create_fx_ids(engine.processor.convo, nullptr);
                auto param_container = layout::CreateItem(lay, g.scratch_arena, param_container_options);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.convo.ir,
                                         LayoutType::Effect,
                                         k_nullopt,
                                         true);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.convo.highpass,
                    engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbHighpass));

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(
                    g,
                    mix_container,
                    ids.convo.wet,
                    engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbWet));
                LayoutParameterComponent(
                    g,
                    mix_container,
                    ids.convo.dry,
                    engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbDry));

                ids.divider = layout::CreateItem(lay, g.scratch_arena, divider_options);
                dyn::Append(effects, ids);
            } break;

            case EffectType::Count: PanicIfReached(); break;
        }
    }

    //
    //
    //
    layout::RunContext(lay);

    layout::Id closest_divider = layout::k_invalid_id;
    if (dragging_fx_unit && imgui.IsViewportHovered(imgui.curr_viewport)) {
        f32 const rel_y_pos = imgui.WindowPosToViewportPos(GuiIo().in.cursor_pos).y;
        f32 distance = Abs(layout::GetRect(lay, switches_bottom_divider).y - rel_y_pos);
        closest_divider = switches_bottom_divider;
        usize closest_slot = 0;
        auto const original_slot = FindSlotInEffects(ordered_effects, dragging_fx_unit->fx);

        for (auto ids : effects) {
            if (f32 const d = Abs(layout::GetRect(lay, ids.divider).y - rel_y_pos); d < distance) {
                distance = d;
                closest_divider = ids.divider;
                closest_slot = FindSlotInEffects(ordered_effects, ids.fx) + 1;
                if (closest_slot > original_slot) --closest_slot;
            }
        }

        ASSERT(closest_slot <= ordered_effects.size);
        if (dragging_fx_unit->drop_slot != closest_slot)
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        dragging_fx_unit->drop_slot = closest_slot;
    }

    auto const draw_divider = [&](layout::Id id) {
        auto const room_at_scroll_viewport_bottom = GuiIo().WwToPixels(15.0f);
        auto const line_r =
            imgui.RegisterAndConvertRect(layout::GetRect(lay, id).WithH(room_at_scroll_viewport_bottom));
        imgui.draw_list->AddLine(line_r.TopLeft(),
                                 line_r.TopRight(),
                                 (id == closest_divider) ? LiveCol(UiColMap::FXDividerLineDropZone)
                                                         : LiveCol(UiColMap::MidViewportDivider));
    };

    auto const fx_knob_joining_line_thickness = LivePx(FXKnobJoiningLineThickness);
    auto const fx_knob_joining_line_pad_lr = LivePx(FXKnobJoiningLinePadLR);
    auto const draw_knob_joining_line = [&](layout::Id knob1, layout::Id knob2) {
        auto r1 = imgui.RegisterAndConvertRect(layout::GetRect(lay, knob1));
        auto r2 = imgui.RegisterAndConvertRect(layout::GetRect(lay, knob2));
        f32x2 const start {r1.Right() + fx_knob_joining_line_pad_lr,
                           r1.CentreY() - (fx_knob_joining_line_thickness / 2)};
        f32x2 const end {r2.x - fx_knob_joining_line_pad_lr, start.y};
        imgui.draw_list->AddLine(start,
                                 end,
                                 LiveCol(UiColMap::FXKnobJoiningLine),
                                 fx_knob_joining_line_thickness);
    };

    auto const do_all_ids = [&](Span<LayIDPair> ids, Span<ParamIndex const> params, FXColours cols) {
        for (auto const i : Range(ids.size))
            KnobAndLabel(g,
                         engine.processor.main_params.DescribedValue(params[i]),
                         ids[i],
                         knobs::DefaultKnob(imgui, cols.highlight));

        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_descriptors[ToInt(params[i])];
            if (info.grouping_within_module != 0) {
                if (info.grouping_within_module == previous_group)
                    draw_knob_joining_line(ids[i - 1].control, ids[i].control);
                previous_group = info.grouping_within_module;
            }
        }
    };

    draw_divider(switches_bottom_divider);

    for (auto ids : effects) {
        imgui.PushId((u64)ids.fx->type);
        DEFER { imgui.PopId(); };

        draw_divider(ids.divider);
        if (dragging_fx_unit && dragging_fx_unit->fx == ids.fx) continue;

        auto const do_heading = [&](Effect& fx, u32 col) {
            {
                auto const id = imgui.MakeId("heading");
                auto const r = layout::GetRect(lay, ids.heading);
                buttons::Button(g,
                                id,
                                r,
                                k_effect_info[ToInt(fx.type)].name,
                                buttons::EffectHeading(imgui, col));

                if (imgui.WasJustActivated(id, MouseButton::Left)) {
                    dragging_fx_unit = DraggingFX {id, &fx, FindSlotInEffects(ordered_effects, &fx), {}};
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                }

                if (imgui.IsHotOrActive(id, MouseButton::Left))
                    GuiIo().out.wants.cursor_type = CursorType::AllArrows;
                Tooltip(g,
                        id,
                        g.imgui.ViewportRectToWindowRect(r),
                        fmt::Format(g.scratch_arena, "{}", k_effect_info[ToInt(fx.type)].description),
                        {});
            }

            {
                auto const close_id = imgui.MakeId("close");
                auto const r = layout::GetRect(lay, ids.close);
                if (buttons::Button(g,
                                    close_id,
                                    r,
                                    ICON_FA_XMARK,
                                    buttons::IconButton(imgui).WithIconScaling(0.9f))) {
                    SetParameterValue(engine.processor, k_effect_info[ToInt(fx.type)].on_param_index, 0, {});
                }
                Tooltip(g,
                        close_id,
                        g.imgui.ViewportRectToWindowRect(r),
                        fmt::Format(g.scratch_arena, "Remove {}", k_effect_info[ToInt(fx.type)].name),
                        {});
            }
        };

        switch (ids.fx->type) {
            case EffectType::Distortion: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.distortion, cols.back);
                auto& d = ids.distortion;

                buttons::PopupWithItems(
                    g,
                    engine.processor.main_params.DescribedValue(ParamIndex::DistortionType),
                    d.type.control,
                    buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.main_params.DescribedValue(ParamIndex::DistortionType),
                              d.type.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::DistortionDrive),
                             d.amount,
                             knobs::DefaultKnob(imgui, cols.highlight));
                break;
            }
            case EffectType::BitCrush: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.bit_crush, cols.back);
                auto& b = ids.bit_crush;

                draggers::Dragger(g,
                                  engine.processor.main_params.DescribedValue(ParamIndex::BitCrushBits),
                                  b.bits.control,
                                  draggers::DefaultStyle(imgui));
                labels::Label(g,
                              engine.processor.main_params.DescribedValue(ParamIndex::BitCrushBits),
                              b.bits.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::BitCrushBitRate),
                             b.sample_rate,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::BitCrushWet),
                             b.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::BitCrushDry),
                             b.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));

                draw_knob_joining_line(b.wet.control, b.dry.control);
                break;
            }
            case EffectType::Compressor: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.compressor, cols.back);
                auto& b = ids.compressor;

                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::CompressorThreshold),
                             b.threshold,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::CompressorRatio),
                             b.ratio,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::CompressorGain),
                             b.gain,
                             knobs::BidirectionalKnob(imgui, cols.highlight));

                buttons::Toggle(g,
                                engine.processor.main_params.DescribedValue(ParamIndex::CompressorAutoGain),
                                b.auto_gain,
                                buttons::ParameterToggleButton(imgui, cols.highlight));
                break;
            }
            case EffectType::FilterEffect: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.filter_effect, cols.back);
                auto& f = ids.filter;

                buttons::PopupWithItems(g,
                                        engine.processor.main_params.DescribedValue(ParamIndex::FilterType),
                                        f.type.control,
                                        buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.main_params.DescribedValue(ParamIndex::FilterType),
                              f.type.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::FilterCutoff),
                             f.cutoff,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::FilterResonance),
                             f.reso,
                             knobs::DefaultKnob(imgui, cols.highlight));
                if (f.using_gain) {
                    KnobAndLabel(g,
                                 engine.processor.main_params.DescribedValue(ParamIndex::FilterGain),
                                 f.gain,
                                 knobs::DefaultKnob(imgui, cols.highlight));
                }
                break;
            }
            case EffectType::StereoWiden: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.stereo_widen, cols.back);
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::StereoWidenWidth),
                             ids.stereo.width,
                             knobs::BidirectionalKnob(imgui, cols.highlight));
                break;
            }
            case EffectType::Chorus: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.chorus, cols.back);
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ChorusRate),
                             ids.chorus.rate,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ChorusDepth),
                             ids.chorus.depth,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ChorusHighpass),
                             ids.chorus.highpass,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ChorusWet),
                             ids.chorus.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ChorusDry),
                             ids.chorus.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));
                draw_knob_joining_line(ids.chorus.wet.control, ids.chorus.dry.control);
                break;
            }

            case EffectType::Reverb: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.reverb, cols.back);
                do_all_ids(ids.reverb.ids, k_reverb_params, cols);
                break;
            }

            case EffectType::Phaser: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.phaser, cols.back);
                do_all_ids(ids.phaser.ids, k_phaser_params, cols);
                break;
            }

            case EffectType::Delay: {
                auto const cols = GetFxCols(imgui, ids.fx->type);
                do_heading(engine.processor.delay, cols.back);
                auto const knob_style = knobs::DefaultKnob(imgui, cols.highlight);

                if (engine.processor.main_params.BoolValue(ParamIndex::DelayTimeSyncSwitch)) {
                    buttons::PopupWithItems(
                        g,
                        engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeSyncedL),
                        ids.delay.left.control,
                        buttons::ParameterPopupButton(imgui));
                    buttons::PopupWithItems(
                        g,
                        engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeSyncedR),
                        ids.delay.right.control,
                        buttons::ParameterPopupButton(imgui));
                    labels::Label(g,
                                  engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeSyncedL),
                                  ids.delay.left.label,
                                  labels::ParameterCentred(imgui));
                    labels::Label(g,
                                  engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeSyncedR),
                                  ids.delay.right.label,
                                  labels::ParameterCentred(imgui));
                } else {
                    KnobAndLabel(g,
                                 engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeLMs),
                                 ids.delay.left,
                                 knob_style);
                    KnobAndLabel(g,
                                 engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeRMs),
                                 ids.delay.right,
                                 knob_style);
                }
                draw_knob_joining_line(ids.delay.left.control, ids.delay.right.control);

                buttons::Toggle(g,
                                engine.processor.main_params.DescribedValue(ParamIndex::DelayTimeSyncSwitch),
                                ids.delay.sync_btn,
                                buttons::ParameterToggleButton(imgui, cols.highlight));

                buttons::PopupWithItems(g,
                                        engine.processor.main_params.DescribedValue(ParamIndex::DelayMode),
                                        ids.delay.mode.control,
                                        buttons::ParameterPopupButton(imgui));
                labels::Label(g,
                              engine.processor.main_params.DescribedValue(ParamIndex::DelayMode),
                              ids.delay.mode.label,
                              labels::ParameterCentred(imgui));

                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::DelayFeedback),
                             ids.delay.feedback,
                             knob_style);
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::DelayMix),
                             ids.delay.mix,
                             knob_style);
                KnobAndLabel(
                    g,
                    engine.processor.main_params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones),
                    ids.delay.filter_cutoff,
                    knob_style);
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::DelayFilterSpread),
                             ids.delay.filter_spread,
                             knob_style);
                draw_knob_joining_line(ids.delay.filter_cutoff.control, ids.delay.filter_spread.control);

                break;
            }

            case EffectType::ConvolutionReverb: {
                auto const cols = GetFxCols(imgui, ids.fx->type);

                do_heading(engine.processor.convo, cols.back);

                DoImpulseResponseMenu(g, frame_context, ids.convo.ir.control);
                labels::Label(g, ids.convo.ir.label, "Impulse", labels::ParameterCentred(imgui));

                KnobAndLabel(
                    g,
                    engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbHighpass),
                    ids.convo.highpass,
                    knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbWet),
                             ids.convo.wet,
                             knobs::DefaultKnob(imgui, cols.highlight));
                KnobAndLabel(g,
                             engine.processor.main_params.DescribedValue(ParamIndex::ConvolutionReverbDry),
                             ids.convo.dry,
                             knobs::DefaultKnob(imgui, cols.highlight));

                draw_knob_joining_line(ids.convo.wet.control, ids.convo.dry.control);
                break;
            }
            case EffectType::Count: PanicIfReached(); break;
        }
    }

    if (dragging_fx_unit) {
        GuiIo().out.wants.cursor_type = CursorType::AllArrows;
        {
            auto style = buttons::EffectHeading(
                imgui,
                ChangeBrightness(GetFxCols(imgui, dragging_fx_unit->fx->type).back | 0xff000000, 0.7f));
            style.draw_with_overlay_graphics = true;

            auto const text = k_effect_info[ToInt(dragging_fx_unit->fx->type)].name;
            Rect btn_r {.pos = GuiIo().in.cursor_pos, .size = get_heading_size(text)};
            btn_r.pos += {btn_r.h, 0};
            buttons::FakeButton(g, btn_r, text, style);
        }

        {
            auto const space_around_cursor = 100.0f;
            Rect spacer_r;
            spacer_r.pos = GuiIo().in.cursor_pos;
            spacer_r.y -= space_around_cursor / 2;
            spacer_r.w = 1;
            spacer_r.h = space_around_cursor;

            auto wnd = imgui.curr_viewport;
            if (!Rect::DoRectsIntersect(spacer_r, wnd->clipping_rect.ReducedVertically(spacer_r.h))) {
                bool const going_up = GuiIo().in.cursor_pos.y < wnd->clipping_rect.CentreY();

                auto const d = 100.0f * GuiIo().in.delta_time;
                GuiIo().WakeupAtTimedInterval(g.redraw_counter, 0.016);

                imgui.SetYScroll(wnd,
                                 Clamp(wnd->scroll_offset.y + (going_up ? -d : d), 0.0f, wnd->scroll_max.y));
            }
        }
    }

    bool effects_order_changed = false;

    if (dragging_fx_unit && imgui.WasJustDeactivated(dragging_fx_unit->id, MouseButton::Left)) {
        MoveEffectToNewSlot(ordered_effects, dragging_fx_unit->fx, dragging_fx_unit->drop_slot);
        effects_order_changed = true;
        dragging_fx_unit.Clear();
    }

    {
        auto const fx_switch_board_number_width = LivePx(FXSwitchBoardNumberWidth);
        auto const fx_switch_board_grab_region_width = LivePx(FXSwitchBoardGrabRegionWidth);

        auto& dragging_fx = g.dragging_fx_switch;
        usize fx_index = 0;
        for (auto const slot : Range(k_num_effect_types)) {
            auto const whole_r = layout::GetRect(lay, switches[slot]);
            auto const number_r = whole_r.WithW(fx_switch_board_number_width);
            auto const slot_r = whole_r.CutLeft(fx_switch_board_number_width);
            auto const converted_slot_r = imgui.RegisterAndConvertRect(slot_r);
            auto const grabber_r = slot_r.CutLeft(slot_r.w - fx_switch_board_grab_region_width);

            labels::Label(g,
                          number_r,
                          fmt::Format(g.scratch_arena, "{}", slot + 1),
                          labels::Parameter(imgui));

            if (dragging_fx &&
                (converted_slot_r.Contains(GuiIo().in.cursor_pos) || dragging_fx->drop_slot == slot)) {
                if (dragging_fx->drop_slot != slot)
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                dragging_fx->drop_slot = slot;
                imgui.draw_list->AddRectFilled(converted_slot_r,
                                               LiveCol(UiColMap::FXButtonDropZone),
                                               corner_rounding);
            } else {
                auto fx = ordered_effects[fx_index++];
                if (dragging_fx && fx == dragging_fx->fx) fx = ordered_effects[fx_index++];

                auto style = buttons::ParameterToggleButton(imgui, GetFxCols(imgui, fx->type).button);
                style.no_tooltips = true;
                auto [changed, id] = buttons::Toggle(g,
                                                     engine.processor.main_params.DescribedValue(
                                                         k_effect_info[ToInt(fx->type)].on_param_index),
                                                     slot_r,
                                                     k_effect_info[ToInt(fx->type)].name,
                                                     style);

                {
                    auto grabber_style = buttons::EffectButtonGrabber(imgui);
                    if (imgui.IsHot(id)) grabber_style.main_cols.reg = grabber_style.main_cols.hot_on;
                    buttons::FakeButton(g, grabber_r, {}, grabber_style);

                    auto converted_grabber_r = imgui.RegisterAndConvertRect(grabber_r);
                    imgui.RegisterRectForMouseTracking(converted_grabber_r);

                    if (converted_grabber_r.Contains(GuiIo().in.cursor_pos))
                        GuiIo().out.wants.cursor_type = CursorType::AllArrows;
                }

                if (imgui.IsActive(id, MouseButton::Left) && !dragging_fx) {
                    auto const click_pos = GuiIo().in.mouse_buttons[0].last_press.point;
                    auto const current_pos = GuiIo().in.cursor_pos;
                    auto const delta = current_pos - click_pos;

                    constexpr f32 k_wiggle_room = 3;
                    if (Sqrt((delta.x * delta.x) + (delta.y * delta.y)) > k_wiggle_room)
                        dragging_fx = DraggingFX {id, fx, slot, GuiIo().in.cursor_pos - converted_slot_r.pos};
                }
            }
        }

        if (dragging_fx) {
            auto style =
                buttons::ParameterToggleButton(imgui, GetFxCols(imgui, dragging_fx->fx->type).button);
            style.draw_with_overlay_graphics = true;

            Rect btn_r {layout::GetRect(lay, switches[0])};
            btn_r.pos = GuiIo().in.cursor_pos - dragging_fx->relative_grab_point;

            auto active_fx = dragging_fx->fx;
            buttons::FakeButton(g,
                                btn_r,
                                k_effect_info[ToInt(active_fx->type)].name,
                                EffectIsOn(engine.processor.main_params, active_fx),
                                style);
            GuiIo().out.wants.cursor_type = CursorType::AllArrows;
        }

        if (dragging_fx && imgui.WasJustDeactivated(dragging_fx->id, MouseButton::Left)) {
            MoveEffectToNewSlot(ordered_effects, dragging_fx->fx, dragging_fx->drop_slot);
            effects_order_changed = true;
            dragging_fx.Clear();
        }
    }

    if (effects_order_changed) {
        engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                     StoreMemoryOrder::Release);
        engine.processor.inbox_flags.FetchOr(audio_thread_inbox::FxOrderChanged, RmwMemoryOrder::Release);
        engine.processor.host.request_process(&engine.processor.host);
    }
}
