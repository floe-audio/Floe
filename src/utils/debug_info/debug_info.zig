// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");
const c = @cImport({
    @cInclude("debug_info.h");
});
const builtin = @import("builtin");
const native_os = builtin.os.tag;

const ModuleInfo = struct {
    arena: std.heap.ArenaAllocator,
    self: std.debug.SelfInfo = undefined,
    module: *std.debug.SelfInfo.Module = undefined,
    dwarf: ?*std.debug.Dwarf = null,
};

fn WriteErrorToErrorBuffer(
    error_buffer: [*c]u8,
    error_buffer_size: usize,
    err: anyerror,
) void {
    if (error_buffer == null) return;
    if (error_buffer_size == 0) return;

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

fn Create() !*ModuleInfo {
    const self = try std.heap.c_allocator.create(ModuleInfo);
    errdefer std.heap.c_allocator.destroy(self);

    self.* = ModuleInfo{
        .arena = std.heap.ArenaAllocator.init(std.heap.page_allocator),
    };
    self.self = try std.debug.SelfInfo.open(self.arena.allocator());
    errdefer self.self.deinit();

    const address = @intFromPtr(&CreateSelfModuleInfo);

    // We only need the module for this current binary - we just want stack traces for our own code, not
    // the whole process.
    self.module = try self.self.getModuleForAddress(address);
    if (native_os == .windows) {
        // There seems to be a bug in the Windows implementation where getDwarfInfoForAddress won't compile,
        // so we workaround.
        if (self.module.dwarf != null) {
            self.dwarf = &self.module.dwarf.?;
        }
    } else {
        self.dwarf = try self.module.getDwarfInfoForAddress(self.arena.allocator(), address);
    }

    // We populate the cache here so it's not done in SymbolInfo where we want to be thread-safe and signal-safe.
    if (self.dwarf) |dwarf| {
        const compile_unit = try dwarf.findCompileUnit(address);
        try dwarf.populateSrcLocCache(self.arena.allocator(), compile_unit);
    }

    return self;
}

export fn CreateSelfModuleInfo(error_buffer: [*c]u8, error_buffer_size: usize) callconv(.c) c.SelfModuleHandle {
    const self = Create() catch |err| {
        WriteErrorToErrorBuffer(error_buffer, error_buffer_size, err);
        return null;
    };

    return self;
}

export fn DestroySelfModuleInfo(module_info: c.SelfModuleHandle) callconv(.c) void {
    if (module_info == null) return;

    const self: *ModuleInfo = @alignCast(@ptrCast(module_info));

    self.self.deinit();
    self.arena.deinit();
    std.heap.c_allocator.destroy(self);
}

export fn SymbolInfo(
    module_info: c.SelfModuleHandle,
    addresses: [*c]const usize,
    num_addresses: usize,
    user_data: ?*anyopaque,
    callback: c.SymbolInfoCallback,
) callconv(.c) void {
    if (module_info == null or addresses == null or callback == null or num_addresses == 0)
        return;

    const self: *ModuleInfo = @alignCast(@ptrCast(module_info.?));
    const cb = callback.?;

    for (addresses[0..num_addresses]) |address| {
        if (address < self.module.base_address) {
            const symbol_info = c.SymbolInfoData{
                .address = address,
                .name = "???",
                .compile_unit_name = "???",
                .file = null,
                .line = -1,
                .column = -1,
            };
            cb(user_data, &symbol_info);
            continue;
        }

        var temp_arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        defer temp_arena.deinit();
        var stack_allocator = std.heap.stackFallback(8000, temp_arena.allocator());
        var temp_allocator = stack_allocator.get();

        const symbol = self.module.getSymbolAtAddress(self.self.allocator, address) catch continue;

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
        cb(user_data, &symbol_info);
    }
}

export fn HasAddressesInCurrentModule(
    module_info: c.SelfModuleHandle,
    addresses: [*c]const usize,
    num_addresses: usize,
) callconv(.c) c_int {
    if (module_info == null or addresses == null or num_addresses == 0) return 0;

    const self: *ModuleInfo = @alignCast(@ptrCast(module_info.?));

    // TODO: we ideally want this function to work without dwarf info. We'd perhaps need to work out the
    // base_address and extend of our current module and see if the address is in that range.

    if (self.dwarf == null) return 1; // We don't know, so assume yes.

    var result: c_int = 0;

    for (addresses[0..num_addresses]) |address| {
        if (address < self.module.base_address) continue;

        _ = self.dwarf.?.findCompileUnit(address) catch continue;
        result = 1;
        break;
    }

    return result;
}
