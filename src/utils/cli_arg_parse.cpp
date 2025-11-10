// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_arg_parse.hpp"

#include "tests/framework.hpp"

ErrorCodeCategory const g_cli_error_code_category = {
    .category_id = "CL",
    .message =
        [](Writer const& writer, ErrorCode e) {
            return writer.WriteChars(({
                String s {};
                switch ((CliError)e.code) {
                    case CliError::InvalidArguments: s = "invalid arguments"; break;
                    case CliError::HelpRequested: s = "help requested"; break;
                    case CliError::VersionRequested: s = "version requested"; break;
                }
                s;
            }));
        },
};

TEST_CASE(TestParseCommandLineArgs) {
    auto& a = tester.scratch_arena;

    SUBCASE("args to strings span") {
        char const* argv[] = {"program-name", "arg1", "arg2"};
        auto const argc = (int)ArraySize(argv);
        {
            auto args = ArgsToStringsSpan(a, {argc, argv}, false);
            CHECK(args.size == 2);
            CHECK_EQ(args[0], "arg1"_s);
            CHECK_EQ(args[1], "arg2"_s);
        }
        {
            auto args = ArgsToStringsSpan(a, {argc, argv}, true);
            CHECK(args.size == 3);
            CHECK_EQ(args[0], "program-name"_s);
            CHECK_EQ(args[1], "arg1"_s);
            CHECK_EQ(args[2], "arg2"_s);
        }
    }

    auto const check_arg = [&](HashTable<String, Span<String>> table, String arg, Span<String const> values) {
        CAPTURE(arg);
        CAPTURE(values);
        tester.log.Debug("Checking arg: {}, values: {}", arg, values);
        auto f = table.Find(arg);
        CHECK(f != nullptr);
        if (f) CHECK_EQ(*f, values);
    };

    SUBCASE("mutliple short and long args") {
        auto args = ArgsToKeyValueTable(a, Array {"-a"_s, "b", "--c", "d", "e", "-f", "--key=value"});
        CHECK_EQ(args.size, 4uz);
        check_arg(args, "a"_s, Array {"b"_s});
        check_arg(args, "c"_s, Array {"d"_s, "e"});
        check_arg(args, "f"_s, {});
        check_arg(args, "key"_s, Array {"value"_s});
    }

    SUBCASE("no args") {
        auto args = ArgsToKeyValueTable(a, Span<String> {});
        CHECK_EQ(args.size, 0uz);
    }

    SUBCASE("arg without value") {
        auto args = ArgsToKeyValueTable(a, Array {"--filter"_s});
        CHECK_EQ(args.size, 1uz);
        CHECK(args.Find("filter"_s));
    }

    SUBCASE("positional args are ignored") {
        auto args = ArgsToKeyValueTable(a, Array {"filter"_s});
        CHECK_EQ(args.size, 0uz);
    }

    SUBCASE("short arg with value") {
        auto args = ArgsToKeyValueTable(a, Array {"-a=b"_s});
        check_arg(args, "a"_s, Array {"b"_s});
        (void)args;
    }

    SUBCASE("long arg with value") {
        auto args = ArgsToKeyValueTable(a, Array {"--a=b"_s});
        check_arg(args, "a"_s, Array {"b"_s});
    }

    SUBCASE("parsing") {
        enum class ArgId {
            A,
            B,
            C,
            D,
            E,
            Count,
        };
        constexpr auto k_arg_defs = MakeCommandLineArgDefs<ArgId>({
            {
                .id = (u32)ArgId::A,
                .key = "a-arg",
                .description = "desc",
                .value_type = "type",
                .required = true,
                .num_values = 1,
            },
            {
                .id = (u32)ArgId::B,
                .key = "b-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 0,
            },
            {
                .id = (u32)ArgId::C,
                .key = "c-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 0,
            },
            {
                .id = (u32)ArgId::D,
                .key = "d-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = 2,
            },
            {
                .id = (u32)ArgId::E,
                .key = "e-arg",
                .description = "desc",
                .value_type = "type",
                .required = false,
                .num_values = -1,
            },

        });

        DynamicArray<char> buffer {a};
        auto writer = dyn::WriterFor(buffer);

        SUBCASE("valid args") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg"_s, "value", "--c-arg"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            CHECK(args.size == k_arg_defs.size);

            auto a_arg = args[ToInt(ArgId::A)];
            CHECK(a_arg.values == Array {"value"_s});
            CHECK(a_arg.was_provided);
            CHECK(a_arg.info.id == (u32)ArgId::A);

            auto b_arg = args[ToInt(ArgId::B)];
            CHECK(!b_arg.was_provided);

            auto c_arg = args[ToInt(ArgId::C)];
            CHECK(c_arg.was_provided);
            CHECK(c_arg.values.size == 0);
        }

        SUBCASE("missing required args") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--b-arg"_s, "value"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            REQUIRE(o.HasError());
            CHECK(buffer.size > 0);
        }

        SUBCASE("help is handled when requested") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--help"_s},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = true,
                                                    .print_usage_on_error = false,
                                                });
            REQUIRE(o.HasError());
            CHECK(o.Error() == CliError::HelpRequested);
            CHECK(buffer.size > 0);
        }

        SUBCASE("version is handled when requested") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--version"_s},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = true,
                                                    .print_usage_on_error = false,
                                                    .version = "1.0.0"_s,
                                                });
            REQUIRE(o.HasError());
            CHECK(o.Error() == CliError::VersionRequested);
            CHECK(buffer.size > 0);
        }

        SUBCASE("arg that requires exactly 2 values") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg=1"_s, "--d-arg", "1", "2"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            auto d_arg = args[ToInt(ArgId::D)];
            CHECK(d_arg.was_provided);
            CHECK(d_arg.values == Array {"1"_s, "2"_s});
        }

        SUBCASE("arg that can receive any number of arguments") {
            auto const o = ParseCommandLineArgs(writer,
                                                a,
                                                "my-program",
                                                Array {"--a-arg=1"_s, "--e-arg", "1", "2", "3", "4"},
                                                k_arg_defs,
                                                {
                                                    .handle_help_option = false,
                                                    .print_usage_on_error = false,
                                                });
            auto const args = REQUIRE_UNWRAP(o);
            auto e_arg = args[ToInt(ArgId::E)];
            CHECK(e_arg.was_provided);
            CHECK(e_arg.values == Array {"1"_s, "2"_s, "3"_s, "4"_s});
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterCliArgParseTests) { REGISTER_TEST(TestParseCommandLineArgs); }
