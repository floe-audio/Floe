// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Simple HTTP server for testing, mimics go-httpbin functionality.
// We use this to test HTTP client implementations without needing external dependencies.

const std = @import("std");
const net = std.net;
const http = std.http;
const json = std.json;

server: net.Server,
thread: ?std.Thread,
should_stop: std.atomic.Value(bool),

const TestHttpServer = @This();

pub fn start() !TestHttpServer {
    const address = net.Address.parseIp4("127.0.0.1", 8081) catch unreachable;
    const server = try address.listen(.{
        .reuse_address = true,
        .force_nonblocking = true,
    });

    var result = TestHttpServer{
        .server = server,
        .thread = null,
        .should_stop = std.atomic.Value(bool).init(false),
    };

    result.thread = try std.Thread.spawn(.{}, serverLoop, .{&result});

    return result;
}

pub fn stop(self: *TestHttpServer) void {
    self.should_stop.store(true, .release);
    if (self.thread) |thread| {
        thread.join();
        self.thread = null;
    }
    self.server.deinit();
}

fn serverLoop(self: *TestHttpServer) void {
    var header_buffer: [9000]u8 = undefined;

    while (!self.should_stop.load(.acquire)) {
        const connection = self.server.accept() catch |err| switch (err) {
            error.WouldBlock => {
                // No connection available, sleep briefly and check for shutdown
                std.time.sleep(std.time.ns_per_ms * 100);
                continue;
            },
            else => {
                // Other errors indicate we should stop
                break;
            },
        };
        defer connection.stream.close();

        var server = http.Server.init(connection, &header_buffer);

        while (server.state == .ready and !self.should_stop.load(.acquire)) {
            var request = server.receiveHead() catch |err| switch (err) {
                error.HttpConnectionClosing => break,
                else => {
                    std.log.err("Failed to receive HTTP head: {}", .{err});
                    break;
                },
            };

            handleRequest(&request) catch |err| {
                std.log.err("Failed to handle request: {}", .{err});
                break;
            };
        }
    }
}

fn handleRequest(request: *http.Server.Request) !void {
    const method = request.head.method;
    const target = request.head.target;

    if (std.mem.eql(u8, target, "/get") and method == .GET) {
        try handleGet(request);
    } else if (std.mem.eql(u8, target, "/post") and method == .POST) {
        try handlePost(request);
    } else {
        try send404(request);
    }
}

fn handleGet(request: *http.Server.Request) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    // Build headers object
    var headers_obj = json.ObjectMap.init(allocator);
    var headers_iter = request.iterateHeaders();
    while (headers_iter.next()) |header| {
        var header_array = json.Array.init(allocator);
        try header_array.append(.{ .string = header.value });
        try headers_obj.put(header.name, .{ .array = header_array });
    }

    // Build response object
    var response_obj = json.ObjectMap.init(allocator);
    try response_obj.put("args", .{ .object = json.ObjectMap.init(allocator) });
    try response_obj.put("headers", .{ .object = headers_obj });
    try response_obj.put("method", .{ .string = "GET" });
    try response_obj.put("origin", .{ .string = "127.0.0.1" });
    try response_obj.put("url", .{ .string = "http://127.0.0.1:8081/get" });

    const response_value = json.Value{ .object = response_obj };

    var response_buf = std.ArrayList(u8).init(allocator);
    try json.stringify(response_value, .{ .whitespace = .indent_2 }, response_buf.writer());

    try request.respond(response_buf.items, .{
        .status = .ok,
        .extra_headers = &.{
            .{ .name = "content-type", .value = "application/json" },
        },
    });
}

fn handlePost(request: *http.Server.Request) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    // Read request body
    const reader = try request.reader();
    const body = reader.readAllAlloc(allocator, 8192) catch "";

    // Build headers object
    var headers_obj = json.ObjectMap.init(allocator);
    var headers_iter = request.iterateHeaders();
    while (headers_iter.next()) |header| {
        var header_array = json.Array.init(allocator);
        try header_array.append(.{ .string = header.value });
        try headers_obj.put(header.name, .{ .array = header_array });
    }

    // Build response object
    var response_obj = json.ObjectMap.init(allocator);
    try response_obj.put("args", .{ .object = json.ObjectMap.init(allocator) });
    try response_obj.put("headers", .{ .object = headers_obj });
    try response_obj.put("method", .{ .string = "POST" });
    try response_obj.put("origin", .{ .string = "127.0.0.1" });
    try response_obj.put("url", .{ .string = "http://127.0.0.1:8081/post" });
    try response_obj.put("data", .{ .string = body });
    try response_obj.put("files", .{ .object = json.ObjectMap.init(allocator) });
    try response_obj.put("form", .{ .object = json.ObjectMap.init(allocator) });
    try response_obj.put("json", .null);

    const response_value = json.Value{ .object = response_obj };

    var response_buf = std.ArrayList(u8).init(allocator);
    try json.stringify(response_value, .{ .whitespace = .indent_2 }, response_buf.writer());

    try request.respond(response_buf.items, .{
        .status = .ok,
        .extra_headers = &.{
            .{ .name = "content-type", .value = "application/json" },
        },
    });
}

fn send404(request: *http.Server.Request) !void {
    try request.respond("Not Found", .{
        .status = .not_found,
        .extra_headers = &.{
            .{ .name = "content-type", .value = "text/plain" },
        },
    });
}
