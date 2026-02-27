// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "renderer.hpp"

#include "foundation/foundation.hpp"

#if !IS_WINDOWS
Renderer* CreateNewRendererDirect3D9() {
    PanicIfReached();
    return nullptr;
}
#else
Renderer* CreateNewRendererOpenGl() {
    PanicIfReached();
    return nullptr;
}
#endif
