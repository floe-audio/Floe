// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <stb_sprintf.h>

#include "foundation/container/dynamic_array.hpp"
#include "foundation/container/span.hpp"
#include "foundation/container/tagged_union.hpp"
#include "foundation/error/error_code.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/geometry.hpp"
#include "foundation/utils/random.hpp"
#include "foundation/utils/string.hpp"
#include "foundation/utils/time.hpp"
#include "foundation/utils/writer.hpp"

namespace fmt {

/*

IMPROVE: use the type-system and structs to configure format options instead of character codes. e.g.:
    fmt::Format("Float is {}", fmt::Arg(5.92013f, {.float_precision = 2}));
    fmt::Format("Int is {}", fmt::Arg(2340931, .{.hex = true, .show_plus = true}));
Instead of:
    fmt::Format("{.2}", 5.92013f);
    fmt::Format("{x+}", 2340931);
It's more verbose but I think it could be a system that is more more extensible and easier to understand. We
can put whatever we want in the Args options struct rather than having to use character codes.


Simple string formatting that covers the cases that we need for now. Uses python-like curly braces for
formatting.

Cool feature: you can wrap an argument to the format function with DumpStruct to dump the struct using
clang's __builtin_dump_struct, useful for debugging. e.g. Format("{}", DumpStruct(my_object));

Format (..., "{}: {}", val, "hi");

Inside the {} you can have various things as shown in the table below.
- <number> means any integer.

type                 | code       | description
-------------------------------------------------------------------------------------------------------------
any                  | <number>   | THIS MUST BE FIRST. The minimum number of characters for the value. This
                                  | is padded with spaces unless this number starts with a 0. e.g. 4 (padded
                                  | with spaces) or 04 (padded with 0).
f32 or f64           | g          | Auto-float (uses scientific notation if needed)
f32 or f64           | .<number>  | float precision e.g. {.2}
integer              | x          | Hexadecimal (lowercase)
integer              | X          | Hexadecimal (uppercase)
integer              | +          | Show a + if the number is positive
ErrorCode            | u          | Don't print the debug info of the error
DateAndTime          | t          | Print as RFC 3339 with UTC timezone

*/

constexpr usize k_rfc3339_utc_size = "YYYY-MM-ddTHH:mm:ss.sssZ"_s.size;
constexpr auto k_timestamp_str_size = "2022-12-31 23:59:59.999"_s.size;

struct FormatOptions {
    bool auto_float_format = false;
    bool lowercase_hex = false;
    bool uppercase_hex = false;
    bool show_plus = false;
    String float_precision {};
    bool is_string_literal {};
    bool error_debug_info = true;
    u64 required_width {};
    char padding_character {};
    bool rfc3339_utc = false;
};

PUBLIC constexpr ErrorCodeOr<void>
PadToRequiredWidthIfNeeded(Writer writer, FormatOptions options, usize size) {
    if (options.required_width && size < options.required_width)
        TRY(writer.WriteCharRepeated(options.padding_character, options.required_width - size));
    return k_success;
}

template <Scalar Type>
constexpr auto ScalarToInt(Type x) {
    if constexpr (Pointer<Type>)
        return (uintptr)x;
    else if constexpr (Enum<Type>)
        return (UnderlyingType<Type>)x;
    else
        return x;
}

template <typename T>
concept TypeWithCustomValueToString =
    requires(T value, Writer writer, FormatOptions options) { CustomValueToString(writer, value, options); };

struct IntToStringOptions {
    enum class Base { Decimal, Hexadecimal, Base32 };
    Base base = Base::Decimal;
    bool include_sign = false;
    bool capitalize = false;
};

template <Integral IntType>
PUBLIC constexpr usize IntToString(IntType num, char* buffer, IntToStringOptions options = {}) {
    if (num == 0) {
        buffer[0] = '0';
        return 1;
    }

    IntType base {};
    switch (options.base) {
        case IntToStringOptions::Base::Decimal: base = 10; break;
        case IntToStringOptions::Base::Hexadecimal: base = 16; break;
        case IntToStringOptions::Base::Base32: base = 32; break;
    }

    bool const is_negative = SignedInt<IntType> && num < 0;
    if (is_negative) num = -num; // Make the number positive for conversion

    // Start with the end of the buffer
    char* ptr = buffer;

    if (num == 0) {
        *ptr++ = '0';
    } else {
        do {
            *ptr++ = (options.capitalize ? "0123456789ABCDEFGHIJKLMNPQRSTVWX"
                                         : "0123456789abcdefghijklmnpqrstvwx")[num % base];
            num /= base;
        } while (num != 0);
    }

    if constexpr (SignedInt<IntType>) {
        if (is_negative)
            *ptr++ = '-';
        else if (options.include_sign)
            *ptr++ = '+';
    }

    *ptr = '\0';

    // Reverse the string
    auto const length = (usize)(ptr - buffer);
    char* start = buffer;
    char* end = buffer + length - 1;
    while (start < end) {
        char const temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }

    return length;
}

template <Integral IntType>
PUBLIC constexpr DynamicArrayBounded<char, 32> IntToString(IntType num, IntToStringOptions options = {}) {
    DynamicArrayBounded<char, 32> result;
    result.size = IntToString(num, result.data, options);
    return result;
}

template <Struct T>
struct DumpStructWrapper {
    T const& value;
};
template <typename T>
constexpr DumpStructWrapper<T> DumpStruct(T const& x) {
    return {x};
}

struct DumpStructContext {
    Writer writer;
    usize pos {};
    char buffer[4000];
};

static constexpr void DumpStructSprintf(DumpStructContext& ctx, char const* format, auto... args) {
    auto const size_remaining = (int)(ArraySize(ctx.buffer) - ctx.pos);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wdouble-promotion"
    auto const n = stbsp_snprintf(ctx.buffer + ctx.pos, size_remaining, format, args...);
#pragma clang diagnostic pop

    ASSERT(n >= 0);

    auto const num_written = Min(size_remaining, n);
    ctx.pos += (usize)num_written;
}

template <typename T>
PUBLIC ErrorCodeOr<void> ValueToString(Writer writer, T const& value, FormatOptions options) {
    using Type = RemoveCV<T>;

    if constexpr (TypeWithCustomValueToString<Type>) {
        return CustomValueToString(writer, value, options);
    }

    else if constexpr (IsSpecializationOf<Type, Optional>) {
        if (value.HasValue())
            return ValueToString(writer, *value, options);
        else
            TRY(writer.WriteChars("k_nullopt"_s));
        return k_success;
    }

    else if constexpr (FloatingPoint<Type>) {
        u32 format_buf_size = 0;
        char format_buf[8];
        format_buf[format_buf_size++] = '%';
        for (auto c : options.float_precision)
            format_buf[format_buf_size++] = c;
        format_buf[format_buf_size++] = options.auto_float_format ? 'g' : 'f';
        format_buf[format_buf_size++] = '\0';

        char buffer[32];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
        auto const num_written = stbsp_snprintf(buffer, (int)ArraySize(buffer), format_buf, value);
#pragma clang diagnostic pop
        if (num_written < 0) Panic("invalid snprintf format");
        TRY(PadToRequiredWidthIfNeeded(writer, options, (usize)num_written));
        TRY(writer.WriteChars(String {buffer, (usize)num_written}));
        return k_success;
    }

    else if constexpr (Same<Type, char>) {
        TRY(PadToRequiredWidthIfNeeded(writer, options, 1));
        TRY(writer.WriteChar(value));
        return k_success;
    }

    else if constexpr (Same<Type, bool>) {
        String const str = value ? "true"_s : "false"_s;
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Same<Type, SourceLocation>) {
        auto const file = FromNullTerminated(value.file);
        auto const file_line_separator = ":"_s;
        auto const line = IntToString(value.line);
        auto const function = FromNullTerminated(value.function);
        auto const line_function_separator = ": "_s;
        auto const size =
            file.size + file_line_separator.size + line.size + line_function_separator.size + function.size;
        TRY(PadToRequiredWidthIfNeeded(writer, options, size));
        for (auto const str : Array {file, file_line_separator, line, line_function_separator, function})
            TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Same<Type, DateAndTime>) {
        auto size = k_rfc3339_utc_size;
        if (!options.rfc3339_utc) size -= 1; // we don't need the Z

        TRY(PadToRequiredWidthIfNeeded(writer, options, size));
        TRY(ValueToString(writer, value.year, {.required_width = 4, .padding_character = '0'}));
        TRY(writer.WriteChar('-'));
        TRY(ValueToString(writer,
                          value.months_since_jan + 1,
                          {.required_width = 2, .padding_character = '0'}));
        TRY(writer.WriteChar('-'));
        TRY(ValueToString(writer, value.day_of_month, {.required_width = 2, .padding_character = '0'}));
        TRY(writer.WriteChar(options.rfc3339_utc ? 'T' : ' '));
        TRY(ValueToString(writer, value.hour, {.required_width = 2, .padding_character = '0'}));
        TRY(writer.WriteChar(':'));
        TRY(ValueToString(writer, value.minute, {.required_width = 2, .padding_character = '0'}));
        TRY(writer.WriteChar(':'));
        TRY(ValueToString(writer, value.second, {.required_width = 2, .padding_character = '0'}));
        TRY(writer.WriteChar('.'));
        TRY(ValueToString(writer, value.millisecond, {.required_width = 3, .padding_character = '0'}));
        if (options.rfc3339_utc) TRY(writer.WriteChar('Z'));
        return k_success;
    }

    else if constexpr (Convertible<Type, String> || ConstructibleWithArgs<String, Type>) {
        String const str {value};
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (Convertible<Type, char const*>) {
        auto const str = FromNullTerminated(value);
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (EnumWithCount<Type>) {
        auto const str = EnumToString(value);
        TRY(PadToRequiredWidthIfNeeded(writer, options, str.size));
        TRY(writer.WriteChars(str));
        return k_success;
    }

    else if constexpr (IsSpecializationOf<Type, TaggedUnion>) {
        ErrorCodeOr<void> result = k_success;
        value.Visit([&](auto const& x) { result = ValueToString(writer, x, options); });
        return result;
    }

    else if constexpr (Integral<Type> || Enum<Type> || Convertible<AddConst<Type>, void const*>) {
        char buffer[32] {};
        auto const size = IntToString(ScalarToInt(value),
                                      buffer,
                                      {
                                          .base = (options.lowercase_hex || options.uppercase_hex)
                                                      ? IntToStringOptions::Base::Hexadecimal
                                                      : IntToStringOptions::Base::Decimal,
                                          .include_sign = options.show_plus,
                                          .capitalize = options.uppercase_hex,
                                      });
        TRY(PadToRequiredWidthIfNeeded(writer, options, size));
        TRY(writer.WriteChars(String {buffer, size}));
        return k_success;
    }

    else if constexpr (IsSpecializationOf<Type, Span>) {
        // IMPROVE: support width option
        ASSERT(options.required_width == 0);
        options.required_width = 0;
        TRY(writer.WriteChar('{'));
        for (auto const& i : value) {
            TRY(ValueToString(writer, i, options));
            if (&i != &Last(value)) TRY(writer.WriteChars(", "_s));
        }
        TRY(writer.WriteChar('}'));
        return k_success;
    }

    else if constexpr (Same<Type, ErrorCode>) {
        // IMPROVE: support width option
        ASSERT(options.required_width == 0);
        if (value.category == nullptr) return writer.WriteChars("success");
        if (value.category->message) TRY(value.category->message(writer, value));
        TRY(writer.WriteChars(" (error "));
        TRY(writer.WriteChars(FromNullTerminated(value.category->category_id)));
        TRY(writer.WriteChars(IntToString(value.code)));
        TRY(writer.WriteChar(')'));
        if (options.error_debug_info) {
            TRY(writer.WriteChar('\n'));

            TRY(writer.WriteChars(FromNullTerminated(value.source_location.file)));
            TRY(writer.WriteChar(':'));
            TRY(writer.WriteChars(IntToString(value.source_location.line)));

            TRY(writer.WriteChars(": "));
            TRY(writer.WriteChars(FromNullTerminated(value.source_location.function)));
            if (value.extra_debug_info) {
                TRY(writer.WriteChars(", "));
                TRY(writer.WriteChars(FromNullTerminated(value.extra_debug_info)));
            }
        }
        return k_success;
    }

    else if constexpr (IsSpecializationOf<Type, DumpStructWrapper>) {
        DumpStructContext ctx {.writer = writer};
        __builtin_dump_struct(&value.value, DumpStructSprintf, ctx);
        if (ctx.pos != ArraySize(ctx.buffer)) --ctx.pos;
        TRY(writer.WriteChars(String {ctx.buffer, ctx.pos}));
        return k_success;
    }

    else if constexpr (Same<Type, Rect>) {
        // IMPROVE: support width option
        ASSERT(options.required_width == 0);
        TRY(writer.WriteChar('('));
        TRY(ValueToString(writer, value.pos, options));
        TRY(writer.WriteChars(", "_s));
        TRY(ValueToString(writer, value.size, options));
        TRY(writer.WriteChar(')'));
        return k_success;
    }

    else if constexpr (Vector<Type>) {
        ASSERT(options.required_width == 0); // IMPROVE: support width option
        constexpr auto k_num_elements = NumVectorElements<Type>();
        TRY(writer.WriteChar('('));
        for (auto const i : Range(k_num_elements)) {
            TRY(ValueToString(writer, value[i], options));
            if (i != k_num_elements - 1) TRY(writer.WriteChars(", "_s));
        }
        TRY(writer.WriteChar(')'));
        return k_success;
    }

    else
        static_assert(
            false,
            "Unsupported type for formatting, either add it to the function or define CustomValueToString function for it");

    PanicIfReached();
    return k_success;
}

namespace details {

struct BraceSectionResult {
    char const* new_p;
    bool should_format_type;
    FormatOptions options;
};

// This is primarily done in a separate function in order to reduce the size of the template function
[[maybe_unused]] static ErrorCodeOr<BraceSectionResult>
ParseBraceSection(Writer writer, char const* p, char const* end) {
    ASSERT(*p == '{');
    for (; p < end; ++p) {
        ++p;
        if (p == end) Panic("mismatched {}");
        if (*p == '{') {
            TRY(writer.WriteChar('{'));
            return BraceSectionResult {.new_p = p, .should_format_type = false};
        }

        auto const start = p;
        while (*p != '}') {
            if (p == end) Panic("mismatched {}");
            ++p;
        }
        String brace_contents {start, (usize)(p - start)};
        FormatOptions options {};
        options.padding_character = ' ';

        if (brace_contents.size) {
            if (StartsWith(brace_contents, '0')) options.padding_character = '0';

            usize num_chars_read = 0;
            if (auto o = ParseInt(brace_contents, ParseIntBase::Decimal, &num_chars_read); o.HasValue()) {
                options.required_width = (u64)Max<s64>(o.Value(), 0);
                brace_contents.RemovePrefix(num_chars_read);
            }

            for (usize i = 0; i < brace_contents.size; ++i) {
                switch (brace_contents[i]) {
                    case 'g': options.auto_float_format = true; break;
                    case 'x': options.lowercase_hex = true; break;
                    case 'X': options.uppercase_hex = true; break;
                    case '+': options.show_plus = true; break;
                    case '.': {
                        usize num_numbers = 1;
                        for (auto c : brace_contents.SubSpan(i + 1))
                            if (c >= '0' && c <= '9')
                                ++num_numbers;
                            else
                                break;
                        if (num_numbers == 1) Panic("expected numbers after . in format string");

                        options.float_precision = brace_contents.SubSpan(i, num_numbers);
                        i += num_numbers;
                        break;
                    }
                    case 'u': options.error_debug_info = false; break;
                    case 't': options.rfc3339_utc = true; break;
                    default: Panic("unknown options inside {}");
                }
            }
        }

        BraceSectionResult result {};
        result.new_p = p;
        result.should_format_type = true;
        result.options = options;
        return result;
    }
    PanicIfReached();
    return {};
}

// 'format' is advanced to after the next brace-section
template <typename Arg>
[[maybe_unused]] static ErrorCodeOr<void>
FindAndWriteNextValue(Writer writer, String& format, Arg const& arg) {
    auto const starting_size = format.size;
    auto const end = format.end();
    for (auto p = format.begin(); p != end; ++p) {
        if (*p == '{') {
            auto brace_result = TRY(ParseBraceSection(writer, p, end));
            p = brace_result.new_p;
            if (!brace_result.should_format_type) continue;

            TRY(ValueToString(writer, arg, brace_result.options));

            ASSERT(p >= format.begin());
            format.RemovePrefix((usize)(p - format.begin() + 1));
            break;
        } else if (*p == '}') {
            ++p;
            if (p == end || *p != '}') Panic("mismatched }");
            TRY(writer.WriteChar('}'));
        } else {
            TRY(writer.WriteChar(*p));
        }
    }

    if (starting_size == format.size) Panic("more args than {}");

    return k_success;
}

static constexpr ErrorCodeOr<void> WriteRemaining(Writer writer, String format) {
    auto const end = format.end();
    for (auto p = format.begin(); p != end; ++p) {
        if (*p == '{') {
            ++p;
            if (p == end || *p != '{') Panic("more {} than args");
        } else if (*p == '}') {
            ++p;
            if (p == end || *p != '}') Panic("mismatched {}");
        }
        TRY(writer.WriteChar(*p));
    }

    return k_success;
}

} // namespace details

// Writer
//=================================================
template <typename... Args>
PUBLIC ErrorCodeOr<void> FormatToWriter(Writer writer, String format, Args const&... args) {
    ErrorCodeOr<void> result = k_success;
    bool const succeeded =
        ((result = details::FindAndWriteNextValue(writer, format, args), result.Succeeded()) && ...);
    if (!succeeded) return result;
    return details::WriteRemaining(writer, format);
}

template <typename... Args>
PUBLIC inline ErrorCodeOr<void> AppendLine(Writer writer, String format, Args const&... args) {
    TRY(FormatToWriter(writer, format, args...));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

template <typename Arg>
PUBLIC inline ErrorCodeOr<void> AppendRaw(Writer writer, Arg const& arg) {
    return ValueToString(writer, arg, FormatOptions {});
}

template <typename Arg>
PUBLIC inline ErrorCodeOr<void> AppendLineRaw(Writer writer, Arg const& arg) {
    TRY(ValueToString(writer, arg, FormatOptions {}));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

// Char buffer
// We tend to ignore writer errors here because we don't handle memory allocation failure anyways
//=================================================
template <dyn::DynArray DynCharArray, typename... Args>
PUBLIC inline void Append(DynCharArray& output, String format, Args const&... args) {
    output.Reserve(output.size + (format.size * 2));
    auto result = FormatToWriter(dyn::WriterFor(output), format, args...);
    if (result.HasError()) __builtin_debugtrap();
}

template <dyn::DynArray DynCharArray, typename... Args>
PUBLIC inline auto& Assign(DynCharArray& output, String format, Args const&... args) {
    dyn::Clear(output);
    output.Reserve(format.size * 2);
    auto const outcome = FormatToWriter(dyn::WriterFor(output), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
    return output;
}

template <usize k_size, typename... Args>
PUBLIC_INLINE DynamicArrayBounded<char, k_size> FormatInline(String format, Args const&... args) {
    DynamicArrayBounded<char, k_size> result;
    auto const outcome = FormatToWriter(dyn::WriterFor(result), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
    return result;
}

template <typename... Args>
PUBLIC inline MutableString Format(Allocator& a, String format, Args const&... args) {
    DynamicArray<char> result {a};
    result.Reserve(format.size * 2);
    auto const outcome = FormatToWriter(dyn::WriterFor(result), format, args...);
    if (outcome.HasError()) __builtin_debugtrap();
    return result.ToOwnedSpan();
}

//=================================================
struct StringReplacement {
    String find;
    String replacement;
};

PUBLIC MutableString FormatStringReplace(Allocator& a,
                                         String text,
                                         Span<StringReplacement const> replacements) {
    if (replacements.size == 0) return a.Clone(text);

    DynamicArray<char> result {a};
    result.Reserve(text.size * 2 / 3);

    while (text.size) {
        bool found = false;
        for (auto r : replacements) {
            if (StartsWithSpan(text, r.find)) {
                dyn::AppendSpan(result, r.replacement);
                text.RemovePrefix(r.find.size);
                found = true;
            }
        }
        if (!found) {
            dyn::Append(result, text[0]);
            text.RemovePrefix(1);
        }
    }

    return result.ToOwnedSpan();
}

PUBLIC MutableString Join(Allocator& a, Span<String const> strings, String separator = {}) {
    if (strings.size == 0) return {};

    auto const total_size = TotalSize(strings) + (separator.size * (strings.size - 1));
    auto result = a.AllocateExactSizeUninitialised<char>(total_size);
    usize pos = 0;
    for (auto const [i, part] : Enumerate(strings)) {
        WriteAndIncrement(pos, result, part);
        if (separator.size && i != strings.size - 1) WriteAndIncrement(pos, result, separator);
    }

    return result;
}

[[nodiscard]] PUBLIC MutableString JoinAppendResizeAllocation(Allocator& a,
                                                              MutableString allocated_path,
                                                              Span<String const> strings,
                                                              String separator = {}) {
    if (!strings.size) return allocated_path;
    auto result =
        a.ResizeType(allocated_path,
                     allocated_path.size,
                     allocated_path.size + TotalSize(strings) + (separator.size * (strings.size - 1)));
    usize pos = allocated_path.size;
    for (auto const [i, part] : Enumerate(strings)) {
        WriteAndIncrement(pos, result, part);
        if (separator.size && i != strings.size - 1) WriteAndIncrement(pos, result, separator);
    }

    return result;
}

template <usize k_size>
PUBLIC_INLINE DynamicArrayBounded<char, k_size> JoinInline(Span<String const> strings,
                                                           String separator = {}) {
    DynamicArrayBounded<char, k_size> result;

    for (auto const [i, part] : Enumerate(strings)) {
        if (auto const p = part.SubSpan(0, Min(k_size - result.size, part.size)); p.size)
            WriteAndIncrement(result.size, (char*)result.data, part);
        else
            return result;
        if (separator.size && i != strings.size - 1 && result.size != k_size)
            WriteAndIncrement(result.size, (char*)result.data, separator);
    }

    return result;
}

// buffer must be at least 8 bytes
PUBLIC usize PrettyFileSize(f64 size, char* buffer) {
    char const* units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    u32 unit = 0;

    // Max string: "999.99 PB" (9 chars), so we need to limit the size
    if (size >= 1000.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0) // 1000 PB
        return 0;

    while (size >= 1024.0 && unit < 5) {
        size /= 1024.0;
        unit++;
    }

    // Format based on unit size
    if (unit >= 3) // GB and above
        return CheckedCast<usize>(stbsp_snprintf(buffer, 8, "%.2f %s", size, units[unit]));
    else
        return CheckedCast<usize>(stbsp_snprintf(buffer, 8, "%.0f %s", size, units[unit]));
}

PUBLIC_INLINE DynamicArrayBounded<char, 8> PrettyFileSize(f64 size) {
    DynamicArrayBounded<char, 8> result;
    result.size = PrettyFileSize(size, result.data);
    return result;
}

constexpr usize k_uuid_size = 32;

using UuidArray = Array<char, k_uuid_size>;

// Write 32 chars, using all 8 bytes of each u64
PUBLIC void Uuid(u64& seed, char* out) {
    for (usize i = 0; i < 4; i++) {
        u64 const r = RandomU64(seed);
        for (usize j = 0; j < 8; j++)
            out[(i * 8) + j] = "0123456789abcdef"[r >> (j * 4) & 0xf];
    }
}

PUBLIC String Uuid(u64& seed, Allocator& a) {
    auto result = a.AllocateExactSizeUninitialised<char>(k_uuid_size);
    Uuid(seed, result.data);
    return result;
}

PUBLIC UuidArray Uuid(u64& seed) {
    UuidArray result;
    Uuid(seed, (char*)result.data);
    return result;
}

using TimestampRfc3339UtcArray = DynamicArrayBounded<char, k_rfc3339_utc_size>;

PUBLIC TimestampRfc3339UtcArray TimestampRfc3339Utc(DateAndTime date) {
    DynamicArrayBounded<char, k_rfc3339_utc_size> result;
    auto const _ = ValueToString(dyn::WriterFor(result), date, {.rfc3339_utc = true});
    return result;
}

} // namespace fmt
