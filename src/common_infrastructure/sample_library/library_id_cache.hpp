// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "sample_library.hpp"

// Persists a map of LibraryId hash -> library id_string to a file in the user data directory, so tools that
// only see the hash (preset_tool, etc.) can recover the human-readable name without re-scanning every
// library on disk.
//
// The plugin's library scanner writes this file after each scan completes. Tools read it on startup and
// replay each entry through HashLibraryIdString to populate the in-process string registry. Concurrent
// readers are safe (shared file lock); the plugin is the only writer.

struct LibraryIdCacheEntry {
    sample_lib::LibraryId id;
    String id_string;
};

// Absolute path to the cache file. Stable across processes — both writer and reader resolve to the same
// location. Pass create=true if you intend to write.
String LibraryIdCachePath(ArenaAllocator& arena, bool create_parent_dir);

// Writes the cache atomically under an exclusive file lock. Logs errors via the standard error reporter but
// otherwise swallows them — failure to refresh the cache is non-fatal.
void WriteLibraryIdCache(Span<LibraryIdCacheEntry const> entries);

// Reads the cache file and registers each entry. Silently returns if the file is missing or malformed.
void LoadLibraryIdCache(ArenaAllocator& arena);
