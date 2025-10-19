// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "foundation/foundation.hpp"

enum class WebError : u32 {
    ApiError,
    NetworkError,
    Non200Response,
    Count,
};

extern ErrorCodeCategory const g_web_error_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(WebError) { return g_web_error_category; }

void WebGlobalInit();
void WebGlobalCleanup();

struct RequestOptions {
    Span<String const> headers;
    f32 timeout_seconds = 10.0f;
};

// These block until complete. They support HTTP and HTTPS.
ErrorCodeOr<void> HttpsGet(String url, Writer writer, RequestOptions options = {});
ErrorCodeOr<void>
HttpsPost(String url, String body, Optional<Writer> response_writer, RequestOptions options = {});
