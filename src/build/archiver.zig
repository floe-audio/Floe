// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// CLI tool to create ZIP/tar.gz archives. It means we don't have to depend on external tools.

const std = @import("std");
const builtin = @import("builtin");
const c = @cImport(@cInclude("miniz.h"));

const ArchiveType = enum { zip, tar_gz };

const FileEntry = struct {
    source_path: []const u8,
    archive_path: []const u8,
};

const Archive = union(enum) {
    zip: c.mz_zip_archive,
    tar_gz: @TypeOf(std.tar.writer(std.io.AnyWriter{ .context = undefined, .writeFn = undefined })),
};

fn createZipArchivePathZ(allocator: std.mem.Allocator, path: []const u8, trailing_slash: bool) ![:0]u8 {
    const result = try std.fmt.allocPrintZ(
        allocator,
        "{s}{s}",
        .{ path, if (trailing_slash and !std.mem.endsWith(u8, path, "/")) "/" else "" },
    );

    if (builtin.os.tag != .windows)
        return result;

    for (result[0..result.len]) |*char| {
        if (char.* == '/') {
            char.* = '\\';
        }
    }
    return result;
}

fn addFileToArchive(
    archive: *Archive,
    allocator: std.mem.Allocator,
    source_file_path: []const u8,
    archive_path: []const u8,
) !void {
    const cwd = std.fs.cwd();

    switch (archive.*) {
        .zip => |*zip| {
            const file_content = cwd.readFileAlloc(
                std.heap.page_allocator,
                source_file_path,
                std.math.maxInt(usize),
            ) catch |err| switch (err) {
                error.FileTooBig => {
                    std.log.err("File too big: {s}\n", .{source_file_path});
                    return;
                },
                else => return err,
            };
            defer std.heap.page_allocator.free(file_content);

            const archive_path_z = try createZipArchivePathZ(allocator, archive_path, false);

            const result = c.mz_zip_writer_add_mem(
                zip,
                archive_path_z.ptr,
                file_content.ptr,
                file_content.len,
                @bitCast(@as(i32, c.MZ_DEFAULT_COMPRESSION)),
            );

            if (result == c.MZ_FALSE) {
                std.log.err("Failed to add file to archive: {s}\n", .{source_file_path});
                return error.ZipAddFileFailed;
            }
        },
        .tar_gz => |*tar_writer| {
            const file_handle = try std.fs.cwd().openFile(source_file_path, .{});
            defer file_handle.close();

            try tar_writer.writeFile(archive_path, file_handle);
        },
    }
}

fn addDirToArchive(
    archive: *Archive,
    dir_path: []const u8,
) !void {
    switch (archive.*) {
        .zip => |*zip| {
            const path_z = try createZipArchivePathZ(std.heap.page_allocator, dir_path, true);

            const dir_result = c.mz_zip_writer_add_mem(
                zip,
                path_z.ptr,
                null,
                0,
                @bitCast(@as(i32, c.MZ_DEFAULT_COMPRESSION)),
            );

            if (dir_result == c.MZ_FALSE) {
                std.log.err("Failed to add directory to archive: {s}\n", .{dir_path});
                return error.ZipAddDirFailed;
            }
        },
        .tar_gz => |*tar_writer| {
            try tar_writer.writeDir(dir_path, .{});
        },
    }
}

fn addFileOrDir(archive: *Archive, allocator: std.mem.Allocator, entry: FileEntry) !void {
    const cwd = std.fs.cwd();

    // Entries are allowed to be either files or directories.
    const stat = cwd.statFile(entry.source_path) catch |err| switch (err) {
        error.FileNotFound => {
            std.log.err("File not found: {s}\n", .{entry.source_path});
            return;
        },
        else => return err,
    };

    switch (stat.kind) {
        .file => {
            try addFileToArchive(archive, allocator, entry.source_path, entry.archive_path);
        },
        .directory => {
            try addDirToArchive(archive, entry.archive_path);

            var dir = cwd.openDir(entry.source_path, .{ .iterate = true }) catch |err| {
                std.log.err("Failed to open directory: {s}\n", .{entry.source_path});
                return err;
            };
            defer dir.close();

            var walker = try dir.walk(allocator);
            defer walker.deinit();

            while (try walker.next()) |dir_entry| {
                const full_source_path = try std.fs.path.join(
                    allocator,
                    &.{ entry.source_path, dir_entry.path },
                );

                const full_archive_path = try std.mem.join(
                    allocator,
                    "/",
                    &.{
                        std.mem.trimRight(u8, entry.archive_path, "/"),
                        std.mem.trimLeft(u8, dir_entry.path, "/"),
                    },
                );

                switch (dir_entry.kind) {
                    .file => {
                        try addFileToArchive(archive, allocator, full_source_path, full_archive_path);
                    },
                    .directory => {
                        try addDirToArchive(archive, try createZipArchivePathZ(allocator, full_archive_path, true));
                    },
                    else => {
                        std.log.warn("Skipping unsupported file type: {s}\n", .{full_source_path});
                    },
                }
            }
        },
        else => {
            std.log.warn("Skipping unsupported file type: {s}\n", .{entry.source_path});
        },
    }
}

fn createArchive(
    archive_type: ArchiveType,
    allocator: std.mem.Allocator,
    out_path: []const u8,
    files: []const FileEntry,
) !void {
    switch (archive_type) {
        .zip => {
            // Create archive.
            var archive: Archive = .{ .zip = .{} };
            if (c.mz_zip_writer_init_heap(&archive.zip, 0, 65536) == c.MZ_FALSE) {
                return error.ZipInitFailed;
            }
            defer _ = c.mz_zip_writer_end(&archive.zip);

            // Add files and directories to archive.
            for (files) |file| {
                try addFileOrDir(&archive, allocator, file);
            }

            // Get the finalized heap archive (this also finalizes the archive). We don't bother freeing this.
            var archive_data: ?*anyopaque = null;
            var archive_size: usize = 0;
            if (c.mz_zip_writer_finalize_heap_archive(&archive.zip, &archive_data, &archive_size) == c.MZ_FALSE) {
                return error.ZipFinalizeHeapFailed;
            }

            std.debug.assert(archive_data != null);

            // Write the archive data to file.
            std.fs.cwd().writeFile(.{
                .sub_path = out_path,
                .data = @as([*]const u8, @ptrCast(archive_data.?))[0..archive_size],
                .flags = .{},
            }) catch |err| {
                std.log.err("Failed to write archive to file: {s}\n", .{out_path});
                return err;
            };
        },
        .tar_gz => {
            const out_file = try std.fs.cwd().createFile(out_path, .{});
            defer out_file.close();

            var buffered_writer = std.io.bufferedWriter(out_file.writer());

            var compressor = try std.compress.gzip.compressor(buffered_writer.writer(), .{ .level = .default });

            const tar_writer = std.tar.writer(compressor.writer().any());

            var archive: Archive = .{ .tar_gz = tar_writer };

            // Add files and directories to archive
            for (files) |file| {
                try addFileOrDir(&archive, allocator, file);
            }

            try archive.tar_gz.finish();
            try compressor.finish();
            try buffered_writer.flush();
        },
    }
}

pub fn main() !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    const allocator = arena.allocator();

    const args = try std.process.argsAlloc(allocator);

    // Expecting at least: program name, output zip path, and one file pair (source and archive path).
    std.debug.assert(args.len >= 4);

    // First argument is archive type.
    const archive_type = std.meta.stringToEnum(ArchiveType, args[1]) orelse {
        std.log.err("Invalid archive type: {s}\n", .{args[1]});
        return error.InvalidArchiveType;
    };

    // Second argument is output zip path.
    const out_path = args[2];

    // Remaining arguments are in pairs: source path and archive path.
    const file_inputs = args[3..];
    std.debug.assert(file_inputs.len % 2 == 0);

    var files: []FileEntry = try allocator.alloc(FileEntry, file_inputs.len / 2);
    {
        var file_index: usize = 0;
        var i: usize = 0;
        while (i < file_inputs.len) : (i += 2) {
            files[file_index] = FileEntry{
                .source_path = file_inputs[i],
                .archive_path = file_inputs[i + 1],
            };
            file_index += 1;
        }
    }

    try createArchive(archive_type, allocator, out_path, files);
}
