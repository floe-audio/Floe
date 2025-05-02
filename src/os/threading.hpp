// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

struct NativeHandleSizes {
    usize thread;
    usize mutex;
    usize cond_var;
    usize sema;
};

static constexpr NativeHandleSizes NativeHandleSizes() {
    if constexpr (IS_LINUX)
        return {.thread = 8, .mutex = 40, .cond_var = 48, .sema = 32};
    else if constexpr (IS_MACOS)
        return {.thread = 8, .mutex = 64, .cond_var = 48, .sema = 4};
    else if constexpr (IS_WINDOWS)
        return {.thread = 8, .mutex = 40, .cond_var = 8, .sema = 8};
    return {.thread = 8, .mutex = 40, .cond_var = 8, .sema = 8};
}

void SleepThisThread(int milliseconds);
void YieldThisThread();

u64 CurrentThreadId(); // signal-safe

void SetCurrentThreadPriorityRealTime();

constexpr static usize k_max_thread_name_size = 16;
// tag_only will tag the thread ID with our thread_local name, rather than attempt to
// set the thread using the OS.
void SetThreadName(String name, bool tag_only);
Optional<DynamicArrayBounded<char, k_max_thread_name_size>> ThreadName(bool tag_only);

// We use this primarily in assertions to check what is the main thread. The main thread could technically
// change thread ID. It's a 'logical' main thread, not a physical one.
extern thread_local u8 g_is_logical_main_thread;

// This is re-entrant safe. If it returns false, there's already a thread that is the logical main thread.
[[nodiscard]] bool EnterLogicalMainThread();
void LeaveLogicalMainThread();

namespace detail {
void AssertThreadNameIsValid(String name);
void SetThreadLocalThreadName(String name);
Optional<String> GetThreadLocalThreadName();
} // namespace detail

struct ThreadStartOptions {
    Optional<usize> stack_size {};
};

class Thread {
  public:
    NON_COPYABLE(Thread);

    Thread();
    ~Thread();

    Thread(Thread&& other);
    Thread& operator=(Thread&& other);

    using StartFunction = TrivialFixedSizeFunction<256, void()>;

    void Start(StartFunction&& function, String name, ThreadStartOptions options = {});
    void Detach();
    void Join();
    bool Joinable() const;

    // Private.
    struct ThreadStartData {
        ThreadStartData(StartFunction&& f, String name, ThreadStartOptions o)
            : start_function(Move(f))
            , options(o) {
            detail::AssertThreadNameIsValid(name);
            thread_name = name;
        }
        void StartThread() {
            SetThreadName(thread_name, false);
            start_function();
        }

        // private
        StartFunction start_function;
        ThreadStartOptions options;
        DynamicArrayBounded<char, k_max_thread_name_size> thread_name {};
    };

  private:
    OpaqueHandle<NativeHandleSizes().thread> m_thread {};
#if !IS_WINDOWS
    bool m_active {};
#endif
};

// This class is based on Jeff Preshing's Semaphore class
// Copyright (c) 2015 Jeff Preshing
// SPDX-License-Identifier: Zlib
// https://github.com/preshing/cpp11-on-multicore
class Semaphore {
  public:
    Semaphore(int initialCount = 0);
    ~Semaphore();
    NON_COPYABLE(Semaphore);

    void Wait();
    bool TryWait();
    bool TimedWait(u64 microseconds);
    void Signal(int count);
    void Signal();

  private:
    OpaqueHandle<NativeHandleSizes().sema> m_sema;
};

// These atomics are just a wrapper around the compiler instrinsics:
// https://gcc.gnu.org/onlinedocs/gcc-4.9.2/gcc/_005f_005fatomic-Builtins.html
//
// It's the same as the C/C++ memory model: https://en.cppreference.com/w/cpp/atomic/memory_order.
//
// Helpful articles on atomics and memory ordering:
// - https://accu.org/journals/overload/32/182/teodorescu/
// - https://dev.to/kprotty/understanding-atomics-and-memory-ordering-2mom
//
// NOTE: __ATOMIC_CONSUME is also available but we're not using it here. cppreference says "The specification
// of release-consume ordering is being revised, and the use of memory_order_consume is temporarily
// discouraged"

enum class LoadMemoryOrder {
    Relaxed = __ATOMIC_RELAXED,

    // Ensures all memory operations declared after actually happen after it.
    Acquire = __ATOMIC_ACQUIRE,

    // Same as Acquire, except guarantees a single total modification ordering of all the operations that are
    // tagged SequentiallyConsistent. Not commonly needed. It's useful when there's multiple atomic variables
    // at play.
    SequentiallyConsistent = __ATOMIC_SEQ_CST,
};

enum class StoreMemoryOrder {
    Relaxed = __ATOMIC_RELAXED,

    // Ensures that all memory operations declared before it actually happen before it.
    Release = __ATOMIC_RELEASE,

    // Same as Release, except guarantees a single total modification ordering of all the operations that are
    // tagged SequentiallyConsistent. Not commonly needed. It's useful when there's multiple atomic variables
    // at play.
    SequentiallyConsistent = __ATOMIC_SEQ_CST,
};

// Read-Modify-Write memory order
enum class RmwMemoryOrder {
    Relaxed = __ATOMIC_RELAXED,
    Acquire = __ATOMIC_ACQUIRE,
    Release = __ATOMIC_RELEASE,
    AcquireRelease = __ATOMIC_ACQ_REL, // both acquire and release
    SequentiallyConsistent = __ATOMIC_SEQ_CST,
};

template <TriviallyCopyable Type>
struct Atomic {
    constexpr Atomic() = default;
    constexpr Atomic(Type v) : raw(v) {}

    NON_COPYABLE_AND_MOVEABLE(Atomic);

    ALWAYS_INLINE void Store(Type v, StoreMemoryOrder memory_order) { AtomicStore(this, v, memory_order); }
    ALWAYS_INLINE Type Load(LoadMemoryOrder memory_order) const { return AtomicLoad(this, memory_order); }

    // Returns the previous value.
    ALWAYS_INLINE Type Exchange(Type desired, RmwMemoryOrder memory_order) {
        return AtomicExchange(this, desired, memory_order);
    }

    // CompareExchange:
    // - Returns true if the exchange succeeded.
    // - If expected != desired, 'expected' is updated with the actual value.
    // - The failure memory order must not be stronger than the success memory order.
    // - Weak may fail spuriously, strong will not. Use strong unless you are already in a loop that can
    //   handle spurious failures.
    ALWAYS_INLINE bool CompareExchangeWeak(Type& expected,
                                           Type desired,
                                           RmwMemoryOrder success_rmw_memory_order,
                                           LoadMemoryOrder failure_load_memory_order) {
        return AtomicCompareExchange(this,
                                     &expected,
                                     desired,
                                     true,
                                     success_rmw_memory_order,
                                     failure_load_memory_order);
    }
    ALWAYS_INLINE bool CompareExchangeStrong(Type& expected,
                                             Type desired,
                                             RmwMemoryOrder success_rmw_memory_order,
                                             LoadMemoryOrder failure_load_memory_order) {
        return AtomicCompareExchange(this,
                                     &expected,
                                     desired,
                                     false,
                                     success_rmw_memory_order,
                                     failure_load_memory_order);
    }

#define ATOMIC_INTEGER_METHOD(name, builtin)                                                                 \
    template <Integral U = Type>                                                                             \
    ALWAYS_INLINE Type Atomic##name(Atomic<Type> volatile* atomic, Type v, RmwMemoryOrder memory_order) {    \
        return builtin(&atomic->raw, v, (int)memory_order);                                                  \
    }                                                                                                        \
    template <Integral U = Type>                                                                             \
    ALWAYS_INLINE Type name(Type v, RmwMemoryOrder memory_order) {                                           \
        return Atomic##name(this, v, memory_order);                                                          \
    }

    ATOMIC_INTEGER_METHOD(FetchAdd, __atomic_fetch_add)
    ATOMIC_INTEGER_METHOD(FetchSub, __atomic_fetch_sub)
    ATOMIC_INTEGER_METHOD(FetchAnd, __atomic_fetch_and)
    ATOMIC_INTEGER_METHOD(FetchOr, __atomic_fetch_or)
    ATOMIC_INTEGER_METHOD(FetchXor, __atomic_fetch_xor)
    ATOMIC_INTEGER_METHOD(FetchNand, __atomic_fetch_nand)
    ATOMIC_INTEGER_METHOD(AddFetch, __atomic_add_fetch)
    ATOMIC_INTEGER_METHOD(SubFetch, __atomic_sub_fetch)
    ATOMIC_INTEGER_METHOD(AndFetch, __atomic_and_fetch)
    ATOMIC_INTEGER_METHOD(OrFetch, __atomic_or_fetch)
    ATOMIC_INTEGER_METHOD(XorFetch, __atomic_xor_fetch)
    ATOMIC_INTEGER_METHOD(NandFetch, __atomic_nand_fetch)

    // These additonal functions ensure that the atomic operations are handled using volatile pointers -
    // important for correct code generation. This is the way libc++ does it.
    ALWAYS_INLINE static void
    AtomicStore(Atomic<Type> volatile* atomic, Type v, StoreMemoryOrder memory_order) {
        __atomic_store(&atomic->raw, &v, (int)memory_order);
    }
    ALWAYS_INLINE static Type AtomicLoad(Atomic<Type> const volatile* atomic, LoadMemoryOrder memory_order) {
        Type result;
        __atomic_load(&atomic->raw, &result, (int)memory_order);
        return result;
    }
    ALWAYS_INLINE static Type
    AtomicExchange(Atomic<Type> volatile* atomic, Type desired, RmwMemoryOrder memory_order) {
        alignas(Type) unsigned char buf[sizeof(Type)];
        auto* ptr = reinterpret_cast<Type*>(buf);
        __atomic_exchange(&atomic->raw, &desired, ptr, (int)memory_order);
        return *ptr;
    }
    ALWAYS_INLINE static bool AtomicCompareExchange(Atomic<Type> volatile* atomic,
                                                    Type* expected,
                                                    Type desired,
                                                    bool weak,
                                                    RmwMemoryOrder success_memory_order,
                                                    LoadMemoryOrder failure_memory_order) {
        return __atomic_compare_exchange(&atomic->raw,
                                         expected,
                                         &desired,
                                         weak,
                                         (int)success_memory_order,
                                         (int)failure_memory_order);
    }

    // Align 1/2/4/8/16-byte types to at least their size.
    static constexpr int k_min_alignment = (sizeof(Type) & (sizeof(Type) - 1)) || sizeof(Type) > 16
                                               ? 0
                                               : sizeof(Type);

    static constexpr int k_alignment = k_min_alignment > alignof(Type) ? k_min_alignment : alignof(Type);

    static_assert(sizeof(Type) != 0);
    static_assert(__atomic_always_lock_free(sizeof(Type), nullptr));

    alignas(k_alignment) Type raw {};
};

// futex
enum class WaitResult { WokenOrSpuriousOrNotExpected, TimedOut };
enum class NumWaitingThreads { One, All };
// Checks if value == expected, if so, it waits until wake() is called, if not, it returns. Can also return
// spuriously. Similar to std::atomic<>::wait().
WaitResult WaitIfValueIsExpected(Atomic<u32>& value, u32 expected, Optional<u32> timeout_milliseconds = {});
void WakeWaitingThreads(Atomic<u32>& value, NumWaitingThreads num_waiters);

inline bool
WaitIfValueIsExpectedStrong(Atomic<u32>& value, u32 expected, Optional<u32> timeout_milliseconds = {}) {
    while (value.Load(LoadMemoryOrder::Acquire) == expected)
        if (WaitIfValueIsExpected(value, expected, timeout_milliseconds) == WaitResult::TimedOut)
            return false;
    return true;
}

// llvm-project/libc/src/__support/threads/sleep.h
inline static void SpinLoopPause() {
#if defined(__X86_64__)
    __builtin_ia32_pause();
#elif defined(__ARM_ARCH)
    __builtin_arm_isb(0xf);
#endif
}

class AtomicFlag {
  public:
    bool ExchangeTrue(RmwMemoryOrder mem_order) { return __atomic_test_and_set(&m_flag, (int)mem_order); }
    void StoreFalse(StoreMemoryOrder mem_order) { __atomic_clear(&m_flag, (int)mem_order); }

  private:
    bool volatile m_flag {};
};

struct AtomicCountdown {
    NON_COPYABLE(AtomicCountdown);

    explicit AtomicCountdown(u32 initial_value) : counter(initial_value) {}

    void CountDown(u32 steps = 1) {
        auto const current = counter.SubFetch(steps, RmwMemoryOrder::AcquireRelease);
        if (current == 0)
            WakeWaitingThreads(counter, NumWaitingThreads::All);
        else
            ASSERT(current < LargestRepresentableValue<u32>());
    }

    void Increase(u32 steps = 1) { counter.FetchAdd(steps, RmwMemoryOrder::AcquireRelease); }

    bool TryWait() const { return counter.Load(LoadMemoryOrder::Acquire) == 0; }

    WaitResult WaitUntilZero(Optional<u32> timeout_ms = {}) {
        while (true) {
            auto const current = counter.Load(LoadMemoryOrder::Acquire);
            ASSERT(current < LargestRepresentableValue<u32>());
            if (current == 0) return WaitResult::WokenOrSpuriousOrNotExpected;
            if (WaitIfValueIsExpected(counter, current, timeout_ms) == WaitResult::TimedOut)
                return WaitResult::TimedOut;
        }
        return WaitResult::WokenOrSpuriousOrNotExpected;
    }

    Atomic<u32> counter;
};

inline void AtomicThreadFence(RmwMemoryOrder memory_order) { __atomic_thread_fence(int(memory_order)); }
inline void AtomicSignalFence(RmwMemoryOrder memory_order) { __atomic_signal_fence(int(memory_order)); }

struct CallOnceFlag {
    bool Called() const { return v.Load(LoadMemoryOrder::Acquire) == k_called; }
    bool Calling() const { return v.Load(LoadMemoryOrder::Acquire) == k_calling; }
    void Reset() { v.Store(k_not_called, StoreMemoryOrder::Release); }

    static constexpr u32 k_not_called = 0;
    static constexpr u32 k_calling = 1;
    static constexpr u32 k_called = 2;
    Atomic<u32> v = k_not_called;
};

// If the function hasn't been called before, it will call it once, even if multiple threads run this function
// at the same time. In any case, after this function returns, the function has been called.
// Same as pthread_once.
PUBLIC void CallOnce(CallOnceFlag& flag, FunctionRef<void()> function) {
    if (flag.v.Load(LoadMemoryOrder::Acquire) != CallOnceFlag::k_called) {
        // IMPROVE: probably faster to use a mutex here but we want to avoid initialising a global mutex at
        // the moment (pthread_mutex_init, InitializeCriticalSection) because the order of initialisation of
        // global objects with constructors can be bug-prone.

        u32 expected = CallOnceFlag::k_not_called;
        if (flag.v.CompareExchangeStrong(expected,
                                         CallOnceFlag::k_calling,
                                         RmwMemoryOrder::AcquireRelease,
                                         LoadMemoryOrder::Acquire)) {
            function();
            flag.v.Store(CallOnceFlag::k_called, StoreMemoryOrder::Release);
            WakeWaitingThreads(flag.v, NumWaitingThreads::All);
        } else {
            while (flag.v.Load(LoadMemoryOrder::Acquire) != CallOnceFlag::k_called)
                if (WaitIfValueIsExpected(flag.v, CallOnceFlag::k_calling, 4000u) == WaitResult::TimedOut)
                    Panic("Possible recursive call to CallOnce");
        }
    }
    ASSERT_EQ(flag.v.Load(LoadMemoryOrder::Relaxed), CallOnceFlag::k_called);
}

// Futex-based mutex, possibly slower than the pthread/CriticalSection based mutexes, but doesn't require
// any initialisation.
// This based on Zig's Mutex
// https://github.com/ziglang/zig/blob/master/lib/std/Thread/Mutex.zig
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT
struct MutexThin {
    static constexpr u32 k_unlocked = 0;
    static constexpr u32 k_locked = 1;
    static constexpr u32 k_contended = 2;

    Atomic<u32> state = k_unlocked;

    // Returns false if timed out.
    bool Lock(Optional<u32> timeout_ms = {}) {
        if (!TryLock()) return LockSlow(timeout_ms);
        return true;
    }

    bool TryLock() {
        u32 expected = k_unlocked;
        return state.CompareExchangeWeak(expected,
                                         k_locked,
                                         RmwMemoryOrder::Acquire,
                                         LoadMemoryOrder::Relaxed);
    }

    // Returns false if timed out.
    bool LockSlow(Optional<u32> timeout_ms = {}) {
        if (state.Load(LoadMemoryOrder::Relaxed) == k_contended) {
            if (WaitIfValueIsExpected(state, k_contended, timeout_ms) == WaitResult::TimedOut) return false;
        }

        while (state.Exchange(k_contended, RmwMemoryOrder::Acquire) != k_unlocked)
            if (WaitIfValueIsExpected(state, k_contended, timeout_ms) == WaitResult::TimedOut) return false;

        return true;
    }

    void Unlock() {
        auto const s = state.Exchange(k_unlocked, RmwMemoryOrder::Release);
        ASSERT(s != k_unlocked);

        if (s == k_contended) WakeWaitingThreads(state, NumWaitingThreads::One);
    }
};

struct CountedInitFlag {
    u32 counter = 0;
    MutexThin mutex {};
};

PUBLIC void CountedInit(CountedInitFlag& flag, FunctionRef<void()> function) {
    flag.mutex.Lock();
    DEFER { flag.mutex.Unlock(); };
    if (flag.counter == 0) function();
    flag.counter += 1;
}

PUBLIC void CountedDeinit(CountedInitFlag& flag, FunctionRef<void()> function) {
    flag.mutex.Lock();
    DEFER { flag.mutex.Unlock(); };
    ASSERT(flag.counter > 0, "mismatched CountedInit/CountedDeinit");
    flag.counter -= 1;
    if (flag.counter == 0) function();
}

struct WorkSignaller {
    void Signal() {
        if (flag.Exchange(k_signalled, RmwMemoryOrder::AcquireRelease) == k_not_signalled)
            WakeWaitingThreads(flag, NumWaitingThreads::One);
    }
    void WaitUntilSignalledOrSpurious(Optional<u32> timeout_milliseconds = {}) {
        if (flag.Exchange(k_not_signalled, RmwMemoryOrder::AcquireRelease) == k_not_signalled)
            WaitIfValueIsExpected(flag, k_not_signalled, timeout_milliseconds);
    }

    void WaitUntilSignalled(Optional<u32> timeout_milliseconds = {}) {
        if (flag.Exchange(k_not_signalled, RmwMemoryOrder::AcquireRelease) == k_not_signalled) do {
                WaitIfValueIsExpected(flag, k_not_signalled, timeout_milliseconds);
            } while (flag.Load(LoadMemoryOrder::Relaxed) == k_not_signalled);
    }

    static constexpr u32 k_signalled = 1;
    static constexpr u32 k_not_signalled = 0;
    Atomic<u32> flag {0};
};

struct Mutex {
    Mutex();
    ~Mutex();
    void Lock();
    bool TryLock();
    void Unlock();

    NON_COPYABLE_AND_MOVEABLE(Mutex);

    OpaqueHandle<NativeHandleSizes().mutex> mutex;
};

struct RecursiveMutex {
    RecursiveMutex();
    ~RecursiveMutex();
    void Lock();
    bool TryLock();
    void Unlock();

    NON_COPYABLE_AND_MOVEABLE(RecursiveMutex);

    OpaqueHandle<NativeHandleSizes().mutex> mutex;
};

struct ScopedMutexLock {
    ScopedMutexLock(Mutex& l) : mutex(l) { l.Lock(); }
    ~ScopedMutexLock() { mutex.Unlock(); }

    Mutex& mutex;
};

class ConditionVariable {
  public:
    ConditionVariable();
    ~ConditionVariable();

    void Wait(ScopedMutexLock& lock);
    void TimedWait(ScopedMutexLock& lock, u64 wait_ms);
    void WakeOne();
    void WakeAll();

  private:
    OpaqueHandle<NativeHandleSizes().cond_var> m_cond_var;
};

class MovableScopedMutexLock {
  public:
    NON_COPYABLE(MovableScopedMutexLock);

    MovableScopedMutexLock(Mutex& l) : m_l(&l) { l.Lock(); }
    ~MovableScopedMutexLock() {
        if (m_locked) m_l->Unlock();
    }

    MovableScopedMutexLock(MovableScopedMutexLock&& other) : m_locked(other.m_locked), m_l(other.m_l) {
        other.m_l = nullptr;
        other.m_locked = false;
    }

    void Unlock() {
        m_l->Unlock();
        m_locked = false;
    }

  private:
    bool m_locked {true};
    Mutex* m_l;
};

template <typename Type>
class MutexProtected {
  public:
    using ValueType = Type;

    MutexProtected() = default;

    template <typename... Args>
    requires(ConstructibleWithArgs<Type, Args...>)
    constexpr MutexProtected(Args&&... value) : m_value(Forward<Args>(value)...) {}

    template <typename Function>
    decltype(auto) Use(Function&& function) {
        ScopedMutexLock const lock(mutex);
        return function(m_value);
    }

    Type& GetWithoutMutexProtection() { return m_value; }

    Mutex mutex {};

  private:
    Type m_value;
};

class SpinLock {
  public:
    void Lock() {
        while (m_lock_flag.ExchangeTrue(RmwMemoryOrder::Acquire)) {
        }
    }

    bool TryLock() { return !m_lock_flag.ExchangeTrue(RmwMemoryOrder::Acquire); }

    void Unlock() { m_lock_flag.StoreFalse(StoreMemoryOrder::Release); }

  private:
    AtomicFlag m_lock_flag = {};
};

class ScopedSpinLock {
  public:
    ScopedSpinLock(SpinLock& l) : m_l(l) { l.Lock(); }
    ~ScopedSpinLock() { m_l.Unlock(); }

  private:
    SpinLock& m_l;
};
