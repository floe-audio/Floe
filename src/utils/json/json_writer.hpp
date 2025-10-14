// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

namespace json {

enum class WrittenType {
    None,
    OpenContainer,
    CloseContainer,
    Value,
    Key,
};

struct WriteContext {
    Writer out {};
    bool add_whitespace {true};

    WrittenType last_type {WrittenType::None};
    int current_indent {0};
};

// we assume utf-8
PUBLIC ErrorCodeOr<void> EncodeString(String str, Writer& out) {
    for (usize i = 0; i < str.size;) {
        auto const c = str[i];

        usize utf8_size;
        if (0 == ((s32)0xffffff80 & c))
            utf8_size = 1;
        else if (0 == ((s32)0xfffff800 & c))
            utf8_size = 2;
        else if (0 == ((s32)0xffff0000 & c))
            utf8_size = 3;
        else
            utf8_size = 4;
        DEFER { i += utf8_size; };

        if (utf8_size == 1) {
            switch (c) {
                case '"': TRY(out.WriteChars("\\\"")); break;
                case '\\': TRY(out.WriteChars("\\\\")); break;
                case '\b': TRY(out.WriteChars("\\b")); break;
                case '\f': TRY(out.WriteChars("\\f")); break;
                case '\n': TRY(out.WriteChars("\\n")); break;
                case '\r': TRY(out.WriteChars("\\r")); break;
                case '\t': TRY(out.WriteChars("\\t")); break;
                default: {
                    if (c < 0x20) {
                        TRY(out.WriteChars("\\u00"));
                        auto hex =
                            fmt::IntToString((u8)c, {.base = fmt::IntToStringOptions::Base::Hexadecimal});
                        TRY(out.WriteChars(hex));
                    } else {
                        TRY(out.WriteChar(c));
                    }
                }
            }
        } else {
            TRY(out.WriteChars(str.SubSpan(i, utf8_size)));
        }
    }

    return k_success;
}

PUBLIC String EncodeString(String str, Allocator& alloc) {
    DynamicArray<char> result(alloc);
    result.Reserve(str.size + 2);
    auto writer = dyn::WriterFor(result);

    auto _ = EncodeString(str, writer);

    return result.ToOwnedSpan();
}

namespace detail {

static ErrorCodeOr<void> Append(WriteContext& ctx, char c) {
    ASSERT(ctx.out.object != nullptr);
    return ctx.out.WriteChar(c);
}

static ErrorCodeOr<void> Append(WriteContext& ctx, String str) {
    ASSERT(ctx.out.object != nullptr);
    return ctx.out.WriteChars(str);
}

static ErrorCodeOr<void> WriteCommaAndNewLine(WriteContext& ctx) {
    if (ctx.last_type != WrittenType::OpenContainer) TRY(Append(ctx, ','));
    if (ctx.add_whitespace) TRY(Append(ctx, '\n'));
    return k_success;
}

static ErrorCodeOr<void> WriteIndent(WriteContext& ctx) {
    if (!ctx.add_whitespace) return k_success;
    auto num_to_write = ctx.current_indent;
    String const tabs {"\t\t\t\t\t\t\t\t"};

    ASSERT(num_to_write >= 0);
    while (num_to_write != 0) {
        auto const this_write_size = Min((usize)num_to_write, tabs.size);
        TRY(Append(ctx, tabs.SubSpan(0, this_write_size)));
        num_to_write -= this_write_size;
    }
    return k_success;
}

static ErrorCodeOr<void> WriteValueIndent(WriteContext& ctx) {
    ASSERT(ctx.last_type != WrittenType::None); // the first item in a json file must be a container

    if (ctx.last_type != WrittenType::Key) {
        TRY(WriteCommaAndNewLine(ctx));
        TRY(WriteIndent(ctx));
    }
    ctx.last_type = WrittenType::Value;
    return k_success;
}

template <typename T>
[[maybe_unused]] static ErrorCodeOr<void> WriteFloat(WriteContext& ctx, T val) {
    constexpr usize k_buf_size = 128;
    char buffer[k_buf_size];
    char const* format;
    if constexpr (Same<T, f32>)
        format = "%.9g";
    else
        format = "%.17g";
    auto const num_written = stbsp_snprintf(buffer, k_buf_size, format, (f64)val);
    auto str = String(buffer, (usize)num_written);
    if (auto dot_index = Find(str, '.')) {
        if (ContainsOnly(str.SubSpan(*dot_index + 1), '0')) str = {str.data, *dot_index};
    }
    return Append(ctx, str);
}

static ErrorCodeOr<void> WriteOpenContainer(WriteContext& ctx, char c) {
    if (ctx.last_type == WrittenType::Value || ctx.last_type == WrittenType::CloseContainer) {
        TRY(Append(ctx, ','));
        if (ctx.add_whitespace) TRY(Append(ctx, '\n'));
    } else if (ctx.last_type == WrittenType::OpenContainer) {
        if (ctx.add_whitespace) TRY(Append(ctx, '\n'));
    }

    if (ctx.last_type != WrittenType::Key) TRY(WriteIndent(ctx));
    TRY(Append(ctx, c));
    ctx.current_indent++;
    ctx.last_type = WrittenType::OpenContainer;
    return k_success;
}

static ErrorCodeOr<void> WriteCloseContainer(WriteContext& ctx, char c) {
    if (ctx.last_type != WrittenType::OpenContainer && ctx.add_whitespace) TRY(Append(ctx, '\n'));
    ctx.current_indent--;
    if (ctx.last_type != WrittenType::OpenContainer) TRY(WriteIndent(ctx));
    TRY(Append(ctx, c));
    ctx.last_type = WrittenType::CloseContainer;
    return k_success;
}

} // namespace detail

PUBLIC void ResetWriter(WriteContext& ctx) {
    ctx.current_indent = 0;
    ctx.last_type = WrittenType::None;
}

PUBLIC ErrorCodeOr<void> WriteKey(WriteContext& ctx, String key) {
    using namespace detail;
    ASSERT(ctx.last_type != WrittenType::Key); // can't have multiple keys
    ASSERT(ctx.last_type != WrittenType::None); // the first item in a json file must be a container

    TRY(WriteCommaAndNewLine(ctx));
    TRY(WriteIndent(ctx));
    TRY(Append(ctx, '\"'));
    TRY(EncodeString(key, ctx.out));
    TRY(Append(ctx, "\":"));
    if (ctx.add_whitespace) TRY(Append(ctx, ' '));
    ctx.last_type = WrittenType::Key;
    return k_success;
}

//
PUBLIC ErrorCodeOr<void> WriteObjectBegin(WriteContext& ctx) { return detail::WriteOpenContainer(ctx, '{'); }
PUBLIC ErrorCodeOr<void> WriteObjectEnd(WriteContext& ctx) { return detail::WriteCloseContainer(ctx, '}'); }

PUBLIC ErrorCodeOr<void> WriteArrayBegin(WriteContext& ctx) { return detail::WriteOpenContainer(ctx, '['); }
PUBLIC ErrorCodeOr<void> WriteArrayEnd(WriteContext& ctx) { return detail::WriteCloseContainer(ctx, ']'); }

//
PUBLIC ErrorCodeOr<void> WriteKeyObjectBegin(WriteContext& ctx, String key) {
    TRY(WriteKey(ctx, key));
    TRY(WriteObjectBegin(ctx));
    return k_success;
}
PUBLIC ErrorCodeOr<void> WriteKeyArrayBegin(WriteContext& ctx, String key) {
    TRY(WriteKey(ctx, key));
    TRY(WriteArrayBegin(ctx));
    return k_success;
}

//
PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, Integral auto val) {
    using namespace detail;
    TRY(WriteValueIndent(ctx));
    return Append(ctx, fmt::IntToString(val, {.base = fmt::IntToStringOptions::Base::Decimal}));
}

PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, FloatingPoint auto val) {
    using namespace detail;
    TRY(WriteValueIndent(ctx));
    return WriteFloat(ctx, val);
}

PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, bool val) {
    using namespace detail;
    TRY(WriteValueIndent(ctx));
    return Append(ctx, val ? "true"_s : "false"_s);
}

PUBLIC ErrorCodeOr<void> WriteNull(WriteContext& ctx) {
    using namespace detail;
    TRY(WriteValueIndent(ctx));
    return Append(ctx, "null");
}

PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, String val) {
    using namespace detail;

    TRY(WriteValueIndent(ctx));
    TRY(Append(ctx, '\"'));
    TRY(EncodeString(val, ctx.out));
    TRY(Append(ctx, '\"'));
    return k_success;
}

//

template <typename Type>
concept TypeWithWriteValue = requires(WriteContext& ctx, Type val) {
    { WriteValue(ctx, val) };
};

PUBLIC ErrorCodeOr<void> WriteKeyValue(WriteContext& ctx, String key, TypeWithWriteValue auto val) {
    TRY(WriteKey(ctx, key));
    return WriteValue(ctx, val);
}

PUBLIC ErrorCodeOr<void> WriteKeyNull(WriteContext& ctx, String key) {
    TRY(WriteKey(ctx, key));
    return WriteNull(ctx);
}

// Passing a string literal was getting incorrectly interpreted as a bool,
// so we specialise for that case.
template <usize N>
PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, char const (&arr)[N]) {
    return WriteValue(ctx, String(arr));
}

template <typename Type>
requires(!CharacterType<Type>)
PUBLIC ErrorCodeOr<void> WriteValue(WriteContext& ctx, Span<Type> const& val) {
    TRY(WriteArrayBegin(ctx));
    for (auto const& v : val)
        TRY(WriteValue(ctx, v));
    TRY(WriteArrayEnd(ctx));
    return k_success;
}

template <usize N>
PUBLIC ErrorCodeOr<void> WriteKeyValue(WriteContext& ctx, String key, char const (&arr)[N]) {
    return WriteKeyValue(ctx, key, String(arr));
}

} // namespace json
