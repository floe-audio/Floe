// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

struct CommandLineArgDefinition {
    u32 id; // normally an enum, used for lookup
    String key; // long form, used as --key
    String description;
    String value_type; // for --help, e.g. path, time, num, depth
    bool required;
    int num_values; // 0 for no value, -1 for unlimited, else exact number
    char short_key {}; // optional single-character alias, used as -x. 0 means unset.
};

// Synthetic definition for the universally-supported --log-level flag (handled by ParseCommandLineArgs,
// not part of any tool's arg list). Listed in --help output via PrintUsage.
constexpr CommandLineArgDefinition k_log_level_arg_def {
    .id = 0,
    .key = "log-level"_s,
    .description = "Minimum log level to print: debug, info, warning, error"_s,
    .value_type = "level"_s,
    .required = false,
    .num_values = 1,
};

struct CommandLineArg {
    Optional<String> Value() const {
        ASSERT_EQ(info.num_values, 1);
        return was_provided && values.size ? Optional<String> {values[0]} : k_nullopt;
    }
    CommandLineArgDefinition const& info;
    Span<String> values; // empty if no values given
    bool was_provided;
};

// args straight from main()
struct ArgsCstr {
    int size;
    char const* const* args; // remember the first arg is the program name
};

struct PositionalArgsInfo {
    String name {}; // e.g. "preset-path"; presence enables positional parsing
    String description {}; // shown in the "Positional arguments:" help section
    int min_count {0};
    int max_count {-1}; // -1 for unlimited
    Span<String>* out {nullptr}; // where to write collected positionals
};

PUBLIC ErrorCodeOr<void> PrintUsage(Writer writer,
                                    String exe_name,
                                    String description,
                                    Span<CommandLineArgDefinition const> args,
                                    Span<CommandLineArgDefinition const> implicit_args = {},
                                    PositionalArgsInfo const& positionals = {}) {
    if (description.size) TRY(fmt::FormatToWriter(writer, "{}\n\n", description));

    TRY(fmt::FormatToWriter(writer, "Usage: {} [OPTIONS]", exe_name));
    if (positionals.name.size) {
        bool const optional = positionals.min_count == 0;
        bool const multi = positionals.max_count != 1;
        TRY(fmt::FormatToWriter(writer,
                                " {}<{}>{}{}",
                                optional ? "["_s : ""_s,
                                positionals.name,
                                multi ? "..."_s : ""_s,
                                optional ? "]"_s : ""_s));
    }
    TRY(fmt::FormatToWriter(writer, "\n\n"));

    static auto print_arg_key_val = [](Writer writer,
                                       CommandLineArgDefinition const& arg) -> ErrorCodeOr<void> {
        if (arg.short_key)
            TRY(fmt::FormatToWriter(writer, "  -{}, --{}", arg.short_key, arg.key));
        else
            TRY(fmt::FormatToWriter(writer, "      --{}", arg.key));

        switch (arg.num_values) {
            case 0: break;
            case 1: TRY(fmt::FormatToWriter(writer, " <{}>", arg.value_type)); break;
            case -1: TRY(fmt::FormatToWriter(writer, " <{}>...", arg.value_type)); break;
            default: {
                for (auto const _ : Range(arg.num_values))
                    TRY(fmt::FormatToWriter(writer, " <{}>", arg.value_type));
                break;
            }
        }
        return k_success;
    };

    usize max_key_val_size = 0;
    auto const measure_arg = [&](CommandLineArgDefinition const& arg) -> ErrorCodeOr<void> {
        usize key_val_size = 0;
        Writer key_val_size_writer {};
        key_val_size_writer.Set<usize>(key_val_size,
                                       [](usize& size, Span<u8 const> bytes) -> ErrorCodeOr<void> {
                                           size += bytes.size;
                                           return k_success;
                                       });
        TRY(print_arg_key_val(key_val_size_writer, arg));
        max_key_val_size = Max(max_key_val_size, key_val_size);
        return k_success;
    };
    for (auto const& arg : args)
        TRY(measure_arg(arg));
    for (auto const& arg : implicit_args)
        TRY(measure_arg(arg));

    static auto print_arg = [max_key_val_size](Writer writer,
                                               CommandLineArgDefinition const& arg) -> ErrorCodeOr<void> {
        usize key_val_size = 0;
        {
            Writer key_val_size_writer {};
            key_val_size_writer.Set<usize>(key_val_size,
                                           [](usize& size, Span<u8 const> bytes) -> ErrorCodeOr<void> {
                                               size += bytes.size;
                                               return k_success;
                                           });
            TRY(print_arg_key_val(key_val_size_writer, arg));
        }
        TRY(print_arg_key_val(writer, arg));
        TRY(writer.WriteCharRepeated(' ', max_key_val_size - key_val_size));
        TRY(fmt::FormatToWriter(writer, "  {}", arg.description));
        TRY(writer.WriteChar('\n'));
        return k_success;
    };

    if (positionals.name.size && positionals.description.size) {
        TRY(fmt::FormatToWriter(writer, "Positional arguments:\n"));
        TRY(fmt::FormatToWriter(writer, "  <{}>  {}\n", positionals.name, positionals.description));
    }

    if (FindIf(args, [](auto const& arg) { return arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Required arguments:\n"));
        for (auto const& arg : args)
            if (arg.required) TRY(print_arg(writer, arg));
    }

    if (implicit_args.size || FindIf(args, [](auto const& arg) { return !arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Optional arguments:\n"));
        for (auto const& arg : args)
            if (!arg.required) TRY(print_arg(writer, arg));
        for (auto const& arg : implicit_args)
            TRY(print_arg(writer, arg));
    }

    TRY(writer.WriteChar('\n'));

    return k_success;
}

PUBLIC Span<String> ArgsToStringsSpan(ArenaAllocator& arena, ArgsCstr args, bool include_program_name) {
    ASSERT(args.size > 0);
    auto const argv_start_index = (usize)(include_program_name ? 0 : 1);
    auto const result_size = (usize)args.size - argv_start_index;
    if (!result_size) return {};
    auto result = arena.AllocateExactSizeUninitialised<String>(result_size);
    for (auto const result_index : Range(result_size))
        result[result_index] = FromNullTerminated(args.args[result_index + argv_start_index]);
    return result;
}

namespace detail {

enum class ArgType : u8 { Short, Long, None };

PUBLIC ArgType ClassifyArg(String arg) {
    if (arg.size < 2) return ArgType::None;
    if (arg[0] == '-' && IsAlphanum(arg[1])) return ArgType::Short;
    if (arg.size > 2) {
        if (arg[0] == '-' && arg[1] == '-') return ArgType::Long;
        if (arg[0] == '-' && IsAlphanum(arg[1]) && arg[2] == '=') return ArgType::Short;
    }
    return ArgType::None;
}

PUBLIC usize ArgPrefixSize(ArgType type) {
    switch (type) {
        case ArgType::Short: return 1;
        case ArgType::Long: return 2;
        case ArgType::None: return 0;
    }
    return 0;
}

struct KeyVal {
    String key;
    String value;
};

PUBLIC KeyVal SplitKeyAndInlineValue(String arg) {
    if (auto const opt_index = Find(arg, '='))
        return KeyVal {arg.SubSpan(0, *opt_index), arg.SubSpan(*opt_index + 1)};
    return KeyVal {arg, ""_s};
}

} // namespace detail

// Supports things like:
// "-a", "-a=value", "--arg value", "--arg=value", "--arg value1 value2"
// Positional args (non-flag args appearing before any flag) are collected into positionals_out if non-null,
// and silently dropped otherwise. Positional args appearing after a flag are consumed as that flag's values,
// since flags greedily collect values until the next flag; callers wanting positionals must therefore place
// them before any flag on the command line. (This function is arity-unaware; ParseCommandLineArgs supports
// GNU-style mixing of positionals and flags.)
PUBLIC HashTable<String, Span<String>> ArgsToKeyValueTable(ArenaAllocator& arena,
                                                           Span<String const> args,
                                                           DynamicArray<String>* positionals_out = nullptr) {
    DynamicHashTable<String, Span<String>> result {arena};
    String current_key {};
    DynamicArray<String> current_values {arena};

    for (auto const arg_index : Range(args.size)) {
        if (auto const type = detail::ClassifyArg(args[arg_index]); type != detail::ArgType::None) {
            auto const arg = args[arg_index].SubSpan(detail::ArgPrefixSize(type));
            auto const [key, value] = detail::SplitKeyAndInlineValue(arg);

            if (key != current_key) {
                // it's a new key, flush the values of the previous
                if (current_key.size) result.Insert(current_key, current_values.ToOwnedSpan());
                current_key = key;
            }

            if (value.size) dyn::Append(current_values, value);
        } else {
            if (current_key.size)
                dyn::Append(current_values, args[arg_index]);
            else if (positionals_out)
                dyn::Append(*positionals_out, args[arg_index]);
        }
    }

    if (current_key.size) result.Insert(current_key, current_values.ToOwnedSpan());

    return result.ToOwnedTable();
}

PUBLIC HashTable<String, Span<String>>
ArgsToKeyValueTable(ArenaAllocator& arena, ArgsCstr args, DynamicArray<String>* positionals_out = nullptr) {
    return ArgsToKeyValueTable(arena, ArgsToStringsSpan(arena, args, false), positionals_out);
}

enum class CliError : u8 {
    InvalidArguments,
    HelpRequested,
    VersionRequested,
};
extern ErrorCodeCategory const g_cli_error_code_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(CliError) { return g_cli_error_code_category; }

struct ParseCommandLineArgsOptions {
    bool handle_help_option = true;
    bool print_usage_on_error = true;
    bool handle_log_level_option = true; // intercepts --log-level and calls SetLogLevel
    String description {};
    String version {}; // if present will be printed on --version
    // If .out is non-null, positional arguments are collected and validated against min/max_count.
    // The other fields drive --help output (Usage line and "Positional arguments:" section).
    // If .out is null, any positional argument is an error.
    PositionalArgsInfo positionals {};
};

// always returns a span the same size as the arg_defs, if an arg wasn't set it will have was_provided = false
PUBLIC ErrorCodeOr<Span<CommandLineArg>> ParseCommandLineArgs(Writer writer,
                                                              ArenaAllocator& arena,
                                                              String program_name,
                                                              Span<String const> args,
                                                              Span<CommandLineArgDefinition const> arg_defs,
                                                              ParseCommandLineArgsOptions options = {}) {
    auto const implicit_args = options.handle_log_level_option
                                   ? Span<CommandLineArgDefinition const> {&k_log_level_arg_def, 1}
                                   : Span<CommandLineArgDefinition const> {};

    auto error = [&](CliError e) -> ErrorCode {
        if (options.print_usage_on_error)
            TRY(PrintUsage(writer,
                           program_name,
                           options.description,
                           arg_defs,
                           implicit_args,
                           options.positionals));
        return ErrorCode {e};
    };

    auto result = arena.AllocateExactSizeUninitialised<CommandLineArg>(arg_defs.size);
    for (auto const arg_index : Range(arg_defs.size)) {
        PLACEMENT_NEW(&result[arg_index])
        CommandLineArg {
            .info = arg_defs[arg_index],
            .values = {},
            .was_provided = false,
        };
    }

    DynamicArray<String> positionals {arena};
    bool end_of_flags = false; // true after seeing "--"
    usize cursor = 0;
    while (cursor < args.size) {
        auto const cur = args[cursor];

        if (!end_of_flags && cur == "--"_s) {
            end_of_flags = true;
            ++cursor;
            continue;
        }

        auto const type = end_of_flags ? detail::ArgType::None : detail::ClassifyArg(cur);
        if (type == detail::ArgType::None) {
            dyn::Append(positionals, cur);
            ++cursor;
            continue;
        }

        auto const stripped = cur.SubSpan(detail::ArgPrefixSize(type));
        auto const [key, inline_value] = detail::SplitKeyAndInlineValue(stripped);
        ++cursor;

        if (options.handle_help_option && key == "help") {
            TRY(PrintUsage(writer,
                           program_name,
                           options.description,
                           arg_defs,
                           implicit_args,
                           options.positionals));
            return ErrorCode {CliError::HelpRequested};
        }

        if (options.version.size && key == "version") {
            TRY(fmt::FormatToWriter(writer, "Version {}\n", options.version));
            return ErrorCode {CliError::VersionRequested};
        }

        bool const is_log_level = options.handle_log_level_option && key == k_log_level_arg_def.key;

        Optional<usize> def_index;
        int expected_num_values = 0;
        if (is_log_level) {
            expected_num_values = k_log_level_arg_def.num_values;
        } else {
            def_index = FindIf(arg_defs, [&](auto const& arg) {
                if (arg.key == key) return true;
                if (arg.short_key && key.size == 1 && key[0] == arg.short_key) return true;
                return false;
            });
            if (!def_index) {
                TRY(fmt::FormatToWriter(writer, "Unknown option: {}\n", key));
                return error(CliError::InvalidArguments);
            }
            expected_num_values = arg_defs[*def_index].num_values;
        }

        // Collect values
        DynamicArray<String> values {arena};
        if (inline_value.size) dyn::Append(values, inline_value);

        if (expected_num_values == -1) {
            // greedy: consume non-flag args until next flag or "--"
            while (cursor < args.size) {
                if (args[cursor] == "--"_s) break;
                if (detail::ClassifyArg(args[cursor]) != detail::ArgType::None) break;
                dyn::Append(values, args[cursor]);
                ++cursor;
            }
        } else if (expected_num_values > 0) {
            // consume up to N non-flag args (subtracting any inline value already collected)
            auto needed = (usize)expected_num_values - Min((usize)expected_num_values, values.size);
            while (needed > 0 && cursor < args.size) {
                if (args[cursor] == "--"_s) break;
                if (detail::ClassifyArg(args[cursor]) != detail::ArgType::None) break;
                dyn::Append(values, args[cursor]);
                ++cursor;
                --needed;
            }
        }

        if (is_log_level) {
            if (!values.size) {
                TRY(fmt::FormatToWriter(writer, "Option --{} requires a value\n", key));
                return error(CliError::InvalidArguments);
            }
            auto const level = ParseLogLevelName(values[0]);
            if (!level) {
                TRY(fmt::FormatToWriter(writer, "Unknown log level: {}\n", values[0]));
                return error(CliError::InvalidArguments);
            }
            SetLogLevel(*level);
            continue;
        }

        auto const& arg = arg_defs[*def_index];
        if (arg.num_values != 0 && !values.size) {
            TRY(fmt::FormatToWriter(writer,
                                    "Option --{} requires {} value\n",
                                    key,
                                    arg.num_values == 1 ? "a"_s
                                                        : ((arg.num_values == -1)
                                                               ? "at least one"_s
                                                               : String(fmt::IntToString(arg.num_values)))));
            return error(CliError::InvalidArguments);
        }
        if (arg.num_values > 0 && (int)values.size != arg.num_values) {
            TRY(fmt::FormatToWriter(writer,
                                    "Option --{} requires {} value(s), got {}\n",
                                    key,
                                    arg.num_values,
                                    values.size));
            return error(CliError::InvalidArguments);
        }

        result[*def_index].values = values.ToOwnedSpan();
        result[*def_index].was_provided = true;
    }

    for (auto const arg_index : Range(arg_defs.size))
        if (arg_defs[arg_index].required && !result[arg_index].was_provided) {
            TRY(fmt::FormatToWriter(writer, "Required arg --{} not provided\n", arg_defs[arg_index].key));
            return error(CliError::InvalidArguments);
        }

    if (options.positionals.out) {
        if ((int)positionals.size < options.positionals.min_count) {
            TRY(fmt::FormatToWriter(writer,
                                    "Expected at least {} positional argument(s), got {}\n",
                                    options.positionals.min_count,
                                    positionals.size));
            return error(CliError::InvalidArguments);
        }
        if (options.positionals.max_count >= 0 && (int)positionals.size > options.positionals.max_count) {
            TRY(fmt::FormatToWriter(writer,
                                    "Expected at most {} positional argument(s), got {}\n",
                                    options.positionals.max_count,
                                    positionals.size));
            return error(CliError::InvalidArguments);
        }
        *options.positionals.out = positionals.ToOwnedSpan();
    } else if (positionals.size) {
        TRY(fmt::FormatToWriter(writer, "Unexpected positional argument: {}\n", positionals[0]));
        return error(CliError::InvalidArguments);
    }

    return result;
}

PUBLIC ErrorCodeOr<Span<CommandLineArg>> ParseCommandLineArgs(Writer writer,
                                                              ArenaAllocator& arena,
                                                              ArgsCstr args,
                                                              Span<CommandLineArgDefinition const> arg_defs,
                                                              ParseCommandLineArgsOptions options = {}) {
    return ParseCommandLineArgs(writer,
                                arena,
                                FromNullTerminated(args.args[0]),
                                ArgsToStringsSpan(arena, args, false),
                                arg_defs,
                                options);
}

PUBLIC ValueOrError<Span<CommandLineArg>, int>
ParseCommandLineArgsStandard(ArenaAllocator& arena,
                             ArgsCstr args,
                             Span<CommandLineArgDefinition const> arg_defs,
                             ParseCommandLineArgsOptions options = {
                                 .handle_help_option = true,
                                 .print_usage_on_error = true,
                             }) {
    auto writer = StdWriter(StdStream::Err);
    auto result = ParseCommandLineArgs(writer, arena, args, arg_defs, options);
    if (result.HasError()) {
        if (result.Error() == CliError::HelpRequested)
            return 0;
        else if (result.Error() == CliError::VersionRequested)
            return 0;
        return 1;
    }
    return result.Value();
}

// Subcommand support.
//
// Layered on top of ParseCommandLineArgs without modifying it. The first non-flag arg is treated
// as the verb; everything after it is forwarded to the per-verb arg defs unchanged. Each verb is
// parsed by an unmodified call into ParseCommandLineArgs, so per-verb `--help`, `--version`,
// `--log-level`, positionals, and validation all behave exactly as for a flat CLI.
//
// Help routing:
//   <exe>                 -> top-level help, returns InvalidArguments (missing command)
//   <exe> --help          -> top-level help, returns HelpRequested
//   <exe> <verb> --help   -> per-verb help, returns HelpRequested (handled by inner parser)
//   <exe> --version       -> prints version, returns VersionRequested
//
// --log-level is per-verb only (no pre-verb position is recognised).
struct CommandLineSubcommand {
    u32 id; // normally an enum, matches the verb's index
    String name; // verb token, e.g. "inspect"
    String description; // one-liner shown in top-level help; also used as the per-verb help description
    Span<CommandLineArgDefinition const> args; // built with MakeCommandLineArgDefs
    PositionalArgsInfo positionals {};
};

struct ParsedSubcommand {
    u32 id;
    Span<CommandLineArg> args;
};

struct ParseSubcommandsOptions {
    String description {}; // top-level (printed above the command list)
    String version {};
    bool handle_log_level_option = true;
    bool print_usage_on_error = true;
};

PUBLIC ErrorCodeOr<void> PrintSubcommandsUsage(Writer writer,
                                               String exe_name,
                                               String description,
                                               Span<CommandLineSubcommand const> subs) {
    if (description.size) TRY(fmt::FormatToWriter(writer, "{}\n\n", description));
    TRY(fmt::FormatToWriter(writer, "Usage: {} <command> [OPTIONS]\n\n", exe_name));
    TRY(fmt::FormatToWriter(writer, "Commands:\n"));

    usize max_name = 0;
    for (auto const& s : subs)
        max_name = Max(max_name, s.name.size);

    for (auto const& s : subs) {
        TRY(fmt::FormatToWriter(writer, "  {}", s.name));
        TRY(writer.WriteCharRepeated(' ', (max_name - s.name.size) + 2));
        TRY(fmt::FormatToWriter(writer, "{}\n", s.description));
    }
    TRY(fmt::FormatToWriter(writer,
                            "\nRun '{} <command> --help' for more information on a command.\n",
                            exe_name));
    return k_success;
}

PUBLIC ErrorCodeOr<ParsedSubcommand> ParseCommandLineSubcommands(Writer writer,
                                                                 ArenaAllocator& arena,
                                                                 ArgsCstr argv,
                                                                 Span<CommandLineSubcommand const> subs,
                                                                 ParseSubcommandsOptions options = {}) {
    ASSERT(argv.size > 0);
    auto const program_name = FromNullTerminated(argv.args[0]);

    auto print_top_help = [&]() {
        return PrintSubcommandsUsage(writer, program_name, options.description, subs);
    };

    if (argv.size < 2) {
        if (options.print_usage_on_error) {
            TRY(fmt::FormatToWriter(writer, "Error: a command is required\n\n"));
            TRY(print_top_help());
        }
        return ErrorCode {CliError::InvalidArguments};
    }

    auto const first = FromNullTerminated(argv.args[1]);

    if (first == "--help"_s || first == "-h"_s) {
        TRY(print_top_help());
        return ErrorCode {CliError::HelpRequested};
    }
    if (options.version.size && first == "--version"_s) {
        TRY(fmt::FormatToWriter(writer, "Version {}\n", options.version));
        return ErrorCode {CliError::VersionRequested};
    }

    auto const sub_index = FindIf(subs, [&](auto const& s) { return s.name == first; });
    if (!sub_index) {
        if (options.print_usage_on_error) {
            TRY(fmt::FormatToWriter(writer, "Error: unknown command '{}'\n\n", first));
            TRY(print_top_help());
        }
        return ErrorCode {CliError::InvalidArguments};
    }
    auto const& sub = subs[*sub_index];

    auto const sub_program_name = fmt::Join(arena, Array {program_name, " "_s, sub.name});
    auto const after_verb =
        ArgsToStringsSpan(arena, ArgsCstr {argv.size - 1, argv.args + 1}, /*include_program_name=*/false);

    auto result = TRY(ParseCommandLineArgs(writer,
                                           arena,
                                           sub_program_name,
                                           after_verb,
                                           sub.args,
                                           {
                                               .handle_help_option = true,
                                               .print_usage_on_error = options.print_usage_on_error,
                                               .handle_log_level_option = options.handle_log_level_option,
                                               .description = sub.description,
                                               .version = options.version,
                                               .positionals = sub.positionals,
                                           }));

    return ParsedSubcommand {.id = sub.id, .args = result};
}

PUBLIC ValueOrError<ParsedSubcommand, int>
ParseCommandLineSubcommandsStandard(ArenaAllocator& arena,
                                    ArgsCstr argv,
                                    Span<CommandLineSubcommand const> subs,
                                    ParseSubcommandsOptions options = {}) {
    auto writer = StdWriter(StdStream::Err);
    auto result = ParseCommandLineSubcommands(writer, arena, argv, subs, options);
    if (result.HasError()) {
        if (result.Error() == CliError::HelpRequested) return 0;
        if (result.Error() == CliError::VersionRequested) return 0;
        return 1;
    }
    return result.Value();
}

// Compile-time helper that ensures command line arg definitions exactly match an enum. Allowing for easy
// lookup.
template <EnumWithCount EnumType, usize N>
consteval Array<CommandLineArgDefinition, N> MakeCommandLineArgDefs(CommandLineArgDefinition (&&a)[N]) {
    auto args = ArrayT(a);

    if (args.size != ToInt(EnumType::Count))
        throw "MakeCommandLineArgDefs: size of array doesn't match enum count";

    for (auto const [arg_index, arg] : Enumerate(args)) {
        if (arg.id != (u32)arg_index) throw "MakeCommandLineArgDefs: id is out of order with enum value";
        if (!arg.key.size) throw "MakeCommandLineArgDefs: key is empty";
        if (!arg.description.size) throw "MakeCommandLineArgDefs: description is empty";
        if (arg.num_values != 0 && !arg.value_type.size) throw "MakeCommandLineArgDefs: value_type is empty";

        for (auto const& other_arg : args) {
            if (&arg == &other_arg) continue;
            if (arg.key == other_arg.key) throw "MakeCommandLineArgDefs: duplicate key";
            if (arg.short_key && arg.short_key == other_arg.short_key)
                throw "MakeCommandLineArgDefs: duplicate short_key";
        }
    }

    return args;
}

template <EnumWithCount EnumType, usize N>
consteval Array<CommandLineSubcommand, N> MakeSubcommands(CommandLineSubcommand (&&a)[N]) {
    auto subs = ArrayT(a);

    if (subs.size != ToInt(EnumType::Count)) throw "MakeSubcommands: size of array doesn't match enum count";

    for (auto const [sub_index, sub] : Enumerate(subs)) {
        if (sub.id != (u32)sub_index) throw "MakeSubcommands: id is out of order with enum value";
        if (!sub.name.size) throw "MakeSubcommands: name is empty";
        if (!sub.description.size) throw "MakeSubcommands: description is empty";
        if (sub.name == "--help"_s || sub.name == "-h"_s || sub.name == "--version"_s)
            throw "MakeSubcommands: name collides with a reserved option";

        for (auto const& other : subs) {
            if (&sub == &other) continue;
            if (sub.name == other.name) throw "MakeSubcommands: duplicate name";
        }
    }

    return subs;
}

// NOTE: not necessary if you created args with MakeCommandLineArgDefs - you can just use array indexing
PUBLIC constexpr CommandLineArg const* LookupArg(Span<CommandLineArg const> args, auto id) {
    auto index = FindIf(args, [&](auto const& arg) { return arg.info.id == (u32)id; });
    if (index) return &args[*index];
    return nullptr;
}
