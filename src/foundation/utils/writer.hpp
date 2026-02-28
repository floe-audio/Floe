// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/array.hpp"
#include "foundation/container/span.hpp"
#include "foundation/error/error_code.hpp"
#include "foundation/universal_defs.hpp"
#include "foundation/utils/memory.hpp"

struct Writer {
    template <typename ObjectType>
    constexpr void Set(ObjectType& obj,
                       ErrorCodeOr<void> (*write_bytes)(ObjectType& obj, Span<u8 const> bytes)) {
        object = (void*)&obj;
        write_bytes_function_ptr = (void*)write_bytes;
        invoke_write_bytes = [](void* func_ptr, void* ob, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            return ((decltype(write_bytes))func_ptr)(*(ObjectType*)ob, bytes);
        };
    }

    template <typename UserData>
    constexpr void SetContained(UserData user_data,
                                ErrorCodeOr<void> (*write_bytes)(UserData user_data, Span<u8 const> bytes)) {
        object = (void*)(uintptr)user_data;
        write_bytes_function_ptr = (void*)write_bytes;
        invoke_write_bytes = [](void* func_ptr, void* ob, Span<u8 const> bytes) -> ErrorCodeOr<void> {
            return ((decltype(write_bytes))func_ptr)((UserData)(uintptr)ob, bytes);
        };
    }

    ErrorCodeOr<void> WriteByte(u8 byte) const { return WriteBytes({&byte, 1}); }
    ErrorCodeOr<void> WriteBytes(Span<u8 const> bytes) const {
        return invoke_write_bytes(write_bytes_function_ptr, object, bytes);
    }
    ErrorCodeOr<void> WriteChar(char c) const { return WriteByte((u8)c); }
    ErrorCodeOr<void> WriteChars(Span<char const> cs) const { return WriteBytes(cs.ToByteSpan()); }

    ErrorCodeOr<void> WriteCharRepeated(char c, usize count) const {
        Array<u8, 32> bytes = {};
        FillMemory(bytes, (u8)c);

        usize remaining = count;
        while (remaining > 0) {
            auto const to_write = Min(remaining, bytes.size);
            TRY(WriteBytes(Span<u8 const> {bytes}.SubSpan(0, to_write)));
            remaining -= to_write;
        }
        return k_success;
    }

    ErrorCodeOr<void> (*invoke_write_bytes)(void* func_ptr, void* object, Span<u8 const> bytes) = {};
    void* write_bytes_function_ptr = {};
    void* object = {};
    usize bytes_written = {};
};

// This is based on Zig's BufferedWriter
// https://github.com/ziglang/zig
// Copyright (c) Zig contributors
// SPDX-License-Identifier: MIT
template <usize k_size>
struct BufferedWriter {
    ~BufferedWriter() {
        // We don't Flush() here because we want to handle any flush errors outside this code.
        ASSERT(end == 0, "missing Flush()");
    }

    // If it fails, the end is not reset to 0.
    ErrorCodeOr<void> Flush() {
        if (end) {
            TRY(unbuffered_writer.WriteBytes({buf.data, end}));
            end = 0;
        }
        return k_success;
    }

    void Reset() { end = 0; }

    void FlushReset() {
        auto _ = Flush();
        Reset();
    }

    ::Writer Writer() {
        ::Writer result;
        result.Set<BufferedWriter>(*this,
                                   [](BufferedWriter& self, Span<u8 const> bytes) -> ErrorCodeOr<void> {
                                       return self.Write(bytes);
                                   });
        return result;
    }

    ErrorCodeOr<void> Write(Span<u8 const> bytes) {
        if (end + bytes.size > buf.size) {
            TRY(Flush());
            if (bytes.size > buf.size) return unbuffered_writer.WriteBytes(bytes);
        }

        ASSERT(end + bytes.size <= buf.size);
        __builtin_memcpy(buf.data + end, bytes.data, bytes.size);
        end += bytes.size;
        return k_success;
    }

    ::Writer unbuffered_writer;
    Array<u8, k_size> buf = {};
    usize end {};
};
