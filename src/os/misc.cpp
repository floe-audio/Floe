// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/misc.hpp"

#include "tests/framework.hpp"
#ifdef _WIN32
#include <windows.h>

#include "os/undef_windows_macros.h"
#endif
#include <string.h> // strerror
#include <time.h> // tests

#include "os/threading.hpp"

static constexpr ErrorCodeCategory k_errno_category {
    .category_id = "PX",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
#ifdef _WIN32
        char buffer[200];
        auto strerror_return_code = strerror_s(buffer, ArraySize(buffer), (int)code.code);
        if (strerror_return_code != 0) {
            PanicIfReached();
            return fmt::FormatToWriter(writer, "strerror failed: {}", strerror_return_code);
        } else {
            if (buffer[0] != '\0') buffer[0] = ToUppercaseAscii(buffer[0]);
            return writer.WriteChars(FromNullTerminated(buffer));
        }
#elif IS_LINUX
        char buffer[200] {};
        auto const err_str = strerror_r((int)code.code, buffer, ArraySize(buffer));
        if (buffer[0] != '\0') buffer[0] = ToUppercaseAscii(buffer[0]);
        return writer.WriteChars(FromNullTerminated(err_str ? err_str : buffer));
#elif IS_MACOS
        char buffer[200] {};
        auto _ = strerror_r((int)code.code, buffer, ArraySize(buffer));
        if (buffer[0] != '\0') buffer[0] = ToUppercaseAscii(buffer[0]);
        return writer.WriteChars(FromNullTerminated(buffer));
#endif
    },
};

ErrorCode ErrnoErrorCode(s64 error_code, char const* extra_debug_info, SourceLocation loc) {
    return ErrorCode {k_errno_category, error_code, extra_debug_info, loc};
}

Mutex& StdStreamMutex(StdStream stream) {
    switch (stream) {
        case StdStream::Out: {
            [[clang::no_destroy]] static Mutex out_mutex;
            return out_mutex;
        }
        case StdStream::Err: {
            [[clang::no_destroy]] static Mutex err_mutex;
            return err_mutex;
        }
    }
    PanicIfReached();
}

Writer StdWriter(StdStream stream) {
    Writer result;
    result.SetContained<StdStream>(stream, [](StdStream stream, Span<u8 const> bytes) -> ErrorCodeOr<void> {
        return StdPrint(stream, String {(char const*)bytes.data, bytes.size});
    });
    return result;
}

#if !IS_WINDOWS
bool IsRunningUnderWine() { return false; }
void WindowsRaiseException(u32) {}
#endif

DynamicArrayBounded<char, fmt::k_timestamp_str_size> Timestamp() {
    return fmt::FormatInline<fmt::k_timestamp_str_size>(
        "{}",
        LocalTimeFromNanosecondsSinceEpoch(NanosecondsSinceEpoch()));
}
DynamicArrayBounded<char, fmt::k_timestamp_str_size> TimestampUtc() {
    return fmt::FormatInline<fmt::k_timestamp_str_size>(
        "{}",
        UtcTimeFromNanosecondsSinceEpoch(NanosecondsSinceEpoch()));
}

constexpr auto CountLeapYears(s16 year) {
    // Count years divisible by 4 (including 1970)
    auto const years_div_4 = ((year - 1) / 4) - (1969 / 4);
    // Subtract years divisible by 100
    auto const years_div_100 = ((year - 1) / 100) - (1969 / 100);
    // Add back years divisible by 400
    auto const years_div_400 = ((year - 1) / 400) - (1969 / 400);

    return years_div_4 - years_div_100 + years_div_400;
}

s128 NanosecondsSinceEpoch(DateAndTime const& date) {
    constexpr s32 k_days_before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    constexpr s64 k_nanos_per_second = 1000000000LL;
    constexpr s64 k_nanos_per_minute = k_nanos_per_second * 60LL;
    constexpr s64 k_nanos_per_hour = k_nanos_per_minute * 60LL;
    constexpr s64 k_nanos_per_day = k_nanos_per_hour * 24LL;

    ASSERT(date.IsValid(true));

    s128 result = 0;

    // Calculate total days since epoch using year difference and leap years
    auto const year_diff = date.year - 1970;
    auto const leap_days = CountLeapYears(date.year);
    result = ((year_diff * 365LL) + leap_days) * k_nanos_per_day;

    // Add days from months using lookup table
    result += k_days_before_month[date.months_since_jan] * k_nanos_per_day;

    // Add leap day if we're past February in a leap year
    if (date.months_since_jan > 1 && IsLeapYear(date.year)) result += k_nanos_per_day;

    // Add days in current month
    result += (date.day_of_month - 1) * k_nanos_per_day;

    result += date.hour * k_nanos_per_hour;
    result += date.minute * k_nanos_per_minute;
    result += date.second * k_nanos_per_second;
    result += date.millisecond * 1000000LL;
    result += date.microsecond * 1000LL;
    result += date.nanosecond;

    return result;
}

TEST_CASE(TestEpochTime) {
    auto check_approx = [&](s64 a, s64 b, Optional<s64> wrap_max) {
        auto b_below = b - 1;
        if (wrap_max && b_below < 0) b_below = wrap_max.Value();
        auto b_above = b + 1;
        if (wrap_max && b_above > wrap_max.Value()) b_above = 0;
        CHECK(a == b || a == b_below || a == b_above);
    };

    auto check_against_std = [&](DateAndTime t, tm const& std_time) {
        check_approx(t.year, std_time.tm_year + 1900, {});
        check_approx(t.months_since_jan, std_time.tm_mon, 11);
        check_approx(t.day_of_month, std_time.tm_mday, 31);
        check_approx(t.hour, std_time.tm_hour, 23);
        check_approx(t.minute, std_time.tm_min, 59);
        check_approx(t.second, std_time.tm_sec, 59);
    };

    SUBCASE("local") {
        auto const ns = NanosecondsSinceEpoch();
        auto const t = LocalTimeFromNanosecondsSinceEpoch(ns);

        auto std_time = time(nullptr);
        auto std_local_time = *localtime(&std_time); // NOLINT(concurrency-mt-unsafe)

        check_against_std(t, std_local_time);
    }

    SUBCASE("utc") {
        auto const ns = NanosecondsSinceEpoch();
        auto const t = UtcTimeFromNanosecondsSinceEpoch(ns);

        auto std_time = time(nullptr);
        auto std_utc_time = *gmtime(&std_time); // NOLINT(concurrency-mt-unsafe)
        check_against_std(t, std_utc_time);
    }

    SUBCASE("datetime to ns") {
        DateAndTime dt {
            .year = 1970,
            .months_since_jan = 0,
            .day_of_month = 1,
            .hour = 0,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 0);

        // One day after epoch
        dt = {
            .year = 1970,
            .months_since_jan = 0,
            .day_of_month = 2,
            .hour = 0,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 86400 * (s128)1'000'000'000);

        // Epoch timestamp: 1739464477
        // Date and time (GMT): Thursday, 13 February 2025 16:34:37
        dt = {
            .year = 2025,
            .months_since_jan = 1,
            .day_of_month = 13,
            .hour = 16,
            .minute = 34,
            .second = 37,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 1739464477 * (s128)1'000'000'000);

        // Epoch timestamp: 951755677
        // Date and time (GMT): Monday, 28 February 2000 16:34:37
        dt = {
            .year = 2000,
            .months_since_jan = 1,
            .day_of_month = 28,
            .hour = 16,
            .minute = 34,
            .second = 37,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 951755677 * (s128)1'000'000'000);

        // Epoch timestamp: 951825600
        // Date and time (GMT): Tuesday, 29 February 2000 12:00:00
        dt = {
            .year = 2000,
            .months_since_jan = 1,
            .day_of_month = 29,
            .hour = 12,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 951825600 * (s128)1'000'000'000);
    }

    return k_success;
}

TEST_CASE(TestTimePoint) {
    Stopwatch const sw;

    auto t1 = TimePoint::Now();
    SleepThisThread(1);
    REQUIRE(t1.Raw());
    auto t2 = TimePoint::Now();

    auto us = SecondsToMicroseconds(t2 - t1);
    REQUIRE(us >= 0.0);
    REQUIRE(ApproxEqual(SecondsToMilliseconds(t2 - t1), us / 1000.0, 0.1));
    REQUIRE(ApproxEqual(t2 - t1, us / (1000.0 * 1000.0), 0.1));

    tester.log.Debug("Time has passed: {}", sw);
    return k_success;
}

TEST_CASE(TestLockableSharedMemory) {
    SUBCASE("Basic creation and initialization") {
        constexpr usize k_size = 1024;
        auto mem1 = TRY(CreateLockableSharedMemory("test1"_s, k_size));
        // Check size is correct
        CHECK_EQ(mem1.data.size, k_size);
        // Check memory is zero initialized
        for (usize i = 0; i < k_size; i++)
            CHECK_EQ(mem1.data[i], 0);
    }

    SUBCASE("Multiple opens see same memory") {
        constexpr usize k_size = 1024;
        auto mem1 = TRY(CreateLockableSharedMemory("test2"_s, k_size));
        auto mem2 = TRY(CreateLockableSharedMemory("test2"_s, k_size));

        // Write pattern through first mapping
        LockSharedMemory(mem1);
        for (usize i = 0; i < k_size; i++)
            mem1.data[i] = (u8)(i & 0xFF);
        UnlockSharedMemory(mem1);

        // Verify pattern through second mapping
        LockSharedMemory(mem2);
        for (usize i = 0; i < k_size; i++)
            CHECK_EQ(mem2.data[i], (u8)(i & 0xFF));
        UnlockSharedMemory(mem2);
    }

    return k_success;
}

TEST_CASE(TestOsRandom) {
    CHECK_NEQ(RandomSeed(), 0u);
    return k_success;
}

TEST_CASE(TestGetInfo) {
    GetOsInfo();
    GetSystemStats();
    return k_success;
}

TEST_CASE(TestGetEnvVar) {
    SUBCASE("c string version") {
        auto const v = GetEnvironmentVariable("PATH", tester.scratch_arena);
        CHECK(v);
        CHECK(v->size > 0);
        tester.log.Debug("PATH: {}", v);
    }

    SUBCASE("string version") {
        auto const v = GetEnvironmentVariable("PATH"_s, tester.scratch_arena);
        CHECK(v);
        CHECK(v->size > 0);
        tester.log.Debug("PATH: {}", v);
    }

    SUBCASE("non-existant variable") {
        auto const v = GetEnvironmentVariable("FMNDTEBORPDXCMW"_s, tester.scratch_arena);
        CHECK(!v);
    }

    return k_success;
}

TEST_CASE(TestIsRunningUnderDebugger) {
    auto const r = IsRunningUnderDebugger();
    tester.log.Debug("Is running under debugger: {}", r);
    return k_success;
}

TEST_REGISTRATION(RegisterMiscTests) {
    REGISTER_TEST(TestEpochTime);
    REGISTER_TEST(TestGetEnvVar);
    REGISTER_TEST(TestGetInfo);
    REGISTER_TEST(TestIsRunningUnderDebugger);
    REGISTER_TEST(TestLockableSharedMemory);
    REGISTER_TEST(TestOsRandom);
    REGISTER_TEST(TestTimePoint);
}
