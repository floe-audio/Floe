// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/cloneable.hpp"
#include "foundation/universal_defs.hpp"

struct NulloptType {
    enum class Construct { Token };
    explicit constexpr NulloptType(Construct) noexcept {}
    constexpr operator bool() const { return false; }
};

inline constexpr NulloptType k_nullopt {NulloptType::Construct::Token};

struct Allocator;

template <typename Type>
class Optional;

// This version of Optional is constexpr-ready. Unions are allowed in constexpr so long as the compiler can
// track which is the active member. Any type of reinterpret_cast is not allowed in constexpr.
template <TriviallyCopyable Type>
class [[nodiscard]] Optional<Type> {
  public:
    using ValueType = Type;

    constexpr Optional() : no_value {}, has_value {false} {}
    constexpr Optional(NulloptType) : no_value {}, has_value {} {}
    constexpr Optional(Type const& v) : value {v}, has_value {true} {}
    constexpr ~Optional() = default;

    constexpr Optional(Optional&& other) = default;
    constexpr Optional& operator=(Optional&& other) = default;

    constexpr Optional(Optional const& other) requires(TriviallyCopyAssignable<Type>)
    = default;
    constexpr Optional& operator=(Optional const& other) requires(TriviallyCopyAssignable<Type>)
    = default;

    constexpr Optional(Optional const& other) requires(!TriviallyCopyAssignable<Type>)
        : has_value {other.has_value} {
        if (other.has_value) PLACEMENT_NEW(&value) Type(other.value);
    }

    constexpr Optional& operator=(Optional const& other) requires(!TriviallyCopyAssignable<Type>)
    {
        if (this != &other) {
            has_value = other.has_value;
            if (other.has_value) PLACEMENT_NEW(&value) Type(other.value);
        }
        return *this;
    }

    template <typename U = Type>
    requires(!Same<RemoveCVReference<U>, Optional<Type>> && ConstructibleWithArgs<Type, U &&>)
    constexpr Optional(U&& value) : value {Forward<U>(value)}
                                  , has_value {true} {}

    constexpr bool operator==(Optional const& other) const {
        if (has_value && other.has_value) return value == other.value;
        return has_value == other.has_value;
    }

    constexpr bool operator==(Type const& other) const {
        if (has_value) return value == other;
        return false;
    }

    constexpr Type* NullableValue() {
        if (has_value) return &value;
        return nullptr;
    }

    constexpr Type const* NullableValue() const {
        if (has_value) return &value;
        return nullptr;
    }
    constexpr void Clear() { has_value = false; }

    constexpr bool HasValue() const { return has_value; }
    constexpr Type& Value() {
        ASSERT(has_value);
        return value;
    }
    constexpr Type const& Value() const {
        ASSERT(has_value);
        return value;
    }
    constexpr explicit operator bool() const { return has_value; }
    constexpr Type* operator->() { return &Value(); }
    constexpr Type& operator*() { return Value(); }
    constexpr Type const* operator->() const { return &Value(); }
    constexpr Type const& operator*() const { return Value(); }
    constexpr Type ValueOr(Type fallback) const {
        if (has_value) return value;
        return fallback;
    }

    constexpr Type ReleaseValue() {
        auto result = Value();
        has_value = false;
        return result;
    }
    constexpr Type ReleaseValueOr(Type fallback) {
        if (has_value) return ReleaseValue();
        return fallback;
    }

    template <class... Args>
    requires(ConstructibleWithArgs<Type, Args...>)
    constexpr void Emplace(Args&&... args) {
        if constexpr (!TriviallyCopyAssignable<Type>)
            PLACEMENT_NEW(&value) Type(Forward<Args>(args)...);
        else
            value = Type(Forward<Args>(args)...);
        has_value = true;
    }

    constexpr Type& ValueOrCreate() {
        if (!has_value) Emplace();
        return Value();
    }

    constexpr Optional Clone(Allocator& a, CloneType clone_type = CloneType::Shallow) const;

    constexpr auto Transform(auto&& f) const {
        using U = RemoveCVReference<InvokeResult<decltype(f), Type>>;
        if (HasValue())
            return Optional<U> {f(Value())};
        else
            return Optional<U> {};
    }

    constexpr auto AndThen(auto&& f) const {
        if (HasValue())
            return f(Value());
        else
            return RemoveCVReference<InvokeResult<decltype(f), Type>> {};
    }

    constexpr auto OrElse(auto&& f) const {
        if (HasValue())
            return Value();
        else
            return f();
    }

    union {
        Type value;
        u8 no_value;
    };
    bool has_value;
};

template <typename Type>
requires(!TriviallyCopyable<Type>)
class [[nodiscard]] Optional<Type> {
  public:
    using ValueType = Type;

    constexpr Optional() = default;
    constexpr Optional(NulloptType) noexcept {}

    template <typename U = Type>
    requires(!Same<RemoveCVReference<U>, Optional<Type>> && ConstructibleWithArgs<Type, U &&>)
    constexpr Optional(U&& value) {
        PLACEMENT_NEW(&m_storage) Type(Forward<U>(value));
        m_has_value = true;
    }

    constexpr Optional(Optional const& other) : m_has_value(other.m_has_value) {
        if (other.HasValue()) PLACEMENT_NEW(&m_storage) Type(other.Value());
    }

    constexpr Optional& operator=(Optional const& other) {
        if (this != &other) {
            Clear();
            m_has_value = other.m_has_value;
            if (other.HasValue()) PLACEMENT_NEW(&m_storage) Type(other.Value());
        }
        return *this;
    }

    constexpr Optional& operator=(Optional&& other) {
        if (this != &other) {
            Clear();
            m_has_value = other.m_has_value;
            if (other.HasValue()) PLACEMENT_NEW(&m_storage) Type(other.ReleaseValue());
        }
        return *this;
    }

    constexpr Optional(Optional&& other) : m_has_value(other.m_has_value) {
        if (other.m_has_value) PLACEMENT_NEW(&m_storage) Type(other.ReleaseValue());
        other.m_has_value = false;
    }

    constexpr ~Optional() { Clear(); }

    constexpr bool HasValue() const { return m_has_value; }

    constexpr Type* operator->() { return &Value(); }
    constexpr Type& operator*() { return Value(); }
    constexpr Type const* operator->() const { return &Value(); }
    constexpr Type const& operator*() const { return Value(); }

    constexpr explicit operator bool() const { return HasValue(); }

    template <class... Args>
    requires(ConstructibleWithArgs<Type, Args...>)
    constexpr void Emplace(Args&&... args) {
        Clear();
        PLACEMENT_NEW(&m_storage) Type(Forward<Args>(args)...);
        m_has_value = true;
    }

    constexpr Optional Clone(Allocator& a, CloneType clone_type = CloneType::Shallow) const;

    constexpr Type& Value() {
        ASSERT(HasValue());
        return *(Type*)m_storage;
    }
    constexpr Type const& Value() const {
        ASSERT(HasValue());
        return *(Type const*)m_storage;
    }
    constexpr Type ValueOr(Type fallback) {
        if (HasValue()) return Value();
        return fallback;
    }
    constexpr Type* NullableValue() {
        if (HasValue()) return &Value();
        return nullptr;
    }
    constexpr Type const* NullableValue() const {
        if (HasValue()) return &Value();
        return nullptr;
    }

    constexpr auto AndThen(auto&& f) const {
        if (HasValue())
            return f(Value());
        else
            return RemoveCVReference<InvokeResult<decltype(f), Type>> {};
    }

    constexpr auto Transform(auto&& f) const {
        using U = RemoveCVReference<InvokeResult<decltype(f), Type>>;
        if (HasValue())
            return Optional<U> {f(Value())};
        else
            return Optional<U> {};
    }

    constexpr auto OrElse(auto&& f) const {
        if (HasValue())
            return Value();
        else
            return f();
    }

    constexpr Type ReleaseValue() {
        ASSERT(HasValue());
        Type released_value = Move(Value());
        Value().~Type();
        m_has_value = false;
        return released_value;
    }

    constexpr Type ReleaseValueOr(Type fallback) {
        if (HasValue()) return ReleaseValue();
        return Move(fallback);
    }

    constexpr void Clear() {
        if (HasValue()) Value().~Type();
        m_has_value = false;
    }

    constexpr bool operator==(Optional const& other) const {
        if (m_has_value && other.m_has_value) return Value() == other.Value();
        return !m_has_value && !other.m_has_value;
    }

    constexpr bool operator==(Type const& other) const {
        if (m_has_value) return Value() == other;
        return false;
    }

  private:
    alignas(Type) u8 m_storage[sizeof(Type)];
    bool m_has_value = false;
};

template <SignedInt Type>
class [[nodiscard]] OptionalIndex {
  public:
    using ValueType = Type;
    PROPAGATE_TRIVIALLY_COPYABLE(OptionalIndex, Type);

    constexpr OptionalIndex() {}
    constexpr OptionalIndex(NulloptType) {}
    constexpr OptionalIndex(s32 i) : m_value(i) {}
    constexpr auto& Value() const {
        ASSERT(HasValue());
        return m_value;
    }
    constexpr auto& Value() {
        ASSERT(HasValue());
        return m_value;
    }
    constexpr auto& Raw() { return m_value; }
    constexpr bool HasValue() const { return m_value >= 0; }
    constexpr explicit operator bool() const { return HasValue(); }
    constexpr void Clear() { m_value = -1; }
    constexpr bool operator==(OptionalIndex const& other) const { return m_value == other.m_value; }
    constexpr Type* operator->() { return &Value(); }
    constexpr Type operator*() { return Value(); }
    constexpr Type const* operator->() const { return &Value(); }
    constexpr Type const& operator*() const { return Value(); }

  private:
    Type m_value {-1};
};

#define TRY_OPT_OR(expression, fallback_code)                                                                \
    ({                                                                                                       \
        auto&& CONCAT(try_result, __LINE__) = (expression);                                                  \
        if (!CONCAT(try_result, __LINE__).HasValue()) {                                                      \
            fallback_code;                                                                                   \
        }                                                                                                    \
        CONCAT(try_result, __LINE__).ReleaseValue();                                                         \
    })
