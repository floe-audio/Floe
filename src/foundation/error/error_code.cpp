// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

enum class TestErrors {
    Error1,
    Error2,
};

constexpr ErrorCodeCategory k_test_error_code_category = {
    .category_id = "CM",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((TestErrors)code.code) {
            case TestErrors::Error1: str = "error 1"; break;
            case TestErrors::Error2: str = "error 2"; break;
        }
        return writer.WriteChars(str);
    }};

inline ErrorCodeCategory const& ErrorCategoryForEnum(TestErrors) { return k_test_error_code_category; }

TEST_CASE(TestErrorCode) {
    auto const e1 = ErrorCode {TestErrors::Error1};
    CHECK(e1.category == &k_test_error_code_category);
    CHECK(e1.code == (s64)TestErrors::Error1);
    CHECK(e1 == TestErrors::Error1);
    CHECK(e1 != TestErrors::Error2);
    CHECK(e1 == ErrorCode {TestErrors::Error1});

    auto const e2 = ErrorCode {TestErrors::Error2};
    CHECK(e1 != e2);

    return k_success;
}

TEST_REGISTRATION(RegisterErrorCodeTests) { REGISTER_TEST(TestErrorCode); }
