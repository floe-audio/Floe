// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "utils/debug/tracy_wrapped.hpp"

enum class JobPriority : u8 { High = 0, Normal = 1, Low = 2 };

struct ThreadPool {
    using FunctionType = FunctionQueue<>::Function;

    ~ThreadPool() { StopAllThreads(); }

    void Init(String pool_name, Optional<u32> num_threads) {
        ZoneScoped;
        ASSERT_EQ(m_workers.size, 0u);
        ASSERT(pool_name.size < k_max_thread_name_size - 4u);
        if (!num_threads) num_threads = Min(Max(CachedSystemStats().num_logical_cpus / 2u, 1u), 4u);

        dyn::Resize(m_workers, *num_threads);
        for (auto [i, w] : Enumerate(m_workers)) {
            auto const name = fmt::FormatInline<k_max_thread_name_size>("{}:{}", pool_name, i);
            w.Start([this]() { WorkerProc(this); }, name, {});
        }
    }

    void StopAllThreads() {
        ZoneScoped;
        m_thread_stop_requested.Store(true, StoreMemoryOrder::Release);
        m_cond_var.WakeAll();
        for (auto& t : m_workers)
            if (t.Joinable()) t.Join();
        dyn::Clear(m_workers);
        m_thread_stop_requested.Store(false, StoreMemoryOrder::Release);
    }

    void AddJob(FunctionType f, JobPriority priority = JobPriority::Normal) {
        ZoneScoped;
        ASSERT(f);
        ASSERT(m_workers.size > 0);
        {
            ScopedMutexLock const lock(m_mutex);
            m_job_queues[ToInt(priority)].Push(f);
        }
        m_cond_var.WakeOne();
    }

    // The caller owns the future and is responsible for ensuring it outlives the async task.
    // The cleanup function is always called, regardless of whether the task completed successfully or was
    // cancelled.
    void Async(auto& future, auto&& function, auto&& cleanup, JobPriority priority = JobPriority::Normal) {
        ZoneScoped;
        ASSERT(m_workers.size > 0);

        using FutureType = RemoveReference<decltype(future)>;
        using JobFunctionType = RemoveReference<decltype(function)>;

        static_assert(Same<typename FutureType::ValueType, InvokeResult<JobFunctionType>>);

        future.SetPending();

        AddJob(
            [&future, f = Move(function), cleanup = Move(cleanup)]() mutable {
                DEFER { cleanup(); }; // Always clean-up.
                if (!future.TrySetRunning()) return;
                future.SetResult(f());
            },
            priority);
    }

  private:
    static bool AllQueuesEmpty(Array<FunctionQueue<>, 3>& queues) {
        for (auto& queue : queues)
            if (!queue.Empty()) return false;
        return true;
    }

    static void WorkerProc(ThreadPool* thread_pool) {
        ZoneScoped;
        ArenaAllocatorWithInlineStorage<4000> scratch_arena {Malloc::Instance()};
        while (true) {
            Optional<FunctionQueue<>::Function> f {};
            {
                ScopedMutexLock lock(thread_pool->m_mutex);
                while (AllQueuesEmpty(thread_pool->m_job_queues) &&
                       !thread_pool->m_thread_stop_requested.Load(LoadMemoryOrder::Acquire))
                    thread_pool->m_cond_var.Wait(lock);

                // Check queues in priority order: High, Normal, Low
                for (auto& queue : thread_pool->m_job_queues) {
                    f = queue.TryPop(scratch_arena);
                    if (f) break;
                }
            }
            if (f) (*f)();

            if (thread_pool->m_thread_stop_requested.Load(LoadMemoryOrder::Acquire)) return;
            scratch_arena.ResetCursorAndConsolidateRegions();
        }
    }

    DynamicArray<Thread> m_workers {Malloc::Instance()};
    Atomic<bool> m_thread_stop_requested {};
    Mutex m_mutex {};
    ConditionVariable m_cond_var {};
    Array<FunctionQueue<>, 3> m_job_queues {FunctionQueue<> {.arena = Malloc::Instance()},
                                            FunctionQueue<> {.arena = Malloc::Instance()},
                                            FunctionQueue<> {.arena = Malloc::Instance()}};
};
