// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer.hpp"

#include <IconsFontAwesome6.h>

#include "engine/engine.hpp"
#include "engine/loop_modes.hpp"
#include "gui.hpp"
#include "gui/gui_menu.hpp"
#include "gui2_inst_picker.hpp"
#include "gui_button_widgets.hpp"
#include "gui_curve_map.hpp"
#include "gui_dragger_widgets.hpp"
#include "gui_drawing_helpers.hpp"
#include "gui_envelope.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_modal_windows.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_waveform.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "gui_window.hpp"
#include "processor/layer_processor.hpp"

namespace layer_gui {

static void DoInstSelectorRightClickMenu(Gui* g, Rect r, u32 layer) {
    auto& imgui = g->imgui;
    auto layer_obj = &g->engine.Layer(layer);
    auto const popup_id = imgui.GetID("inst selector popup");
    auto const right_clicker_id = imgui.GetID("inst selector right clicker");

    imgui.RegisterAndConvertRect(&r);
    imgui.PopupButtonBehavior(r,
                              right_clicker_id,
                              popup_id,
                              {.right_mouse = true, .triggers_on_mouse_up = true});

    if (imgui.IsPopupOpen(popup_id)) {
        auto const items = Array {"Unload instrument"_s};

        PopupMenuItems menu(g, items);

        auto settings = PopupWindowSettings(imgui);
        settings.flags =
            imgui::WindowFlags_AutoWidth | imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoPosition;
        if (imgui.BeginWindowPopup(settings, popup_id, r)) {
            DEFER { imgui.EndWindow(); };

            if (layer_obj->instrument_id.tag == InstrumentType::None)
                menu.DoFakeButton(items[0]);
            else if (menu.DoButton(items[0]))
                LoadInstrument(g->engine, layer, InstrumentType::None);
        }
    }
}

static void DoInstSelectorGUI(Gui* g, Rect r, u32 layer) {
    g->imgui.PushID("inst selector");
    DEFER { g->imgui.PopID(); };
    auto const imgui_id = g->imgui.GetID((u64)layer);

    auto layer_obj = &g->engine.Layer(layer);
    auto const inst_name = layer_obj->InstName();

    Optional<graphics::TextureHandle> icon_tex {};
    if (layer_obj->instrument_id.tag == InstrumentType::Sampler) {
        auto sample_inst_id = layer_obj->instrument_id.Get<sample_lib::InstrumentId>();
        auto imgs = LibraryImagesFromLibraryId(g, sample_inst_id.library, true);
        if (imgs && imgs->icon)
            icon_tex = g->imgui.frame_input.graphics_ctx->GetTextureFromImage(*imgs->icon);
    }

    DoInstSelectorRightClickMenu(g, r, layer);

    if (buttons::Button(g, imgui_id, r, inst_name, buttons::InstSelectorPopupButton(g->imgui, icon_tex))) {
        g->inst_picker_state[layer].common_state_floe_libraries.open = true;
        g->inst_picker_state[layer].common_state_floe_libraries.absolute_button_rect =
            g->imgui.WindowRectToScreenRect(r);
    }

    Tooltip(g, imgui_id, r, ({
                String s {};
                switch (layer_obj->instrument_id.tag) {
                    case InstrumentType::None: s = "Select the instrument for this layer"_s; break;
                    case InstrumentType::WaveformSynth:
                        s = fmt::Format(g->scratch_arena,
                                        "Instrument: {}\nChange or remove the instrument for this layer",
                                        inst_name);
                        break;
                    case InstrumentType::Sampler: {
                        auto const& sample = layer_obj->instrument_id.Get<sample_lib::InstrumentId>();
                        s = fmt::Format(
                            g->scratch_arena,
                            "Instrument: {} from {} by {}\nChange or remove the instrument for this layer",
                            inst_name,
                            sample.library.name,
                            sample.library.author);
                        break;
                    }
                }
                s;
            }));
}

static void DoLoopModeSelectorGui(Gui* g, Rect r, LayerProcessor& layer) {
    g->imgui.PushID("loop mode selector");
    DEFER { g->imgui.PopID(); };
    auto const& param = layer.params[ToInt(LayerParamIndex::LoopMode)];
    auto const desired_loop_mode = param.ValueAsInt<param_values::LoopMode>();

    auto const vol_env_on = layer.VolumeEnvelopeIsOn(false);
    auto const actual_loop_behaviour = ActualLoopBehaviour(layer.instrument, desired_loop_mode, vol_env_on);
    auto const default_loop_behaviour =
        ActualLoopBehaviour(layer.instrument, param_values::LoopMode::InstrumentDefault, vol_env_on);
    DynamicArrayBounded<char, 64> default_mode_str {"Default: "};
    dyn::AppendSpan(default_mode_str, default_loop_behaviour.value.name);

    auto const imgui_id = BeginParameterGUI(g, param, r);

    Optional<f32> val {};

    auto const style = buttons::ParameterPopupButton(g->imgui);

    // Draw around the whole thing, not just the menu.
    if (style.back_cols.reg) {
        auto const converted_r = g->imgui.GetRegisteredAndConvertedRect(r);
        g->imgui.graphics->AddRectFilled(converted_r.Min(),
                                         converted_r.Max(),
                                         style.back_cols.reg,
                                         LiveSize(g->imgui, UiSizeId::CornerRounding));
    }

    auto const btn_w = LiveSize(g->imgui, UiSizeId::NextPrevButtonSize);
    auto const margin_r = LiveSize(g->imgui, UiSizeId::ParamIntButtonMarginR);
    rect_cut::CutRight(r, margin_r);
    auto rect_r = rect_cut::CutRight(r, btn_w);
    auto rect_l = rect_cut::CutRight(r, btn_w);

    auto popup_style = style;
    popup_style.back_cols = {};
    if (buttons::Popup(g, imgui_id, imgui_id + 1, r, actual_loop_behaviour.value.short_name, popup_style)) {
        StartFloeMenu(g);
        DEFER { EndFloeMenu(g); };
        DEFER { g->imgui.EndWindow(); };

        auto items = param_values::k_loop_mode_strings;

        items[ToInt(param_values::LoopMode::InstrumentDefault)] = default_mode_str;

        auto const w = MenuItemWidth(g, items);
        auto const h = LiveSize(g->imgui, UiSizeId::MenuItemHeight);

        for (auto const i : Range<u32>(items.size)) {
            bool state = i == ToInt(desired_loop_mode);
            auto const behaviour =
                ActualLoopBehaviour(layer.instrument, (param_values::LoopMode)i, vol_env_on);
            auto const valid = behaviour.is_desired;
            Rect const item_rect = {.xywh {0, h * (f32)i, w, h}};
            auto const item_id = g->imgui.GetID((uintptr)i);

            if (buttons::Toggle(g,
                                item_id,
                                item_rect,
                                state,
                                items[i],
                                buttons::MenuItem(g->imgui, true, !valid)) &&
                i != ToInt(desired_loop_mode))
                val = (f32)i;

            {
                DynamicArray<char> tooltip {g->scratch_arena};

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

                Tooltip(g, item_id, item_rect, tooltip);
            }
        }
    }

    {
        auto current = param.LinearValue();
        if (g->imgui.SliderRange(
                {
                    .flags = imgui::DefSlider().flags,
                    .sensitivity = 100 + (5000 * 1.0f / param.info.linear_range.Delta()),
                    .draw = [](IMGUI_DRAW_SLIDER_ARGS) {},
                },
                r,
                imgui_id,
                param.info.linear_range.min,
                param.info.linear_range.max,
                current,
                param.info.default_linear_value)) {
            val = current;
        }
    }

    auto const button_style = buttons::IconButton(g->imgui);
    auto const left_id = imgui_id - 4;
    auto const right_id = imgui_id + 4;

    auto increment_mode = [&](f32 step) {
        auto new_val = (f32)param.ValueAsInt<int>() + step;
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
    Tooltip(g, left_id, rect_l, "Previous loop mode"_s);
    Tooltip(g, right_id, rect_r, "Next loop mode"_s);

    EndParameterGUI(g,
                    imgui_id,
                    param,
                    r,
                    val,
                    (ParamDisplayFlags)(ParamDisplayFlagsNoTooltip | ParamDisplayFlagsNoValuePopup));

    Tooltip(g,
            imgui_id,
            r,
            fmt::Format(g->scratch_arena,
                        "{}: {}\n\n{} {}",
                        param.info.name,
                        actual_loop_behaviour.value.name,
                        actual_loop_behaviour.value.description,
                        actual_loop_behaviour.reason));
}

static String GetPageTitle(PageType type) {
    switch (type) {
        case PageType::Main: return "Main";
        case PageType::Eq: return "EQ";
        case PageType::Keyboard: return "Play";
        case PageType::Lfo: return "LFO";
        case PageType::Filter: return "Filter";
        case PageType::Count: PanicIfReached();
    }
    return "";
}

void Layout(Gui* g,
            LayerProcessor* layer,
            LayerLayoutTempIDs& c,
            LayerLayout* layer_gui,
            f32 width,
            f32 height) {
    using enum UiSizeId;
    auto const param_popup_button_height = LiveSize(g->imgui, ParamPopupButtonHeight);
    auto const page_heading_height = LiveSize(g->imgui, Page_HeadingHeight);

    auto container = layout::CreateItem(g->layout,
                                        {
                                            .size = {width, height},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        });

    // selector
    {

        c.selector_box = layout::CreateItem(
            g->layout,
            {
                .parent = container,
                .size = {layout::k_fill_parent, LiveSize(g->imgui, LayerSelectorBoxHeight)},
                .margins = {.l = LiveSize(g->imgui, LayerSelectorBoxMarginL),
                            .r = LiveSize(g->imgui, LayerSelectorBoxMarginR),
                            .t = LiveSize(g->imgui, LayerSelectorBoxMarginT),
                            .b = LiveSize(g->imgui, LayerSelectorBoxMarginB)},
                .contents_direction = layout::Direction::Row,
                .contents_align = layout::Alignment::Start,
            });

        c.selector_menu = layout::CreateItem(g->layout,
                                             {
                                                 .parent = c.selector_box,
                                                 .size = layout::k_fill_parent,
                                             });

        auto const layer_selector_button_w = LiveSize(g->imgui, ResourceSelectorRandomButtonW);
        auto const layer_selector_lr_button_w = LiveSize(g->imgui, UiSizeId::NextPrevButtonSize);
        auto const layer_selector_box_buttons_margin_r = LiveSize(g->imgui, LayerSelectorBoxButtonsMarginR);

        c.selector_l = layout::CreateItem(g->layout,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_lr_button_w, layout::k_fill_parent},
                                          });
        c.selector_r = layout::CreateItem(g->layout,
                                          {
                                              .parent = c.selector_box,
                                              .size = {layer_selector_lr_button_w, layout::k_fill_parent},
                                          });
        c.selector_randomise =
            layout::CreateItem(g->layout,
                               {
                                   .parent = c.selector_box,
                                   .size = {layer_selector_button_w, layout::k_fill_parent},
                                   .margins = {.r = layer_selector_box_buttons_margin_r},
                               });
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // mixer container 1
    {
        auto subcontainer_1 = layout::CreateItem(g->layout,
                                                 {
                                                     .parent = container,
                                                     .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                     .margins {
                                                         .l = LiveSize(g->imgui, LayerMixerContainer1MarginL),
                                                         .r = LiveSize(g->imgui, LayerMixerContainer1MarginR),
                                                         .t = LiveSize(g->imgui, LayerMixerContainer1MarginT),
                                                         .b = LiveSize(g->imgui, LayerMixerContainer1MarginB),
                                                     },
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::Alignment::Middle,
                                                 });

        c.volume = layout::CreateItem(g->layout,
                                      {
                                          .parent = subcontainer_1,
                                          .size = LiveSize(g->imgui, LayerVolumeKnobSize),
                                          .margins = {.r = LiveSize(g->imgui, LayerVolumeKnobMarginR)},
                                      });

        c.mute_solo = layout::CreateItem(
            g->layout,
            {
                .parent = subcontainer_1,
                .size = {LiveSize(g->imgui, LayerMuteSoloWidth), LiveSize(g->imgui, LayerMuteSoloHeight)},
                .margins {
                    .l = LiveSize(g->imgui, LayerMuteSoloMarginL),
                    .r = LiveSize(g->imgui, LayerMuteSoloMarginR),
                    .t = LiveSize(g->imgui, LayerMuteSoloMarginT),
                    .b = LiveSize(g->imgui, LayerMuteSoloMarginB),
                },
            });
    }

    // mixer container 2
    {
        auto subcontainer_2 = layout::CreateItem(g->layout,
                                                 {
                                                     .parent = container,
                                                     .size = layout::k_hug_contents,
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::Alignment::Middle,
                                                 });
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob1,
                                 layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                                 LayerPitchMarginLR);
        layout::SetSize(g->layout,
                        c.knob1.control,
                        f32x2 {
                            LiveSize(g->imgui, LayerPitchWidth),
                            LiveSize(g->imgui, LayerPitchHeight),
                        });
        layout::SetMargins(g->layout,
                           c.knob1.control,
                           {
                               .t = LiveSize(g->imgui, LayerPitchMarginT),
                               .b = LiveSize(g->imgui, LayerPitchMarginB),
                           });

        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob2,
                                 layer->params[ToInt(LayerParamIndex::TuneCents)],
                                 LayerMixerKnobGapX);
        LayoutParameterComponent(g,
                                 subcontainer_2,
                                 c.knob3,
                                 layer->params[ToInt(LayerParamIndex::Pan)],
                                 LayerMixerKnobGapX);
    }

    auto const layer_mixer_divider_vert_margins = LiveSize(g->imgui, LayerMixerDividerVertMargins);
    // divider
    c.divider = layout::CreateItem(g->layout,
                                   {
                                       .parent = container,
                                       .size = {layout::k_fill_parent, 1},
                                       .margins = {.tb = layer_mixer_divider_vert_margins},
                                   });

    // tabs
    {
        auto tab_lay =
            layout::CreateItem(g->layout,
                               {
                                   .parent = container,
                                   .size = {layout::k_fill_parent, LiveSize(g->imgui, LayerParamsGroupTabsH)},
                                   .margins = {.lr = LiveSize(g->imgui, LayerParamsGroupBoxGapX)},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Middle,
                               });

        auto const layer_params_group_tabs_gap = LiveSize(g->imgui, LayerParamsGroupTabsGap);
        for (auto const i : Range(k_num_pages)) {
            auto const page_type = (PageType)i;
            auto size =
                draw::GetTextWidth(g->imgui.graphics->context->CurrentFont(), GetPageTitle(page_type));
            if (page_type == PageType::Filter || page_type == PageType::Lfo || page_type == PageType::Eq)
                size += LiveSize(g->imgui, LayerParamsGroupTabsIconW2);
            c.tabs[i] =
                layout::CreateItem(g->layout,
                                   {
                                       .parent = tab_lay,
                                       .size = {size + layer_params_group_tabs_gap, layout::k_fill_parent},
                                   });
        }
    }

    // divider2
    {
        c.divider2 = layout::CreateItem(g->layout,
                                        {
                                            .parent = container,
                                            .size = {layout::k_fill_parent, 1},
                                            .margins = {.tb = layer_mixer_divider_vert_margins},
                                        });
    }

    {
        auto const page_heading_margin_l = LiveSize(g->imgui, Page_HeadingMarginL);
        auto const page_heading_margin_t = LiveSize(g->imgui, Page_HeadingMarginT);
        auto const page_heading_margin_b = LiveSize(g->imgui, Page_HeadingMarginB);
        auto const heading_margins = layout::Margins {
            .l = page_heading_margin_l,
            .r = 0,
            .t = page_heading_margin_t,
            .b = page_heading_margin_b,
        };

        auto page_container = layout::CreateItem(g->layout,
                                                 {
                                                     .parent = container,
                                                     .size = layout::k_fill_parent,
                                                     .contents_direction = layout::Direction::Column,
                                                     .contents_align = layout::Alignment::Start,
                                                 });

        auto const main_envelope_h = LiveSize(g->imgui, Main_EnvelopeH);

        switch (layer_gui->selected_page) {
            case PageType::Main: {
                auto const waveform_margins_lr = LiveSize(g->imgui, Main_WaveformMarginLR);
                c.main.waveform = layout::CreateItem(
                    g->layout,
                    {
                        .parent = page_container,
                        .size = {layout::k_fill_parent, LiveSize(g->imgui, Main_WaveformH)},
                        .margins =
                            {
                                .lr = waveform_margins_lr,
                                .tb = LiveSize(g->imgui, Main_WaveformMarginTB),
                            },
                    });

                c.main.waveform_label = layout::CreateItem(
                    g->layout,
                    {
                        .parent = page_container,
                        .size = {layout::k_fill_parent, LiveSize(g->imgui, Main_WaveformLabelH)},
                        .margins = {.lr = waveform_margins_lr},
                    });

                auto const main_item_margin_l = LiveSize(g->imgui, Main_ItemMarginL);
                auto const main_item_margin_r = LiveSize(g->imgui, Main_ItemMarginR);
                auto const main_item_height = LiveSize(g->imgui, Main_ItemHeight);
                auto const main_item_gap_y = LiveSize(g->imgui, Main_ItemGapY);
                auto btn_container =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.l = main_item_margin_l, .r = main_item_margin_r},
                                           .contents_direction = layout::Direction::Row,
                                       });
                c.main.reverse = layout::CreateItem(
                    g->layout,
                    {
                        .parent = btn_container,
                        .size = {LiveSize(g->imgui, Main_ReverseButtonWidth), main_item_height},
                        .margins = {.tb = main_item_gap_y},
                    });
                c.main.loop_mode =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = btn_container,
                                           .size = {layout::k_fill_parent, param_popup_button_height},
                                           .margins = {.tb = main_item_gap_y},
                                       });

                auto const main_divider_margin_t = LiveSize(g->imgui, Main_DividerMarginT);
                auto const main_divider_margin_b = LiveSize(g->imgui, Main_DividerMarginB);
                c.main.divider = layout::CreateItem(
                    g->layout,
                    {
                        .parent = page_container,
                        .size = {layout::k_fill_parent, 1},
                        .margins = {.t = main_divider_margin_t, .b = main_divider_margin_b},
                    });

                c.main.env_on = layout::CreateItem(g->layout,
                                                   {
                                                       .parent = page_container,
                                                       .size = {layout::k_fill_parent, page_heading_height},
                                                       .margins = ({
                                                           auto m = heading_margins;
                                                           m.b = 0;
                                                           m;
                                                       }),
                                                   });

                c.main.envelope = layout::CreateItem(g->layout,
                                                     {
                                                         .parent = page_container,
                                                         .size = {layout::k_fill_parent, main_envelope_h},
                                                         .margins {
                                                             .lr = LiveSize(g->imgui, Main_EnvelopeMarginLR),
                                                             .tb = LiveSize(g->imgui, Main_EnvelopeMarginTB),
                                                         },
                                                     });
                break;
            }
            case PageType::Filter: {
                auto const filter_gap_y_before_knobs = LiveSize(g->imgui, Filter_GapYBeforeKnobs);

                auto filter_heading_container =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins {.b = filter_gap_y_before_knobs},
                                           .contents_direction = layout::Direction::Row,
                                       });
                c.filter.filter_on =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = filter_heading_container,
                                           .size = {LiveSize(g->imgui, Filter_OnWidth), page_heading_height},
                                           .margins = heading_margins,
                                           .anchor = layout::Anchor::Top,
                                       });
                c.filter.filter_type =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = filter_heading_container,
                                           .size = {layout::k_fill_parent, param_popup_button_height},
                                           .margins = {.lr = page_heading_margin_l},
                                       });

                auto filter_knobs_container =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Middle,
                                       });
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.cutoff,
                                         layer->params[ToInt(LayerParamIndex::FilterCutoff)],
                                         Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.reso,
                                         layer->params[ToInt(LayerParamIndex::FilterResonance)],
                                         Page_3KnobGapX);
                LayoutParameterComponent(g,
                                         filter_knobs_container,
                                         c.filter.env_amount,
                                         layer->params[ToInt(LayerParamIndex::FilterEnvAmount)],
                                         Page_3KnobGapX);

                c.filter.envelope =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, main_envelope_h},
                                           .margins {
                                               .lr = LiveSize(g->imgui, Filter_EnvelopeMarginLR),
                                               .tb = LiveSize(g->imgui, Filter_EnvelopeMarginTB),
                                           },
                                       });
                break;
            }
            case PageType::Eq: {
                c.eq.on = layout::CreateItem(g->layout,
                                             {
                                                 .parent = page_container,
                                                 .size = {layout::k_fill_parent, page_heading_height},
                                                 .margins = heading_margins,
                                             });

                auto const eq_band_gap_y = LiveSize(g->imgui, EQ_BandGapY);
                {
                    c.eq.type[0] =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, param_popup_button_height},
                                               .margins {
                                                   .lr = page_heading_margin_l,
                                                   .tb = eq_band_gap_y,
                                               },
                                           });

                    auto knob_container =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                           });
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[0],
                                             layer->params[ToInt(LayerParamIndex::EqFreq1)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[0],
                                             layer->params[ToInt(LayerParamIndex::EqResonance1)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[0],
                                             layer->params[ToInt(LayerParamIndex::EqGain1)],
                                             Page_3KnobGapX);
                    layout::SetMargins(g->layout, knob_container, {.b = eq_band_gap_y});
                }

                {
                    c.eq.type[1] =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, param_popup_button_height},
                                               .margins {
                                                   .lr = page_heading_margin_l,
                                                   .tb = eq_band_gap_y,
                                               },
                                           });
                    auto knob_container =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                           });
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.freq[1],
                                             layer->params[ToInt(LayerParamIndex::EqFreq2)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.reso[1],
                                             layer->params[ToInt(LayerParamIndex::EqResonance2)],
                                             Page_3KnobGapX);
                    LayoutParameterComponent(g,
                                             knob_container,
                                             c.eq.gain[1],
                                             layer->params[ToInt(LayerParamIndex::EqGain2)],
                                             Page_3KnobGapX);
                }

                break;
            }
            case PageType::Keyboard: {
                auto const midi_item_height = LiveSize(g->imgui, MIDI_ItemHeight);
                auto const midi_item_width = LiveSize(g->imgui, MIDI_ItemWidth);
                auto const midi_item_margin_lr = LiveSize(g->imgui, MIDI_ItemMarginLR);
                auto const midi_item_gap_y = LiveSize(g->imgui, MIDI_ItemGapY);

                auto layout_item = [&](layout::Id& control, layout::Id& name, f32 height) {
                    auto parent =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,

                                           });
                    control = layout::CreateItem(g->layout,
                                                 {
                                                     .parent = parent,
                                                     .size = {midi_item_width, height},
                                                     .margins {
                                                         .lr = midi_item_margin_lr,
                                                         .tb = midi_item_gap_y,
                                                     },
                                                 });
                    name = layout::CreateItem(g->layout,
                                              {
                                                  .parent = parent,
                                                  .size = {layout::k_fill_parent, height},
                                              });
                };

                layout_item(c.play.transpose, c.play.transpose_name, midi_item_height);

                auto const button_options = layout::ItemOptions {
                    .parent = page_container,
                    .size = {layout::k_fill_parent, midi_item_height},
                    .margins {
                        .lr = midi_item_margin_lr,
                        .tb = midi_item_gap_y,
                    },
                };
                c.play.keytrack = layout::CreateItem(g->layout, button_options);
                c.play.mono = layout::CreateItem(g->layout, button_options);
                c.play.retrig = layout::CreateItem(g->layout, button_options);

                c.play.velo_name =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, midi_item_height},
                                           .margins = {.lr = midi_item_margin_lr, .b = midi_item_gap_y},
                                       });
                c.play.velo_graph = layout::CreateItem(
                    g->layout,
                    {
                        .parent = page_container,
                        .size = {layout::k_fill_parent, LiveSize(g->imgui, MIDI_VeloGraphHeight)},
                        .margins = {.lr = midi_item_margin_lr},
                    });
                break;
            }
            case PageType::Lfo: {
                c.lfo.on = layout::CreateItem(g->layout,
                                              {
                                                  .parent = page_container,
                                                  .size = {layout::k_fill_parent, page_heading_height},
                                                  .margins = heading_margins,
                                              });
                auto layout_item = [&](layout::Id& control, layout::Id& name) {
                    auto parent =
                        layout::CreateItem(g->layout,
                                           {
                                               .parent = page_container,
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                           });
                    control = layout::CreateItem(
                        g->layout,
                        {
                            .parent = parent,
                            .size = {LiveSize(g->imgui, LFO_ItemWidth), param_popup_button_height},
                            .margins {
                                .l = LiveSize(g->imgui, LFO_ItemMarginL),
                                .r = LiveSize(g->imgui, LFO_ItemMarginR),
                                .tb = LiveSize(g->imgui, LFO_ItemGapY),
                            },
                        });
                    name = layout::CreateItem(g->layout,
                                              {
                                                  .parent = parent,
                                                  .size = {layout::k_fill_parent, param_popup_button_height},
                                              });
                };

                layout_item(c.lfo.target, c.lfo.target_name);
                layout_item(c.lfo.shape, c.lfo.shape_name);
                layout_item(c.lfo.mode, c.lfo.mode_name);

                auto knob_container =
                    layout::CreateItem(g->layout,
                                       {
                                           .parent = page_container,
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.t = LiveSize(g->imgui, LFO_GapYBeforeKnobs)},
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Middle,
                                       });

                LayoutParameterComponent(g,
                                         knob_container,
                                         c.lfo.amount,
                                         layer->params[ToInt(LayerParamIndex::LfoAmount)],
                                         Page_2KnobGapX);

                LayoutParameterComponent(
                    g,
                    knob_container,
                    c.lfo.rate,
                    layer->params[layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()
                                      ? ToInt(LayerParamIndex::LfoRateTempoSynced)
                                      : ToInt(LayerParamIndex::LfoRateHz)],
                    Page_2KnobGapX,
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
    auto col = LiveCol(imgui, UiColMap::LayerSelectorMenuLoading);
    auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);
    imgui.graphics->AddRectFilled(min, max, col, rounding);
}

void Draw(Gui* g,
          Engine* engine,
          Rect r,
          LayerProcessor* layer,
          LayerLayoutTempIDs& c,
          LayerLayout* layer_gui) {
    using enum UiSizeId;

    auto settings = FloeWindowSettings(g->imgui, [&](IMGUI_DRAW_WINDOW_BG_ARGS) {});
    settings.flags |= imgui::WindowFlags_NoScrollbarY;
    g->imgui.BeginWindow(settings, g->imgui.GetID((uintptr)layer), r);
    DEFER { g->imgui.EndWindow(); };

    auto const draw_divider = [&](layout::Id id) {
        auto line_r = layout::GetRect(g->layout, id);
        g->imgui.RegisterAndConvertRect(&line_r);
        g->imgui.graphics->AddLine({line_r.x, line_r.Bottom()},
                                   {line_r.Right(), line_r.Bottom()},
                                   LiveCol(g->imgui, UiColMap::LayerDividerLine));
    };

    // Inst selector
    {
        auto selector_left_id = g->imgui.GetID("SelcL");
        auto selector_right_id = g->imgui.GetID("SelcR");
        auto selector_menu_r = layout::GetRect(g->layout, c.selector_menu);
        auto selector_left_r = layout::GetRect(g->layout, c.selector_l);
        auto selector_right_r = layout::GetRect(g->layout, c.selector_r);

        bool should_highlight = false;
        if (layer->UsesTimbreLayering() &&
            (g->timbre_slider_is_held ||
             CcControllerMovedParamRecently(g->engine.processor, ParamIndex::MasterTimbre))) {
            should_highlight = true;
        }

        auto const registered_selector_box_r =
            g->imgui.GetRegisteredAndConvertedRect(layout::GetRect(g->layout, c.selector_box));
        {
            auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
            auto const col = should_highlight ? LiveCol(g->imgui, UiColMap::LayerSelectorMenuBackHighlight)
                                              : LiveCol(g->imgui, UiColMap::LayerSelectorMenuBack);
            g->imgui.graphics->AddRectFilled(registered_selector_box_r.Min(),
                                             registered_selector_box_r.Max(),
                                             col,
                                             rounding);
        }

        DoInstSelectorGUI(g, selector_menu_r, layer->index);
        if (auto percent =
                g->engine.sample_lib_server_async_channel.instrument_loading_percents[(usize)layer->index]
                    .Load(LoadMemoryOrder::Relaxed);
            percent != -1) {
            f32 const load_percent = (f32)percent / 100.0f;
            DrawSelectorProgressBar(g->imgui, registered_selector_box_r, load_percent);
            g->imgui.WakeupAtTimedInterval(g->redraw_counter, 0.1);
        }

        if (buttons::Button(g,
                            selector_left_id,
                            selector_left_r,
                            ICON_FA_CARET_LEFT,
                            buttons::IconButton(g->imgui))) {
            InstPickerContext context {
                .layer = *layer,
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };
            LoadAdjacentInstrument(context,
                                   g->inst_picker_state[layer->index],
                                   SearchDirection::Backward,
                                   false);
        }
        if (buttons::Button(g,
                            selector_right_id,
                            selector_right_r,
                            ICON_FA_CARET_RIGHT,
                            buttons::IconButton(g->imgui))) {
            InstPickerContext context {
                .layer = *layer,
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };
            LoadAdjacentInstrument(context,
                                   g->inst_picker_state[layer->index],
                                   SearchDirection::Forward,
                                   false);
        }
        {
            auto rand_id = g->imgui.GetID("Rand");
            auto rand_r = layout::GetRect(g->layout, c.selector_randomise);
            if (buttons::Button(g,
                                rand_id,
                                rand_r,
                                ICON_FA_SHUFFLE,
                                buttons::IconButton(g->imgui).WithRandomiseIconScaling())) {
                InstPickerContext context {
                    .layer = *layer,
                    .sample_library_server = g->shared_engine_systems.sample_library_server,
                    .library_images = g->library_images,
                    .engine = g->engine,
                    .unknown_library_icon = UnknownLibraryIcon(g),
                    .notifications = g->notifications,
                    .persistent_store = g->shared_engine_systems.persistent_store,
                };
                context.Init(g->scratch_arena);
                DEFER { context.Deinit(); };
                LoadRandomInstrument(context, g->inst_picker_state[layer->index], false);
            }
            Tooltip(g, rand_id, rand_r, "Load a random instrument"_s);
        }

        Tooltip(g, selector_left_id, selector_left_r, "Load the previous instrument"_s);
        Tooltip(g, selector_right_id, selector_right_r, "Load the next instrument"_s);
    }

    if (layer->instrument.tag == InstrumentType::None) return;

    // divider
    draw_divider(c.divider);

    auto const volume_knob_r = layout::GetRect(g->layout, c.volume);
    // level meter
    {
        auto const layer_peak_meter_width = LiveSize(g->imgui, LayerPeakMeterWidth);
        auto const layer_peak_meter_height = LiveSize(g->imgui, LayerPeakMeterHeight);
        auto const layer_peak_meter_bottom_gap = LiveSize(g->imgui, LayerPeakMeterBottomGap);

        Rect const peak_meter_r {.xywh {
            volume_knob_r.Centre().x - (layer_peak_meter_width / 2),
            volume_knob_r.y + (volume_knob_r.h - (layer_peak_meter_height + layer_peak_meter_bottom_gap)),
            layer_peak_meter_width,
            layer_peak_meter_height - layer_peak_meter_bottom_gap}};
        auto const& processor = engine->processor.layer_processors[(usize)layer->index];
        peak_meters::PeakMeter(g, peak_meter_r, processor.peak_meter, false);
    }

    // volume
    {
        auto const volume_name_h = layout::GetRect(g->layout, c.knob1.label).h;
        auto const volume_name_y_gap = LiveSize(g->imgui, LayerVolumeNameGapY);
        Rect const volume_name_r {.xywh {volume_knob_r.x,
                                         volume_knob_r.Bottom() - volume_name_h + volume_name_y_gap,
                                         volume_knob_r.w,
                                         volume_name_h}};

        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::Volume)],
                     volume_knob_r,
                     volume_name_r,
                     knobs::DefaultKnob(g->imgui));
    }

    // mute and solo
    {
        auto mute_solo_r = layout::GetRect(g->layout, c.mute_solo);
        Rect const mute_r = {.xywh {mute_solo_r.x, mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};
        Rect const solo_r = {
            .xywh {mute_solo_r.x + (mute_solo_r.w / 2), mute_solo_r.y, mute_solo_r.w / 2, mute_solo_r.h}};

        auto const col_border = LiveCol(g->imgui, UiColMap::MuteSoloButtonBorder);
        auto const col_background = LiveCol(g->imgui, UiColMap::MuteSoloButtonBackground);
        auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
        auto reg_mute_solo_r = g->imgui.GetRegisteredAndConvertedRect(mute_solo_r);
        auto reg_mute_r = g->imgui.GetRegisteredAndConvertedRect(mute_r);
        g->imgui.graphics->AddRectFilled(reg_mute_solo_r.Min(),
                                         reg_mute_solo_r.Max(),
                                         col_background,
                                         rounding);
        g->imgui.graphics->AddLine({reg_mute_r.Right(), reg_mute_r.y},
                                   {reg_mute_r.Right(), reg_mute_r.Bottom()},
                                   col_border);

        buttons::Toggle(g,
                        layer->params[ToInt(LayerParamIndex::Mute)],
                        mute_r,
                        "M",
                        buttons::MuteButton(g->imgui));
        buttons::Toggle(g,
                        layer->params[ToInt(LayerParamIndex::Solo)],
                        solo_r,
                        "S",
                        buttons::SoloButton(g->imgui));
    }

    // knobs
    {
        auto semitone_style = draggers::DefaultStyle(g->imgui);
        semitone_style.always_show_plus = true;
        draggers::Dragger(g,
                          layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                          c.knob1.control,
                          semitone_style);
        labels::Label(g,
                      layer->params[ToInt(LayerParamIndex::TuneSemitone)],
                      c.knob1.label,
                      labels::ParameterCentred(g->imgui));

        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::TuneCents)],
                     c.knob2,
                     knobs::BidirectionalKnob(g->imgui));
        KnobAndLabel(g,
                     layer->params[ToInt(LayerParamIndex::Pan)],
                     c.knob3,
                     knobs::BidirectionalKnob(g->imgui));
    }

    draw_divider(c.divider2);

    // current page
    switch (layer_gui->selected_page) {
        case PageType::Main: {
            // waveform
            {
                GUIDoSampleWaveform(g, layer, layout::GetRect(g->layout, c.main.waveform));

                labels::Label(g,
                              layout::GetRect(g->layout, c.main.waveform_label),
                              layer->InstTypeName(),
                              labels::WaveformLabel(g->imgui));

                bool const greyed_out = layer->inst.tag == InstrumentType::WaveformSynth;
                buttons::Toggle(g,
                                layer->params[ToInt(LayerParamIndex::Reverse)],
                                c.main.reverse,
                                buttons::ParameterToggleButton(g->imgui, {}, greyed_out));

                DoLoopModeSelectorGui(g, layout::GetRect(g->layout, c.main.loop_mode), *layer);
            }

            draw_divider(c.main.divider);

            // Envelope
            {
                buttons::Toggle(g,
                                layer->params[ToInt(LayerParamIndex::VolEnvOn)],
                                c.main.env_on,
                                buttons::LayerHeadingButton(g->imgui));
                bool const env_on = layer->params[ToInt(LayerParamIndex::VolEnvOn)].ValueAsBool() ||
                                    layer->instrument.tag == InstrumentType::WaveformSynth;
                GUIDoEnvelope(g,
                              layer,
                              layout::GetRect(g->layout, c.main.envelope),
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
            bool const greyed_out = !layer->params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::FilterOn)],
                            c.filter.filter_on,
                            buttons::LayerHeadingButton(g->imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::FilterType)],
                                    c.filter.filter_type,
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterCutoff)],
                         c.filter.cutoff,
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterResonance)],
                         c.filter.reso,
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::FilterEnvAmount)],
                         c.filter.env_amount,
                         knobs::BidirectionalKnob(g->imgui),
                         greyed_out);

            GUIDoEnvelope(g,
                          layer,
                          layout::GetRect(g->layout, c.filter.envelope),
                          greyed_out ||
                              (layer->params[ToInt(LayerParamIndex::FilterEnvAmount)].LinearValue() == 0),
                          {LayerParamIndex::FilterAttack,
                           LayerParamIndex::FilterDecay,
                           LayerParamIndex::FilterSustain,
                           LayerParamIndex::FilterRelease},
                          GuiEnvelopeType::Filter);

            break;
        }
        case PageType::Eq: {
            bool const greyed_out = !layer->params[ToInt(LayerParamIndex::EqOn)].ValueAsBool();
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::EqOn)],
                            layout::GetRect(g->layout, c.eq.on),
                            buttons::LayerHeadingButton(g->imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::EqType1)],
                                    layout::GetRect(g->layout, c.eq.type[0]),
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqFreq1)],
                         c.eq.freq[0],
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqResonance1)],
                         c.eq.reso[0],
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqGain1)],
                         c.eq.gain[0],
                         knobs::BidirectionalKnob(g->imgui),
                         greyed_out);

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::EqType2)],
                                    layout::GetRect(g->layout, c.eq.type[1]),
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqFreq2)],
                         c.eq.freq[1],
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqResonance2)],
                         c.eq.reso[1],
                         knobs::DefaultKnob(g->imgui),
                         greyed_out);
            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::EqGain2)],
                         c.eq.gain[1],
                         knobs::BidirectionalKnob(g->imgui),
                         greyed_out);

            break;
        }
        case PageType::Keyboard: {
            draggers::Dragger(g,
                              layer->params[ToInt(LayerParamIndex::MidiTranspose)],
                              c.play.transpose,
                              draggers::DefaultStyle(g->imgui));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::MidiTranspose)],
                          c.play.transpose_name,
                          labels::Parameter(g->imgui));
            {
                auto const label_id = g->imgui.GetID("transp");
                auto const label_r = layout::GetRect(g->layout, c.play.transpose_name);
                g->imgui.ButtonBehavior(g->imgui.GetRegisteredAndConvertedRect(label_r), label_id, {});
                Tooltip(g,
                        label_id,
                        label_r,
                        layer->params[ToInt(LayerParamIndex::MidiTranspose)].info.tooltip);
                if (g->imgui.IsHot(label_id)) g->imgui.frame_output.cursor_type = CursorType::Default;
            }

            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::Keytrack)],
                            c.play.keytrack,
                            buttons::MidiButton(g->imgui));
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::Monophonic)],
                            c.play.mono,
                            buttons::MidiButton(g->imgui));

            {
                {
                    auto label_r = layout::GetRect(g->layout, c.play.velo_name);
                    auto const imgui_id = g->imgui.GetID("vel->vol");
                    labels::Label(g, label_r, "Velocity to volume curve", labels::Parameter(g->imgui));
                    g->imgui.RegisterAndConvertRect(&label_r);
                    g->imgui.SetHot(label_r, imgui_id);
                    Tooltip(g, imgui_id, label_r, "Curve that maps velocity to volume"_s, true);
                }

                auto const velograph_r =
                    g->imgui.GetRegisteredAndConvertedRect(layout::GetRect(g->layout, c.play.velo_graph));

                if (DoCurveMap(g, layer->velocity_curve_map, velograph_r.Min(), velograph_r.Max(), ({
                                   Optional<f32> velocity {};
                                   if (g->engine.processor.voice_pool.num_active_voices.Load(
                                           LoadMemoryOrder::Relaxed)) {
                                       velocity =
                                           g->engine.processor.voice_pool.last_velocity[layer->index].Load(
                                               LoadMemoryOrder::Relaxed);
                                   }
                                   velocity;
                               })))
                    layer->velocity_curve_map.RenderCurveToLookupTable();
            }

            break;
        }
        case PageType::Lfo: {
            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::LfoOn)],
                            c.lfo.on,
                            buttons::LayerHeadingButton(g->imgui));
            auto const greyed_out = !layer->params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool();

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoDestination)],
                                    c.lfo.target,
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoDestination)],
                          c.lfo.target_name,
                          labels::Parameter(g->imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoRestart)],
                                    c.lfo.mode,
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoRestart)],
                          c.lfo.mode_name,
                          labels::Parameter(g->imgui));

            buttons::PopupWithItems(g,
                                    layer->params[ToInt(LayerParamIndex::LfoShape)],
                                    c.lfo.shape,
                                    buttons::ParameterPopupButton(g->imgui, greyed_out));
            labels::Label(g,
                          layer->params[ToInt(LayerParamIndex::LfoShape)],
                          c.lfo.shape_name,
                          labels::Parameter(g->imgui));

            KnobAndLabel(g,
                         layer->params[ToInt(LayerParamIndex::LfoAmount)],
                         c.lfo.amount,
                         knobs::BidirectionalKnob(g->imgui),
                         greyed_out);

            Parameter const* rate_param;
            if (layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)].ValueAsBool()) {
                rate_param = &layer->params[ToInt(LayerParamIndex::LfoRateTempoSynced)];
                buttons::PopupWithItems(g,
                                        *rate_param,
                                        c.lfo.rate.control,
                                        buttons::ParameterPopupButton(g->imgui, greyed_out));
            } else {
                rate_param = &layer->params[ToInt(LayerParamIndex::LfoRateHz)];
                knobs::Knob(g,
                            *rate_param,
                            c.lfo.rate.control,
                            knobs::DefaultKnob(g->imgui).GreyedOut(greyed_out));
            }

            auto const rate_name_r = layout::GetRect(g->layout, c.lfo.rate.label);
            labels::Label(g, *rate_param, rate_name_r, labels::ParameterCentred(g->imgui, greyed_out));

            auto const lfo_sync_switch_width = LiveSize(g->imgui, LFO_SyncSwitchWidth);
            auto const lfo_sync_switch_height = LiveSize(g->imgui, LFO_SyncSwitchHeight);
            auto const lfo_sync_switch_gap_y = LiveSize(g->imgui, LFO_SyncSwitchGapY);

            buttons::Toggle(g,
                            layer->params[ToInt(LayerParamIndex::LfoSyncSwitch)],
                            {.xywh {rate_name_r.x + (rate_name_r.w / 2) - (lfo_sync_switch_width / 2),
                                    rate_name_r.Bottom() + lfo_sync_switch_gap_y,
                                    lfo_sync_switch_width,
                                    lfo_sync_switch_height}},
                            buttons::ParameterToggleButton(g->imgui));

            break;
        }
        case PageType::Count: PanicIfReached();
    }

    // tabs
    for (auto const i : Range(k_num_pages)) {
        auto const page_type = (PageType)i;
        bool state = page_type == layer_gui->selected_page;
        auto const id = g->imgui.GetID((u64)i);
        auto const tab_r = layout::GetRect(g->layout, c.tabs[i]);
        auto const name {GetPageTitle(page_type)};
        bool const has_dot =
            (page_type == PageType::Filter &&
             layer->params[ToInt(LayerParamIndex::FilterOn)].ValueAsBool()) ||
            (page_type == PageType::Lfo && layer->params[ToInt(LayerParamIndex::LfoOn)].ValueAsBool()) ||
            (page_type == PageType::Eq && layer->params[ToInt(LayerParamIndex::EqOn)].ValueAsBool());
        if (buttons::Toggle(g, id, tab_r, state, name, buttons::LayerTabButton(g->imgui, has_dot)))
            layer_gui->selected_page = page_type;
        Tooltip(g, id, tab_r, fmt::Format(g->scratch_arena, "Open {} tab", name));
    }

    // overlay
    if (LayerIsSilent(engine->processor, layer->index)) {
        auto const pos = g->imgui.curr_window->unpadded_bounds.pos;
        g->imgui.graphics->AddRectFilled(pos,
                                         pos + g->imgui.Size(),
                                         LiveCol(g->imgui, UiColMap::LayerMutedOverlay));
    }
}

} // namespace layer_gui
