// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_knob_widgets.hpp"

// IMPORTANT: This file considered technical debt. It's due to be superseded by new code that uses better
// techniques for the same result by leaning into the GuiBuilder system amongst other things.
// The problems with this file:
// - Use of separate layout and then draw steps rather than our new unified builder.
// - Messy functions
// - Many function arguments rather an options structs with designated initialiser syntax
// - Overuse of function default arguments
// - Use of monolithic 'GuiState' struct rather than specific arguments

struct LayIDPair {
    layout::Id control;
    layout::Id label;
};

enum class LayoutType { Generic, Layer, Effect };

layout::Id LayoutParameterComponent(GuiState& g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    LayoutType type,
                                    Optional<ParamIndex> index_for_menu_items,
                                    bool is_convo_ir,
                                    Optional<UiSizeId> size_index_for_gapx = {},
                                    bool set_gapx_independent_of_size = false,
                                    bool set_bottom_gap_independent_of_size = false);

layout::Id LayoutParameterComponent(GuiState& g,
                                    layout::Id parent,
                                    LayIDPair& ids,
                                    DescribedParamValue const& param,
                                    Optional<UiSizeId> size_index_for_gapx = {},
                                    bool set_gapx_independent_of_size = false,
                                    bool set_bottom_gap_independent_of_size = false);

bool KnobAndLabel(GuiState& g,
                  DescribedParamValue const& param,
                  Rect knob_r,
                  Rect label_r,
                  knobs::Style const& style,
                  bool greyed_out = false);

bool KnobAndLabel(GuiState& g,
                  DescribedParamValue const& param,
                  LayIDPair ids,
                  knobs::Style const& style,
                  bool greyed_out = false);
