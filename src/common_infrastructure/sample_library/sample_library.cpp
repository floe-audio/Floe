// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sample_library.hpp"

#include "tests/framework.hpp"

ErrorCodeOr<void>
CustomValueToString(Writer writer, sample_lib::LibraryIdRef id, fmt::FormatOptions options) {
    auto const sep = " - "_s;
    TRY(PadToRequiredWidthIfNeeded(writer, options, id.author.size + sep.size + id.name.size));
    TRY(writer.WriteChars(id.author));
    TRY(writer.WriteChars(sep));
    return writer.WriteChars(id.name);
}

namespace sample_lib {

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
    for (auto& item : items)
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

VoidOrError<String> PostReadBookkeeping(Library& lib, Allocator& arena, ArenaAllocator& scratch_arena) {
    // Sort items into their folders, and by name within each folder.
    auto const sort_function = [](auto* a, auto* b) {
        auto const& a_folder = a->folder;
        auto const& b_folder = b->folder;
        if (!a_folder && b_folder) return true; // no-folder items first
        if (a_folder && !b_folder) return false; // no-folder items first
        if (!a_folder && !b_folder) return a->name < b->name; // no-folder items by name
        // They both have folders
        if (a_folder != b_folder) return *a_folder < *b_folder;
        return a->name < b->name;
    };

    {
        lib.sorted_instruments = arena.AllocateExactSizeUninitialised<Instrument*>(lib.insts_by_name.size);
        usize index = 0;
        for (auto [key, value, _] : lib.insts_by_name) {
            auto& inst = *value;
            lib.sorted_instruments[index++] = &inst;
        }

        Sort(lib.sorted_instruments, sort_function);
    }

    {
        lib.sorted_irs = arena.AllocateExactSizeUninitialised<ImpulseResponse*>(lib.irs_by_name.size);
        usize index = 0;
        for (auto [key, value, _] : lib.irs_by_name) {
            auto& ir = *value;
            lib.sorted_irs[index++] = &ir;
        }

        Sort(lib.sorted_irs, sort_function);
    }

    for (auto [key, value, _] : lib.insts_by_name) {
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

    for (auto [key, inst_ptr, _] : lib.insts_by_name) {
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
                                               inst.name);
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

    for (auto [key, inst_ptr, _] : lib.insts_by_name) {
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
    for (auto [key, inst_ptr, _] : lib.insts_by_name) {
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

    if (lib.sorted_instruments.size)
        FinaliseFolderTree(&lib.root_folders[ToInt(ResourceType::Instrument)], lib.sorted_instruments);
    if (lib.sorted_irs.size) FinaliseFolderTree(&lib.root_folders[ToInt(ResourceType::Ir)], lib.sorted_irs);

    return k_success;
}

} // namespace detail

} // namespace sample_lib

TEST_REGISTRATION(RegisterLibraryTests) {}
