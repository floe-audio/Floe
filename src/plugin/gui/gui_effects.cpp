// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_effects.hpp"

#include <float.h>
#include <icons-fa/IconsFontAwesome5.h>

#include "effects/effect.hpp"
#include "gui.hpp"
#include "gui/framework/colours.hpp"
#include "gui/gui_dragger_widgets.hpp"
#include "gui_editor_ui_style.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "param_info.hpp"
#include "plugin_instance.hpp"

constexpr auto k_reverb_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Reverb},
    .skip = ParamIndex::ReverbOn,
}>();

constexpr auto k_new_phaser_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Phaser},
    .skip = ParamIndex::PhaserOn,
}>();

struct EffectIDs {
    LayID heading;
    LayID divider;
    LayID close;

    Effect* fx;
    EffectType type;

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

            LayID auto_gain;
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
            LayIDPair freq;
            LayIDPair mod_freq;
            LayIDPair mod_depth;
            LayIDPair feedback;
            LayIDPair stages;
            LayID mod_stereo;
            LayIDPair wet;
            LayIDPair dry;
        } phaser;

        struct {
            Array<LayIDPair, k_reverb_params.size> ids;
        } reverb;

        struct {
            Array<LayIDPair, k_new_phaser_params.size> ids;
        } new_phaser;

        struct {
            LayID legacy_btn;
            bool is_legacy;
            LayIDPair feedback;
            LayIDPair left;
            LayIDPair right;
            LayIDPair wet;
            LayID sync_btn;
            union {
                struct {
                    LayIDPair damp;
                } old;
                struct {
                    LayIDPair mode;
                    LayIDPair filter;
                } sinevibes;
            };
        } delay;

        struct {
            LayIDPair feedback;
            LayIDPair left;
            LayIDPair right;
            LayIDPair mix;
            LayIDPair filter_cutoff;
            LayIDPair filter_spread;
            LayIDPair mode;
            LayID sync_btn;
        } new_delay;

        struct {
            LayIDPair ir;
            LayIDPair highpass;
            LayIDPair wet;
            LayIDPair dry;
        } convo;
    };
};

static void ImpulseResponseMenuItems(Gui* g) {
    DynamicArrayInline<String, k_core_version_1_irs.size + 1> items;
    dyn::Append(items, "None");
    dyn::AppendSpan(items, k_core_version_1_irs);

    int current = 0;
    if (auto ir = g->plugin.processor.convo.ir_index) {
        auto const found = Find(items, String(ir->ir_name));
        if (found) current = (int)*found;
    }

    if (DoMultipleMenuItems(g, items, current)) {
        _ = SetConvolutionIr(g->plugin,
                             sample_lib::IrId {
                                 .library_name = String(k_core_library_name),
                                 .ir_name = items[(usize)current],
                             });
    }
}

static void DoImpulseResponseMenu(Gui* g, LayID lay_id) {
    auto r = g->layout.GetRect(lay_id);

    auto id = g->imgui.GetID("Impulse");
    auto const ir_name =
        g->plugin.processor.convo.ir_index ? String(g->plugin.processor.convo.ir_index->ir_name) : "None"_s;
    if (buttons::Popup(g, id, id + 1, r, ir_name, buttons::ParameterPopupButton())) {
        ImpulseResponseMenuItems(g);
        g->imgui.EndWindow();
    }
    Tooltip(g, id, r, fmt::Format(g->scratch_arena, "Impulse: {}\n{}", ir_name.size, "Impulse response"));
}

struct FXColours {
    u32 back;
    u32 highlight;
    u32 button;
};

static FXColours GetFxCols(EffectType type) {
    switch (type) {
        case EffectType::Distortion:
            return {GMCC(Distortion, Back), GMCC(Distortion, Highlight), GMCC(Distortion, Button)};
        case EffectType::BitCrush:
            return {GMCC(BitCrush, Back), GMCC(BitCrush, Highlight), GMCC(BitCrush, Button)};
        case EffectType::Compressor:
            return {GMCC(Compressor, Back), GMCC(Compressor, Highlight), GMCC(Compressor, Button)};
        case EffectType::FilterEffect:
            return {GMCC(Filter, Back), GMCC(Filter, Highlight), GMCC(Filter, Button)};
        case EffectType::StereoWiden:
            return {GMCC(Stereo, Back), GMCC(Stereo, Highlight), GMCC(Stereo, Button)};
        case EffectType::Chorus: return {GMCC(Chorus, Back), GMCC(Chorus, Highlight), GMCC(Chorus, Button)};
        case EffectType::Reverb: return {GMCC(Reverb, Back), GMCC(Reverb, Highlight), GMCC(Reverb, Button)};
        case EffectType::NewDelay: return {GMCC(Delay, Back), GMCC(Delay, Highlight), GMCC(Delay, Button)};
        case EffectType::ConvolutionReverb:
            return {GMCC(Convolution, Back), GMCC(Convolution, Highlight), GMCC(Convolution, Button)};
        case EffectType::Phaser: return {GMCC(Phaser, Back), GMCC(Phaser, Highlight), GMCC(Phaser, Button)};
        case EffectType::Count: PanicIfReached();
    }
    return {};
}

void DoEffectsWindow(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto& lay = g->layout;
    auto& plugin = g->plugin;

#define GUI_SIZE(cat, n, v, u) [[maybe_unused]] const auto cat##n = editor::GetSize(imgui, UiSizeId::cat##n);
#include SIZES_DEF_FILENAME
#undef GUI_SIZE

    auto settings = FloeWindowSettings(imgui, [](IMGUI_DRAW_WINDOW_BG_ARGS) {});
    settings.flags |= imgui::WindowFlags_AlwaysDrawScrollY;
    settings.pad_top_left = {(f32)FXWindowPadL, (f32)FXWindowPadT};
    settings.pad_bottom_right = {(f32)FXWindowPadR, (f32)FXWindowPadB};
    imgui.BeginWindow(settings, r, "Effects");
    DEFER { imgui.EndWindow(); };
    DEFER { lay.Reset(); };

    LayID switches[k_num_effect_types];
    for (auto& s : switches)
        s = LAY_INVALID_ID;
    LayID switches_bottom_divider;
    DynamicArrayInline<EffectIDs, k_num_effect_types> effects;

    auto& dragging_fx_unit = g->dragging_fx_unit;

    //
    //
    //
    auto const root_width = (LayScalar)(imgui.Width());
    auto effects_root = lay.CreateRootItem(root_width, (LayScalar)imgui.Height(), LAY_COLUMN | LAY_START);

    int const switches_left_col_size = k_num_effect_types / 2 + (k_num_effect_types % 2);

    {
        auto switches_container = lay.CreateParentItem(effects_root, 1, 0, LAY_HFILL, LAY_ROW);
        lay.SetMargins(switches_container,
                       FXSwitchBoardMarginL,
                       FXSwitchBoardMarginT,
                       FXSwitchBoardMarginR,
                       FXSwitchBoardMarginB);

        auto left = lay.CreateParentItem(switches_container, 1, 0, LAY_HFILL, LAY_COLUMN);
        auto right = lay.CreateParentItem(switches_container, 1, 0, LAY_HFILL, LAY_COLUMN);

        for (auto const i : Range(k_num_effect_types)) {
            auto const parent = (i < switches_left_col_size) ? left : right;
            switches[i] = lay.CreateChildItem(parent,
                                              (root_width / 2) - FXSwitchBoardMarginL - FXSwitchBoardMarginR,
                                              FXSwitchBoardItemHeight,
                                              0);
        }
    }

    switches_bottom_divider = lay.CreateChildItem(effects_root, 1, 1, LAY_HFILL);
    lay.SetMargins(switches_bottom_divider, 0, 0, 0, FXDividerMarginB);

    auto heading_font = g->fira_sans;

    auto get_heading_size = [&](String name) {
        auto size = heading_font->CalcTextSizeA(heading_font->font_size_no_scale *
                                                    buttons::EffectHeading(0).text_scaling,
                                                FLT_MAX,
                                                0.0f,
                                                name);
        f32 const epsilon = 2;
        return f32x2 {Round(size.x + epsilon) + (f32)FXHeadingExtraWidth, (f32)FXHeadingH};
    };

    auto create_fx_ids = [&](Effect& fx, LayID* heading_container_out) {
        EffectIDs ids;
        ids.type = fx.type;
        ids.fx = &fx;

        auto master_heading_container =
            lay.CreateParentItem(effects_root, 1, 0, LAY_HFILL, LAY_ROW | LAY_START);

        auto const heading_size = get_heading_size(k_effect_info[ToInt(fx.type)].name);
        ids.heading = lay.CreateChildItem(master_heading_container,
                                          (LayScalar)heading_size.x,
                                          (LayScalar)heading_size.y,
                                          LAY_LEFT | LAY_TOP);
        lay.SetMargins(ids.heading, FXHeadingL, 0, FXHeadingR, 0);

        auto heading_container =
            lay.CreateParentItem(master_heading_container, 1, 0, LAY_HFILL, LAY_ROW | LAY_END);

        ids.close = lay.CreateChildItem(master_heading_container, FXCloseButtonWidth, FXCloseButtonHeight, 0);

        if (heading_container_out) *heading_container_out = heading_container;
        return ids;
    };

    auto create_divider_id = [&]() {
        auto result = lay.CreateChildItem(effects_root, 1, 1, LAY_HFILL);
        lay.SetMargins(result, 0, FXDividerMarginT, 0, FXDividerMarginB);
        return result;
    };

    auto create_param_container = [&]() {
        return lay.CreateParentItem(effects_root, 1, 0, LAY_HFILL, LAY_ROW | LAY_MIDDLE | LAY_WRAP);
    };

    auto create_subcontainer = [&](LayID parent) { return lay.CreateParentItem(parent, 0, 0, 0, LAY_ROW); };

    auto layout_all = [&](Span<LayIDPair> ids, Span<ParamIndex const> params) {
        auto param_container = create_param_container();

        Optional<LayID> container {};
        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_infos[ToInt(params[i])];
            LayID inner_container = param_container;
            if (info.grouping_within_module != 0) {
                if (!container || info.grouping_within_module != previous_group)
                    container = create_subcontainer(param_container);
                inner_container = *container;
                previous_group = info.grouping_within_module;
            }
            LayoutParameterComponent(g, inner_container, ids[i], plugin.processor.params[ToInt(params[i])]);
        }
    };

    auto ordered_effects = DecodeEffectsArray(plugin.processor.desired_effects_order.Load(),
                                              plugin.processor.effects_ordered_by_type);

    for (auto fx : ordered_effects) {
        if (!EffectIsOn(plugin.processor.params, fx)) continue;

        switch (fx->type) {
            case EffectType::Distortion: {
                auto ids = create_fx_ids(plugin.processor.distortion, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.distortion.type,
                                         plugin.processor.params[ToInt(ParamIndex::DistortionType)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.distortion.amount,
                                         plugin.processor.params[ToInt(ParamIndex::DistortionDrive)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::BitCrush: {
                auto ids = create_fx_ids(plugin.processor.bit_crush, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.bit_crush.bits,
                                         plugin.processor.params[ToInt(ParamIndex::BitCrushBits)]);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.bit_crush.sample_rate,
                                         plugin.processor.params[ToInt(ParamIndex::BitCrushBitRate)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.bit_crush.wet,
                                         plugin.processor.params[ToInt(ParamIndex::BitCrushWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.bit_crush.dry,
                                         plugin.processor.params[ToInt(ParamIndex::BitCrushDry)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Compressor: {
                LayID heading_container;
                auto ids = create_fx_ids(plugin.processor.compressor, &heading_container);
                auto param_container = create_param_container();

                ids.compressor.auto_gain =
                    lay.CreateChildItem(heading_container, FXCompressorAutoGainWidth, FXParamButtonHeight, 0);

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.threshold,
                                         plugin.processor.params[ToInt(ParamIndex::CompressorThreshold)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.ratio,
                                         plugin.processor.params[ToInt(ParamIndex::CompressorRatio)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.compressor.gain,
                                         plugin.processor.params[ToInt(ParamIndex::CompressorGain)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::FilterEffect: {
                auto ids = create_fx_ids(plugin.processor.filter_effect, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.type,
                                         plugin.processor.params[ToInt(ParamIndex::FilterType)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.cutoff,
                                         plugin.processor.params[ToInt(ParamIndex::FilterCutoff)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.filter.reso,
                                         plugin.processor.params[ToInt(ParamIndex::FilterResonance)]);
                ids.filter.using_gain =
                    plugin.processor.filter_effect.IsUsingGainParam(plugin.processor.params);
                if (ids.filter.using_gain) {
                    LayoutParameterComponent(g,
                                             param_container,
                                             ids.filter.gain,
                                             plugin.processor.params[ToInt(ParamIndex::FilterGain)]);
                }

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::StereoWiden: {
                auto ids = create_fx_ids(plugin.processor.stereo_widen, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.stereo.width,
                                         plugin.processor.params[ToInt(ParamIndex::StereoWidenWidth)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Chorus: {
                auto ids = create_fx_ids(plugin.processor.chorus, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.rate,
                                         plugin.processor.params[ToInt(ParamIndex::ChorusRate)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.highpass,
                                         plugin.processor.params[ToInt(ParamIndex::ChorusHighpass)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.chorus.depth,
                                         plugin.processor.params[ToInt(ParamIndex::ChorusDepth)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.wet,
                                         plugin.processor.params[ToInt(ParamIndex::ChorusWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.chorus.dry,
                                         plugin.processor.params[ToInt(ParamIndex::ChorusDry)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Reverb: {
                auto ids = create_fx_ids(plugin.processor.reverb, nullptr);

                layout_all(ids.reverb.ids, k_reverb_params);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::Phaser: {
                auto ids = create_fx_ids(plugin.processor.phaser, nullptr);

                layout_all(ids.new_phaser.ids, k_new_phaser_params);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::NewDelay: {
                LayID heading_container;
                auto ids = create_fx_ids(plugin.processor.new_delay, &heading_container);
                auto param_container = create_param_container();

                ids.new_delay.sync_btn =
                    lay.CreateChildItem(heading_container, FXDelaySyncBtnWidth, FXParamButtonHeight, 0);

                auto left = &plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedL)];
                auto right = &plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedR)];
                if (!plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncSwitch)].ValueAsBool()) {
                    left = &plugin.processor.params[ToInt(ParamIndex::NewDelayTimeLMs)];
                    right = &plugin.processor.params[ToInt(ParamIndex::NewDelayTimeRMs)];
                }
                LayoutParameterComponent(g, param_container, ids.new_delay.left, *left, nullopt, false, true);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.new_delay.right,
                                         *right,
                                         nullopt,
                                         false,
                                         true);
                {
                    LayoutParameterComponent(g,
                                             param_container,
                                             ids.new_delay.feedback,
                                             plugin.processor.params[ToInt(ParamIndex::NewDelayFeedback)]);
                }
                {
                    auto const id =
                        LayoutParameterComponent(g,
                                                 param_container,
                                                 ids.new_delay.mode,
                                                 plugin.processor.params[ToInt(ParamIndex::NewDelayMode)]);
                    lay_set_behave(&lay.ctx, id, LAY_BREAK);
                }
                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.new_delay.filter_cutoff,
                    plugin.processor.params[ToInt(ParamIndex::NewDelayFilterCutoffSemitones)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.new_delay.filter_spread,
                                         plugin.processor.params[ToInt(ParamIndex::NewDelayFilterSpread)]);
                LayoutParameterComponent(g,
                                         param_container,
                                         ids.new_delay.mix,
                                         plugin.processor.params[ToInt(ParamIndex::NewDelayMix)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
                break;
            }

            case EffectType::ConvolutionReverb: {
                auto ids = create_fx_ids(plugin.processor.convo, nullptr);
                auto param_container = create_param_container();

                LayoutParameterComponent(g,
                                         param_container,
                                         ids.convo.ir.control,
                                         ids.convo.ir.label,
                                         LayoutType::Effect,
                                         nullopt,
                                         true);

                LayoutParameterComponent(
                    g,
                    param_container,
                    ids.convo.highpass,
                    plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbHighpass)]);

                auto mix_container = create_subcontainer(param_container);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.convo.wet,
                                         plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbWet)]);
                LayoutParameterComponent(g,
                                         mix_container,
                                         ids.convo.dry,
                                         plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbDry)]);

                ids.divider = create_divider_id();
                dyn::Append(effects, ids);
            } break;

            case EffectType::Count: PanicIfReached(); break;
        }
    }

    //
    //
    //
    lay.PerformLayout();

    LayID closest_divider = LAY_INVALID_ID;
    if (dragging_fx_unit && imgui.HoveredWindow() == imgui.CurrentWindow()) {
        f32 const rel_y_pos = imgui.ScreenPosToWindowPos(imgui.platform->cursor_pos).y;
        f32 distance = Abs(lay.GetRect(switches_bottom_divider).y - rel_y_pos);
        closest_divider = switches_bottom_divider;
        usize closest_slot = 0;
        auto const original_slot = FindSlotInEffects(ordered_effects, dragging_fx_unit->fx);

        for (auto ids : effects) {
            if (f32 const d = Abs(lay.GetRect(ids.divider).y - rel_y_pos); d < distance) {
                distance = d;
                closest_divider = ids.divider;
                closest_slot = FindSlotInEffects(ordered_effects, ids.fx) + 1;
                if (closest_slot > original_slot) --closest_slot;
            }
        }

        ASSERT(closest_slot <= ordered_effects.size);
        if (dragging_fx_unit->drop_slot != closest_slot)
            imgui.platform->gui_update_requirements.requires_another_update = true;
        dragging_fx_unit->drop_slot = closest_slot;
    }

    auto const draw_divider = [&](LayID id) {
        auto const room_at_scroll_window_bottom = imgui.PointsToPixels(15);
        auto const line_r =
            imgui.GetRegisteredAndConvertedRect(lay.GetRect(id).WithH(room_at_scroll_window_bottom));
        imgui.graphics->AddLine(line_r.TopLeft(),
                                line_r.TopRight(),
                                (id == closest_divider) ? GMC(FXDividerLineDropZone) : GMC(FXDividerLine));
    };

    auto const draw_knob_joining_line = [&](LayID knob1, LayID knob2) {
        auto r1 = imgui.GetRegisteredAndConvertedRect(lay.GetRect(knob1));
        auto r2 = imgui.GetRegisteredAndConvertedRect(lay.GetRect(knob2));
        f32x2 const start {r1.Right() + FXKnobJoiningLinePadLR,
                           r1.CentreY() - FXKnobJoiningLineThickness / 2};
        f32x2 const end {r2.x - FXKnobJoiningLinePadLR, start.y};
        imgui.graphics->AddLine(start, end, GMC(FXKnobJoiningLine), (f32)FXKnobJoiningLineThickness);
    };

    auto const do_all_ids = [&](Span<LayIDPair> ids, Span<ParamIndex const> params, FXColours cols) {
        for (auto const i : Range(ids.size))
            KnobAndLabel(g,
                         plugin.processor.params[ToInt(params[i])],
                         ids[i],
                         knobs::DefaultKnob(cols.highlight));

        u8 previous_group = 0;
        for (auto const i : Range(ids.size)) {
            auto const& info = k_param_infos[ToInt(params[i])];
            if (info.grouping_within_module != 0) {
                if (info.grouping_within_module == previous_group)
                    draw_knob_joining_line(ids[i - 1].control, ids[i].control);
                previous_group = info.grouping_within_module;
            }
        }
    };

    draw_divider(switches_bottom_divider);

    for (auto ids : effects) {
        imgui.PushID((u64)ids.fx->type);
        DEFER { imgui.PopID(); };

        draw_divider(ids.divider);
        if (dragging_fx_unit && dragging_fx_unit->fx == ids.fx) continue;

        auto const do_heading = [&](Effect& fx, u32 col) {
            {
                auto const id = imgui.GetID("heading");
                auto const r = lay.GetRect(ids.heading);
                buttons::Button(g, id, r, k_effect_info[ToInt(fx.type)].name, buttons::EffectHeading(col));

                if (imgui.WasJustActivated(id)) {
                    dragging_fx_unit = DraggingFX {id, &fx, FindSlotInEffects(ordered_effects, &fx), {}};
                    imgui.platform->gui_update_requirements.requires_another_update = true;
                }

                if (imgui.IsHotOrActive(id))
                    g->gui_platform.gui_update_requirements.cursor_type = CursorType::AllArrows;
                Tooltip(g,
                        id,
                        r,
                        fmt::Format(g->scratch_arena, "{}", k_effect_info[ToInt(fx.type)].description));
            }

            {
                auto const close_id = imgui.GetID("close");
                auto const r = lay.GetRect(ids.close);
                if (buttons::Button(g,
                                    close_id,
                                    r,
                                    ICON_FA_TIMES,
                                    buttons::IconButton().WithIconScaling(0.7f))) {
                    SetParameterValue(plugin.processor, k_effect_info[ToInt(fx.type)].on_param_index, 0, {});
                }
                Tooltip(g,
                        close_id,
                        r,
                        fmt::Format(g->scratch_arena, "Remove {}", k_effect_info[ToInt(fx.type)].name));
            }
        };

        switch (ids.type) {
            case EffectType::Distortion: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.distortion, cols.back);
                auto& d = ids.distortion;

                buttons::PopupWithItems(g,
                                        plugin.processor.params[ToInt(ParamIndex::DistortionType)],
                                        d.type.control,
                                        buttons::ParameterPopupButton());
                labels::Label(g,
                              plugin.processor.params[ToInt(ParamIndex::DistortionType)],
                              d.type.label,
                              labels::ParameterCentred());

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::DistortionDrive)],
                             d.amount,
                             knobs::DefaultKnob(cols.highlight));
                break;
            }
            case EffectType::BitCrush: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.bit_crush, cols.back);
                auto& b = ids.bit_crush;

                draggers::Dragger(g,
                                  plugin.processor.params[ToInt(ParamIndex::BitCrushBits)],
                                  b.bits.control,
                                  draggers::DefaultStyle());
                labels::Label(g,
                              plugin.processor.params[ToInt(ParamIndex::BitCrushBits)],
                              b.bits.label,
                              labels::ParameterCentred());

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::BitCrushBitRate)],
                             b.sample_rate,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::BitCrushWet)],
                             b.wet,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::BitCrushDry)],
                             b.dry,
                             knobs::DefaultKnob(cols.highlight));

                draw_knob_joining_line(b.wet.control, b.dry.control);
                break;
            }
            case EffectType::Compressor: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.compressor, cols.back);
                auto& b = ids.compressor;

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::CompressorThreshold)],
                             b.threshold,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::CompressorRatio)],
                             b.ratio,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::CompressorGain)],
                             b.gain,
                             knobs::BidirectionalKnob(cols.highlight));

                buttons::Toggle(g,
                                plugin.processor.params[ToInt(ParamIndex::CompressorAutoGain)],
                                b.auto_gain,
                                buttons::ParameterToggleButton(cols.highlight));
                break;
            }
            case EffectType::FilterEffect: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.filter_effect, cols.back);
                auto& f = ids.filter;

                buttons::PopupWithItems(g,
                                        plugin.processor.params[ToInt(ParamIndex::FilterType)],
                                        f.type.control,
                                        buttons::ParameterPopupButton());
                labels::Label(g,
                              plugin.processor.params[ToInt(ParamIndex::FilterType)],
                              f.type.label,
                              labels::ParameterCentred());

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::FilterCutoff)],
                             f.cutoff,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::FilterResonance)],
                             f.reso,
                             knobs::DefaultKnob(cols.highlight));
                if (f.using_gain) {
                    KnobAndLabel(g,
                                 plugin.processor.params[ToInt(ParamIndex::FilterGain)],
                                 f.gain,
                                 knobs::DefaultKnob(cols.highlight));
                }
                break;
            }
            case EffectType::StereoWiden: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.stereo_widen, cols.back);
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::StereoWidenWidth)],
                             ids.stereo.width,
                             knobs::BidirectionalKnob(cols.highlight));
                break;
            }
            case EffectType::Chorus: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.chorus, cols.back);
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ChorusRate)],
                             ids.chorus.rate,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ChorusDepth)],
                             ids.chorus.depth,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ChorusHighpass)],
                             ids.chorus.highpass,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ChorusWet)],
                             ids.chorus.wet,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ChorusDry)],
                             ids.chorus.dry,
                             knobs::DefaultKnob(cols.highlight));
                draw_knob_joining_line(ids.chorus.wet.control, ids.chorus.dry.control);
                break;
            }

            case EffectType::Reverb: {
                auto const cols = GetFxCols(ids.type);
                do_heading(plugin.processor.reverb, cols.back);
                do_all_ids(ids.reverb.ids, k_reverb_params, cols);
                break;
            }

            case EffectType::Phaser: {
                auto const cols = GetFxCols(ids.type);
                do_heading(plugin.processor.phaser, cols.back);
                do_all_ids(ids.new_phaser.ids, k_new_phaser_params, cols);
                break;
            }

            case EffectType::NewDelay: {
                auto const cols = GetFxCols(ids.type);
                do_heading(plugin.processor.new_delay, cols.back);
                auto const knob_style = knobs::DefaultKnob(cols.highlight);

                if (plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncSwitch)].ValueAsBool()) {
                    buttons::PopupWithItems(g,
                                            plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedL)],
                                            ids.new_delay.left.control,
                                            buttons::ParameterPopupButton());
                    buttons::PopupWithItems(g,
                                            plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedR)],
                                            ids.new_delay.right.control,
                                            buttons::ParameterPopupButton());
                    labels::Label(g,
                                  plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedL)],
                                  ids.new_delay.left.label,
                                  labels::ParameterCentred());
                    labels::Label(g,
                                  plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncedR)],
                                  ids.new_delay.right.label,
                                  labels::ParameterCentred());
                } else {
                    KnobAndLabel(g,
                                 plugin.processor.params[ToInt(ParamIndex::NewDelayTimeLMs)],
                                 ids.new_delay.left,
                                 knob_style);
                    KnobAndLabel(g,
                                 plugin.processor.params[ToInt(ParamIndex::NewDelayTimeRMs)],
                                 ids.new_delay.right,
                                 knob_style);
                }
                draw_knob_joining_line(ids.new_delay.left.control, ids.new_delay.right.control);

                buttons::Toggle(g,
                                plugin.processor.params[ToInt(ParamIndex::NewDelayTimeSyncSwitch)],
                                ids.new_delay.sync_btn,
                                buttons::ParameterToggleButton(cols.highlight));

                buttons::PopupWithItems(g,
                                        plugin.processor.params[ToInt(ParamIndex::NewDelayMode)],
                                        ids.new_delay.mode.control,
                                        buttons::ParameterPopupButton());
                labels::Label(g,
                              plugin.processor.params[ToInt(ParamIndex::NewDelayMode)],
                              ids.new_delay.mode.label,
                              labels::ParameterCentred());

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::NewDelayFeedback)],
                             ids.new_delay.feedback,
                             knob_style);
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::NewDelayMix)],
                             ids.new_delay.mix,
                             knob_style);
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::NewDelayFilterCutoffSemitones)],
                             ids.new_delay.filter_cutoff,
                             knob_style);
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::NewDelayFilterSpread)],
                             ids.new_delay.filter_spread,
                             knob_style);
                draw_knob_joining_line(ids.new_delay.filter_cutoff.control,
                                       ids.new_delay.filter_spread.control);

                break;
            }

            case EffectType::ConvolutionReverb: {
                auto const cols = GetFxCols(ids.type);

                do_heading(plugin.processor.convo, cols.back);

                DoImpulseResponseMenu(g, ids.convo.ir.control);
                labels::Label(g, ids.convo.ir.label, "Impulse", labels::ParameterCentred());

                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbHighpass)],
                             ids.convo.highpass,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbWet)],
                             ids.convo.wet,
                             knobs::DefaultKnob(cols.highlight));
                KnobAndLabel(g,
                             plugin.processor.params[ToInt(ParamIndex::ConvolutionReverbDry)],
                             ids.convo.dry,
                             knobs::DefaultKnob(cols.highlight));

                draw_knob_joining_line(ids.convo.wet.control, ids.convo.dry.control);
                break;
            }
            case EffectType::Count: PanicIfReached(); break;
        }
    }

    if (dragging_fx_unit) {
        g->gui_platform.gui_update_requirements.cursor_type = CursorType::AllArrows;
        {
            auto style = buttons::EffectHeading(
                colours::ChangeBrightness(GetFxCols(dragging_fx_unit->fx->type).back | 0xff000000, 0.7f));
            style.draw_with_overlay_graphics = true;

            auto const text = k_effect_info[ToInt(dragging_fx_unit->fx->type)].name;
            Rect btn_r {g->gui_platform.cursor_pos, get_heading_size(text)};
            btn_r.pos += {btn_r.h, 0};
            buttons::FakeButton(g, btn_r, text, style);
        }

        {
            auto const space_around_cursor = 100.0f;
            Rect spacer_r;
            spacer_r.pos = g->gui_platform.cursor_pos;
            spacer_r.y -= space_around_cursor / 2;
            spacer_r.w = 1;
            spacer_r.h = space_around_cursor;

            auto wnd = imgui.CurrentWindow();
            if (!Rect::DoRectsIntersect(spacer_r, wnd->clipping_rect.ReducedVertically(spacer_r.h))) {
                bool const going_up = g->gui_platform.cursor_pos.y < wnd->clipping_rect.CentreY();

                auto const d = 100.0f * g->gui_platform.delta_time;
                imgui.RedrawAtIntervalSeconds(g->redraw_counter, 0.016);

                imgui.SetYScroll(wnd,
                                 Clamp(wnd->scroll_offset.y + (going_up ? -d : d), 0.0f, wnd->scroll_max.y));
            }
        }
    }

    bool effects_order_changed = false;

    if (dragging_fx_unit && imgui.WasJustDeactivated(dragging_fx_unit->id)) {
        MoveEffectToNewSlot(ordered_effects, dragging_fx_unit->fx, dragging_fx_unit->drop_slot);
        effects_order_changed = true;
        dragging_fx_unit.Clear();
    }

    {
        auto& dragging_fx = g->dragging_fx_switch;
        usize fx_index = 0;
        for (auto const slot : Range(k_num_effect_types)) {
            auto const whole_r = lay.GetRect(switches[slot]);
            auto const number_r = whole_r.WithW((f32)FXSwitchBoardNumberWidth);
            auto const slot_r = whole_r.CutLeft((f32)FXSwitchBoardNumberWidth);
            auto const converted_slot_r = imgui.GetRegisteredAndConvertedRect(slot_r);
            auto const grabber_r = slot_r.CutLeft(slot_r.w - (f32)FXSwitchBoardGrabRegionWidth);

            labels::Label(g, number_r, fmt::Format(g->scratch_arena, "{}", slot + 1), labels::Parameter());

            if (dragging_fx &&
                (imgui.platform->ContainsCursor(converted_slot_r) || dragging_fx->drop_slot == slot)) {
                if (dragging_fx->drop_slot != slot)
                    imgui.platform->gui_update_requirements.requires_another_update = true;
                dragging_fx->drop_slot = slot;
                imgui.graphics->AddRectFilled(converted_slot_r.Min(),
                                              converted_slot_r.Max(),
                                              GMC(FXButtonDropZone),
                                              (f32)CornerRounding);
            } else {
                auto fx = ordered_effects[fx_index++];
                if (dragging_fx && fx == dragging_fx->fx) fx = ordered_effects[fx_index++];

                auto style = buttons::ParameterToggleButton(GetFxCols(fx->type).button);
                style.no_tooltips = true;
                auto [changed, id] = buttons::Toggle(
                    g,
                    plugin.processor.params[ToInt(k_effect_info[ToInt(fx->type)].on_param_index)],
                    slot_r,
                    k_effect_info[ToInt(fx->type)].name,
                    style);

                {
                    auto grabber_style = buttons::EffectButtonGrabber();
                    if (imgui.IsHot(id)) grabber_style.main_cols.reg = grabber_style.main_cols.hot_on;
                    buttons::FakeButton(g, grabber_r, {}, grabber_style);

                    auto converted_grabber_r = imgui.GetRegisteredAndConvertedRect(grabber_r);
                    imgui.RegisterRegionForMouseTracking(&converted_grabber_r);

                    if (g->gui_platform.ContainsCursor(converted_grabber_r))
                        g->gui_platform.gui_update_requirements.cursor_type = CursorType::AllArrows;
                }

                if (imgui.IsActive(id) && !dragging_fx) {
                    auto const click_pos = g->gui_platform.last_mouse_down_point[0];
                    auto const current_pos = g->gui_platform.cursor_pos;
                    auto const delta = current_pos - click_pos;

                    constexpr f32 k_wiggle_room = 3;
                    if (Sqrt(delta.x * delta.x + delta.y * delta.y) > k_wiggle_room) {
                        dragging_fx =
                            DraggingFX {id, fx, slot, g->gui_platform.cursor_pos - converted_slot_r.pos};
                    }
                }
            }
        }

        if (dragging_fx) {
            auto style = buttons::ParameterToggleButton(GetFxCols(dragging_fx->fx->type).button);
            style.draw_with_overlay_graphics = true;

            Rect btn_r {lay.GetRect(switches[0])};
            btn_r.pos = imgui.platform->cursor_pos - dragging_fx->relative_grab_point;

            auto active_fx = dragging_fx->fx;
            buttons::FakeButton(g,
                                btn_r,
                                k_effect_info[ToInt(active_fx->type)].name,
                                EffectIsOn(plugin.processor.params, active_fx),
                                style);
            g->gui_platform.gui_update_requirements.cursor_type = CursorType::AllArrows;
        }

        if (dragging_fx && imgui.WasJustDeactivated(dragging_fx->id)) {
            MoveEffectToNewSlot(ordered_effects, dragging_fx->fx, dragging_fx->drop_slot);
            effects_order_changed = true;
            dragging_fx.Clear();
        }
    }

    if (effects_order_changed) {
        plugin.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects));
        plugin.processor.events_for_audio_thread.Push(EventForAudioThreadType::FxOrderChanged);
    }
}
