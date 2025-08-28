// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

enum class CommonError {
    InvalidFileFormat,
    CorruptFile,
    PluginHostError,
    CurrentFloeVersionTooOld,
    NotFound,
};

extern ErrorCodeCategory const g_common_error_category;
inline ErrorCodeCategory const& ErrorCategoryForEnum(CommonError) { return g_common_error_category; }
