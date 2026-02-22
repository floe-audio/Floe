// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_widget_compounds.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_state.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_label_widgets.hpp"
#include "gui_widget_helpers.hpp"

// IMPORTANT: This file considered technical debt. It's due to be superseded by new code that uses better
// techniques for the same result by leaning into the GuiBuilder system amongst other things.
// The problems with this file:
// - Use of separate layout and then draw steps rather than our new unified builder.
// - Messy functions
// - Many function arguments rather an options structs with designated initialiser syntax
// - Overuse of function default arguments
// - Use of monolithic 'GuiState' struct rather than specific arguments

layout::Id LayoutParameterComponent(GuiState& g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    LayoutType type,
                                    Optional<ParamIndex> index_for_menu_items,
                                    bool is_convo_ir,
                                    Optional<UiSizeId> size_index_for_gapx,
                                    bool set_gapx_independent_of_size,
                                    bool set_bottom_gap_independent_of_size) {
    auto width = type == LayoutType::Layer
                     ? LivePx(UiSizeId::KnobLargeW)
                     : (type == LayoutType::Effect ? LivePx(UiSizeId::ParamComponentSmallWidth)
                                                   : LivePx(UiSizeId::ParamComponentExtraSmallWidth));
    auto const starting_width = width;
    auto height = width - LivePx(UiSizeId::ParamComponentHeightOffset);
    auto const starting_height = height;
    auto gap_x =
        size_index_for_gapx ? LivePx(*size_index_for_gapx) : LivePx(UiSizeId::ParamComponentMarginLR);
    ASSERT(gap_x >= 0.0f);
    auto gap_bottom = LivePx(UiSizeId::ParamComponentMarginB);
    auto gap_top = LivePx(UiSizeId::ParamComponentMarginT);

    auto const param_popup_button_height = LivePx(UiSizeId::ParamPopupButtonHeight);
    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width = MaxStringLength(g, menu_items) + (LivePx(UiSizeId::MenuTextMarginL) * 2);
        auto const btn_w = LivePx(UiSizeId::NextPrevButtonSize);
        auto const margin_r = LivePx(UiSizeId::ParamIntButtonMarginR);
        strings_width += btn_w * 2 + margin_r;
        width = strings_width;
        height = param_popup_button_height;
    } else if (is_convo_ir) {
        height = param_popup_button_height;
        width = LivePx(UiSizeId::FXConvoIRWidth);
    }

    if (set_gapx_independent_of_size && width != starting_width)
        gap_x = Max(0.0f, gap_x - ((width - starting_width) / 2));

    if (set_bottom_gap_independent_of_size && height != starting_height) {
        auto const delta = Max(0.0f, starting_height - height);
        gap_bottom += delta / 2;
        gap_top += delta / 2;
    }

    auto container = layout::CreateItem(g.layout,
                                        g.scratch_arena,
                                        {
                                            .parent = parent,
                                            .size = layout::k_hug_contents,
                                            .margins {.lr = gap_x, .t = gap_top, .b = gap_bottom},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                        });

    ids.control = layout::CreateItem(g.layout,
                                     g.scratch_arena,
                                     {
                                         .parent = container,
                                         .size = {width, height},
                                         .margins = {.b = LivePx(UiSizeId::ParamComponentLabelGapY)},
                                     });
    ids.label = layout::CreateItem(g.layout,
                                   g.scratch_arena,
                                   {
                                       .parent = container,
                                       .size = {width, g.fonts.Current()->font_size},
                                   });

    return container;
}

layout::Id LayoutParameterComponent(GuiState& g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    DescribedParamValue const& param,
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
        layout::SetSize(g.layout,
                        ids.control,
                        f32x2 {LivePx(UiSizeId::FXDraggerWidth), LivePx(UiSizeId::FXDraggerHeight)});
        auto margins = layout::GetMargins(g.layout, ids.control);
        margins.t = LivePx(UiSizeId::FXDraggerMarginT);
        margins.b = LivePx(UiSizeId::FXDraggerMarginB);
        layout::SetMargins(g.layout, ids.control, margins);
    }

    return result;
}

bool KnobAndLabel(GuiState& g,
                  DescribedParamValue const& param,
                  Rect knob_r,
                  Rect label_r,
                  knobs::Style const& style,
                  bool greyed_out) {
    knobs::Style knob_style = style;
    knob_style.GreyedOut(greyed_out);
    if (param.info.display_format == ParamDisplayFormat::VolumeAmp)
        knob_style.overload_position = param.info.LineariseValue(1, true);
    bool const changed = knobs::Knob(g, param, knob_r, knob_style);
    labels::Label(g, param, label_r, labels::ParameterCentred(g.imgui, greyed_out));
    return changed;
}

bool KnobAndLabel(GuiState& g,
                  DescribedParamValue const& param,
                  LayIDPair ids,
                  knobs::Style const& style,
                  bool greyed_out) {
    return KnobAndLabel(g,
                        param,
                        layout::GetRect(g.layout, ids.control),
                        layout::GetRect(g.layout, ids.label),
                        style,
                        greyed_out);
}
