// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "sample_library.hpp"

// Structured dump of a sample_lib::Library — emits one of two formats:
// - JSON (default): standard JSON document.
// - Lua: a single value-expression suitable for splicing into a `return { ... }` chunk, with table keys as
//   unquoted identifiers.
//
// The module exposes two layers:
// - A small structured writer (Context + Write* primitives) that the caller drives directly. Callers that
//   want to interleave their own keys (e.g. inspector tools adding parse/orphan metadata) use these.
// - A `Dump` entry point that writes the library payload — `library`, `instruments`, `irs`,
//   `files_requiring_attribution` — as four keys into a Context that is already inside an open object.

namespace library_dump {

enum class Format : u8 { Json, Lua };

struct Context {
    Writer out;
    Format format;
    enum class Last : u8 { None, Open, Close, Value, Key } last = Last::None;
    int indent = 0;
};

ErrorCodeOr<void> WriteObjectBegin(Context& c);
ErrorCodeOr<void> WriteObjectEnd(Context& c);
ErrorCodeOr<void> WriteArrayBegin(Context& c);
ErrorCodeOr<void> WriteArrayEnd(Context& c);
ErrorCodeOr<void> WriteKey(Context& c, String key);
ErrorCodeOr<void> WriteKeyObjectBegin(Context& c, String key);
ErrorCodeOr<void> WriteKeyArrayBegin(Context& c, String key);
ErrorCodeOr<void> WriteKeyNull(Context& c, String key);
ErrorCodeOr<void> WriteNull(Context& c);
ErrorCodeOr<void> WriteValue(Context& c, String v);
ErrorCodeOr<void> WriteValue(Context& c, bool v);
ErrorCodeOr<void> WriteValueInteger(Context& c, s64 v);
ErrorCodeOr<void> WriteValueFloat(Context& c, f64 v, bool single_precision);

template <Integral T>
inline ErrorCodeOr<void> WriteValue(Context& c, T v) {
    return WriteValueInteger(c, (s64)v);
}

template <FloatingPoint T>
inline ErrorCodeOr<void> WriteValue(Context& c, T v) {
    return WriteValueFloat(c, (f64)v, Same<T, f32>);
}

template <typename V>
inline ErrorCodeOr<void> WriteKeyValue(Context& c, String key, V const& value) {
    TRY(WriteKey(c, key));
    return WriteValue(c, value);
}

// Writes the four library-derivable top-level keys (library, instruments, irs, files_requiring_attribution)
// into the currently-open object on `ctx`. The caller is responsible for opening the surrounding container.
ErrorCodeOr<void> Dump(Context& ctx, sample_lib::Library const& lib, ArenaAllocator& arena);

} // namespace library_dump
