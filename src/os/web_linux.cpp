// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <curl/curl.h>

#include "utils/debug/tracy_wrapped.hpp"
#include "utils/logger/logger.hpp"

#include "misc.hpp"
#include "web.hpp"

size_t WriteFunction(void* ptr, size_t size, size_t nmemb, void* data) {
    if (!data) return size * nmemb;
    auto& writer = *(Writer*)data;
    auto _ = writer.WriteBytes({(u8 const*)ptr, size * nmemb});
    return size * nmemb;
}

void WebGlobalInit() {
    ZoneScoped;
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void WebGlobalCleanup() {
    ZoneScoped;
    curl_global_cleanup();
}

ErrorCodeOr<void> HttpsGet(String url, Writer writer, RequestOptions options) {
    auto curl = curl_easy_init();
    if (!curl) return ErrorCode {WebError::ApiError};
    DEFER { curl_easy_cleanup(curl); };

    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};

    curl_easy_setopt(curl, CURLOPT_URL, NullTerminated(url, arena));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/" LIBCURL_VERSION);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFunction);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writer);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (unsigned)options.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (unsigned)options.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    char error_buffer[CURL_ERROR_SIZE] {};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    auto const return_code = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        LogDebug({}, "Reponse is non-200: {}, error: {}", http_code, FromNullTerminated(error_buffer));
        return ErrorCode {WebError::Non200Response};
    }
    if (return_code != CURLE_OK) {
        if (return_code == CURLE_OPERATION_TIMEDOUT || return_code == CURLE_COULDNT_CONNECT ||
            return_code == CURLE_COULDNT_RESOLVE_HOST) {
            return ErrorCode {WebError::NetworkError};
        }
        LogDebug({}, "CURL ERROR: {}, {}", (int)return_code, FromNullTerminated(error_buffer));
        return ErrorCode {WebError::ApiError};
    }

    return k_success;
}

ErrorCodeOr<void>
HttpsPost(String url, String body, Optional<Writer> response_writer, RequestOptions options) {
    auto curl = curl_easy_init();
    if (!curl) return ErrorCode {WebError::ApiError};
    DEFER { curl_easy_cleanup(curl); };

    ArenaAllocatorWithInlineStorage<1000> arena {Malloc::Instance()};

    curl_easy_setopt(curl, CURLOPT_URL, NullTerminated(url, arena));

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NullTerminated(body, arena));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFunction);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_writer ? &*response_writer : nullptr);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (unsigned)options.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (unsigned)options.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);

    struct curl_slist* curl_headers = nullptr;
    DEFER { curl_slist_free_all(curl_headers); };
    if (options.headers.size) {
        for (auto const& header : options.headers) {
            ASSERT(header.size);
            curl_headers = curl_slist_append(curl_headers, NullTerminated(header, arena));
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    char error_buffer[CURL_ERROR_SIZE] {};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    auto const return_code = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        LogDebug({}, "Reponse is non-200: {}, error: {}", http_code, FromNullTerminated(error_buffer));
        return ErrorCode {WebError::Non200Response};
    }
    if (return_code != CURLE_OK) {
        LogDebug({}, "CURL ERROR: {}, {}", (int)return_code, FromNullTerminated(error_buffer));
        return ErrorCode {WebError::ApiError};
    }

    return k_success;
}
