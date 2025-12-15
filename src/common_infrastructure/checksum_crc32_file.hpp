// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <miniz.h>

#include "foundation/foundation.hpp"

struct ChecksumValues {
    u32 crc32;
    usize file_size;
};

struct ChecksumLine {
    String path; // Relative to the root of the folder. POSIX-style.
    u32 crc32;
    usize file_size;
};

using ChecksumTable = HashTable<String, ChecksumValues>;

void AppendChecksumLine(DynamicArray<char>& buffer, ChecksumLine line);

void AppendCommentLine(DynamicArray<char>& buffer, String comment);

ErrorCodeOr<void>
WriteChecksumsValuesToFile(String path, ChecksumTable checksum_values, Allocator& allocator, String comment);

// similar format to Unix cksum - except cksum uses a different crc algorithm
ErrorCodeOr<ChecksumTable> ParseChecksumFile(String checksum_file_data, ArenaAllocator& arena);

ErrorCodeOr<u32> ChecksumForFile(String path, ArenaAllocator& scratch_arena);

ErrorCodeOr<ChecksumTable>
ChecksumsForFolder(String folder, ArenaAllocator& arena, ArenaAllocator& scratch_arena);

enum class CompareChecksumsResult { Same, Differ, SameButHasExtraFiles };

struct CompareChecksumsOptions {
    bool ignore_path_nesting = false; // Consider foo/file.txt == bar/file.txt if the checksums match.
    bool test_table_allowed_extra_files = false;
};

CompareChecksumsResult CompareChecksums(ChecksumTable const& authority,
                                        ChecksumTable const& test_table,
                                        CompareChecksumsOptions const& options);

ErrorCodeOr<bool>
FileMatchesChecksum(String filepath, ChecksumValues const& checksum, ArenaAllocator& scratch_arena);
