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

const Segment = struct {
    start: usize,
    end: usize,
};

const ModuleInfo = struct {
    arena: std.heap.ArenaAllocator,
    self: std.debug.SelfInfo = undefined,
    module: *std.debug.SelfInfo.Module = undefined,
    dwarf: ?*std.debug.Dwarf = null,
    segments: if (native_os != .windows) std.ArrayListUnmanaged(Segment) else void,
    whole_segment: if (native_os == .windows) ?Segment else void,
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

pub const MODULEINFO = extern struct {
    lpBaseOfDll: ?*anyopaque,
    SizeOfImage: u32,
    EntryPoint: ?*anyopaque,
};

pub extern "kernel32" fn K32GetModuleInformation(
    hProcess: ?std.os.windows.HANDLE,
    hModule: ?std.os.windows.HINSTANCE,
    lpmodinfo: ?*MODULEINFO,
    cb: u32,
) callconv(@import("std").os.windows.WINAPI) std.os.windows.BOOL;

pub const GET_MODULE_HANDLE_EX_FLAG_PIN = @as(u32, 1);
pub const GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT = @as(u32, 2);
pub const GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = @as(u32, 4);

pub extern "kernel32" fn GetModuleHandleExW(
    dwFlags: u32,
    lpModuleName: ?[*:0]const u16,
    phModule: ?*?std.os.windows.HINSTANCE,
) callconv(@import("std").os.windows.WINAPI) std.os.windows.BOOL;

// NOTE: copied directly from std.debug.SelfInfo. The existing implementation isn't a great fit for us so we
// copy the code and make the changes we need. Specifically:
// - We don't care about PDB info. We only need the DWARF info. Related: https://github.com/ziglang/zig/pull/22492
// This code is likely very brittle when Zig changes their implementation, but it works for now.
fn lookupModuleWin32(self: *std.debug.SelfInfo, address: usize) !*std.debug.SelfInfo.Module {
    for (self.modules.items) |*module| {
        if (address >= module.base_address and address < module.base_address + module.size) {
            if (self.address_map.get(module.base_address)) |obj_di| {
                return obj_di;
            }

            const obj_di = try self.allocator.create(std.debug.SelfInfo.Module);
            errdefer self.allocator.destroy(obj_di);

            const mapped_module = @as([*]const u8, @ptrFromInt(module.base_address))[0..module.size];
            var coff_obj = try std.coff.Coff.init(mapped_module, true);

            // The string table is not mapped into memory by the loader, so if a section name is in the
            // string table then we have to map the full image file from disk. This can happen when
            // a binary is produced with -gdwarf, since the section names are longer than 8 bytes.
            if (coff_obj.strtabRequired()) {
                var name_buffer: [std.os.windows.PATH_MAX_WIDE + 4:0]u16 = undefined;
                // openFileAbsoluteW requires the prefix to be present
                @memcpy(name_buffer[0..4], &[_]u16{ '\\', '?', '?', '\\' });

                const process_handle = std.os.windows.GetCurrentProcess();
                const len = std.os.windows.kernel32.GetModuleFileNameExW(
                    process_handle,
                    module.handle,
                    @ptrCast(&name_buffer[4]),
                    std.os.windows.PATH_MAX_WIDE,
                );

                if (len == 0) return error.MissingDebugInfo;
                const coff_file = std.fs.openFileAbsoluteW(name_buffer[0 .. len + 4 :0], .{}) catch |err| switch (err) {
                    error.FileNotFound => return error.MissingDebugInfo,
                    else => return err,
                };
                errdefer coff_file.close();

                var section_handle: std.os.windows.HANDLE = undefined;
                const create_section_rc = std.os.windows.ntdll.NtCreateSection(
                    &section_handle,
                    std.os.windows.STANDARD_RIGHTS_REQUIRED | std.os.windows.SECTION_QUERY | std.os.windows.SECTION_MAP_READ,
                    null,
                    null,
                    std.os.windows.PAGE_READONLY,
                    // The documentation states that if no AllocationAttribute is specified, then SEC_COMMIT is the default.
                    // In practice, this isn't the case and specifying 0 will result in INVALID_PARAMETER_6.
                    std.os.windows.SEC_COMMIT,
                    coff_file.handle,
                );
                if (create_section_rc != .SUCCESS) return error.MissingDebugInfo;
                errdefer std.os.windows.CloseHandle(section_handle);

                var coff_len: usize = 0;
                var base_ptr: usize = 0;
                const map_section_rc = std.os.windows.ntdll.NtMapViewOfSection(
                    section_handle,
                    process_handle,
                    @ptrCast(&base_ptr),
                    null,
                    0,
                    null,
                    &coff_len,
                    .ViewUnmap,
                    0,
                    std.os.windows.PAGE_READONLY,
                );
                if (map_section_rc != .SUCCESS) return error.MissingDebugInfo;
                errdefer std.debug.assert(std.os.windows.ntdll.NtUnmapViewOfSection(process_handle, @ptrFromInt(base_ptr)) == .SUCCESS);

                const section_view = @as([*]const u8, @ptrFromInt(base_ptr))[0..coff_len];
                coff_obj = try std.coff.Coff.init(section_view, false);

                module.mapped_file = .{
                    .file = coff_file,
                    .section_handle = section_handle,
                    .section_view = section_view,
                };
            }
            errdefer if (module.mapped_file) |mapped_file| mapped_file.deinit();

            obj_di.* = try readCoffDebugInfo(self.allocator, &coff_obj);
            obj_di.base_address = module.base_address;

            try self.address_map.putNoClobber(module.base_address, obj_di);
            return obj_di;
        }
    }

    return error.MissingDebugInfo;
}

// NOTE: copied directly from std.debug.SelfInfo, see above comment.
fn readCoffDebugInfo(allocator: std.mem.Allocator, coff_obj: *std.coff.Coff) !std.debug.SelfInfo.Module {
    nosuspend {
        var di: std.debug.SelfInfo.Module = .{
            .base_address = undefined,
            .coff_image_base = coff_obj.getImageBase(),
            .coff_section_headers = undefined,
        };

        if (coff_obj.getSectionByName(".debug_info")) |_| {
            // This coff file has embedded DWARF debug info
            var sections: std.debug.Dwarf.SectionArray = std.debug.Dwarf.null_section_array;
            errdefer for (sections) |section| if (section) |s| if (s.owned) allocator.free(s.data);

            inline for (@typeInfo(std.debug.Dwarf.Section.Id).@"enum".fields, 0..) |section, i| {
                sections[i] = if (coff_obj.getSectionByName("." ++ section.name)) |section_header| blk: {
                    break :blk .{
                        .data = try coff_obj.getSectionDataAlloc(section_header, allocator),
                        .virtual_address = section_header.virtual_address,
                        .owned = true,
                    };
                } else null;
            }

            var dwarf: std.debug.Dwarf = .{
                .endian = native_endian,
                .sections = sections,
                .is_macho = false,
            };

            try std.debug.Dwarf.open(&dwarf, allocator);
            di.dwarf = dwarf;
        }

        // NOTE: at this same point in the Zig code, PDB info is read. We don't do that here.

        return di;
    }
}

fn Create() !*ModuleInfo {
    const self = try std.heap.c_allocator.create(ModuleInfo);
    errdefer std.heap.c_allocator.destroy(self);

    self.* = ModuleInfo{
        .arena = std.heap.ArenaAllocator.init(std.heap.page_allocator),
        .segments = if (native_os != .windows) .{} else {},
        .whole_segment = if (native_os == .windows) null else {},
    };
    self.self = try std.debug.SelfInfo.open(self.arena.allocator());
    errdefer self.self.deinit();

    const address = @intFromPtr(&CreateSelfModuleInfo);

    // We only need the module for this current binary - we just want stack traces for our own code, not
    // the whole process.
    self.module = if (native_os == .windows) try lookupModuleWin32(&self.self, address) else try self.self.getModuleForAddress(address);
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
    _ = try self.module.getSymbolAtAddress(self.self.allocator, address);

    switch (native_os) {
        .windows => {
            var module: ?std.os.windows.HINSTANCE = null;
            if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                @alignCast(@ptrCast(&CreateSelfModuleInfo)),
                &module,
            ) != 0) {
                var module_info: MODULEINFO = undefined;
                if (K32GetModuleInformation(
                    std.os.windows.GetCurrentProcess(),
                    module,
                    &module_info,
                    @sizeOf(MODULEINFO),
                ) != 0) {
                    const base_address: usize = @intFromPtr(module_info.lpBaseOfDll);
                    self.whole_segment = Segment{
                        .start = base_address,
                        .end = base_address + module_info.SizeOfImage,
                    };
                    if (builtin.mode == .Debug) std.debug.print("got Windows module info: {x} - {x}\n", .{
                        self.whole_segment.?.start,
                        self.whole_segment.?.end,
                    });
                }
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
            if (builtin.mode == .Debug) std.debug.print("got Linux module info, num segments: {}\n", .{
                self.segments.items.len,
            });
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

                        const seg_start = segment_cmd.vmaddr;
                        const seg_end = seg_start + segment_cmd.vmsize;
                        try self.segments.append(self.arena.allocator(), Segment{
                            .start = seg_start,
                            .end = seg_end,
                        });
                    },
                    else => {},
                };
            }

            if (builtin.mode == .Debug) std.debug.print("got macOS module info, num segments: {}\n", .{
                self.segments.items.len,
            });
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

    if (native_os == .windows) {
        if (self.whole_segment) |segment| {
            for (addresses[0..num_addresses]) |address| {
                if (address >= segment.start and address < segment.end) return 1;
            }
            return 0;
        }
    } else {
        for (self.segments.items) |segment| {
            for (addresses[0..num_addresses]) |address| {
                std.debug.print("checking address {x} against segment {x} - {x}\n", .{
                    address,
                    segment.start,
                    segment.end,
                });
                if (address >= segment.start and address < segment.end) return 1;
            }
        }
        if (self.segments.items.len != 0) return 0;
    }

    // Fallback.

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
