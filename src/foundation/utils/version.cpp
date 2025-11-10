// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestVersion) {
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {1, 0, 0}) == "1.0.0"_s);
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {10, 99, 98}) == "10.99.98"_s);

    CHECK(Version {1, 0, 0} == Version {1, 0, 0});
    CHECK(Version {1, 1, 0} > Version {1, 0, 0});
    CHECK(Version {1, 0, 0} < Version {1, 1, 0});
    CHECK(Version {0, 0, 0} < Version {1, 0, 0});
    CHECK(Version {1, 0, 100} < Version {2, 4, 10});
    CHECK(Version {0, 0, 100} < Version {0, 0, 101});

    auto const check_string_parsing = [&](String str, Version ver) {
        CAPTURE(str);
        auto const parsed_ver = ParseVersionString(str);
        REQUIRE(parsed_ver.HasValue());
        CHECK(ver == *parsed_ver);
    };

    CHECK(!ParseVersionString("1"));
    CHECK(!ParseVersionString("1.2"));
    CHECK(!ParseVersionString("hello"));
    CHECK(!ParseVersionString(",,what"));
    CHECK(!ParseVersionString("1,1,2"));
    CHECK(!ParseVersionString("1a,1,2bv"));
    CHECK(!ParseVersionString("200a.200.400a"));
    CHECK(!ParseVersionString("."));
    CHECK(!ParseVersionString(".."));
    CHECK(!ParseVersionString("..."));
    CHECK(!ParseVersionString("...."));
    CHECK(!ParseVersionString(".1.2"));
    CHECK(!ParseVersionString("12.."));
    CHECK(!ParseVersionString(".1."));
    CHECK(!ParseVersionString(""));
    CHECK(!ParseVersionString(" 200   .  4.99 "));

    check_string_parsing("1.1.1", {1, 1, 1});
    check_string_parsing("0.0.0", {0, 0, 0});
    check_string_parsing("1.0.99", {1, 0, 99});
    check_string_parsing("1.0.0-alpha.1", {1, 0, 0});
    check_string_parsing("1.0.0-alpha+abcdef", {1, 0, 0});
    check_string_parsing("1.0.0-alpha+2.2.0", {1, 0, 0});

    // Unsupported pre-releases are ignored.
    check_string_parsing("1.0.0-beta", {1, 0, 0});
    check_string_parsing("1.2.3.alpha.4", {1, 2, 3});
    check_string_parsing("1.2.3.rc.5", {1, 2, 3});

    // Valid betas are parsed.
    check_string_parsing("1.0.0-beta.1", {1, 0, 0, 1});
    check_string_parsing("2.5.10-beta.255", {2, 5, 10, 255});
    check_string_parsing("0.1.0-beta.0", {0, 1, 0, 0});
    check_string_parsing("0.1.0-beta.0+e39ef3c", {0, 1, 0, 0}); // Build metadata ignored.
    check_string_parsing("0.1.0-beta.0+", {0, 1, 0, 0}); // Build metadata ignored.

    {
        u32 prev_version = 0;
        u16 maj = 0;
        u8 min = 0;
        u8 pat = 0;
        for (auto _ : Range(256)) {
            ++pat;
            if (pat > 20) {
                pat = 0;
                ++min;
                if (min > 20) ++maj;
            }

            auto const version = PackVersionIntoU32(maj, min, pat);
            CHECK(version > prev_version);
            prev_version = version;
        }
    }

    CHECK(PackVersionIntoU32(1, 1, 2) < PackVersionIntoU32(1, 2, 0));

    // Test that invalid beta patterns are ignored (not treated as errors)
    check_string_parsing("1.0.0-beta.", {1, 0, 0});
    check_string_parsing("1.0.0-beta.a", {1, 0, 0});
    check_string_parsing("1.0.0-beta.256", {1, 0, 0});
    check_string_parsing("1.0.0-beta.1.2", {1, 0, 0});
    check_string_parsing("1.0.0-beta.1.2+e39ef3c", {1, 0, 0});

    // Test beta version formatting
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {1, 0, 0, 1}) == "1.0.0-beta.1"_s);
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {2, 5, 10, 255}) == "2.5.10-beta.255"_s);
    CHECK(fmt::Format(tester.scratch_arena, "{}", Version {1, 0, 0}) == "1.0.0"_s);

    // Test beta version comparisons - beta < release
    CHECK(Version {1, 0, 0, 1} < Version {1, 0, 0});
    CHECK(Version {1, 0, 0, 255} < Version {1, 0, 0});
    CHECK(!(Version {1, 0, 0} < Version {1, 0, 0, 1}));

    // Test beta version comparisons - beta ordering
    CHECK(Version {1, 0, 0, 1} < Version {1, 0, 0, 2});
    CHECK(Version {1, 0, 0, 0} < Version {1, 0, 0, 1});
    CHECK(Version {1, 0, 0, 254} < Version {1, 0, 0, 255});

    // Test equality with beta versions
    CHECK(Version {1, 0, 0, 1} == Version {1, 0, 0, 1});
    CHECK(Version {1, 0, 0} == Version {1, 0, 0});
    CHECK(!(Version {1, 0, 0, 1} == Version {1, 0, 0}));
    CHECK(!(Version {1, 0, 0} == Version {1, 0, 0, 1}));
    CHECK(!(Version {1, 0, 0, 1} == Version {1, 0, 0, 2}));

    // Test complex version ordering with beta
    CHECK(Version {1, 0, 0, 1} < Version {1, 0, 1});
    CHECK(Version {1, 0, 0, 255} < Version {1, 0, 1, 0});
    CHECK(Version {1, 0, 0, 1} < Version {1, 1, 0, 0});
    CHECK(Version {0, 9, 9, 255} < Version {1, 0, 0, 0});

    return k_success;
}

TEST_REGISTRATION(RegisterVersionTests) { REGISTER_TEST(TestVersion); }
