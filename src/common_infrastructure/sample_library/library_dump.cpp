// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "library_dump.hpp"

#include <stb_sprintf.h>

#include "common_infrastructure/folder_node.hpp"
#include "common_infrastructure/tags.hpp"

namespace library_dump {

static ErrorCodeOr<void> WriteIndent(Context& c) {
    for (auto n = c.indent; n > 0; --n)
        TRY(c.out.WriteChar('\t'));
    return k_success;
}

static ErrorCodeOr<void> WriteEncodedString(Context& c, String s) {
    for (auto const ch : s) {
        switch (ch) {
            case '"': TRY(c.out.WriteChars("\\\""_s)); break;
            case '\\': TRY(c.out.WriteChars("\\\\"_s)); break;
            case '\n': TRY(c.out.WriteChars("\\n"_s)); break;
            case '\r': TRY(c.out.WriteChars("\\r"_s)); break;
            case '\t': TRY(c.out.WriteChars("\\t"_s)); break;
            case '\b': TRY(c.out.WriteChars("\\b"_s)); break;
            case '\f': TRY(c.out.WriteChars("\\f"_s)); break;
            default:
                if ((u8)ch < 0x20) {
                    // JSON: \u00XX; Lua: \xXX (Lua 5.x supports hex escapes).
                    TRY(c.out.WriteChars(c.format == Format::Json ? "\\u00"_s : "\\x"_s));
                    auto const hex =
                        fmt::IntToString((u8)ch, {.base = fmt::IntToStringOptions::Base::Hexadecimal});
                    if (hex.size == 1) TRY(c.out.WriteChar('0'));
                    TRY(c.out.WriteChars(hex));
                } else {
                    TRY(c.out.WriteChar(ch));
                }
        }
    }
    return k_success;
}

static ErrorCodeOr<void> WriteSeparator(Context& c) {
    if (c.last != Context::Last::Open) TRY(c.out.WriteChar(','));
    TRY(c.out.WriteChar('\n'));
    return k_success;
}

static ErrorCodeOr<void> WriteValueIndent(Context& c) {
    if (c.last != Context::Last::Key) {
        TRY(WriteSeparator(c));
        TRY(WriteIndent(c));
    }
    c.last = Context::Last::Value;
    return k_success;
}

static ErrorCodeOr<void> WriteOpenContainer(Context& c, char json_char) {
    if (c.last == Context::Last::Value || c.last == Context::Last::Close)
        TRY(c.out.WriteChars(",\n"_s));
    else if (c.last == Context::Last::Open)
        TRY(c.out.WriteChar('\n'));
    if (c.last != Context::Last::Key) TRY(WriteIndent(c));
    TRY(c.out.WriteChar(c.format == Format::Lua ? '{' : json_char));
    c.indent++;
    c.last = Context::Last::Open;
    return k_success;
}

static ErrorCodeOr<void> WriteCloseContainer(Context& c, char json_char) {
    if (c.last != Context::Last::Open) TRY(c.out.WriteChar('\n'));
    c.indent--;
    if (c.last != Context::Last::Open) TRY(WriteIndent(c));
    TRY(c.out.WriteChar(c.format == Format::Lua ? '}' : json_char));
    c.last = Context::Last::Close;
    return k_success;
}

ErrorCodeOr<void> WriteObjectBegin(Context& c) { return WriteOpenContainer(c, '{'); }
ErrorCodeOr<void> WriteObjectEnd(Context& c) { return WriteCloseContainer(c, '}'); }
ErrorCodeOr<void> WriteArrayBegin(Context& c) { return WriteOpenContainer(c, '['); }
ErrorCodeOr<void> WriteArrayEnd(Context& c) { return WriteCloseContainer(c, ']'); }

ErrorCodeOr<void> WriteKey(Context& c, String key) {
    TRY(WriteSeparator(c));
    TRY(WriteIndent(c));
    if (c.format == Format::Json) {
        TRY(c.out.WriteChar('"'));
        TRY(WriteEncodedString(c, key));
        TRY(c.out.WriteChars("\": "_s));
    } else {
        TRY(c.out.WriteChars(key));
        TRY(c.out.WriteChars(" = "_s));
    }
    c.last = Context::Last::Key;
    return k_success;
}

ErrorCodeOr<void> WriteNull(Context& c) {
    TRY(WriteValueIndent(c));
    return c.out.WriteChars(c.format == Format::Json ? "null"_s : "nil"_s);
}

ErrorCodeOr<void> WriteValue(Context& c, String v) {
    TRY(WriteValueIndent(c));
    TRY(c.out.WriteChar('"'));
    TRY(WriteEncodedString(c, v));
    return c.out.WriteChar('"');
}

ErrorCodeOr<void> WriteValue(Context& c, bool v) {
    TRY(WriteValueIndent(c));
    return c.out.WriteChars(v ? "true"_s : "false"_s);
}

ErrorCodeOr<void> WriteValueInteger(Context& c, s64 v) {
    TRY(WriteValueIndent(c));
    return c.out.WriteChars(fmt::IntToString(v, {.base = fmt::IntToStringOptions::Base::Decimal}));
}

ErrorCodeOr<void> WriteValueFloat(Context& c, f64 v, bool single_precision) {
    TRY(WriteValueIndent(c));
    char buffer[128];
    auto const n = stbsp_snprintf(buffer, (int)ArraySize(buffer), single_precision ? "%.9g" : "%.17g", v);
    auto str = String(buffer, (usize)n);
    if (auto const dot = Find(str, '.'); dot && ContainsOnly(str.SubSpan(*dot + 1), '0'))
        str = {str.data, *dot};
    return c.out.WriteChars(str);
}

ErrorCodeOr<void> WriteKeyObjectBegin(Context& c, String key) {
    TRY(WriteKey(c, key));
    return WriteObjectBegin(c);
}

ErrorCodeOr<void> WriteKeyArrayBegin(Context& c, String key) {
    TRY(WriteKey(c, key));
    return WriteArrayBegin(c);
}

ErrorCodeOr<void> WriteKeyNull(Context& c, String key) {
    TRY(WriteKey(c, key));
    return WriteNull(c);
}

// ----- enum → string helpers ------------------------------------------------

static String LoopModeString(sample_lib::LoopMode m) {
    switch (m) {
        case sample_lib::LoopMode::Standard: return "standard"_s;
        case sample_lib::LoopMode::PingPong: return "ping-pong"_s;
        case sample_lib::LoopMode::Count: break;
    }
    return "unknown"_s;
}

static String LoopRequirementString(sample_lib::LoopRequirement r) {
    switch (r) {
        case sample_lib::LoopRequirement::Default: return "default"_s;
        case sample_lib::LoopRequirement::AlwaysLoop: return "always-loop"_s;
        case sample_lib::LoopRequirement::NeverLoop: return "never-loop"_s;
        case sample_lib::LoopRequirement::Count: break;
    }
    return "unknown"_s;
}

static String KeytrackRequirementString(sample_lib::KeytrackRequirement r) {
    switch (r) {
        case sample_lib::KeytrackRequirement::Default: return "default"_s;
        case sample_lib::KeytrackRequirement::Always: return "always"_s;
        case sample_lib::KeytrackRequirement::Never: return "never"_s;
        case sample_lib::KeytrackRequirement::Count: break;
    }
    return "unknown"_s;
}

static String TriggerEventString(sample_lib::TriggerEvent e) {
    switch (e) {
        case sample_lib::TriggerEvent::NoteOn: return "note-on"_s;
        case sample_lib::TriggerEvent::NoteOff: return "note-off"_s;
        case sample_lib::TriggerEvent::Count: break;
    }
    return "unknown"_s;
}

static String SamplerCategoryString(sample_lib::SamplerCategory c) {
    switch (c) {
        case sample_lib::SamplerCategory::Empty: return "empty"_s;
        case sample_lib::SamplerCategory::Sliced: return "sliced"_s;
        case sample_lib::SamplerCategory::SingleSample: return "single-sample"_s;
        case sample_lib::SamplerCategory::Multisample: return "multisample"_s;
    }
    return "unknown"_s;
}

static String FileFormatString(sample_lib::FileFormat f) {
    switch (f) {
        case sample_lib::FileFormat::Lua: return "lua"_s;
        case sample_lib::FileFormat::Mdata: return "mdata"_s;
    }
    return "unknown"_s;
}

// Builds the path "Top/Sub/Leaf" by walking up to (but not including) the root folder, since the root folder
// just holds the library display name and is not meaningful for navigation.
static String FolderPathString(FolderNode const* folder, ArenaAllocator& arena) {
    if (!folder) return ""_s;
    DynamicArray<String> parts {arena};
    for (auto f = folder; f && f->parent; f = f->parent)
        dyn::Append(parts, f->name);
    if (parts.size == 0) return ""_s;
    DynamicArray<char> result {arena};
    for (auto i = (ssize)parts.size - 1; i >= 0; --i) {
        if (result.size) dyn::Append(result, '/');
        dyn::AppendSpan(result, parts[(usize)i]);
    }
    return result.ToOwnedSpan();
}

static ErrorCodeOr<void> WriteTagsArray(Context& ctx, TagsBitset const& tags) {
    TRY(WriteArrayBegin(ctx));
    tags.ForEachSetBit([&](usize bit) {
        auto const& info = GetTagInfo((TagType)bit);
        auto _ = WriteValue(ctx, info.name);
    });
    TRY(WriteArrayEnd(ctx));
    return k_success;
}

static ErrorCodeOr<void> WriteRegion(Context& ctx, sample_lib::Region const& region) {
    TRY(WriteObjectBegin(ctx));
    TRY(WriteKeyValue(ctx, "sample_path", (String)region.path));
    TRY(WriteKeyValue(ctx, "root_key", (s64)region.root_key));

    TRY(WriteKeyObjectBegin(ctx, "trigger"));
    TRY(WriteKeyValue(ctx, "event", TriggerEventString(region.trigger.trigger_event)));
    TRY(WriteKeyValue(ctx, "key_low", (s64)region.trigger.key_range.start));
    TRY(WriteKeyValue(ctx, "key_high", (s64)region.trigger.key_range.end));
    TRY(WriteKeyValue(ctx, "velo_low", (s64)region.trigger.velocity_range.start));
    TRY(WriteKeyValue(ctx, "velo_high", (s64)region.trigger.velocity_range.end));
    if (region.trigger.round_robin_index)
        TRY(WriteKeyValue(ctx, "round_robin_index", (s64)*region.trigger.round_robin_index));
    TRY(WriteKeyValue(ctx, "round_robin_sequencing_group", (s64)region.trigger.round_robin_sequencing_group));
    TRY(WriteKeyValue(ctx,
                      "feather_overlapping_velocity_layers",
                      region.trigger.feather_overlapping_velocity_layers));
    TRY(WriteObjectEnd(ctx));

    TRY(WriteKeyObjectBegin(ctx, "loop"));
    TRY(WriteKeyValue(ctx, "requirement", LoopRequirementString(region.loop.loop_requirement)));
    if (region.loop.builtin_loop) {
        auto const& bl = *region.loop.builtin_loop;
        TRY(WriteKeyObjectBegin(ctx, "builtin"));
        TRY(WriteKeyValue(ctx, "mode", LoopModeString(bl.mode)));
        TRY(WriteKeyValue(ctx, "start_frame", bl.start_frame));
        TRY(WriteKeyValue(ctx, "end_frame", bl.end_frame));
        TRY(WriteKeyValue(ctx, "crossfade_frames", (s64)bl.crossfade_frames));
        TRY(WriteKeyValue(ctx, "lock_loop_points", (bool)bl.lock_loop_points));
        TRY(WriteKeyValue(ctx, "lock_mode", (bool)bl.lock_mode));
        TRY(WriteObjectEnd(ctx));
    }
    TRY(WriteObjectEnd(ctx));

    TRY(WriteKeyObjectBegin(ctx, "audio_props"));
    TRY(WriteKeyValue(ctx, "gain_db", region.audio_props.gain_db));
    TRY(WriteKeyValue(ctx, "tune_cents", region.audio_props.tune_cents));
    TRY(WriteKeyValue(ctx, "start_offset_frames", (s64)region.audio_props.start_offset_frames));
    TRY(WriteKeyValue(ctx, "fade_in_frames", (s64)region.audio_props.fade_in_frames));
    TRY(WriteObjectEnd(ctx));

    TRY(WriteKeyObjectBegin(ctx, "playback"));
    TRY(WriteKeyValue(ctx,
                      "keytrack_requirement",
                      KeytrackRequirementString(region.playback.keytrack_requirement)));
    TRY(WriteObjectEnd(ctx));

    if (region.timbre_layering.layer_range) {
        TRY(WriteKeyObjectBegin(ctx, "timbre_layering"));
        TRY(WriteKeyValue(ctx, "layer_low", (s64)region.timbre_layering.layer_range->start));
        TRY(WriteKeyValue(ctx, "layer_high", (s64)region.timbre_layering.layer_range->end));
        TRY(WriteObjectEnd(ctx));
    }

    if (region.slices.size) {
        TRY(WriteKeyArrayBegin(ctx, "slices"));
        for (auto const& s : region.slices) {
            TRY(WriteObjectBegin(ctx));
            TRY(WriteKeyValue(ctx, "start_frame", (s64)s.start_frame));
            TRY(WriteKeyValue(ctx, "length_proportion", (s64)s.length_proportion));
            TRY(WriteObjectEnd(ctx));
        }
        TRY(WriteArrayEnd(ctx));
        TRY(WriteKeyValue(ctx, "loop_beats", (s64)region.loop_beats));
        TRY(WriteKeyValue(ctx, "native_bpm", region.native_bpm));
    }

    TRY(WriteObjectEnd(ctx));
    return k_success;
}

static ErrorCodeOr<void>
WriteInstrument(Context& ctx, sample_lib::Instrument const& inst, ArenaAllocator& arena) {
    TRY(WriteObjectBegin(ctx));
    TRY(WriteKeyValue(ctx, "id", inst.id));
    TRY(WriteKeyValue(ctx, "name", inst.name));
    TRY(WriteKeyValue(ctx, "folder", FolderPathString(inst.folder, arena)));
    if (inst.description)
        TRY(WriteKeyValue(ctx, "description", *inst.description));
    else
        TRY(WriteKeyNull(ctx, "description"));
    TRY(WriteKeyValue(ctx, "category", SamplerCategoryString(inst.category)));
    TRY(WriteKeyValue(ctx, "uses_timbre_layering", inst.uses_timbre_layering));
    TRY(WriteKey(ctx, "tags"));
    TRY(WriteTagsArray(ctx, inst.tags));

    TRY(WriteKeyObjectBegin(ctx, "loop_overview"));
    TRY(WriteKeyValue(ctx, "has_loops", (bool)inst.loop_overview.has_loops));
    TRY(WriteKeyValue(ctx, "has_non_loops", (bool)inst.loop_overview.has_non_loops));
    TRY(WriteKeyValue(ctx,
                      "user_defined_loops_allowed",
                      (bool)inst.loop_overview.user_defined_loops_allowed));
    TRY(WriteKeyValue(ctx,
                      "all_regions_require_looping",
                      (bool)inst.loop_overview.all_regions_require_looping));
    if (inst.loop_overview.all_loops_mode)
        TRY(WriteKeyValue(ctx, "all_loops_mode", LoopModeString(*inst.loop_overview.all_loops_mode)));
    TRY(WriteObjectEnd(ctx));

    if (inst.named_key_ranges.size) {
        TRY(WriteKeyArrayBegin(ctx, "named_key_ranges"));
        for (auto const& r : inst.named_key_ranges) {
            TRY(WriteObjectBegin(ctx));
            TRY(WriteKeyValue(ctx, "name", r.name));
            TRY(WriteKeyValue(ctx, "key_low", (s64)r.key_range.start));
            TRY(WriteKeyValue(ctx, "key_high", (s64)r.key_range.end));
            TRY(WriteObjectEnd(ctx));
        }
        TRY(WriteArrayEnd(ctx));
    }

    TRY(WriteKeyValue(ctx, "num_regions", (s64)inst.regions.size));
    TRY(WriteKeyArrayBegin(ctx, "regions"));
    for (auto const& region : inst.regions)
        TRY(WriteRegion(ctx, region));
    TRY(WriteArrayEnd(ctx));

    TRY(WriteObjectEnd(ctx));
    return k_success;
}

static ErrorCodeOr<void> WriteIr(Context& ctx, sample_lib::ImpulseResponse const& ir) {
    TRY(WriteObjectBegin(ctx));
    TRY(WriteKeyValue(ctx, "id", ir.id));
    TRY(WriteKeyValue(ctx, "name", ir.name));
    TRY(WriteKeyValue(ctx, "path", (String)ir.path));
    if (ir.description)
        TRY(WriteKeyValue(ctx, "description", *ir.description));
    else
        TRY(WriteKeyNull(ctx, "description"));
    TRY(WriteKey(ctx, "tags"));
    TRY(WriteTagsArray(ctx, ir.tags));
    TRY(WriteKeyObjectBegin(ctx, "audio_props"));
    TRY(WriteKeyValue(ctx, "gain_db", ir.audio_props.gain_db));
    TRY(WriteObjectEnd(ctx));
    TRY(WriteObjectEnd(ctx));
    return k_success;
}

ErrorCodeOr<void> Dump(Context& ctx, sample_lib::Library const& lib, ArenaAllocator& arena) {
    TRY(WriteKeyObjectBegin(ctx, "library"));
    TRY(WriteKeyValue(ctx, "path", lib.path));
    TRY(WriteKeyValue(ctx, "format", FileFormatString(lib.file_format_specifics.tag)));
    TRY(WriteKeyValue(ctx, "id", lib.id_string));
    TRY(WriteKeyValue(ctx, "name", lib.name));
    TRY(WriteKeyValue(ctx, "author", lib.author));
    TRY(WriteKeyValue(ctx, "revision", (s64)lib.revision));
    TRY(WriteKeyValue(ctx, "tagline", lib.tagline));
    if (lib.description)
        TRY(WriteKeyValue(ctx, "description", *lib.description));
    else
        TRY(WriteKeyNull(ctx, "description"));
    if (lib.library_url)
        TRY(WriteKeyValue(ctx, "library_url", *lib.library_url));
    else
        TRY(WriteKeyNull(ctx, "library_url"));
    if (lib.author_url)
        TRY(WriteKeyValue(ctx, "author_url", *lib.author_url));
    else
        TRY(WriteKeyNull(ctx, "author_url"));
    if (lib.background_image_path)
        TRY(WriteKeyValue(ctx, "background_image_path", (String)*lib.background_image_path));
    else
        TRY(WriteKeyNull(ctx, "background_image_path"));
    if (lib.icon_image_path)
        TRY(WriteKeyValue(ctx, "icon_image_path", (String)*lib.icon_image_path));
    else
        TRY(WriteKeyNull(ctx, "icon_image_path"));
    TRY(WriteKeyValue(ctx, "num_instruments", (s64)lib.insts_by_id.size));
    TRY(WriteKeyValue(ctx, "num_irs", (s64)lib.irs_by_id.size));
    TRY(WriteKeyValue(ctx, "num_regions", (s64)lib.num_regions));
    TRY(WriteKeyValue(ctx, "num_instrument_samples", (s64)lib.num_instrument_samples));
    TRY(WriteObjectEnd(ctx));

    TRY(WriteKeyArrayBegin(ctx, "instruments"));
    for (auto inst : lib.sorted_instruments)
        TRY(WriteInstrument(ctx, *inst, arena));
    TRY(WriteArrayEnd(ctx));

    TRY(WriteKeyArrayBegin(ctx, "irs"));
    for (auto ir : lib.sorted_irs)
        TRY(WriteIr(ctx, *ir));
    TRY(WriteArrayEnd(ctx));

    TRY(WriteKeyArrayBegin(ctx, "files_requiring_attribution"));
    for (auto [path, attribution, _] : lib.files_requiring_attribution) {
        TRY(WriteObjectBegin(ctx));
        TRY(WriteKeyValue(ctx, "path", path.str));
        TRY(WriteKeyValue(ctx, "title", attribution.title));
        TRY(WriteKeyValue(ctx, "license_name", attribution.license_name));
        TRY(WriteKeyValue(ctx, "license_url", attribution.license_url));
        TRY(WriteKeyValue(ctx, "attributed_to", attribution.attributed_to));
        if (attribution.attribution_url)
            TRY(WriteKeyValue(ctx, "attribution_url", *attribution.attribution_url));
        else
            TRY(WriteKeyNull(ctx, "attribution_url"));
        TRY(WriteObjectEnd(ctx));
    }
    TRY(WriteArrayEnd(ctx));

    return k_success;
}

} // namespace library_dump
