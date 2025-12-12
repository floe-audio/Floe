// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

#include "common_errors.hpp"

struct ChecksumValues {
    u32 crc32;
    usize file_size;
};

struct ChecksumLine {
    String path; // relative to the root of the folder, POSIX-style
    u32 crc32;
    usize file_size;
};

using ChecksumTable = HashTable<String, ChecksumValues>;

// similar format to Unix cksum - except cksum uses a different crc algorithm
PUBLIC void AppendChecksumLine(DynamicArray<char>& buffer, ChecksumLine line) {
    if constexpr (IS_WINDOWS)
        for (auto c : line.path)
            ASSERT(c != '\\');

    fmt::Append(buffer, "{08x} {} {}\n", line.crc32, line.file_size, line.path);
}

PUBLIC void AppendCommentLine(DynamicArray<char>& buffer, String comment) {
    fmt::Append(buffer, "; {}\n", comment);
}

PUBLIC String SerialiseChecksumsValues(HashTable<String, ChecksumValues> checksum_values,
                                       Allocator& allocator,
                                       String comment) {
    DynamicArray<char> buffer {allocator};
    if (comment.size) AppendCommentLine(buffer, comment);
    for (auto const& [path, checksum, _] : checksum_values)
        AppendChecksumLine(
            buffer,
            ChecksumLine {.path = path, .crc32 = checksum.crc32, .file_size = checksum.file_size});
    return buffer.ToOwnedSpan();
}

PUBLIC ErrorCodeOr<void>
WriteChecksumsValuesToFile(String path, ChecksumTable checksum_values, Allocator& allocator, String comment) {
    auto const data = SerialiseChecksumsValues(checksum_values, allocator, comment);
    TRY(WriteFile(path, data));
    return k_success;
}

// A parser for the checksum file format
struct ChecksumFileParser {
    static String CutStart(String& whole, usize size) {
        auto const result = whole.SubSpan(0, size);
        whole = whole.SubSpan(size);
        return result;
    }

    ErrorCodeOr<Optional<ChecksumLine>> ReadLine() {
        while (auto opt_line = SplitWithIterator(file_data, cursor, '\n')) {
            auto line = *opt_line;

            if (line.size == 0) continue;
            if (StartsWith(line, ';')) continue;

            auto const crc = TRY_OPT_OR(ParseIntTrimString(line, ParseIntBase::Hexadecimal, false),
                                        return ErrorCode {CommonError::InvalidFileFormat});

            if (!StartsWith(line, ' ')) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1);

            auto const file_size = TRY_OPT_OR(ParseIntTrimString(line, ParseIntBase::Decimal, false),
                                              return ErrorCode {CommonError::InvalidFileFormat});

            if (!StartsWith(line, ' ')) return ErrorCode {CommonError::InvalidFileFormat};
            line.RemovePrefix(1);
            auto const path = line;

            return ChecksumLine {
                .path = path,
                .crc32 = (u32)crc,
                .file_size = (usize)file_size,
            };
        }

        return k_nullopt;
    }

    String const file_data;
    usize cursor = 0uz;
};

PUBLIC u32 Crc32(Span<u8 const> data) {
    return CheckedCast<u32>(mz_crc32(MZ_CRC32_INIT, data.data, data.size));
}

PUBLIC ErrorCodeOr<ChecksumTable> ParseChecksumFile(String checksum_file_data,
                                                    ArenaAllocator& scratch_arena) {
    DynamicHashTable<String, ChecksumValues> checksum_values(scratch_arena);
    ChecksumFileParser parser {checksum_file_data};
    while (auto line = TRY(parser.ReadLine()))
        checksum_values.Insert(line->path, ChecksumValues {line->crc32, line->file_size});
    return checksum_values.ToOwnedTable();
}

PUBLIC ErrorCodeOr<u32> ChecksumForFile(String path, ArenaAllocator& scratch_arena) {
    auto const file_data = TRY(ReadEntireFile(path, scratch_arena)).ToByteSpan();
    DEFER {
        if (file_data.size) scratch_arena.Free(file_data);
    };
    return Crc32(file_data);
}

PUBLIC ErrorCodeOr<ChecksumTable>
ChecksumsForFolder(String folder, ArenaAllocator& arena, ArenaAllocator& scratch_arena) {
    ChecksumTable checksums {};

    auto it = TRY(dir_iterator::RecursiveCreate(scratch_arena,
                                                folder,
                                                {
                                                    .wildcard = "*",
                                                    .get_file_size = true,
                                                    .skip_dot_files = false,
                                                }));
    DEFER { dir_iterator::Destroy(it); };

    while (auto entry = TRY(dir_iterator::Next(it, arena))) {
        if (entry->type == FileType::File) {

            auto relative_path = entry->subpath;
            if constexpr (IS_WINDOWS) {
                // we use POSIX-style paths in the checksum file
                Replace(relative_path, '\\', '/');
            }
            ASSERT(relative_path.size);
            ASSERT(relative_path[0] != '/');

            auto const file_data =
                TRY(ReadEntireFile(dir_iterator::FullPath(it, *entry, scratch_arena), scratch_arena))
                    .ToByteSpan();
            DEFER {
                if (file_data.size) scratch_arena.Free(file_data);
            };

            checksums.InsertGrowIfNeeded(arena,
                                         relative_path,
                                         ChecksumValues {
                                             .crc32 = Crc32(file_data),
                                             .file_size = entry->file_size,
                                         });
        }
    }

    return checksums;
}

enum class CompareChecksumsResult { Same, Differ, SameButHasExtraFiles };

struct CompareChecksumsOptions {
    bool test_table_allowed_extra_files = false;
    bool ignore_extra_auto_generated_files = false; // When allowing extra files, ignore .DS_Store, etc.
    Optional<Writer> diff_log = {};
};

// Path is a relative path in a folder. Such as "foo.txt", or "Documents/my-doc.md".
static bool PathIsAutoGenerated(String path) {
    if constexpr (IS_MACOS) {
        if (path::Filename(path) == ".DS_Store") return true;
    }
    return false;
}

PUBLIC CompareChecksumsResult CompareChecksums(ChecksumTable const& authority,
                                               ChecksumTable const& test_table,
                                               CompareChecksumsOptions const& options) {
    // We can do some early-out checks.
    if (!options.diff_log) {
        if (!options.test_table_allowed_extra_files) {
            if (authority.size != test_table.size) return CompareChecksumsResult::Differ;
        } else {
            if (test_table.size < authority.size) return CompareChecksumsResult::Differ;
        }
    }

    for (auto const [key, a_val, key_hash] : authority) {
        if (auto const b_val = test_table.FindElement(key, key_hash)) {
            if (a_val.crc32 != b_val->data.crc32 || a_val.file_size != b_val->data.file_size) {
                if (options.diff_log)
                    auto _ = fmt::FormatToWriter(*options.diff_log, "File has changed: {}\n", key);
                return CompareChecksumsResult::Differ;
            }
        } else {
            if (options.diff_log)
                auto _ = fmt::FormatToWriter(*options.diff_log, "File is missing: {}\n", key);
            return CompareChecksumsResult::Differ;
        }
    }

    // At this stage we know that all the files are present and match. Now we check for extras.
    ASSERT(test_table.size >= authority.size);

    if (!options.test_table_allowed_extra_files) {
        if (test_table.size != authority.size) {
            bool all_files_are_auto_generated = true;
            for (auto const [key, _, key_hash] : test_table)
                if (!authority.Find(key, key_hash)) {
                    if (options.diff_log)
                        auto _ = fmt::FormatToWriter(*options.diff_log, "File is extra: {}\n", key);
                    if (!PathIsAutoGenerated(key)) all_files_are_auto_generated = false;
                }

            return all_files_are_auto_generated ? CompareChecksumsResult::Same
                                                : CompareChecksumsResult::SameButHasExtraFiles;
        }
    }

    return CompareChecksumsResult::Same;
}

PUBLIC ErrorCodeOr<bool>
FileMatchesChecksum(String filepath, ChecksumValues const& checksum, ArenaAllocator& scratch_arena) {
    auto f = TRY(OpenFile(filepath, FileMode::Read()));
    auto const file_size = TRY(f.FileSize());
    return file_size == checksum.file_size &&
           Crc32(TRY(f.ReadWholeFile(scratch_arena)).ToByteSpan()) == checksum.crc32;
}
