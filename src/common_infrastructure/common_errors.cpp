// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common_errors.hpp"

#include "foundation/foundation.hpp"

ErrorCodeCategory const g_common_error_category = {
    .category_id = "CM",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((CommonError)code.code) {
            case CommonError::InvalidFileFormat: str = "invalid file format"; break;
            case CommonError::CorruptFile: str = "corrupt file"; break;
            case CommonError::CurrentFloeVersionTooOld: str = "current Floe version too old"; break;
            case CommonError::PluginHostError: str = "plugin host error"; break;
            case CommonError::NotFound: str = "item not found"; break;
        }
        return writer.WriteChars(str);
    },
};
