// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Contains code from Zig's std.debug.SelfInfo module:
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT

const std = @import("std");
const c = @cImport({
    @cInclude("debug_info.h");
});
const builtin = @import("builtin");
const native_os = builtin.os.tag;
const native_endian = builtin.cpu.arch.endian();

const SelfInfo = @import("SelfInfo.zig");

pub const panic = std.debug.FullPanic(myPanic);

fn myPanic(msg: []const u8, first_trace_addr: ?usize) noreturn {
    _ = first_trace_addr;
    std.debug.print("Panic in Zig code: {s}\n", .{msg});
    c.PanicHandler(msg.ptr, msg.len);
    std.process.exit(1);
}

const Segment = struct {
    start: usize,
    end: usize,
};

const ModuleInfo = struct {
    arena: std.heap.ArenaAllocator,
    self: SelfInfo = undefined,
    module: *SelfInfo.Module = undefined,
    dwarf: ?*std.debug.Dwarf = null,
    segments: std.ArrayListUnmanaged(Segment),
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
        .segments = .{},
    };
    errdefer self.arena.deinit();

    self.self = try SelfInfo.open(self.arena.allocator());
    errdefer self.self.deinit();

    const address = @intFromPtr(&c.PanicHandler);

    // We only need the module for this current binary - we just want stack traces for our own code, not
    // the whole process.
    self.module = try self.self.getModuleForAddress(address);
    self.dwarf = try self.module.getDwarfInfoForAddress(self.arena.allocator(), address);

    // We populate the cache here so it's not done in SymbolInfo where we want to be thread-safe and signal-safe.
    _ = try self.module.getSymbolAtAddress(self.self.allocator, address);

    switch (native_os) {
        .windows => {
            // Bit of a hack - we are using data structures that might be internal workings of the SelfInfo code.
            for (self.self.modules.items) |*module| {
                if (module.base_address != self.module.base_address) continue;
                try self.segments.append(self.arena.allocator(), Segment{
                    .start = module.base_address,
                    .end = module.base_address + module.size,
                });
            }
        },
        .linux => {
            try std.posix.dl_iterate_phdr(self, error{ IterFn, OutOfMemory }, struct {
                fn callback(info: *std.posix.dl_phdr_info, size: usize, inner_self: *ModuleInfo) error{ IterFn, OutOfMemory }!void {
                    _ = size;
                    if (inner_self.module.base_address != info.addr) return;
                    const phdrs = info.phdr[0..info.phnum];
                    for (phdrs) |*phdr| {
                        if (phdr.p_type != std.elf.PT_LOAD) continue;

                        const seg_start = info.addr +% phdr.p_vaddr;
                        const seg_end = seg_start + phdr.p_memsz;
                        try inner_self.segments.append(inner_self.arena.allocator(), Segment{
                            .start = seg_start,
                            .end = seg_end,
                        });
                    }
                }
            }.callback);
        },

        .macos => {
            const image_count = std.c._dyld_image_count();

            var i: u32 = 0;
            while (i < image_count) : (i += 1) {
                const header = std.c._dyld_get_image_header(i) orelse continue;
                const base_address = @intFromPtr(header);
                if (self.module.base_address != base_address) continue;

                var it = std.macho.LoadCommandIterator{
                    .ncmds = header.ncmds,
                    .buffer = @alignCast(@as(
                        [*]u8,
                        @ptrFromInt(@intFromPtr(header) + @sizeOf(std.macho.mach_header_64)),
                    )[0..header.sizeofcmds]),
                };

                while (it.next()) |cmd| switch (cmd.cmd()) {
                    .SEGMENT_64 => {
                        const segment_cmd = cmd.cast(std.macho.segment_command_64).?;
                        if (!std.mem.eql(u8, "__TEXT", segment_cmd.segName())) continue;

                        const seg_start = segment_cmd.vmaddr + self.module.vmaddr_slide;
                        const seg_end = seg_start + segment_cmd.vmsize;
                        try self.segments.append(self.arena.allocator(), Segment{
                            .start = seg_start,
                            .end = seg_end,
                        });
                    },
                    else => {},
                };
            }
        },

        else => {},
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

fn InModule(self: *ModuleInfo, address: usize) bool {
    for (self.segments.items) |segment| {
        if (address >= segment.start and address < segment.end) return true;
    }
    return false;
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
        if (!InModule(self, address)) {
            const symbol_info = c.SymbolInfoData{
                .address = address,
                .name = "???",
                .compile_unit_name = "???",
                .file = null,
                .line = -1,
                .column = -1,
                .address_in_self_module = 0,
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
            .address_in_self_module = 1,
        };
        cb(user_data, &symbol_info);
    }
}

export fn IsAddressInCurrentModule(
    module_info: c.SelfModuleHandle,
    address: usize,
) callconv(.c) c_int {
    if (module_info == null) return 0;

    const self: *ModuleInfo = @alignCast(@ptrCast(module_info.?));

    return if (InModule(self, address)) 1 else 0;
}
