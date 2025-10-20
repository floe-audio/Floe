// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tests/framework.hpp"

TEST_CASE(TestPath) {
    auto& scratch_arena = tester.scratch_arena;

    using namespace path;

    SUBCASE("Trim") {
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo/"_s, Format::Posix), "foo"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        CHECK_EQ(TrimDirectorySeparatorsEnd("foo////\\\\"_s, Format::Windows), "foo"_s);

        SUBCASE("windows") {
            // Basic drive paths - should trim normally
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo////"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo/"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/foo"_s, Format::Windows), "C:/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:\\Documents\\"_s, Format::Windows), "C:\\Documents"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:\\Documents\\\\\\\\"_s, Format::Windows),
                     "C:\\Documents"_s);

            // Drive roots - should NOT be trimmed (these are filesystem roots)
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:\\"_s, Format::Windows), "C:\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("D:\\"_s, Format::Windows), "D:\\"_s);

            // Multiple separators after drive root - should trim to single separator
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:////"_s, Format::Windows), "C:/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:\\\\\\\\"_s, Format::Windows), "C:\\"_s);

            // UNC paths - network shares
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\server\\share\\foo\\bar\\"_s, Format::Windows),
                     "\\\\server\\share\\foo\\bar"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\server\\share\\foo\\bar\\\\\\\\"_s, Format::Windows),
                     "\\\\server\\share\\foo\\bar"_s);

            // UNC share roots - should NOT be trimmed (these are network filesystem roots)
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\server\\share\\"_s, Format::Windows),
                     "\\\\server\\share\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\server\\share"_s, Format::Windows),
                     "\\\\server\\share"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\192.168.1.100\\c$\\"_s, Format::Windows),
                     "\\\\192.168.1.100\\c$\\"_s);

            // DOS device paths - should NEVER be trimmed
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\C:\\"_s, Format::Windows), "\\\\?\\C:\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\C:\\temp\\"_s, Format::Windows), "\\\\?\\C:\\temp"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\.\\C:\\"_s, Format::Windows), "\\\\.\\C:\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\.\\PhysicalDrive0\\"_s, Format::Windows),
                     "\\\\.\\PhysicalDrive0\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\Volume{b75e2c83-0000-0000-0000-602f00000000}\\"_s,
                                                Format::Windows),
                     "\\\\?\\Volume{b75e2c83-0000-0000-0000-602f00000000}\\"_s);

            // DOS device UNC paths
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\?\\UNC\\server\\share\\"_s, Format::Windows),
                     "\\\\?\\UNC\\server\\share"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\.\\UNC\\server\\share\\folder\\"_s, Format::Windows),
                     "\\\\.\\UNC\\server\\share\\folder"_s);

            // Root of current drive - should NOT be trimmed
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\"_s, Format::Windows), "\\"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Windows), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\\\\\"_s, Format::Windows), "\\"_s);

            // Drive-relative paths (no separator after colon) - should trim normally
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:temp\\"_s, Format::Windows), "C:temp"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("D:Documents\\files\\"_s, Format::Windows),
                     "D:Documents\\files"_s);

            // Relative paths - should trim normally
            CHECK_EQ(TrimDirectorySeparatorsEnd("folder\\"_s, Format::Windows), "folder"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("folder\\subfolder\\"_s, Format::Windows),
                     "folder\\subfolder"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("..\\parent\\"_s, Format::Windows), "..\\parent"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(".\\current\\"_s, Format::Windows), ".\\current"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("Documents\\\\\\\\\\\\\\\\"_s, Format::Windows),
                     "Documents"_s);

            // Mixed separators - should handle both \ and /
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:/Documents\\Files/"_s, Format::Windows),
                     "C:/Documents\\Files"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("folder/subfolder\\//\\\\"_s, Format::Windows),
                     "folder/subfolder"_s);

            // Edge cases
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Windows), ""_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("filename"_s, Format::Windows), "filename"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:"_s, Format::Windows), "C:"_s);

            // Filenames with extensions
            CHECK_EQ(TrimDirectorySeparatorsEnd("C:\\file.txt\\"_s, Format::Windows), "C:\\file.txt"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("document.pdf\\\\\\\\"_s, Format::Windows), "document.pdf"_s);

            // Long UNC paths with multiple levels
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\fileserver\\department\\projects\\2024\\Q4\\"_s,
                                                Format::Windows),
                     "\\\\fileserver\\department\\projects\\2024\\Q4"_s);

            // Invalid/malformed paths that should still be handled gracefully
            CHECK_EQ(TrimDirectorySeparatorsEnd("\\\\\\server\\share\\"_s, Format::Windows),
                     "\\\\\\server\\share"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("C::\\"_s, Format::Windows), "C::"_s);
        }

        SUBCASE("posix") {
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo////"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo/"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/foo"_s, Format::Posix), "/foo"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("/"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd("////"_s, Format::Posix), "/"_s);
            CHECK_EQ(TrimDirectorySeparatorsEnd(""_s, Format::Posix), ""_s);
        }
    }

    SUBCASE("Join") {
        DynamicArrayBounded<char, 128> s;
        s = "foo"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = "foo/"_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "foo/bar"_s);

        s = ""_s;
        JoinAppend(s, "/bar"_s, Format::Posix);
        CHECK_EQ(s, "bar"_s);

        s = "foo"_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = "foo"_s;
        JoinAppend(s, "/"_s, Format::Posix);
        CHECK_EQ(s, "foo"_s);

        s = ""_s;
        JoinAppend(s, ""_s, Format::Posix);
        CHECK_EQ(s, ""_s);

        s = "C:/"_s;
        JoinAppend(s, "foo"_s, Format::Windows);
        CHECK_EQ(s, "C:/foo"_s);

        s = "/"_s;
        JoinAppend(s, "foo"_s, Format::Posix);
        CHECK_EQ(s, "/foo"_s);

        {
            auto result = Join(scratch_arena, Array {"foo"_s, "bar"_s, "baz"_s}, Format::Posix);
            CHECK_EQ(result, "foo/bar/baz"_s);
        }
    }

    SUBCASE("Utils") {
        CHECK_EQ(Filename("foo"), "foo"_s);
        CHECK_EQ(Extension("/file.txt"_s), ".txt"_s);
        CHECK(IsAbsolute("/file.txt"_s, Format::Posix));
        CHECK(IsAbsolute("C:/file.txt"_s, Format::Windows));
        CHECK(IsAbsolute("C:\\file.txt"_s, Format::Windows));
        CHECK(IsAbsolute("\\\\server\\share"_s, Format::Windows));
        CHECK(!IsAbsolute("C:"_s, Format::Windows));
        CHECK(!IsAbsolute(""_s, Format::Windows));
    }

    // This SUBCASE is based on Zig's code
    // https://github.com/ziglang/zig
    // Copyright (c) Zig contributors
    // SPDX-License-Identifier: MIT
    SUBCASE("Directory") {
        CHECK_EQ(Directory("/a/b/c", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a/b/c///", Format::Posix), "/a/b"_s);
        CHECK_EQ(Directory("/a", Format::Posix), "/"_s);
        CHECK(!Directory("/", Format::Posix).HasValue());
        CHECK(!Directory("//", Format::Posix).HasValue());
        CHECK(!Directory("///", Format::Posix).HasValue());
        CHECK(!Directory("////", Format::Posix).HasValue());
        CHECK(!Directory("", Format::Posix).HasValue());
        CHECK(!Directory("a", Format::Posix).HasValue());
        CHECK(!Directory("a/", Format::Posix).HasValue());
        CHECK(!Directory("a//", Format::Posix).HasValue());

        CHECK(!Directory("c:\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:\\foo", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\", Format::Windows), "c:\\"_s);
        CHECK_EQ(Directory("c:\\foo\\bar", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\", Format::Windows), "c:\\foo"_s);
        CHECK_EQ(Directory("c:\\foo\\bar\\baz", Format::Windows), "c:\\foo\\bar"_s);
        CHECK(!Directory("\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\foo", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\", Format::Windows), "\\"_s);
        CHECK_EQ(Directory("\\foo\\bar", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\", Format::Windows), "\\foo"_s);
        CHECK_EQ(Directory("\\foo\\bar\\baz", Format::Windows), "\\foo\\bar"_s);
        CHECK(!Directory("c:", Format::Windows).HasValue());
        CHECK(!Directory("c:foo", Format::Windows).HasValue());
        CHECK(!Directory("c:foo\\", Format::Windows).HasValue());
        CHECK_EQ(Directory("c:foo\\bar", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\", Format::Windows), "c:foo"_s);
        CHECK_EQ(Directory("c:foo\\bar\\baz", Format::Windows), "c:foo\\bar"_s);
        CHECK(!Directory("file:stream", Format::Windows).HasValue());
        CHECK_EQ(Directory("dir\\file:stream", Format::Windows), "dir"_s);
        CHECK(!Directory("\\\\unc\\share", Format::Windows).HasValue());
        CHECK_EQ(Directory("\\\\unc\\share\\foo", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\", Format::Windows), "\\\\unc\\share\\"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\", Format::Windows), "\\\\unc\\share\\foo"_s);
        CHECK_EQ(Directory("\\\\unc\\share\\foo\\bar\\baz", Format::Windows), "\\\\unc\\share\\foo\\bar"_s);
        CHECK_EQ(Directory("/a/b/", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a/b", Format::Windows), "/a"_s);
        CHECK_EQ(Directory("/a", Format::Windows), "/"_s);
        CHECK(!Directory("", Format::Windows).HasValue());
        CHECK(!Directory("/", Format::Windows).HasValue());
        CHECK(!Directory("////", Format::Windows).HasValue());
        CHECK(!Directory("foo", Format::Windows).HasValue());
    }

    SUBCASE("IsWithinDirectory") {
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo"));
        CHECK(IsWithinDirectory("/foo/bar/baz", "/foo/bar"));
        CHECK(IsWithinDirectory("foo/bar/baz", "foo"));
        CHECK(!IsWithinDirectory("/foo", "/foo"));
        CHECK(!IsWithinDirectory("/foo/bar/baz", "/bar"));
        CHECK(!IsWithinDirectory("/foobar/baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/foo"));
        CHECK(!IsWithinDirectory("baz", "/o"));
    }

    SUBCASE("Windows Parse") {
        {
            auto const p = ParseWindowsPath("C:/foo/bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "C:"_s);
        }
        {
            auto const p = ParseWindowsPath("//a/b");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "//a/b"_s);
        }
        {
            auto const p = ParseWindowsPath("c:../");
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, "c:"_s);
        }
        {
            auto const p = ParseWindowsPath({});
            CHECK(!p.is_abs);
            CHECK_EQ(p.drive, ""_s);
        }
        {
            auto const p = ParseWindowsPath("D:\\foo\\bar");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "D:"_s);
        }
        {
            auto const p = ParseWindowsPath("\\\\LOCALHOST\\c$\\temp\\test-file.txt");
            CHECK(p.is_abs);
            CHECK_EQ(p.drive, "\\\\LOCALHOST\\c$"_s);
        }
    }

    SUBCASE("MakeSafeForFilename") {
        CHECK_EQ(MakeSafeForFilename("foo", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo/bar", scratch_arena), "foo bar"_s);
        CHECK_EQ(MakeSafeForFilename("foo/bar/baz", scratch_arena), "foo bar baz"_s);
        CHECK_EQ(MakeSafeForFilename("", scratch_arena), ""_s);
        CHECK_EQ(MakeSafeForFilename("\"\"\"", scratch_arena), ""_s);
        CHECK_EQ(MakeSafeForFilename("foo  ", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo  \"", scratch_arena), "foo"_s);
        CHECK_EQ(MakeSafeForFilename("foo: <bar>|<baz>", scratch_arena), "foo bar baz"_s);
    }

    SUBCASE("CompactPath") {
        SUBCASE("compact only") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = false,
                .compact_middle_sections = true,
            };
            SUBCASE("Linux style") {
                CHECK_EQ(MakeDisplayPath("/a/b/c", options, scratch_arena, Format::Posix), "/a/b/c"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d", options, scratch_arena, Format::Posix), "/a/b/c/d"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                         "/a/b/…/d/e"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f", options, scratch_arena, Format::Posix),
                         "/a/b/…/e/f"_s);
                CHECK_EQ(MakeDisplayPath("/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "/home/user/…/src/main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f/g/h/i", options, scratch_arena, Format::Posix),
                         "/a/b/…/h/i"_s);
                CHECK_EQ(MakeDisplayPath("/Volumes/My Drive", options, scratch_arena, Format::Posix),
                         "/Volumes/My Drive"_s);
                CHECK_EQ(MakeDisplayPath("/Volumes/My Drive/Folder/Subfolder/Final",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "/Volumes/My Drive/…/Subfolder/Final"_s);
            }
            SUBCASE("Windows style") {
                CHECK_EQ(MakeDisplayPath("C:/a/b/c", options, scratch_arena, Format::Windows), "C:/a/b/c"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d", options, scratch_arena, Format::Windows),
                         "C:/a/b/c/d"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/d/e"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e/f", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/e/f"_s);
                CHECK_EQ(MakeDisplayPath("C:/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "C:/home/user/…/src/main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e/f/g/h/i", options, scratch_arena, Format::Windows),
                         "C:/a/b/…/h/i"_s);
                CHECK_EQ(MakeDisplayPath("D:\\My Documents\\Projects\\App\\src\\main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "D:\\My Documents\\Projects\\…\\src\\main.cpp"_s);
                CHECK_EQ(MakeDisplayPath("\\\\unc\\share\\foo\\bar\\baz\\blah\\foo",
                                         options,
                                         scratch_arena,
                                         Format::Windows),
                         "\\\\unc\\share\\foo\\bar\\…\\blah\\foo"_s);
            }
        }
        SUBCASE("compact and stylize") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = true,
                .compact_middle_sections = true,
            };
            CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                     "a › b › … › d › e"_s);
            CHECK_EQ(MakeDisplayPath("/a/b/c/d/e/f", options, scratch_arena, Format::Posix),
                     "a › b › … › e › f"_s);
            CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                     "C: › a › b › … › d › e"_s);
            CHECK_EQ(MakeDisplayPath("\\\\unc\\share\\foo\\bar\\baz\\blah\\foo",
                                     options,
                                     scratch_arena,
                                     Format::Windows),
                     "\\\\unc\\share › foo › bar › … › blah › foo"_s);
        }
        SUBCASE("stylize only") {
            auto const options = DisplayPathOptions {
                .stylize_dir_separators = true,
                .compact_middle_sections = false,
            };
            SUBCASE("Linux style") {
                CHECK_EQ(MakeDisplayPath("/a/b/c", options, scratch_arena, Format::Posix), "a › b › c"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d", options, scratch_arena, Format::Posix),
                         "a › b › c › d"_s);
                CHECK_EQ(MakeDisplayPath("/a/b/c/d/e", options, scratch_arena, Format::Posix),
                         "a › b › c › d › e"_s);
                CHECK_EQ(MakeDisplayPath("/home/user/docs/projects/app/src/main.cpp",
                                         options,
                                         scratch_arena,
                                         Format::Posix),
                         "home › user › docs › projects › app › src › main.cpp"_s);
            }
            SUBCASE("Windows style") {
                CHECK_EQ(MakeDisplayPath("C:/a/b/c", options, scratch_arena, Format::Windows),
                         "C: › a › b › c"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d", options, scratch_arena, Format::Windows),
                         "C: › a › b › c › d"_s);
                CHECK_EQ(MakeDisplayPath("C:/a/b/c/d/e", options, scratch_arena, Format::Windows),
                         "C: › a › b › c › d › e"_s);
            }
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterPathTests) { REGISTER_TEST(TestPath); }
