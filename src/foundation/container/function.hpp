// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"

template <typename T>
struct FunctionRef;

template <typename ReturnType, class... Args>
struct FunctionRef<ReturnType(Args...)> {
    constexpr FunctionRef() = default;
    constexpr FunctionRef(decltype(nullptr)) {}

    // IMPORTANT: the function/lambda you pass in must outlive the FunctionRef
    template <typename F>
    requires(!Same<FunctionRef, RemoveReference<F>> && FunctionWithSignature<F, ReturnType, Args...>)
    constexpr FunctionRef(F&& func) {
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(decltype(&func))d)(args...); };
        function_object = (void*)&func;
    }

    constexpr ReturnType operator()(Args... args) const {
        return invoke_function(function_object, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    void* function_object = nullptr;
};

template <typename T>
struct TrivialFunctionRef;

template <typename ReturnType, class... Args>
struct TrivialFunctionRef<ReturnType(Args...)> {
    constexpr TrivialFunctionRef() = default;
    constexpr TrivialFunctionRef(decltype(nullptr)) {}

    // IMPORTANT: the function/lambda you pass in must outlive the FunctionRef
    template <typename F>
    requires(!Same<TrivialFunctionRef, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             (FunctionPointer<Decay<F>> || TriviallyCopyable<F>))
    constexpr TrivialFunctionRef(F const& func) {
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(F*)d)(args...); };
        function_object = (void*)&func;
        function_object_size = !FunctionPointer<Decay<F>> ? sizeof(Decay<F>) : 0;
    }

    TrivialFunctionRef CloneObject(ArenaAllocator& a) {
        TrivialFunctionRef result;
        result.invoke_function = invoke_function;
        if (function_object_size != 0) {
            auto allocation = a.Allocate({.size = function_object_size,
                                          .alignment = k_max_alignment,
                                          .allow_oversized_result = false});
            __builtin_memcpy(allocation.data, function_object, function_object_size);
            result.function_object = (void*)allocation.data;
            result.function_object_size = function_object_size;
        } else {
            result.function_object = function_object;
            result.function_object_size = function_object_size;
        }
        return result;
    }

    constexpr ReturnType operator()(Args... args) const {
        return invoke_function(function_object, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    void* function_object = nullptr;
    usize function_object_size = 0; // Not used for plain function pointers
};

template <usize, class T>
struct TrivialFixedSizeFunction;

template <usize k_capacity, class ReturnType, class... Args>
struct TrivialFixedSizeFunction<k_capacity, ReturnType(Args...)> {
    constexpr TrivialFixedSizeFunction() = default;

    constexpr TrivialFixedSizeFunction(decltype(nullptr)) {}

    template <typename F>
    requires(FunctionPointer<Decay<F>>)
    constexpr TrivialFixedSizeFunction(F* f) {
        Decay<F> function_pointer = f;
        Set(function_pointer);
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr TrivialFixedSizeFunction(F const& func) {
        Set(func);
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr TrivialFixedSizeFunction& operator=(F const& func) {
        Set(func);
        return *this;
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr void Set(F const& func) {
        static_assert(sizeof(F) <= k_capacity);
        __builtin_memcpy_inline(function_object_storage, &func, sizeof(F));
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(F*)d)(args...); };
    }

    constexpr ReturnType operator()(Args... args) const {
        ASSERT(invoke_function);
        return invoke_function((void*)function_object_storage, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    alignas(k_max_alignment) u8 function_object_storage[Max(k_capacity, sizeof(void*))];
};

template <typename T>
struct TrivialAllocatedFunction;

template <typename ReturnType, class... Args>
struct TrivialAllocatedFunction<ReturnType(Args...)> {
    NON_COPYABLE(TrivialAllocatedFunction);

    // Create empty
    constexpr TrivialAllocatedFunction(Allocator& a) : allocator(a) {}

    // Create from TrivialFunctionRef
    constexpr TrivialAllocatedFunction(TrivialFunctionRef<ReturnType(Args...)> const& ref, Allocator& a)
        : allocator(a) {
        if (ref.function_object_size) {
            auto mem = allocator.Allocate({.size = ref.function_object_size,
                                           .alignment = k_max_alignment,
                                           .allow_oversized_result = true});
            __builtin_memcpy(mem.data, ref.function_object, ref.function_object_size);
            function_object = (void*)mem.data;
            function_object_size = mem.size;
        } else {
            function_object = (u8*)ref.function_object;
            function_object_size = 0;
        }
        invoke_function = ref.invoke_function;
    }

    // Create from a function/lambda - we just delegate to the TrivialFunctionRef constructor
    template <typename F>
    requires(!Same<F, TrivialFunctionRef<ReturnType(Args...)>>)
    constexpr TrivialAllocatedFunction(F&& f, Allocator& a)
        : TrivialAllocatedFunction(TrivialFunctionRef<ReturnType(Args...)> {Forward<F>(f)}, a) {}

    // Move constructor
    constexpr TrivialAllocatedFunction(TrivialAllocatedFunction&& other) : allocator(other.allocator) {
        invoke_function = Exchange(other.invoke_function, nullptr);
        function_object = Exchange(other.function_object, {});
        function_object_size = Exchange(other.function_object_size, 0u);
    }

    // Move assignment
    constexpr TrivialAllocatedFunction& operator=(TrivialAllocatedFunction&& other) {
        if (function_object_size) allocator.Free(AllocatedSpan());
        invoke_function = Exchange(other.invoke_function, nullptr);
        function_object = Exchange(other.function_object, {});
        function_object_size = Exchange(other.function_object_size, 0u);
        return *this;
    }

    // Destructor
    constexpr ~TrivialAllocatedFunction() {
        if (function_object_size) allocator.Free(AllocatedSpan());
    }

    // Assignment from a TrivialFunctionRef
    constexpr TrivialAllocatedFunction& operator=(TrivialFunctionRef<ReturnType(Args...)> const& ref) {
        // If the new function is empty, just clear our state.
        if (!ref.function_object) {
            if (function_object_size) allocator.Free(AllocatedSpan());
            invoke_function = nullptr;
            function_object = nullptr;
            function_object_size = 0;
            return *this;
        }

        // Is the new function is a function pointer?
        if (!ref.function_object_size) {
            if (function_object_size) allocator.Free(AllocatedSpan());
            function_object = ref.function_object;
            function_object_size = 0;
            invoke_function = ref.invoke_function;
            return *this;
        }

        // At this stage the new function is a lambda or similar: it has a size.

        // We might be a function pointer already - we need to clear that out so that we don't take it as a
        // pointer to an existing allocation further down.
        if (!function_object_size) function_object = nullptr;

        auto const mem =
            allocator.Reallocate<u8>(ref.function_object_size, AllocatedSpan(), function_object_size, true);
        function_object = (void*)mem.data;
        function_object_size = mem.size;
        __builtin_memcpy(function_object, ref.function_object, ref.function_object_size);
        invoke_function = ref.invoke_function;
        return *this;
    }

    // Assignment from a function/lambda - we just delegate to the TrivialFunctionRef assignment
    template <typename F>
    requires(!Same<F, TrivialFunctionRef<ReturnType(Args...)>>)
    constexpr TrivialAllocatedFunction& operator=(F&& func) {
        return (*this = TrivialFunctionRef<ReturnType(Args...)> {Forward<F>(func)});
    }

    constexpr ReturnType operator()(Args... args) const {
        ASSERT(invoke_function);
        return invoke_function((void*)function_object, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    // Convertible to the ref type
    constexpr operator TrivialFunctionRef<ReturnType(Args...)>() const {
        TrivialFunctionRef<ReturnType(Args...)> result;
        result.invoke_function = invoke_function;
        result.function_object = function_object;
        result.function_object_size = function_object_size;
        return result;
    }

    Span<u8> AllocatedSpan() const { return {(u8*)function_object, function_object_size}; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    Allocator& allocator;
    void* function_object = nullptr;
    usize function_object_size = 0; // Not used for plain function pointers.
};
