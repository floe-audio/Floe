// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_compounds.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"

layout::Id LayoutParameterComponent(Gui* g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    LayoutType type,
                                    Optional<ParamIndex> index_for_menu_items,
                                    bool is_convo_ir,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    auto& imgui = g->imgui;

    auto width = type == LayoutType::Layer ? LiveSize(imgui, UiSizeId::ParamComponentLargeWidth)
                                           : (type == LayoutType::Effect
                                                  ? LiveSize(imgui, UiSizeId::ParamComponentSmallWidth)
                                                  : LiveSize(imgui, UiSizeId::ParamComponentExtraSmallWidth));
    auto const starting_width = width;
    auto height = width - LiveSize(imgui, UiSizeId::ParamComponentHeightOffset);
    auto const starting_height = height;
    auto gap_x = size_index_for_gapx ? LiveSize(imgui, *size_index_for_gapx)
                                     : LiveSize(imgui, UiSizeId::ParamComponentMarginLR);
    ASSERT(gap_x >= 0.0f);
    auto gap_bottom = LiveSize(imgui, UiSizeId::ParamComponentMarginB);
    auto gap_top = LiveSize(imgui, UiSizeId::ParamComponentMarginT);

    auto const param_popup_button_height = LiveSize(imgui, UiSizeId::ParamPopupButtonHeight);
    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width =
            MaxStringLength(g, menu_items) + (LiveSize(imgui, UiSizeId::MenuButtonTextMarginL) * 2);
        auto const btn_w = LiveSize(g->imgui, UiSizeId::NextPrevButtonSize);
        auto const margin_r = LiveSize(g->imgui, UiSizeId::ParamIntButtonMarginR);
        strings_width += btn_w * 2 + margin_r;
        width = strings_width;
        height = param_popup_button_height;
    } else if (is_convo_ir) {
        height = param_popup_button_height;
        width = LiveSize(imgui, UiSizeId::FXConvoIRWidth);
    }

    if (set_gapx_independent_of_size && width != starting_width)
        gap_x = Max(0.0f, gap_x - ((width - starting_width) / 2));

    if (set_bottom_gap_independent_of_size && height != starting_height) {
        auto const delta = Max(0.0f, starting_height - height);
        gap_bottom += delta / 2;
        gap_top += delta / 2;
    }

    auto container = layout::CreateItem(g->layout,
                                        g->scratch_arena,
                                        {
                                            .parent = parent,
                                            .size = layout::k_hug_contents,
                                            .margins {.lr = gap_x, .t = gap_top, .b = gap_bottom},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        });

    ids.control = layout::CreateItem(g->layout,
                                     g->scratch_arena,
                                     {
                                         .parent = container,
                                         .size = {width, height},
                                         .margins = {.b = LiveSize(imgui, UiSizeId::ParamComponentLabelGapY)},
                                     });
    ids.label = layout::CreateItem(g->layout,
                                   g->scratch_arena,
                                   {
                                       .parent = container,
                                       .size = {width, (imgui.graphics->context->CurrentFontSize())},
                                   });

    return container;
}

layout::Id LayoutParameterComponent(Gui* g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    Parameter const& param,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    auto result = LayoutParameterComponent(
        g,
        parent,
        ids,
        param.info.IsLayerParam() ? LayoutType::Layer
                                  : (param.info.IsEffectParam() ? LayoutType::Effect : LayoutType::Generic),
        param.info.value_type == ParamValueType::Menu ? Optional<ParamIndex> {param.info.index} : k_nullopt,
        false,
        size_index_for_gapx,
        set_gapx_independent_of_size,
        set_bottom_gap_independent_of_size);

    if (param.info.value_type == ParamValueType::Int) {
        layout::SetSize(g->layout,
                        ids.control,
                        f32x2 {LiveSize(g->imgui, UiSizeId::FXDraggerWidth),
                               LiveSize(g->imgui, UiSizeId::FXDraggerHeight)});
        auto margins = layout::GetMargins(g->layout, ids.control);
        margins.t = LiveSize(g->imgui, UiSizeId::FXDraggerMarginT);
        margins.b = LiveSize(g->imgui, UiSizeId::FXDraggerMarginB);
        layout::SetMargins(g->layout, ids.control, margins);
    }

    return result;
}

bool KnobAndLabel(Gui* g,
                  Parameter const& param,
                  Rect knob_r,
                  Rect label_r,
                  knobs::Style const& style,
                  bool greyed_out) {
    knobs::Style knob_style = style;
    knob_style.GreyedOut(greyed_out);
    if (param.info.display_format == ParamDisplayFormat::VolumeAmp)
        knob_style.overload_position = param.info.LineariseValue(1, true);
    bool const changed = knobs::Knob(g, param, knob_r, knob_style);
    labels::Label(g, param, label_r, labels::ParameterCentred(g->imgui, greyed_out));
    return changed;
}

bool KnobAndLabel(Gui* g, Parameter const& param, LayIDPair ids, knobs::Style const& style, bool greyed_out) {
    return KnobAndLabel(g,
                        param,
                        layout::GetRect(g->layout, ids.control),
                        layout::GetRect(g->layout, ids.label),
                        style,
                        greyed_out);
}
