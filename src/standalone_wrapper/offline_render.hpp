// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// Fails unless the entire string is a number.
Optional<f64> ParseWholeFloat(String str);

struct OfflineRenderEvent {
    u64 frame;
    u8 midi[3];
};

struct OfflineRenderEventsParseError {
    u32 line_number;
    String message;
};

// One event per line: "<seconds> <status> [<data1> [<data2>]]" with the MIDI bytes in hex, e.g.
// "1.5 90 3C 64". '#' starts a comment. Only channel messages are supported. The result is in time order,
// events at the same time keep their order from the text.
ValueOrError<Span<OfflineRenderEvent>, OfflineRenderEventsParseError>
ParseOfflineRenderEvents(String text, f64 sample_rate, ArenaAllocator& arena);

enum class RenderTailType : u8 { Seconds, UntilSilent };

struct RenderTailUntilSilent {
    f32 threshold_db;
};

using RenderTail = TaggedUnion<RenderTailType,
                               TypeAndTag<f64, RenderTailType::Seconds>,
                               TypeAndTag<RenderTailUntilSilent, RenderTailType::UntilSilent>>;

// Seconds ("2.5") or a silence threshold ("-90dB").
Optional<RenderTail> ParseRenderTail(String str);

enum class UntilSilentStatus : u8 { Continue, WentSilent, TimedOut };

// Ends once the output has stayed below the threshold for a few seconds after tail_start_frame, so gaps such
// as those between delay repeats don't end it early.
UntilSilentStatus
CheckUntilSilent(u64 tail_start_frame, u64 audible_end_frame, u64 current_frame, f64 sample_rate);

constexpr f64 k_until_silent_timeout_seconds = 60;

struct WriteFlacFileArgs {
    f64 sample_rate;
    u32 num_channels;
    Span<f32 const> interleaved_samples;
};

// 24-bit. Returns false on failure.
bool WriteFlacFile(String path, WriteFlacFileArgs args, ArenaAllocator& scratch_arena);
