// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/loop_modes.hpp"
#include "gui2_inst_browser.hpp"
#include "gui_curve_map.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_envelope.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_library_images.hpp"
#include "gui_state.hpp"
#include "gui_viewport_utils.hpp"
#include "gui_waveform.hpp"
#include "old/gui_button_widgets.hpp"
#include "old/gui_dragger_widgets.hpp"
#include "old/gui_label_widgets.hpp"
#include "old/gui_menu.hpp"
#include "old/gui_widget_compounds.hpp"
#include "old/gui_widget_helpers.hpp"
#include "processor/layer_processor.hpp"

// TODO: this code needs entirely updating to use GuiBuilder, and no includes of old/* code.

namespace layer_gui {

static void DoInstSelectorRightClickMenu(GuiState& g, Rect r, u32 layer) {
    auto& imgui = g.imgui;
    auto layer_obj = &g.engine.Layer(layer);
    auto const popup_id = imgui.MakeId("inst selector popup");
    auto const right_clicker_id = imgui.MakeId("inst selector right clicker");

    r = imgui.RegisterAndConvertRect(r);
    imgui.PopupMenuButtonBehaviour(r,
                                   right_clicker_id,
                                   popup_id,
                                   {.mouse_button = MouseButton::Right, .event = MouseButtonEvent::Up});

    if (imgui.IsPopupMenuOpen(popup_id)) {
        g.imgui.BeginViewport(FloeMenuConfig(g.imgui), popup_id, r);
        DEFER { imgui.EndViewport(); };

        auto const items = Array {"Unload instrument"_s};

        PopupMenuItems menu(g, items);

        StartFloeMenu(g);
        DEFER { EndFloeMenu(g); };

        if (layer_obj->instrument_id.tag == InstrumentType::None)
            menu.DoFakeButton(items[0]);
        else if (menu.DoButton(items[0]))
            LoadInstrument(g.engine, layer, InstrumentType::None);
    }
}

static void DoInstSelectorGUI(GuiState& g, Rect r, u32 layer) {
    g.imgui.PushId("inst selector");
    DEFER { g.imgui.PopId(); };
    auto const imgui_id = g.imgui.MakeId((u64)layer);

    auto layer_obj = &g.engine.Layer(layer);
    auto const inst_name = layer_obj->InstName();

    Optional<TextureHandle> icon_tex {};
    if (layer_obj->instrument_id.tag == InstrumentType::Sampler) {
        auto sample_inst_id = layer_obj->instrument_id.Get<sample_lib::InstrumentId>();
        auto imgs = GetLibraryImages(g.library_images,
                                     g.imgui,
                                     sample_inst_id.library,
                                     g.shared_engine_systems.sample_library_server,
                                     LibraryImagesTypes::Icon);

        if (imgs.icon) icon_tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.icon);
    }

    DoInstSelectorRightClickMenu(g, r, layer);

    if (buttons::Button(g, imgui_id, r, inst_name, buttons::InstSelectorPopupButton(g.imgui, icon_tex))) {
        g.imgui.OpenModalViewport(g.inst_browser_state[layer].id);
        g.inst_browser_state[layer].common_state.absolute_button_rect = g.imgui.ViewportRectToWindowRect(r);
    }

    Tooltip(
        g,
        imgui_id,
        g.imgui.ViewportRectToWindowRect(r),
        ({
            String s {};
            switch (layer_obj->instrument.tag) {
                case InstrumentType::None: s = "Select the instrument for this layer"_s; break;
                case InstrumentType::WaveformSynth:
                    s = fmt::Format(g.scratch_arena,
                                    "Current instrument: {}\nChange or remove the instrument for this layer",
                                    inst_name);
                    break;
                case InstrumentType::Sampler: {
                    auto const& sample = layer_obj->instrument.GetFromTag<InstrumentType::Sampler>();
                    s = fmt::Format(
                        g.scratch_arena,
                        "Change or remove the instrument for this layer\n\nCurrent instrument: {} from {} by {}.{}{}",
                        inst_name,
                        sample->instrument.library.name,
                        sample->instrument.library.author,
                        sample->instrument.description ? "\n\n" : "",
                        sample->instrument.description ? sample->instrument.description : "");
                    break;
                }
            }
            s;
        }),
        {});
}

static void DoLoopModeSelectorGui(GuiState& g, Rect r, LayerProcessor& layer) {
    g.imgui.PushId("loop mode selector");
    DEFER { g.imgui.PopId(); };
    auto& params = g.engine.processor.main_params;
    auto const param = params.DescribedValue(layer.index, LayerParamIndex::LoopMode);
    auto const desired_loop_mode = param.IntValue<param_values::LoopMode>();

    auto const vol_env_on = layer.VolumeEnvelopeIsOn(params);
    auto const actual_loop_behaviour = ActualLoopBehaviour(layer.instrument, desired_loop_mode, vol_env_on);
    auto const default_loop_behaviour =
        ActualLoopBehaviour(layer.instrument, param_values::LoopMode::InstrumentDefault, vol_env_on);
    DynamicArrayBounded<char, 64> default_mode_str {"Default: "};
    dyn::AppendSpan(default_mode_str, default_loop_behaviour.value.name);

    auto const imgui_id = BeginParameterGUI(g, param, r);

    Optional<f32> val {};

    auto const style = buttons::ParameterPopupButton(g.imgui);

    // Draw around the whole thing, not just the menu.
    if (style.back_cols.reg) {
        auto const converted_r = g.imgui.RegisterAndConvertRect(r);
        g.imgui.draw_list->AddRectFilled(converted_r,
                                         style.back_cols.reg,
                                         LiveSize(UiSizeId::CornerRounding));
    }

    auto const btn_w = LiveSize(UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveSize(UiSizeId::ParamIntButtonMarginR);
    rect_cut::CutRight(r, margin_r);
    auto rect_r = rect_cut::CutRight(r, btn_w);
    auto rect_l = rect_cut::CutRight(r, btn_w);

    auto popup_style = style;
    popup_style.back_cols = {};
    if (buttons::Popup(g, imgui_id, imgui_id + 1, r, actual_loop_behaviour.value.short_name, popup_style)) {
        StartFloeMenu(g);
        DEFER { EndFloeMenu(g); };
        DEFER { g.imgui.EndViewport(); };

        auto items = param_values::k_loop_mode_strings;

        items[ToInt(param_values::LoopMode::InstrumentDefault)] = default_mode_str;

        auto const w = MenuItemWidth(g, items);
        auto const h = LiveSize(UiSizeId::MenuItemHeight);

        for (auto const i : Range<u32>(items.size)) {
            bool state = i == ToInt(desired_loop_mode);
            auto const behaviour =
                ActualLoopBehaviour(layer.instrument, (param_values::LoopMode)i, vol_env_on);
            auto const valid = behaviour.is_desired;
            Rect const item_rect = {.xywh {0, h * (f32)i, w, h}};
            auto const item_id = g.imgui.MakeId((uintptr)i);

            if (buttons::Toggle(g,
                                item_id,
                                item_rect,
                                state,
                                items[i],
                                buttons::MenuItem(g.imgui, true, !valid)) &&
                i != ToInt(desired_loop_mode))
                val = (f32)i;

            {
                DynamicArray<char> tooltip {g.scratch_arena};

                if (!valid) fmt::Append(tooltip, ICON_FA_REPEAT "Not available: {}\n\n", behaviour.reason);

                dyn::AppendSpan(tooltip, LoopModeDescription((param_values::LoopMode)i));

                if (i == ToInt(param_values::LoopMode::InstrumentDefault)) {
                    fmt::Append(tooltip, "\n\n{}'s default behaviour: \n", layer.InstName());
                    dyn::AppendSpan(tooltip, default_loop_behaviour.value.description);
                    if (auto const reason = default_loop_behaviour.reason; reason.size) {
                        dyn::Append(tooltip, ' ');
                        dyn::AppendSpan(tooltip, reason);
                    }
                }

                Tooltip(g, item_id, g.imgui.ViewportRectToWindowRect(item_rect), tooltip, {});
            }
        }
    }

    {
        auto current = param.LinearValue();
        if (g.imgui.SliderBehaviourRange({
                .rect_in_window_coords = g.imgui.RegisterAndConvertRect(r),
                .id = imgui_id,
                .min = param.info.linear_range.min,
                .max = param.info.linear_range.max,
                .value = current,
                .default_value = param.info.default_linear_value,
                .cfg =
                    {
                        .sensitivity = 20,
                        .slower_with_shift = true,
                        .default_on_modifer = true,
                    },
            })) {
            val = current;
        }
    }

    auto const button_style = buttons::IconButton(g.imgui);
    auto const left_id = imgui_id - 4;
    auto const right_id = imgui_id + 4;

    auto increment_mode = [&](f32 step) {
        auto new_val = (f32)param.IntValue<int>() + step;
        for (auto _ : Range(ToInt(param_values::LoopMode::Count))) {
            if (step < 0 && new_val < param.info.linear_range.min) new_val = param.info.linear_range.max;
            if (step > 0 && new_val > param.info.linear_range.max) new_val = param.info.linear_range.min;

            auto const mode = (param_values::LoopMode)new_val;
            if (mode != param_values::LoopMode::InstrumentDefault) {
                // We only increment to a value is valid, and not the same as the current value. This feels
                // the most intuitive otherwise it feels like the button doesn't do anything.
                if (auto const other = ActualLoopBehaviour(layer.instrument, mode, vol_env_on);
                    other.is_desired && other.value.id != actual_loop_behaviour.value.id) {
                    val = new_val;
                    break;
                }
            }

            new_val += step;
        }
    };

    if (buttons::Button(g, left_id, rect_l, ICON_FA_CARET_LEFT, button_style)) increment_mode(-1);
    if (buttons::Button(g, right_id, rect_r, ICON_FA_CARET_RIGHT, button_style)) increment_mode(1);
    Tooltip(g, left_id, g.imgui.ViewportRectToWindowRect(rect_l), "Previous loop mode"_s, {});
    Tooltip(g, right_id, g.imgui.ViewportRectToWindowRect(rect_r), "Next loop mode"_s, {});

    EndParameterGUI(g,
                    imgui_id,
                    param,
                    r,
                    val,
                    (ParamDisplayFlags)(ParamDisplayFlagsNoTooltip | ParamDisplayFlagsNoValuePopup));

    Tooltip(g,
            imgui_id,
            g.imgui.ViewportRectToWindowRect(r),
            fmt::Format(g.scratch_arena,
                        "{}: {}\n\n{} {}",
                        param.info.name,
                        actual_loop_behaviour.value.name,
                        actual_loop_behaviour.value.description,
                        actual_loop_behaviour.reason),
            {});
}

static String GetPageTitle(PageType type) {
    switch (type) {
        case PageType::Main: return "Main";
        case PageType::Eq: return "EQ";
        case PageType::Play: return "Play";
        case PageType::Lfo: return "LFO";
        case PageType::Filter: return "Filter";
        case PageType::Count: PanicIfReached();
    }
    return "";
}

void Layout(GuiState& g,
            LayerProcessor* layer,
            LayerLayoutTempIDs& c,
            LayerLayout* layer_gui,
            f32 width,
            f32 height) {
    using enum UiSizeId;
    auto const param_popup_button_height = LiveSize(ParamPopupButtonHeight);
    auto const page_heading_height = LiveSize(PageHeadingHeight);

    auto& params = g.engine.processor.main_params;

    auto container = layout::CreateItem(g.layout,
                                        g.scratch_arena,
                                        {
                                            .size = {width, height},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        });

    // selector
    {

        c.selector_box =
            layout::CreateItem(g.layout,
                               g.scratch_arena,
                               {
                                   .parent = container,
                                   .size = {layout::k_fill_parent, LiveSize(LayerSelectorBoxHeight)},
                                   .margins = {.l = LiveSize(LayerSelectorBoxMarginL),
                                               .r = LiveSize(LayerSelectorBoxMarginR),
                                               .t = LiveSize(LayerSelectorBoxMarginT),
                                               .b = LiveSize(LayerSelectorBoxMarginB)},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                               });

        c.selector_menu = layout::CreateItem(g.layout,
                                             g.scratch_arena,
                                             {
                                                 .parent = c.selector_box,
                                                 .size = layout::k_fill_parent,
                                             });

        auto const layer_selector_button_w = LiveSize(ResourceSelectorRandomButtonW);
        auto const layer_selector_lr_button_w = LiveSize(UiSizeId::NextPrevButtonSize);
        auto const layer_selector_box_buttons_margin_r = LiveSize(LayerSelectorBoxButtonsMarginR);

        c.selector_l = layout::CreateItem(g.layout,
                                          g.scratch_arena,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_lr_button_w, layout::k_fill_parent},
                                          });
        c.selector_r = layout::CreateItem(g.layout,
                                          g.scratch_arena,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_lr_button_w, layout::k_fill_parent},
                                          });
        c.selector_randomise =
            layout::CreateItem(g.layout,
                               g.scratch_arena,
                               {
                                   .parent = c.selector_box,
                                   .size = {layer_selector_button_w, layout::k_fill_parent},
                                   .margins = {.r = layer_selector_box_buttons_margin_r},
                               });
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // mixer container 1
    {
        auto subcontainer_1 = layout::CreateItem(g.layout,
                                                 g.scratch_arena,
                                                 {
                                                     .parent = container,
                                                     .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                     .margins {
                                                         .l = LiveSize(LayerMixerContainer1MarginL),
                                                         .r = LiveSize(LayerMixerContainer1MarginR),
                                                         .t = LiveSize(LayerMixerContainer1MarginT),
                                                         .b = LiveSize(LayerMixerContainer1MarginB),
                                                     },
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::Alignment::Middle,
                                                 });

        c.volume = layout::CreateItem(g.layout,
                                      g.scratch_arena,
                                      {
                                          .parent = subcontainer_1,
                                          .size = LiveSize(LayerVolumeKnobSize),
                                          .margins = {.r = LiveSize(LayerVolumeKnobMarginR)},
                                      });

        c.mute_solo =
            layout::CreateItem(g.layout,
                               g.scratch_arena,
                               {
                                   .parent = subcontainer_1,
                                   .size = {LiveSize(LayerMuteSoloWidth), LiveSize(LayerMuteSoloHeight)},
                                   .margins {
                                       .l = LiveSize(LayerMuteSoloMarginL),
                                       .r = LiveSize(LayerMuteSoloMarginR),
                                       .t = LiveSize(LayerMuteSoloMarginT),
                                       .b = LiveSize(LayerMuteSoloMarginB),
                                   },
                               });
    }

    // mixer container 2
    {
        auto subcontainer_2 = layout::CreateItem(g.layout,
                                                 g.scratch_arena,
                                                 {
                                                     .parent = container,
                                                     .size = layout::k_hug_contents,
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::Alignment::Middle,
                                                 });
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob1,
                                 params.DescribedValue(layer->index, LayerParamIndex::TuneSemitone),
                                 LayerPitchMarginLR);
        layout::SetSize(g.layout,
                        c.knob1.control,
                        f32x2 {
                            LiveSize(LayerPitchWidth),
                            LiveSize(LayerPitchHeight),
                        });
        layout::SetMargins(g.layout,
                           c.knob1.control,
                           {
                               .t = LiveSize(LayerPitchMarginT),
                               .b = LiveSize(LayerPitchMarginB),
                           });

        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob2,
                                 params.DescribedValue(layer->index, LayerParamIndex::TuneCents),
                                 LayerMixerKnobGapX);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob3,
                                 params.DescribedValue(layer->index, LayerParamIndex::Pan),
                                 LayerMixerKnobGapX);
    }

    auto const layer_mixer_divider_vert_margins = LiveSize(LayerMixerDividerVertMargins);
    // divider
    c.divider = layout::CreateItem(g.layout,
                                   g.scratch_arena,
                                   {
                                       .parent = container,
                                       .size = {layout::k_fill_parent, 1},
                                       .margins = {.tb = layer_mixer_divider_vert_margins},
                                   });

    // tabs
    {
        auto tab_lay =
            layout::CreateItem(g.layout,
                               g.scratch_arena,
                               {
                                   .parent = container,
                                   .size = {layout::k_fill_parent, LiveSize(LayerParamsGroupTabsH)},
                                   .margins = {.lr = LiveSize(LayerParamsGroupBoxGapX)},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                               });

        auto const layer_params_group_tabs_gap = LiveSize(LayerParamsGroupTabsGap);
        for (auto const i : Range(k_num_pages)) {
            auto const page_type = (PageType)i;
            auto size = g.fonts.Current()->CalcTextSize(GetPageTitle(page_type), {}).x;

            if (page_type == PageType::Filter || page_type == PageType::Lfo || page_type == PageType::Eq)
                size += LiveSize(LayerParamsGroupTabsIconW2);
            c.tabs[i] =
                layout::CreateItem(g.layout,
                                   g.scratch_arena,
                                   {
                                       .parent = tab_lay,
                                       .size = {size + layer_params_group_tabs_gap, layout::k_fill_parent},
                                   });
        }
    }

    // divider2
    {
        c.divider2 = layout::CreateItem(g.layout,
                                        g.scratch_arena,
                                        {
                                            .parent = container,
                                            .size = {layout::k_fill_parent, 1},
                                            .margins = {.tb = layer_mixer_divider_vert_margins},
                                        });
    }

    {
        auto const page_heading_margin_l = LiveSize(PageHeadingMarginL);
        auto const page_heading_margin_t = LiveSize(PageHeadingMarginT);
        auto const page_heading_margin_b = LiveSize(PageHeadingMarginB);
        auto const heading_margins = Margins {
            .l = page_heading_margin_l,
            .r = 0,
            .t = page_heading_margin_t,
            .b = page_heading_margin_b,
        };

        auto page_container = layout::CreateItem(g.layout,
                                                 g.scratch_arena,
                                                 {
                                                     .parent = container,
                                                     .size = layout::k_fill_parent,
                                                     .contents_direction = layout::Direction::Column,
                                                     .contents_align = layout::Alignment::Start,
                                                 });

        auto const main_envelope_h = LiveSize(MainEnvelopeH);

        switch (layer_gui->selected_page) {
            case PageType::Main: {
                auto const waveform_margins_lr = LiveSize(MainWaveformMarginLR);
                c.main.waveform =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, LiveSize(MainWaveformH)},
                                           .margins =
                                               {
                                                   .lr = waveform_margins_lr,
                                                   .tb = LiveSize(MainWaveformMarginTB),
                                               },
                                       });

                c.main.waveform_label =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, LiveSize(MainWaveformLabelH)},
                                           .margins = {.lr = waveform_margins_lr},
                                       });

                auto const main_item_margin_l = LiveSize(MainItemMarginL);
                auto const main_item_margin_r = LiveSize(MainItemMarginR);
                auto const main_item_height = LiveSize(MainItemHeight);
                auto const main_item_gap_y = LiveSize(MainItemGapY);
                auto btn_container =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.l = main_item_margin_l, .r = main_item_margin_r},
                                           .contents_direction = layout::Direction::Row,
                                       });
                c.main.reverse =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = btn_container,
                                           .size = {LiveSize(MainReverseButtonWidth), main_item_height},
                                           .margins = {.tb = main_item_gap_y},
                                       });
                c.main.loop_mode =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = btn_container,
                                           .size = {layout::k_fill_parent, param_popup_button_height},
                                           .margins = {.tb = main_item_gap_y},
                                       });

                auto const main_divider_margin_t = LiveSize(MainDividerMarginT);
                auto const main_divider_margin_b = LiveSize(MainDividerMarginB);
                c.main.divider = layout::CreateItem(
                    g.layout,
                    g.scratch_arena,
                    {
                        .parent = page_container,
                        .size = {layout::k_fill_parent, 1},
                        .margins = {.t = main_divider_margin_t, .b = main_divider_margin_b},
                    });

                c.main.env_on = layout::CreateItem(g.layout,
                                                   g.scratch_arena,
                                                   {
                                                       .parent = page_container,
                                                       .size = {layout::k_fill_parent, page_heading_height},
                                                       .margins = ({
                                                           auto m = heading_margins;
                                                           m.b = 0;
                                                           m;
                                                       }),
                                                   });

                c.main.envelope = layout::CreateItem(g.layout,
                                                     g.scratch_arena,
                                                     {
                                                         .parent = page_container,
                                                         .size = {layout::k_fill_parent, main_envelope_h},
                                                         .margins {
                                                             .lr = LiveSize(MainEnvelopeMarginLR),
                                                             .tb = LiveSize(MainEnvelopeMarginTB),
                                                         },
                                                     });
                break;
            }
            case PageType::Filter: {
                auto const filter_gap_y_before_knobs = LiveSize(FilterGapYBeforeKnobs);

                auto filter_heading_container =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins {.b = filter_gap_y_before_knobs},
                                           .contents_direction = layout::Direction::Row,
                                       });
                c.filter.filter_on =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = filter_heading_container,
                                           .size = {LiveSize(FilterOnWidth), page_heading_height},
                                           .margins = heading_margins,
                                           .anchor = layout::Anchor::Top,
                                       });
                c.filter.filter_type =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = filter_heading_container,
                                           .size = {layout::k_fill_parent, param_popup_button_height},
                                           .margins = {.lr = page_heading_margin_l},
                                       });

                auto filter_knobs_container =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Middle,
                                       });
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.cutoff,
                                         params.DescribedValue(layer->index, LayerParamIndex::FilterCutoff),
                                         Page3KnobGapX);
                LayoutParameterComponent(
                    g,
                    filter_knobs_container,
                    c.filter.reso,
                    params.DescribedValue(layer->index, LayerParamIndex::FilterResonance),
                    Page3KnobGapX);
                LayoutParameterComponent(
                    g,
                    filter_knobs_container,
                    c.filter.env_amount,
                    params.DescribedValue(layer->index, LayerParamIndex::FilterEnvAmount),
                    Page3KnobGapX);

                c.filter.envelope = layout::CreateItem(g.layout,
                                                       g.scratch_arena,
                                                       {
                                                           .parent = page_container,
                                                           .size = {layout::k_fill_parent, main_envelope_h},
                                                           .margins {
                                                               .lr = LiveSize(FilterEnvelopeMarginLR),
                                                               .tb = LiveSize(FilterEnvelopeMarginTB),
                                                           },
                                                       });
                break;
            }
            case PageType::Eq: {
                c.eq.on = layout::CreateItem(g.layout,
                                             g.scratch_arena,
                                             {
                                                 .parent = page_container,
                                                 .size = {layout::k_fill_parent, page_heading_height},
                                                 .margins = heading_margins,
                                             });

                auto const eq_band_gap_y = LiveSize(EqBandGapY);
                {
                    c.eq.type[0] =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, param_popup_button_height},
                                               .margins {
                                                   .lr = page_heading_margin_l,
                                                   .tb = eq_band_gap_y,
                                               },
                                           });

                    auto knob_container =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                           });
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[0],
                                             params.DescribedValue(layer->index, LayerParamIndex::EqFreq1),
                                             Page3KnobGapX);
                    LayoutParameterComponent(
                        g,
                        knob_container,
                        c.eq.reso[0],
                        params.DescribedValue(layer->index, LayerParamIndex::EqResonance1),
                        Page3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[0],
                                             params.DescribedValue(layer->index, LayerParamIndex::EqGain1),
                                             Page3KnobGapX);
                    layout::SetMargins(g.layout, knob_container, {.b = eq_band_gap_y});
                }

                {
                    c.eq.type[1] =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, param_popup_button_height},
                                               .margins {
                                                   .lr = page_heading_margin_l,
                                                   .tb = eq_band_gap_y,
                                               },
                                           });
                    auto knob_container =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                           });
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[1],
                                             params.DescribedValue(layer->index, LayerParamIndex::EqFreq2),
                                             Page3KnobGapX);
                    LayoutParameterComponent(
                        g,
                        knob_container,
                        c.eq.reso[1],
                        params.DescribedValue(layer->index, LayerParamIndex::EqResonance2),
                        Page3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[1],
                                             params.DescribedValue(layer->index, LayerParamIndex::EqGain2),
                                             Page3KnobGapX);
                }

                break;
            }
            case PageType::Play: {
                auto const midi_item_height = LiveSize(MidiItemHeight);
                auto const midi_item_width = LiveSize(MidiItemWidth);
                auto const midi_item_margin_lr = LiveSize(MidiItemMarginLR);
                auto const midi_item_gap_y = LiveSize(MidiItemGapY);

                auto layout_item = [&](layout::Id& control, layout::Id& name, f32 height) {
                    auto parent =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,

                                           });
                    control = layout::CreateItem(g.layout,
                                                 g.scratch_arena,
                                                 {
                                                     .parent = parent,
                                                     .size = {midi_item_width, height},
                                                     .margins {
                                                         .lr = midi_item_margin_lr,
                                                         .tb = midi_item_gap_y,
                                                     },
                                                 });
                    name = layout::CreateItem(g.layout,
                                              g.scratch_arena,
                                              {
                                                  .parent = parent,
                                                  .size = {layout::k_fill_parent, height},
                                              });
                };

                layout_item(c.play.transpose, c.play.transpose_name, midi_item_height);

                layout_item(c.play.pitchbend, c.play.pitchbend_name, midi_item_height);

                auto const button_options = layout::ItemOptions {
                    .parent = page_container,
                    .size = {layout::k_fill_parent, midi_item_height},
                    .margins {
                        .lr = midi_item_margin_lr,
                        .tb = midi_item_gap_y,
                    },
                };
                c.play.keytrack = layout::CreateItem(g.layout, g.scratch_arena, button_options);

                layout_item(c.play.mono, c.play.mono_name, midi_item_height);

                {
                    {
                        auto const row =
                            layout::CreateItem(g.layout,
                                               g.scratch_arena,
                                               {
                                                   .parent = page_container,
                                                   .size = {layout::k_fill_parent, midi_item_height},
                                                   .margins {
                                                       .lr = midi_item_margin_lr,
                                                       .tb = midi_item_gap_y,
                                                   },
                                                   .contents_gap = GuiIo().WwToPixels(4.0f),
                                                   .contents_direction = layout::Direction::Row,
                                               });
                        auto const item_options = layout::ItemOptions {
                            .parent = row,
                            .size = layout::k_fill_parent,
                        };
                        c.play.key_range_text = layout::CreateItem(g.layout, g.scratch_arena, item_options);
                        c.play.key_range_low = layout::CreateItem(g.layout, g.scratch_arena, item_options);
                        c.play.key_range_high = layout::CreateItem(g.layout, g.scratch_arena, item_options);
                    }
                    {
                        auto const row =
                            layout::CreateItem(g.layout,
                                               g.scratch_arena,
                                               {
                                                   .parent = page_container,
                                                   .size = {layout::k_fill_parent, midi_item_height},
                                                   .margins {
                                                       .lr = midi_item_margin_lr,
                                                       .tb = midi_item_gap_y,
                                                   },
                                                   .contents_gap = GuiIo().WwToPixels(4.0f),
                                                   .contents_direction = layout::Direction::Row,
                                               });
                        auto const item_options = layout::ItemOptions {
                            .parent = row,
                            .size = layout::k_fill_parent,
                        };
                        c.play.key_range_fade_text =
                            layout::CreateItem(g.layout, g.scratch_arena, item_options);
                        c.play.key_range_low_fade =
                            layout::CreateItem(g.layout, g.scratch_arena, item_options);
                        c.play.key_range_high_fade =
                            layout::CreateItem(g.layout, g.scratch_arena, item_options);
                    }
                }

                c.play.velo_name =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, midi_item_height},
                                           .margins = {.lr = midi_item_margin_lr, .tb = midi_item_gap_y},
                                       });
                c.play.velo_graph =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, LiveSize(MidiVeloGraphHeight)},
                                           .margins = {.lr = midi_item_margin_lr},
                                       });
                break;
            }
            case PageType::Lfo: {
                c.lfo.on = layout::CreateItem(g.layout,
                                              g.scratch_arena,
                                              {
                                                  .parent = page_container,
                                                  .size = {layout::k_fill_parent, page_heading_height},
                                                  .margins = heading_margins,
                                              });
                auto const layout_menu_and_label = [&](layout::Id& control, layout::Id& name) {
                    auto parent =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                           });
                    control =
                        layout::CreateItem(g.layout,
                                           g.scratch_arena,
                                           {
                                               .parent = parent,
                                               .size = {LiveSize(LfoItemWidth), param_popup_button_height},
                                               .margins {
                                                   .l = LiveSize(LfoItemMarginL),
                                                   .r = LiveSize(LfoItemMarginR),
                                                   .tb = LiveSize(LfoItemGapY),
                                               },
                                           });
                    name = layout::CreateItem(g.layout,
                                              g.scratch_arena,
                                              {
                                                  .parent = parent,
                                                  .size = {layout::k_fill_parent, param_popup_button_height},
                                              });
                };

                layout_menu_and_label(c.lfo.target, c.lfo.target_name);
                layout_menu_and_label(c.lfo.shape, c.lfo.shape_name);
                layout_menu_and_label(c.lfo.mode, c.lfo.mode_name);

                auto knob_container =
                    layout::CreateItem(g.layout,
                                       g.scratch_arena,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.t = LiveSize(LfoGapYBeforeKnobs)},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Middle,
                                       });

                LayoutParameterComponent(g,
                                         knob_container,
                                         c.lfo.amount,
                                         params.DescribedValue(layer->index, LayerParamIndex::LfoAmount),
                                         Page2KnobGapX);

                LayoutParameterComponent(
                    g,
                    knob_container,
                    c.lfo.rate,
                    params.DescribedValue(layer->index,
                                          params.BoolValue(layer->index, LayerParamIndex::LfoSyncSwitch)
                                              ? LayerParamIndex::LfoRateTempoSynced
                                              : LayerParamIndex::LfoRateHz),
                    Page2KnobGapX,
                    true);
                break;
            }
            case PageType::Count: PanicIfReached();
        }
    }
}

static void DrawSelectorProgressBar(imgui::Context const& imgui, Rect r, f32 load_percent) {
    auto min = r.Min();
    auto max = f32x2 {r.x + Max(4.0f, r.w * load_percent), r.Bottom()};
    auto col = LiveCol(UiColMap::LayerSelectorMenuLoading);
    auto const rounding = LiveSize(UiSizeId::CornerRounding);
    imgui.draw_list->AddRectFilled(min, max, col, rounding);
}

void Draw(GuiState& g,
          GuiFrameContext const& frame_context,
          Rect r,
          LayerProcessor* layer,
          LayerLayoutTempIDs& c,
          LayerLayout* layer_gui) {
    using enum UiSizeId;

    auto& params = g.engine.processor.main_params;

    auto settings = FloeStandardConfig(g.imgui, {});
    settings.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
    g.imgui.BeginViewport(settings, g.imgui.MakeId((uintptr)layer), r);
    DEFER { g.imgui.EndViewport(); };

    auto const draw_divider = [&](layout::Id id) {
        auto line_r = layout::GetRect(g.layout, id);
        line_r = g.imgui.RegisterAndConvertRect(line_r);
        g.imgui.draw_list->AddLine({line_r.x, line_r.Bottom()},
                                   {line_r.Right(), line_r.Bottom()},
                                   LiveCol(UiColMap::LayerDividerLine));
    };

    // Inst selector
    {
        auto selector_left_id = g.imgui.MakeId("SelcL");
        auto selector_right_id = g.imgui.MakeId("SelcR");
        auto selector_menu_r = layout::GetRect(g.layout, c.selector_menu);
        auto selector_left_r = layout::GetRect(g.layout, c.selector_l);
        auto selector_right_r = layout::GetRect(g.layout, c.selector_r);

        bool should_highlight = false;
        if (layer->UsesTimbreLayering() &&
            (g.timbre_slider_is_held ||
             CcControllerMovedParamRecently(g.engine.processor, ParamIndex::MasterTimbre))) {
            should_highlight = true;
        }

        auto const registered_selector_box_r =
            g.imgui.RegisterAndConvertRect(layout::GetRect(g.layout, c.selector_box));
        {
            auto const rounding = LiveSize(UiSizeId::CornerRounding);
            auto const col = should_highlight ? LiveCol(UiColMap::LayerSelectorMenuBackHighlight)
                                              : LiveCol(UiColMap::LayerSelectorMenuBack);
            g.imgui.draw_list->AddRectFilled(registered_selector_box_r, col, rounding);
        }

        DoInstSelectorGUI(g, selector_menu_r, layer->index);
        if (auto percent =
                g.engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer->index]
                    .Load(LoadMemoryOrder::Relaxed);
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            DrawSelectorProgressBar(g.imgui, registered_selector_box_r, load_percent);
            GuiIo().WakeupAtTimedInterval(g.redraw_counter, 0.1);
        }

        if (buttons::Button(g,
                            selector_left_id,
                            selector_left_r,
                            ICON_FA_CARET_LEFT,
                            buttons::IconButton(g.imgui))) {
            InstBrowserContext context {
                .layer = *layer,
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadAdjacentInstrument(context, g.inst_browser_state[layer->index], SearchDirection::Backward);
        }
        if (buttons::Button(g,
                            selector_right_id,
                            selector_right_r,
                            ICON_FA_CARET_RIGHT,
                            buttons::IconButton(g.imgui))) {
            InstBrowserContext context {
                .layer = *layer,
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadAdjacentInstrument(context, g.inst_browser_state[layer->index], SearchDirection::Forward);
        }
        {
            auto rand_id = g.imgui.MakeId("Rand");
            auto rand_r = layout::GetRect(g.layout, c.selector_randomise);
            if (buttons::Button(g,
                                rand_id,
                                rand_r,
                                ICON_FA_SHUFFLE,
                                buttons::IconButton(g.imgui).WithRandomiseIconScaling())) {
                InstBrowserContext context {
                    .layer = *layer,
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .library_images = g.library_images,
                    .engine = g.engine,
                    .prefs = g.prefs,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                LoadRandomInstrument(context, g.inst_browser_state[layer->index]);
            }
            Tooltip(g,
                    rand_id,
                    g.imgui.ViewportRectToWindowRect(rand_r),
                    "Load a random instrument.\n\nThis is based on the currently selected filters."_s,
                    {});
        }

        Tooltip(g,
                selector_left_id,
                g.imgui.ViewportRectToWindowRect(selector_left_r),
                "Load the previous instrument\n\nThis is based on the currently selected filters."_s,
                {});
        Tooltip(g,
                selector_right_id,
                g.imgui.ViewportRectToWindowRect(selector_right_r),
                "Load the next instrument\n\nThis is based on the currently selected filters."_s,
                {});
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // divider
    draw_divider(c.divider);

    auto const volume_knob_r = layout::GetRect(g.layout, c.volume);
    // level meter
    {
        auto const layer_peak_meter_width = LiveSize(LayerPeakMeterWidth);
        auto const layer_peak_meter_height = LiveSize(LayerPeakMeterHeight);
        auto const layer_peak_meter_bottom_gap = LiveSize(LayerPeakMeterBottomGap);

        Rect const peak_meter_r {.xywh {
            volume_knob_r.Centre().x - (layer_peak_meter_width / 2),
            volume_knob_r.y + (volume_knob_r.h - (layer_peak_meter_height + layer_peak_meter_bottom_gap)),
            layer_peak_meter_width,
            layer_peak_meter_height - layer_peak_meter_bottom_gap}};
        auto const& processor = g.engine.processor.layer_processors[(usize)layer->index];
        DrawPeakMeter(g.imgui, g.imgui.RegisterAndConvertRect(peak_meter_r), processor.peak_meter, false);
    }

    // volume
    {
        auto const volume_name_h = layout::GetRect(g.layout, c.knob1.label).h;
        auto const volume_name_y_gap = LiveSize(LayerVolumeNameGapY);
        Rect const volume_name_r {.xywh {volume_knob_r.x,
                                         volume_knob_r.Bottom() - volume_name_h + volume_name_y_gap,
                                         volume_knob_r.w,
                                         volume_name_h}};

        KnobAndLabel(g,
                     params.DescribedValue(layer->index, LayerParamIndex::Volume),
                     volume_knob_r,
                     volume_name_r,
                     knobs::DefaultKnob(g.imgui));
    }

    // mute and solo
    {
        auto mute_solo_r = layout::GetRect(g.layout, c.mute_solo);
        Rect const mute_r = {.xywh {mute_solo_r.x, mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};
        Rect const solo_r = {
            .xywh {mute_solo_r.x + (mute_solo_r.w / 2), mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};

        auto const col_border = LiveCol(UiColMap::MuteSoloButtonBorder);
        auto const col_background = LiveCol(UiColMap::MuteSoloButtonBackground);
        auto const rounding = LiveSize(UiSizeId::CornerRounding);
        auto reg_mute_solo_r = g.imgui.RegisterAndConvertRect(mute_solo_r);
        auto reg_mute_r = g.imgui.RegisterAndConvertRect(mute_r);
        g.imgui.draw_list->AddRectFilled(reg_mute_solo_r, col_background, rounding);
        g.imgui.draw_list->AddLine({reg_mute_r.Right(), reg_mute_r.y},
                                   {reg_mute_r.Right(), reg_mute_r.Bottom()},
                                   col_border);

        buttons::Toggle(g,
                        params.DescribedValue(layer->index, LayerParamIndex::Mute),
                        mute_r,
                        "M",
                        buttons::MuteButton(g.imgui));
        buttons::Toggle(g,
                        params.DescribedValue(layer->index, LayerParamIndex::Solo),
                        solo_r,
                        "S",
                        buttons::SoloButton(g.imgui));
    }

    // knobs
    {
        auto semitone_style = draggers::DefaultStyle(g.imgui);
        semitone_style.always_show_plus = true;
        draggers::Dragger(g,
                          params.DescribedValue(layer->index, LayerParamIndex::TuneSemitone),
                          c.knob1.control,
                          semitone_style);
        labels::Label(g,
                      params.DescribedValue(layer->index, LayerParamIndex::TuneSemitone),
                      c.knob1.label,
                      labels::ParameterCentred(g.imgui));

        KnobAndLabel(g,
                     params.DescribedValue(layer->index, LayerParamIndex::TuneCents),
                     c.knob2,
                     knobs::BidirectionalKnob(g.imgui));
        KnobAndLabel(g,
                     params.DescribedValue(layer->index, LayerParamIndex::Pan),
                     c.knob3,
                     knobs::BidirectionalKnob(g.imgui));
    }

    draw_divider(c.divider2);

    // current page
    switch (layer_gui->selected_page) {
        case PageType::Main: {
            // waveform
            {
                DoWaveformElement(g, *layer, layout::GetRect(g.layout, c.main.waveform));

                labels::Label(g,
                              layout::GetRect(g.layout, c.main.waveform_label),
                              layer->InstTypeName(),
                              labels::WaveformLabel(g.imgui));

                bool const greyed_out = layer->instrument_id.tag == InstrumentType::WaveformSynth;
                buttons::Toggle(g,
                                params.DescribedValue(layer->index, LayerParamIndex::Reverse),
                                c.main.reverse,
                                buttons::ParameterToggleButton(g.imgui, {}, greyed_out));

                DoLoopModeSelectorGui(g, layout::GetRect(g.layout, c.main.loop_mode), *layer);
            }

            draw_divider(c.main.divider);

            // Envelope
            {
                buttons::Toggle(g,
                                params.DescribedValue(layer->index, LayerParamIndex::VolEnvOn),
                                c.main.env_on,
                                buttons::LayerHeadingButton(g.imgui));
                bool const env_on = params.BoolValue(layer->index, LayerParamIndex::VolEnvOn) ||
                                    layer->instrument.tag == InstrumentType::WaveformSynth;
                DoEnvelopeGui(g,
                              *layer,
                              layout::GetRect(g.layout, c.main.envelope),
                              !env_on,
                              {LayerParamIndex::VolumeAttack,
                               LayerParamIndex::VolumeDecay,
                               LayerParamIndex::VolumeSustain,
                               LayerParamIndex::VolumeRelease},
                              GuiEnvelopeType::Volume);
            }

            break;
        }
        case PageType::Filter: {
            bool const greyed_out = !params.BoolValue(layer->index, LayerParamIndex::FilterOn);
            buttons::Toggle(g,
                            params.DescribedValue(layer->index, LayerParamIndex::FilterOn),
                            c.filter.filter_on,
                            buttons::LayerHeadingButton(g.imgui));

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::FilterType),
                                    c.filter.filter_type,
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));

            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::FilterCutoff),
                         c.filter.cutoff,
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::FilterResonance),
                         c.filter.reso,
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::FilterEnvAmount),
                         c.filter.env_amount,
                         knobs::BidirectionalKnob(g.imgui),
                         greyed_out);

            DoEnvelopeGui(g,
                          *layer,
                          layout::GetRect(g.layout, c.filter.envelope),
                          greyed_out ||
                              (params.LinearValue(layer->index, LayerParamIndex::FilterEnvAmount) == 0),
                          {LayerParamIndex::FilterAttack,
                           LayerParamIndex::FilterDecay,
                           LayerParamIndex::FilterSustain,
                           LayerParamIndex::FilterRelease},
                          GuiEnvelopeType::Filter);

            break;
        }
        case PageType::Eq: {
            bool const greyed_out = !params.BoolValue(layer->index, LayerParamIndex::EqOn);
            buttons::Toggle(g,
                            params.DescribedValue(layer->index, LayerParamIndex::EqOn),
                            layout::GetRect(g.layout, c.eq.on),
                            buttons::LayerHeadingButton(g.imgui));

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::EqType1),
                                    layout::GetRect(g.layout, c.eq.type[0]),
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));

            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqFreq1),
                         c.eq.freq[0],
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqResonance1),
                         c.eq.reso[0],
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqGain1),
                         c.eq.gain[0],
                         knobs::BidirectionalKnob(g.imgui),
                         greyed_out);

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::EqType2),
                                    layout::GetRect(g.layout, c.eq.type[1]),
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));

            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqFreq2),
                         c.eq.freq[1],
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqResonance2),
                         c.eq.reso[1],
                         knobs::DefaultKnob(g.imgui),
                         greyed_out);
            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::EqGain2),
                         c.eq.gain[1],
                         knobs::BidirectionalKnob(g.imgui),
                         greyed_out);

            break;
        }
        case PageType::Play: {
            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::MidiTranspose),
                              c.play.transpose,
                              draggers::DefaultStyle(g.imgui));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::MidiTranspose),
                          c.play.transpose_name,
                          labels::Parameter(g.imgui));

            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::PitchBendRange),
                              c.play.pitchbend,
                              draggers::DefaultStyle(g.imgui));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::PitchBendRange),
                          c.play.pitchbend_name,
                          labels::Parameter(g.imgui));

            {
                auto const label_id = g.imgui.MakeId("transp");
                auto const label_r = layout::GetRect(g.layout, c.play.transpose_name);
                auto const label_window_r = g.imgui.RegisterAndConvertRect(label_r);
                g.imgui.ButtonBehaviour(label_window_r, label_id, {});
                Tooltip(
                    g,
                    label_id,
                    label_window_r,
                    k_param_descriptors[ToInt(ParamIndexFromLayerParamIndex(layer->index,
                                                                            LayerParamIndex::MidiTranspose))]
                        .tooltip,
                    {});
                if (g.imgui.IsHot(label_id)) GuiIo().out.wants.cursor_type = CursorType::Default;
            }

            buttons::Toggle(g,
                            params.DescribedValue(layer->index, LayerParamIndex::Keytrack),
                            c.play.keytrack,
                            buttons::MidiButton(g.imgui));
            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::MonophonicMode),
                                    c.play.mono,
                                    buttons::ParameterPopupButton(g.imgui));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::MonophonicMode),
                          c.play.mono_name,
                          labels::Parameter(g.imgui));

            labels::Label(g, c.play.key_range_text, "Range", labels::Parameter(g.imgui));

            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::KeyRangeLow),
                              c.play.key_range_low,
                              draggers::NoteNameStyle(g.imgui));
            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::KeyRangeHigh),
                              c.play.key_range_high,
                              draggers::NoteNameStyle(g.imgui));

            labels::Label(g, c.play.key_range_fade_text, "Key Fade", labels::Parameter(g.imgui));

            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::KeyRangeLowFade),
                              c.play.key_range_low_fade,
                              draggers::DefaultStyle(g.imgui));

            draggers::Dragger(g,
                              params.DescribedValue(layer->index, LayerParamIndex::KeyRangeHighFade),
                              c.play.key_range_high_fade,
                              draggers::DefaultStyle(g.imgui));

            {
                {
                    auto label_r = layout::GetRect(g.layout, c.play.velo_name);
                    auto const imgui_id = g.imgui.MakeId("vel->vol");
                    labels::Label(g, label_r, "Velocity to volume curve", labels::Parameter(g.imgui));
                    label_r = g.imgui.RegisterAndConvertRect(label_r);
                    g.imgui.SetHot(label_r, imgui_id);
                    Tooltip(g, imgui_id, label_r, "Curve that maps velocity to volume"_s, {});
                }

                auto const velograph_r =
                    g.imgui.RegisterAndConvertRect(layout::GetRect(g.layout, c.play.velo_graph));

                if (DoCurveMap(
                        g,
                        layer->velocity_curve_map,
                        velograph_r.Min(),
                        velograph_r.Max(),
                        ({
                            Optional<f32> velocity {};
                            if (g.engine.processor.voice_pool.num_active_voices.Load(
                                    LoadMemoryOrder::Relaxed)) {
                                velocity = g.engine.processor.voice_pool.last_velocity[layer->index].Load(
                                    LoadMemoryOrder::Relaxed);
                            }
                            velocity;
                        }),
                        "Configures how MIDI velocity maps to volume. X-axis: velocity, Y-axis: volume. Adjust the curve to customize this relationship."))
                    layer->velocity_curve_map.RenderCurveToLookupTable();
            }

            break;
        }
        case PageType::Lfo: {
            buttons::Toggle(g,
                            params.DescribedValue(layer->index, LayerParamIndex::LfoOn),
                            c.lfo.on,
                            buttons::LayerHeadingButton(g.imgui));
            auto const greyed_out = !params.BoolValue(layer->index, LayerParamIndex::LfoOn);

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::LfoDestination),
                                    c.lfo.target,
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::LfoDestination),
                          c.lfo.target_name,
                          labels::Parameter(g.imgui));

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::LfoRestart),
                                    c.lfo.mode,
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::LfoRestart),
                          c.lfo.mode_name,
                          labels::Parameter(g.imgui));

            buttons::PopupWithItems(g,
                                    params.DescribedValue(layer->index, LayerParamIndex::LfoShape),
                                    c.lfo.shape,
                                    buttons::ParameterPopupButton(g.imgui, greyed_out));
            labels::Label(g,
                          params.DescribedValue(layer->index, LayerParamIndex::LfoShape),
                          c.lfo.shape_name,
                          labels::Parameter(g.imgui));

            KnobAndLabel(g,
                         params.DescribedValue(layer->index, LayerParamIndex::LfoAmount),
                         c.lfo.amount,
                         knobs::BidirectionalKnob(g.imgui),
                         greyed_out);

            Optional<DescribedParamValue> rate_param;
            if (params.BoolValue(layer->index, LayerParamIndex::LfoSyncSwitch)) {
                rate_param = params.DescribedValue(layer->index, LayerParamIndex::LfoRateTempoSynced);
                buttons::PopupWithItems(g,
                                        *rate_param,
                                        c.lfo.rate.control,
                                        buttons::ParameterPopupButton(g.imgui, greyed_out));
            } else {
                rate_param = params.DescribedValue(layer->index, LayerParamIndex::LfoRateHz);
                knobs::Knob(g,
                            *rate_param,
                            c.lfo.rate.control,
                            knobs::DefaultKnob(g.imgui).GreyedOut(greyed_out));
            }

            auto const rate_name_r = layout::GetRect(g.layout, c.lfo.rate.label);
            labels::Label(g, *rate_param, rate_name_r, labels::ParameterCentred(g.imgui, greyed_out));

            auto const lfo_sync_switch_width = LiveSize(LfoSyncSwitchWidth);
            auto const lfo_sync_switch_height = LiveSize(LfoSyncSwitchHeight);
            auto const lfo_sync_switch_gap_y = LiveSize(LfoSyncSwitchGapY);

            buttons::Toggle(g,
                            params.DescribedValue(layer->index, LayerParamIndex::LfoSyncSwitch),
                            {.xywh {rate_name_r.x + (rate_name_r.w / 2) - (lfo_sync_switch_width / 2),
                                    rate_name_r.Bottom() + lfo_sync_switch_gap_y,
                                    lfo_sync_switch_width,
                                    lfo_sync_switch_height}},
                            buttons::ParameterToggleButton(g.imgui));

            break;
        }
        case PageType::Count: PanicIfReached();
    }

    // tabs
    for (auto const i : Range(k_num_pages)) {
        auto const page_type = (PageType)i;
        bool state = page_type == layer_gui->selected_page;
        auto const id = g.imgui.MakeId((u64)i);
        auto const tab_r = layout::GetRect(g.layout, c.tabs[i]);
        auto const name {GetPageTitle(page_type)};
        bool const has_dot =
            (page_type == PageType::Filter && params.BoolValue(layer->index, LayerParamIndex::FilterOn)) ||
            (page_type == PageType::Lfo && params.BoolValue(layer->index, LayerParamIndex::LfoOn)) ||
            (page_type == PageType::Eq && params.BoolValue(layer->index, LayerParamIndex::EqOn));
        if (buttons::Toggle(g, id, tab_r, state, name, buttons::LayerTabButton(g.imgui, has_dot)))
            layer_gui->selected_page = page_type;
        Tooltip(g,
                id,
                g.imgui.ViewportRectToWindowRect(tab_r),
                fmt::Format(g.scratch_arena, "Open {} tab", name),
                {});
    }

    if (g.macros_gui_state.active_destination_knob) {
        auto const param =
            LayerParamIndexAndLayerFor(*g.macros_gui_state.active_destination_knob->dest.param_index);
        if (param && param->layer_num == layer->index) {
            auto const new_page = [&]() -> Optional<PageType> {
                switch (param->param) {
                    case LayerParamIndex::Volume:
                    case LayerParamIndex::Mute:
                    case LayerParamIndex::Solo:
                    case LayerParamIndex::Pan:
                    case LayerParamIndex::TuneCents:
                    case LayerParamIndex::TuneSemitone: return k_nullopt;

                    case LayerParamIndex::LoopMode:
                    case LayerParamIndex::LoopStart:
                    case LayerParamIndex::LoopEnd:
                    case LayerParamIndex::LoopCrossfade:
                    case LayerParamIndex::SampleOffset:
                    case LayerParamIndex::Reverse:
                    case LayerParamIndex::VolEnvOn:
                    case LayerParamIndex::VolumeAttack:
                    case LayerParamIndex::VolumeDecay:
                    case LayerParamIndex::VolumeSustain:
                    case LayerParamIndex::VolumeRelease: return PageType::Main;

                    case LayerParamIndex::FilterOn:
                    case LayerParamIndex::FilterCutoff:
                    case LayerParamIndex::FilterResonance:
                    case LayerParamIndex::FilterType:
                    case LayerParamIndex::FilterEnvAmount:
                    case LayerParamIndex::FilterAttack:
                    case LayerParamIndex::FilterDecay:
                    case LayerParamIndex::FilterSustain:
                    case LayerParamIndex::FilterRelease: return PageType::Filter;

                    case LayerParamIndex::LfoOn:
                    case LayerParamIndex::LfoShape:
                    case LayerParamIndex::LfoRestart:
                    case LayerParamIndex::LfoAmount:
                    case LayerParamIndex::LfoDestination:
                    case LayerParamIndex::LfoRateTempoSynced:
                    case LayerParamIndex::LfoRateHz:
                    case LayerParamIndex::LfoSyncSwitch: return PageType::Lfo;

                    case LayerParamIndex::EqOn:
                    case LayerParamIndex::EqFreq1:
                    case LayerParamIndex::EqResonance1:
                    case LayerParamIndex::EqGain1:
                    case LayerParamIndex::EqType1:
                    case LayerParamIndex::EqFreq2:
                    case LayerParamIndex::EqResonance2:
                    case LayerParamIndex::EqGain2:
                    case LayerParamIndex::EqType2: return PageType::Eq;

                    case LayerParamIndex::VelocityMapping:
                    case LayerParamIndex::Keytrack:
                    case LayerParamIndex::Monophonic:
                    case LayerParamIndex::MonophonicMode:
                    case LayerParamIndex::MidiTranspose:
                    case LayerParamIndex::PitchBendRange:
                    case LayerParamIndex::KeyRangeLow:
                    case LayerParamIndex::KeyRangeHigh:
                    case LayerParamIndex::KeyRangeLowFade:
                    case LayerParamIndex::KeyRangeHighFade: return PageType::Play;

                    case LayerParamIndex::Count: PanicIfReached();
                }
            }();
            if (new_page && new_page != layer_gui->selected_page) {
                layer_gui->selected_page = *new_page;
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }
        }
    }

    // overlay
    if (LayerIsSilent(g.engine.processor, layer->index)) {
        auto const pos = g.imgui.curr_viewport->unpadded_bounds.pos;
        g.imgui.draw_list->AddRectFilled(pos,
                                         pos + g.imgui.CurrentVpSize(),
                                         LiveCol(UiColMap::LayerMutedOverlay));
    }
}

} // namespace layer_gui
