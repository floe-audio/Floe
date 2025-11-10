// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/threading.hpp"

struct StartingGun {
    void WaitUntilFired() {
        while (true) {
            WaitIfValueIsExpected(value, 0);
            if (value.Load(LoadMemoryOrder::Acquire) == 1) return;
        }
    }
    void Fire() {
        value.Store(1, StoreMemoryOrder::Release);
        WakeWaitingThreads(value, NumWaitingThreads::All);
    }
    Atomic<u32> value {0};
};
