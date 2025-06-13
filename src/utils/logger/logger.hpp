// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"

// About logging:
// - Debug logs are for debugging on a developer's machine. Use them however you want. They are disabled in
//   production build
// - All other log types are for production use. We have a strict policy: log about the state of the program,
//   and only ever non-personal external state. For example, never log a filepath. It could contain a
//   username. On the other hand, information about the CPU is fine because it's not personal.

enum class LogLevel { Debug, Info, Warning, Error };

struct WriteLogLineOptions {
    bool ansi_colors = false;
    bool no_info_prefix = false;
    bool timestamp = false;
    bool thread = false;
    bool newline = true;
};

struct LogRingBuffer {
    static constexpr usize k_buffer_size = 1 << 13; // must be a power of 2
    static constexpr usize k_max_message_size = LargestRepresentableValue<u8>();

    struct Message {
        u64 seconds_since_epoch {};
        String message;
    };

    struct Snapshot {
        Optional<Message> Next(usize& pos) const;
        DynamicArrayBounded<u8, k_buffer_size> buffer;
    };

    void Write(String message);
    void Reset();
    [[nodiscard]] Snapshot TakeSnapshot();

    Array<u8, k_buffer_size> buffer;
    MutexThin mutex {};
    u16 write {};
    u16 read {};
};

enum class ModuleName {
    Global,
    Main,
    Package,
    Gui,
    ErrorReporting,
    Filesystem,
    SampleLibrary,
    Clap,
    SampleLibraryServer,
    Preferences,
    Standalone,
    PresetServer,
};

constexpr String ModuleNameString(ModuleName module_name) {
    switch (module_name) {
        case ModuleName::Global: return "🌍glbl"_s;
        case ModuleName::Main: return "🚀main"_s;
        case ModuleName::Package: return "📦pkg";
        case ModuleName::Gui: return "🖥️gui";
        case ModuleName::ErrorReporting: return "⚠️report";
        case ModuleName::Filesystem: return "📁fs";
        case ModuleName::SampleLibrary: return "📚smpl-lib";
        case ModuleName::Clap: return "👏clap";
        case ModuleName::SampleLibraryServer: return "📚smpl-srv";
        case ModuleName::Preferences: return "⚙️sett";
        case ModuleName::Standalone: return "🧍stand";
        case ModuleName::PresetServer: return "📂prst-srv";
    }
}

using MessageWriteFunction = FunctionRef<ErrorCodeOr<void>(Writer)>;

ErrorCodeOr<void> WriteLogLine(Writer writer,
                               ModuleName module_name,
                               LogLevel level,
                               MessageWriteFunction write_message,
                               WriteLogLineOptions options);

struct LogConfig {
    enum class Destination { Stderr, File };
    Destination destination = Destination::Stderr;
    LogLevel min_level_allowed = PRODUCTION_BUILD ? LogLevel::Info : LogLevel::Debug;
};

ErrorCodeOr<void> CleanupOldLogFilesIfNeeded(ArenaAllocator& scratch_arena);

void Log(ModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message);

template <typename... Args>
void Log(ModuleName module_name, LogLevel level, String format, Args const&... args) {
    Log(module_name, level, [&](Writer writer) { return fmt::FormatToWriter(writer, format, args...); });
}

// thread-safe, not signal-safe
// Returns log message strings in the order they were written.
LogRingBuffer::Snapshot GetLatestLogMessages();

void InitLogger(LogConfig);
void ShutdownLogger();

// TODO: remove Trace
void Trace(ModuleName module_name, String message = {}, SourceLocation loc = SourceLocation::Current());

// A macro unfortunatly seems the best way to avoid repeating the same code while keep template
// instantiations low (needed for fast compile times)
#define DECLARE_LOG_FUNCTION(level)                                                                          \
    template <typename... Args>                                                                              \
    void Log##level(ModuleName module_name, String format, Args const&... args) {                            \
        if constexpr (sizeof...(args) == 0) {                                                                \
            Log(module_name, LogLevel::level, [&](Writer writer) { return writer.WriteChars(format); });     \
        } else {                                                                                             \
            Log(module_name, LogLevel::level, [&](Writer writer) {                                           \
                return fmt::FormatToWriter(writer, format, args...);                                         \
            });                                                                                              \
        }                                                                                                    \
    }

DECLARE_LOG_FUNCTION(Debug)
DECLARE_LOG_FUNCTION(Info)
DECLARE_LOG_FUNCTION(Error)
DECLARE_LOG_FUNCTION(Warning)

#define DBG_PRINT_EXPR(x)     LogDebug({}, "{}: {} = {}", __FUNCTION__, #x, x)
#define DBG_PRINT_EXPR2(x, y) LogDebug({}, "{}: {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y)
#define DBG_PRINT_EXPR3(x, y, z)                                                                             \
    LogDebug({}, "{}: {} = {}, {} = {}, {} = {}", __FUNCTION__, #x, x, #y, y, #z, z)
#define DBG_PRINT_STRUCT(x) LogDebug({}, "{}: {} = {}", __FUNCTION__, #x, fmt::DumpStruct(x))
