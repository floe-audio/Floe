// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "string.hpp"

#include <math.h> // HUGE_VAL
#include <stdlib.h> // strtod

#include "tests/framework.hpp"

Optional<double> ParseFloat(String str, usize* num_chars_read) {
    char buffer[32];
    CopyStringIntoBufferWithNullTerm(buffer, str);
    char* str_end {};
    auto result = strtod(buffer, &str_end);
    if (result == HUGE_VAL) return k_nullopt;
    usize const chars_read =
        (str_end >= buffer && str_end <= (buffer + sizeof(buffer))) ? (usize)(str_end - buffer) : 0;
    if (chars_read == 0) return k_nullopt;
    if (num_chars_read) *num_chars_read = chars_read;
    return result;
}

TEST_CASE(TestAsciiToUppercase) {
    CHECK(ToUppercaseAscii('a') == 'A');
    CHECK(ToUppercaseAscii('z') == 'Z');
    CHECK(ToUppercaseAscii('A') == 'A');
    CHECK(ToUppercaseAscii('M') == 'M');
    CHECK(ToUppercaseAscii('0') == '0');
    CHECK(ToUppercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToUppercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestAsciiToLowercase) {
    CHECK(ToLowercaseAscii('A') == 'a');
    CHECK(ToLowercaseAscii('Z') == 'z');
    CHECK(ToLowercaseAscii('a') == 'a');
    CHECK(ToLowercaseAscii('m') == 'm');
    CHECK(ToLowercaseAscii('0') == '0');
    CHECK(ToLowercaseAscii(' ') == ' ');
    for (int i = SmallestRepresentableValue<char>(); i <= LargestRepresentableValue<char>(); ++i)
        ToLowercaseAscii((char)i);
    return k_success;
}

TEST_CASE(TestNullTermStringsEqual) {
    CHECK(NullTermStringsEqual("", ""));
    CHECK(!NullTermStringsEqual("a", ""));
    CHECK(!NullTermStringsEqual("", "a"));
    CHECK(!NullTermStringsEqual("aaa", "a"));
    CHECK(!NullTermStringsEqual("a", "aaa"));
    CHECK(NullTermStringsEqual("aaa", "aaa"));
    return k_success;
}

TEST_CASE(TestSplitWithIterator) {
    auto check = [&](String whole, char token, Span<String> expected_parts, bool skip_consecutive) {
        CAPTURE(whole);
        CAPTURE(expected_parts);
        CAPTURE(skip_consecutive);

        {
            usize cursor {0uz};
            usize index = 0;
            while (auto part = SplitWithIterator(whole, cursor, token, skip_consecutive))
                CHECK_EQ(*part, expected_parts[index++]);
            CHECK_EQ(index, expected_parts.size);
        }

        {
            usize index = 0;
            for (auto const part :
                 SplitIterator {.whole = whole, .token = token, .skip_consecutive = skip_consecutive}) {
                CHECK_EQ(part, expected_parts[index++]);
            }
            CHECK_EQ(index, expected_parts.size);
        }
    };

    check("aa\nbb", '\n', Array {"aa"_s, "bb"}, false);
    check("aa", '\n', Array {"aa"_s}, false);
    check("aa\n\nbb", '\n', Array {"aa"_s, "", "bb"}, false);
    check("\n\nbb", '\n', Array {""_s, "", "bb"}, false);
    check("aa\n\n", '\n', Array {"aa"_s, ""}, false);
    check("\n\n", '\n', Array {""_s, ""}, false);

    check("aa\nbb", '\n', Array {"aa"_s, "bb"}, true);
    check("aa", '\n', Array {"aa"_s}, true);
    check("aa\n\nbb", '\n', Array {"aa"_s, "bb"}, true);
    check("\n\nbb", '\n', Array {"bb"_s}, true);
    check("aa\n\n", '\n', Array {"aa"_s}, true);
    check("\n\n", '\n', {}, true);

    return k_success;
}

TEST_CASE(TestSplit) {
    auto check = [&](String whole, char token, Span<String> expected_parts) {
        CAPTURE(whole);
        CAPTURE(expected_parts);

        auto split = Split(whole, token, tester.scratch_arena);
        REQUIRE(split.size == expected_parts.size);
        for (auto const i : Range(expected_parts.size))
            CHECK(split[i] == expected_parts[i]);
    };
    check("aa\nbb", '\n', Array<String, 2> {"aa", "bb"});
    check("aa", '\n', Array<String, 1> {"aa"});
    return k_success;
}

TEST_CASE(TestParseFloat) {
    CHECK(!ParseFloat(""));
    CHECK(!ParseFloat("string"));

    usize num_chars_read = 0;
    CHECK_APPROX_EQ(ParseFloat("0", &num_chars_read).Value(), 0.0, 0.0001);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_APPROX_EQ(ParseFloat("10", &num_chars_read).Value(), 10.0, 0.0001);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_APPROX_EQ(ParseFloat("-10", &num_chars_read).Value(), -10.0, 0.0001);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_APPROX_EQ(ParseFloat("238942349.230", &num_chars_read).Value(), 238942349.230, 0.0001);
    CHECK_EQ(num_chars_read, 13u);
    return k_success;
}

TEST_CASE(TestParseInt) {
    CHECK(!ParseInt("", ParseIntBase::Decimal));
    CHECK(!ParseInt("string", ParseIntBase::Decimal));
    CHECK(!ParseInt("  ", ParseIntBase::Decimal));

    usize num_chars_read = 0;
    CHECK_EQ(ParseInt("0", ParseIntBase::Decimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Decimal, &num_chars_read).Value(), 10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("-10", ParseIntBase::Decimal, &num_chars_read).Value(), -10);
    CHECK_EQ(num_chars_read, 3u);
    CHECK_EQ(ParseInt("238942349", ParseIntBase::Decimal, &num_chars_read).Value(), 238942349);
    CHECK_EQ(num_chars_read, 9u);

    CHECK_EQ(ParseInt("0", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0);
    CHECK_EQ(num_chars_read, 1u);
    CHECK_EQ(ParseInt("10", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0x10);
    CHECK_EQ(num_chars_read, 2u);
    CHECK_EQ(ParseInt("deadc0de", ParseIntBase::Hexadecimal, &num_chars_read).Value(), 0xdeadc0de);
    CHECK_EQ(num_chars_read, 8u);

    return k_success;
}

TEST_CASE(TestCopyStringIntoBuffer) {
    SUBCASE("char[N] overload") {
        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Size 1 buffer") {
            char buf[1];
            CopyStringIntoBufferWithNullTerm(buf, "abc");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Empty source") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "");
            CHECK(buf[0] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }

    SUBCASE("Span<char> overload") {
        SUBCASE("Dest empty") { CopyStringIntoBufferWithNullTerm(nullptr, 0, "abc"); }

        SUBCASE("Source empty") {
            char buffer[6];
            CopyStringIntoBufferWithNullTerm(buffer, 6, "");
            CHECK(buffer[0] == 0);
        }

        SUBCASE("Small buffer") {
            char buf[2];
            CopyStringIntoBufferWithNullTerm(buf, 2, "abc");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == '\0');
        }

        SUBCASE("Whole source fits") {
            char buf[8];
            CopyStringIntoBufferWithNullTerm(buf, "aa");
            CHECK(buf[0] == 'a');
            CHECK(buf[1] == 'a');
            CHECK(buf[2] == '\0');
        }
    }
    return k_success;
}

TEST_CASE(TestMatchWildcard) {
    CHECK(MatchWildcard("*foo*", "foobar"));
    CHECK(MatchWildcard(".*-file", ".text-file"));
    CHECK(MatchWildcard("floe_*.cpp", "floe_functions.cpp"));
    CHECK(MatchWildcard("mirtestãingããage_*.cpp", "mirtestãingããage_functions.cpp"));
    CHECK(MatchWildcard("*.floe*", "1.floe"));
    CHECK(MatchWildcard("*.floe*", "1.floe-wraith"));
    CHECK(MatchWildcard("*.floe*", "1.floe-none"));
    CHECK(!MatchWildcard("*.floe*", "foo.py"));
    return k_success;
}

TEST_CASE(TestStringAlgorithms) {
    SUBCASE("ContainsCaseInsensitiveAscii") {
        String const str = "abcde";
        CHECK(ContainsCaseInsensitiveAscii(str, "abcde"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "abcd"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "abcdef"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "bc"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "BC"_s));
        CHECK(!ContainsCaseInsensitiveAscii(str, "cb"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "c"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, "C"_s));
        CHECK(ContainsCaseInsensitiveAscii(str, ""_s));
    }

    SUBCASE("Compare") {
        CHECK(CompareAscii("aaa"_s, "aaa"_s) == 0);
        CHECK_OP(CompareAscii("aaa"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii("za"_s, "AAA"_s), >, 0);
        CHECK_OP(CompareAscii(""_s, ""_s), ==, 0);
        CHECK_OP(CompareAscii("a"_s, ""_s), >, 0);
        CHECK_OP(CompareAscii(""_s, "a"_s), <, 0);

        CHECK(CompareCaseInsensitiveAscii("Aaa"_s, "aaa"_s) == 0);
        CHECK(CompareCaseInsensitiveAscii(""_s, ""_s) == 0);
    }

    SUBCASE("IsEqualToCaseInsensitveAscii") {
        CHECK(IsEqualToCaseInsensitiveAscii("aa"_s, "AA"_s));
        CHECK(IsEqualToCaseInsensitiveAscii(""_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aa"_s, "AAA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("aaa"_s, "AA"_s));
        CHECK(!IsEqualToCaseInsensitiveAscii("a"_s, ""_s));
        CHECK(!IsEqualToCaseInsensitiveAscii(""_s, "1"_s));
    }

    SUBCASE("whitespace") {
        CHECK(CountWhitespaceAtStart("  a"_s) == 2);
        CHECK(CountWhitespaceAtStart("\t\n\r a"_s) == 4);
        CHECK(CountWhitespaceAtStart(" "_s) == 1);
        CHECK(CountWhitespaceAtStart("a "_s) == 0);
        CHECK(CountWhitespaceAtStart(""_s) == 0);

        CHECK(CountWhitespaceAtEnd("a  "_s) == 2);
        CHECK(CountWhitespaceAtEnd("a \t\n\r"_s) == 4);
        CHECK(CountWhitespaceAtEnd(" "_s) == 1);
        CHECK(CountWhitespaceAtEnd(" a"_s) == 0);
        CHECK(CountWhitespaceAtEnd(""_s) == 0);

        CHECK(WhitespaceStripped(" aa  "_s) == "aa");
        CHECK(WhitespaceStrippedStart(" aa  "_s) == "aa  ");
    }

    SUBCASE("FindUtf8TruncationPoint") {
        auto check = [&](String str, usize max_len, usize expected) {
            CAPTURE(str);
            CAPTURE(max_len);
            CAPTURE(expected);
            auto result = FindUtf8TruncationPoint(str, max_len);
            CHECK_EQ(result, expected);
            CHECK(IsValidUtf8(str.SubSpan(0, result)));
        };

        SUBCASE("ascii") {
            auto const str = "Hello World"_s;
            check(str, 5, 5);
            check(str, 10, 10);
        }

        SUBCASE("2-byte UTF-8 character") {
            auto const str = "café"_s;
            check(str, 4, 3);
            check(str, 3, 3);
        }

        SUBCASE("3-byte UTF-8 character") {
            // 0xE2 0x82 0xAC
            auto const str = "Cost: €"_s;

            check(str, 8, 6);
            check(str, 7, 6);
            check(str, 6, 6);
            check(str, 5, 5);
        }

        SUBCASE("4-byte UTF-8 character") {
            // "𐍈" (Gothic letter aiha) is 0xF0 0x90 0x8D 0x88 in UTF-8
            auto const str = "Symbol: \xF0\x90\x8D\x88"_s;

            check(str, 11, 8);
            check(str, 10, 8);
            check(str, 9, 8);
            check(str, 8, 8);
        }

        SUBCASE("Edge cases") {
            auto const str = "€"_s;
            CHECK_EQ(FindUtf8TruncationPoint(str, 1), 0u);
            CHECK_EQ(FindUtf8TruncationPoint(str, 2), 0u);
        }
    }

    return k_success;
}

TEST_CASE(TestNarrowWiden) {
    auto& a = tester.scratch_arena;
    // IMPROVE: check against Windows MultiByteToWideChar
    auto const utf8_str = FromNullTerminated((char const*)u8"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt");
    auto const wstr = L"C:/testãingãã/†‡œÀÏàåùçÁéÄöüÜß.txt"_s;

    SUBCASE("standard functions") {
        auto const converted_wstr = Widen(a, utf8_str);
        CHECK(converted_wstr.HasValue());
        CHECK(converted_wstr.Value() == wstr);
        auto const original_str = Narrow(a, converted_wstr.Value());
        CHECK(original_str.HasValue());
        CHECK(original_str.Value() == utf8_str);
    }

    SUBCASE("widen append") {
        DynamicArray<wchar_t> str {a};
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size);
        CHECK(str == wstr);
        CHECK(WidenAppend(str, utf8_str));
        CHECK(str.size == wstr.size * 2);
    }

    SUBCASE("narrow append") {
        DynamicArray<char> str {a};
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size);
        CHECK(str == utf8_str);
        CHECK(NarrowAppend(str, wstr));
        CHECK(str.size == utf8_str.size * 2);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterStringTests) {
    REGISTER_TEST(TestAsciiToUppercase);
    REGISTER_TEST(TestCopyStringIntoBuffer);
    REGISTER_TEST(TestMatchWildcard);
    REGISTER_TEST(TestStringAlgorithms);
    REGISTER_TEST(TestAsciiToLowercase);
    REGISTER_TEST(TestNullTermStringsEqual);
    REGISTER_TEST(TestSplitWithIterator);
    REGISTER_TEST(TestSplit);
    REGISTER_TEST(TestParseFloat);
    REGISTER_TEST(TestParseInt);
    REGISTER_TEST(TestNarrowWiden);
}
