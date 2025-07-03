// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

struct CommandLineArgDefinition {
    u32 id; // normally an enum, used for lookup
    String key;
    String description;
    String value_type; // for --help, e.g. path, time, num, depth
    bool required;
    int num_values; // 0 for no value, -1 for unlimited, else exact number
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

PUBLIC ErrorCodeOr<void>
PrintUsage(Writer writer, String exe_name, String description, Span<CommandLineArgDefinition const> args) {
    if (description.size) TRY(fmt::FormatToWriter(writer, "{}\n\n", description));

    TRY(fmt::FormatToWriter(writer, "Usage: {} [ARGS]\n\n", exe_name));

    static auto print_arg_key_val = [](Writer writer,
                                       CommandLineArgDefinition const& arg) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer, "  --{}", arg.key));

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
    for (auto const& arg : args) {
        usize key_val_size = 0;
        Writer key_val_size_writer {};
        key_val_size_writer.Set<usize>(key_val_size,
                                       [](usize& size, Span<u8 const> bytes) -> ErrorCodeOr<void> {
                                           size += bytes.size;
                                           return k_success;
                                       });
        TRY(print_arg_key_val(key_val_size_writer, arg));
        max_key_val_size = Max(max_key_val_size, key_val_size);
    }

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

    if (FindIf(args, [](auto const& arg) { return arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Required arguments:\n"));
        for (auto const& arg : args)
            if (arg.required) TRY(print_arg(writer, arg));
    }

    if (FindIf(args, [](auto const& arg) { return !arg.required; })) {
        TRY(fmt::FormatToWriter(writer, "Optional arguments:\n"));
        for (auto const& arg : args)
            if (!arg.required) TRY(print_arg(writer, arg));
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

// Doesn't support positional args, but does support things like:
// "-a", "-a=value", "--arg value", "--arg=value", "--arg value1 value2"
PUBLIC HashTable<String, Span<String>> ArgsToKeyValueTable(ArenaAllocator& arena, Span<String const> args) {
    DynamicHashTable<String, Span<String>> result {arena};
    enum class ArgType { Short, Long, None };
    auto const arg_type = [](String arg) {
        if (arg[0] == '-' && IsAlphanum(arg[1])) return ArgType::Short;
        if (arg.size > 2) {
            if (arg[0] == '-' && arg[1] == '-') return ArgType::Long;
            if (arg[0] == '-' && IsAlphanum(arg[1]) && arg[2] == '=') return ArgType::Short;
        }
        return ArgType::None;
    };
    auto const prefix_size = [](ArgType type) -> usize {
        switch (type) {
            case ArgType::Short: return 1;
            case ArgType::Long: return 2;
            case ArgType::None: return 0;
        }
        return 0;
    };
    struct KeyVal {
        String key;
        String value;
    };
    auto const try_get_combined_key_val = [](String arg) {
        if (auto const opt_index = Find(arg, '='))
            return KeyVal {arg.SubSpan(0, *opt_index), arg.SubSpan(*opt_index + 1)};
        return KeyVal {arg, ""_s};
    };

    String current_key {};
    DynamicArray<String> current_values {arena};

    for (auto const arg_index : Range(args.size)) {
        if (auto const type = arg_type(args[arg_index]); type != ArgType::None) {
            auto const arg = args[arg_index].SubSpan(prefix_size(type));
            auto const [key, value] = try_get_combined_key_val(arg);

            if (key != current_key) {
                // it's a new key, flush the values of the previous
                if (current_key.size) result.Insert(current_key, current_values.ToOwnedSpan());
                current_key = key;
            }

            if (value.size) dyn::Append(current_values, value);
        } else {
            if (current_key.size)
                dyn::Append(current_values, args[arg_index]);
            else {
                // positional arguments are not supported at the moment
            }
        }
    }

    if (current_key.size) result.Insert(current_key, current_values.ToOwnedSpan());

    return result.ToOwnedTable();
}

PUBLIC HashTable<String, Span<String>> ArgsToKeyValueTable(ArenaAllocator& arena, ArgsCstr args) {
    return ArgsToKeyValueTable(arena, ArgsToStringsSpan(arena, args, false));
}

enum class CliError {
    InvalidArguments,
    HelpRequested,
    VersionRequested,
};
extern ErrorCodeCategory const g_cli_error_code_category;
PUBLIC ErrorCodeCategory const& ErrorCategoryForEnum(CliError) { return g_cli_error_code_category; }

struct ParseCommandLineArgsOptions {
    bool handle_help_option = true;
    bool print_usage_on_error = true;
    String description {};
    String version {}; // if present will be printed on --version
};

// always returns a span the same size as the arg_defs, if an arg wasn't set it will have was_provided = false
PUBLIC ErrorCodeOr<Span<CommandLineArg>> ParseCommandLineArgs(Writer writer,
                                                              ArenaAllocator& arena,
                                                              String program_name,
                                                              Span<String const> args,
                                                              Span<CommandLineArgDefinition const> arg_defs,
                                                              ParseCommandLineArgsOptions options = {}) {
    auto error = [&](CliError e) -> ErrorCode {
        if (options.print_usage_on_error)
            TRY(PrintUsage(writer, program_name, options.description, arg_defs));
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

    for (auto const [key, values, _] : ArgsToKeyValueTable(arena, args)) {
        if (options.handle_help_option && key == "help") {
            TRY(PrintUsage(writer, program_name, options.description, arg_defs));
            return ErrorCode {CliError::HelpRequested};
        }

        if (options.version.size && key == "version") {
            TRY(fmt::FormatToWriter(writer, "Version {}\n", options.version));
            return ErrorCode {CliError::VersionRequested};
        }

        auto const arg_index = FindIf(arg_defs, [&](auto const& arg) { return arg.key == key; });
        if (!arg_index) {
            TRY(fmt::FormatToWriter(writer, "Unknown option: {}\n", key));
            return error(CliError::InvalidArguments);
        }

        auto const& arg = arg_defs[*arg_index];

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

        result[*arg_index].values = values;
        result[*arg_index].was_provided = true;
    }

    for (auto const arg_index : Range(arg_defs.size))
        if (arg_defs[arg_index].required && !result[arg_index].was_provided) {
            TRY(fmt::FormatToWriter(writer, "Required arg --{} not provided\n", arg_defs[arg_index].key));
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
    auto writer = StdWriter(StdStream::Out);
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
        }
    }

    return args;
}

// NOTE: not necessary if you created args with MakeCommandLineArgDefs - you can just use array indexing
PUBLIC constexpr CommandLineArg const* LookupArg(Span<CommandLineArg const> args, auto id) {
    auto index = FindIf(args, [&](auto const& arg) { return arg.info.id == (u32)id; });
    if (index) return &args[*index];
    return nullptr;
}
