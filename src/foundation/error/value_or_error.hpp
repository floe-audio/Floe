// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/tagged_union.hpp"
#include "foundation/error/try.hpp"

enum class ResultType : u8 {
    Error,
    Value,
};

template <typename ValueType, typename ErrorType>
using ResultTypeUnion = TaggedUnion<ResultType,
                                    TypeAndTag<ValueType, ResultType::Value>,
                                    TypeAndTag<ErrorType, ResultType::Error>>;

template <typename ValueType, typename ErrorType>
struct ValueOrError : ResultTypeUnion<ValueType, ErrorType> {
    using Super = ResultTypeUnion<ValueType, ErrorType>;
    ValueOrError(auto v) : Super(v) {}
    ValueOrError& operator=(auto t) {
        Super::Set(t);
        return *this;
    }

    bool HasError() const { return Super::tag == ResultType::Error; }
    ErrorType Error() const { return Super::template Get<ErrorType>(); }
    ValueType ReleaseValue() const { return Super::template Get<ValueType>(); }
};

template <typename ErrorType>
struct VoidOrError {
    VoidOrError() = default;
    VoidOrError(ErrorType v) {
        tag = ResultType::Error;
        error = v;
    }
    VoidOrError& operator=(ErrorType v) {
        tag = ResultType::Error;
        error = v;
        return *this;
    }
    VoidOrError(SuccessType) {}
    VoidOrError& operator=(SuccessType) {
        tag = ResultType::Value;
        return *this;
    }

    bool HasError() const { return tag == ResultType::Error; }
    ErrorType Error() const { return error; }
    void ReleaseValue() const {}
    bool Succeeded() const { return tag == ResultType::Value; }

    ResultType tag {ResultType::Value};
    ErrorType error {};
};
