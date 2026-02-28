// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// An error code is an integer that describes what occurred to stop an operation completing as expected.
//
// It's used for errors that we should handle. For example: filesystem errors, network errors, user input
// errors. It's not to be used for errors in the code like invalid arguments: use ASSERT() or Panic() for
// that.
//
// An error code is for the code to handle, not the user to see. It might be used as part of a notification
// shown to the user, but we should always provide more context than just the error code. If we are showing an
// error to a user it should be because the user needs to know about it: what was the action taken, what can
// be done about it.
//
// This ErrorCode struct is generic way to represent error codes. Codes for various systems and APIs can be
// represented by this single struct. This allows for consistency and reuse across many systems.
//
// It works by type-erasing error code. In ErrorCode, they're always an s64. Additionally, a pointer to a
// ErrorCodeCategory is paired with the code to give context to the type-erased code. For debugging purposes
// the ErrorCode struct also includes some source-location information.

#pragma once
#include "foundation/memory/cloneable.hpp"
#include "foundation/universal_defs.hpp"

#include "try.hpp"

struct ErrorCodeCategory;

// You can associate an enum with an ErrorCategory by defining a function called ErrorCategoryForEnum() which
// takes the enum value and returns the ErrorCategory that should be associated with this type:
//    const ErrorCategory &ErrorCategoryForEnum(MyEnumType e);
// With this association we can offer a few convenience constructors/methods.
template <typename T>
concept ErrorEnumWithCategory = requires(T t) { ErrorCategoryForEnum(t); };

struct [[nodiscard]] ErrorCode {
    constexpr ErrorCode() : code(0), category(nullptr), extra_debug_info(""), source_location() {}

    constexpr ErrorCode(ErrorEnumWithCategory auto e,
                        char const* extra_debug_info = nullptr,
                        SourceLocation source_loc = SourceLocation::Current())
        : code((s64)e)
        , category(&ErrorCategoryForEnum(e))
        , extra_debug_info(extra_debug_info)
        , source_location(source_loc) {}

    constexpr ErrorCode(ErrorCodeCategory const& type,
                        s64 c,
                        char const* extra_debug_info = nullptr,
                        SourceLocation source_loc = SourceLocation::Current())
        : code(c)
        , category(&type)
        , extra_debug_info(extra_debug_info)
        , source_location(source_loc) {}

    constexpr bool operator==(ErrorEnumWithCategory auto e) const {
        return category == &ErrorCategoryForEnum(e) && code == (s64)e;
    }

    constexpr bool operator==(ErrorCode const& other) const {
        return category == other.category && code == other.code;
    }

    s64 code;
    ErrorCodeCategory const* category;
    char const* extra_debug_info;
    SourceLocation source_location;
};

// An ErrorCodeOr is a tagged union of an ErrorCode and a given type. It supports moving/destructors etc.
// Later on we might want to just make this for trivial types because that's normally what we are using and
// it's simple.
template <typename Type>
class [[nodiscard]] ErrorCodeOr {
  public:
    PROPAGATE_TRIVIALLY_COPYABLE(ErrorCodeOr, Type);

    ErrorCodeOr() : m_storage_type(StorageType::Uninit) {}
    ErrorCodeOr(ErrorCode ec) : m_storage_type(StorageType::Error) {
        PLACEMENT_NEW(&m_storage) ErrorCode(ec);
    }

    template <typename U = Type>
    explicit(!Convertible<U&&, Type>) ErrorCodeOr(U&& value)
        requires(!Same<RemoveCVReference<U>, ErrorCodeOr<Type>> && ConstructibleWithArgs<Type, U &&>)
        : m_storage_type(StorageType::Value) {
        PLACEMENT_NEW(&m_storage) Type(Forward<U>(value));
    }

    ErrorCodeOr(ErrorCodeOr const& other) : m_storage_type(other.m_storage_type) {
        if (other.m_storage_type == StorageType::Value)
            PLACEMENT_NEW(&m_storage) Type(other.Value());
        else if (other.m_storage_type == StorageType::Error)
            PLACEMENT_NEW(&m_storage)::ErrorCode(other.Error());
    }

    ErrorCodeOr(ErrorCodeOr&& other) : m_storage_type(other.m_storage_type) {
        if (other.HasValue())
            PLACEMENT_NEW(&m_storage) Type(other.ReleaseValue());
        else if (other.HasError())
            PLACEMENT_NEW(&m_storage)::ErrorCode(other.ReleaseError());
        other.m_storage_type = StorageType::Uninit;
    }

    ErrorCodeOr& operator=(ErrorCodeOr const& other) {
        if (this != &other) {
            Clear();
            if (other.HasValue())
                PLACEMENT_NEW(&m_storage) Type(other.Value());
            else if (other.HasError())
                PLACEMENT_NEW(&m_storage)::ErrorCode(other.Error());
            m_storage_type = other.m_storage_type;
        }
        return *this;
    }

    ErrorCodeOr& operator=(ErrorCodeOr&& other) {
        if (this != &other) {
            if (other.HasValue() && HasValue()) {
                Swap(other.Value(), Value());
            } else {
                Clear();
                m_storage_type = other.m_storage_type;
                if (other.HasValue())
                    PLACEMENT_NEW(&m_storage) Type(other.ReleaseValue());
                else if (other.HasError())
                    PLACEMENT_NEW(&m_storage)::ErrorCode(other.ReleaseError());
            }
            other.m_storage_type = StorageType::Uninit;
        }
        return *this;
    }

    ErrorCodeOr& operator=(Type&& value) {
        if (HasValue()) {
            Swap(Value(), value);
        } else {
            Clear();
            PLACEMENT_NEW(&m_storage) Type(Move(value));
            m_storage_type = StorageType::Value;
        }

        return *this;
    }

    ~ErrorCodeOr() { Clear(); }

    bool HasValue() const { return m_storage_type == StorageType::Value; }
    bool HasError() const { return m_storage_type == StorageType::Error; }
    bool Uninitialised() const { return m_storage_type == StorageType::Uninit; }

    Type& Value() {
        ASSERT(HasValue());
        return *(Type*)m_storage;
    }
    Type const& Value() const {
        ASSERT(HasValue());
        return *(Type const*)m_storage;
    }
    Type ValueOr(Type fallback) {
        if (HasValue()) return Value();
        return fallback;
    }
    Type OrElse(auto&& function) {
        if (HasValue()) return Value();
        return function(Error());
    }
    Type ReleaseValueOr(Type fallback) {
        if (HasValue()) return ReleaseValue();
        return Move(fallback);
    }

    ErrorCode& Error() {
        ASSERT(HasError());
        return *(::ErrorCode*)m_storage;
    }
    ErrorCode const& Error() const {
        ASSERT(HasError());
        return *(ErrorCode const*)m_storage;
    }

    template <typename U = Type>
    requires(Cloneable<Type>)
    ErrorCodeOr<Type> Clone(Allocator& a, CloneType clone_type) const {
        if (HasValue()) return Value().Clone(a, clone_type);
        if (HasError()) return Error();
        return {};
    }

    Type ReleaseValue() {
        ASSERT(HasValue());
        Type released_value = Move(Value());
        Value().~Type();
        m_storage_type = StorageType::Uninit;
        return released_value;
    }

    ErrorCode ReleaseError() {
        ASSERT(HasError());
        auto released_error = Move(Error());
        m_storage_type = StorageType::Uninit;
        return released_error;
    }

  private:
    void Clear() {
        if (HasValue()) Value().~Type();
        m_storage_type = StorageType::Uninit;
    }

    enum class StorageType : u8 {
        Uninit,
        Value,
        Error,
    };

    alignas((alignof(Type) > alignof(::ErrorCode)) ? alignof(Type) : alignof(::ErrorCode))
        u8 m_storage[(sizeof(Type) > sizeof(::ErrorCode)) ? sizeof(Type) : sizeof(::ErrorCode)];
    StorageType m_storage_type;
};

template <>
class [[nodiscard]] ErrorCodeOr<void> {
  public:
    ErrorCodeOr(ErrorCode ec) : m_error(ec) {}
    ErrorCodeOr() {}
    constexpr ErrorCodeOr(SuccessType) {}

    bool HasError() const { return m_error.category != nullptr; }
    bool Succeeded() const { return m_error.category == nullptr; }

    ErrorCode Error() const { return m_error; }
    void ReleaseValue() {}

    constexpr bool operator==(ErrorCode const& ec) const { return m_error == ec; }

  private:
    ErrorCode m_error {};
};

struct Writer;

struct ErrorCodeCategory {
    // a few uppercase characters to identify this category, not null
    char const* category_id;
    // function can be not-set. returns a human-readable short description of the error code, starts with a
    // capital letter and does _not_ end with a full stop
    ErrorCodeOr<void> (*const message)(Writer const& writer, ErrorCode e);
};

struct ErrorCodeRelocateTryHelpers {
    TRY_HELPER_INHERIT(IsError, TryHelpers)
    TRY_HELPER_INHERIT(ExtractValue, TryHelpers)
    static ErrorCode ExtractError(auto const& o, SourceLocation loc = SourceLocation::Current()) {
        auto e = o.Error();
        e.source_location = loc;
        return e;
    }
};
#define TRY_I(expression) TRY_X(ErrorCodeRelocateTryHelpers, expression)
