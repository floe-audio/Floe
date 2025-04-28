// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h> // needs to be first
//
#include <dbghelp.h>
#include <process.h>
#include <processthreadsapi.h>
#include <synchapi.h>

//
#include "os/undef_windows_macros.h"
//

#include "foundation/foundation.hpp"

#include "threading.hpp"

// The Semaphore class is based on Jeff Preshing's Semaphore class
// Copyright (c) 2015 Jeff Preshing
// SPDX-License-Identifier: Zlib
// https://github.com/preshing/cpp11-on-multicore
Semaphore::Semaphore(int initialCount) {
    ASSERT(initialCount >= 0);
    m_sema.As<HANDLE>() = CreateSemaphore(nullptr, initialCount, MAXLONG, nullptr);
}
Semaphore::~Semaphore() { CloseHandle(m_sema.As<HANDLE>()); }
void Semaphore::Wait() { WaitForSingleObject(m_sema.As<HANDLE>(), INFINITE); }
bool Semaphore::TryWait() { return WaitForSingleObject(m_sema.As<HANDLE>(), 0) != WAIT_TIMEOUT; }
bool Semaphore::TimedWait(u64 usecs) {
    return WaitForSingleObject(m_sema.As<HANDLE>(), (unsigned long)(usecs / 1000)) != WAIT_TIMEOUT;
}
void Semaphore::Signal() { ReleaseSemaphore(m_sema.As<HANDLE>(), 1, nullptr); }
void Semaphore::Signal(int count) { ReleaseSemaphore(m_sema.As<HANDLE>(), count, nullptr); }

void SleepThisThread(int milliseconds) { ::Sleep((DWORD)milliseconds); }
void YieldThisThread() { Sleep(0); }

Mutex::Mutex() { InitializeCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
Mutex::~Mutex() { DeleteCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
void Mutex::Lock() { EnterCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
bool Mutex::TryLock() { return TryEnterCriticalSection(&mutex.As<CRITICAL_SECTION>()) != FALSE; }
void Mutex::Unlock() { LeaveCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
RecursiveMutex::RecursiveMutex() { InitializeCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
RecursiveMutex::~RecursiveMutex() { DeleteCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
void RecursiveMutex::Lock() { EnterCriticalSection(&mutex.As<CRITICAL_SECTION>()); }
bool RecursiveMutex::TryLock() { return TryEnterCriticalSection(&mutex.As<CRITICAL_SECTION>()) != FALSE; }
void RecursiveMutex::Unlock() { LeaveCriticalSection(&mutex.As<CRITICAL_SECTION>()); }

WaitResult WaitIfValueIsExpected(Atomic<u32>& value, u32 expected, Optional<u32> timeout_milliseconds) {
    auto const succeed = WaitOnAddress(&value.raw,
                                       &expected,
                                       sizeof(expected),
                                       timeout_milliseconds ? *timeout_milliseconds : INFINITE);
    auto result = WaitResult::WokenOrSpuriousOrNotExpected;
    if (!succeed && GetLastError() == ERROR_TIMEOUT)
        result = WaitResult::TimedOut;
    else
        ASSERT(succeed);
    return result;
}

void WakeWaitingThreads(Atomic<u32>& v, NumWaitingThreads num_waiters) {
    switch (num_waiters) {
        case NumWaitingThreads::One: WakeByAddressSingle(&v.raw); break;
        case NumWaitingThreads::All: WakeByAddressAll(&v.raw); break;
    }
}

//
// ==========================================================================================================

u64 CurrentThreadId() { return (u64)(uintptr_t)GetCurrentThreadId(); }

void SetCurrentThreadPriorityRealTime() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

Thread::Thread() {}
Thread::~Thread() { ASSERT(!Joinable()); }

Thread::Thread(Thread&& other) : m_thread(other.m_thread) { other.m_thread.As<HANDLE>() = nullptr; }

Thread& Thread::operator=(Thread&& other) {
    ASSERT(!Joinable());
    m_thread = other.m_thread;
    other.m_thread.As<HANDLE>() = nullptr;
    return *this;
}

static unsigned __stdcall ThreadProc(void* data) {
    try {
        auto d = (Thread::ThreadStartData*)data;
        d->StartThread();
        delete d;
    } catch (PanicException) {
    }
    return 0;
}

void SetThreadName(String name, bool tag_only) {
    (void)tag_only; // We don't set the thread name in the OS anyways.
    detail::SetThreadLocalThreadName(name);
}

Optional<DynamicArrayBounded<char, k_max_thread_name_size>> ThreadName(bool tag_only) {
    if (g_is_main_thread) return "main"_s;
    (void)tag_only; // We don't set the thread name in the OS anyways.
    auto const name = detail::GetThreadLocalThreadName();
    if (name) return *name;
    return k_nullopt;
}

void Thread::Start(StartFunction&& function, String name, ThreadStartOptions options) {
    ASSERT(m_thread.As<HANDLE>() == nullptr);

    auto thread_start_data = new Thread::ThreadStartData(Move(function), name, options);

    unsigned thread_id;
    m_thread.As<HANDLE>() = (void*)_beginthreadex(nullptr,
                                                  options.stack_size ? (unsigned int)*options.stack_size : 0,
                                                  &ThreadProc,
                                                  thread_start_data,
                                                  0,
                                                  &thread_id);
    if (m_thread.As<HANDLE>() == nullptr) Panic("Failed to create a thread");
}

bool Thread::Joinable() const { return m_thread.As<HANDLE>() != nullptr; }

void Thread::Join() {
    ASSERT(Joinable());
    ASSERT_EQ(WaitForSingleObject(m_thread.As<HANDLE>(), INFINITE), WAIT_OBJECT_0);
    CloseHandle(m_thread.As<HANDLE>());
    m_thread.As<HANDLE>() = nullptr;
}

void Thread::Detach() {
    ASSERT(Joinable());
    CloseHandle(m_thread.As<HANDLE>());
    m_thread.As<HANDLE>() = nullptr;
}

ConditionVariable::ConditionVariable() { InitializeConditionVariable(&m_cond_var.As<CONDITION_VARIABLE>()); }
ConditionVariable::~ConditionVariable() {}

void ConditionVariable::Wait(ScopedMutexLock& lock) {
    SleepConditionVariableCS(&m_cond_var.As<CONDITION_VARIABLE>(),
                             &lock.mutex.mutex.As<CRITICAL_SECTION>(),
                             INFINITE);
}
void ConditionVariable::TimedWait(ScopedMutexLock& lock, u64 wait_ms) {
    if (wait_ms > 0)
        SleepConditionVariableCS(&m_cond_var.As<CONDITION_VARIABLE>(),
                                 &lock.mutex.mutex.As<CRITICAL_SECTION>(),
                                 CheckedCast<DWORD>(wait_ms));
}
void ConditionVariable::WakeOne() { WakeConditionVariable(&m_cond_var.As<CONDITION_VARIABLE>()); }
void ConditionVariable::WakeAll() { WakeAllConditionVariable(&m_cond_var.As<CONDITION_VARIABLE>()); }
