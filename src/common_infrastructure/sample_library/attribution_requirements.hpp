// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "sample_library.hpp"

// Floe provides an up-to-date list of all sounds that require crediting the authors. It synchronises this
// list with other instances of Floe using shared memory. This is necessary because sometimes DAWs will load
// plugins in separate processes. Providing the definitive list of attributions makes using CC-BY sounds very
// easy for example.

struct AttributionRequirementsState {
    u64 const instance_id = (u64)NanosecondsSinceEpoch();
    Optional<LockableSharedMemory>& shared_attributions_store;
    DynamicArray<char> formatted_text {Malloc::Instance()}; // empty if none needed
    Atomic<TimePoint> last_update_time {};
};

struct AttributionsStore {
    struct Item {
        u64 instance_id;
        u32 time_seconds_since_epoch;
        String title;
        Optional<String> license_name;
        Optional<String> license_url;
        String attributed_to;
        Optional<String> attribution_url;
    };

    enum class Mode { Read, Write };

    bool SerialiseNumber(Integral auto& value) {
        if (mode == Mode::Read) {
            if (pos + sizeof(value) > data.size) return false;
            __builtin_memcpy_inline(&value, data.data + pos, sizeof(value));
            pos += sizeof(value);
            return true;
        } else {
            if (pos + sizeof(value) > data.size) return false;
            __builtin_memcpy_inline(data.data + pos, &value, sizeof(value));
            pos += sizeof(value);
            return true;
        }
    }

    bool SerialiseString(String& str, ArenaAllocator& arena) {
        auto size = (u16)Min(str.size, LargestRepresentableValue<u16>() - 1uz);
        if (!SerialiseNumber(size)) return false;
        ASSERT(size < 200);
        if (mode == Mode::Read) {
            if (pos + size > data.size) return false;
            str = arena.Clone(Span {(char*)data.data + pos, size});
            pos += size;
            return true;
        } else {
            if (pos + size > data.size) return false;
            for (auto c : str)
                ASSERT(c >= 32 && c <= 126);
            CopyMemory(data.data + pos, str.data, size);
            pos += size;
            return true;
        }
    }

    bool SerialiseString(Optional<String>& str, ArenaAllocator& arena) {
        String s = str.ValueOr({});
        if (!SerialiseString(s, arena)) return false;
        if (s.size)
            str = s;
        else
            str = {};
        return true;
    }

    bool Serialise(DynamicArray<Item>& items, ArenaAllocator& arena) {
        // In the shared memory we store a block of data that we sequentially read or write.
        // Importantly, the shared memory is zero-initialised, so when it's first created we will read 0 for
        // the number of items.

        auto num_items = CheckedCast<u16>(items.size);
        if (!SerialiseNumber(num_items)) return false;
        if (mode == Mode::Read) dyn::Resize(items, num_items);

        for (auto& item : items) {
            if (!SerialiseNumber(item.instance_id)) return false;
            if (!SerialiseNumber(item.time_seconds_since_epoch)) return false;
            if (!SerialiseString(item.title, arena)) return false;
            if (!SerialiseString(item.license_name, arena)) return false;
            if (!SerialiseString(item.license_url, arena)) return false;
            if (!SerialiseString(item.attributed_to, arena)) return false;
            if (!SerialiseString(item.attribution_url, arena)) return false;
        }

        return true;
    }

    Mode mode;
    Span<u8> data;
    u32 pos;
};

static void AddAttributionItems(AttributionRequirementsState& reqs,
                                DynamicArray<AttributionsStore::Item>& items,
                                Span<sample_lib::Instrument const*> insts,
                                sample_lib::ImpulseResponse const* ir) {
    auto const timestamp = CheckedCast<u32>(NanosecondsSinceEpoch() / 1'000'000'000);

    auto add_if_not_already_there = [&](AttributionsStore::Item const& item) {
        for (auto const& i : items)
            if (i.title == item.title && i.attributed_to == item.attributed_to &&
                i.attribution_url == item.attribution_url && i.license_name == item.license_name &&
                i.license_url == item.license_url)
                return;
        dyn::Append(items, item);
    };

    auto attribution_for_path = [&](sample_lib::Library const& lib,
                                    String path) -> sample_lib::FileAttribution* {
        if (auto const attr = lib.files_requiring_attribution.Find({path})) return attr;

        // folders are also supported in files_requiring_attribution
        for (auto dir = path::Directory(path, path::Format::Posix); dir;
             dir = path::Directory(*dir, path::Format::Posix)) {
            if (auto const attr = lib.files_requiring_attribution.Find({*dir})) return attr;
        }

        return nullptr;
    };

    for (auto& i : insts) {
        auto const& lib = i->library;

        if (lib.files_requiring_attribution.size) {
            for (auto const& r : i->regions) {
                if (auto const attr = attribution_for_path(lib, r.path.str)) {
                    add_if_not_already_there({
                        .instance_id = reqs.instance_id,
                        .time_seconds_since_epoch = timestamp,
                        .title = attr->title,
                        .license_name = attr->license_name,
                        .license_url = attr->license_url,
                        .attributed_to = attr->attributed_to,
                        .attribution_url = attr->attribution_url,
                    });
                }
            }
        }
    }

    if (ir) {
        auto const& lib = ir->library;
        if (lib.files_requiring_attribution.size) {
            if (auto const attr = attribution_for_path(lib, ir->path.str)) {
                add_if_not_already_there({
                    .instance_id = reqs.instance_id,
                    .time_seconds_since_epoch = timestamp,
                    .title = attr->title,
                    .license_name = attr->license_name,
                    .license_url = attr->license_url,
                    .attributed_to = attr->attributed_to,
                    .attribution_url = attr->attribution_url,
                });
            }
        }
    }
}

static void SyncItemsWithSharedMemory(AttributionRequirementsState& reqs,
                                      DynamicArray<AttributionsStore::Item>& items,
                                      ArenaAllocator& scratch_arena) {
    if (!reqs.shared_attributions_store) {
        auto o = CreateLockableSharedMemory("floe_attribution", Kb(100));
        if (o.HasValue()) reqs.shared_attributions_store = o.Value();
    }
    if (!reqs.shared_attributions_store) return;

    LockSharedMemory(*reqs.shared_attributions_store);
    DEFER { UnlockSharedMemory(*reqs.shared_attributions_store); };

    AttributionsStore store {
        .mode = AttributionsStore::Mode::Read,
        .data = reqs.shared_attributions_store->data,
        .pos = 0,
    };

    DynamicArray<AttributionsStore::Item> existing_items {scratch_arena};

    // read
    store.Serialise(existing_items, scratch_arena);

    // write
    // add the existing items if they're not too old, and not from this instance
    constexpr u32 k_max_age_seconds = 60 * 60 * 12; // 12 hours
    auto const now = CheckedCast<u32>(NanosecondsSinceEpoch() / 1'000'000'000);
    for (auto const& i : existing_items)
        if (i.instance_id != reqs.instance_id && now - i.time_seconds_since_epoch <= k_max_age_seconds)
            dyn::Append(items, i);
    store.mode = AttributionsStore::Mode::Write;
    store.pos = 0;
    store.Serialise(items, scratch_arena);
}

PUBLIC void DeinitAttributionRequirements(AttributionRequirementsState& engine,
                                          ArenaAllocator& scratch_arena) {
    if (!engine.shared_attributions_store) return;

    LockSharedMemory(*engine.shared_attributions_store);
    DEFER { UnlockSharedMemory(*engine.shared_attributions_store); };

    AttributionsStore store {
        .mode = AttributionsStore::Mode::Read,
        .data = engine.shared_attributions_store->data,
        .pos = 0,
    };
    DynamicArray<AttributionsStore::Item> items {scratch_arena};
    store.Serialise(items, scratch_arena);

    dyn::RemoveValueIf(items,
                       [&](AttributionsStore::Item const& i) { return i.instance_id == engine.instance_id; });

    store.mode = AttributionsStore::Mode::Write;
    store.pos = 0;
    store.Serialise(items, scratch_arena);
}

PUBLIC void UpdateAttributionText(AttributionRequirementsState& reqs,
                                  ArenaAllocator& scratch_arena,
                                  Span<sample_lib::Instrument const*> insts,
                                  sample_lib::ImpulseResponse const* ir) {
    DynamicArray<AttributionsStore::Item> items {scratch_arena};
    AddAttributionItems(reqs, items, insts, ir);
    SyncItemsWithSharedMemory(reqs, items, scratch_arena);

    auto& out = reqs.formatted_text;

    dyn::Clear(out);

    auto used = scratch_arena.NewMultiple<bool>(items.size);

    for (auto const [i, a] : Enumerate(items)) {
        if (used[i]) continue;
        if (out.size)
            fmt::Append(out, "\n");
        else
            dyn::AppendSpan(out, "Source Material Credits:\n"_s);

        fmt::Append(out, "\"{}\"", a.title);
        used[i] = true;
        u32 num_other_titles = 0;
        auto const items_have_same_attribution = [](AttributionsStore::Item const& lhs,
                                                    AttributionsStore::Item const& rhs) {
            return lhs.license_name == rhs.license_name && lhs.license_url == rhs.license_url &&
                   lhs.attributed_to == rhs.attributed_to && lhs.attribution_url == rhs.attribution_url;
        };
        for (auto const [j, other_a] : Enumerate(items)) {
            if (used[j]) continue;
            if (!items_have_same_attribution(a, other_a)) continue;
            if (other_a.title == a.title) {
                used[j] = true; // skip if it's an exact duplicate
                continue;
            }
            num_other_titles++;
        }
        if (num_other_titles) {
            // write the other titles, considering correct grammar
            u32 num_other_titles_written = 0;
            for (auto const [j, other_a] : Enumerate(items)) {
                if (used[j]) continue;
                if (!items_have_same_attribution(a, other_a)) continue;
                if (num_other_titles_written == num_other_titles - 1)
                    fmt::Append(out, ", and \"{}\"", other_a.title);
                else
                    fmt::Append(out, ", \"{}\"", other_a.title);
                num_other_titles_written++;
                used[j] = true;
            }
        }

        fmt::Append(out, " by {}", a.attributed_to);
        if (a.attribution_url) fmt::Append(out, " ({})", a.attribution_url);
        if (a.license_name) fmt::Append(out, " | {}", a.license_name);
        if (a.license_url) fmt::Append(out, " ({})", a.license_url);
    }

    reqs.last_update_time.Store(TimePoint::Now(), StoreMemoryOrder::Relaxed);
}

PUBLIC bool AttributionTextNeedsUpdate(AttributionRequirementsState const& reqs) {
    constexpr f64 k_refresh_seconds = 3;
    return (TimePoint::Now() - reqs.last_update_time.Load(LoadMemoryOrder::Relaxed)) > k_refresh_seconds;
}

PUBLIC void MarkNeedsAttributionTextUpdate(AttributionRequirementsState& reqs) {
    reqs.last_update_time.Store({}, StoreMemoryOrder::Relaxed);
}
