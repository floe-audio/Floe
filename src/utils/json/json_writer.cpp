// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "json_writer.hpp"

#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

TEST_CASE(TestJsonWriter) {
    using namespace json;

    json::WriteContext write_ctx {};
    DynamicArray<char> output {Malloc::Instance()};

    SUBCASE("basics") {
        write_ctx = {.out = dyn::WriterFor(output), .add_whitespace = true};

        {
            TRY(WriteObjectBegin(write_ctx));
            DEFER { REQUIRE_UNWRAP(WriteObjectEnd(write_ctx)); };

            u8 const v1 {};
            u16 const v2 {};
            u32 const v3 {};
            u64 const v4 {};
            s8 const v5 {};
            s16 const v6 {};
            s32 const v7 {};
            s64 const v8 {};
            f32 const v10 {};
            f64 const v11 {};
            bool const v12 {};

            TRY(WriteKeyValue(write_ctx, "smol", 1.0 / 7.0));
            TRY(WriteKeyValue(write_ctx, "big", Pow(k_pi<>, 25.0f)));

            TRY(WriteKeyValue(write_ctx, "v1", v1));
            TRY(WriteKeyValue(write_ctx, "v2", v2));
            TRY(WriteKeyValue(write_ctx, "v3", v3));
            TRY(WriteKeyValue(write_ctx, "v4", v4));
            TRY(WriteKeyValue(write_ctx, "v5", v5));
            TRY(WriteKeyValue(write_ctx, "v6", v6));
            TRY(WriteKeyValue(write_ctx, "v7", v7));
            TRY(WriteKeyValue(write_ctx, "v8", v8));
            TRY(WriteKeyValue(write_ctx, "v10", v10));
            TRY(WriteKeyValue(write_ctx, "v11", v11));
            TRY(WriteKeyValue(write_ctx, "v12", v12));
            TRY(WriteKeyNull(write_ctx, "null"));

            TRY(WriteKeyValue(write_ctx, "key", 100));
            TRY(WriteKeyValue(write_ctx, "key2", 0.4));
            TRY(WriteKeyValue(write_ctx, "key", "string"));

            DynamicArray<String> strs {Malloc::Instance()};
            dyn::Assign(strs, ArrayT<String>({"hey", "ho", "yo"}));
            TRY(WriteKeyValue(write_ctx, "string array", strs.Items()));

            {
                TRY(WriteKeyArrayBegin(write_ctx, "array"));
                DEFER { REQUIRE_UNWRAP(WriteArrayEnd(write_ctx)); };

                TRY(WriteValue(write_ctx, v1));
                TRY(WriteValue(write_ctx, v2));
                TRY(WriteValue(write_ctx, v3));
                TRY(WriteValue(write_ctx, v4));
                TRY(WriteValue(write_ctx, v5));
                TRY(WriteValue(write_ctx, v6));
                TRY(WriteValue(write_ctx, v7));
                TRY(WriteValue(write_ctx, v8));
                TRY(WriteValue(write_ctx, v10));
                TRY(WriteValue(write_ctx, v11));
                TRY(WriteValue(write_ctx, v12));
                TRY(WriteNull(write_ctx));

                TRY(WriteValue(write_ctx, "string"));

                TRY(WriteValue(write_ctx, strs.Items()));
            }
        }

        tester.log.Debug("{}", output);

        CHECK(Parse(output, [](EventHandlerStack&, Event const&) { return true; }, tester.scratch_arena, {})
                  .Succeeded());
    }

    SUBCASE("utf8") {
        write_ctx = {.out = dyn::WriterFor(output), .add_whitespace = false};
        TRY(WriteArrayBegin(write_ctx));
        TRY(WriteValue(write_ctx, "H:/Floe PresetsÉe"));
        TRY(WriteArrayEnd(write_ctx));

        tester.log.Debug("{}", output);
        CHECK_EQ(output.Items(), "[\"H:/Floe PresetsÉe\"]"_s);
    }
    return k_success;
}

TEST_REGISTRATION(RegisterJsonWriterTests) { REGISTER_TEST(TestJsonWriter); }
