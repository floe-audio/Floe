// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_reader.hpp"

#include "tests/framework.hpp"
#include "utils/leak_detecting_allocator.hpp"

TEST_CASE(TestJsonReader) {
    using namespace json;

    LeakDetectingAllocator const leak_detecting_a;
    ReaderSettings settings {};

    auto callback = [](EventHandlerStack&, Event const& event) {
        if constexpr (0) {
            switch (event.type) {
                case EventType::String:
                    tester.log.Debug("JSON event String: {} -> {}", event.key, event.string);
                    break;
                case EventType::Double:
                    tester.log.Debug("JSON event f64: {} -> {}", event.key, event.real);
                    break;
                case EventType::Int:
                    tester.log.Debug("JSON event Int: {} -> {}", event.key, event.integer);
                    break;
                case EventType::Bool:
                    tester.log.Debug("JSON event Bool: {} -> {}", event.key, event.boolean);
                    break;
                case EventType::Null: tester.log.Debug("JSON event Null: {}", event.key); break;
                case EventType::ObjectStart: tester.log.Debug("JSON event ObjectStart: {}", event.key); break;
                case EventType::ObjectEnd: tester.log.Debug("JSON event ObjectEnd"); break;
                case EventType::ArrayStart: tester.log.Debug("JSON event ArrayStart, {}", event.key); break;
                case EventType::ArrayEnd: tester.log.Debug("JSON event ArrayEnd"); break;
                case EventType::HandlingStarted: tester.log.Debug("Json event HandlingStarting"); break;
                case EventType::HandlingEnded: tester.log.Debug("Json event HandlingEnded"); break;
            }
        }
        return true;
    };

    SUBCASE("foo") {
        String const test = "{\"description\":\"Essential data for Floe\",\"name\":\"Core\",\"version\":1}";

        struct Data {
            u32 id_magic = {};
            Array<char, 64> name {};
            u32 version;
            u32 name_hash {0};
            String description {};
            String url {};
            String default_inst_path {};
            Version required_floe_version {};
            String file_extension {};
        };

        Data data;
        auto& a = tester.scratch_arena;
        auto parsed = Parse(
            test,
            [&data, &a](EventHandlerStack&, Event const& event) {
                if (SetIfMatching(event, "description", data.description, a)) return true;
                if (SetIfMatching(event, "url", data.url, a)) return true;
                if (SetIfMatching(event, "default_inst_relative_folder", data.default_inst_path, a))
                    return true;
                if (SetIfMatching(event, "file_extension", data.file_extension, a)) return true;
                if (SetIfMatching(event, "required_floe_version_major", data.required_floe_version.major))
                    return true;
                if (SetIfMatching(event, "required_floe_version_minor", data.required_floe_version.minor))
                    return true;
                if (SetIfMatching(event, "required_floe_version_patch", data.required_floe_version.patch))
                    return true;
                return false;
            },
            tester.scratch_arena,
            {});

        CHECK(!parsed.HasError());
    }

    SUBCASE("test1") {
        String const test = R"foo(
        {
            "name" : "Wraith",
            "param" : {
                "value" : 0.1,
                "hash" : 987234
            },
            "packs" : [
                {
                    "name" : "abc",
                    "hash" : 923847
                },
                {
                    "name" : "def",
                    "hash" : 58467
                }
            ],
            "numbers" : [ 0, 5, 6, 7, 8 ],
            "boolean" : false
        }
        )foo";

        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("test2") {
        // http://json.org/JSON_checker/
        String const test = R"foo(
        [
            "JSON Test Pattern pass1",
            {"object with 1 member":["array with 1 element"]},
            {},
            [],
            -42,
            true,
            false,
            null,
            {
                "integer": 1234567890,
                "real": -9876.543210,
                "e": 0.123456789e-12,
                "E": 1.234567890E+34,
                "":  23456789012E66,
                "zero": 0,
                "one": 1,
                "space": " ",
                "quote": "\"",
                "backslash": "\\",
                "controls": "\b\f\n\r\t",
                "slash": "/ & \/",
                "alpha": "abcdefghijklmnopqrstuvwyz",
                "ALPHA": "ABCDEFGHIJKLMNOPQRSTUVWYZ",
                "digit": "0123456789",
                "0123456789": "digit",
                "special": "`1~!@#$%^&*()_+-={':[,]}|;.</>?",
                "hex": "\u0123\u4567\u89AB\uCDEF\uabcd\uef4A",
                "true": true,
                "false": false,
                "null": null,
                "array":[  ],
                "object":{  },
                "address": "50 St. James Street",
                "url": "http://www.JSON.org/",
                "comment": "// /* <!-- --",
                "# -- --> */": " ",
                " s p a c e d " :[1,2 , 3

        ,

        4 , 5        ,          6           ,7        ],"compact":[1,2,3,4,5,6,7],
                "jsontext": "{\"object with 1 member\":[\"array with 1 element\"]}",
                "quotes": "&#34; \u0022 %22 0x22 034 &#x22;",
                "\/\\\"\uCAFE\uBABE\uAB98\uFCDE\ubcda\uef4A\b\f\n\r\t`1~!@#$%^&*()_+-=[]{}|;:',./<>?"
        : "A key can be any string"
            },
            0.5 ,98.6
        ,
        99.44
        ,

        1066,
        1e1,
        0.1e1,
        1e-1,
        1e00,2e+00,2e-00
        ,"rosebud"]

        )foo";

        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("nested test") {
        REQUIRE(Parse("[[[[[[[[[[[[[[[[[[[[[[[[[\"hello\"]]]]]]]]]]]]]]]]]]]]]]]]]",
                      callback,
                      tester.scratch_arena,
                      settings)
                    .Succeeded());
    }

    SUBCASE("should fail") {
        auto should_fail = [&](String test) {
            auto r = Parse(test, callback, tester.scratch_arena, settings);
            REQUIRE(r.HasError());
            tester.log.Debug("{}", r.Error().message);
        };

        should_fail("[\"mismatch\"}");
        should_fail("{\"nope\"}");
        should_fail("[0e]");
        should_fail("0.");
        should_fail("0.0e");
        should_fail("0.0e-");
        should_fail("0.0e+");
        should_fail("1e+");
        should_fail("{e}");
        should_fail("{1}");
        should_fail("[\"Colon instead of comma\": false]");
        should_fail("[0,]");
        should_fail("{\"key\":\"value\",}");
        should_fail("{no_quotes:\"str\"}");
    }

    SUBCASE("extra settings") {
        String const test = R"foo(
        {
            // "name" : "Wraith",
            /* "param" : {
                "value" : 0.1, 
                "hash" : 987234,
            }, */
            "packs" : [
                {
                    "name" : "abc",
                    "hash" : 923847
                },
                {
                    "name" : "def",
                    "hash" : 58467
                }
            ],
            "numbers" : [ 0, 5, 6, 7, 8, ],
            "boolean" : false,
            key_without_quotes : 10
        }
        )foo";
        settings.allow_comments = true;
        settings.allow_trailing_commas = true;
        settings.allow_keys_without_quotes = true;
        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("newlines") {
        String const test = "{\"foo\":\r\n\"val\"}";
        REQUIRE(Parse(test, callback, tester.scratch_arena, settings).Succeeded());
    }

    SUBCASE("escape codes") {
        String const test = R"foo({ 
            "item": "value  \u000f \uFFFF \n \r \t \\ \" \/"
        })foo";
        REQUIRE(Parse(
                    test,
                    [&](EventHandlerStack&, Event const& event) {
                        if (event.type == EventType::String)
                            REQUIRE(event.string == "value  \u000f \uFFFF \n \r \t \\ \" /"_s);
                        return true;
                    },
                    tester.scratch_arena,
                    settings)
                    .Succeeded());
    }
    return k_success;
}

TEST_REGISTRATION(RegisterJsonReaderTests) { REGISTER_TEST(TestJsonReader); }
