// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestFormatStringReplace) {
    auto& a = tester.scratch_arena;
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA__ bar __BBB__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                          {"__BBB__", "bar"},
                                      })),
             "test foo bar bar"_s);
    CHECK_EQ(fmt::FormatStringReplace(a,
                                      "test __AAA____AAA__",
                                      ArrayT<fmt::StringReplacement>({
                                          {"__AAA__", "foo"},
                                      })),
             "test foofoo"_s);
    CHECK_EQ(fmt::FormatStringReplace(a, "abc", {}), "abc"_s);
    return k_success;
}

TEST_CASE(TestIntToString) {
    auto to_string = [](int value, fmt::IntToStringOptions options) {
        DynamicArrayBounded<char, 32> result;
        auto size = IntToString(value, result.data, options);
        result.ResizeWithoutCtorDtor(size);
        return result;
    };

    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Decimal}) == "10"_s);
    CHECK(to_string(-99, {.base = fmt::IntToStringOptions::Base::Decimal}) == "-99"_s);
    CHECK(to_string(10, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "a");
    CHECK(to_string(255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "ff");
    CHECK(to_string(0xfedcba, {.base = fmt::IntToStringOptions::Base::Hexadecimal, .capitalize = true}) ==
          "FEDCBA");
    CHECK(to_string(-255, {.base = fmt::IntToStringOptions::Base::Hexadecimal}) == "-ff");
    return k_success;
}

TEST_CASE(TestFormat) {
    auto& a = tester.scratch_arena;

    SUBCASE("basics") {
        DynamicArrayBounded<char, 256> buf;
        fmt::Assign(buf, "text {}, end", 100);
        CHECK_EQ(buf, "text 100, end"_s);
    }

    SUBCASE("basics") {
        CHECK_EQ(fmt::Format(a, "foo {} bar", 1), "foo 1 bar"_s);
        CHECK_EQ(fmt::Format(a, "{} {} {} {}", 1, 2, 3, 99999), "1 2 3 99999"_s);
        CHECK_EQ(fmt::Format(a, "{} :: {}", "key"_s, 100), "key :: 100"_s);
        CHECK_EQ(fmt::Format(a, "{}", "yeehar"), "yeehar"_s);
        CHECK_EQ(fmt::Format(a, "empty format"), "empty format"_s);
        CHECK_NEQ(fmt::Format(a, "ptr: {}", (void const*)""), ""_s);
    }

    SUBCASE("formats") {
        CHECK_NEQ(fmt::Format(a, "auto f32: {g}", 2.0), ""_s);
        CHECK_EQ(fmt::Format(a, "{x}", 255), "ff"_s);
        CHECK_EQ(fmt::Format(a, "{.2}", 0.2), "0.20"_s);
        CHECK_EQ(fmt::Format(a, "{.1}", 0.8187f), "0.8"_s);
    }

    SUBCASE("width") {
        SUBCASE("pad with spaces") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{1}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{2}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{3}", 10), " 10"_s);
            CHECK_EQ(fmt::Format(a, "{4}", 10), "  10"_s);
            CHECK_EQ(fmt::Format(a, "{4x}", 255), "  ff"_s);
        }

        SUBCASE("pad with zeros") {
            CHECK_EQ(fmt::Format(a, "{0}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{01}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{02}", 10), "10"_s);
            CHECK_EQ(fmt::Format(a, "{03}", 10), "010"_s);
            CHECK_EQ(fmt::Format(a, "{04}", 10), "0010"_s);
            CHECK_EQ(fmt::Format(a, "{04x}", 255), "00ff"_s);
            CHECK_EQ(fmt::Format(a, "{07.2}", 3.1111), "0003.11"_s);
        }
    }

    SUBCASE("errors") {
        CHECK_PANICS(fmt::Format(a, "{} {} {} {}", 1));
        CHECK_PANICS(fmt::Format(a, "{}", 1, 1, 1, 1));
        CHECK_PANICS(fmt::Format(a, "{sefsefsef}", 1));
        CHECK_PANICS(fmt::Format(a, "{{}", 1));
        CHECK_PANICS(fmt::Format(a, " {{} ", 1));
        CHECK_PANICS(fmt::Format(a, "{}}", 1));
        CHECK_PANICS(fmt::Format(a, " {}} ", 1));
    }

    SUBCASE("brace literals") {
        CHECK_EQ(fmt::Format(a, "{{}}"), "{}"_s);
        CHECK_EQ(fmt::Format(a, "{{}} {}", 10), "{} 10"_s);
        CHECK_EQ(fmt::Format(a, "{} {{}}", 10), "10 {}"_s);
        CHECK_EQ(fmt::Format(a, "{} {{fff}}", 10), "10 {fff}"_s);
    }

    SUBCASE("strings") {
        CHECK_EQ(fmt::Format(a, "{}", ""), ""_s);
        CHECK_EQ(fmt::Format(a, "{}", "string literal"), "string literal"_s);
        CHECK_EQ(fmt::Format(a, "{}", (char const*)"const char pointer"), "const char pointer"_s);
    }

    SUBCASE("Error") {
        ErrorCodeCategory const category {
            .category_id = "test",
            .message = [](Writer const& writer, ErrorCode error) -> ErrorCodeOr<void> {
                TRY(writer.WriteChars("error code: "));
                TRY(writer.WriteChars(
                    fmt::IntToString(error.code, {.base = fmt::IntToStringOptions::Base::Decimal})));
                return k_success;
            },
        };
        ErrorCode const err {category, 100};
        CHECK_NEQ(fmt::Format(a, "{}", err), ""_s);
        CHECK_NEQ(fmt::Format(a, "{u}", err), ""_s);
    }

    SUBCASE("Dump struct") {
        struct TestStruct {
            int a;
            int b;
            char const* c;
        };
        TestStruct const test {1, 2, "three"};
        tester.log.Debug("struct1 is: {}", fmt::DumpStruct(test));

        auto const arr = Array {
            TestStruct {1, 2, "three"},
            TestStruct {4, 5, "six"},
        };
        tester.log.Debug("struct2 is: {}", fmt::DumpStruct(arr));

        struct OtherStruct {
            int a;
            int b;
            char const* c;
            TestStruct d;
            TestStruct e;
        };
        OtherStruct const other {1, 2, "three", {4, 5, "six"}, {7, 8, "nine"}};
        tester.log.Debug("struct3 is: {}", fmt::DumpStruct(other));

        tester.log.Debug("struct4 is: {}", fmt::DumpStruct(tester));
    }

    SUBCASE("DateAndTime") {
        DateAndTime date {
            .year = 2021,
            .months_since_jan = 1,
            .day_of_month = 1,
            .hour = 12,
            .minute = 30,
            .second = 45,
            .millisecond = 123,
        };
        CHECK_EQ(fmt::Format(a, "{}", date), "2021-02-01 12:30:45.123"_s);
        CHECK_EQ(fmt::Format(a, "{t}", date), "2021-02-01T12:30:45.123Z"_s);
    }

    SUBCASE("Join") {
        CHECK_EQ(fmt::Join(a, {}, ""), ""_s);
        CHECK_EQ(fmt::Join(a, {}, ","), ""_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s}, ""), "a"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s}, ""), "ab"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s, "c"_s}, ""), "abc"_s);
        CHECK_EQ(fmt::Join(a, Array {"a"_s, "b"_s, "c"_s}, ","), "a,b,c"_s);

        CHECK_EQ(fmt::JoinInline<8>({}, ""), ""_s);
        CHECK_EQ(fmt::JoinInline<8>({}, ","), ""_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s}, ""), "a"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s}, ""), "ab"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s, "c"_s}, ""), "abc"_s);
        CHECK_EQ(fmt::JoinInline<8>(Array {"a"_s, "b"_s, "c"_s}, ","), "a,b,c"_s);
    }

    SUBCASE("PrettyFileSize") {
        CHECK_EQ(fmt::PrettyFileSize(0), "0 B"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024), "1 kB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1524), "1 kB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024 * 1024), "1 MB"_s);
        CHECK_EQ(fmt::PrettyFileSize(1024 * 1024 * 1024), "1.00 GB"_s);
        CHECK_EQ(fmt::PrettyFileSize((1024 * 1024 * 1024) + (1024 * 1024 * 100)), "1.10 GB"_s);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterFormatTests) {
    REGISTER_TEST(TestFormatStringReplace);
    REGISTER_TEST(TestIntToString);
    REGISTER_TEST(TestFormat);
}
