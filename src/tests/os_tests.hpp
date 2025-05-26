// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <time.h>

#include "os/filesystem.hpp"
#include "os/web.hpp"
#include "tests/framework.hpp"
#include "utils/json/json_reader.hpp"

TEST_CASE(TestEpochTime) {
    auto check_approx = [&](s64 a, s64 b, Optional<s64> wrap_max) {
        auto b_below = b - 1;
        if (wrap_max && b_below < 0) b_below = wrap_max.Value();
        auto b_above = b + 1;
        if (wrap_max && b_above > wrap_max.Value()) b_above = 0;
        CHECK(a == b || a == b_below || a == b_above);
    };

    auto check_against_std = [&](DateAndTime t, tm const& std_time) {
        check_approx(t.year, std_time.tm_year + 1900, {});
        check_approx(t.months_since_jan, std_time.tm_mon, 11);
        check_approx(t.day_of_month, std_time.tm_mday, 31);
        check_approx(t.hour, std_time.tm_hour, 23);
        check_approx(t.minute, std_time.tm_min, 59);
        check_approx(t.second, std_time.tm_sec, 59);
    };

    SUBCASE("local") {
        auto const ns = NanosecondsSinceEpoch();
        auto const t = LocalTimeFromNanosecondsSinceEpoch(ns);

        auto std_time = time(nullptr);
        auto std_local_time = *localtime(&std_time); // NOLINT(concurrency-mt-unsafe)

        check_against_std(t, std_local_time);
    }

    SUBCASE("utc") {
        auto const ns = NanosecondsSinceEpoch();
        auto const t = UtcTimeFromNanosecondsSinceEpoch(ns);

        auto std_time = time(nullptr);
        auto std_utc_time = *gmtime(&std_time); // NOLINT(concurrency-mt-unsafe)
        check_against_std(t, std_utc_time);
    }

    SUBCASE("datetime to ns") {
        DateAndTime dt {
            .year = 1970,
            .months_since_jan = 0,
            .day_of_month = 1,
            .hour = 0,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 0);

        // One day after epoch
        dt = {
            .year = 1970,
            .months_since_jan = 0,
            .day_of_month = 2,
            .hour = 0,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 86400 * (s128)1'000'000'000);

        // Epoch timestamp: 1739464477
        // Date and time (GMT): Thursday, 13 February 2025 16:34:37
        dt = {
            .year = 2025,
            .months_since_jan = 1,
            .day_of_month = 13,
            .hour = 16,
            .minute = 34,
            .second = 37,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 1739464477 * (s128)1'000'000'000);

        // Epoch timestamp: 951755677
        // Date and time (GMT): Monday, 28 February 2000 16:34:37
        dt = {
            .year = 2000,
            .months_since_jan = 1,
            .day_of_month = 28,
            .hour = 16,
            .minute = 34,
            .second = 37,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 951755677 * (s128)1'000'000'000);

        // Epoch timestamp: 951825600
        // Date and time (GMT): Tuesday, 29 February 2000 12:00:00
        dt = {
            .year = 2000,
            .months_since_jan = 1,
            .day_of_month = 29,
            .hour = 12,
            .minute = 0,
            .second = 0,
            .millisecond = 0,
        };
        CHECK_EQ(NanosecondsSinceEpoch(dt), 951825600 * (s128)1'000'000'000);
    }

    return k_success;
}

TEST_CASE(TestFileApi) {
    auto& scratch_arena = tester.scratch_arena;
    auto const filename1 = path::Join(scratch_arena, Array {tests::TempFolder(tester), "filename1"});
    auto const filename2 = path::Join(scratch_arena, Array {tests::TempFolder(tester), "filename2"});
    DEFER { auto _ = Delete(filename1, {}); };
    DEFER { auto _ = Delete(filename2, {}); };
    constexpr auto k_data = "data"_s;

    SUBCASE("Write and read") {
        TRY(CreateDirectory(tests::TempFolder(tester), {.create_intermediate_directories = true}));

        SUBCASE("Open API") {
            {
                auto f = TRY(OpenFile(filename1, FileMode::Write()));
                CHECK(f.Write(k_data.ToByteSpan()).HasValue());
            }
            {
                auto f = TRY(OpenFile(filename1, FileMode::Read()));
                CHECK_EQ(TRY(f.FileSize()), k_data.size);
                CHECK_EQ(TRY(f.ReadWholeFile(scratch_arena)), k_data);
            }
        }
        SUBCASE("read-all API") {
            TRY(WriteFile(filename1, k_data.ToByteSpan()));
            CHECK_EQ(TRY(ReadEntireFile(filename1, scratch_arena)), k_data);
        }
    }

    SUBCASE("Seek") {
        TRY(WriteFile(filename1, k_data.ToByteSpan()));
        auto f = TRY(OpenFile(filename1, FileMode::Read()));
        TRY(f.Seek(2, File::SeekOrigin::Start));
        char buffer[2];
        CHECK_EQ(TRY(f.Read(buffer, 2)), 2u);
        CHECK_EQ(String(buffer, 2), k_data.SubSpan(2));
    }

    SUBCASE("Lock a file") {
        for (auto const type : Array {FileLockOptions::Type::Exclusive, FileLockOptions::Type::Shared}) {
            for (auto const non_blocking : Array {true, false}) {
                auto f = TRY(OpenFile(filename1, FileMode::Write()));
                auto locked = TRY(f.Lock({.type = type, .non_blocking = non_blocking}));
                CHECK(locked);
                if (locked) TRY(f.Unlock());
            }
        }
    }

    SUBCASE("Move a File object") {
        auto f = OpenFile(filename1, FileMode::Read());
        auto f2 = Move(f);
    }

    SUBCASE("Read from one large file and write to another") {
        auto buffer = tester.scratch_arena.AllocateExactSizeUninitialised<u8>(Mb(8));
        {
            auto f = TRY(OpenFile(filename1, FileMode::Write()));
            FillMemory(buffer, 'a');
            TRY(f.Write(buffer));
            FillMemory(buffer, 'b');
            TRY(f.Write(buffer));
        }

        {
            auto f = TRY(OpenFile(filename1, FileMode::Read()));

            {
                TRY(ReadSectionOfFileAndWriteToOtherFile(f, 0, buffer.size, filename2));
                auto f2 = TRY(ReadEntireFile(filename2, tester.scratch_arena));
                FillMemory(buffer, 'a');
                CHECK(f2.ToByteSpan() == buffer);
            }

            {
                TRY(ReadSectionOfFileAndWriteToOtherFile(f, buffer.size, 8, filename2));
                auto f2 = TRY(ReadEntireFile(filename2, tester.scratch_arena));
                FillMemory({buffer.data, 8}, 'b');
                CHECK(f2.ToByteSpan() == Span<u8> {buffer.data, 8});
            }
        }
    }

    SUBCASE("Last modified time") {
        auto time = NanosecondsSinceEpoch();
        {
            auto f = TRY(OpenFile(filename1, FileMode::Write()));
            TRY(f.Write(k_data.ToByteSpan()));
            TRY(f.Flush());
            TRY(f.SetLastModifiedTimeNsSinceEpoch(time));
        }
        {
            auto f = TRY(OpenFile(filename1, FileMode::Read()));
            auto last_modified = TRY(f.LastModifiedTimeNsSinceEpoch());
            CHECK_EQ(last_modified, time);
        }
    }

    SUBCASE("Try opening a file that does not exist") {
        auto const f = OpenFile("foo", FileMode::Read());
        REQUIRE(f.HasError());
    }

    SUBCASE("Try reading an entire file that does not exist") {
        auto const data = ReadEntireFile("foo", tester.scratch_arena);
        REQUIRE(data.HasError());
    }
    return k_success;
}

TEST_CASE(TestFilesystem) {
    auto& a = tester.scratch_arena;

    SUBCASE("DirectoryIteratorV2") {
        auto dir = String(path::Join(a, Array {tests::TempFolder(tester), "DirectoryIteratorV2 test"}));
        auto _ = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));
        DEFER {
            if (auto o = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively}); o.HasError())
                LOG_WARNING("failed to delete temp dir: {}", o.Error());
        };

        SUBCASE("empty dir") {
            SUBCASE("non-recursive") {
                auto it = REQUIRE_UNWRAP(dir_iterator::Create(a, dir, {}));
                DEFER { dir_iterator::Destroy(it); };
                auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                CHECK(!opt_entry.HasValue());
            }
            SUBCASE("recursive") {
                auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a, dir, {}));
                DEFER { dir_iterator::Destroy(it); };
                auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                CHECK(!opt_entry.HasValue());
            }
        }

        SUBCASE("dir with files") {
            auto const file1 = path::Join(a, Array {dir, "file1.txt"_s});
            auto const file2 = path::Join(a, Array {dir, "file2.txt"_s});
            auto const file3 = path::Join(a, Array {dir, ".file3.wav"_s});
            auto const subdir1 = String(path::Join(a, Array {dir, "subdir1"_s}));
            auto const subdir1_file1 = path::Join(a, Array {subdir1, "subdir1_file1.txt"_s});
            auto const subdir2 = String(path::Join(a, Array {dir, "subdir2"_s}));
            auto const subdir2_file1 = path::Join(a, Array {subdir2, "subdir2_file1.txt"_s});
            auto const subdir2_subdir = String(path::Join(a, Array {subdir2, "subdir2_subdir"_s}));

            TRY(CreateDirectory(subdir1, {.create_intermediate_directories = false}));
            TRY(CreateDirectory(subdir2, {.create_intermediate_directories = false}));
            TRY(CreateDirectory(subdir2_subdir, {.create_intermediate_directories = false}));

            TRY(WriteFile(file1, "data"_s.ToByteSpan()));
            TRY(WriteFile(file2, "data"_s.ToByteSpan()));
            TRY(WriteFile(file3, "data"_s.ToByteSpan()));
            TRY(WriteFile(subdir1_file1, "data"_s.ToByteSpan()));
            TRY(WriteFile(subdir2_file1, "data"_s.ToByteSpan()));

            auto contains = [](Span<dir_iterator::Entry const> entries, dir_iterator::Entry entry) {
                for (auto const& e : entries)
                    if (e.subpath == entry.subpath && e.type == entry.type) return true;
                return false;
            };
            DynamicArrayBounded<dir_iterator::Entry, 10> entries;

            SUBCASE("non-recursive") {
                SUBCASE("standard options") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 5u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone(".file3.wav"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                }

                SUBCASE("skip dot files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = true,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 4u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                }

                SUBCASE("only .txt files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*.txt",
                                                                      .get_file_size = false,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 2u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                }

                SUBCASE("get file size") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "*",
                                                                      .get_file_size = true,
                                                                      .skip_dot_files = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };
                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        if (opt_entry->type == FileType::File) CHECK_EQ(opt_entry->file_size, 4u);
                }

                SUBCASE("no files matching pattern") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::Create(a,
                                                                  dir,
                                                                  {
                                                                      .wildcard = "sef9823ksdjf39s*",
                                                                      .get_file_size = false,
                                                                  }));
                    DEFER { dir_iterator::Destroy(it); };
                    auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a));
                    CHECK(!opt_entry.HasValue());
                }

                SUBCASE("non existent dir") {
                    auto it = dir_iterator::Create(a,
                                                   IS_WINDOWS ? "C:/seflskflks"_s : "/aoidlkdsf"_s,
                                                   {
                                                       .wildcard = "*",
                                                       .get_file_size = false,
                                                   });
                    // Create is allow to succeed even if the path does not exist.
                    if (it.HasValue()) {
                        auto const next = dir_iterator::Next(it.Value(), a);
                        CHECK(next.HasError() && next.Error() == FilesystemError::PathDoesNotExist);
                    } else {
                        CHECK(it.Error() == FilesystemError::PathDoesNotExist);
                    }
                }
            }

            SUBCASE("recursive") {
                SUBCASE("standard options") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = false,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 8u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone(".file3.wav"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_subdir"_s}),
                                    .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }

                SUBCASE("skip dot files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = true,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 7u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir1"_s), .type = FileType::Directory}));
                    CHECK(contains(entries, {.subpath = a.Clone("subdir2"_s), .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_subdir"_s}),
                                    .type = FileType::Directory}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }

                SUBCASE("only .txt files") {
                    auto it = REQUIRE_UNWRAP(dir_iterator::RecursiveCreate(a,
                                                                           dir,
                                                                           {
                                                                               .wildcard = "*.txt",
                                                                               .get_file_size = false,
                                                                               .skip_dot_files = false,
                                                                           }));
                    DEFER { dir_iterator::Destroy(it); };

                    while (auto opt_entry = REQUIRE_UNWRAP(dir_iterator::Next(it, a)))
                        dyn::Append(entries, opt_entry.Value());

                    CHECK_EQ(entries.size, 4u);
                    CHECK(contains(entries, {.subpath = a.Clone("file1.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries, {.subpath = a.Clone("file2.txt"_s), .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir1"_s, "subdir1_file1.txt"_s}),
                                    .type = FileType::File}));
                    CHECK(contains(entries,
                                   {.subpath = path::Join(a, Array {"subdir2"_s, "subdir2_file1.txt"_s}),
                                    .type = FileType::File}));
                }
            }
        }
    }

    SUBCASE("Absolute") {
        auto check = [&](String str, bool expecting_success) {
            CAPTURE(str);
            CAPTURE(expecting_success);
            auto o = AbsolutePath(a, str);
            if (!expecting_success) {
                REQUIRE(o.HasError());
                return;
            }
            if (o.HasError()) {
                LOG_WARNING("Failed to AbsolutePath: {}", o.Error());
                return;
            }
            REQUIRE(o.HasValue());
            tester.log.Debug(o.Value());
            REQUIRE(path::IsAbsolute(o.Value()));
        };

        check("foo", true);
        check("something/foo.bar", true);
        check("/something/foo.bar", true);
    }

    SUBCASE("KnownDirectory") {
        auto error_writer = StdWriter(StdStream::Err);
        for (auto const i : Range(ToInt(KnownDirectoryType::Count))) {
            auto type = (KnownDirectoryType)i;
            auto known_folder = KnownDirectory(a, type, {.create = false, .error_log = &error_writer});
            String type_name = EnumToString(type);
            tester.log.Debug("Found {} dir: {} ", type_name, known_folder);
            CHECK(path::IsAbsolute(known_folder));
        }
    }

    SUBCASE("TemporaryDirectoryOnSameFilesystemAs") {
        auto const abs_path = KnownDirectory(tester.arena, KnownDirectoryType::GlobalData, {.create = true});
        auto temp_dir = TRY(TemporaryDirectoryOnSameFilesystemAs(abs_path, a));
        tester.log.Debug("Temporary directory on same filesystem: {}", temp_dir);
        CHECK(path::IsAbsolute(temp_dir));
        CHECK(GetFileType(temp_dir).HasValue());
    }

    SUBCASE("DeleteDirectory") {
        auto test_delete_directory = [&a, &tester]() -> ErrorCodeOr<void> {
            auto const dir = path::Join(a, Array {tests::TempFolder(tester), "DeleteDirectory test"});
            TRY(CreateDirectory(dir, {.create_intermediate_directories = true}));

            // create files and folders within the dir
            {
                DynamicArray<char> file = {dir, a};
                path::JoinAppend(file, "test_file1.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "test_file2.txt"_s);
                TRY(WriteFile(file, "data"_s.ToByteSpan()));

                dyn::Resize(file, dir.size);
                path::JoinAppend(file, "folder"_s);
                TRY(CreateDirectory(file));
            }

            TRY(Delete(dir, {}));
            CHECK(GetFileType(dir).HasError());
            return k_success;
        };

        if (auto o = test_delete_directory(); o.HasError())
            LOG_WARNING("Failed to test DeleteDirectory: {}", o.Error());
    }

    SUBCASE("CreateDirectory") {
        auto const dir = path::Join(a, Array {tests::TempFolder(tester), "CreateDirectory test"});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false}));
        CHECK(TRY(GetFileType(dir)) == FileType::Directory);
        TRY(Delete(dir, {}));
    }

    SUBCASE("relocate files") {
        auto const dir = String(path::Join(a, Array {tests::TempFolder(tester), "Relocate files test"}));
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false}));
        DEFER { auto _ = Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively}); };

        auto const path1 = path::Join(a, Array {dir, "test-path1"});
        auto const path2 = path::Join(a, Array {dir, "test-path2"});

        SUBCASE("Rename") {
            SUBCASE("basic file rename") {
                TRY(WriteFile(path1, "data"_s.ToByteSpan()));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::File);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("file rename replaces existing") {
                TRY(WriteFile(path1, "data1"_s.ToByteSpan()));
                TRY(WriteFile(path2, "data2"_s.ToByteSpan()));
                TRY(Rename(path1, path2));
                CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("move dir") {
                TRY(CreateDirectory(path1, {.create_intermediate_directories = false}));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::Directory);
                CHECK(GetFileType(path1).HasError());
            }

            SUBCASE("move dir ok if new_name exists but is empty") {
                TRY(CreateDirectory(path1, {.create_intermediate_directories = false}));
                TRY(CreateDirectory(path2, {.create_intermediate_directories = false}));
                TRY(Rename(path1, path2));
                CHECK(TRY(GetFileType(path2)) == FileType::Directory);
                CHECK(GetFileType(path1).HasError());
            }
        }

        SUBCASE("CopyFile") {
            SUBCASE("basic file copy") {
                TRY(WriteFile(path1, "data"_s.ToByteSpan()));
                TRY(CopyFile(path1, path2, ExistingDestinationHandling::Fail));
            }

            SUBCASE("ExistingDesinationHandling") {
                TRY(WriteFile(path1, "data1"_s.ToByteSpan()));
                TRY(WriteFile(path2, "data2"_s.ToByteSpan()));

                SUBCASE("ExistingDestinationHandling::Fail works") {
                    auto const o = CopyFile(path1, path2, ExistingDestinationHandling::Fail);
                    REQUIRE(o.HasError());
                    CHECK(o.Error() == FilesystemError::PathAlreadyExists);
                }

                SUBCASE("ExistingDestinationHandling::Overwrite works") {
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Overwrite));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                }

                SUBCASE("ExistingDestinationHandling::Skip works") {
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Skip));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data2"_s);
                }

                SUBCASE("Overwrite a hidden file") {
                    TRY(WindowsSetFileAttributes(path2, WindowsFileAttributes {.hidden = true}));
                    TRY(CopyFile(path1, path2, ExistingDestinationHandling::Overwrite));
                    CHECK(TRY(ReadEntireFile(path2, a)) == "data1"_s);
                }
            }
        }
    }

    SUBCASE("Trash") {
        SUBCASE("file") {
            auto const filename = tests::TempFilename(tester);
            TRY(WriteFile(filename, "data"_s));
            auto trashed_file = TRY(TrashFileOrDirectory(filename, tester.scratch_arena));
            tester.log.Debug("File in trash: {}", trashed_file);
            CHECK(GetFileType(filename).HasError());
        }

        SUBCASE("folder") {
            auto const folder = tests::TempFilename(tester);
            TRY(CreateDirectory(folder, {.create_intermediate_directories = false}));
            auto const subfile = path::Join(tester.scratch_arena, Array {folder, "subfile.txt"});
            TRY(WriteFile(subfile, "data"_s));
            auto trashed_folder = TRY(TrashFileOrDirectory(folder, tester.scratch_arena));
            tester.log.Debug("Folder in trash: {}", trashed_folder);
        }
    }

    return k_success;
}

TEST_CASE(TestDirectoryWatcher) {
    auto& a = tester.scratch_arena;

    for (auto const recursive : Array {true, false}) {
        CAPTURE(recursive);

        auto const dir = (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-test"});
        auto _ =
            Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively, .fail_if_not_exists = false});
        TRY(CreateDirectory(dir, {.create_intermediate_directories = false, .fail_if_exists = true}));

        struct TestPath {
            static TestPath Create(ArenaAllocator& a, String root_dir, String subpath) {
                auto const full = path::Join(a, Array {root_dir, subpath});
                return TestPath {
                    .full_path = full,
                    .subpath = full.SubSpan(full.size - subpath.size, subpath.size),
                };
            }
            String full_path;
            String subpath;
        };

        auto const file = TestPath::Create(a, dir, "file1.txt");
        TRY(WriteFile(file.full_path, "data"));

        auto const subdir = TestPath::Create(a, dir, "subdir");
        TRY(CreateDirectory(subdir.full_path,
                            {.create_intermediate_directories = false, .fail_if_exists = true}));

        auto const subfile = TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2.txt"}));
        TRY(WriteFile(subfile.full_path, "data"));

        auto watcher = TRY(CreateDirectoryWatcher(a));
        DEFER { DestoryDirectoryWatcher(watcher); };

        auto const dirs_to_watch = Array {DirectoryToWatch {
            .path = dir,
            .recursive = recursive,
        }};
        auto const args = PollDirectoryChangesArgs {
            .dirs_to_watch = dirs_to_watch,
            .retry_failed_directories = false,
            .result_arena = a,
            .scratch_arena = a,
        };

        if (auto const dir_changes_span = TRY(PollDirectoryChanges(watcher, args)); dir_changes_span.size) {
            tester.log.Debug("Unexpected result");
            for (auto const& dir_changes : dir_changes_span) {
                tester.log.Debug("  {}", dir_changes.linked_dir_to_watch->path);
                tester.log.Debug("  {}", dir_changes.error);
                for (auto const& subpath_changeset : dir_changes.subpath_changesets)
                    tester.log.Debug("    {} {}",
                                     subpath_changeset.subpath,
                                     DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes));
            }
            REQUIRE(false);
        }

        auto check = [&](Span<DirectoryWatcher::DirectoryChanges::Change const> expected_changes)
            -> ErrorCodeOr<void> {
            auto found_expected = a.NewMultiple<bool>(expected_changes.size);

            // we give the watcher some time and a few attempts to detect the changes
            for (auto const _ : Range(100)) {
                SleepThisThread(2);
                auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));

                for (auto const& directory_changes : directory_changes_span) {
                    auto const& path = directory_changes.linked_dir_to_watch->path;

                    CHECK(path::Equal(path, dir));
                    if (directory_changes.error) {
                        tester.log.Debug("Error in {}: {}", path, *directory_changes.error);
                        continue;
                    }
                    CHECK(!directory_changes.error.HasValue());

                    for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                        if (subpath_changeset.changes & DirectoryWatcher::ChangeType::ManualRescanNeeded) {
                            tester.log.Error("Manual rescan needed for {}", path);
                            continue;
                        }

                        bool was_expected = false;
                        for (auto const [index, expected] : Enumerate(expected_changes)) {
                            if (path::Equal(subpath_changeset.subpath, expected.subpath) &&
                                (!subpath_changeset.file_type.HasValue() ||
                                 subpath_changeset.file_type.Value() == expected.file_type)) {
                                if (expected.changes & subpath_changeset.changes) {
                                    was_expected = true;
                                    found_expected[index] = true;
                                    break;
                                }
                            }
                        }

                        tester.log.Debug("{} change: \"{}\" {{ {} }} in \"{}\"",
                                         was_expected ? "Expected" : "Unexpected",
                                         subpath_changeset.subpath,
                                         DirectoryWatcher::ChangeType::ToString(subpath_changeset.changes),
                                         path);
                    }
                }

                if (ContainsOnly(found_expected, true)) break;
            }

            for (auto const [index, expected] : Enumerate(expected_changes)) {
                CAPTURE(expected.subpath);
                CAPTURE(DirectoryWatcher::ChangeType::ToString(expected.changes));
                if (!found_expected[index]) {
                    tester.log.Debug("Expected change not found: {} {}",
                                     expected.subpath,
                                     DirectoryWatcher::ChangeType::ToString(expected.changes));
                }
                CHECK(found_expected[index]);
            }

            return k_success;
        };

        SUBCASE(recursive ? "recursive"_s : "non-recursive"_s) {
            SUBCASE("delete is detected") {
                TRY(Delete(file.full_path, {}));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Deleted,
                }}));
            }

            SUBCASE("modify is detected") {
                TRY(WriteFile(file.full_path, "new data"));
                TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                    file.subpath,
                    FileType::File,
                    DirectoryWatcher::ChangeType::Modified,
                }}));
            }

            SUBCASE("rename is detected") {
                auto const new_file = TestPath::Create(a, dir, "file1_renamed.txt");
                TRY(Rename(file.full_path, new_file.full_path));
                TRY(check(Array {
                    DirectoryWatcher::DirectoryChanges::Change {
                        file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedOldName,
                    },
                    DirectoryWatcher::DirectoryChanges::Change {
                        new_file.subpath,
                        FileType::File,
                        IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                 : DirectoryWatcher::ChangeType::RenamedNewName,
                    },
                }));
            }

            // On Windows, the root folder does not receive events
            if constexpr (!IS_WINDOWS) {
                SUBCASE("deleting root is detected") {
                    auto const delete_outcome =
                        Delete(dir, {.type = DeleteOptions::Type::DirectoryRecursively});
                    if (!delete_outcome.HasError()) {
                        auto args2 = args;
                        bool found_delete_self = false;
                        for (auto const _ : Range(4)) {
                            SleepThisThread(5);
                            auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args2));
                            for (auto const& directory_changes : directory_changes_span) {
                                for (auto const& subpath_changeset : directory_changes.subpath_changesets) {
                                    if (subpath_changeset.subpath.size == 0 &&
                                        subpath_changeset.changes & DirectoryWatcher::ChangeType::Deleted) {
                                        CHECK(subpath_changeset.file_type == FileType::Directory);
                                        found_delete_self = true;
                                        args2.dirs_to_watch = {};
                                        break;
                                    }
                                }
                            }
                            if (found_delete_self) break;
                        }
                        CHECK(found_delete_self);
                    } else {
                        tester.log.Debug(
                            "Failed to delete root watched dir: {}. This is probably normal behaviour",
                            delete_outcome.Error());
                    }
                }
            }

            SUBCASE("no crash moving root dir") {
                auto const dir_name = fmt::Format(a, "{}-moved", dir);
                auto const move_outcome = Rename(dir, dir_name);
                if (!move_outcome.HasError()) {
                    DEFER { auto _ = Delete(dir_name, {.type = DeleteOptions::Type::DirectoryRecursively}); };
                    // On Linux, we don't get any events. Perhaps a MOVE only triggers when the underlying
                    // file object really moves and perhaps a rename like this doesn't do that. Either way I
                    // think we just need to check nothing bad happens in this case and that will do.
                } else {
                    tester.log.Debug("Failed to move root watched dir: {}. This is probably normal behaviour",
                                     move_outcome.Error());
                }
            }

            // Wine seems to have trouble with recursive watching
            static auto const recursive_supported = !IsRunningUnderWine();

            if (recursive && recursive_supported) {
                SUBCASE("delete in subfolder is detected") {
                    TRY(Delete(subfile.full_path, {}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("modify is detected") {
                    TRY(WriteFile(subfile.full_path, "new data"));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subfile.subpath,
                        FileType::File,
                        DirectoryWatcher::ChangeType::Modified,
                    }}));
                }

                SUBCASE("rename is detected") {
                    auto const new_subfile =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir.subpath, "file2_renamed.txt"}));
                    TRY(Rename(subfile.full_path, new_subfile.full_path));
                    TRY(check(Array {
                        DirectoryWatcher::DirectoryChanges::Change {
                            subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedOldName,
                        },
                        DirectoryWatcher::DirectoryChanges::Change {
                            new_subfile.subpath,
                            FileType::File,
                            IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                     : DirectoryWatcher::ChangeType::RenamedNewName,
                        },
                    }));
                }

                SUBCASE("deleting subfolder is detected") {
                    TRY(Delete(subdir.full_path, {.type = DeleteOptions::Type::DirectoryRecursively}));
                    TRY(check(Array {DirectoryWatcher::DirectoryChanges::Change {
                        subdir.subpath,
                        FileType::Directory,
                        DirectoryWatcher::ChangeType::Deleted,
                    }}));
                }

                SUBCASE("newly created subfolder is watched") {
                    // create a new subdir
                    auto const subdir2 = TestPath::Create(a, dir, "subdir2");
                    TRY(CreateDirectory(subdir2.full_path,
                                        {.create_intermediate_directories = false, .fail_if_exists = true}));

                    // create a file within it
                    auto const subfile2 =
                        TestPath::Create(a, dir, path::Join(a, Array {subdir2.subpath, "file2.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        // Windows doesn't seem to give us the subdir2 'added' event
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir2.subpath,
                                FileType::Directory,
                                DirectoryWatcher::ChangeType::Added,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }

                SUBCASE("moved subfolder is still watched") {
                    auto const subdir_moved = TestPath::Create(a, dir, "subdir-moved");
                    TRY(Rename(subdir.full_path, subdir_moved.full_path));

                    auto const subfile2 =
                        TestPath::Create(a,
                                         dir,
                                         path::Join(a, Array {subdir_moved.subpath, "file-in-moved.txt"}));
                    TRY(WriteFile(subfile2.full_path, "data"));

                    if constexpr (IS_WINDOWS) {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    } else {
                        TRY(check(Array {
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedOldName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subdir_moved.subpath,
                                FileType::Directory,
                                IS_MACOS ? DirectoryWatcher::ChangeType::RenamedOldOrNewName
                                         : DirectoryWatcher::ChangeType::RenamedNewName,
                            },
                            DirectoryWatcher::DirectoryChanges::Change {
                                subfile2.subpath,
                                FileType::File,
                                DirectoryWatcher::ChangeType::Added,
                            },
                        }));
                    }
                }
            } else {
                SUBCASE("delete in subfolder is not detected") {
                    TRY(Delete(subfile.full_path, {}));

                    for (auto const _ : Range(2)) {
                        SleepThisThread(2);
                        auto const directory_changes_span = TRY(PollDirectoryChanges(watcher, args));
                        for (auto const& directory_changes : directory_changes_span)
                            for (auto const& subpath_changeset : directory_changes.subpath_changesets)
                                CHECK(!path::Equal(subpath_changeset.subpath, subfile.subpath));
                    }
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestDirectoryWatcherErrors) {
    auto& a = tester.scratch_arena;

    auto const dir =
        (String)path::Join(a, Array {tests::TempFolder(tester), "directory-watcher-errors-test"});

    auto watcher = TRY(CreateDirectoryWatcher(a));
    DEFER { DestoryDirectoryWatcher(watcher); };

    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        // we're not expecting a top-level error, that should only be for if the whole watching system fails
        REQUIRE(outcome.HasValue());

        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    // retrying should not repeat the error unless retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = false,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        CHECK(outcome.Value().size == 0);
    }

    // the error should repeat if retry_failed_directories is set
    {
        auto const outcome = PollDirectoryChanges(watcher,
                                                  PollDirectoryChangesArgs {
                                                      .dirs_to_watch = Array {DirectoryToWatch {
                                                          .path = dir,
                                                          .recursive = false,
                                                      }},
                                                      .retry_failed_directories = true,
                                                      .result_arena = a,
                                                      .scratch_arena = a,
                                                  });

        CHECK(outcome.HasValue());
        auto const directory_changes_span = outcome.Value();
        REQUIRE_EQ(directory_changes_span.size, 1u);
        auto const& directory_changes = directory_changes_span[0];
        REQUIRE(directory_changes.error.HasValue());
        CHECK(directory_changes.error.Value() == FilesystemError::PathDoesNotExist);
    }

    return k_success;
}

TEST_CASE(TestTimePoint) {
    Stopwatch const sw;

    auto t1 = TimePoint::Now();
    SleepThisThread(1);
    REQUIRE(t1.Raw());
    auto t2 = TimePoint::Now();

    auto us = SecondsToMicroseconds(t2 - t1);
    REQUIRE(us >= 0.0);
    REQUIRE(ApproxEqual(SecondsToMilliseconds(t2 - t1), us / 1000.0, 0.1));
    REQUIRE(ApproxEqual(t2 - t1, us / (1000.0 * 1000.0), 0.1));

    tester.log.Debug("Time has passed: {}", sw);
    return k_success;
}

TEST_CASE(TestMutex) {
    Mutex m;
    m.Lock();
    m.TryLock();
    m.Unlock();
    return k_success;
}

TEST_CASE(TestFutex) {
    for (auto wake_mode : Array {NumWaitingThreads::One, NumWaitingThreads::All}) {
        Atomic<u32> atomic {0};

        Thread thread;
        thread.Start(
            [&]() {
                SleepThisThread(1);
                atomic.Store(1, StoreMemoryOrder::Relaxed);
                WakeWaitingThreads(atomic, wake_mode);
            },
            "thread");

        while (atomic.Load(LoadMemoryOrder::Relaxed) == 1)
            WaitIfValueIsExpected(atomic, 1, {});

        thread.Join();
    }

    {
        Atomic<u32> atomic {0};
        CHECK_EQ(WaitIfValueIsExpected(atomic, 0, 1u), WaitResult::TimedOut);
    }
    return k_success;
}

int g_global_int = 0;

TEST_CASE(TestThread) {
    Thread thread;
    REQUIRE(!thread.Joinable());

    thread.Start(
        []() {
            g_global_int = 1;
            SleepThisThread(1);
        },
        "test-thread");

    REQUIRE(thread.Joinable());
    thread.Join();

    REQUIRE(g_global_int == 1);
    return k_success;
}

TEST_CASE(TestCallOnce) {
    CallOnceFlag flag {};
    int i = 0;
    CHECK(!flag.Called());
    CallOnce(flag, [&]() { i = 1; });
    CHECK(flag.Called());
    CHECK_EQ(i, 1);
    CallOnce(flag, [&]() { i = 2; });
    CHECK_EQ(i, 1);
    return k_success;
}

TEST_CASE(TestLockableSharedMemory) {
    SUBCASE("Basic creation and initialization") {
        constexpr usize k_size = 1024;
        auto mem1 = TRY(CreateLockableSharedMemory("test1"_s, k_size));
        // Check size is correct
        CHECK_EQ(mem1.data.size, k_size);
        // Check memory is zero initialized
        for (usize i = 0; i < k_size; i++)
            CHECK_EQ(mem1.data[i], 0);
    }

    SUBCASE("Multiple opens see same memory") {
        constexpr usize k_size = 1024;
        auto mem1 = TRY(CreateLockableSharedMemory("test2"_s, k_size));
        auto mem2 = TRY(CreateLockableSharedMemory("test2"_s, k_size));

        // Write pattern through first mapping
        LockSharedMemory(mem1);
        for (usize i = 0; i < k_size; i++)
            mem1.data[i] = (u8)(i & 0xFF);
        UnlockSharedMemory(mem1);

        // Verify pattern through second mapping
        LockSharedMemory(mem2);
        for (usize i = 0; i < k_size; i++)
            CHECK_EQ(mem2.data[i], (u8)(i & 0xFF));
        UnlockSharedMemory(mem2);
    }

    return k_success;
}

TEST_CASE(TestOsRandom) {
    CHECK_NEQ(RandomSeed(), 0u);
    return k_success;
}

TEST_CASE(TestGetInfo) {
    GetOsInfo();
    GetSystemStats();
    return k_success;
}

TEST_CASE(TestWeb) {
    // get/post to httpbin.org
    WebGlobalInit();
    DEFER { WebGlobalCleanup(); };

    {
        DynamicArray<char> buffer {tester.scratch_arena};
        auto o = HttpsGet("https://httpbin.org/get", dyn::WriterFor(buffer));
        if (o.HasError()) {
            LOG_WARNING("Failed to HttpsGet: {}", o.Error());
        } else {
            tester.log.Debug("GET response: {}", buffer);

            if (!ContainsCaseInsensitiveAscii(buffer, "unavailable"_s)) {
                using namespace json;
                auto parse_o = json::Parse(buffer,
                                           [&](EventHandlerStack&, Event const& event) {
                                               String url;
                                               if (SetIfMatchingRef(event, "url", url)) {
                                                   CHECK_EQ(url, "https://httpbin.org/get"_s);
                                                   return true;
                                               }
                                               return false;
                                           },
                                           tester.scratch_arena,
                                           {});
                if (parse_o.HasError())
                    TEST_FAILED("Invalid HTTP GET JSON response: {}", parse_o.Error().message);
            } else {
                LOG_WARNING("HTTP test server is unavailable");
            }
        }
    }

    {
        DynamicArray<char> buffer {tester.scratch_arena};
        auto o = HttpsPost("https://httpbin.org/post",
                           "data",
                           dyn::WriterFor(buffer),
                           {
                               .headers = Array {"Content-Type: text/plain"_s},
                           });
        if (o.HasError()) {
            LOG_WARNING("Failed to HttpsPost: {}", o.Error());
        } else {
            tester.log.Debug("POST response: {}", buffer);

            if (!ContainsCaseInsensitiveAscii(buffer, "unavailable"_s)) {
                using namespace json;
                auto parse_o = json::Parse(buffer,
                                           [&](EventHandlerStack&, Event const& event) {
                                               String data;
                                               if (SetIfMatchingRef(event, "data", data)) {
                                                   CHECK_EQ(data, "data"_s);
                                                   return true;
                                               }
                                               return false;
                                           },
                                           tester.scratch_arena,
                                           {});
                if (parse_o.HasError())
                    TEST_FAILED("Invalid HTTP POST JSON response: {}", parse_o.Error().message);
            } else {
                LOG_WARNING("HTTP test server is unavailable");
            }
        }
    }

    return k_success;
}

TEST_CASE(TestGetEnvVar) {
    SUBCASE("c string version") {
        auto const v = GetEnvironmentVariable("PATH", tester.scratch_arena);
        CHECK(v);
        CHECK(v->size > 0);
        tester.log.Debug("PATH: {}", v);
    }

    SUBCASE("string version") {
        auto const v = GetEnvironmentVariable("PATH"_s, tester.scratch_arena);
        CHECK(v);
        CHECK(v->size > 0);
        tester.log.Debug("PATH: {}", v);
    }

    SUBCASE("non-existant variable") {
        auto const v = GetEnvironmentVariable("FMNDTEBORPDXCMW"_s, tester.scratch_arena);
        CHECK(!v);
    }

    return k_success;
}

TEST_CASE(TestIsRunningUnderDebugger) {
    auto const r = IsRunningUnderDebugger();
    tester.log.Debug("Is running under debugger: {}", r);
    return k_success;
}

TEST_REGISTRATION(RegisterOsTests) {
    REGISTER_TEST(TestCallOnce);
    REGISTER_TEST(TestDirectoryWatcher);
    REGISTER_TEST(TestDirectoryWatcherErrors);
    REGISTER_TEST(TestEpochTime);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestFileApi);
    REGISTER_TEST(TestFilesystem);
    REGISTER_TEST(TestFutex);
    REGISTER_TEST(TestGetEnvVar);
    REGISTER_TEST(TestGetInfo);
    REGISTER_TEST(TestIsRunningUnderDebugger);
    REGISTER_TEST(TestLockableSharedMemory);
    REGISTER_TEST(TestMutex);
    REGISTER_TEST(TestOsRandom);
    REGISTER_TEST(TestThread);
    REGISTER_TEST(TestTimePoint);
    REGISTER_TEST(TestWeb);
}
