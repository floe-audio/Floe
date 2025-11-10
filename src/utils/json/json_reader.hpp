// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// WARNING: This API leaves a lot to be desired...
// - Be careful with lifetime of lambdas that you pass into this API. You might need to keep the functions
//   alive longer than you's expect.
// - The callback-based approach is pretty cumbersome. An iterator-based approach would be better.

namespace json {

enum class EventType {
    String,
    Double,
    Int,
    Bool,
    Null,
    ObjectStart,
    ObjectEnd,
    ArrayStart,
    ArrayEnd,

    // Special types that signify when a handler callback first received focus, and when it's about to loose
    // focus
    HandlingStarted,
    HandlingEnded,
};

struct Event {
    String key {};
    EventType type {};
    union {
        String string {};
        f64 real;
        s64 integer;
        bool boolean;
    };
};

struct ReaderSettings {
    bool allow_comments = false;
    bool allow_trailing_commas = false;
    bool allow_keys_without_quotes = false;
};

struct JsonParseError {
    String message;
};

class EventHandler;
using EventHandlerStack = DynamicArray<EventHandler>;
using EventCallbackRef = TrivialFunctionRef<bool(EventHandlerStack& handler_stack, Event const& event)>;

using EventCallback = TrivialAllocatedFunction<bool(EventHandlerStack& handler_stack, Event const& event)>;

template <usize k_size>
using EventCallbackFixedSize =
    TrivialFixedSizeFunction<k_size, bool(EventHandlerStack& handler_stack, Event const& event)>;

class EventHandler {
  public:
    EventHandler(EventCallbackRef callback, Allocator& a) : m_handle_event_callback(callback, a) {
        ASSERT(callback);
    }

    void HandleEvent(EventHandlerStack& handler_stack, Event const& event) {
        ASSERT(m_nesting != 0 || (m_nesting == 0 && (event.type == EventType::ArrayStart ||
                                                     event.type == EventType::ObjectStart)));

        if (event.type == EventType::ArrayStart || event.type == EventType::ObjectStart) {
            if (m_nesting++ == 0) {
                m_handle_event_callback(handler_stack, {.key = "", .type = EventType::HandlingStarted});
                return;
            }
        } else if (event.type == EventType::ArrayEnd || event.type == EventType::ObjectEnd) {
            --m_nesting;
            if (m_nesting == 0) {
                m_handle_event_callback(handler_stack, {.key = "", .type = EventType::HandlingEnded});
                dyn::Pop(handler_stack);
                if (handler_stack.size) Last(handler_stack).HandleEvent(handler_stack, event);
                return;
            }
        }

        if (m_nesting < m_ignore_until_level && !m_handle_event_callback(handler_stack, event)) {
            if (m_nesting != 1 &&
                (event.type == EventType::ArrayStart || event.type == EventType::ObjectStart)) {
                m_ignore_until_level = m_nesting - 1;
            }
        } else if (m_nesting == m_ignore_until_level) {
            m_ignore_until_level = LargestRepresentableValue<int>();
        }
    }

  private:
    int m_nesting = 0;
    int m_ignore_until_level = LargestRepresentableValue<int>();
    EventCallback m_handle_event_callback;
};

namespace detail {

static inline char const* SkipWhitespace(char const* p, char const* end) {
    while (p != end && IsWhitespace(*p))
        ++p;
    return p;
}

enum class TokenType {
    Invalid,

    Colon,
    OpenBracket,
    CloseBracket,
    OpenBrace,
    CloseBrace,
    Comma,

    String,
    Integer,
    Double,
    True,
    False,
    Null,

    Spacing,
    EndOfLine,
    Comment,

    EndOfStream,
};

struct Token {
    TokenType type;
    union {
        String text {};
        f64 real;
        s64 integer;
        bool boolean;
    };
};

struct Tokeniser {
    char const* at;
    char const* end;
    ReaderSettings* settings;
    ArenaAllocator& scratch_arena;
};

static void ConsumeEndOfLine(Tokeniser* tokeniser, char c) {
    // count \r\n and \n\r as a single end of line
    char const cp = *tokeniser->at;
    auto next = tokeniser->at + 1;
    if (next < tokeniser->end && ((c == '\r' && cp == '\n') || (c == '\n' && cp == '\r')))
        tokeniser->at = next;
}

static ValueOrError<Token, JsonParseError> GetToken(Tokeniser* tokeniser) {
    auto& at = tokeniser->at;
    auto end = tokeniser->end;
    if (at >= end) {
        Token result = {};
        result.type = TokenType::EndOfStream;
        return result;
    }

    auto start = at;

    auto c = *tokeniser->at;
    auto next = tokeniser->at + 1;
    at = next;

    Token token = {};

    switch (c) {
        case ':': {
            token.type = TokenType::Colon;
            break;
        }
        case ',': {
            token.type = TokenType::Comma;
            break;
        }
        case '[': {
            token.type = TokenType::OpenBracket;
            break;
        }
        case ']': {
            token.type = TokenType::CloseBracket;
            break;
        }
        case '{': {
            token.type = TokenType::OpenBrace;
            break;
        }
        case '}': {
            token.type = TokenType::CloseBrace;
            break;
        }

        case '"': {
            token.type = TokenType::String;

            bool escape = false;
            while (at < end) {
                char const cp = *at;
                ++at;
                if (cp == '\\' && !escape) {
                    escape = true;
                } else {
                    if (!escape && cp == '\"') break;
                    escape = false;
                }
            }

            if (at >= end) return JsonParseError {"Expected quote at end of string"};

            break;
        }

        default: {
            if (IsWhitespace(c)) {
                token.type = TokenType::Spacing;
                at = SkipWhitespace(at, end);
            } else if (tokeniser->settings->allow_comments && c == '/') {
                bool comment_consumed = false;

                auto cp = *at;
                auto next_cp = at + 1;
                if (next_cp < end) {
                    if (cp == '/') {
                        at = next_cp;
                        while (at < end) {
                            cp = *at;
                            next_cp = at + 1;
                            if (IsEndOfLine(cp)) {
                                comment_consumed = true;
                                break;
                            } else {
                                at = next_cp;
                            }
                        }
                    } else if (cp == '*') {
                        at = next_cp;
                        while (at < end) {
                            cp = *at;
                            at = at + 1;
                            if (IsEndOfLine(cp)) {
                                ConsumeEndOfLine(tokeniser, cp);
                            } else if (cp == '*') {
                                cp = *at;
                                next_cp = at + 1;
                                if (cp == '/') {
                                    at = next_cp;
                                    comment_consumed = true;
                                    break;
                                }
                            }
                        }
                    } else {
                        return JsonParseError {"Unexpected character"};
                    }
                }

                token.type = TokenType::Comment;
                if (!comment_consumed) return JsonParseError {"No end of comment"};
            } else if (IsAlpha(c)) {
                while (at < end) {
                    char const cp = *at;
                    auto next_cp = at + 1;
                    if (IsAlpha(cp) || IsDigit(cp) || (cp == '_'))
                        at = next_cp;
                    else
                        break;
                }

                auto const str = String {start, (usize)(at - start)};
                if (str == "true"_s)
                    token.type = TokenType::True;
                else if (str == "false"_s)
                    token.type = TokenType::False;
                else if (str == "null"_s)
                    token.type = TokenType::Null;
                else if (!tokeniser->settings->allow_keys_without_quotes)
                    return JsonParseError {"Unknown alphanumeric value"};
                else
                    token.type = TokenType::String;
            } else if (IsDigit(c) || c == '-') {
                while (at < end) {
                    constexpr String k_allowed_chars = "0123456789.eE-+";
                    if (Contains(k_allowed_chars, *at))
                        ++at;
                    else
                        break;
                }

                auto const number_string = String {start, (usize)(at - start)};

                bool is_real = false;
                for (auto const character : number_string) {
                    if (character == '.' || character == 'e' || character == 'E') {
                        usize num_chars_read = 0;
                        auto opt = ParseFloat(number_string, &num_chars_read);
                        if (!opt || num_chars_read != number_string.size)
                            return JsonParseError {"The number is not in a correct format"};
                        token.real = opt.Value();
                        token.type = TokenType::Double;
                        is_real = true;
                        break;
                    }
                }

                if (!is_real) {
                    usize num_chars_read = 0;
                    auto opt = ParseInt(number_string, ParseIntBase::Decimal, &num_chars_read);
                    if (!opt || num_chars_read != number_string.size)
                        return JsonParseError {"The number is not in a correct format"};
                    token.integer = opt.Value();
                    token.type = TokenType::Integer;
                }
            } else {
                return JsonParseError {"Unexpected character"};
            }
            break;
        }
    }

    if (token.type == TokenType::String) {
        ASSERT(at >= start);
        token.text = {start, (usize)(at - start)};
        if (token.text[0] == '"') {
            token.text.RemovePrefix(1); // remove the quotes
            token.text.RemoveSuffix(1);
        }

        bool contains_escape_chars = false;
        for (auto character : token.text) {
            if (character == '\\') {
                contains_escape_chars = true;
                break;
            }
        }
        if (contains_escape_chars) {
            auto data = tokeniser->scratch_arena.AllocateExactSizeUninitialised<char>(token.text.size);
            auto out = (char*)data.data;

            auto str_end = End(token.text);
            bool invalid_escape_chars = false;
            for (auto it = Begin(token.text); it != str_end && !invalid_escape_chars;) {
                if (*it == '\\') {
                    ++it;
                    ASSERT(it != str_end);

                    constexpr String k_valid_escapes = "\"\\/bfnrtu";
                    if (!Find(k_valid_escapes, *it)) {
                        invalid_escape_chars = true;
                        break;
                    }
                    if (*it != 'u') {
                        switch (*it) {
                            case 'b': *out++ = '\b'; break;
                            case 'f': *out++ = '\f'; break;
                            case 'n': *out++ = '\n'; break;
                            case 'r': *out++ = '\r'; break;
                            case 't': *out++ = '\t'; break;
                            default: *out++ = *it;
                        }
                        ++it;
                    } else {
                        ++it;
                        auto sub_it = it;
                        for (auto _ : Range(4)) {
                            if (sub_it == str_end || !IsHexDigit(*sub_it)) {
                                invalid_escape_chars = true;
                                break;
                            }
                            ++sub_it;
                        }
                        char buffer[4] = {*it, *(it + 1), *(it + 2), *(it + 3)};
                        auto const codepoint =
                            ParseInt({buffer, sizeof(buffer)}, ParseIntBase::Hexadecimal).ValueOr(1);

                        if (codepoint < 0x80) {
                            *out++ = (char)codepoint;
                        } else if (codepoint < 0x800) {
                            *out++ = (char)((codepoint >> 6) | 0xC0);
                            *out++ = (char)((codepoint & 0x3F) | 0x80);
                        } else if (codepoint < 0x10000) {
                            *out++ = (char)((codepoint >> 12) | 0xE0);
                            *out++ = (char)(((codepoint >> 6) & 0x3F) | 0x80);
                            *out++ = (char)((codepoint & 0x3F) | 0x80);
                        } else if (codepoint < 0x110000) {
                            *out++ = (char)((codepoint >> 18) | 0xF0);
                            *out++ = (char)(((codepoint >> 12) & 0x3F) | 0x80);
                            *out++ = (char)(((codepoint >> 6) & 0x3F) | 0x80);
                            *out++ = (char)((codepoint & 0x3F) | 0x80);
                        }

                        it += 4;
                    }
                } else {
                    *out++ = *it;
                    ++it;
                }
            }

            if (invalid_escape_chars) return JsonParseError {"Invalid escape characters"};

            token.text = {(char const*)data.data, (usize)(out - (char*)data.data)};
        }
    }

    return token;
}

static ValueOrError<Token, JsonParseError> GetUsefulToken(Tokeniser* tokeniser) {
    Token token;
    do {
        auto const next_token = TRY(GetToken(tokeniser));
        token = next_token;
    } while (token.type == TokenType::Spacing || token.type == TokenType::EndOfLine ||
             token.type == TokenType::Comment);
    return token;
}

constexpr usize k_initial_stack_size = 10;

class EventHandlerContext {
  public:
    EventHandlerContext(ArenaAllocator& temp_allocator, EventCallbackRef root_callback)
        : m_handler_stack(temp_allocator) {
        m_handler_stack.Reserve(k_initial_stack_size);
        PushHandler(root_callback);
    }
    ~EventHandlerContext() {}

    bool HandleEvent(Event const& event) {
        CurrentHandler().HandleEvent(m_handler_stack, event);
        return true;
    }

    void PushHandler(EventCallbackRef callback) {
        dyn::Emplace(m_handler_stack, callback, m_handler_stack.allocator);
    }
    void PopHandler() { dyn::Pop(m_handler_stack); }
    EventHandler& CurrentHandler() { return Last(m_handler_stack); }

  private:
    EventHandlerStack m_handler_stack;
};

} // namespace detail

PUBLIC VoidOrError<JsonParseError>
Parse(String str, EventCallbackRef event_callback, ArenaAllocator& scratch_arena, ReaderSettings settings) {
    using namespace detail;

    enum class ContainerType : u8 { Object, Array };
    enum ExpectedType : u8 {
        TypeKeyOrCloseBrace = 1,
        TypeCommaOrCloseBrace = 2,
        TypeCommaOrCloseBracket = 4,
        TypeColon = 8,
        TypeValue = 16,
        TypeValueOrCloseBracket = 32,
        TypeContainer = 64,

        TypeAnyCloseBrace = TypeCommaOrCloseBrace | TypeKeyOrCloseBrace,
        TypeAnyCloseBracket = TypeCommaOrCloseBracket | TypeValueOrCloseBracket,
        TypeAnyValue = TypeValue | TypeValueOrCloseBracket,
        TypeAnyComma = TypeCommaOrCloseBrace | TypeCommaOrCloseBracket,
    };
    struct Frame {
        ContainerType container;
        u8 expected;
    };

    Tokeniser tokeniser {.scratch_arena = scratch_arena};
    tokeniser.at = str.data;
    tokeniser.end = str.data + str.size;
    tokeniser.settings = &settings;
    EventHandlerContext event_handler_context {scratch_arena, Move(event_callback)};

    DynamicArray<Frame> stack(scratch_arena);
    stack.Reserve(sizeof(Frame) * k_initial_stack_size);

    String key = {};
    TokenType prev_token_type = {};

    while (true) {
        auto const rhs = TRY(GetUsefulToken(&tokeniser));

        auto* frame = stack.size ? &Last(stack) : nullptr;
        auto expected = frame ? frame->expected : TypeContainer;

        switch (rhs.type) {
            case TokenType::EndOfStream: {
                if (stack.size) return JsonParseError {"Unexpected end of file"};
                return k_success;
            }
            case TokenType::Comma: {
                if (!(expected & TypeAnyComma)) return JsonParseError {"Unexpected comma"};
                frame->expected = (frame->container == ContainerType::Array) ? TypeValueOrCloseBracket
                                                                             : TypeKeyOrCloseBrace;
                key = {};
                break;
            }
            case TokenType::Colon: {
                if (expected != TypeColon) return JsonParseError {"Unexpected colon"};
                frame->expected = TypeValue;
                break;
            }

            case TokenType::String: {
                if (expected == TypeKeyOrCloseBrace) {
                    frame->expected = (TypeColon);
                    key = rhs.text;
                } else if (expected & TypeAnyValue) {
                    event_handler_context.HandleEvent(
                        Event {.key = key, .type = EventType::String, .string = rhs.text});
                    frame->expected = (frame->container == ContainerType::Array) ? TypeCommaOrCloseBracket
                                                                                 : TypeCommaOrCloseBrace;
                } else {
                    return JsonParseError {"Unexpected string"};
                }
                break;
            }
            case TokenType::Integer:
            case TokenType::Double:
            case TokenType::True:
            case TokenType::False:
            case TokenType::Null: {
                if (!(expected & TypeAnyValue)) return JsonParseError {"Unexpected value"};
                frame->expected = (frame->container == ContainerType::Array) ? TypeCommaOrCloseBracket
                                                                             : TypeCommaOrCloseBrace;
                switch (rhs.type) {
                    case TokenType::Integer: {
                        event_handler_context.HandleEvent(
                            Event {.key = key, .type = EventType::Int, .integer = rhs.integer});
                        break;
                    }
                    case TokenType::Double: {
                        event_handler_context.HandleEvent(
                            Event {.key = key, .type = EventType::Double, .real = rhs.real});
                        break;
                    }
                    case TokenType::True: {
                        event_handler_context.HandleEvent(
                            Event {.key = key, .type = EventType::Bool, .boolean = true});
                        break;
                    }
                    case TokenType::False: {
                        event_handler_context.HandleEvent(
                            Event {.key = key, .type = EventType::Bool, .boolean = false});
                        break;
                    }
                    case TokenType::Null: {
                        event_handler_context.HandleEvent(Event {.key = key, .type = EventType::Null});
                        break;
                    }
                    default: break;
                }
                break;
            }
            case TokenType::OpenBrace: {
                if (!(expected & (TypeAnyValue | TypeContainer)))
                    return JsonParseError {"Unexpected open brace"};

                dyn::Append(stack, Frame {ContainerType::Object, TypeKeyOrCloseBrace});
                event_handler_context.HandleEvent(Event {.key = key, .type = EventType::ObjectStart});
                key = {};
                break;
            }
            case TokenType::CloseBrace: {
                if (!(expected & TypeAnyCloseBrace))
                    return JsonParseError {"Unexpected close brace"};
                else if (prev_token_type == TokenType::Comma && !settings.allow_trailing_commas)
                    return JsonParseError {"Trailing commas are not allowed"};
                event_handler_context.HandleEvent(Event {.key = {}, .type = EventType::ObjectEnd});
                dyn::Pop(stack);
                if (stack.size) {
                    Last(stack).expected = (Last(stack).container == ContainerType::Array)
                                               ? TypeCommaOrCloseBracket
                                               : TypeCommaOrCloseBrace;
                }
                key = {};
                break;
            }
            case TokenType::OpenBracket: {
                if (!(expected & (TypeAnyValue | TypeContainer)))
                    return JsonParseError {"Unexpected open bracket"};

                dyn::Append(stack, Frame {ContainerType::Array, TypeValueOrCloseBracket});
                event_handler_context.HandleEvent(Event {.key = key, .type = EventType::ArrayStart});
                key = {};
                break;
            }
            case TokenType::CloseBracket: {
                if (!(expected & TypeAnyCloseBracket))
                    return JsonParseError {"Unexpected close bracket"};
                else if (prev_token_type == TokenType::Comma && !settings.allow_trailing_commas)
                    return JsonParseError {"Trailing commas are not allowed"};
                event_handler_context.HandleEvent(Event {.key = {}, .type = EventType::ArrayEnd});
                dyn::Pop(stack);
                if (stack.size) {
                    Last(stack).expected = (Last(stack).container == ContainerType::Array)
                                               ? TypeCommaOrCloseBracket
                                               : TypeCommaOrCloseBrace;
                }
                key = {};
                break;
            }
            default: {
                return JsonParseError {"Unexpected token"};
            }
        }

        prev_token_type = rhs.type;
    }
    PanicIfReached();

    return k_success;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, bool& result) {
    if (event.type == EventType::Bool && event.key == expected_key) {
        result = event.boolean;
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, Integral auto& result) {
    using Type = RemoveReference<decltype(result)>;
    if (event.type == EventType::Int && event.key == expected_key) {
        if (NumberCastIsSafe<Type>(event.integer)) {
            result = (Type)event.integer;
            return true;
        }
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, DynamicArray<char>& result) {
    if (event.type == EventType::String && event.key == expected_key) {
        dyn::Assign(result, event.string);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatchingRef(Event const& event, String expected_key, String& result) {
    if (event.type == EventType::String && event.key == expected_key) {
        result = event.string;
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, String& result, Allocator& a) {
    if (event.type == EventType::String && event.key == expected_key) {
        result = a.Clone(event.string);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, MutableString& result, Allocator& a) {
    if (event.type == EventType::String && event.key == expected_key) {
        result = a.Clone(event.string);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, FloatingPoint auto& result) {
    if (event.type == EventType::Double && event.key == expected_key) {
        result = (RemoveReference<decltype(result)>)event.real;
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatching(Event const& event, String expected_key, Optional<Version>& result) {
    if (event.type == EventType::String && event.key == expected_key) {
        result = ParseVersionString(event.string);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatchingContainer(EventType type,
                                   EventHandlerStack& handler_stack,
                                   Event const& event,
                                   String expected_key,
                                   EventCallbackRef callback) {
    ASSERT(type == EventType::ArrayStart || type == EventType::ObjectStart);
    if (event.type == type && event.key == expected_key) {
        dyn::Emplace(handler_stack, callback, handler_stack.allocator);
        Last(handler_stack).HandleEvent(handler_stack, event);
        return true;
    }
    return false;
}

PUBLIC bool SetIfMatchingObject(EventHandlerStack& handler_stack,
                                Event const& event,
                                String expected_key,
                                EventCallbackRef callback) {
    return SetIfMatchingContainer(EventType::ObjectStart, handler_stack, event, expected_key, callback);
}

PUBLIC bool SetIfMatchingArray(EventHandlerStack& handler_stack,
                               Event const& event,
                               String expected_key,
                               EventCallbackRef callback) {
    return SetIfMatchingContainer(EventType::ArrayStart, handler_stack, event, expected_key, callback);
}

PUBLIC bool SetIfMatchingArray(EventHandlerStack& handler_stack,
                               Event const& event,
                               String expected_key,
                               DynamicArray<DynamicArray<char>>& string_array,
                               Allocator& string_allocator) {
    return SetIfMatchingArray(handler_stack,
                              event,
                              expected_key,
                              [&string_array, &string_allocator](EventHandlerStack&, Event const& event) {
                                  if (event.type == EventType::String) {
                                      dyn::Append(string_array,
                                                  DynamicArray<char> {event.string, string_allocator});
                                      return true;
                                  }
                                  return false;
                              });
}

template <typename Type>
PUBLIC bool SetIfMatchingArray(EventHandlerStack& handler_stack,
                               Event const& event,
                               String expected_key,
                               DynamicArray<Type>& array) {
    return SetIfMatchingArray(handler_stack,
                              event,
                              expected_key,
                              {[&array](EventHandlerStack&, Event const& event) {
                                   if constexpr (Integral<Type> || __is_enum(Type)) {
                                       if (event.type == EventType::Int) {
                                           array.Append((Type)event.integer);
                                           return true;
                                       }
                                   } else if constexpr (FloatingPoint<Decay<Type>>) {
                                       if (event.type == EventType::Double) {
                                           array.Append((Type)event.real);
                                           return true;
                                       }
                                   } else if constexpr (true) {
                                       throw "todo";
                                   }
                                   return false;
                               },
                               handler_stack.allocator});
}

template <typename Type>
PUBLIC bool SetIfMatching(Event const& event, String expected_key, Optional<Type>& result) {
    Type v;
    if (SetIfMatching(event, expected_key, v)) {
        result = v;
        return true;
    }
    return false;
}

template <typename Type>
PUBLIC bool SetIfMatchingRef(Event const& event, String expected_key, Optional<Type>& result) {
    Type v;
    if (SetIfMatchingRef(event, expected_key, v)) {
        result = v;
        return true;
    }
    return false;
}

} // namespace json
