// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

struct SuccessType {
    enum class Construct { Token };
    explicit constexpr SuccessType(Construct) {}
};
inline constexpr SuccessType k_success {SuccessType::Construct::Token};

#define TRY_HELPER_INHERIT(method, parent)                                                                   \
    static inline auto method(auto& o) { return parent::method(o); }

// This is the standard way of inquiring the state of a return union. You can create a alternate versions of
// this struct and then use it with TRY_X or TRY_H.
struct TryHelpers {
    static auto IsError(auto const& o) { return o.HasError(); }
    static auto ExtractError(auto const& o) { return o.Error(); }
    static auto ExtractValue(auto& o) { return o.ReleaseValue(); }
};

// Uses clang 'statement expression', a non-standard extension
// NOTE: cannot be used multiple times on the same line
#define TRY_X(try_helpers, expression)                                                                       \
    ({                                                                                                       \
        auto&& CONCAT(try_result, __LINE__) = (expression);                                                  \
        if (try_helpers::IsError(CONCAT(try_result, __LINE__))) [[unlikely]] {                               \
            return try_helpers::ExtractError(CONCAT(try_result, __LINE__));                                  \
        }                                                                                                    \
        try_helpers::ExtractValue(CONCAT(try_result, __LINE__));                                             \
    })

#define TRY(expression)   TRY_X(TryHelpers, expression)
#define TRY_H(expression) TRY_X(H, expression) // you must define a custom TryHelpers with the name H

#define TRY_OR(expression, fallback_code)                                                                    \
    ({                                                                                                       \
        auto&& CONCAT(try_result, __LINE__) = (expression);                                                  \
        if (TryHelpers::IsError(CONCAT(try_result, __LINE__))) [[unlikely]] {                                \
            [[maybe_unused]] auto const error = TryHelpers::ExtractError(CONCAT(try_result, __LINE__));      \
            fallback_code;                                                                                   \
        }                                                                                                    \
        TryHelpers::ExtractValue(CONCAT(try_result, __LINE__));                                              \
    })
