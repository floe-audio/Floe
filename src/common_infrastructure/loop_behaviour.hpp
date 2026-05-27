// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

enum class LoopBehaviourId : u8 {
    NoLoop,
    BuiltinLoopStandard,
    BuiltinLoopPingPong,
    CustomLoopStandard,
    CustomLoopPingPong,
    MixedLoops,
    MixedNonLoopsAndLoops,
};

struct LoopBehaviour {
    struct Value {
        LoopBehaviourId id;
        Optional<sample_lib::LoopMode> mode;
        String name;
        String short_name;
        String description;
        bool editable;
    };
    Value value;
    String reason;
    bool is_desired;
};

PUBLIC bool IsBuiltinLoop(LoopBehaviourId id) {
    switch (id) {
        case LoopBehaviourId::BuiltinLoopStandard:
        case LoopBehaviourId::BuiltinLoopPingPong: return true;
        case LoopBehaviourId::NoLoop:
        case LoopBehaviourId::CustomLoopStandard:
        case LoopBehaviourId::CustomLoopPingPong:
        case LoopBehaviourId::MixedLoops:
        case LoopBehaviourId::MixedNonLoopsAndLoops: return false;
    }
    PanicIfReached();
    return false;
}
