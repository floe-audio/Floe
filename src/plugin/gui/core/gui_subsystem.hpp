// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/persistent_store.hpp"

template <typename State>
struct GuiSubsystem {
    void (*encode)(State const&, persistent_store::StoreTable&, ArenaAllocator&) = nullptr;
    void (*decode)(State&, persistent_store::StoreTable const&) = nullptr;
};
