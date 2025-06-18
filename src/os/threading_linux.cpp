// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "threading.hpp"

// The Semaphore class is based on Jeff Preshing's Semaphore class
// Copyright (c) 2015 Jeff Preshing
// SPDX-License-Identifier: Zlib
// https://github.com/preshing/cpp11-on-multicore
Semaphore::Semaphore(int initialCount) {
    ASSERT(initialCount >= 0);
    sem_init(&m_sema.As<sem_t>(), 0, (unsigned)initialCount);
}

Semaphore::~Semaphore() { sem_destroy(&m_sema.As<sem_t>()); }

void Semaphore::Wait() {
    // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
    int rc;
    do {
        rc = sem_wait(&m_sema.As<sem_t>());
    } while (rc == -1 && errno == EINTR);
}

bool Semaphore::TryWait() {
    int rc;
    do {
        rc = sem_trywait(&m_sema.As<sem_t>());
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == EAGAIN);
}

bool Semaphore::TimedWait(u64 microseconds) {
    struct timespec ts;
    int const usecs_in_1_sec = 1000000;
    int const nsecs_in_1_sec = 1000000000;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += microseconds / usecs_in_1_sec;
    ts.tv_nsec += (microseconds % usecs_in_1_sec) * 1000;
    // sem_timedwait bombs if you have more than 1e9 in tv_nsec
    // so we have to clean things up before passing it in
    if (ts.tv_nsec > nsecs_in_1_sec) {
        ts.tv_nsec -= nsecs_in_1_sec;
        ++ts.tv_sec;
    }

    int rc;
    do {
        rc = sem_timedwait(&m_sema.As<sem_t>(), &ts);
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == ETIMEDOUT);
}

void Semaphore::Signal() { sem_post(&m_sema.As<sem_t>()); }

void Semaphore::Signal(int count) {
    while (count-- > 0)
        sem_post(&m_sema.As<sem_t>());
}

static long Futex(u32* uaddr, int futex_op, u32 val, const struct timespec* timeout, u32* uaddr2, u32 val3) {
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

WaitResult WaitIfValueIsExpected(Atomic<u32>& v, u32 expected, Optional<u32> timeout_milliseconds) {
    timespec tm;
    if (timeout_milliseconds) {
        auto const ms = *timeout_milliseconds;
        tm = {
            .tv_sec = ms / 1000,
            .tv_nsec = (ms % 1000) * 1000000,
        };
    }

    auto const return_code = Futex((u32*)__builtin_addressof(v.raw),
                                   FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
                                   expected,
                                   timeout_milliseconds ? &tm : nullptr,
                                   nullptr,
                                   0);

    auto result = WaitResult::WokenOrSpuriousOrNotExpected;
    if (return_code < 0) {
        auto const e = errno;
        switch (e) {
            case ETIMEDOUT: result = WaitResult::TimedOut; break;
            case EINTR: // spurious wakeup
            case EAGAIN: // v != expected
            case EINVAL: // possibly timeout overflow
                break;
            case EFAULT: PanicIfReached(); break;
        }
    }
    return result;
}

void WakeWaitingThreads(Atomic<u32>& v, NumWaitingThreads waiters) {
    auto const return_code = Futex((u32*)__builtin_addressof(v.raw),
                                   FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
                                   waiters == NumWaitingThreads::One ? 1 : LargestRepresentableValue<s32>(),
                                   nullptr,
                                   nullptr,
                                   0);
    if (return_code < 0) {
        auto const err = errno;
        (void)err;
        PanicIfReached();
    }
}
