// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdlib.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

bool FillDistributionInfo(OsInfo& info, String filename) {
    auto const file_data = TRY_OR(ReadEntireFile(filename, PageAllocator::Instance()), return false);
    DEFER { PageAllocator::Instance().Free(file_data.ToByteSpan()); };

    for (auto const line : SplitIterator {file_data, '\n'}) {
        auto const equals_pos = Find(line, '=');
        if (!equals_pos) continue;

        auto const key = WhitespaceStripped(line.SubSpan(0, *equals_pos));
        auto value = WhitespaceStripped(line.SubSpan(*equals_pos + 1));
        value = TrimEndIfMatches(value, '"');
        value = TrimStartIfMatches(value, '"');

        if (key == "PRETTY_NAME")
            dyn::AssignFitInCapacity(info.distribution_pretty_name, value);
        else if (key == "ID")
            dyn::AssignFitInCapacity(info.distribution_name, value);
        else if (key == "VERSION_ID")
            dyn::AssignFitInCapacity(info.distribution_version, value);
    }

    return true;
}

// This code is based on Sentry's Native SDK
// Copyright (c) 2019 Sentry (https://sentry.io) and individual contributors.
// SPDX-License-Identifier: MIT
OsInfo GetOsInfo() {
    OsInfo result {};

    if (!FillDistributionInfo(result, "/etc/os-release")) FillDistributionInfo(result, "/usr/lib/os-release");

    struct utsname uts {};
    if (uname(&uts) == 0) {
        char const* release = uts.release;
        size_t num_dots = 0;
        for (; release[0] != '\0'; release++) {
            char c = release[0];
            if (c == '.') num_dots += 1;
            if (!(c >= '0' && c <= '9') && (c != '.' || num_dots > 2)) break;
        }
        auto release_start = release;
        if (release[0] == '-' || release[0] == '.') release_start++;

        if (release_start[0] != '\0')
            dyn::AssignFitInCapacity(result.build, FromNullTerminated(release_start));

        dyn::AssignFitInCapacity(result.name, FromNullTerminated((char const*)uts.sysname));
        dyn::AssignFitInCapacity(result.version, FromNullTerminated((char const*)uts.release));
    }

    if (!result.name.size) result.name = "Linux"_s;

    return result;
}

String GetFileBrowserAppName() { return "File Explorer"; }

bool FillCpuInfo(SystemStats& stats, char const* filename) {
    auto fd = open(filename, O_RDONLY);
    if (fd == -1) return false;
    DEFER { close(fd); };

    constexpr usize k_max_file_size = Kb(16);
    DynamicArrayBounded<char, k_max_file_size> file_data {};

    auto const num_read = read(fd, file_data.data, k_max_file_size);
    if (num_read == -1) return false;
    file_data.size = (usize)num_read;

    for (auto const line : SplitIterator {file_data, '\n'}) {
        auto const colon_pos = Find(line, ':');
        if (!colon_pos) continue;

        auto const key = WhitespaceStripped(line.SubSpan(0, *colon_pos));
        auto value = WhitespaceStripped(line.SubSpan(*colon_pos + 1));

        if (key == "model name")
            stats.cpu_name = value;
        else if (key == "cpu MHz")
            if (auto const mhz = ParseFloat(value)) stats.frequency_mhz = *mhz;
    }

    return true;
}

SystemStats GetSystemStats() {
    SystemStats result {};
    if (!result.page_size) {
        result.num_logical_cpus = (u32)sysconf(_SC_NPROCESSORS_ONLN);
        result.page_size = (u32)sysconf(_SC_PAGESIZE);
        ASSERT(result.num_logical_cpus);
        ASSERT(result.page_size);
        FillCpuInfo(result, "/proc/cpuinfo");
    }
    return result;
}

bool IsRunningUnderDebugger() {
    auto fd = open("/proc/self/status", O_RDONLY);
    if (fd == -1) return false;
    DEFER { close(fd); };

    constexpr usize k_max_file_size = Kb(4);
    DynamicArrayBounded<char, k_max_file_size> file_data {};
    auto const num_read = read(fd, file_data.data, k_max_file_size);
    if (num_read == -1) return false;
    file_data.size = (usize)num_read;

    for (auto const line : SplitIterator {file_data, '\n'}) {
        auto const colon_pos = Find(line, ':');
        if (!colon_pos) continue;

        auto const key = WhitespaceStripped(line.SubSpan(0, *colon_pos));
        auto value = WhitespaceStripped(line.SubSpan(*colon_pos + 1));

        if (key == "TracerPid") return value[0] != '0';
    }

    return false;
}

u64 RandomSeed() {
    u64 seed = 0;
    auto _ = getrandom(&seed, sizeof(seed), 0);
    return seed;
}

void OpenFolderInFileBrowser(String path) {
    PathArena path_allocator {Malloc::Instance()};
    // IMPROVE: system is not thread-safe?
    auto _ =
        ::system(fmt::Format(path_allocator, "xdg-open {}\0", path).data); // NOLINT(concurrency-mt-unsafe)
}

void OpenUrlInBrowser(String url) {
    ArenaAllocatorWithInlineStorage<200> arena {Malloc::Instance()};
    // IMPROVE: system is not thread-safe?
    auto _ = ::system(fmt::Format(arena, "xdg-open {}\0", url).data); // NOLINT(concurrency-mt-unsafe)
}
