// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui/core/gui_fwd.hpp"
#include "processor/layer_processor.hpp"

enum class GuiEnvelopeType { Volume, Filter, Count };

constexpr usize k_num_adsr_params = 4;

struct GuiEnvelopeCursor {
    f32 cursor {};
    OnePoleLowPassFilter<f32> cursor_smoother {};
    u64 marker_id {(u64)-1};
};

void DoEnvelopeGui(GuiState& g,
                   LayerProcessor& layer,
                   Rect viewport_r,
                   bool greyed_out,
                   Array<LayerParamIndex, k_num_adsr_params> adsr_layer_params,
                   GuiEnvelopeType type);
