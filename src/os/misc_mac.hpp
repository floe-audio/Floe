// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#define Rect MacRect
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#import <Foundation/Foundation.h>
#pragma clang diagnostic pop
#undef Rect

#include "foundation/foundation.hpp"

ErrorCode ErrorFromNSError(NSError* error,
                           char const* extra_debug_info = nullptr,
                           SourceLocation source_loc = SourceLocation::Current());

static NSString* StringToNSString(String str) {
    NSString* nspath = [[NSString alloc] initWithBytes:(void const*)str.data
                                                length:(NSUInteger)str.size
                                              encoding:NSUTF8StringEncoding];
    return nspath;
}

// This is a macro instead of a function because UTF8String might have shorter
// lifetime than the function
#define NSStringToString(nsstring) FromNullTerminated(nsstring.UTF8String)
