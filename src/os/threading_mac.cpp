// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <mach/mach.h>
#include <pthread.h>
#include <semaphore.h>

#include "threading.hpp"

// This is based on Zig's futex
// https://github.com/ziglang/zig
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT
//
// Undocumented futex-like API available on darwin 16+
// (macOS 10.12+, iOS 10.0+, tvOS 10.0+, watchOS 3.0+, catalyst 13.0+).
//
// [ulock.h]: https://github.com/apple/darwin-xnu/blob/master/bsd/sys/ulock.h
// [sys_ulock.c]: https://github.com/apple/darwin-xnu/blob/master/bsd/kern/sys_ulock.c
// clang-format off
// NOLINTNEXTLINE(readability-identifier-naming, bugprone-reserved-identifier)
extern "C" int __ulock_wait2(uint32_t operation, void* addr, uint64_t value, uint64_t timeout_ns, uint64_t value2);
// NOLINTNEXTLINE(readability-identifier-naming, bugprone-reserved-identifier)
extern "C" int __ulock_wake(uint32_t operation, void* addr, uint64_t wake_value);
// clang-format on
#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL        0x00000100
#define ULF_NO_ERRNO        0x01000000

// The Semaphore class is based on Jeff Preshing's Semaphore class
// Copyright (c) 2015 Jeff Preshing
// SPDX-License-Identifier: Zlib
// https://github.com/preshing/cpp11-on-multicore
Semaphore::Semaphore(int initialCount) {
    ASSERT(initialCount >= 0);
    semaphore_create(mach_task_self(), &m_sema.As<semaphore_t>(), SYNC_POLICY_FIFO, initialCount);
}

Semaphore::~Semaphore() { semaphore_destroy(mach_task_self(), m_sema.As<semaphore_t>()); }

void Semaphore::Wait() { semaphore_wait(m_sema.As<semaphore_t>()); }

bool Semaphore::TryWait() { return TimedWait(0); }

bool Semaphore::TimedWait(uint64_t timeout_usecs) {
    mach_timespec_t ts;
    ts.tv_sec = (unsigned)(timeout_usecs / 1000000);
    ts.tv_nsec = (timeout_usecs % 1000000) * 1000;

    // added in OSX 10.10:
    // https://developer.apple.com/library/prerelease/mac/documentation/General/Reference/APIDiffsMacOSX10_10SeedDiff/modules/Darwin.html
    kern_return_t rc = semaphore_timedwait(m_sema.As<semaphore_t>(), ts);

    return rc != KERN_OPERATION_TIMED_OUT;
}

void Semaphore::Signal() { semaphore_signal(m_sema.As<semaphore_t>()); }

void Semaphore::Signal(int count) {
    while (count-- > 0)
        semaphore_signal(m_sema.As<semaphore_t>());
}

void WakeWaitingThreads(Atomic<u32>& value, NumWaitingThreads num_waiters) {
    while (true) {
        auto const return_code =
            __ulock_wake(UL_COMPARE_AND_WAIT | (num_waiters == NumWaitingThreads::One ? 0 : ULF_WAKE_ALL),
                         &value.raw,
                         0);
        if (return_code >= 0) return;
        auto const err = errno;
        switch (err) {
            case EINTR: continue;
            case ENOENT: return;
            default: PanicIfReached();
        }
    }
}

WaitResult WaitIfValueIsExpected(Atomic<u32>& value, u32 expected, Optional<u32> timeout_milliseconds) {
    u64 const timeout_ns = timeout_milliseconds ? (u64)*timeout_milliseconds * 1000 * 1000 : 0;
    auto const return_code =
        __ulock_wait2(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, &value.raw, expected, timeout_ns, 0);
    if (return_code < 0) {
        // With ULF_NO_ERRNO, the return code contains the negated error number directly.
        switch (-return_code) {
            case EINTR: break; // Interrupted by OS or other spurious signalling.
            case EFAULT: break; // Address was paged out (unlikely but possible).
            case ETIMEDOUT: return WaitResult::TimedOut;
            default: PanicIfReached();
        }
    }
    return WaitResult::WokenOrSpuriousOrNotExpected;
}
