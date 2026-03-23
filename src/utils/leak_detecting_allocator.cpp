// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "leak_detecting_allocator.hpp"

#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"
#include "utils/logger/logger.hpp"

LeakDetectingAllocator::~LeakDetectingAllocator() {
    ArenaAllocatorWithInlineStorage<1000> scratch_arena {Malloc::Instance()};
    for (auto& i : m_allocations) {
        if (!i.stack_trace)
            LogDebug({}, "ERROR: memory leak detected of {} bytes, no stacktrace available", i.data.size);
        else {
            LogDebug({},
                     "ERROR: memory leak detected of {} bytes, allocated at location:\n{}",
                     i.data.size,
                     StacktraceString(*i.stack_trace, scratch_arena, {.ansi_colours = true}));
        }
    }

    ASSERT_EQ(m_allocations.size, 0u);
}

Span<u8> LeakDetectingAllocator::DoCommand(AllocatorCommandUnion const& command_union) {
    switch (command_union.tag) {
        case AllocatorCommand::Allocate: {
            auto const& cmd = command_union.Get<AllocateCommand>();
            auto result = Malloc::Instance().DoCommand(cmd);
            {
                ScopedMutexLock const lock(m_lock);
                dyn::Append(m_allocations, {result, CurrentStacktrace().ReleaseValueOr({})});
            }
            FillMemory(result.ToByteSpan(), 0xcd); // fill it with unusual data
            return result;
        }

        case AllocatorCommand::Free: {
            auto const& cmd = command_union.Get<FreeCommand>();
            Malloc::Instance().DoCommand(cmd);
            ScopedMutexLock const lock(m_lock);
            auto const removed =
                dyn::RemoveValueIf(m_allocations, [memory = cmd.allocation](Allocation const& v) {
                    return memory.data == v.data.data;
                });
            ASSERT(removed != 0);
            return {};
        }

        case AllocatorCommand::Resize: {
            auto const& cmd = command_union.Get<ResizeCommand>();
            {
                ScopedMutexLock const lock(m_lock);
                auto const removed =
                    dyn::RemoveValueIf(m_allocations, [memory = cmd.allocation](Allocation const& v) {
                        return memory.data == v.data.data;
                    });
                ASSERT(removed != 0);
            }

            auto result = Malloc::Instance().DoCommand(cmd);

            {
                ScopedMutexLock const lock(m_lock);
                dyn::Append(m_allocations, {result, CurrentStacktrace()});
            }

            return result;
        }
    }
    return {};
}
