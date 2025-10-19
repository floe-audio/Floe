// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <winhttp.h>

#include "os/undef_windows_macros.h"
//

#include "os/misc_windows.hpp"
#include "utils/logger/logger.hpp"

#include "misc.hpp"
#include "web.hpp"

enum class HttpMethod { Get, Post };

static Optional<String> WinhttpErrorMessage(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_OUT_OF_HANDLES: return "out of handles";
        case ERROR_WINHTTP_TIMEOUT: return "timeout";
        case ERROR_WINHTTP_INTERNAL_ERROR: return "internal error";
        case ERROR_WINHTTP_INVALID_URL: return "invalid URL";
        case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return "unrecognized scheme";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "name not resolved";
        case ERROR_WINHTTP_INVALID_OPTION: return "invalid option";
        case ERROR_WINHTTP_OPTION_NOT_SETTABLE: return "option not settable";
        case ERROR_WINHTTP_SHUTDOWN: return "shutdown";
        case ERROR_WINHTTP_LOGIN_FAILURE: return "login failure";
        case ERROR_WINHTTP_OPERATION_CANCELLED: return "operation cancelled";
        case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE: return "incorrect handle type";
        case ERROR_WINHTTP_INCORRECT_HANDLE_STATE: return "incorrect handle state";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "cannot connect";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "connection error";
        case ERROR_WINHTTP_RESEND_REQUEST: return "resend request";
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID: return "secure cert date invalid";
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID: return "secure cert CN invalid";
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED: return "client auth cert needed";
        case ERROR_WINHTTP_SECURE_INVALID_CA: return "secure invalid CA";
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED: return "secure cert revocation failed";
        case ERROR_WINHTTP_CANNOT_CALL_BEFORE_OPEN: return "cannot call before open";
        case ERROR_WINHTTP_CANNOT_CALL_BEFORE_SEND: return "cannot call before send";
        case ERROR_WINHTTP_CANNOT_CALL_AFTER_SEND: return "cannot call after send";
        case ERROR_WINHTTP_CANNOT_CALL_AFTER_OPEN: return "cannot call after open";
        case ERROR_WINHTTP_HEADER_NOT_FOUND: return "header not found";
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: return "invalid server response";
        case ERROR_WINHTTP_INVALID_HEADER: return "invalid header";
        case ERROR_WINHTTP_INVALID_QUERY_REQUEST: return "invalid query request";
        case ERROR_WINHTTP_HEADER_ALREADY_EXISTS: return "header already exists";
        case ERROR_WINHTTP_REDIRECT_FAILED: return "redirect failed";
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR: return "secure channel error";
        case ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT: return "bad auto proxy script";
        case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT: return "unable to download script";
        case ERROR_WINHTTP_SECURE_INVALID_CERT: return "secure invalid cert";
        case ERROR_WINHTTP_SECURE_CERT_REVOKED: return "secure cert revoked";
        case ERROR_WINHTTP_NOT_INITIALIZED: return "not initialized";
        case ERROR_WINHTTP_SECURE_FAILURE: return "secure failure";
        case ERROR_WINHTTP_UNHANDLED_SCRIPT_TYPE: return "unhandled script type";
        case ERROR_WINHTTP_SCRIPT_EXECUTION_ERROR: return "script execution error";
        case ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR: return "auto proxy service error";
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return "secure cert wrong usage";
        case ERROR_WINHTTP_AUTODETECTION_FAILED: return "autodetection failed";
        case ERROR_WINHTTP_HEADER_COUNT_EXCEEDED: return "header count exceeded";
        case ERROR_WINHTTP_HEADER_SIZE_OVERFLOW: return "header size overflow";
        case ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW:
            return "chunked encoding header size overflow";
        case ERROR_WINHTTP_RESPONSE_DRAIN_OVERFLOW: return "response drain overflow";
        case ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY: return "client cert no private key";
        case ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY: return "client cert no access private key";
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED_PROXY: return "client auth cert needed proxy";
        case ERROR_WINHTTP_SECURE_FAILURE_PROXY: return "secure failure proxy";
        case ERROR_WINHTTP_RESERVED_189: return "reserved 189";
        case ERROR_WINHTTP_HTTP_PROTOCOL_MISMATCH: return "HTTP protocol mismatch";
    }
    return k_nullopt;
}

static ErrorCode LogAndReturn(ErrorCode code) {
    auto const err = GetLastError();
    if (auto msg = WinhttpErrorMessage(err))
        LogDebug({}, "WinHTTP error: {}", *msg);
    else
        LogDebug({},
                 "WinHTTP (Windows error): {}",
                 Win32ErrorCode(err, code.extra_debug_info, code.source_location));
    return code;
}

static ErrorCodeOr<void> HttpRequestImpl(String url,
                                         HttpMethod method,
                                         Optional<String> body,
                                         Optional<Writer> response_writer,
                                         RequestOptions options) {
    ArenaAllocatorWithInlineStorage<1000> temp_arena {Malloc::Instance()};

    URL_COMPONENTS url_comps {};
    url_comps.dwStructSize = sizeof(URL_COMPONENTS);
    url_comps.dwHostNameLength = (DWORD)-1;
    url_comps.dwUrlPathLength = (DWORD)-1;
    auto wide_url = *Widen(temp_arena, url);
    if (!WinHttpCrackUrl(wide_url.data, (DWORD)wide_url.size, 0, &url_comps))
        return LogAndReturn(ErrorCode {WebError::ApiError});

    ASSERT(url_comps.lpszHostName);
    ASSERT(url_comps.dwHostNameLength);
    ASSERT(url_comps.lpszUrlPath);
    ASSERT(url_comps.dwUrlPathLength);

    auto const server = NullTerminated({url_comps.lpszHostName, url_comps.dwHostNameLength}, temp_arena);
    auto const path = NullTerminated({url_comps.lpszUrlPath, url_comps.dwUrlPathLength}, temp_arena);

    auto session =
        WinHttpOpen(L"Floe", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) return LogAndReturn(ErrorCode {WebError::NetworkError});
    DEFER { WinHttpCloseHandle(session); };

    auto const timeout_ms = int(options.timeout_seconds * 1000);
    WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

    // Detect if this is HTTPS or HTTP
    bool const is_https = url_comps.nScheme == INTERNET_SCHEME_HTTPS;

    if (is_https) {
        unsigned long protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    INTERNET_PORT const port = url_comps.nPort
                                   ? url_comps.nPort
                                   : (is_https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    auto connection = WinHttpConnect(session, server, port, 0);
    if (connection == nullptr) return LogAndReturn(ErrorCode {WebError::NetworkError});
    DEFER { WinHttpCloseHandle(connection); };

    wchar_t const* method_str = (method == HttpMethod::Get) ? L"GET" : L"POST";
    auto request = WinHttpOpenRequest(connection,
                                      method_str,
                                      path,
                                      nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      is_https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) return LogAndReturn(ErrorCode {WebError::NetworkError});
    DEFER { WinHttpCloseHandle(request); };

    // Add custom headers
    for (auto const& header : options.headers) {
        auto wide_header = *Widen(temp_arena, header);
        if (!WinHttpAddRequestHeaders(request,
                                      wide_header.data,
                                      (DWORD)wide_header.size,
                                      WINHTTP_ADDREQ_FLAG_ADD)) {
            return LogAndReturn(ErrorCode {WebError::ApiError});
        }
    }

    // Send request (with or without body)
    if (method == HttpMethod::Post && body.HasValue()) {
        if (!WinHttpSendRequest(request,
                                WINHTTP_NO_ADDITIONAL_HEADERS,
                                0,
                                (LPVOID)body->data,
                                (DWORD)body->size,
                                (DWORD)body->size,
                                0)) {
            return LogAndReturn(ErrorCode {WebError::NetworkError});
        }
    } else {
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            return LogAndReturn(ErrorCode {WebError::NetworkError});
    }

    if (!WinHttpReceiveResponse(request, nullptr)) return LogAndReturn(ErrorCode {WebError::NetworkError});

    // Process response if writer is provided
    if (response_writer.HasValue()) {
        DWORD bytes_available = 0;
        DynamicArray<u8> out_buffer {PageAllocator::Instance()};
        do {
            bytes_available = 0;
            if (WinHttpQueryDataAvailable(request, &bytes_available)) {
                dyn::Resize(out_buffer, bytes_available);
                DWORD bytes_read = 0;
                if (!WinHttpReadData(request, (LPVOID)(out_buffer.data), bytes_available, &bytes_read))
                    return LogAndReturn(ErrorCode {WebError::NetworkError});
                TRY(response_writer->WriteBytes({out_buffer.data, bytes_read}));
            } else {
                return LogAndReturn(ErrorCode {WebError::NetworkError});
            }
        } while (bytes_available > 0);
    }

    return k_success;
}

ErrorCodeOr<void> HttpsGet(String url, Writer response_writer, RequestOptions options) {
    return HttpRequestImpl(url, HttpMethod::Get, k_nullopt, response_writer, options);
}

ErrorCodeOr<void>
HttpsPost(String url, String body, Optional<Writer> response_writer, RequestOptions options) {
    return HttpRequestImpl(url, HttpMethod::Post, body, response_writer, options);
}

void WebGlobalInit() {}
void WebGlobalCleanup() {}
