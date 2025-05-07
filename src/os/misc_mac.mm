// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "os/misc_mac.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wcast-align"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#pragma clang diagnostic pop

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/threading.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"

static ErrorCodeOr<void> ConvertToStringWithNSDomain(NSErrorDomain domain, s64 code, Writer const& writer) {
    // We haven't saved the userInfo from the original source (as we converted the error code into a plain
    // integer to fit in our Error struct). However, the documentation says that if there is no userInfo, "a
    // default string is constructed from the domain and code" for the localizedDescription, so we should be
    // ok.
    auto nserr = [NSError errorWithDomain:domain code:(NSInteger)code userInfo:nil];
    auto const error_text = nserr.localizedDescription.UTF8String;
    return writer.WriteChars(FromNullTerminated(error_text));
}

static constexpr ErrorCodeCategory k_cocoa_category {
    .category_id = "CC",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSCocoaErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_mach_category {
    .category_id = "MC",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSMachErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_os_status_category {
    .category_id = "OS",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSOSStatusErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_url_category {
    .category_id = "UR",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return ConvertToStringWithNSDomain(NSURLErrorDomain, code.code, writer);
    }};

static constexpr ErrorCodeCategory k_unknown_category {
    .category_id = "UN",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return fmt::FormatToWriter(writer, "Mac error (no domain): {}", code.code);
    }};

ErrorCode ErrorFromNSError(NSError* error, char const* extra_debug_info, SourceLocation loc) {
    ASSERT(error != nil);
    ErrorCodeCategory const* code_type = nullptr;
    if ([error.domain isEqualToString:NSCocoaErrorDomain])
        code_type = &k_cocoa_category;
    else if ([error.domain isEqualToString:NSPOSIXErrorDomain])
        return ErrnoErrorCode(error.code, extra_debug_info, loc);
    else if ([error.domain isEqualToString:NSOSStatusErrorDomain])
        code_type = &k_os_status_category;
    else if ([error.domain isEqualToString:NSMachErrorDomain])
        code_type = &k_mach_category;
    else if ([error.domain isEqualToString:NSURLErrorDomain])
        code_type = &k_url_category;
    else
        code_type = &k_unknown_category;
    return {*code_type, (s64)error.code, extra_debug_info, loc};
}

OsInfo GetOsInfo() {
    OsInfo result {
        .name = "macOS"_s,
    };

    // This code is based on Sentry's Native SDK
    // Copyright (c) 2019 Sentry (https://sentry.io) and individual contributors.
    // SPDX-License-Identifier: MIT
    usize size = result.version.Capacity();
    if (sysctlbyname("kern.osproductversion", result.version.data, &size, nullptr, 0) == 0) {
        result.version.size = NullTerminatedSize(result.version.data);
        auto const num_dots = Count(result.version, '.');
        if (num_dots < 2) dyn::AppendSpan(result.version, ".0");
    }

    size = result.build.Capacity();
    if (sysctlbyname("kern.osversion", result.build.data, &size, nullptr, 0) == 0)
        result.build.size = NullTerminatedSize(result.build.data);

    struct utsname uts {};
    if (uname(&uts) == 0)
        dyn::AssignFitInCapacity(result.kernel_version, FromNullTerminated((char const*)uts.release));

    return result;
}

String GetFileBrowserAppName() { return "Finder"; }

SystemStats GetSystemStats() {
    SystemStats result {};

    result.num_logical_cpus = (u32)[[NSProcessInfo processInfo] activeProcessorCount];
    ASSERT(result.num_logical_cpus);
    result.page_size = (u32)NSPageSize();
    ASSERT(result.page_size);

    auto size = result.cpu_name.Capacity();
    if (sysctlbyname("machdep.cpu.brand_string", result.cpu_name.data, &size, nullptr, 0) == 0)
        result.cpu_name.size = NullTerminatedSize(result.cpu_name.data);

    u64 freq = 0;
    size = sizeof(freq);
    if (sysctlbyname("hw.cpufrequency", &freq, &size, nullptr, 0) == 0)
        result.frequency_mhz = (double)freq / 1000000.0;

    return result;
}

void OpenFolderInFileBrowser(String path) {
    NSURL* file_url = [NSURL fileURLWithPath:StringToNSString(path)];
    NSURL* folder_url = [file_url URLByDeletingLastPathComponent];
    [[NSWorkspace sharedWorkspace] openURL:folder_url];
}

void OpenUrlInBrowser(String url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:StringToNSString(url)]];
}

u64 RandomSeed() {
    u64 result;
    arc4random_buf(&result, sizeof(result));
    return result;
}

// https://github.com/grafikrobot/debugging/blob/main/src/macos.cxx
// MIT License
// Copyright (c) 2021-2022 René Ferdinand Rivera Morell
// Copyright (c) 2018 Isabella Muerte
bool IsRunningUnderDebugger() {
    mach_msg_type_number_t count {};
    Array<exception_mask_t, EXC_TYPES_COUNT> masks {};
    Array<mach_port_t, EXC_TYPES_COUNT> ports {};
    Array<exception_behavior_t, EXC_TYPES_COUNT> behaviors {};
    Array<thread_state_flavor_t, EXC_TYPES_COUNT> flavors {};
    exception_mask_t mask = EXC_MASK_ALL & ~(EXC_MASK_RESOURCE | EXC_MASK_GUARD);
    kern_return_t result = task_get_exception_ports(mach_task_self(),
                                                    mask,
                                                    masks.data,
                                                    &count,
                                                    ports.data,
                                                    behaviors.data,
                                                    flavors.data);
    if (result != KERN_SUCCESS) return false;
    for (unsigned i = 0; i < count; ++i)
        if (MACH_PORT_VALID(ports[i])) return true;
    return false;
}
