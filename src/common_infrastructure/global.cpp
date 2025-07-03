// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "global.hpp"

#include <valgrind/valgrind.h>

#include "utils/debug_info/debug_info.h"

#include "error_reporting.hpp"

static void StartupTracy() {
#ifdef TRACY_ENABLE
    ___tracy_startup_profiler();
#endif
}

static void ShutdownTracy() {
#ifdef TRACY_ENABLE
    ___tracy_shutdown_profiler();
#endif
}

static u32 g_tracy_init = 0;

void GlobalInit(GlobalInitOptions options) {
#if __has_feature(thread_sanitizer)
    // Very unstable to run Valgrind with ThreadSanitizer.
    if (RUNNING_ON_VALGRIND) __builtin_abort();
#endif

    if (g_tracy_init++ == 0) StartupTracy();

    if (options.set_main_thread) SetThreadName("main", FinalBinaryIsPlugin());

    SetPanicHook([](char const* message_c_str, SourceLocation loc, uintptr loc_pc) {
        // We don't have to be signal-safe here.

        if (!PRODUCTION_BUILD && IsRunningUnderDebugger()) __builtin_debugtrap();

        ArenaAllocatorWithInlineStorage<2000> arena {PageAllocator::Instance()};

        auto const stacktrace = CurrentStacktrace(ProgramCounter {loc_pc});
        auto const thread_id = CurrentThreadId();

        // Step 1: log the error for easier local debugging.
        Log(ModuleName::ErrorReporting, LogLevel::Error, [&](Writer writer) -> ErrorCodeOr<void> {
            TRY(fmt::FormatToWriter(writer,
                                    "[panic] ({}) {} (address: 0x{x}, thread: {})\n",
                                    ToString(g_final_binary_type),
                                    FromNullTerminated(message_c_str),
                                    loc_pc,
                                    thread_id));
            auto _ = FrameInfo::FromSourceLocation(loc, loc_pc, IsAddressInCurrentModule(loc_pc))
                         .Write(0, writer, {});
            if (stacktrace) {
                auto stack = stacktrace->Items();
                if (stack[0] == loc_pc) stack.RemovePrefix(1);
                TRY(WriteStacktrace(stack,
                                    writer,
                                    {
                                        .ansi_colours = false,
                                        .demangle = true,
                                    }));
            }
            return k_success;
        });

        // Step 2: send an error report to Sentry.
        {
            auto const thread_name = ThreadName(false);
            sentry::SentryOrFallback sentry {};
            DynamicArray<char> response {arena};
            TRY_OR(sentry::SubmitCrash(*sentry,
                                       stacktrace,
                                       sentry::ErrorEvent::Thread {
                                           .id = thread_id,
                                           .is_main = g_is_logical_main_thread != 0,
                                           .name = thread_name.Transform([](String s) { return s; }),
                                       },
                                       sentry::ErrorEvent::Exception {
                                           .type = "Panic",
                                           .value = FromNullTerminated(message_c_str),
                                       },
                                       "",
                                       arena,
                                       {
                                           .write_to_file_if_needed = true,
                                           .response = dyn::WriterFor(response),
                                           .request_options =
                                               {
                                                   .timeout_seconds = 3,
                                               },
                                       }),
                   {
                       LogError(ModuleName::ErrorReporting,
                                "Failed to submit panic to Sentry: {}, {}",
                                error,
                                response);
                   });
        }
    });

    if (auto const err = InitStacktraceState(options.current_binary_path))
        ReportError(ErrorLevel::Warning,
                    HashComptime("stacktrace_init_failed"),
                    "Failed to initialize stacktrace state: {}",
                    *err);

    InitLogger({.destination = ({
                    LogConfig::Destination d;
                    switch (g_final_binary_type) {
                        case FinalBinaryType::Clap:
                        case FinalBinaryType::AuV2:
                        case FinalBinaryType::Vst3: d = LogConfig::Destination::File; break;
                        case FinalBinaryType::Standalone:
                        case FinalBinaryType::Packager:
                        case FinalBinaryType::PresetEditor:
                        case FinalBinaryType::WindowsInstaller:
                        case FinalBinaryType::WindowsUninstaller:
                        case FinalBinaryType::DocsPreprocessor:
                        case FinalBinaryType::Tests: d = LogConfig::Destination::Stderr; break;
                    }
                    d;
                })});

    InitLogFolderIfNeeded();

    // after tracy
    BeginCrashDetection([](String crash_message, uintptr error_program_counter) {
        // This function is async-signal-safe.

        auto const stacktrace = CurrentStacktrace(ProgramCounter {error_program_counter});

        // We might be running as a shared library and the crash could have occurred in a callstack
        // completely unrelated to us. We don't want to write a crash report in that case.
        if (stacktrace && !HasAddressesInCurrentModule(*stacktrace)) return;

        if (!PRODUCTION_BUILD && IsRunningUnderDebugger()) __builtin_debugtrap();

        FixedSizeAllocator<4000> allocator {nullptr};

        auto const thread_id = CurrentThreadId();

        // Step 1: dump info to stderr. This is useful for debugging: either us as developers, host
        // developers, or if this code is running in a CLI - the user.
        {
            auto buffered_writer = BufferedWriter<1000> {StdWriter(StdStream::Err)};
            auto writer = buffered_writer.Writer();
            DEFER { buffered_writer.FlushReset(); };

            auto _ =
                fmt::FormatToWriter(writer,
                                    "\n" ANSI_COLOUR_SET_FOREGROUND_RED
                                    "[crash] ({}) {} (address: 0x{x}, thread: {})" ANSI_COLOUR_RESET "\n",
                                    ToString(g_final_binary_type),
                                    crash_message,
                                    error_program_counter,
                                    thread_id);
            if (stacktrace) {
                auto _ = WriteStacktrace(*stacktrace,
                                         writer,
                                         {
                                             .ansi_colours = true,
                                             .demangle = IS_WINDOWS,
                                         });
            }
            auto _ = writer.WriteChar('\n');
        }

        // Step 2: write a crash report to a file in the Sentry format.
        {
            auto const log_folder = LogFolder();
            if (!log_folder) {
                auto _ = StdPrint(StdStream::Err, "Log folder is not set, cannot write crash report\n");
                return;
            }

            sentry::SentryOrFallback sentry {};
            auto _ = sentry::WriteCrashToFile(*sentry,
                                              stacktrace,
                                              sentry::ErrorEvent::Thread {
                                                  .id = thread_id,
                                              },
                                              sentry::ErrorEvent::Exception {
                                                  .type = "Crash",
                                                  .value = crash_message,
                                              },
                                              *log_folder,
                                              "",
                                              allocator);
        }
    });

    if (options.init_error_reporting) InitBackgroundErrorReporting({});
}

void GlobalDeinit(GlobalShutdownOptions options) {
    if (options.shutdown_error_reporting) ShutdownBackgroundErrorReporting();

    EndCrashDetection(); // before tracy

    ShutdownStacktraceState();

    ShutdownLogger();

    ASSERT(g_tracy_init);
    if (--g_tracy_init == 0) ShutdownTracy();
}
