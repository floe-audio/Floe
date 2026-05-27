// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

namespace mdata {

/*

MDATA file format. Binary, chunk-based, similar to RIFF. This exists purely for backwards-compatibility
reasons: it's not very well designed.

Don't change the size or layout of these structs; they are used directly when deserialising. It assumes
little-endian everywhere.

1. First thing in the file is the MasterHeader
2. After that is the HeaderIDInfoJson which is a JSON string containing various info about the
   library
3. Next is the HeaderID_StringPool chunk which can be used by any subsequent chunks
4. Any other chunks can be in any order, or not present at all.

*/

constexpr u32 MakeHeaderId(char const (&data)[5]) { return U32FromChars(data); }

using Index = s32;
constexpr Index k_invalid_md_index = -1;

static_assert(sizeof(OptionalIndex<Index>) == sizeof(Index));
static_assert(alignof(OptionalIndex<Index>) == alignof(Index));

constexpr s32 k_max_groups_in_inst = 16;
constexpr s32 k_no_round_robin_or_xfade = -1;
constexpr usize k_max_library_name_size = 64;
constexpr u32 k_invalid_library_name_hash = 0;

enum HeaderID : u32 {
    HeaderIdMasterMagic = MakeHeaderId("MDTA"),
    HeaderIdInfoJson = MakeHeaderId("INFO"),
    HeaderIdStringPool = MakeHeaderId("STRG"),

    HeaderIdFileDataPool = MakeHeaderId("FILE"),
    HeaderIdInstrumentInfoArray = MakeHeaderId("INST"),
    HeaderIdExtendedInstrumentInfoArray = MakeHeaderId("INSX"),
    HeaderIdSamplerRegionInfoArray = MakeHeaderId("SMPL"),
    HeaderIdDirectoryEntryArray = MakeHeaderId("DIRL"),
    HeaderIdDirectoryEntryTreeRoots = MakeHeaderId("ROOT"),
    HeaderIdFileInfoArray = MakeHeaderId("ASST"),
};

struct StringInPool {
    bool operator==(StringInPool const& other) const {
        return size != 0 && offset == other.offset && size == other.size;
    }

    u32 offset = 0;
    u32 size = 0;
};

struct MasterHeader {
    String Name() const {
        static_assert(sizeof(char[k_max_library_name_size]) == sizeof(Array<char, k_max_library_name_size>));
        static_assert(alignof(char[k_max_library_name_size]) ==
                      alignof(Array<char, k_max_library_name_size>));
        return FromNullTerminated(name.data);
    }

    u32 id_magic = HeaderIdMasterMagic;
    Array<char, k_max_library_name_size> name {};
    u32 version;

    // right after the master header is the HeaderID_InfoChunk which is a json string
};

struct ChunkHeader {
    HeaderID id;
    s32 size_bytes_of_following_data;
};

enum FolderType : u8 {
    FolderTypeSampler,
    FolderTypeFiles,
    FolderTypeIRs,
    FolderTypeSpecials,
    FolderTypeCount,
};

constexpr String k_md_folder_type_names[] = {
    "sampler",
    "files",
    "irs",
    "Specials",
};

static_assert(ArraySize(k_md_folder_type_names) == FolderTypeCount, "");

struct DirectoryEntry {
    bool HasChildren() const { return first_child != k_invalid_md_index; }

    Index file_info_index = k_invalid_md_index; // index of the file that this item represents, if it has one
    Index inst_info_index = k_invalid_md_index; // index of the inst that this folder contains, if it has one
    StringInPool name = {}; // just the name of the file
    StringInPool virtual_filepath = {}; // full virtual path
    Index parent = k_invalid_md_index; // index within dir_entries
    Index first_child = k_invalid_md_index; // index within dir_entries
    Index prev = k_invalid_md_index; // index within dir_entries
    Index next = k_invalid_md_index; // index within dir_entries
    u8 is_folder = false;
};

struct DirectoryEntryTreeRoots {
    Index master_root;
    Index folder_roots[FolderTypeCount];
};

enum AudioFileType : u8 {
    AudioFileTypeRaw16Pcm,
    AudioFileTypeRaw24Pcm,
    AudioFileTypeRaw32Fp,
    AudioFileTypeFlac,
};

enum FileType : u8 {
    FileTypeImage,
    FileTypeFont,
    FileTypeRawAudioSamples,
    FileTypeAudioFlac,
    FileTypeSpecialAudioData,
    FileTypePreset,
};

enum SpecialAudioDataType : s8 {
    SpecialAudioDataTypeNone = -1,
    SpecialAudioDataTypeSine,
    SpecialAudioDataTypeWhiteNoiseStereo,
    SpecialAudioDataTypeWhiteNoiseMono,
    SpecialAudioDataTypeCount,
};

struct FileInfo {
    u32 hash {};
    u32 size_bytes {};
    u64 offset_in_file_data_pool {};
    StringInPool name_no_ext {};
    StringInPool name {};
    StringInPool virtual_filepath {};

    FileType file_type {};
    FolderType folder_type {};
    Index index_in_folder_type {};

    SpecialAudioDataType special_audio_data_type {}; // ONLY valid if file_type = FileType_SpecialAudioData

    // valid if the file contains audio data
    u8 channels {};
    f32 sample_rate {};
    u32 num_frames {};
    AudioFileType audio_format {};
};

enum SampleLoopingMode : s8 {
    SampleLoopingModeDefault,
    SampleLoopingModeAlwaysLoopWholeRegion,
    SampleLoopingModeAlwaysLoopAnyRegion,
    SampleLoopingModeAlwaysLoopSetRegion,
};

struct SamplerRegionInfo {
    Index file_info_index = k_invalid_md_index; // index in files array
    Index inst_info_index = k_invalid_md_index; // index in the insts array
    Index group_index = k_invalid_md_index; // index within the group of the inst (0 to MAX_GROUPS-1)

    s8 root_note = 60; // c4
    s8 low_note = 0;
    s8 high_note = 127;
    s8 low_velo = 1;
    s8 high_velo = 127;
    s8 looping_mode = SampleLoopingModeDefault;
    s32 loop_start = 0;
    s32 loop_end = 0;
    s32 loop_crossfade = 0;
};

struct SamplerRegionGroup {
    Index index = k_invalid_md_index;
    StringInPool name = {};
    s32 round_robin_or_xfade_index = k_no_round_robin_or_xfade;
    s32 num_regions = 0;
};

struct InstrumentInfo {
    Span<SamplerRegionGroup const> Groups() const { return {groups, CheckedCast<usize>(num_groups)}; }

    u32 hash = 0;
    Index index = k_invalid_md_index; // index in insts array
    StringInPool name = {};
    StringInPool virtual_filepath = {};
    s32 num_groups = 0;
    SamplerRegionGroup groups[k_max_groups_in_inst] = {};
    s32 total_num_regions = 0;
    s32 max_rr_pos_or_xfade_index = 0;
    s8 unused_looping_mode {};

    // Confusingly, this points into the array of sampler regions, rather than the array of files. But for
    // backwards compatibility we can't change it.
    // NOTE(Sam, July 2024): despite the comment above, I'm getting unexpected results when I try to use this.
    // For now, I'm going to ignore this and just use the audio file of the instrument most-middle region -
    // this works great anyways.
    Index sampler_region_index_for_gui_waveform = k_invalid_md_index;
};

enum InstExtendedFlags : u8 {
    InstExtendedFlagsNone = 0,
    InstExtendedFlagsGroupsAreXfadeLayers = 1 << 0,
    InstExtendedFlagsFeatherVelocityLayers = 1 << 1,
    InstExtendedFlagsTriggerOnRelease = 1 << 2,
    InstExtendedFlagsIsWhiteNoiseStereo = 1 << 3,
    InstExtendedFlagsIsWhiteNoiseMono = 1 << 4,
};

struct ExtendedInstrumentInfo {
    Index inst_index {};
    u32 flags {InstExtendedFlagsNone};
};

constexpr String k_md_special_audio_filename_prefix = "#Special: ";
constexpr String k_md_special_audio_type_names[] = {
    "Sine",
    "White Noise",
    "White Noise (Mono)",
};

static_assert(ArraySize(k_md_special_audio_type_names) == SpecialAudioDataTypeCount, "");

//
//
//

PUBLIC constexpr u64 IrFileHash(mdata::FileInfo const& ir_file) {
    return (u64)ir_file.size_bytes << 32 | (u64)ir_file.num_frames;
}

PUBLIC String StringFromStringPool(char const* string_pool_block, mdata::StringInPool s) {
    auto start = string_pool_block + s.offset;
    return String(start, s.size);
}

PUBLIC mdata::SpecialAudioDataType SpecialAudioDataFromInstPath(String inst_path) {
    if (inst_path.size) {
        if (StartsWithSpan(inst_path, mdata::k_md_special_audio_filename_prefix)) {
            auto p = inst_path.SubSpan(mdata::k_md_special_audio_filename_prefix.size);
            int index = 0;
            for (auto& special_name : mdata::k_md_special_audio_type_names) {
                if (p == special_name) return (mdata::SpecialAudioDataType)index;
                index++;
            }
        }
    }
    return mdata::SpecialAudioDataTypeNone;
}

template <typename Type>
PUBLIC inline Type ClampCrossfadeSize(Type crossfade, Type start, Type end, Type total, bool is_ping_pong) {
    ASSERT(crossfade >= 0);
    auto loop_size = end - start;
    Type result;
    if (!is_ping_pong)
        result = Min(crossfade, loop_size, start);
    else
        result = Min(crossfade, start, total - end, loop_size);
    return Max<Type>(result, 0);
}

PUBLIC inline void SetReasonableLoopPoints(int& loop_start,
                                           int& loop_end,
                                           int& loop_crossfade,
                                           u64 total_frame_count,
                                           bool is_ping_pong) {
    s32 const smallest_loop_size_allowed = Max((s32)((f64)total_frame_count * 0.001), 32);
    loop_start = Max(0, loop_start);
    loop_end = Min((s32)total_frame_count, Max(loop_start + smallest_loop_size_allowed, loop_end));
    loop_crossfade =
        ClampCrossfadeSize<s32>(loop_crossfade, loop_start, loop_end, (s32)total_frame_count, is_ping_pong);
}

PUBLIC inline void
SetReasonableLoopPoints(mdata::SamplerRegionInfo& s, u64 total_frame_count, bool is_ping_pong) {
    SetReasonableLoopPoints(s.loop_start, s.loop_end, s.loop_crossfade, total_frame_count, is_ping_pong);
}

} // namespace mdata
