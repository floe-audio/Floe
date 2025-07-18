// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sentry.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

#include "error_reporting.hpp"
#include "preferences.hpp"

namespace sentry {

String DeviceIdPath(ArenaAllocator& arena, bool create) {
    return KnownDirectoryWithSubdirectories(arena,
                                            KnownDirectoryType::UserData,
                                            Array {"Floe"_s},
                                            "device_id",
                                            {.create = create});
}

// A random string that we save to disk to identify if errors occur for multiple 'users'.
static Optional<fmt::UuidArray> DeviceId(Atomic<u64>& seed) {
    PathArena path_arena {PageAllocator::Instance()};
    auto const path = DeviceIdPath(path_arena, true);

    auto file = TRY_OR(OpenFile(path, FileMode::ReadWrite()), {
        LogError(ModuleName::ErrorReporting, "Failed to create device_id file: {}", error);
        return k_nullopt;
    });

    TRY_OR(file.Lock({.type = FileLockOptions::Type::Exclusive}), {
        LogError(ModuleName::ErrorReporting, "Failed to lock device_id file: {}", error);
        return k_nullopt;
    });
    DEFER { auto _ = file.Unlock(); };

    auto const size = TRY_OR(file.FileSize(), {
        LogError(ModuleName::ErrorReporting, "Failed to get size of device_id file: {}", error);
        return k_nullopt;
    });

    if (size == fmt::k_uuid_size) {
        fmt::UuidArray uuid {};
        TRY_OR(file.Read(uuid.data, uuid.size), {
            LogError(ModuleName::ErrorReporting, "Failed to read device_id file: {}", error);
            return k_nullopt;
        });

        bool valid = true;
        for (auto c : uuid) {
            if (!IsHexDigit(c)) {
                valid = false;
                break;
            }
        }
        if (valid) return uuid;
    }

    // File is invalid or empty, let's recreate it
    auto const uuid = detail::Uuid(seed);

    TRY_OR(file.Seek(0, File::SeekOrigin::Start),
           { LogError(ModuleName::ErrorReporting, "Failed to seek device_id file: {}", error); });

    TRY_OR(file.Truncate(0),
           { LogError(ModuleName::ErrorReporting, "Failed to truncate device_id file: {}", error); });

    TRY_OR(file.Write(uuid),
           { LogError(ModuleName::ErrorReporting, "Failed to write device_id file: {}", error); });

    auto _ = file.Flush();

    return uuid;
}

static void CheckDsn(DsnInfo dsn) {
    ASSERT(dsn.dsn.size);
    ASSERT(dsn.host.size);
    ASSERT(dsn.project_id.size);
    ASSERT(dsn.public_key.size);
}

static void CheckTags(Span<Tag const> tags) {
    ASSERT(tags.size < 20);
    for (auto const& tag : tags) {
        ASSERT(tag.key.size);
        ASSERT(tag.value.size);
        ASSERT(tag.key.size < 200);
        ASSERT(tag.value.size < 200);
    }
}

static MutableString CreateJsonBlob(Sentry& sentry,
                                    FunctionRef<ErrorCodeOr<void>(json::WriteContext& writer)> write_func) {
    DynamicArray<char> blob {sentry.arena};
    blob.Reserve(1024);
    json::WriteContext json_writer {
        .out = dyn::WriterFor(blob),
        .add_whitespace = false,
    };
    auto _ = json::WriteObjectBegin(json_writer);
    auto _ = write_func(json_writer);
    auto _ = json::WriteObjectEnd(json_writer);

    // we want to be able to print the context directly into an existing JSON event so we remove the
    // object braces
    dyn::Pop(blob);
    dyn::Remove(blob, 0);

    return blob.ToOwnedSpan();
}

// not thread-safe, dsn must be static, tags are cloned
static void InitSentry(Sentry& sentry, DsnInfo dsn, Span<Tag const> tags) {
    CheckDsn(dsn);
    CheckTags(tags);

    sentry.dsn = dsn;
    sentry.seed.Store(RandomSeed(), StoreMemoryOrder::Relaxed);
    sentry.session_id = detail::Uuid(sentry.seed);
    sentry.device_id = DeviceId(sentry.seed);
    sentry.online_reporting_disabled.Store(IsOnlineReportingDisabled(), StoreMemoryOrder::Relaxed);

    // clone the tags
    ASSERT_EQ(sentry.tags.size, 0u);
    sentry.tags = sentry.arena.Clone(tags, CloneType::Deep);

    if (sentry.device_id) {
        sentry.user_context_json =
            CreateJsonBlob(sentry, [&sentry](json::WriteContext& json) -> ErrorCodeOr<void> {
                TRY(json::WriteKeyObjectBegin(json, "user"));
                TRY(json::WriteKeyValue(json, "id", *sentry.device_id));
                TRY(json::WriteObjectEnd(json));
                return k_success;
            });
    }

    sentry.device_context_json = CreateJsonBlob(sentry, [](json::WriteContext& json) -> ErrorCodeOr<void> {
        auto const system = CachedSystemStats();
        TRY(json::WriteKeyObjectBegin(json, "device"));
        TRY(json::WriteKeyValue(json, "name", "desktop"));
        TRY(json::WriteKeyValue(json, "arch", system.Arch()));
        TRY(json::WriteKeyValue(json, "cpu_description", system.cpu_name));
        TRY(json::WriteKeyValue(json, "processor_count", system.num_logical_cpus));
        TRY(json::WriteKeyValue(json, "processor_frequency", system.frequency_mhz));
        TRY(json::WriteKeyValue(json, "memory_size", system.total_ram_bytes));
        TRY(json::WriteObjectEnd(json));
        return k_success;
    });

    sentry.os_context_json = CreateJsonBlob(sentry, [](json::WriteContext& json_writer) -> ErrorCodeOr<void> {
        auto const os = GetOsInfo();
        TRY(json::WriteKeyObjectBegin(json_writer, "os"));
        TRY(json::WriteKeyValue(json_writer, "name", os.name));
        if (os.version.size) TRY(json::WriteKeyValue(json_writer, "version", os.version));
        if (os.build.size) TRY(json::WriteKeyValue(json_writer, "build", os.build));
        if (os.kernel_version.size)
            TRY(json::WriteKeyValue(json_writer, "kernel_version", os.kernel_version));
        if (os.pretty_name.size) TRY(json::WriteKeyValue(json_writer, "pretty_name", os.pretty_name));
        if (os.distribution_name.size)
            TRY(json::WriteKeyValue(json_writer, "distribution_name", os.distribution_name));
        if (os.distribution_version.size)
            TRY(json::WriteKeyValue(json_writer, "distribution_version", os.distribution_version));
        if (os.distribution_pretty_name.size)
            TRY(json::WriteKeyValue(json_writer, "distribution_pretty_name", os.distribution_pretty_name));
        TRY(json::WriteObjectEnd(json_writer));
        return k_success;
    });
}

alignas(Sentry) static u8 g_sentry_storage[sizeof(Sentry)];
static Atomic<Sentry*> g_instance {nullptr};

Sentry* GlobalSentry() { return g_instance.Load(LoadMemoryOrder::Acquire); }

Sentry& InitGlobalSentry(DsnInfo dsn, Span<Tag const> tags) {
    auto existing = g_instance.Load(LoadMemoryOrder::Acquire);
    if (existing) return *existing;

    auto sentry = PLACEMENT_NEW(g_sentry_storage) Sentry {};
    InitSentry(*sentry, dsn, tags);

    g_instance.Store(sentry, StoreMemoryOrder::Release);
    return *sentry;
}

// signal-safe
void InitBarebonesSentry(Sentry& sentry) {
    sentry.seed.Store((u64)MicrosecondsSinceEpoch() + __builtin_readcyclecounter(),
                      StoreMemoryOrder::Relaxed);
    sentry.session_id = detail::Uuid(sentry.seed);

    sentry.device_context_json = CreateJsonBlob(sentry, [](json::WriteContext& json) -> ErrorCodeOr<void> {
        TRY(json::WriteKeyObjectBegin(json, "device"));
        TRY(json::WriteKeyValue(json, "name", "desktop"));
        TRY(json::WriteKeyValue(json, "arch", ({
                                    String s;
                                    switch (k_arch) {
                                        case Arch::X86_64: s = "x86_64"; break;
                                        case Arch::Aarch64: s = "aarch64"; break;
                                    }
                                    s;
                                })));
        TRY(json::WriteObjectEnd(json));

        return k_success;
    });

    sentry.os_context_json = CreateJsonBlob(sentry, [](json::WriteContext& json) -> ErrorCodeOr<void> {
        TRY(json::WriteKeyObjectBegin(json, "os"));
        TRY(json::WriteKeyValue(json, "name", ({
                                    String s;
                                    if (IS_WINDOWS) s = "Windows";
                                    if (IS_LINUX) s = "Linux";
                                    if (IS_MACOS) s = "macOS";
                                    s;
                                })));
        TRY(json::WriteObjectEnd(json));

        return k_success;
    });
}

TEST_CASE(TestSentry) {
    SUBCASE("Parse DSN") {
        auto o = ParseDsn("https://publickey@host.com/123");
        REQUIRE(o.HasValue());
        CHECK_EQ(o->dsn, "https://publickey@host.com/123"_s);
        CHECK_EQ(o->host, "host.com"_s);
        CHECK_EQ(o->project_id, "123"_s);
        CHECK_EQ(o->public_key, "publickey"_s);

        CHECK(!ParseDsn("https://host.com/123"));
        CHECK(!ParseDsn("https://publickey@host.com"));
        CHECK(!ParseDsn("  "));
        CHECK(!ParseDsn(""));
    }

    SUBCASE("Basics") {
        auto& sentry = InitGlobalSentry(ParseDsnOrThrow(k_dsn), {});
        CHECK(sentry.device_id);

        // build an envelope
        DynamicArray<char> envelope {tester.scratch_arena};
        EnvelopeWriter writer = {.writer = dyn::WriterFor(envelope)};
        TRY(EnvelopeAddHeader(sentry, writer, true));
        TRY(EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::Ok));
        TRY(EnvelopeAddEvent(sentry,
                             writer,
                             {
                                 .level = ErrorEvent::Level::Info,
                                 .message = "Test event message"_s,
                                 .stacktrace = CurrentStacktrace(),
                                 .tags = ArrayT<Tag>({
                                     {"tag1", "value1"},
                                     {"tag2", "value2"},
                                 }),
                             },
                             {
                                 .signal_safe = true,
                                 .diagnostics = true,
                             }));
        TRY(EnvelopeAddSessionUpdate(sentry, writer, SessionStatus::EndedNormally));

        CHECK(envelope.size > 0);
    }

    return k_success;
}

} // namespace sentry

TEST_REGISTRATION(RegisterSentryTests) { REGISTER_TEST(sentry::TestSentry); }
