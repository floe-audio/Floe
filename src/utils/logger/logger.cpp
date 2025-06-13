// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "tests/framework.hpp"

static u32 Mask(u32 val) { return val & (LogRingBuffer::k_buffer_size - 1); }

void LogRingBuffer::Write(String message) {
    // We allow indexes to grow continuously until they naturally wrap around. These are the requirements
    // to make this work.
    static_assert(IsPowerOfTwo(k_buffer_size));
    static_assert(UnsignedInt<decltype(write)>);
    static_assert(Same<decltype(write), decltype(read)>);
    // The maximum capacity can only be half the range of the index data types. (So 2^31-1 when using 32
    // bit unsigned integers)
    static_assert(k_buffer_size <= ((1 << ((sizeof(write) * 8) - 1)) - 1));

    if (message.size > k_max_message_size) [[unlikely]]
        message.size = FindUtf8TruncationPoint(message, k_max_message_size);

    if (!mutex.Lock(2000u)) throw PanicException {};
    DEFER { mutex.Unlock(); };

    constexpr usize k_prefix_bytes = 1 + sizeof(u64);

    // if there's no room for this message, we remove the oldest messages until there is room
    while (true) {
        auto const used = (decltype(write))(write - read);
        ASSERT_HOT(used <= buffer.size);
        auto const remaining = buffer.size - used;

        if (remaining >= (message.size + k_prefix_bytes))
            // There's enough space.
            break;

        // Advance the read pointer to remove the oldest message.
        auto const tail_message_size = buffer[Mask(read)];
        read += 1; // message size byte
        read += sizeof(u64); // timestamp
        read += tail_message_size;
    }

    // Write the message size.
    buffer[Mask(write++)] = (u8)message.size;

    // Write the bytes of the u64 timestamp.
    auto const seconds_since_epoch = (u64)MicrosecondsSinceEpoch() / 1'000'000;
    for (usize i = 0; i < sizeof(u64); i++)
        buffer[Mask(write++)] = (u8)(seconds_since_epoch >> (i * 8));

    // Write the message.
    for (auto c : message)
        buffer[Mask(write++)] = (u8)c;
}

LogRingBuffer::Snapshot LogRingBuffer::TakeSnapshot() {
    mutex.Lock();
    DEFER { mutex.Unlock(); };

    Snapshot snapshot {};

    dyn::Resize(snapshot.buffer, write - read);

    // We copy it so that there is no ring-buffer wrap-around issues.
    u32 out_index = 0;
    auto pos = read;
    while (pos != write)
        snapshot.buffer[out_index++] = buffer[Mask(pos++)];

    return snapshot;
}

Optional<LogRingBuffer::Message> LogRingBuffer::Snapshot::Next(usize& pos) const {
    if (pos >= buffer.size) return k_nullopt;

    auto const message_size = (u8)buffer[pos++];
    u64 seconds_since_epoch = 0;
    for (usize i = 0; i < sizeof(u64); i++)
        seconds_since_epoch |= ((u64)buffer[pos++]) << (i * 8);

    auto const message_start = pos;
    pos += message_size;

    return LogRingBuffer::Message {
        .seconds_since_epoch = seconds_since_epoch,
        .message = String {(char const*)(u8 const*)buffer.data + message_start, message_size},
    };
}

void LogRingBuffer::Reset() {
    mutex.Lock();
    DEFER { mutex.Unlock(); };
    write = 0;
    read = 0;
}

ErrorCodeOr<void> WriteLogLine(Writer writer,
                               ModuleName module_name,
                               LogLevel level,
                               MessageWriteFunction write_message,
                               WriteLogLineOptions options) {
    bool needs_space = false;
    bool needs_open_bracket = true;

    auto const begin_prefix_item = [&]() -> ErrorCodeOr<void> {
        char buf[2];
        usize len = 0;
        if (Exchange(needs_open_bracket, false)) buf[len++] = '[';
        if (Exchange(needs_space, true)) buf[len++] = ' ';
        if (len) TRY(writer.WriteChars({buf, len}));
        return k_success;
    };

    if (options.timestamp) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(Timestamp()));
    }

    TRY(begin_prefix_item());
    TRY(writer.WriteChars(ModuleNameString(module_name)));

    if (!(options.no_info_prefix && level == LogLevel::Info)) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(({
            String s;
            switch (level) {
                case LogLevel::Debug:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_BLUE("debug")) : "debug";
                    break;
                case LogLevel::Info: s = "info"; break;
                case LogLevel::Warning:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_YELLOW("warning")) : "warning";
                    break;
                case LogLevel::Error:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_RED("error")) : "error";
            }
            s;
        })));
    }

    if (options.thread) {
        TRY(begin_prefix_item());
        if (auto const thread_name = ThreadName(false))
            TRY(writer.WriteChars(*thread_name));
        else
            TRY(writer.WriteChars(fmt::IntToString(CurrentThreadId(),
                                                   fmt::IntToStringOptions {
                                                       .base = fmt::IntToStringOptions::Base::Hexadecimal,
                                                   })));
    }

    auto const prefix_was_written = !needs_open_bracket;

    if (prefix_was_written) TRY(writer.WriteChars("] "));
    TRY(write_message(writer));
    if (options.newline) TRY(writer.WriteChar('\n'));
    return k_success;
}

void Trace(ModuleName module_name, String message, SourceLocation loc) {
    Log(module_name, LogLevel::Debug, [&](Writer writer) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer,
                                "trace: {}({}): {}",
                                FromNullTerminated(loc.file),
                                loc.line,
                                loc.function));
        if (message.size) TRY(fmt::FormatToWriter(writer, ": {}", message));
        return k_success;
    });
}

constexpr auto k_log_extension = ".log"_ca;
constexpr auto k_latest_log_filename = ConcatArrays("latest"_ca, k_log_extension);

ErrorCodeOr<void> CleanupOldLogFilesIfNeeded(ArenaAllocator& scratch_arena) {
    InitLogFolderIfNeeded();

    constexpr usize k_max_log_files = 10;

    auto const entries =
        TRY(FindEntriesInFolder(scratch_arena,
                                *LogFolder(),
                                {
                                    .options =
                                        {
                                            .wildcard = ConcatArrays("*"_ca, k_log_extension),
                                        },
                                    .recursive = false,
                                    .only_file_type = FileType::File,
                                }));
    if (entries.size <= k_max_log_files) return k_success;

    struct Entry {
        dir_iterator::Entry const* entry;
        s128 last_modified;
    };
    DynamicArray<Entry> entries_with_last_modified {scratch_arena};

    for (auto const& entry : entries) {
        if (entry.subpath == k_latest_log_filename) continue;

        auto const full_path = path::Join(scratch_arena, Array {*LogFolder(), entry.subpath});
        DEFER { scratch_arena.Free(full_path.ToByteSpan()); };

        // NOTE: the last modified time won't actually refer to the time that the file was written to, but
        // when it was renamed. But that's still a good enough approximation.
        auto const last_modified = TRY(LastModifiedTimeNsSinceEpoch(full_path));

        dyn::Append(entries_with_last_modified, Entry {&entry, last_modified});
    }

    if (entries_with_last_modified.size <= k_max_log_files) return k_success;

    Sort(entries_with_last_modified,
         [](Entry const& a, Entry const& b) { return a.last_modified < b.last_modified; });

    for (auto i : Range(entries_with_last_modified.size - k_max_log_files)) {
        auto const entry = entries_with_last_modified[i];
        auto const full_path = path::Join(scratch_arena, Array {*LogFolder(), entry.entry->subpath});
        DEFER { scratch_arena.Free(full_path.ToByteSpan()); };
        LogDebug(ModuleName::Global, "deleting old log file: {}"_s, full_path);
        auto _ = Delete(full_path, {.type = DeleteOptions::Type::File});
    }

    return k_success;
}

static CountedInitFlag g_counted_init_flag {};
static CallOnceFlag g_call_once_flag {};
alignas(File) static u8 g_file_storage[sizeof(File)];
static File* g_file = nullptr;
static LogConfig g_config {};
static LogRingBuffer g_message_ring_buffer {};

void InitLogger(LogConfig config) {
    ZoneScoped;
    CountedInit(g_counted_init_flag, [&]() { g_config = config; });
}

void ShutdownLogger() {
    ZoneScoped;
    CountedDeinit(g_counted_init_flag, []() {
        if (auto file = Exchange(g_file, nullptr)) file->~File();
        g_call_once_flag.Reset();
    });
}

LogRingBuffer::Snapshot GetLatestLogMessages() { return g_message_ring_buffer.TakeSnapshot(); }

void Log(ModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
    if (level < g_config.min_level_allowed) return;

    // Info, warnings and errors should be added to the ring buffer. We can access these when we report errors
    // online.
    if (level > LogLevel::Debug) {
        DynamicArrayBounded<char, LogRingBuffer::k_max_message_size> message;
        auto _ = WriteLogLine(dyn::WriterFor(message),
                              module_name,
                              level,
                              write_message,
                              {
                                  .ansi_colors = false,
                                  .no_info_prefix = true,
                                  .timestamp = false,
                                  .thread = true,
                                  .newline = false,
                              });
        g_message_ring_buffer.Write(message);
    }

    if (level == LogLevel::Debug) {
        DynamicArrayBounded<char, Kb(8)> message;
        auto const o = write_message(dyn::WriterFor(message));
        if (o.Succeeded()) TracyMessage(message.data, message.size);
    }

    // For debugging purposes, we also log to a file or stderr.
    if constexpr (!PRODUCTION_BUILD) {
        static auto log_to_stderr =
            [](ModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
                constexpr WriteLogLineOptions k_config {
                    .ansi_colors = true,
                    .no_info_prefix = false,
                    .timestamp = true,
                    .thread = true,
                };
                auto& mutex = StdStreamMutex(StdStream::Err);
                mutex.Lock();
                DEFER { mutex.Unlock(); };

                BufferedWriter<Kb(4)> buffered_writer {StdWriter(StdStream::Err)};
                DEFER { buffered_writer.FlushReset(); };

                auto _ = WriteLogLine(buffered_writer.Writer(), module_name, level, write_message, k_config);
            };

        switch (g_config.destination) {
            case LogConfig::Destination::Stderr: {
                log_to_stderr(module_name, level, write_message);
                break;
            }
            case LogConfig::Destination::File: {
                CallOnce(g_call_once_flag, []() {
                    ASSERT(g_file == nullptr);
                    InitLogFolderIfNeeded();

                    auto seed = RandomSeed();
                    ArenaAllocatorWithInlineStorage<500> arena {PageAllocator::Instance()};

                    auto const log_folder = *LogFolder();
                    ASSERT(IsValidUtf8(log_folder));

                    auto const standard_path = path::Join(arena, Array {log_folder, k_latest_log_filename});
                    ASSERT(IsValidUtf8(standard_path));

                    // We have a few requirements here:
                    // - If possible, we want a log file with a fixed name so that it's easier to find and
                    //   use for debugging.
                    // - We don't want to overwrite any log files.
                    // - We need to correctly handle the case where other processes are running this same
                    //   code at the same time; this can happen when the host loads plugins in different
                    //   processes.
                    for (auto _ : Range(50)) {
                        // Try opening the file with exclusive access.
                        auto file_outcome = OpenFile(
                            standard_path,
                            {
                                .capability = FileMode::Capability::Append,
                                .win32_share = FileMode::Share::DeleteRename | FileMode::Share::ReadWrite,
                                .creation = FileMode::Creation::CreateNew, // Exclusive access
                            });
                        if (file_outcome.HasError()) {
                            if (file_outcome.Error() == FilesystemError::PathAlreadyExists) {
                                // We try to oust the standard log file by renaming it to a unique name.
                                // Rename is atomic. If another process is already using the log file, they
                                // will continue to do so safely, but it will be under the new name.
                                auto const unique_path =
                                    path::Join(arena,
                                               Array {log_folder, UniqueFilename("", k_log_extension, seed)});
                                ASSERT(IsValidUtf8(unique_path));
                                auto const rename_o = Rename(standard_path, unique_path);
                                if (rename_o.Succeeded()) {
                                    // We successfully renamed the file. Now let's try opening it again.
                                    continue;
                                } else {
                                    if (rename_o.Error() == FilesystemError::PathDoesNotExist) {
                                        // The file was deleted between our OpenFile and Rename calls. Let's
                                        // try again.
                                        continue;
                                    }

                                    StdPrintFLocked(StdStream::Err,
                                                    "{} failed to rename log file: {}\n",
                                                    CurrentThreadId(),
                                                    rename_o.Error());
                                    return;
                                }
                            }

                            // Some other error occurred, not much we can do.
                            StdPrintFLocked(StdStream::Err,
                                            "{} failed to open log file: {}\n",
                                            CurrentThreadId(),
                                            file_outcome.Error());
                            return;
                        }

                        auto file = PLACEMENT_NEW(g_file_storage) File {file_outcome.ReleaseValue()};
                        g_file = file;
                        return;
                    }

                    StdPrintFLocked(StdStream::Err,
                                    "{} failed to open log file: too many attempts\n",
                                    CurrentThreadId());
                    return;
                });

                auto file = g_file;

                if (!file) {
                    log_to_stderr(module_name, level, write_message);
                    return;
                }

                BufferedWriter<Kb(4)> buffered_writer {file->Writer()};
                DEFER {
                    auto outcome = buffered_writer.Flush();
                    if (outcome.HasError()) {
                        log_to_stderr(ModuleName::Global, LogLevel::Error, [outcome](Writer writer) {
                            return fmt::FormatToWriter(writer,
                                                       "defer flush failed to write log file: {}"_s,
                                                       outcome.Error());
                        });
                    }
                    // We've done what we can with the outcome, let's not trigger any assertion.
                    buffered_writer.Reset();
                };

                auto o = WriteLogLine(buffered_writer.Writer(),
                                      module_name,
                                      level,
                                      write_message,
                                      {
                                          .ansi_colors = false,
                                          .no_info_prefix = false,
                                          .timestamp = true,
                                          .thread = true,
                                      });
                if (o.HasError()) {
                    log_to_stderr(ModuleName::Global, LogLevel::Error, [o](Writer writer) {
                        return fmt::FormatToWriter(writer, "failed to write log file: {}"_s, o.Error());
                    });
                }

                break;
            }
        }
    }
}

TEST_CASE(TestLogRingBuffer) {
    LogRingBuffer ring;
    LogRingBuffer::Snapshot snapshot;
    usize count = 0;
    usize pos = 0;

    SUBCASE("basics") {
        snapshot = ring.TakeSnapshot();
        CHECK_EQ(snapshot.buffer.size, 0u);

        ring.Write("hello");
        snapshot = ring.TakeSnapshot();
        count = 0;
        pos = 0;
        while (true) {
            auto const message = snapshot.Next(pos);
            if (!message) break;
            CHECK_EQ(message->message, "hello"_s);
            ++count;
        }
        CHECK_EQ(count, 1u);

        ring.Reset();
        snapshot = ring.TakeSnapshot();
        CHECK_EQ(snapshot.buffer.size, 0u);

        ring.Write("world");
        snapshot = ring.TakeSnapshot();
        count = 0;
        pos = 0;
        while (true) {
            auto const message = snapshot.Next(pos);
            if (!message) break;
            CHECK_EQ(message->message, "world"_s);
            ++count;
        }
        CHECK_EQ(count, 1u);

        ring.Write("hello");
        count = 0;
        pos = 0;
        snapshot = ring.TakeSnapshot();
        while (true) {
            auto const message = snapshot.Next(pos);
            if (!message) break;
            switch (count) {
                case 0: CHECK_EQ(message->message, "world"_s); break;
                case 1: CHECK_EQ(message->message, "hello"_s); break;
                default: CHECK(false);
            }
            ++count;
        }
        CHECK_EQ(count, 2u);
    }

    SUBCASE("wrap") {
        for (auto _ : Range(1000))
            ring.Write("abcdefghijklmnopqrstuvwxyz");
        snapshot = ring.TakeSnapshot();
        while (true) {
            auto const message = snapshot.Next(pos);
            if (!message) break;
            CHECK_EQ(message->message, "abcdefghijklmnopqrstuvwxyz"_s);
        }
    }

    SUBCASE("randomly add strings") {
        u64 seed = RandomSeed();
        for (auto _ : Range(1000)) {
            DynamicArrayBounded<char, 32> string;
            auto const string_size = RandomIntInRange<u32>(seed, 1, string.Capacity() - 1);
            for (auto _ : Range(string_size)) {
                auto const c = RandomIntInRange<char>(seed, 'a', 'z');
                dyn::AppendAssumeCapacity(string, c);
            }
            ring.Write(string);
        }
        snapshot = ring.TakeSnapshot();
    }

    SUBCASE("add too long string") {
        auto const str =
            tester.arena.AllocateExactSizeUninitialised<char>(LogRingBuffer::k_max_message_size + 1);
        FillMemory(str.data, 'a', str.size);
        ring.Write({str.data, str.size});
        snapshot = ring.TakeSnapshot();
        while (true) {
            auto const message = snapshot.Next(pos);
            if (!message) break;
            CHECK_EQ(message->message.size, LogRingBuffer::k_max_message_size);
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterLogRingBufferTests) { REGISTER_TEST(TestLogRingBuffer); }
