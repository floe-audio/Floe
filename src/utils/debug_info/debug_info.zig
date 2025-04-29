// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const ModuleInfo = struct {
    self: std.debug.SelfInfo,
    module: *std.debug.SelfInfo.Module,
};

export fn CreateSelfModuleInfo() ?*anyopaque {
    var self = std.debug.SelfInfo.open(std.heap.c_allocator) catch |err| {
        std.debug.print("Error opening self info: {}\n", .{err});
        return null;
    };
    const module = self.getModuleForAddress(@intFromPtr(&CreateSelfModuleInfo)) catch |err| {
        std.debug.print("Error getting module for address: {}\n", .{err});
        return null;
    };
    const info = std.heap.c_allocator.create(ModuleInfo) catch |err| {
        std.debug.print("Error creating : {}\n", .{err});
        return null;
    };
    info.* = ModuleInfo{
        .self = self,
        .module = module,
    };

    return info;
}

export fn DestroySelfModuleInfo(module_info: ?*anyopaque) void {
    if (module_info == null) {
        return;
    }
    const info: *ModuleInfo = @alignCast(@ptrCast(module_info));

    info.self.deinit();
    std.heap.c_allocator.destroy(info);
}

const CallbackFunc = fn (
    user_data: ?*anyopaque,
    address: usize,
    name: [*:0]const u8,
    compile_unit_name: [*:0]const u8,
    file: ?[*:0]const u8,
    line: c_int,
    column: c_int,
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

        const symbol = module.getSymbolAtAddress(temp_allocator, address) catch |err| {
            std.debug.print("Error getting symbol at address {}: {}\n", .{ address, err });
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

        const cb = callback.?;
        cb(user_data, address, name_ptr, compile_unit_name_ptr, file_ptr, line, column);
    }
}
