// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/dynamic_array.hpp"
#include "foundation/container/span.hpp"
#include "foundation/utils/string.hpp"

namespace path {

#if IS_WINDOWS
#define FAKE_ABSOLUTE_PATH_PREFIX "C:\\"
#else
#define FAKE_ABSOLUTE_PATH_PREFIX "/"
#endif

constexpr usize k_max = IS_WINDOWS ? ((32767 * 3) + 1) : 4096;

enum class Format {
    Windows,
    Posix,
    Native = IS_WINDOWS ? Windows : Posix,
};

constexpr char k_dir_separator = IS_WINDOWS ? '\\' : '/';
constexpr auto k_dir_separator_str = IS_WINDOWS ? "\\"_ca : "/"_ca;

PUBLIC constexpr bool IsDirectorySeparator(char c, Format format = Format::Native) {
    return (format == Format::Windows) ? (c == '\\' || c == '/') : (c == '/');
}

PUBLIC constexpr bool StartsWithDirectorySeparator(String path, Format format = Format::Native) {
    return path.size && IsDirectorySeparator(path[0], format);
}

PUBLIC constexpr bool EndsWithDirectorySeparator(String path, Format format = Format::Native) {
    return path.size && IsDirectorySeparator(path[path.size - 1], format);
}

PUBLIC constexpr Optional<usize> FindLastDirectorySeparator(String path, Format format = Format::Native) {
    return FindLastIf(path, [format](char c) { return IsDirectorySeparator(c, format); });
}

struct WindowsPathInfo {
    enum class Type { Drive, NetworkShare, Relative };
    Type type;
    bool is_abs;
    String drive;
};

// This function is based on Zig's windowsParsePath
// https://github.com/ziglang/zig
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT
PUBLIC constexpr WindowsPathInfo ParseWindowsPath(String path) {
    auto const relative =
        WindowsPathInfo {.type = WindowsPathInfo::Type::Relative, .is_abs = false, .drive = {}};

    if (path.size < 2) return relative;
    if (path[1] == ':') {
        return {
            .type = WindowsPathInfo::Type::Drive,
            .is_abs = path.size > 2 && IsDirectorySeparator(path[2], Format::Windows),
            .drive = path.SubSpan(0, 2),
        };
    }

    if (path.size < "//a/b"_s.size) return relative;

    for (auto const sep : "\\/"_s) {
        if (path[0] == sep && path[1] == sep) {
            if (path[2] == sep) return relative;

            usize slash_between_server_and_share = 0;
            for (usize i = 2; i < path.size; ++i) {
                if (path[i] == sep) {
                    slash_between_server_and_share = i;
                    break;
                }
            }
            if (slash_between_server_and_share == 0) return relative;
            if (slash_between_server_and_share == path.size - 1) return relative;

            usize end_of_share = 0;
            for (usize i = slash_between_server_and_share + 1; i < path.size; ++i) {
                if (path[i] == sep) {
                    end_of_share = i;
                    break;
                }
            }
            if (end_of_share == 0) end_of_share = path.size;

            return {
                .type = WindowsPathInfo::Type::NetworkShare,
                .is_abs = true,
                .drive = path.SubSpan(0, end_of_share),
            };
        }
    }
    return relative;
}

PUBLIC constexpr bool IsAbsolute(String path, Format format = Format::Native) {
    if (path.size > k_max) return false;

    if (format == Format::Windows) {
        if (path.size < 2) return false;
        if (path[1] == ':') return path.size > 2 && IsDirectorySeparator(path[2], Format::Windows);

        if (path.size < "//a/b"_s.size) return false;

        for (auto const sep : "\\/"_s) {
            if (path[0] == sep && path[1] == sep) {
                if (path[2] == sep) return false;

                usize slash_between_server_and_share = 0;
                for (usize i = 2; i < path.size; ++i) {
                    if (path[i] == sep) {
                        slash_between_server_and_share = i;
                        break;
                    }
                }
                if (slash_between_server_and_share == 0) return false;
                if (slash_between_server_and_share == path.size - 1) return false;

                return true;
            }
        }
        return false;
    } else {
        return path.size && IsDirectorySeparator(path[0], Format::Posix);
    }
}

PUBLIC constexpr usize Depth(String subpath, Format format = Format::Native) {
    ASSERT(!IsAbsolute(subpath, format));
    usize depth = 0;
    for (auto c : subpath)
        if (IsDirectorySeparator(c, format)) ++depth;
    return depth;
}

[[nodiscard]] PUBLIC constexpr String TrimDirectorySeparatorsEnd(String path,
                                                                 Format format = Format::Native) {
    switch (format) {
        case Format::Windows: {
            if (!path.size) return path;

            auto const path_info = ParseWindowsPath(path);

            auto path_section = path.SubSpan(path_info.drive.size);

            if (!path_section.size) return path;

            while (EndsWithDirectorySeparator(path_section, format)) {
                if (path_section.size == 1) break;
                path_section.RemoveSuffix(1);
            }

            return {path.data, (usize)(End(path_section) - path.data)};
        }
        case Format::Posix: {
            if (path.size == 0) return path;

            usize end = path.size;
            while (end > 0 && path.data[end - 1] == '/')
                --end;

            // Special case: if we would trim everything (path was all slashes),
            // preserve a single slash to represent the root directory
            if (end == 0) end = 1;

            return path.SubSpan(0, end);
        }
    }
}

[[nodiscard]] PUBLIC constexpr String TrimDirectorySeparatorsStart(String path,
                                                                   Format format = Format::Native) {
    auto result = path;
    while (StartsWithDirectorySeparator(result, format))
        result.RemovePrefix(1);
    return result;
}

[[nodiscard]] PUBLIC constexpr String TrimDirectorySeparators(String path, Format format) {
    return TrimDirectorySeparatorsEnd(TrimDirectorySeparatorsStart(path, format), format);
}

PUBLIC bool Equal(String a, String b, Format format = Format::Native) {
    a = TrimDirectorySeparatorsEnd(a);
    b = TrimDirectorySeparatorsEnd(b);
    if (format == Format::Windows) {
        if (a.size != b.size) return false;
        for (auto const i : Range(a.size)) {
            auto c1 = ToLowercaseAscii(a[i]);
            auto c2 = ToLowercaseAscii(b[i]);
            if (c1 == '\\') c1 = '/';
            if (c2 == '\\') c2 = '/';
            if (c1 != c2) return false;
        }
        return true;
    } else
        return a == b;
}

PUBLIC constexpr String Filename(String path, Format format = Format::Native) {
    bool found = false;
    usize last_slash = 0;
    for (usize i = path.size - 1; i != usize(-1); --i)
        if (IsDirectorySeparator(path[i], format)) {
            found = true;
            last_slash = i;
            break;
        }
    usize offset = 0;
    if (found) offset = last_slash + 1; // remove the slash
    return path.SubSpan(offset);
}

// This function is based on Zig's windowsParsePath
// https://github.com/ziglang/zig
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT
PUBLIC constexpr Optional<String> Directory(String path, Format format = Format::Native) {
    if (!path.size) return k_nullopt;
    if (format == Format::Windows) {
        auto const root_slice = ParseWindowsPath(path).drive;
        if (path.size == root_slice.size) return k_nullopt;

        auto const have_root_slash =
            path.size > root_slice.size and (path[root_slice.size] == '/' or path[root_slice.size] == '\\');

        auto end_index = path.size - 1;

        while (path[end_index] == '/' or path[end_index] == '\\') {
            if (end_index == 0) return k_nullopt;
            end_index -= 1;
        }

        while (path[end_index] != '/' and path[end_index] != '\\') {
            if (end_index == 0) return k_nullopt;
            end_index -= 1;
        }

        if (have_root_slash and end_index == root_slice.size) end_index += 1;
        if (end_index == 0) return k_nullopt;

        return path.SubSpan(0, end_index);
    } else {
        auto end_index = path.size - 1;
        while (path[end_index] == '/') {
            if (end_index == 0) return k_nullopt;
            end_index -= 1;
        }

        while (path[end_index] != '/') {
            if (end_index == 0) return k_nullopt;
            end_index -= 1;
        }

        if (end_index == 0 and path[0] == '/') return path.SubSpan(0, 1);
        if (end_index == 0) return k_nullopt;

        return path.SubSpan(0, end_index);
    }
    return {};
}

// Be careful with this, paths need to be the same level of 'Canonicalised'
PUBLIC constexpr bool IsWithinDirectory(String path, String directory, Format format = Format::Native) {
    if (directory.size >= path.size) return false;
    if (!IsDirectorySeparator(path[directory.size])) return false;
    return Equal(path.SubSpan(0, directory.size), directory, format);
}

PUBLIC constexpr String Extension(String path) {
    auto filename = Filename(path);
    auto dot_pos = Find(filename, '.');
    if (!dot_pos) return {};
    return filename.SubSpan(*dot_pos);
}

PUBLIC constexpr String FilenameWithoutExtension(String path) {
    auto filename = Filename(path);
    filename.RemoveSuffix(Extension(path).size);
    return filename;
}

PUBLIC constexpr void
JoinAppend(dyn::DynArray auto& output, String item_to_append, Format format = Format::Native) {
    auto trimmed = TrimDirectorySeparatorsEnd(output, format);
    dyn::Resize(output, trimmed.size);

    auto item_to_append_span = TrimDirectorySeparatorsStart(item_to_append, format);
    if (output.size && item_to_append_span.size) {
        if (!IsDirectorySeparator(Last(output), format))
            dyn::Append(output, (format == Format::Windows) ? '\\' : '/');
    }
    dyn::AppendSpan(output, item_to_append_span);
}

PUBLIC constexpr void
JoinAppend(dyn::DynArray auto& output, Span<String const> parts, Format format = Format::Native) {
    for (auto p : parts)
        JoinAppend(output, p, format);
}

[[nodiscard]] PUBLIC constexpr MutableString JoinAppendResizeAllocation(Allocator& a,
                                                                        MutableString allocated_path,
                                                                        Span<String const> parts,
                                                                        Format format = Format::Native) {
    if (!parts.size) return allocated_path;
    auto buffer = a.ResizeType(allocated_path,
                               allocated_path.size,
                               allocated_path.size + TotalSize(parts) + parts.size);
    usize pos = allocated_path.size;
    for (auto const part : parts) {
        if (!part.size) continue;
        if (!IsDirectorySeparator(buffer[pos - 1], format))
            WriteAndIncrement(pos, buffer, format == Format::Windows ? '\\' : '/');
        WriteAndIncrement(pos, buffer, part);
    }
    return a.ResizeType(buffer, pos, pos);
}

PUBLIC constexpr MutableString Join(Allocator& a, Span<String const> parts, Format format = Format::Native) {
    if (!parts.size) return {};
    auto buffer = a.AllocateExactSizeUninitialised<char>(TotalSize(parts) + parts.size - 1);
    usize pos = 0;
    for (auto const part : parts) {
        if (!part.size) continue;
        if (pos && !IsDirectorySeparator(buffer[pos - 1], format))
            WriteAndIncrement(pos, buffer, format == Format::Windows ? '\\' : '/');
        WriteAndIncrement(pos, buffer, part);
    }
    return a.ResizeType(buffer, pos, pos);
}

template <usize k_size>
PUBLIC_INLINE DynamicArrayBounded<char, k_size> JoinInline(Span<String const> parts,
                                                           Format format = Format::Native) {
    if (!parts.size) return {};
    DynamicArrayBounded<char, k_size> buffer;
    buffer.size = TotalSize(parts) + parts.size - 1;
    usize pos = 0;
    for (auto const part : parts) {
        if (!part.size) continue;
        if (pos && !IsDirectorySeparator(buffer[pos - 1], format))
            WriteAndIncrement(pos, MutableString {buffer}, format == Format::Windows ? '\\' : '/');
        WriteAndIncrement(pos, MutableString {buffer}, part);
    }
    buffer.size = pos;
    return buffer;
}

PUBLIC bool Contains(Span<String const> paths, String path, Format format = Format::Native) {
    for (auto p : paths)
        if (Equal(p, path, format)) return true;
    return false;
}

constexpr WString k_win32_long_path_prefix {L"\\\\?\\"};

PUBLIC constexpr bool IsNetworkShare(WString path) {
    if (path.size < L"//a/b"_s.size) return false;

    for (auto const sep : L"\\/"_s) {
        if (path[0] == sep && path[1] == sep) {
            if (path[2] == sep) return false;

            usize slash_between_server_and_share = 0;
            for (usize i = 2; i < path.size; ++i) {
                if (path[i] == sep) {
                    slash_between_server_and_share = i;
                    break;
                }
            }
            if (slash_between_server_and_share == 0) return false;
            if (slash_between_server_and_share == path.size - 1) return false;
            return true;
        }
    }
    return false;
}

struct Win32Path {
    // Null-terminated (but not included in the size), uses only forward slashes. Don't free the result.
    MutableWString path;
    usize prefix_size; // the number of characters for the long-path prefix
};

PUBLIC Win32Path MakePathForWin32(Span<WString> parts, ArenaAllocator& arena, bool long_path_prefix) {
    if (parts.size == 0) return Win32Path {.path = {arena.New<wchar_t>(L'\0'), 0}, .prefix_size = 0};

    DynamicArray<wchar_t> result(arena);
    auto const root_path = parts[0];
    usize prefix_size = 0;
    if (long_path_prefix && IsNetworkShare(root_path)) {
        constexpr WString k_network_long_path_prefix = L"\\\\?\\UNC\\";
        dyn::Assign(result, k_network_long_path_prefix);
        dyn::AppendSpan(result, root_path.SubSpan(2));
        prefix_size = k_network_long_path_prefix.size;
    } else {
        if (long_path_prefix) {
            dyn::Assign(result, path::k_win32_long_path_prefix);
            prefix_size = path::k_win32_long_path_prefix.size;
        }
        dyn::AppendSpan(result, root_path);
    }
    for (auto& c : result)
        if (c == L'/') c = L'\\';

    for (usize i = 1; i < parts.size; ++i) {
        auto p = parts[i];
        while (p.size && (p[p.size - 1] == L'\\' || p[p.size - 1] == L'/'))
            p.RemoveSuffix(1);
        while (p.size && (p[0] == L'\\' || p[0] == L'/'))
            p.RemovePrefix(1);
        if (p.size) {
            if (i != 0) dyn::Append(result, L'\\');
            for (auto c : p) {
                if (c == '/') c = '\\';
                dyn::Append(result, c);
            }
        }
    }

    dyn::Append(result, L'\0');
    auto const size = result.size - 1;
    return {.path = {result.ToOwnedSpan().data, size}, .prefix_size = prefix_size};
}

PUBLIC ErrorCodeOr<Win32Path> MakePathForWin32(String path, ArenaAllocator& arena, bool long_path_prefix) {
    return path::MakePathForWin32(ArrayT<WString>({Widen(arena, path).Value()}), arena, long_path_prefix);
}

PUBLIC String MakeSafeForFilename(String name, Allocator& allocator) {
    if (name.size == 0) return {};

    constexpr auto k_remove_chars = ":*?\"<>"_s;
    constexpr auto k_replace_chars = "/\\|"_s;
    auto new_name = allocator.Clone(name);
    usize pos = 0;
    for (auto& c : new_name) {
        if (Contains(k_remove_chars, c)) continue;
        if (Contains(k_replace_chars, c)) {
            new_name[pos++] = ' ';
            continue;
        }

        new_name[pos++] = c;
    }

    // Trim trailing spaces
    while (pos && new_name[pos - 1] == ' ')
        --pos;

    if (pos == 0) {
        allocator.Free(new_name.ToByteSpan());
        return {};
    }

    return allocator.ResizeType(new_name, pos, pos);
}

struct DisplayPathOptions {
    bool stylize_dir_separators {};
    bool compact_middle_sections {};
};

PUBLIC String MakeDisplayPath(String path,
                              DisplayPathOptions options,
                              ArenaAllocator& arena,
                              Format format = Format::Native) {
    constexpr String k_stylized_dir_separator = " › "_s;
    constexpr String k_compact_symbol = "…"_s;
    constexpr s32 k_compact_slash_threshold = 5;
    constexpr s32 k_compact_num_start_sections = 2;
    constexpr s32 k_compact_num_end_sections = 2;

    ASSERT(IsAbsolute(path, format));
    auto const original_path = path;

    if (!options.stylize_dir_separators && !options.compact_middle_sections) return path;

    String drive {};
    if (format == Format::Windows) {
        drive = ParseWindowsPath(path).drive;
        path = path.SubSpan(drive.size);
    }

    s32 slashes = 0;
    for (auto const c : path)
        if (IsDirectorySeparator(c, format)) ++slashes;

    if ((options.compact_middle_sections && slashes >= k_compact_slash_threshold) ||
        options.stylize_dir_separators) {
        DynamicArray<char> result {arena};
        result.Reserve(path.size);

        dyn::AppendSpan(result, drive);
        if (options.stylize_dir_separators && drive.size) dyn::AppendSpan(result, k_stylized_dir_separator);

        s32 slashes_seen = 0;
        for (auto const [char_index, c] : Enumerate(path)) {
            auto const is_dir_sep = IsDirectorySeparator(c, format);
            if (is_dir_sep) {
                ++slashes_seen;
                if (options.compact_middle_sections && slashes >= k_compact_slash_threshold) {
                    if (slashes_seen == (k_compact_num_start_sections + 1)) {
                        if (options.stylize_dir_separators)
                            dyn::AppendSpan(result, k_stylized_dir_separator);
                        else
                            dyn::Append(result, c);
                        dyn::AppendSpan(result, k_compact_symbol);
                        if (options.stylize_dir_separators)
                            dyn::AppendSpan(result, k_stylized_dir_separator);
                        else
                            dyn::Append(result, c);
                        continue;
                    } else if (slashes_seen == slashes - (k_compact_num_end_sections - 1))
                        continue;
                }
            }
            if (slashes_seen < (k_compact_num_start_sections + 1) ||
                slashes_seen >= slashes - (k_compact_num_end_sections - 1) ||
                !options.compact_middle_sections) {
                if (options.stylize_dir_separators) {
                    if (is_dir_sep) {
                        if (char_index != 0) dyn::AppendSpan(result, k_stylized_dir_separator);
                    } else {
                        dyn::Append(result, c);
                    }
                } else {
                    dyn::Append(result, c);
                }
            }
        }

        return result.ToOwnedSpan();
    }

    return original_path;
}

} // namespace path
