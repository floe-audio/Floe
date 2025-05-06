// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/misc.hpp"
#ifdef _WIN32
#include <windows.h>

#include "os/undef_windows_macros.h"
#endif
#include <string.h> // strerror

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
