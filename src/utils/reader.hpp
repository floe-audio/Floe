// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

enum class PathOrMemoryType { File, Memory };
using PathOrMemory = TaggedUnion<PathOrMemoryType,
                                 TypeAndTag<String, PathOrMemoryType::File>,
                                 TypeAndTag<Span<u8 const>, PathOrMemoryType::Memory>>;

struct Reader {
    static ErrorCodeOr<Reader> FromFile(String path) {
        auto f = TRY(OpenFile(path, FileMode::Read()));
        auto const size = TRY(f.FileSize());
        return Reader {
            .size = size,
            .pos = 0,
            .file_base_pos = 0,
            .file = Move(f),
        };
    }

    static ErrorCodeOr<Reader> FromFileSection(String path, usize start_offset, usize size) {
        auto f = TRY(OpenFile(path, FileMode::Read()));
        return Reader {
            .size = size,
            .pos = 0,
            .file_base_pos = start_offset,
            .file = Move(f),
        };
    }

    static Reader FromMemory(Span<u8 const> mem) {
        return Reader {
            .size = mem.size,
            .pos = 0,
            .memory = mem.data,
            .file_base_pos = 0,
        };
    }
    static Reader FromMemory(Span<char const> mem) { return FromMemory(mem.ToByteSpan()); }

    static ErrorCodeOr<Reader> FromPathOrMemory(PathOrMemory p) {
        switch (p.tag) {
            case PathOrMemoryType::File: return FromFile(p.Get<String>());
            case PathOrMemoryType::Memory: return FromMemory(p.Get<Span<u8 const>>());
        }
        PanicIfReached();
        return {};
    }

    // returns the number read, when the return value is less than the requested its the end
    ErrorCodeOr<usize> Read(Span<u8> bytes_out) {
        ASSERT(size >= pos);
        auto bytes = Min(bytes_out.size, size - pos);
        if (!bytes) return bytes;

        if (memory) {
            CopyMemory(bytes_out.data, memory + pos, bytes);
        } else {
            TRY(file->Seek((s64)(file_base_pos + pos), File::SeekOrigin::Start));
            bytes = TRY(file->Read(bytes_out.data, bytes));
        }
        pos += bytes;

        return bytes;
    }
    ErrorCodeOr<usize> Read(void* out, usize out_size) { return Read(Span {(u8*)out, out_size}); }

    // if it's in-memory the arena isn't used
    ErrorCodeOr<Span<u8 const>> ReadOrFetchAll(ArenaAllocator& arena) {
        pos = 0;
        if (memory) {
            return Span<u8 const> {memory, size};
        } else {
            auto result = arena.AllocateExactSizeUninitialised<u8>(size);
            TRY(Read(result));
            return result;
        }
    }

    usize size {};
    usize pos {};
    u8 const* memory {}; // valid if in-memory
    usize file_base_pos {};
    Optional<File> file {}; // valid if its a file
};
