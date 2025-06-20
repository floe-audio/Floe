// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect MacRect
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <Foundation/Foundation.h>
#pragma clang diagnostic pop
#undef Rect
#include <pthread.h>

#include "utils/logger/logger.hpp"

#include "misc_mac.hpp"
#include "web.hpp"

// We could use dispatch_semaphore_t instead, but thread sanitizer doesn't detect it properly leading to false
// positives. However, it does support pthread very well.
struct CompletionSync {
    void Complete() {
        pthread_mutex_lock(&cond_var_lock);
        completed = true;
        pthread_cond_signal(&cond_var);
        pthread_mutex_unlock(&cond_var_lock);
    }
    void WaitForCompletion() {
        pthread_mutex_lock(&cond_var_lock);
        while (!completed)
            pthread_cond_wait(&cond_var, &cond_var_lock);
        pthread_mutex_unlock(&cond_var_lock);
    }

    pthread_mutex_t cond_var_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;
    bool completed = false;
};

ErrorCodeOr<void> HttpsGet(String url, Writer writer, RequestOptions options) {
    NSURL* nsurl = [NSURL URLWithString:StringToNSString(url)];
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsurl];
    [request setHTTPMethod:@"GET"];

    // Add custom headers
    for (auto const& header : options.headers) {
        NSString* header_string = StringToNSString(header);
        NSArray* components = [header_string componentsSeparatedByString:@": "];
        if (components.count == 2) [request setValue:components[1] forHTTPHeaderField:components[0]];
    }

    NSURLSessionConfiguration* session_config = [NSURLSessionConfiguration defaultSessionConfiguration];
    session_config.timeoutIntervalForRequest = (f64)options.timeout_seconds;
    session_config.timeoutIntervalForResource = (f64)options.timeout_seconds;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:session_config];

    __block ErrorCodeOr<void> result {};
    __block CompletionSync completion_sync;

    @try {
        NSURLSessionDataTask* task =
            [session dataTaskWithRequest:request
                       completionHandler:^(NSData* data, NSURLResponse*, NSError* error) {
                         if (!error) {
                             auto const o = writer.WriteBytes({(u8 const*)data.bytes, data.length});
                             if (o.HasError()) result = o.Error();
                         } else {
                             LogDebug({}, "Error: {}", error.localizedDescription.UTF8String);
                             result = ErrorCode {WebError::NetworkError};
                         }
                         completion_sync.Complete();
                       }];
        [task resume];
        completion_sync.WaitForCompletion();
    } @catch (NSException* e) {
        return ErrorCode {WebError::NetworkError};
    }

    return result;
}

ErrorCodeOr<void>
HttpsPost(String url, String body, Optional<Writer> response_writer, RequestOptions options) {
    NSURL* nsurl = [NSURL URLWithString:StringToNSString(url)];
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsurl];
    [request setHTTPMethod:@"POST"];

    // Set request body
    NSData* request_body = [NSData dataWithBytes:body.data length:body.size];
    [request setHTTPBody:request_body];

    // Add custom headers
    for (auto const& header : options.headers) {
        NSString* header_string = StringToNSString(header);
        NSArray* components = [header_string componentsSeparatedByString:@": "];
        if (components.count == 2) [request setValue:components[1] forHTTPHeaderField:components[0]];
    }

    NSURLSessionConfiguration* session_config = [NSURLSessionConfiguration defaultSessionConfiguration];
    session_config.timeoutIntervalForRequest = (f64)options.timeout_seconds;
    session_config.timeoutIntervalForResource = (f64)options.timeout_seconds;
    NSURLSession* session = [NSURLSession sessionWithConfiguration:session_config];

    __block ErrorCodeOr<void> result {};
    __block CompletionSync completion_sync;

    @try {
        NSURLSessionDataTask* task =
            [session dataTaskWithRequest:request
                       completionHandler:^(NSData* data, NSURLResponse*, NSError* error) {
                         if (error) {
                             LogDebug({}, "Error: {}", error.localizedDescription.UTF8String);
                             result = ErrorCode {WebError::NetworkError};
                         } else if (response_writer.HasValue() && data) {
                             auto const o = response_writer->WriteBytes({(u8 const*)data.bytes, data.length});
                             if (o.HasError()) result = o.Error();
                         } else {
                             result = k_success;
                         }
                         completion_sync.Complete();
                       }];
        [task resume];
        completion_sync.WaitForCompletion();
    } @catch (NSException* e) {
        return ErrorCode {WebError::NetworkError};
    }

    return result;
}

void WebGlobalInit() {}
void WebGlobalCleanup() {}
