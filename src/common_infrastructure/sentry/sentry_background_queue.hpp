// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/threading.hpp"
#include "utils/thread_extra/thread_extra.hpp"

#include "sentry.hpp"

namespace sentry {

struct BackgroundQueue {
    Thread thread;
    Atomic<bool> end_thread = false;
    WorkSignaller signaller;
    ThreadsafeBoundedQueue<Error, 32> queue {};
    ThreadsafeBoundedQueue<Feedback, 4> feedback_queue {};
    ArenaAllocator tag_arena {Malloc::Instance()};
};

namespace detail {

static Optional<fmt::UuidArray>
Submit(ArenaAllocator& scratch_arena, Sentry& sentry, String envelope, EnvelopeWriter& writer) {
    DynamicArray<char> response {scratch_arena};
    auto const o = SubmitEnvelope(sentry,
                                  envelope,
                                  &writer,
                                  scratch_arena,
                                  {
                                      .write_to_file_if_needed = true,
                                      .response = dyn::WriterFor(response),
                                      .request_options =
                                          {
                                              .timeout_seconds = 5,
                                          },
                                  });
    if (o.HasError()) {
        LogError(ModuleName::ErrorReporting, "Failed to send Sentry envelope: {}, {}", o.Error(), response);
        return k_nullopt;
    } else {
        LogDebug(ModuleName::ErrorReporting, "Sentry response received: {}", response);
        return o.Value();
    }
}

static void BackgroundThread(BackgroundQueue& queue, Span<Tag const> tags) {
    auto& sentry = InitGlobalSentry(ParseDsnOrThrow(k_dsn), tags);

    ArenaAllocatorWithInlineStorage<4000> scratch_arena {PageAllocator::Instance()};

    InitLogFolderIfNeeded();

    if (auto const o = ConsumeAndSubmitDisasterFiles(sentry, *LogFolder(), scratch_arena); o.HasError())
        LogError(ModuleName::ErrorReporting, "Failed to consume disaster files: {}", o.Error());

    if (auto const o = ConsumeAndSubmitErrorFiles(sentry, *LogFolder(), scratch_arena); o.HasError())
        LogError(ModuleName::ErrorReporting, "Failed to consume error files: {}", o.Error());

    // start session
    if (!sentry.online_reporting_disabled.Load(LoadMemoryOrder::Relaxed) && k_online_reporting) {
        DynamicArray<char> envelope {scratch_arena};
        EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
        auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Ok);
        if (envelope.size) auto _ = Submit(scratch_arena, sentry, envelope, writer);
    }

    while (true) {
        queue.signaller.WaitUntilSignalledOrSpurious(1000u);
        scratch_arena.ResetCursorAndConsolidateRegions();

        bool repeat = false;
        if (auto error = queue.queue.TryPop()) {
            DynamicArray<char> envelope {scratch_arena};
            EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
            auto _ = EnvelopeAddEvent(sentry,
                                      writer,
                                      *error,
                                      {
                                          .signal_safe = false,
                                          .diagnostics = true,
                                      });
            auto _ = Submit(scratch_arena, sentry, envelope, writer);
            repeat = true;
        } else if (auto feedback = queue.feedback_queue.TryPop()) {
            // For some reason, Sentry silently rejects feedback if it's in the same envelope as another
            // event. It also silently rejects it if the feedback event includes contexts such as "os",
            // "device", and the "user" object. So if we want to include diagnostics, we need to send first an
            // event with the diagnostics, and then the feedback with "associated_event_id" set to the event's
            // "event_id".
            Optional<fmt::UuidArray> event_id {};
            if (feedback->include_diagnostics) {
                DynamicArray<char> envelope {scratch_arena};
                EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
                auto _ = EnvelopeAddEvent(sentry,
                                          writer,
                                          {
                                              .level = ErrorEvent::Level::Info,
                                              .message = "Feedback diagnostics",
                                              .tags = {},
                                          },
                                          {
                                              .signal_safe = false,
                                              .diagnostics = feedback->include_diagnostics,
                                          });
                event_id = Submit(scratch_arena, sentry, envelope, writer);
            }
            DynamicArray<char> envelope {scratch_arena};
            EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
            if (event_id) feedback->associated_event_id = *event_id;
            auto _ = EnvelopeAddFeedback(sentry, writer, *feedback);
            auto _ = Submit(scratch_arena, sentry, envelope, writer);
            repeat = true;
        }
        if (repeat) queue.signaller.Signal();

        auto const end = queue.end_thread.Load(LoadMemoryOrder::Acquire);
        if (end && !sentry.online_reporting_disabled.Load(LoadMemoryOrder::Relaxed) && k_online_reporting) {
            DynamicArray<char> envelope {scratch_arena};
            EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
            auto _ = EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::EndedNormally);
            if (envelope.size) auto _ = Submit(scratch_arena, sentry, envelope, writer);
        }

        if (end) break;
    }
}

} // namespace detail

PUBLIC bool StartThread(BackgroundQueue& queue, Span<Tag const> tags) {
    auto cloned_tags = tags.Clone(queue.tag_arena, CloneType::Deep);
    queue.thread.Start(
        [&queue, cloned_tags]() {
            try {
                detail::BackgroundThread(queue, cloned_tags);
            } catch (PanicException) {
            }
        },
        "sentry",
        {});
    return true;
}

PUBLIC void RequestThreadEnd(BackgroundQueue& queue) {
    if (queue.end_thread.Load(LoadMemoryOrder::Acquire)) return;

    queue.end_thread.Store(true, StoreMemoryOrder::Release);
    queue.signaller.Signal();
}

PUBLIC void WaitForThreadEnd(BackgroundQueue& queue) {
    ASSERT_EQ(queue.end_thread.Load(LoadMemoryOrder::Acquire), true);
    if (queue.thread.Joinable()) queue.thread.Join();

    // it's possible there's still messages in the queue, let's write them to file
    SentryOrFallback sentry;
    while (auto const error = queue.queue.TryPop())
        if (error->level >= ErrorEvent::Level::Error) auto _ = WriteErrorToFile(*sentry, *error);
    while (auto const feedback = queue.feedback_queue.TryPop())
        auto _ = WriteFeedbackToFile(*sentry, *feedback);
}

// thread-safe, not signal-safe
// To use this, create a message and then fill in the fields, allocating using the message's arena. Move() it
// into this function.
PUBLIC bool TryEnqueueError(BackgroundQueue& queue, Error&& error) {
    if (!queue.end_thread.Load(LoadMemoryOrder::Acquire)) {
        if (queue.queue.TryPush(Move(error))) {
            queue.signaller.Signal();
            return true;
        }
    }

    return false;
}

PUBLIC bool TryEnqueueFeedback(BackgroundQueue& queue, Feedback&& feedback) {
    if (!queue.end_thread.Load(LoadMemoryOrder::Acquire)) {
        if (queue.feedback_queue.TryPush(Move(feedback))) {
            queue.signaller.Signal();
            return true;
        }
    }

    return false;
}

} // namespace sentry
