// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web.hpp"

#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

ErrorCodeCategory const g_web_error_category {
    .category_id = "WB",
    .message = [](Writer const& writer, ErrorCode e) -> ErrorCodeOr<void> {
        auto const get_str = [code = e.code]() -> String {
            switch ((WebError)code) {
                case WebError::ApiError: return "API error";
                case WebError::NetworkError: return "network error";
                case WebError::Non200Response: return "non-200 response";
                case WebError::Count: break;
            }
            return "";
        };
        return writer.WriteChars(get_str());
    },
};

TEST_CASE(TestWeb) {
    // We expect a local test server to be running.
    constexpr auto k_base_url = "http://127.0.0.1:8081"_s;

    WebGlobalInit();
    DEFER { WebGlobalCleanup(); };

    {
        DynamicArray<char> buffer {tester.scratch_arena};
        auto const get_url = fmt::Join(tester.scratch_arena, Array {k_base_url, "/get"}, ""_s);
        auto o = HttpsGet(get_url, dyn::WriterFor(buffer));
        if (o.HasError()) {
            LOG_WARNING("Failed to HttpsGet: {}", o.Error());
        } else {
            tester.log.Debug("GET response: {}", buffer);

            using namespace json;
            auto parse_o = json::Parse(buffer,
                                       [&](EventHandlerStack&, Event const& event) {
                                           String url;
                                           if (SetIfMatchingRef(event, "url", url)) {
                                               CHECK_EQ(url, get_url);
                                               return true;
                                           }
                                           return false;
                                       },
                                       tester.scratch_arena,
                                       {});
            if (parse_o.HasError())
                TEST_FAILED("Invalid HTTP GET JSON response: {}", parse_o.Error().message);
        }
    }

    {
        DynamicArray<char> buffer {tester.scratch_arena};
        auto const post_url = fmt::Join(tester.scratch_arena, Array {k_base_url, "/post"}, ""_s);
        auto o = HttpsPost(post_url,
                           "data",
                           dyn::WriterFor(buffer),
                           {
                               .headers = Array {"Content-Type: text/plain"_s},
                           });
        if (o.HasError()) {
            LOG_WARNING("Failed to HttpsPost: {}", o.Error());
        } else {
            tester.log.Debug("POST response: {}", buffer);

            using namespace json;
            auto parse_o = json::Parse(buffer,
                                       [&](EventHandlerStack&, Event const& event) {
                                           String data;
                                           if (SetIfMatchingRef(event, "data", data)) {
                                               CHECK_EQ(data, "data"_s);
                                               return true;
                                           }
                                           return false;
                                       },
                                       tester.scratch_arena,
                                       {});
            if (parse_o.HasError())
                TEST_FAILED("Invalid HTTP POST JSON response: {}", parse_o.Error().message);
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterWebTests) { REGISTER_TEST(TestWeb); }
