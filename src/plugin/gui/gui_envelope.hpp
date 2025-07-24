// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "gui_fwd.hpp"
#include "processor/layer_processor.hpp"

enum class GuiEnvelopeType { Volume, Filter, Count };

struct GuiEnvelopeCursor {
    f32 cursor {};
    OnePoleLowPassFilter<f32> cursor_smoother {};
    u64 marker_id {(u64)-1};
};

void GUIDoEnvelope(Gui* g,
                   LayerProcessor* layer,
                   Rect r,
                   bool greyed_out,
                   Array<LayerParamIndex, 4> adsr_layer_params,
                   GuiEnvelopeType type);
