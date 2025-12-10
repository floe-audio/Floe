// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_library.hpp"

#include "tests/framework.hpp"

namespace sample_lib {

usize IdFromAuthorAndName(String author, String name, MutableString out) {
    ASSERT(author.size <= k_max_library_author_size);
    ASSERT(name.size <= k_max_library_name_size);
    ASSERT(out.size >= author.size + name.size + k_default_library_id_separator.size);
    usize pos = 0;
    WriteAndIncrement(pos, out, name);
    WriteAndIncrement(pos, out, " - "_s);
    WriteAndIncrement(pos, out, author);
    return pos;
}

LibraryId IdFromAuthorAndNameInline(String author, String name) {
    LibraryId id {};
    id.size = IdFromAuthorAndName(author, name, {id.data, id.Capacity()});
    return id;
}

LibraryIdRef IdFromAuthorAndNameAlloc(String author, String name, Allocator& allocator) {
    auto const result = allocator.AllocateExactSizeUninitialised<char>(author.size + name.size +
                                                                       k_default_library_id_separator.size);
    IdFromAuthorAndName(author, name, result);
    return result;
}

constexpr auto k_mirage_library_id_suffix = " (Mirage) - FrozenPlain"_s;

usize IdForMdataLibrary(String name, MutableString out) {
    ASSERT(name.size + k_mirage_library_id_suffix.size <= k_max_library_id_size);
    usize pos = 0;
    WriteAndIncrement(pos, out, name);
    WriteAndIncrement(pos, out, k_mirage_library_id_suffix);
    return pos;
}

LibraryId IdForMdataLibraryInline(String name) {
    LibraryId id {};
    id.size = IdForMdataLibrary(name, {id.data, id.Capacity()});
    return id;
}

LibraryIdRef IdForMdataLibraryAlloc(String name, Allocator& arena) {
    auto const result =
        arena.AllocateExactSizeUninitialised<char>(name.size + k_mirage_library_id_suffix.size);
    IdForMdataLibrary(name, result);
    return result;
}

ErrorCodeOr<u64> Hash(String path, Reader& reader, FileFormat format) {
    switch (format) {
        case FileFormat::Mdata: return MdataHash(path, reader);
        case FileFormat::Lua: return LuaHash(path, reader);
    }
    PanicIfReached();
    return {};
}

bool FilenameIsFloeLuaFile(String filename) {
    return IsEqualToCaseInsensitiveAscii(filename, "floe.lua") ||
           EndsWithCaseInsensitiveAscii(filename, ".floe.lua"_s);
}

bool FilenameIsMdataFile(String filename) { return EndsWithCaseInsensitiveAscii(filename, ".mdata"_s); }

Optional<FileFormat> DetermineFileFormat(String path) {
    auto const filename = path::Filename(path);
    if (FilenameIsFloeLuaFile(filename)) return FileFormat::Lua;
    if (FilenameIsMdataFile(filename)) return FileFormat::Mdata;
    return k_nullopt;
}

LibraryPtrOrError Read(Reader& reader,
                       FileFormat format,
                       String filepath,
                       ArenaAllocator& result_arena,
                       ArenaAllocator& scatch_arena,
                       Options options) {
    switch (format) {
        case FileFormat::Mdata: return ReadMdata(reader, filepath, result_arena, scatch_arena);
        case FileFormat::Lua: return ReadLua(reader, filepath, result_arena, scatch_arena, options);
    }
    PanicIfReached();
}

namespace detail {

void InitialiseRootFolders(Library& lib, Allocator& arena) {
    auto root_name = fmt::Format(arena, "{} - {}", lib.name, lib.author);
    for (auto& folder : lib.root_folders) {
        folder.name = root_name;
        folder.display_name = lib.name;
    }
}

static void FinaliseFolderTree(FolderNode* root, auto const& items) {
    for (auto const [_, item, _] : items)
        if (!item->folder)
            item->folder = root;
        else {
            auto top_folder = item->folder;
            while (top_folder->parent)
                top_folder = top_folder->parent;
            ASSERT(top_folder == root);
        }

    SortFolderTree(root);
}

static void AddItemFromFolder(FolderNode const* node, auto output_items, usize& index, auto hash_table) {
    auto const start_index = index;

    for (auto const& [key, item, _] : hash_table)
        if (item->folder == node) output_items[index++] = item;

    Sort(Span {output_items}.SubSpan(start_index, index - start_index),
         [](auto const& a, auto const& b) { return a->name < b->name; });

    for (auto child = node->first_child; child; child = child->next)
        AddItemFromFolder(child, output_items, index, hash_table);
}

static auto BuildSorted(Allocator& arena, auto hash_table, FolderNode const* root_folder) {
    using ValueType = typename decltype(hash_table)::ValueType;
    auto result = arena.AllocateExactSizeUninitialised<ValueType>(hash_table.size);
    usize index = 0;

    AddItemFromFolder(root_folder, result, index, hash_table);

    ASSERT(index == hash_table.size);

    return result;
}

VoidOrError<String> PostReadBookkeeping(Library& lib, Allocator& arena, ArenaAllocator& scratch_arena) {
    if (lib.insts_by_id.size)
        FinaliseFolderTree(&lib.root_folders[ToInt(ResourceType::Instrument)], lib.insts_by_id);
    if (lib.irs_by_name.size) FinaliseFolderTree(&lib.root_folders[ToInt(ResourceType::Ir)], lib.irs_by_name);

    if (!lib.id.size) lib.id = IdFromAuthorAndNameAlloc(lib.author, lib.name, arena);

    lib.sorted_instruments =
        BuildSorted(arena, lib.insts_by_id, &lib.root_folders[ToInt(ResourceType::Instrument)]);
    lib.sorted_irs = BuildSorted(arena, lib.irs_by_name, &lib.root_folders[ToInt(ResourceType::Ir)]);

    for (auto [key, value, _] : lib.insts_by_id) {
        auto& inst = *value;

        inst.loop_overview.all_regions_require_looping = true;

        for (auto const i : ::Range(ToInt(LoopMode::Count)))
            inst.loop_overview.all_loops_convertible_to_mode[i] = true;

        Array<usize, ToInt(LoopMode::Count)> num_loops_per_mode {};
        Array<usize, ToInt(LoopMode::Count)> num_loops_per_mode_with_locked_points {};

        bool all_regions_never_loop = true;

        for (auto& region : inst.regions) {
            if (auto const& l = region.loop.builtin_loop) {
                ++num_loops_per_mode[ToInt(l->mode)];

                if (l->lock_mode) {
                    // This loop mode is locked, therefore all other modes in the
                    // all_loops_convertible_to_mode array should be false.
                    for (auto const i : ::Range(ToInt(LoopMode::Count)))
                        if (i != ToInt(l->mode)) inst.loop_overview.all_loops_convertible_to_mode[i] = false;
                }

                if (l->lock_loop_points) ++num_loops_per_mode_with_locked_points[ToInt(l->mode)];
            }

            if (region.loop.loop_requirement != LoopRequirement::AlwaysLoop)
                inst.loop_overview.all_regions_require_looping = false;
            if (region.loop.loop_requirement != LoopRequirement::NeverLoop) all_regions_never_loop = false;

            if (region.timbre_layering.layer_range) inst.uses_timbre_layering = true;
        }

        auto const num_loops = Sum(num_loops_per_mode);

        if (num_loops) inst.loop_overview.has_loops = true;
        if (num_loops != inst.regions.size) inst.loop_overview.has_non_loops = true;

        inst.loop_overview.all_loops_mode = k_nullopt;
        for (auto const i : ::Range(ToInt(LoopMode::Count))) {
            if (num_loops_per_mode[i] == num_loops) {
                inst.loop_overview.all_loops_mode = LoopMode(i);
                break;
            }
        }

        {
            inst.loop_overview.user_defined_loops_allowed = true;

            // If all regions have loops, and they all have locked loop points, then user-defined loops are
            // not allowed.
            if (num_loops && Sum(num_loops_per_mode_with_locked_points) == num_loops)
                inst.loop_overview.user_defined_loops_allowed = false;

            // If all regions never loop, then user-defined loops are not allowed.
            if (all_regions_never_loop) inst.loop_overview.user_defined_loops_allowed = false;
        }
    }

    for (auto [key, inst_ptr, _] : lib.insts_by_id) {
        auto& inst = *inst_ptr;
        struct RoundRobinGroupInfo {
            u8 max_rr_pos;
            u8 sequencing_group;
        };
        Array<HashTable<String, RoundRobinGroupInfo>, ToInt(TriggerEvent::Count)> round_robin_group_infos {};

        Array<u8, ToInt(TriggerEvent::Count)> sequencing_group_counters {};

        for (auto& region : inst.regions) {
            if (!region.trigger.round_robin_index) continue;

            if (auto const e =
                    round_robin_group_infos[ToInt(region.trigger.trigger_event)].FindOrInsertGrowIfNeeded(
                        scratch_arena,
                        region.trigger.round_robin_sequencing_group_name,
                        {});
                !e.inserted) {
                // This group already exists, so we need to update the max_rr_pos.

                auto& existing = e.element.data;
                existing.max_rr_pos = Max(existing.max_rr_pos, *region.trigger.round_robin_index);

                region.trigger.round_robin_sequencing_group = existing.sequencing_group;
            } else {
                // We've inserted it, so we need to set the actual values.

                auto& counter = sequencing_group_counters[ToInt(region.trigger.trigger_event)];
                if (counter == k_max_round_robin_sequence_groups) {
                    return (String)fmt::Format(arena,
                                               "More than {} round robin groups in instrument {}",
                                               k_max_round_robin_sequence_groups,
                                               inst.id);
                }

                auto& new_group = e.element.data;
                new_group = {
                    .max_rr_pos = *region.trigger.round_robin_index,
                    .sequencing_group = counter++,
                };

                region.trigger.round_robin_sequencing_group = new_group.sequencing_group;
            }
        }

        for (auto const i : ::Range(ToInt(TriggerEvent::Count))) {
            inst.round_robin_sequence_groups[i] =
                arena.NewMultiple<RoundRobinGroup>(sequencing_group_counters[i]);
            for (auto const& [group_key, group_info, _] : round_robin_group_infos[i]) {
                auto& group = inst.round_robin_sequence_groups[i][group_info.sequencing_group];
                group = {
                    .max_rr_pos = group_info.max_rr_pos,
                };
            }
        }
    }

    for (auto [key, inst_ptr, _] : lib.insts_by_id) {
        auto const& inst = *inst_ptr;
        for (auto const& region : inst.regions) {
            if (!region.trigger.feather_overlapping_velocity_layers) continue;
            usize num_overlaps = 0;
            Region const* first_overlap {};
            Region const* second_overlap {};
            for (auto const& other_region : inst.regions) {
                if (&region == &other_region) continue;
                if (!other_region.trigger.feather_overlapping_velocity_layers) continue;
                if (region.trigger.trigger_event == other_region.trigger.trigger_event &&
                    region.trigger.round_robin_index == other_region.trigger.round_robin_index &&
                    region.trigger.round_robin_sequencing_group ==
                        other_region.trigger.round_robin_sequencing_group &&
                    region.trigger.key_range.Overlaps(other_region.trigger.key_range) &&
                    region.trigger.velocity_range.Overlaps(other_region.trigger.velocity_range)) {
                    if (num_overlaps == 0)
                        first_overlap = &other_region;
                    else if (num_overlaps == 1)
                        second_overlap = &other_region;
                    num_overlaps++;

                    // IMPROVE: we could possibly support more than 1 but we'd need to implement a
                    // different kind of feathering algorithm.
                    if (num_overlaps > 2) {
                        return (String)fmt::Format(
                            arena,
                            "Only 2 feathered velocity regions can be present on a given velocity value.\n"
                            "{} ({}, {}) overlaps:\n"
                            "{} ({}, {}) and:\n"
                            "{} ({}, {}) and:\n"
                            "{} ({}, {})",
                            path::Filename(region.path.str),
                            region.trigger.velocity_range.start,
                            region.trigger.velocity_range.end,
                            path::Filename(first_overlap->path.str),
                            first_overlap->trigger.velocity_range.start,
                            first_overlap->trigger.velocity_range.end,
                            path::Filename(second_overlap->path.str),
                            second_overlap->trigger.velocity_range.start,
                            second_overlap->trigger.velocity_range.end,
                            path::Filename(other_region.path.str),
                            other_region.trigger.velocity_range.start,
                            other_region.trigger.velocity_range.end);
                    }
                }
            }
        }
    }
    for (auto [key, inst_ptr, _] : lib.insts_by_id) {
        auto const& inst = *inst_ptr;
        for (auto const& region : inst.regions) {
            if (!region.timbre_layering.layer_range) continue;
            usize num_overlaps = 0;
            Region const* first_overlap {};
            Region const* second_overlap {};
            for (auto const& other_region : inst.regions) {
                if (&region == &other_region) continue;
                if (!other_region.timbre_layering.layer_range) continue;
                if (region.trigger.trigger_event == other_region.trigger.trigger_event &&
                    region.trigger.round_robin_index == other_region.trigger.round_robin_index &&
                    region.trigger.round_robin_sequencing_group ==
                        other_region.trigger.round_robin_sequencing_group &&
                    region.trigger.key_range.Overlaps(other_region.trigger.key_range) &&
                    region.trigger.velocity_range.Overlaps(other_region.trigger.velocity_range) &&
                    region.timbre_layering.layer_range->Overlaps(*other_region.timbre_layering.layer_range)) {
                    if (num_overlaps == 0)
                        first_overlap = &other_region;
                    else if (num_overlaps == 1)
                        second_overlap = &other_region;
                    num_overlaps++;

                    // IMPROVE: we could possibly support more than 1 but we'd need to implement a
                    // different kind of algorithm.
                    if (num_overlaps > 2) {
                        return (String)fmt::Format(
                            arena,
                            "Only 2 timbre layers can be present on a given timbre value.\n"
                            "{} ({}, {}) overlaps:\n"
                            "{} ({}, {}) and:\n"
                            "{} ({}, {}) and:\n"
                            "{} ({}, {})",
                            path::Filename(region.path.str),
                            region.timbre_layering.layer_range->start,
                            region.timbre_layering.layer_range->end,
                            path::Filename(first_overlap->path.str),
                            first_overlap->timbre_layering.layer_range->start,
                            first_overlap->timbre_layering.layer_range->end,
                            path::Filename(second_overlap->path.str),
                            second_overlap->timbre_layering.layer_range->start,
                            second_overlap->timbre_layering.layer_range->end,
                            path::Filename(other_region.path.str),
                            other_region.timbre_layering.layer_range->start,
                            other_region.timbre_layering.layer_range->end);
                    }
                }
            }
        }
    }

    return k_success;
}

TEST_CASE(TestConvertVelocityRange) {
    auto check = [&](s8 low_velo, s8 high_velo, Range<u16> expected_out) {
        auto const out = MapMidiVelocityRangeToNewRange<u16>(low_velo, high_velo);
        CHECK_EQ(out.start, expected_out.start);
        CHECK_EQ(out.end, expected_out.end);
    };

    check(1, 127, {0, 1000});
    check(64, 127, {500, 1000});
    check(1, 10, {0, 79});
    check(11, 20, {79, 159});
    check(21, 30, {159, 238});
    check(31, 40, {238, 317});
    check(41, 50, {317, 396});
    check(51, 60, {396, 476});
    check(61, 70, {476, 555});
    check(71, 80, {555, 634});
    check(81, 90, {634, 714});
    check(91, 100, {714, 793});
    check(101, 110, {793, 872});
    check(111, 120, {872, 951});
    check(121, 127, {951, 1000});

    return k_success;
}

} // namespace detail

} // namespace sample_lib

TEST_REGISTRATION(RegisterLibraryTests) { REGISTER_TEST(sample_lib::detail::TestConvertVelocityRange); }
