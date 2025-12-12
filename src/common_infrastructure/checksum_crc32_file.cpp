// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "checksum_crc32_file.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

#include "common_errors.hpp"

static u32 Crc32(Span<u8 const> data) {
    return CheckedCast<u32>(mz_crc32(MZ_CRC32_INIT, data.data, data.size));
}

static String SerialiseChecksumsValues(HashTable<String, ChecksumValues> checksum_values,
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

// similar format to Unix cksum - except cksum uses a different crc algorithm
void AppendChecksumLine(DynamicArray<char>& buffer, ChecksumLine line) {
    if constexpr (IS_WINDOWS)
        for (auto c : line.path)
            ASSERT(c != '\\');

    fmt::Append(buffer, "{08x} {} {}\n", line.crc32, line.file_size, line.path);
}

void AppendCommentLine(DynamicArray<char>& buffer, String comment) { fmt::Append(buffer, "; {}\n", comment); }

ErrorCodeOr<void>
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

ErrorCodeOr<ChecksumTable> ParseChecksumFile(String checksum_file_data, ArenaAllocator& arena) {
    ChecksumTable checksum_values;
    ChecksumFileParser parser {checksum_file_data};
    while (auto const line = TRY(parser.ReadLine()))
        checksum_values.InsertGrowIfNeeded(arena, line->path, ChecksumValues {line->crc32, line->file_size});
    return checksum_values;
}

ErrorCodeOr<u32> ChecksumForFile(String path, ArenaAllocator& scratch_arena) {
    auto const file_data = TRY(ReadEntireFile(path, scratch_arena)).ToByteSpan();
    DEFER {
        if (file_data.size) scratch_arena.Free(file_data);
    };
    return Crc32(file_data);
}

ErrorCodeOr<ChecksumTable>
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

CompareChecksumsResult CompareChecksums(ChecksumTable const& authority,
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

    auto const print_extras_if_needed = [&]() {
        if (!options.diff_log) return;
        for (auto const [key, _, key_hash] : test_table)
            if (!authority.Find(key, key_hash))
                auto _ = fmt::FormatToWriter(*options.diff_log, "File is extra: {}\n", key);
    };

    if (test_table.size == authority.size) {
        return CompareChecksumsResult::Same;
    } else if (!options.test_table_allowed_extra_files) {
        print_extras_if_needed();
        return CompareChecksumsResult::Differ;
    } else if (!options.allowed_extra_files.size) {
        print_extras_if_needed();
        return CompareChecksumsResult::SameButHasExtraFiles;
    } else {
        // There's extra files, but we've been requested to return 'Same' if all these extras are
        // auto-generated files.
        bool all_files_are_auto_generated = true;
        for (auto const [key, _, key_hash] : test_table)
            if (!authority.Find(key, key_hash)) {
                if (options.diff_log)
                    auto _ = fmt::FormatToWriter(*options.diff_log, "File is extra: {}\n", key);
                if (!FindIf(options.allowed_extra_files, [key](auto const& f) {
                        return f.filename_match_only ? path::Filename(key) == f.path : key == f.path;
                    }))
                    all_files_are_auto_generated = false;
            }

        return all_files_are_auto_generated ? CompareChecksumsResult::Same
                                            : CompareChecksumsResult::SameButHasExtraFiles;
    }
}

ErrorCodeOr<bool>
FileMatchesChecksum(String filepath, ChecksumValues const& checksum, ArenaAllocator& scratch_arena) {
    auto f = TRY(OpenFile(filepath, FileMode::Read()));
    auto const file_size = TRY(f.FileSize());
    return file_size == checksum.file_size &&
           Crc32(TRY(f.ReadWholeFile(scratch_arena)).ToByteSpan()) == checksum.crc32;
}

TEST_CASE(TestCompareChecksums) {
    auto const checksum_file1 = "123456 10 file.txt\n"_s
                                "234546 20 bar.txt\n"
                                "deadc0de 1000 filename\n";
    auto const table1 = TRY(ParseChecksumFile(checksum_file1, tester.scratch_arena));
    CHECK_EQ(table1.size, 3u);

    SUBCASE("basic matching tables") {
        CHECK_EQ(CompareChecksums(table1,
                                  table1,
                                  {
                                      .test_table_allowed_extra_files = false,
                                      .allowed_extra_files = {},
                                  }),
                 CompareChecksumsResult::Same);

        CHECK_EQ(CompareChecksums(table1,
                                  table1,
                                  {
                                      .test_table_allowed_extra_files = true,
                                      .allowed_extra_files = {},
                                  }),
                 CompareChecksumsResult::Same);
    }

    SUBCASE("differing") {
        SUBCASE("same num entires") {
            auto const checksum_file2 = "301293 10 foo.txt\n"_s
                                        "3291123 20 baz.txt\n"
                                        "edaec32 1000 filename\n";
            auto const table2 = TRY(ParseChecksumFile(checksum_file2, tester.scratch_arena));
            CHECK_EQ(table2.size, 3u);
            CHECK_EQ(CompareChecksums(table1,
                                      table2,
                                      {
                                          .test_table_allowed_extra_files = false,
                                          .allowed_extra_files = {},
                                      }),
                     CompareChecksumsResult::Differ);
        }

        SUBCASE("more entries") {
            auto const checksum_file2 = "123456 10 file.txt\n"_s
                                        "234546 20 bar.txt\n"
                                        "45123908 20 baz.txt\n"
                                        "deadc0de 1000 filename\n";
            auto const table2 = TRY(ParseChecksumFile(checksum_file2, tester.scratch_arena));
            CHECK_EQ(table2.size, 4u);

            CHECK_EQ(CompareChecksums(table1,
                                      table2,
                                      {
                                          .test_table_allowed_extra_files = false,
                                          .allowed_extra_files = {},
                                      }),
                     CompareChecksumsResult::Differ);
        }

        SUBCASE("less entries") {
            auto const checksum_file2 = "45123908 20 baz.txt\n"_s;
            auto const table2 = TRY(ParseChecksumFile(checksum_file2, tester.scratch_arena));
            CHECK_EQ(table2.size, 1u);

            CHECK_EQ(CompareChecksums(table1,
                                      table2,
                                      {
                                          .test_table_allowed_extra_files = false,
                                          .allowed_extra_files = {},
                                      }),
                     CompareChecksumsResult::Differ);
        }
    }

    SUBCASE("extra files") {
        auto const checksum_file2 = fmt::Format(tester.scratch_arena,
                                                "{}\n"
                                                "851098 23 extra-file.txt\n",
                                                checksum_file1);
        auto const table2 = TRY(ParseChecksumFile(checksum_file2, tester.scratch_arena));
        CHECK_EQ(table2.size, table1.size + 1);

        CHECK_EQ(CompareChecksums(table1,
                                  table2,
                                  {
                                      .test_table_allowed_extra_files = false,
                                      .allowed_extra_files = {},
                                  }),
                 CompareChecksumsResult::Differ);

        CHECK_EQ(CompareChecksums(table1,
                                  table2,
                                  {
                                      .test_table_allowed_extra_files = true,
                                      .allowed_extra_files = {},
                                  }),
                 CompareChecksumsResult::SameButHasExtraFiles);
    }

    SUBCASE("ignore auto-generated files") {
        auto const checksum_file2 = fmt::Format(tester.scratch_arena,
                                                "{}\n"
                                                "851098 23 folder/.DS_Store\n",
                                                checksum_file1);
        auto const table2 = TRY(ParseChecksumFile(checksum_file2, tester.scratch_arena));
        CHECK_EQ(table2.size, table1.size + 1);

        constexpr auto k_extra_allowed = ArrayT<CompareChecksumsOptions::ExtraFile>({
            {".DS_Store", true},
        });
        CHECK_EQ(CompareChecksums(table1,
                                  table2,
                                  {
                                      .test_table_allowed_extra_files = true,
                                      .allowed_extra_files = k_extra_allowed,
                                  }),
                 CompareChecksumsResult::Same);
    }

    return k_success;
}

TEST_CASE(TestChecksumFileParsing) {
    SUBCASE("empty file") {
        ChecksumFileParser parser {
            .file_data = "",
        };
        auto line = TRY(parser.ReadLine());
        CHECK(!line.HasValue());
    }

    SUBCASE("parses lines correctly") {
        auto const file = R"raw(; comment
0f0f0f0f 1234 /path/to/file
abcdef01 5678 /path/to/another/file)raw"_s;
        ChecksumFileParser parser {
            .file_data = file,
        };

        auto line1 = TRY(parser.ReadLine());
        REQUIRE(line1.HasValue());
        CHECK_EQ(line1.Value().path, "/path/to/file"_s);
        CHECK_EQ(line1.Value().crc32, 0x0f0f0f0fu);
        CHECK_EQ(line1.Value().file_size, 1234u);

        auto line2 = TRY(parser.ReadLine());
        REQUIRE(line2.HasValue());
        CHECK_EQ(line2.Value().path, "/path/to/another/file"_s);
        CHECK_EQ(line2.Value().crc32, 0xabcdef01u);
        CHECK_EQ(line2.Value().file_size, 5678u);
    }

    SUBCASE("handles invalid lines") {
        auto parse_line = [](String line) {
            ChecksumFileParser parser {
                .file_data = line,
            };
            return parser.ReadLine();
        };

        CHECK(parse_line("wf39 qwer path"_s).HasError());
        CHECK(parse_line("fff 12321").HasError());
        CHECK(parse_line("1238").HasError());
        CHECK(parse_line("123 23\npath").HasError());
        CHECK(parse_line("123  23 path").HasError());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterChecksumFileTests) {
    REGISTER_TEST(TestChecksumFileParsing);
    REGISTER_TEST(TestCompareChecksums);
}
