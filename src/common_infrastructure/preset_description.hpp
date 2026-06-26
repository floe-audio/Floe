// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "foundation/utils/string.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/loop_behaviour.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

struct AutoDescriptionLayerInfo {
    String inst_name;
    LoopBehaviour actual_loop_behaviour;
};

enum class AutoDescriptionForm : u8 {
    // Single-line headline, optionally followed by a "[folder]" annotation.
    Headline,
    // Headline plus supplementary detail joined into a single line with no embedded '\n'.
    FullBlock,
};

struct AutoDescriptionWriteOptions {
    AutoDescriptionForm form {};
    u64 random_seed {};
    // Only used when form == Headline. If non-empty, included as a "[folder]" annotation.
    String folder_name {};
};

// Render an auto-generated prose description for the given snapshot. The returned String is allocated
// from `allocator` and contains no '\n'.
String WriteAutoDescription(Allocator& allocator,
                            StateSnapshot const& state,
                            Array<AutoDescriptionLayerInfo, k_num_layers> const& layer_info,
                            AutoDescriptionWriteOptions options);

enum class LongDescriptionKind : u8 {
    Auto,
    User,
    UserContinued,
};
