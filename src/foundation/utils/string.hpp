// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "foundation/container/dynamic_array.hpp"
#include "foundation/universal_defs.hpp"

// Widen/narrow code from stb.h:
// https://github.com/nothings/stb/blob/ae721c50eaf761660b4f90cc590453cdb0c2acd0/deprecated/stb.h#L1010

PUBLIC constexpr usize MaxWidenedStringSize(String utf8_str) { return utf8_str.size; }

PUBLIC constexpr Optional<usize> WidenToBuffer(wchar_t* out, String utf8_str) {
    auto const text_encoding_error = k_nullopt;

    auto str = (unsigned char const*)utf8_str.data;
    u32 c;
    auto const end = str + utf8_str.size;
    usize out_size = 0;
    while (str < end) {
        if (!(*str & 0x80)) {
            out[out_size++] = (wchar_t)*str;
            str++;
        } else if ((*str & 0xe0) == 0xc0) {
            if (*str < 0xc2) return text_encoding_error;
            c = ((u32)*str++ & 0x1f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            out[out_size++] = (wchar_t)(c + (*str & 0x3f));
            str++;
        } else if ((*str & 0xf0) == 0xe0) {
            if (*str == 0xe0 && (str[1] < 0xa0 || str[1] > 0xbf)) return text_encoding_error;
            if (*str == 0xed && str[1] > 0x9f) return text_encoding_error; // str[1] < 0x80 is checked below
            c = ((u32)*str++ & 0x0f) << 12;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            out[out_size++] = (wchar_t)(c + (*str & 0x3f));
            str++;
        } else if ((*str & 0xf8) == 0xf0) {
            if (*str > 0xf4) return text_encoding_error;
            if (*str == 0xf0 && (str[1] < 0x90 || str[1] > 0xbf)) return text_encoding_error;
            if (*str == 0xf4 && str[1] > 0x8f) return text_encoding_error; // str[1] < 0x80 is checked below
            c = ((u32)*str++ & 0x07) << 18;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 12;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += ((u32)*str++ & 0x3f) << 6;
            if ((*str & 0xc0) != 0x80) return text_encoding_error;
            c += (*str++ & 0x3f);
            // utf-8 encodings of values used in surrogate pairs are invalid
            if ((c & 0xFFFFF800) == 0xD800) return text_encoding_error;
            if (c >= 0x10000) {
                c -= 0x10000;
                out[out_size++] = (wchar_t)(0xD800 | (0x3ff & (c >> 10)));
                out[out_size++] = (wchar_t)(0xDC00 | (0x3ff & (c)));
            }
        } else {
            return text_encoding_error;
        }
    }
    return out_size;
}

PUBLIC constexpr usize MaxNarrowedStringSize(WString wstr) { return wstr.size * 3; }
PUBLIC constexpr usize MaxNarrowedStringSize(usize wstr_size) { return wstr_size * 3; }

PUBLIC constexpr Optional<usize> NarrowToBuffer(char* out, WString wstr) {
    auto const text_encoding_error = k_nullopt;
    auto str = wstr.data;
    auto const end = wstr.data + wstr.size;
    usize out_size = 0;
    while (str < end) {
        if (*str < 0x80) {
            out[out_size++] = (char)*str;
            str++;
        } else if (*str < 0x800) {
            out[out_size++] = (char)(0xc0 + (*str >> 6));
            out[out_size++] = (char)(0x80 + (*str & 0x3f));
            str += 1;
        } else if (*str >= 0xd800 && *str < 0xdc00) {
            u32 const c = (((u32)str[0] - 0xd800) << 10) + (((u32)str[1]) - 0xdc00) + 0x10000;
            out[out_size++] = (char)(0xf0 + (c >> 18));
            out[out_size++] = (char)(0x80 + ((c >> 12) & 0x3f));
            out[out_size++] = (char)(0x80 + ((c >> 6) & 0x3f));
            out[out_size++] = (char)(0x80 + ((c) & 0x3f));
            str += 2;
        } else if (*str >= 0xdc00 && *str < 0xe000) {
            return text_encoding_error;
        } else {
            out[out_size++] = (char)(0xe0 + (*str >> 12));
            out[out_size++] = (char)(0x80 + ((*str >> 6) & 0x3f));
            out[out_size++] = (char)(0x80 + ((*str) & 0x3f));
            str += 1;
        }
    }
    return out_size;
}

PUBLIC constexpr bool WidenAppend(DynamicArray<wchar_t>& out, String utf8_str) {
    auto const reserved = out.Reserve(out.size + MaxWidenedStringSize(utf8_str));
    ASSERT(reserved);
    auto const size = TRY_OPT_OR(WidenToBuffer(out.data + out.size, utf8_str), return false);
    out.ResizeWithoutCtorDtor(out.size + size);
    return true;
}

PUBLIC constexpr bool NarrowAppend(DynamicArray<char>& out, WString wstr) {
    auto const reserved = out.Reserve(out.size + MaxNarrowedStringSize(wstr));
    ASSERT(reserved);
    auto const size = TRY_OPT_OR(NarrowToBuffer(out.data + out.size, wstr), return false);
    out.ResizeWithoutCtorDtor(out.size + size);
    return true;
}

PUBLIC constexpr Optional<Span<wchar_t>> Widen(Allocator& a, String utf8_str) {
    Span<wchar_t> const result = a.AllocateExactSizeUninitialised<wchar_t>(MaxWidenedStringSize(utf8_str));
    auto const size = TRY_OPT_OR(WidenToBuffer(result.data, utf8_str), return k_nullopt);
    return a.ResizeType(result, size, size);
}

PUBLIC constexpr Optional<MutableString> Narrow(Allocator& a, WString wstr) {
    DynamicArray<char> result {a};
    if (!NarrowAppend(result, wstr)) return k_nullopt;
    return result.ToOwnedSpan();
}

// do not Free() the result. Has a null terminator, but it is not included in the size.
PUBLIC constexpr Optional<Span<wchar_t>> WidenAllocNullTerm(ArenaAllocator& allocator, String utf8_str) {
    DynamicArray<wchar_t> buffer {allocator};
    if (!WidenAppend(buffer, utf8_str)) return k_nullopt;
    dyn::Append(buffer, L'\0');
    auto result = buffer.ToOwnedSpan();
    result.RemoveSuffix(1);
    return result;
}

// do not Free() the result.
PUBLIC constexpr Optional<MutableString> NarrowAllocNullTerm(ArenaAllocator& allocator, WString wstr) {
    DynamicArray<char> buffer {allocator};
    if (!NarrowAppend(buffer, wstr)) return k_nullopt;
    dyn::Append(buffer, '\0');
    auto result = buffer.ToOwnedSpan();
    result.RemoveSuffix(1);
    return result;
}

constexpr u32 k_invalid_unicode_codepoint = 0xFFFD;

// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
// Convert UTF-8 to 32-bit character, process single character input.
// A nearly-branchless UTF-8 decoder, based on work of Christopher Wellons
// (https://github.com/skeeto/branchless-utf8). We handle UTF-8 decoding error by skipping forward.
PUBLIC constexpr usize Utf8CharacterToUtf32(u32* out_char,
                                            char const* in_text,
                                            char const* in_text_end,
                                            u32 max_codepoint = 0x10FFFF) {
    constexpr u8 const k_lengths[32] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                        0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0};
    constexpr u32 const k_masks[] = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
    constexpr u32 const k_mins[] = {0x400000, 0, 0x80, 0x800, 0x10000};
    constexpr u32 const k_shiftc[] = {0, 18, 12, 6, 0};
    constexpr u32 const k_shifte[] = {0, 6, 4, 2, 0};
    auto const len = k_lengths[(u8)*in_text >> 3];
    usize wanted = len + !len;

    if (in_text_end == nullptr)
        in_text_end = in_text + wanted; // Max length, nulls will be taken into account.

    // Copy at most 'len' bytes, stop copying at 0 or past in_text_end. Branch predictor does a good job here,
    // so it is fast even with excessive branching.
    u8 s[4];
    s[0] = in_text + 0 < in_text_end ? (u8)in_text[0] : 0;
    s[1] = in_text + 1 < in_text_end ? (u8)in_text[1] : 0;
    s[2] = in_text + 2 < in_text_end ? (u8)in_text[2] : 0;
    s[3] = in_text + 3 < in_text_end ? (u8)in_text[3] : 0;

    // Assume a four-byte character and load four bytes. Unused bits are shifted out.
    *out_char = (u32)(s[0] & k_masks[len]) << 18;
    *out_char |= (u32)(s[1] & 0x3f) << 12;
    *out_char |= (u32)(s[2] & 0x3f) << 6;
    *out_char |= (u32)(s[3] & 0x3f) << 0;
    *out_char >>= k_shiftc[len];

    // Accumulate the various error conditions.
    int e = 0;
    e = (*out_char < k_mins[len]) << 6; // non-canonical encoding
    e |= ((*out_char >> 11) == 0x1b) << 7; // surrogate half?
    e |= (*out_char > max_codepoint) << 8; // out of range?
    e |= (s[1] & 0xc0) >> 2;
    e |= (s[2] & 0xc0) >> 4;
    e |= (s[3]) >> 6;
    e ^= 0x2a; // top two bits of each tail byte correct?
    e >>= k_shifte[len];

    if (e) {
        // No bytes are consumed when *in_text == 0 || in_text == in_text_end.
        // One byte is consumed in case of invalid first byte of in_text.
        // All available bytes (at most `len` bytes) are consumed on incomplete/invalid second to last bytes.
        // Invalid or incomplete input may consume less bytes than wanted, therefore every byte has to be
        // inspected in s.
        wanted = Min<usize>(wanted, !!s[0] + !!s[1] + !!s[2] + !!s[3]);
        *out_char = k_invalid_unicode_codepoint;
    }

    return wanted;
}

PUBLIC constexpr u32 Utf8CharacterToUtf32(String utf8, u32 max_codepoint = 0x10FFFF) {
    u32 out_char = 0;
    auto const consumed = Utf8CharacterToUtf32(&out_char, utf8.data, utf8.data + utf8.size, max_codepoint);
    if (consumed == 0) return k_invalid_unicode_codepoint; // Invalid UTF-8.
    return out_char;
}

// From public domain https://github.com/sheredom/utf8.h/tree/master
// Returns the positions of any invalid utf8 characters in the string, else returns null.
PUBLIC constexpr char* InvalidUtf8Position(String string) {
    auto str = string.data;
    auto const n = string.size;
    char const* t = str;
    usize consumed = 0;

    while ((void)(consumed = (usize)(str - t)), consumed < n && '\0' != *str) {
        usize const remaining = n - consumed;

        if (0xf0 == (0xf8 & *str)) {
            /* ensure that there's 4 bytes or more remaining */
            if (remaining < 4) return (char*)str;

            /* ensure each of the 3 following bytes in this 4-byte
             * utf8 codepoint began with 0b10xxxxxx */
            if ((0x80 != (0xc0 & str[1])) || (0x80 != (0xc0 & str[2])) || (0x80 != (0xc0 & str[3])))
                return (char*)str;

            /* ensure that our utf8 codepoint ended after 4 bytes */
            if ((remaining != 4) && (0x80 == (0xc0 & str[4]))) return (char*)str;

            /* ensure that the top 5 bits of this 4-byte utf8
             * codepoint were not 0, as then we could have used
             * one of the smaller encodings */
            if ((0 == (0x07 & str[0])) && (0 == (0x30 & str[1]))) return (char*)str;

            /* 4-byte utf8 code point (began with 0b11110xxx) */
            str += 4;
        } else if (0xe0 == (0xf0 & *str)) {
            /* ensure that there's 3 bytes or more remaining */
            if (remaining < 3) return (char*)str;

            /* ensure each of the 2 following bytes in this 3-byte
             * utf8 codepoint began with 0b10xxxxxx */
            if ((0x80 != (0xc0 & str[1])) || (0x80 != (0xc0 & str[2]))) return (char*)str;

            /* ensure that our utf8 codepoint ended after 3 bytes */
            if ((remaining != 3) && (0x80 == (0xc0 & str[3]))) return (char*)str;

            /* ensure that the top 5 bits of this 3-byte utf8
             * codepoint were not 0, as then we could have used
             * one of the smaller encodings */
            if ((0 == (0x0f & str[0])) && (0 == (0x20 & str[1]))) return (char*)str;

            /* 3-byte utf8 code point (began with 0b1110xxxx) */
            str += 3;
        } else if (0xc0 == (0xe0 & *str)) {
            /* ensure that there's 2 bytes or more remaining */
            if (remaining < 2) return (char*)str;

            /* ensure the 1 following byte in this 2-byte
             * utf8 codepoint began with 0b10xxxxxx */
            if (0x80 != (0xc0 & str[1])) return (char*)str;

            /* ensure that our utf8 codepoint ended after 2 bytes */
            if ((remaining != 2) && (0x80 == (0xc0 & str[2]))) return (char*)str;

            /* ensure that the top 4 bits of this 2-byte utf8
             * codepoint were not 0, as then we could have used
             * one of the smaller encodings */
            if (0 == (0x1e & str[0])) return (char*)str;

            /* 2-byte utf8 code point (began with 0b110xxxxx) */
            str += 2;
        } else if (0x00 == (0x80 & *str)) {
            /* 1-byte ascii (began with 0b0xxxxxxx) */
            str += 1;
        } else {
            /* we have an invalid 0b1xxxxxxx utf8 code point entry */
            return (char*)str;
        }
    }

    return nullptr;
}

PUBLIC constexpr usize Utf8CodepointSize(char first_byte) {
    if (0xf0 == (0xf8 & first_byte)) {
        /* 4 byte utf8 codepoint */
        return 4;
    } else if (0xe0 == (0xf0 & first_byte)) {
        /* 3 byte utf8 codepoint */
        return 3;
    } else if (0xc0 == (0xe0 & first_byte)) {
        /* 2 byte utf8 codepoint */
        return 2;
    }

    /* 1 byte utf8 codepoint otherwise */
    return 1;
}

PUBLIC constexpr bool IsValidUtf8(String string) { return InvalidUtf8Position(string) == nullptr; }

// The string must be valid UTF-8.
PUBLIC constexpr usize FindUtf8TruncationPoint(String str, usize max_size) {
    ASSERT(str.size > 0);
    ASSERT(max_size < str.size);

    auto const last = str.data[max_size - 1];

    // Check if the last byte is part of a multi-byte sequence.
    if (last & 0b10000000) {
        if (last & 0b01000000) {
            // If the last byte is the start of a multi-byte sequence, we know we can truncate 1 before it.
            return max_size - 1;
        } else {
            // If the last byte is not the start of a multi-byte sequence, we need to find the start of the
            // sequence.
            ASSERT(max_size >= 2, "Invalid UTF-8 string");
            ASSERT(str.size >= 2, "Invalid UTF-8 string");
            auto const second_last = str.data[max_size - 2];

            if ((second_last & 0b11100000) == 0b11100000) {
                // 3-byte sequence
                return max_size - 2;
            } else {
                ASSERT(max_size >= 3, "Invalid UTF-8 string");
                ASSERT(str.size >= 3, "Invalid UTF-8 string");
                auto const third_last = str.data[max_size - 3];

                if ((third_last & 0b11110000) == 0b11110000) {
                    // 4-byte sequence
                    return max_size - 3;
                }
            }
        }
    }

    return max_size;
}

PUBLIC constexpr Optional<String>
SplitWithIterator(String whole, usize& cursor, char token, bool skip_consecutive = false) {
    if (cursor >= whole.size) return k_nullopt;

    if (skip_consecutive && cursor == 0) {
        while (cursor < whole.size && whole[cursor] == token)
            ++cursor;
        if (cursor == whole.size) return k_nullopt;
    }

    auto const pos = Find(whole, token, cursor);
    String result;

    if (!pos) {
        result = whole.SubSpan(cursor);
        cursor = whole.size;
    } else {
        result = whole.SubSpan(cursor, *pos - cursor);
        cursor = *pos + 1;

        if (skip_consecutive)
            while (cursor < whole.size && whole[cursor] == token)
                ++cursor;
    }

    return result;
}

struct SplitIterator {
    String whole;
    char token;
    bool skip_consecutive;
    usize cursor {};

    struct Iterator {
        SplitIterator* self;
        Optional<String> value;

        String operator*() const {
            ASSERT(value);
            return *value;
        }

        Iterator& operator++() {
            value = SplitWithIterator(self->whole, self->cursor, self->token, self->skip_consecutive);
            return *this;
        }

        bool operator!=(Iterator const& other) const { return value != other.value; }
    };

    Iterator begin() { return {this, SplitWithIterator(whole, cursor, token, skip_consecutive)}; }
    Iterator end() { return {this, k_nullopt}; }
};

PUBLIC constexpr DynamicArray<String> Split(String str, char token, Allocator& allocator) {
    DynamicArray<String> result {allocator};
    usize cursor {};
    while (auto part = SplitWithIterator(str, cursor, token))
        dyn::Append(result, *part);
    return result;
}

template <usize k_size>
PUBLIC constexpr DynamicArrayBounded<String, k_size> SplitBounded(String str, char token) {
    DynamicArrayBounded<String, k_size> result {};
    usize cursor {};
    while (auto part = SplitWithIterator(str, cursor, token)) {
        if (result.size == k_size) break;
        dyn::Append(result, *part);
    }
    return result;
}

// Supports * and ? wildcards
// https://research.swtch.com/glob
PUBLIC constexpr bool MatchWildcard(String wildcard, String haystack) {
    usize px = 0;
    usize nx = 0;
    usize next_px = 0;
    usize next_nx = 0;
    while (px < wildcard.size || nx < haystack.size) {
        if (px < wildcard.size) {
            auto c = wildcard[px];
            switch (c) {
                case '?': {
                    if (nx < haystack.size) {
                        px++;
                        nx++;
                        continue;
                    }
                    break;
                }
                case '*': {
                    next_px = px;
                    next_nx = nx + 1;
                    px++;
                    continue;
                }
                default: {
                    if (nx < haystack.size && haystack[nx] == c) {
                        px++;
                        nx++;
                        continue;
                    }
                    break;
                }
            }
        }
        if (0 < next_nx && next_nx <= haystack.size) {
            px = next_px;
            nx = next_nx;
            continue;
        }
        return false;
    }

    return true;
}

template <usize k_size>
PUBLIC void CopyStringIntoBufferWithNullTerm(char (&destination)[k_size], String source) {
    auto const size = Min(k_size - 1, source.size);
    CopyMemory(destination, source.data, size);
    destination[size] = '\0';
}

template <usize k_size>
PUBLIC void CopyStringIntoBufferWithNullTerm(Array<char, k_size>& buffer, String source) {
    CopyStringIntoBufferWithNullTerm(buffer.data, source);
}

PUBLIC constexpr void CopyStringIntoBufferWithNullTerm(char* buffer, usize buffer_size, String source) {
    if (!buffer_size) return;

    auto const size = Min(buffer_size - 1, source.size);
    for (auto const i : Range(size))
        buffer[i] = source[i];
    buffer[size] = 0;
}

PUBLIC constexpr const char* IncrementUTF8Characters(void const* str, int num_to_inc) {
    auto s = (u8 const*)str;
    int num_codepoints = 0;
    while (*s && num_codepoints < num_to_inc) {
        if ((*s & 0b11111000) == 0b11110000)
            s += 4;
        else if ((*s & 0b11110000) == 0b11100000)
            s += 3;
        else if ((*s & 0b11100000) == 0b11000000)
            s += 2;
        else
            s += 1;

        num_codepoints++;
    }
    return (char const*)s;
}

PUBLIC constexpr bool NullTermStringsEqual(char const* a, char const* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

PUBLIC constexpr bool NullTermStringStartsWith(char const* str, char const* prefix) {
    while (*prefix) {
        if (*str != *prefix) return false;
        ++str;
        ++prefix;
    }
    return true;
}

template <typename Type>
requires(CharacterType<Type>)
PUBLIC constexpr Span<Type> FromNullTerminated(Type* null_term_data) {
    usize size = 0;
    auto ptr = null_term_data;
    while (*ptr++)
        ++size;

    return Span {null_term_data, size};
}

PUBLIC constexpr usize NullTerminatedSize(char const* str) {
    usize size = 0;
    while (*str++)
        ++size;
    return size;
}

PUBLIC char* NullTerminated(String str, ArenaAllocator& a) {
    auto result = a.AllocateExactSizeUninitialised<char>(str.size + 1);
    CopyMemory(result.data, str.data, str.size);
    result[str.size] = '\0';
    return result.data;
}

PUBLIC wchar_t* NullTerminated(WString str, ArenaAllocator& a) {
    auto result = a.AllocateExactSizeUninitialised<wchar_t>(str.size + 1);
    CopyMemory(result.data, str.data, str.size * sizeof(wchar_t));
    result[str.size] = L'\0';
    return result.data;
}

#define ANSI_COLOUR_SET_FOREGROUND_RED     "\x1b[31m"
#define ANSI_COLOUR_SET_FOREGROUND_GREEN   "\x1b[32m"
#define ANSI_COLOUR_SET_FOREGROUND_YELLOW  "\x1b[33m"
#define ANSI_COLOUR_SET_FOREGROUND_BLUE    "\x1b[34m"
#define ANSI_COLOUR_RESET                  "\x1b[0m"
#define ANSI_COLOUR_FOREGROUND_RED(str)    ANSI_COLOUR_SET_FOREGROUND_RED str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_GREEN(str)  ANSI_COLOUR_SET_FOREGROUND_GREEN str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_YELLOW(str) ANSI_COLOUR_SET_FOREGROUND_YELLOW str ANSI_COLOUR_RESET
#define ANSI_COLOUR_FOREGROUND_BLUE(str)   ANSI_COLOUR_SET_FOREGROUND_BLUE str ANSI_COLOUR_RESET

PUBLIC constexpr char ToUppercaseAscii(char c) {
    if (c >= 'a' && c <= 'z') return c + ('A' - 'a');
    return c;
}

PUBLIC constexpr char ToLowercaseAscii(char c) {
    if (c >= 'A' && c <= 'Z') return c - ('A' - 'a');
    return c;
}

PUBLIC constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }
PUBLIC constexpr bool IsHexDigit(char c) {
    return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
PUBLIC constexpr bool IsLowercaseAscii(char c) { return c >= 'a' && c <= 'z'; }
PUBLIC constexpr bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
PUBLIC constexpr bool IsAlphanum(char c) { return IsAlpha(c) || IsDigit(c); }
PUBLIC constexpr bool IsEndOfLine(char c) { return c == '\n' || c == '\r'; }
PUBLIC constexpr bool IsSpacing(char c) { return c == ' ' || c == '\t'; }
PUBLIC constexpr bool IsWhitespace(char c) { return IsSpacing(c) || IsEndOfLine(c); }
PUBLIC constexpr bool IsPrintableAscii(char c) { return c >= 32 && c <= 126; }
PUBLIC constexpr bool IsUppercaseAscii(char c) { return c >= 'A' && c <= 'Z'; }
PUBLIC constexpr bool IsPrintableAscii(String str) {
    for (auto const c : str)
        if (!IsPrintableAscii(c)) return false;
    return true;
}

// https://en.wikipedia.org/wiki/Whitespace_character
PUBLIC constexpr bool IsSpaceU32(u32 c) {
    switch (c) {
        case 9: return true;
        case 10: return true;
        case 11: return true;
        case 12: return true;
        case 13: return true;
        case 32: return true;
        case 133: return true;
        case 160: return true;
        case 5760: return true;
        case 8192: return true;
        case 8193: return true;
        case 8194: return true;
        case 8195: return true;
        case 8196: return true;
        case 8197: return true;
        case 8198: return true;
        case 8199: return true;
        case 8200: return true;
        case 8201: return true;
        case 8202: return true;
        case 8232: return true;
        case 8233: return true;
        case 8239: return true;
    }
    return false;
}

enum class ParseIntBase { Decimal, Hexadecimal };

PUBLIC constexpr Optional<s64>
ParseInt(String str, ParseIntBase base, usize* num_chars_read = nullptr, bool trim_whitespace = true) {
    auto const end = str.data + str.size;
    auto pos = str.data;

    if (trim_whitespace)
        while (pos != end && IsWhitespace(*pos))
            ++pos;

    if (pos == end) return k_nullopt;

    s64 result = 0;
    bool is_negative = false;
    if (str[0] == '-') {
        is_negative = true;
        ++pos;
    } else if (str[0] == '+') {
        ++pos;
    }
    bool has_digits = false;
    switch (base) {
        case ParseIntBase::Decimal: {
            while (pos != end) {
                if (!IsDigit(*pos)) break;
                result = result * 10 + (*pos - '0');
                ++pos;
                has_digits = true;
            }
            break;
        }
        case ParseIntBase::Hexadecimal: {
            while (pos != end) {
                if (!IsHexDigit(*pos)) break;
                if (IsDigit(*pos))
                    result = result * 16 + (*pos - '0');
                else
                    result = result * 16 + (ToLowercaseAscii(*pos) - 'a' + 10);
                ++pos;
                has_digits = true;
            }
            break;
        }
    }
    if (!has_digits) return k_nullopt;
    if (num_chars_read) *num_chars_read = (usize)(pos - str.data);
    return is_negative ? -result : result;
}

PUBLIC constexpr Optional<s64>
ParseIntTrimString(String& str, ParseIntBase base, bool trim_whitespace = true) {
    usize num_chars_read = 0;
    auto result = ParseInt(str, base, &num_chars_read, trim_whitespace);
    str.RemovePrefix(num_chars_read);
    return result;
}

Optional<double> ParseFloat(String str, usize* num_chars_read = nullptr);

PUBLIC constexpr bool ContainsCaseInsensitiveAscii(String str, String other) {
    if (other.size == 0) return true;
    if (other.size > str.size) return false;

    for (auto const i : Range(str.size - other.size + 1)) {
        bool matched = true;
        for (auto const j : Range(other.size)) {
            if (ToUppercaseAscii(str[i + j]) != ToUppercaseAscii(other[j])) {
                matched = false;
                break;
            }
        }

        if (matched) return true;
    }

    return false;
}

PUBLIC constexpr bool IsEqualToCaseInsensitiveAscii(String str, String other) {
    if (str.size != other.size) return false;
    for (auto const i : Range(str.size))
        if (ToUppercaseAscii(str[i]) != ToUppercaseAscii(other[i])) return false;
    return true;
}

// Same as strcmp
PUBLIC constexpr int CompareAscii(String str, String other) {
    auto const lhs_sz = str.size;
    auto const rhs_sz = other.size;
    auto n = Min(lhs_sz, rhs_sz);
    auto s1 = str.data;
    auto s2 = other.data;
    while (n && *s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        if (lhs_sz < rhs_sz) return -1;
        if (lhs_sz > rhs_sz) return 1;
        return 0;
    }

    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

// Same as strcasecmp
PUBLIC constexpr int CompareCaseInsensitiveAscii(String str, String other) {
    auto const lhs_size = str.size;
    auto const rhs_size = other.size;

    auto n = Min(lhs_size, rhs_size);
    auto s1 = str.data;
    auto s2 = other.data;
    while (n && *s1 && (ToUppercaseAscii(*s1) == ToUppercaseAscii(*s2))) {
        ++s1;
        ++s2;
        --n;
    }
    if (n == 0) {
        if (lhs_size < rhs_size) return -1;
        if (lhs_size > rhs_size) return 1;
        return 0;
    }

    return (*(unsigned char*)s1 - *(unsigned char*)s2);
}

PUBLIC constexpr bool StartsWithCaseInsensitiveAscii(String str, String other) {
    if (other.size > str.size) return false;
    return IsEqualToCaseInsensitiveAscii(str.SubSpan(0, other.size), other);
}

PUBLIC constexpr bool EndsWithCaseInsensitiveAscii(String str, String other) {
    if (other.size > str.size) return false;
    return IsEqualToCaseInsensitiveAscii(str.SubSpan(str.size - other.size), other);
}

PUBLIC constexpr usize CountWhitespaceAtEnd(String str) {
    usize n = 0;
    for (usize i = str.size - 1; i != usize(-1); --i)
        if (IsWhitespace(str[i]))
            ++n;
        else
            break;
    return n;
}

PUBLIC constexpr usize CountWhitespaceAtStart(String str) {
    usize n = 0;
    for (auto const i : Range(str.size))
        if (IsWhitespace(str[i]))
            ++n;
        else
            break;
    return n;
}

template <typename T>
concept StringConstOrNot = Same<T, String> || Same<T, MutableString>;

[[nodiscard]] PUBLIC constexpr auto WhitespaceStrippedStart(StringConstOrNot auto str) {
    return str.SubSpan(CountWhitespaceAtStart(str));
}

[[nodiscard]] PUBLIC constexpr auto WhitespaceStrippedEnd(StringConstOrNot auto str) {
    return str.SubSpan(0, str.size - CountWhitespaceAtEnd(str));
}

[[nodiscard]] PUBLIC constexpr auto WhitespaceStripped(StringConstOrNot auto str) {
    auto const offset = CountWhitespaceAtStart(str);
    return str.SubSpan(offset, str.size - (CountWhitespaceAtEnd(str) + offset));
}

[[nodiscard]] PUBLIC constexpr auto TrimStartIfMatches(StringConstOrNot auto str, String possible_prefix) {
    if (StartsWithSpan(str, possible_prefix)) return str.SubSpan(possible_prefix.size);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimEndIfMatches(StringConstOrNot auto str, String possible_suffix) {
    if (EndsWithSpan(str, possible_suffix)) return str.SubSpan(0, str.size - possible_suffix.size);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimStartIfMatches(StringConstOrNot auto str, char possible_prefix) {
    if (str.size == 0) return str;
    if (str[0] == possible_prefix) return str.SubSpan(1);
    return str;
}

[[nodiscard]] PUBLIC constexpr auto TrimEndIfMatches(StringConstOrNot auto str, char possible_suffix) {
    if (str.size == 0) return str;
    auto const last_index = str.size - 1;
    if (str[last_index] == possible_suffix) return str.SubSpan(0, last_index);
    return str;
}

// https://blog.rink.nu/2023/02/12/behind-the-magic-of-magic_enum/
template <auto e>
requires(EnumWithCount<decltype(e)>)
consteval auto StringifyEnumValue() {
    constexpr auto k_raw = String(__PRETTY_FUNCTION__);
    constexpr auto k_str = [&]() {
        auto offset = FindLast(k_raw, ':').ValueOr(0) + 1;
        auto end = Find(k_raw, ']', offset).ValueOr(k_raw.size);
        return k_raw.SubSpan(offset, end - offset);
    }();
    return k_str;
}

template <typename E, usize... I>
consteval auto EnumStringHelper(IndexSequence<I...>) noexcept {
    return Array<String, sizeof...(I)> {StringifyEnumValue<E(I)>()...};
}

template <typename E>
constexpr auto EnumStrings() {
    return EnumStringHelper<E>(MakeIndexSequence<ToInt(E::Count)>());
}

// Only works for enums with a final value called Count and contiguous values starting from 0.
constexpr String EnumToString(Enum auto e) { return EnumStrings<decltype(e)>()[ToInt(e)]; }

PUBLIC Span<String> CombineStringArrays(Allocator& allocator, Span<String const> a, Span<String const> b) {
    auto result = allocator.AllocateExactSizeUninitialised<String>(a.size + b.size);
    usize pos = 0;
    for (auto const& part : a)
        result[pos++] = part;
    for (auto const& part : b)
        result[pos++] = part;

    return result;
}
