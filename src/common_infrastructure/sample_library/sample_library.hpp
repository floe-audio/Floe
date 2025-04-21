// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/audio_data.hpp"

#include "mdata.hpp"

constexpr usize k_max_library_author_size = 64;
constexpr usize k_max_library_name_size = 64;
constexpr usize k_max_instrument_name_size = 64;
constexpr usize k_max_ir_name_size = 64;

namespace sample_lib {

// A type-safe wrapper to hold a relative path inside a library. This is used to refer to audio files, images,
// etc. It might not represent an actual file on disk. Give these to the library to get Reader.
struct LibraryPath {
    String str;
    constexpr bool operator==(LibraryPath const& other) const { return str == other.str; }
    constexpr bool operator==(String const& other) const { return str == other; }
    explicit operator String() const { return str; }
};

u64 Hash(LibraryPath const& path);

struct Range {
    constexpr bool operator==(Range const& other) const = default;
    constexpr u8 Size() const {
        ASSERT(end >= start);
        return end - start;
    }
    constexpr bool Contains(u8 v) const { return v >= start && v < end; }
    constexpr bool Overlaps(Range const& other) const { return start < other.end && other.start < end; }
    u8 start;
    u8 end; // non-inclusive, A.K.A. one-past the last
};

enum class TriggerEvent : u8 { NoteOn, NoteOff, Count };

enum class LoopMode : u8 { Standard, PingPong, Count };

enum class LoopRequirement : u8 { Default, AlwaysLoop, NeverLoop, Count };
enum class KeytrackRequirement : u8 { Default, Always, Never, Count };

// start and end can be negative meaning they're indexed from the end of the sample.
struct BuiltinLoop {
    s64 start_frame {};
    s64 end_frame {};
    u32 crossfade_frames {};
    LoopMode mode {};

    bool8 lock_loop_points : 1 {}; // Don't allow start, end or crossfade to be overriden.
    bool8 lock_mode : 1 {}; // Don't allow mode to be changed.
};

struct Region {
    LibraryPath path {};
    u8 root_key {};

    struct Loop {
        Optional<BuiltinLoop> builtin_loop {};
        LoopRequirement loop_requirement {LoopRequirement::Default};
    } loop;

    struct TriggerCriteria {
        TriggerEvent trigger_event {TriggerEvent::NoteOn};
        Range key_range {0, 128};
        Range velocity_range {0, 100};

        Optional<u8> round_robin_index {};
        u8 round_robin_sequencing_group {}; // Index into Instrument::round_robin_sequence_groups.

        bool feather_overlapping_velocity_layers {};
        // IMPROVE: add feather_overlapping_velocity_layers_curve: enum: equal-power, quarter-sine, linear

        // IMPROVE: add feather_overlapping_key_ranges
        // IMPROVE: add feather_overlapping_key_ranges_curve: enum: equal-power, quarter-sine, linear

        // private
        String round_robin_sequencing_group_name {"default-rr-group"};
        Optional<String> auto_map_key_range_group {};
    } trigger;

    struct AudioProperties {
        f32 gain_db {0};

        // IMPROVE: add pan
        // IMPROVE: add tune
        // struct Fade {
        //    Duration duration {}; // either samples, ms or %
        //    Curve curve {}; // enum: linear, exponential, quarter-sine, etc.
        // };
        // IMPROVE: add optional fade-in
        // IMPROVE: add optional fade-out
    } audio_props;

    struct Playback {
        KeytrackRequirement keytrack_requirement {KeytrackRequirement::Default};
        // IMPROVE: add monophonic_requirement: enum: default, always, never
        // IMPROVE: add volume_envelope_requirement?
    } playback;

    struct TimbreLayering {
        Optional<Range> layer_range {};
        // IMPROVE: add layer_range_curve: enum: equal-power, quarter-sine, linear
    } timbre_layering;
};

struct Library;

struct LoopOverview {
    Array<bool, ToInt(LoopMode::Count)> all_loops_convertible_to_mode {}; // Convertible or already in mode.
    Optional<LoopMode> all_loops_mode {}; // If all loop modes are the same mode, this will be set.
    bool8 has_loops : 1 {};
    bool8 has_non_loops : 1 {};
    bool8 user_defined_loops_allowed : 1 {};
    bool8 all_regions_require_looping : 1 {}; // Legacy option. If true, looping shouldn't be turned off.
};

struct RoundRobinGroup {
    u8 max_rr_pos {};
};

constexpr auto k_max_round_robin_sequence_groups = 64;

struct Instrument {
    Library const& library;

    String name {};
    Optional<String> folder {}; // may contain '/'
    Optional<String> description {};
    Span<String> tags {};
    LibraryPath audio_file_path_for_waveform {};
    Span<Region> regions {};
    usize regions_allocated_capacity {}; // private

    // IMPROVE: add options to always or never use Floe's volume envelope

    LoopOverview loop_overview {}; // Cached info about the loops in the regions.
    bool uses_timbre_layering {};
    Array<Span<RoundRobinGroup>, ToInt(TriggerEvent::Count)> round_robin_sequence_groups {};
};

// An instrument that has all its audio data loaded into memory.
struct LoadedInstrument {
    Instrument const& instrument;
    Span<AudioData const*> audio_datas {}; // parallel to instrument.regions
    AudioData const* file_for_gui_waveform {};
};

struct ImpulseResponse {
    Library const& library;

    String name {};
    LibraryPath path {};
    Optional<String> folder {}; // may contain '/'
    Span<String> tags {};
    Optional<String> description {};
};

// An impulse response that has its audio data loaded into memory.
struct LoadedIr {
    ImpulseResponse const& ir;
    AudioData const* audio_data;
};

enum class FileFormat { Mdata, Lua };

struct MdataSpecifics {
    HashTable<String, mdata::FileInfo const*> files_by_path;
    Span<mdata::FileInfo> file_infos {};
    String string_pool {};
    u64 file_data_pool_offset {}; // byte offset within the whole file
    Span<u8 const> file_data {}; // if the file from in-memory
};

struct LuaSpecifics {};

using FileFormatSpecifics = TaggedUnion<FileFormat,
                                        TypeAndTag<MdataSpecifics, FileFormat::Mdata>,
                                        TypeAndTag<LuaSpecifics, FileFormat::Lua>>;

struct LibraryIdRef {
    bool operator==(LibraryIdRef const& other) const = default;
    LibraryIdRef Clone(Allocator& arena, CloneType _ = CloneType::Shallow) const {
        return {.author = arena.Clone(author), .name = arena.Clone(name)};
    }
    u64 Hash() const { return HashMultiple(ArrayT<String const>({author, name})); }
    u64 HashWithExtra(String extra) const {
        return HashMultiple(ArrayT<String const>({extra, author, name}));
    }

    String author;
    String name;
};

u64 Hash(LibraryIdRef const& id);

struct FileAttribution {
    String title {}; // title of the work
    String license_name {};
    String license_url {};
    String attributed_to {};
    Optional<String> attribution_url {};
};

struct Library {
    LibraryIdRef Id() const { return {.author = author, .name = name}; }
    String name {};
    String tagline {};
    Optional<String> library_url {};
    Optional<String> description {};
    String author {};
    Optional<String> author_url {};
    u32 minor_version {1};
    Optional<LibraryPath> background_image_path {};
    Optional<LibraryPath> icon_image_path {};
    HashTable<String, Instrument*> insts_by_name {};
    Span<Instrument*> sorted_instruments {};
    HashTable<String, ImpulseResponse*> irs_by_name {};
    Span<ImpulseResponse*> sorted_irs {};
    HashTable<LibraryPath, FileAttribution, sample_lib::Hash> files_requiring_attribution {};
    u32 num_instrument_samples {};
    u32 num_regions {};
    String path {}; // real filesystem path to mdata or lua file
    u64 file_hash {};
    ErrorCodeOr<Reader> (*create_file_reader)(Library const&, LibraryPath path) {};
    FileFormatSpecifics file_format_specifics;
};

constexpr LibraryIdRef k_builtin_library_id = {.author = FLOE_VENDOR, .name = "Built-in"};

// MDATA libraries didn't have an author field, but they were all made by FrozenPlain.
constexpr String k_mdata_library_author = "FrozenPlain (Mirage)"_s;
constexpr LibraryIdRef k_mirage_compat_library_id = {.author = "FrozenPlain", .name = "Mirage Compatibility"};

struct LibraryId {
    LibraryId() = default;
    LibraryId(LibraryIdRef ref) : author(ref.author), name(ref.name) {}

    LibraryIdRef Ref() const { return {.author = author, .name = name}; }
    operator LibraryIdRef() const { return Ref(); }
    u64 Hash() const { return Ref().Hash(); }
    bool operator==(LibraryId const& other) const = default;
    bool operator==(LibraryIdRef const& other) const { return Ref() == other; }

    DynamicArrayBounded<char, k_max_library_author_size> author;
    DynamicArrayBounded<char, k_max_library_name_size> name;
};

struct InstrumentId {
    bool operator==(InstrumentId const& other) const = default;
    bool operator==(LoadedInstrument const& inst) const {
        return inst_name == inst.instrument.name && library == inst.instrument.library.Id();
    }
    u64 Hash() const { return library.Ref().HashWithExtra(inst_name); }
    LibraryId library;
    DynamicArrayBounded<char, k_max_instrument_name_size> inst_name;
};

struct IrId {
    bool operator==(IrId const& other) const = default;
    bool operator==(LoadedIr const& ir) const {
        return library == ir.ir.library.Id() && ir_name == ir.ir.name;
    }
    u64 Hash() const { return library.Ref().HashWithExtra(ir_name); }
    LibraryId library;
    DynamicArrayBounded<char, k_max_ir_name_size> ir_name;
};

enum class LuaErrorCode {
    Memory,
    Syntax,
    Runtime,
    Timeout,
    Unexpected,
};
extern ErrorCodeCategory const lua_error_category;
inline ErrorCodeCategory const& ErrorCategoryForEnum(LuaErrorCode) { return lua_error_category; }

ErrorCodeOr<u64> MdataHash(Reader& reader);
ErrorCodeOr<u64> LuaHash(Reader& reader);
ErrorCodeOr<u64> Hash(Reader& reader, FileFormat format);

struct Error {
    ErrorCode code;
    String message;
};

struct TryHelpersOutcomeToError {
    TRY_HELPER_INHERIT(IsError, TryHelpers)
    TRY_HELPER_INHERIT(ExtractValue, TryHelpers)
    template <typename T>
    static sample_lib::Error ExtractError(ErrorCodeOr<T> const& o) {
        return {o.Error(), ""_s};
    }
};

using LibraryPtrOrError = ValueOrError<Library*, Error>;

bool FilenameIsFloeLuaFile(String filename);
bool FilenameIsMdataFile(String filename);
Optional<FileFormat> DetermineFileFormat(String path);

// only honoured by the lua system
struct Options {
    usize max_memory_allowed = Mb(128);
    f64 max_seconds_allowed = 20;
};

namespace detail {
VoidOrError<String> PostReadBookkeeping(Library& lib, Allocator& arena, ArenaAllocator& scratch_arena);
}

LibraryPtrOrError ReadLua(Reader& reader,
                          String lua_filepath,
                          ArenaAllocator& result_arena,
                          ArenaAllocator& scatch_arena,
                          Options options = {});

LibraryPtrOrError
ReadMdata(Reader& reader, String filepath, ArenaAllocator& result_arena, ArenaAllocator& scratch_arena);

LibraryPtrOrError Read(Reader& reader,
                       FileFormat format,
                       String filepath,
                       ArenaAllocator& result_arena,
                       ArenaAllocator& scatch_arena,
                       Options options = {});

// Lua only
ErrorCodeOr<void> WriteDocumentedLuaExample(Writer writer, bool include_comments = true);
bool CheckAllReferencedFilesExist(Library const& lib, Writer error_writer);

} // namespace sample_lib

ErrorCodeOr<void> CustomValueToString(Writer writer, sample_lib::LibraryIdRef id, fmt::FormatOptions options);
