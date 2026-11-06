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

const ImageKind = enum { elf, macho, pe };

// The identity of our module as it's loaded in memory. This is what an external symbolicator (Sentry) needs to
// match addresses to separately-stored debug info.
const Image = struct {
    kind: ImageKind,
    addr: usize,
    size: usize,
    vmaddr: usize,
    debug_id: [:0]const u8 = "",
    code_id: [:0]const u8 = "",
    code_file: [:0]const u8 = "",
    debug_file: [:0]const u8 = "",
};

const ModuleInfo = struct {
    non_threadsafe_arena: std.heap.ArenaAllocator,
    arena: std.heap.ThreadSafeAllocator,
    self: ?SelfInfo = null,
    // Null if debug info isn't available, such as in a stripped binary.
    module: ?*SelfInfo.Module = null,
    segments: std.ArrayListUnmanaged(Segment),
    image: ?Image = null,
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

fn formatUuid(
    allocator: std.mem.Allocator,
    bytes: [16]u8,
    fields_endian: std.builtin.Endian,
    age: ?u32,
) ![:0]u8 {
    const uuid = try std.fmt.allocPrint(allocator, "{x:0>8}-{x:0>4}-{x:0>4}-{s}-{s}", .{
        std.mem.readInt(u32, bytes[0..4], fields_endian),
        std.mem.readInt(u16, bytes[4..6], fields_endian),
        std.mem.readInt(u16, bytes[6..8], fields_endian),
        std.fmt.fmtSliceHexLower(bytes[8..10]),
        std.fmt.fmtSliceHexLower(bytes[10..16]),
    });
    if (age) |a| return std.fmt.allocPrintZ(allocator, "{s}-{x}", .{ uuid, a });
    return std.fmt.allocPrintZ(allocator, "{s}", .{uuid});
}

fn basenameZ(allocator: std.mem.Allocator, path: []const u8) ![:0]u8 {
    // Windows paths can use either separator.
    var start: usize = 0;
    for (path, 0..) |ch, i| {
        if (ch == '/' or ch == '\\') start = i + 1;
    }
    return allocator.dupeZ(u8, path[start..]);
}

fn addImageAndSegmentsElf(self: *ModuleInfo, address: usize) !void {
    const Context = struct {
        self: *ModuleInfo,
        address: usize,
        found: bool = false,
        err: ?anyerror = null,
    };
    var context = Context{ .self = self, .address = address };

    std.posix.dl_iterate_phdr(&context, error{Found}, struct {
        fn callback(info: *std.posix.dl_phdr_info, size: usize, ctx: *Context) error{Found}!void {
            _ = size;
            const phdrs = info.phdr[0..info.phnum];

            const contains_address = for (phdrs) |*phdr| {
                if (phdr.p_type != std.elf.PT_LOAD) continue;
                const seg_start = info.addr +% phdr.p_vaddr;
                if (ctx.address >= seg_start and ctx.address < seg_start + phdr.p_memsz) break true;
            } else false;
            if (!contains_address) return;

            ctx.found = true;
            process(info, ctx) catch |err| {
                ctx.err = err;
            };
            return error.Found;
        }

        fn process(info: *std.posix.dl_phdr_info, ctx: *Context) !void {
            const allocator = ctx.self.arena.allocator();
            const phdrs = info.phdr[0..info.phnum];

            var min_vaddr: usize = std.math.maxInt(usize);
            var max_vaddr: usize = 0;
            var build_id: ?[]const u8 = null;
            for (phdrs) |*phdr| {
                switch (phdr.p_type) {
                    std.elf.PT_LOAD => {
                        const seg_start = info.addr +% phdr.p_vaddr;
                        try ctx.self.segments.append(allocator, .{
                            .start = seg_start,
                            .end = seg_start + phdr.p_memsz,
                        });
                        min_vaddr = @min(min_vaddr, phdr.p_vaddr);
                        max_vaddr = @max(max_vaddr, phdr.p_vaddr + phdr.p_memsz);
                    },
                    std.elf.PT_NOTE => {
                        if (build_id != null) continue;
                        const notes = @as([*]const u8, @ptrFromInt(info.addr +% phdr.p_vaddr))[0..phdr.p_memsz];
                        var pos: usize = 0;
                        while (pos + 12 <= notes.len) {
                            const name_size = std.mem.readInt(u32, notes[pos..][0..4], native_endian);
                            const desc_size = std.mem.readInt(u32, notes[pos + 4 ..][0..4], native_endian);
                            const note_type = std.mem.readInt(u32, notes[pos + 8 ..][0..4], native_endian);
                            const name_start = pos + 12;
                            const desc_start = name_start + std.mem.alignForward(usize, name_size, 4);
                            const next = desc_start + std.mem.alignForward(usize, desc_size, 4);
                            if (next > notes.len) break;
                            if (note_type == std.elf.NT_GNU_BUILD_ID and
                                std.mem.eql(u8, notes[name_start..][0..name_size], "GNU\x00"))
                            {
                                build_id = notes[desc_start..][0..desc_size];
                                break;
                            }
                            pos = next;
                        }
                    },
                    else => {},
                }
            }

            var image = Image{
                .kind = .elf,
                .addr = info.addr +% min_vaddr,
                .size = max_vaddr - min_vaddr,
                .vmaddr = min_vaddr,
            };

            if (build_id) |id| {
                // Sentry's ELF debug ID is the first 16 bytes of the build ID (zero-padded) formatted as a
                // little-endian UUID.
                var uuid_bytes = [_]u8{0} ** 16;
                const len = @min(id.len, 16);
                @memcpy(uuid_bytes[0..len], id[0..len]);
                image.debug_id = try formatUuid(allocator, uuid_bytes, .little, null);
                image.code_id = try std.fmt.allocPrintZ(allocator, "{s}", .{std.fmt.fmtSliceHexLower(id)});
            }

            // The main program has an empty name.
            const name = std.mem.sliceTo(info.name, 0) orelse "";
            image.code_file = if (name.len != 0)
                try basenameZ(allocator, name)
            else
                try basenameZ(allocator, std.fs.selfExePathAlloc(allocator) catch ""); 

            ctx.self.image = image;
        }
    }.callback) catch {};

    if (context.err) |err| return err;
    if (!context.found) return error.ModuleNotFound;
}

fn addImageAndSegmentsMacho(self: *ModuleInfo, address: usize) !void {
    const allocator = self.arena.allocator();
    const image_count = std.c._dyld_image_count();

    var i: u32 = 0;
    while (i < image_count) : (i += 1) {
        const header = std.c._dyld_get_image_header(i) orelse continue;
        const base_address = @intFromPtr(header);
        if (address < base_address) continue;
        const vmaddr_slide = std.c._dyld_get_image_vmaddr_slide(i);

        var text_segment: ?Segment = null;
        var text_vmaddr: usize = 0;
        var max_vmaddr: usize = 0;
        var uuid: ?[16]u8 = null;

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
                if (std.mem.eql(u8, "__PAGEZERO", segment_cmd.segName())) continue;
                max_vmaddr = @max(max_vmaddr, segment_cmd.vmaddr + segment_cmd.vmsize);
                if (!std.mem.eql(u8, "__TEXT", segment_cmd.segName())) continue;
                text_vmaddr = segment_cmd.vmaddr;
                const seg_start = segment_cmd.vmaddr + vmaddr_slide;
                text_segment = .{ .start = seg_start, .end = seg_start + segment_cmd.vmsize };
            },
            .UUID => uuid = cmd.cast(std.macho.uuid_command).?.uuid,
            else => {},
        };

        const segment = text_segment orelse continue;
        if (address < segment.start or address >= segment.end) continue;

        try self.segments.append(allocator, segment);

        var image = Image{
            .kind = .macho,
            .addr = base_address,
            .size = max_vmaddr - text_vmaddr,
            .vmaddr = text_vmaddr,
            .code_file = try basenameZ(allocator, std.mem.sliceTo(std.c._dyld_get_image_name(i), 0)),
        };
        if (uuid) |u| image.debug_id = try formatUuid(allocator, u, .big, null);
        self.image = image;
        return;
    }

    return error.ModuleNotFound;
}

fn addImageAndSegmentsPe(self: *ModuleInfo, address: usize) !void {
    const windows = std.os.windows;
    const allocator = self.arena.allocator();

    var memory_info: windows.MEMORY_BASIC_INFORMATION = undefined;
    _ = try windows.VirtualQuery(@ptrFromInt(address), &memory_info, @sizeOf(windows.MEMORY_BASIC_INFORMATION));
    const base_address = @intFromPtr(memory_info.AllocationBase);
    const base: [*]const u8 = @ptrFromInt(base_address);

    if (!std.mem.eql(u8, base[0..2], "MZ")) return error.InvalidPeImage;
    const pe_offset = std.mem.readInt(u32, base[0x3c..][0..4], .little);
    const pe = base + pe_offset;
    if (!std.mem.eql(u8, pe[0..4], "PE\x00\x00")) return error.InvalidPeImage;
    const file_header = pe + 4;
    const time_date_stamp = std.mem.readInt(u32, file_header[4..][0..4], .little);
    const optional_header = file_header + 20;
    if (std.mem.readInt(u16, optional_header[0..2], .little) != 0x20b) return error.InvalidPeImage; // PE32+
    const image_base = std.mem.readInt(u64, optional_header[24..][0..8], .little);
    const size_of_image = std.mem.readInt(u32, optional_header[56..][0..4], .little);
    const num_data_directories = std.mem.readInt(u32, optional_header[108..][0..4], .little);

    try self.segments.append(allocator, .{ .start = base_address, .end = base_address + size_of_image });

    var image = Image{
        .kind = .pe,
        .addr = base_address,
        .size = size_of_image,
        .vmaddr = @intCast(image_base),
        .code_id = try std.fmt.allocPrintZ(allocator, "{x:0>8}{x}", .{ time_date_stamp, size_of_image }),
    };

    const debug_directory_index = 6;
    if (num_data_directories > debug_directory_index) {
        const dir = optional_header + 112 + debug_directory_index * 8;
        const dir_rva = std.mem.readInt(u32, dir[0..4], .little);
        const dir_size = std.mem.readInt(u32, dir[4..8], .little);
        const debug_entry_size = 28;
        const codeview_type = 2;
        var entry_index: usize = 0;
        while (dir_rva != 0 and (entry_index + 1) * debug_entry_size <= dir_size) : (entry_index += 1) {
            const entry = base + dir_rva + entry_index * debug_entry_size;
            if (std.mem.readInt(u32, entry[12..][0..4], .little) != codeview_type) continue;
            const data_size = std.mem.readInt(u32, entry[16..][0..4], .little);
            const data_rva = std.mem.readInt(u32, entry[20..][0..4], .little);
            if (data_rva == 0 or data_size < 24) continue;
            const data = (base + data_rva)[0..data_size];
            if (!std.mem.eql(u8, data[0..4], "RSDS")) continue;
            const age = std.mem.readInt(u32, data[20..24], .little);
            image.debug_id = try formatUuid(allocator, data[4..20].*, .little, age);
            image.debug_file = try basenameZ(allocator, std.mem.sliceTo(data[24..], 0));
            break;
        }
    }

    var name_buffer: [windows.PATH_MAX_WIDE]u16 = undefined;
    const name_len = windows.kernel32.GetModuleFileNameW(
        @ptrFromInt(base_address),
        &name_buffer,
        name_buffer.len,
    );
    if (name_len != 0) {
        const name = try std.unicode.utf16LeToUtf8Alloc(allocator, name_buffer[0..name_len]);
        image.code_file = try basenameZ(allocator, name);
    }

    self.image = image;
}

fn openSymbolication(self: *ModuleInfo, address: usize) !void {
    self.self = try SelfInfo.open(self.arena.allocator());
    const module = try self.self.?.getModuleForAddress(address);

    // We populate the cache here so it's not done in SymbolInfo where we want to be thread-safe and signal-safe.
    _ = try module.getSymbolAtAddress(self.self.?.allocator, address);

    self.module = module;
}

fn Create(error_buffer: [*c]u8, error_buffer_size: usize) !*ModuleInfo {
    const self = try std.heap.c_allocator.create(ModuleInfo);
    errdefer std.heap.c_allocator.destroy(self);

    self.* = ModuleInfo{
        .non_threadsafe_arena = std.heap.ArenaAllocator.init(std.heap.page_allocator),
        .arena = .{ .child_allocator = self.non_threadsafe_arena.allocator() },
        .segments = .{},
    };
    errdefer self.non_threadsafe_arena.deinit();

    // We only need the module for this current binary - we just want stack traces for our own code, not
    // the whole process.
    const address = @intFromPtr(&c.PanicHandler);

    switch (native_os) {
        .linux => try addImageAndSegmentsElf(self, address),
        .macos => try addImageAndSegmentsMacho(self, address),
        .windows => try addImageAndSegmentsPe(self, address),
        else => {},
    }

    // Missing debug info is expected for stripped binaries: addresses are still reported and can be
    // symbolicated elsewhere using the image info.
    openSymbolication(self, address) catch |err| {
        WriteErrorToErrorBuffer(error_buffer, error_buffer_size, err);
        if (self.self) |*s| s.deinit();
        self.self = null;
        self.module = null;
    };

    return self;
}

export fn CreateSelfModuleInfo(error_buffer: [*c]u8, error_buffer_size: usize) callconv(.c) c.SelfModuleHandle {
    if (error_buffer != null and error_buffer_size != 0) error_buffer[0] = 0;
    const self = Create(error_buffer, error_buffer_size) catch |err| {
        WriteErrorToErrorBuffer(error_buffer, error_buffer_size, err);
        return null;
    };

    return self;
}

export fn DestroySelfModuleInfo(module_info: c.SelfModuleHandle) callconv(.c) void {
    if (module_info == null) return;

    const self: *ModuleInfo = @alignCast(@ptrCast(module_info));

    if (self.self) |*s| s.deinit();
    self.non_threadsafe_arena.deinit();
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

        const module = self.module orelse {
            const symbol_info = c.SymbolInfoData{
                .address = address,
                .name = "???",
                .compile_unit_name = "???",
                .file = null,
                .line = -1,
                .column = -1,
                .address_in_self_module = 1,
            };
            cb(user_data, &symbol_info);
            continue;
        };

        var temp_arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        defer temp_arena.deinit();
        var stack_allocator = std.heap.stackFallback(8000, temp_arena.allocator());
        var temp_allocator = stack_allocator.get();

        // NOTE: this is actually not 100% signal-safe since we have to use an allocator with a mutex. I think the
        // possiblity for serious problems is low though, and Zig's debug info code is not designed to be signal-safe.
        const symbol = module.getSymbolAtAddress(self.self.?.allocator, address) catch continue;

        const name_ptr = temp_allocator.dupeZ(u8, symbol.name) catch continue;
        const compile_unit_name_ptr = temp_allocator.dupeZ(u8, symbol.compile_unit_name) catch continue;
        const file_ptr: ?[*:0]u8 = if (symbol.source_location) |loc|
            (temp_allocator.dupeZ(u8, loc.file_name) catch continue)
        else
            null;

        if (native_os == .windows) {
            if (file_ptr) |file| {
                var i: usize = 0;
                while (file[i] != 0) : (i += 1) {
                    if (file[i] == '\\') file[i] = '/';
                }
            }
        }

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

export fn SelfModuleImage(module_info: c.SelfModuleHandle, out: [*c]c.SelfModuleImageInfo) callconv(.c) c_int {
    if (module_info == null or out == null) return 0;
    const self: *ModuleInfo = @alignCast(@ptrCast(module_info.?));
    const image = self.image orelse return 0;

    out.* = .{
        .type = switch (image.kind) {
            .elf => "elf",
            .macho => "macho",
            .pe => "pe",
        },
        .image_addr = image.addr,
        .image_size = image.size,
        .image_vmaddr = image.vmaddr,
        .debug_id = image.debug_id.ptr,
        .code_id = image.code_id.ptr,
        .code_file = image.code_file.ptr,
        .debug_file = image.debug_file.ptr,
    };
    return 1;
}
