// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const c = @cImport({
    @cInclude("debug_info.h");
});

const ModuleInfo = struct {
    arena: std.heap.ArenaAllocator,
    self: std.debug.SelfInfo = undefined,
    module: *std.debug.SelfInfo.Module = undefined,
};

fn WriteErrorToErrorBuffer(
    error_buffer: ?*anyopaque,
    error_buffer_size: usize,
    err: anyerror,
) void {
    if (error_buffer != null and error_buffer_size != 0) {
        var buf: [*c]u8 = @ptrCast(error_buffer.?);
        const buf_slice = buf[0..error_buffer_size];
        _ = std.fmt.bufPrintZ(buf_slice, "{}", .{err}) catch |print_err| {
            switch (print_err) {
                error.NoSpaceLeft => {
                    buf_slice[buf_slice.len - 1] = 0;
                },
            }
        };
    }
}

fn Create() !*ModuleInfo {
    const info = try std.heap.c_allocator.create(ModuleInfo);
    errdefer std.heap.c_allocator.destroy(info);

    info.* = ModuleInfo{
        .arena = std.heap.ArenaAllocator.init(std.heap.page_allocator),
    };
    info.self = try std.debug.SelfInfo.open(info.arena.allocator());
    errdefer info.self.deinit();

    // We only need the module for this current binary - we just want stack traces for our own code, not
    // the whole process.
    info.module = try info.self.getModuleForAddress(@intFromPtr(&CreateSelfModuleInfo));

    // We get a symbol to fill the various caches that might be used.
    _ = try info.module.getSymbolAtAddress(info.arena.allocator(), @intFromPtr(&CreateSelfModuleInfo));

    return info;
}

export fn CreateSelfModuleInfo(error_buffer: ?*anyopaque, error_buffer_size: usize) ?*anyopaque {
    const info = Create() catch |err| {
        WriteErrorToErrorBuffer(error_buffer, error_buffer_size, err);
        return null;
    };

    return info;
}

export fn DestroySelfModuleInfo(module_info: ?*anyopaque) void {
    if (module_info == null) {
        return;
    }
    const info: *ModuleInfo = @alignCast(@ptrCast(module_info));

    info.self.deinit();
    info.arena.deinit();
    std.heap.c_allocator.destroy(info);
}

const CallbackFunc = fn (
    user_data: ?*anyopaque,
    symbol: *const c.SymbolInfoData,
) callconv(.C) void;

export fn SymbolInfo(
    module_info: ?*anyopaque,
    addresses: [*c]const usize,
    num_addresses: usize,
    user_data: ?*anyopaque,
    callback: ?*const CallbackFunc,
) void {
    if (module_info == null or addresses == null or callback == null or num_addresses == 0) {
        return;
    }

    const info: *ModuleInfo = @alignCast(@ptrCast(module_info));
    const module = info.module;
    const addr_slice = addresses[0..num_addresses];

    for (addr_slice) |address| {
        var temp_arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        defer temp_arena.deinit();
        var stack_allocator = std.heap.stackFallback(8000, temp_arena.allocator());
        var temp_allocator = stack_allocator.get();

        const symbol = module.getSymbolAtAddress(info.self.allocator, address) catch {
            continue;
        };

        const name_ptr = temp_allocator.dupeZ(u8, symbol.name) catch continue;
        const compile_unit_name_ptr = temp_allocator.dupeZ(u8, symbol.compile_unit_name) catch continue;
        const file_ptr: ?[*:0]const u8 = if (symbol.source_location) |loc|
            (temp_allocator.dupeZ(u8, loc.file_name) catch continue)
        else
            null;

        const line: c_int = if (symbol.source_location) |loc|
            @intCast(loc.line)
        else
            -1;

        const column: c_int = if (symbol.source_location) |loc|
            @intCast(loc.column)
        else
            -1;

        const symbol_info: c.SymbolInfoData = .{
            .address = address,
            .name = name_ptr,
            .compile_unit_name = compile_unit_name_ptr,
            .file = file_ptr,
            .line = line,
            .column = column,
        };

        const cb = callback.?;
        cb(user_data, &symbol_info);
    }
}
