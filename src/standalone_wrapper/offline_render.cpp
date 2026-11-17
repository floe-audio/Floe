// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "offline_render.hpp"

#include <FLAC/stream_encoder.h>

#include "tests/framework.hpp"

static Optional<String> NextWhitespaceSeparatedToken(String& remaining) {
    while (remaining.size && IsWhitespace(remaining[0]))
        remaining.RemovePrefix(1);
    if (!remaining.size) return k_nullopt;
    usize token_size = 0;
    while (token_size < remaining.size && !IsWhitespace(remaining[token_size]))
        ++token_size;
    auto const token = remaining.SubSpan(0, token_size);
    remaining.RemovePrefix(token_size);
    return token;
}

Optional<f64> ParseWholeFloat(String str) {
    usize num_chars_read = 0;
    auto const value = ParseFloat(str, &num_chars_read);
    if (!value || num_chars_read != str.size) return k_nullopt;
    return value;
}

static Optional<u8> ParseHexByte(String token) {
    usize num_chars_read = 0;
    auto const value = ParseInt(token, ParseIntBase::Hexadecimal, &num_chars_read, false);
    if (!value || num_chars_read != token.size || *value < 0 || *value > 0xFF) return k_nullopt;
    return (u8)*value;
}

ValueOrError<Span<OfflineRenderEvent>, OfflineRenderEventsParseError>
ParseOfflineRenderEvents(String text, f64 sample_rate, ArenaAllocator& arena) {
    ASSERT(sample_rate > 0);

    DynamicArray<OfflineRenderEvent> events {arena};
    u32 line_number = 0;
    usize cursor = 0;
    while (auto const raw_line = SplitWithIterator(text, cursor, '\n')) {
        ++line_number;
        auto remaining = ({
            auto line = *raw_line;
            if (auto const comment_pos = Find(line, '#')) line = line.SubSpan(0, *comment_pos);
            line;
        });

        auto const time_token = NextWhitespaceSeparatedToken(remaining);
        if (!time_token) continue;

        auto const seconds = ({
            auto const parsed = ParseWholeFloat(*time_token);
            if (!parsed || !(*parsed >= 0) || *parsed > 1e7)
                return OfflineRenderEventsParseError {line_number, "invalid time, expected seconds >= 0"};
            *parsed;
        });

        DynamicArrayBounded<u8, 3> midi_bytes {};
        while (auto const byte_token = NextWhitespaceSeparatedToken(remaining)) {
            if (midi_bytes.size == midi_bytes.Capacity())
                return OfflineRenderEventsParseError {line_number, "too many MIDI bytes"};
            auto const byte = ParseHexByte(*byte_token);
            if (!byte) return OfflineRenderEventsParseError {line_number, "invalid hex MIDI byte"};
            dyn::Append(midi_bytes, *byte);
        }

        if (!midi_bytes.size) return OfflineRenderEventsParseError {line_number, "missing MIDI bytes"};

        auto const status = midi_bytes[0];
        if (status < 0x80 || status >= 0xF0)
            return OfflineRenderEventsParseError {line_number, "expected a channel message status byte"};
        auto const expected_data_bytes = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1u : 2u;
        if (midi_bytes.size - 1 != expected_data_bytes)
            return OfflineRenderEventsParseError {line_number, "wrong number of data bytes for status"};
        for (auto const data_byte : midi_bytes.Items().SubSpan(1))
            if (data_byte >= 0x80)
                return OfflineRenderEventsParseError {line_number, "MIDI data bytes must be below 80"};

        OfflineRenderEvent event {.frame = (u64)Round(seconds * sample_rate), .midi = {}};
        for (auto const byte_index : Range(midi_bytes.size))
            event.midi[byte_index] = midi_bytes[byte_index];

        // Insertion from the back keeps events with equal times in file order, and is cheap because most
        // files are already in time order.
        dyn::Append(events, event);
        for (auto index = events.size - 1; index > 0 && events[index - 1].frame > events[index].frame;
             --index)
            Swap(events[index - 1], events[index]);
    }

    return events.ToOwnedSpan();
}

Optional<RenderTail> ParseRenderTail(String str) {
    str = WhitespaceStripped(str);
    auto const is_threshold = EndsWithCaseInsensitiveAscii(str, "db"_s);
    if (is_threshold) str = WhitespaceStripped(str.SubSpan(0, str.size - 2));

    auto const value = ParseWholeFloat(str);
    if (!value) return k_nullopt;

    if (is_threshold) {
        if (!(*value >= -200 && *value <= 0)) return k_nullopt;
        return RenderTail {RenderTailUntilSilent {.threshold_db = (f32)*value}};
    }
    if (!(*value >= 0 && *value <= 3600)) return k_nullopt;
    return RenderTail {*value};
}

constexpr f64 k_until_silent_window_seconds = 3;

UntilSilentStatus
CheckUntilSilent(u64 tail_start_frame, u64 audible_end_frame, u64 current_frame, f64 sample_rate) {
    if (current_frame < tail_start_frame) return UntilSilentStatus::Continue;
    auto const seconds_since = [&](u64 frame) {
        return (f64)(current_frame - Min(frame, current_frame)) / sample_rate;
    };
    if (seconds_since(Max(audible_end_frame, tail_start_frame)) >= k_until_silent_window_seconds)
        return UntilSilentStatus::WentSilent;
    if (seconds_since(tail_start_frame) >= k_until_silent_timeout_seconds) return UntilSilentStatus::TimedOut;
    return UntilSilentStatus::Continue;
}

Optional<f32> ParseRenderGainDb(String str) {
    str = WhitespaceStripped(str);
    if (EndsWithCaseInsensitiveAscii(str, "db"_s)) str = WhitespaceStripped(str.SubSpan(0, str.size - 2));
    auto const value = ParseWholeFloat(str);
    if (!value || !(*value >= -60 && *value <= 24)) return k_nullopt;
    return (f32)*value;
}

Int24ConversionStats ConvertToInt24(Span<f32 const> samples, f32 gain, Span<s32> out) {
    ASSERT_EQ(samples.size, out.size);
    constexpr f32 k_max_24_bit = 8388607.0f;
    Int24ConversionStats stats {};
    for (auto const sample_index : Range(samples.size)) {
        auto const scaled = samples[sample_index] * gain;
        auto const magnitude = Abs(scaled);
        stats.peak = Max(stats.peak, magnitude);
        if (magnitude > 1.0f) ++stats.num_clipped_samples;
        out[sample_index] = (s32)Round(Clamp(scaled, -1.0f, 1.0f) * k_max_24_bit);
    }
    return stats;
}

Optional<Int24ConversionStats>
WriteFlacFile(String path, WriteFlacFileArgs args, ArenaAllocator& scratch_arena) {
    ASSERT(args.num_channels);
    ASSERT(args.sample_rate > 0);
    ASSERT(args.interleaved_samples.size % args.num_channels == 0);
    ASSERT(args.gain > 0);

    auto* encoder = FLAC__stream_encoder_new();
    if (!encoder) return k_nullopt;
    DEFER { FLAC__stream_encoder_delete(encoder); };

    auto const num_frames = args.interleaved_samples.size / args.num_channels;
    FLAC__stream_encoder_set_channels(encoder, args.num_channels);
    FLAC__stream_encoder_set_bits_per_sample(encoder, 24);
    FLAC__stream_encoder_set_sample_rate(encoder, (u32)Round(args.sample_rate));
    FLAC__stream_encoder_set_compression_level(encoder, 5);
    FLAC__stream_encoder_set_total_samples_estimate(encoder, num_frames);

    if (FLAC__stream_encoder_init_file(encoder, NullTerminated(path, scratch_arena), nullptr, nullptr) !=
        FLAC__STREAM_ENCODER_INIT_STATUS_OK)
        return k_nullopt;

    static_assert(sizeof(FLAC__int32) == sizeof(s32));
    constexpr usize k_chunk_frames = 4096;
    auto const chunk = scratch_arena.AllocateExactSizeUninitialised<s32>(k_chunk_frames * args.num_channels);
    Int24ConversionStats stats {};
    bool success = true;
    for (usize frame_pos = 0; frame_pos < num_frames && success; frame_pos += k_chunk_frames) {
        auto const chunk_frames = Min(k_chunk_frames, num_frames - frame_pos);
        auto const samples =
            args.interleaved_samples.SubSpan(frame_pos * args.num_channels, chunk_frames * args.num_channels);
        auto const chunk_stats = ConvertToInt24(samples, args.gain, chunk.SubSpan(0, samples.size));
        stats.peak = Max(stats.peak, chunk_stats.peak);
        stats.num_clipped_samples += chunk_stats.num_clipped_samples;
        success = FLAC__stream_encoder_process_interleaved(encoder,
                                                           (FLAC__int32 const*)chunk.data,
                                                           (u32)chunk_frames);
    }

    // Finishing writes the final metadata, so it has to happen even after a failure.
    if (!FLAC__stream_encoder_finish(encoder) || !success) return k_nullopt;
    return stats;
}

TEST_CASE(TestParseOfflineRenderEvents) {
    auto& arena = tester.scratch_arena;

    SUBCASE("parses, sorts stably and converts seconds to frames") {
        auto const result = ParseOfflineRenderEvents("# comment line\n"
                                                     "1.0 90 3C 64 # note on\n"
                                                     "\n"
                                                     "0.5\tb0 01 7f\r\n"
                                                     "1 80 3c 00\n"
                                                     "0 C0 05\n",
                                                     100,
                                                     arena);
        REQUIRE(!result.HasError());
        auto const events = result.ReleaseValue();
        REQUIRE_EQ(events.size, 4u);
        CHECK_EQ(events[0].frame, 0u);
        CHECK_EQ(events[0].midi[0], 0xC0);
        CHECK_EQ(events[0].midi[1], 0x05);
        CHECK_EQ(events[0].midi[2], 0);
        CHECK_EQ(events[1].frame, 50u);
        CHECK_EQ(events[1].midi[0], 0xB0);
        CHECK_EQ(events[2].frame, 100u);
        CHECK_EQ(events[2].midi[0], 0x90);
        CHECK_EQ(events[3].frame, 100u);
        CHECK_EQ(events[3].midi[0], 0x80);
    }

    SUBCASE("rejects invalid lines with their line number") {
        auto const check_error = [&](String text, u32 expected_line) {
            auto const result = ParseOfflineRenderEvents(text, 48000, arena);
            CHECK(result.HasError());
            if (result.HasError()) CHECK_EQ(result.Error().line_number, expected_line);
        };
        check_error("0 90 3C 64\n-1 90 3C 64", 2);
        check_error("abc 90 3C 64", 1);
        check_error("0", 1);
        check_error("0 3C 64", 1);
        check_error("0 F8", 1);
        check_error("0 90 3C", 1);
        check_error("0 C0 05 06", 1);
        check_error("0 90 3C 80", 1);
        check_error("0 90 3C 64 00", 1);
        check_error("0 90 3G 64", 1);
        check_error("nan 90 3C 64", 1);
    }

    return k_success;
}

TEST_CASE(TestParseRenderTail) {
    auto const check_seconds = [&](String str, f64 expected) {
        auto const tail = ParseRenderTail(str);
        REQUIRE(tail);
        REQUIRE(tail->tag == RenderTailType::Seconds);
        CHECK_EQ(tail->Get<f64>(), expected);
    };
    auto const check_threshold = [&](String str, f32 expected) {
        auto const tail = ParseRenderTail(str);
        REQUIRE(tail);
        REQUIRE(tail->tag == RenderTailType::UntilSilent);
        CHECK_EQ(tail->Get<RenderTailUntilSilent>().threshold_db, expected);
    };
    check_seconds("2.5", 2.5);
    check_seconds("0", 0);
    check_threshold("-90db", -90);
    check_threshold("-60.5 dB", -60.5f);
    check_threshold("-90DB", -90);
    CHECK(!ParseRenderTail(""));
    CHECK(!ParseRenderTail("db"));
    CHECK(!ParseRenderTail("-1"));
    CHECK(!ParseRenderTail("10db"));
    CHECK(!ParseRenderTail("2s"));
    return k_success;
}

TEST_CASE(TestCheckUntilSilent) {
    constexpr f64 k_sample_rate = 10;
    // Before the tail starts.
    CHECK(CheckUntilSilent(100, 0, 50, k_sample_rate) == UntilSilentStatus::Continue);
    // Silent since the tail started, but not for long enough.
    CHECK(CheckUntilSilent(100, 0, 110, k_sample_rate) == UntilSilentStatus::Continue);
    CHECK(CheckUntilSilent(100, 0, 130, k_sample_rate) == UntilSilentStatus::WentSilent);
    // Audible in the tail pushes the end back.
    CHECK(CheckUntilSilent(100, 120, 130, k_sample_rate) == UntilSilentStatus::Continue);
    CHECK(CheckUntilSilent(100, 120, 150, k_sample_rate) == UntilSilentStatus::WentSilent);
    // Never goes silent.
    CHECK(CheckUntilSilent(100, 699, 700, k_sample_rate) == UntilSilentStatus::TimedOut);
    return k_success;
}

TEST_CASE(TestParseRenderGainDb) {
    CHECK_EQ(ParseRenderGainDb("-12"), -12.0f);
    CHECK_EQ(ParseRenderGainDb("-12dB"), -12.0f);
    CHECK_EQ(ParseRenderGainDb(" 6.5 db"), 6.5f);
    CHECK_EQ(ParseRenderGainDb("0"), 0.0f);
    CHECK(!ParseRenderGainDb(""));
    CHECK(!ParseRenderGainDb("db"));
    CHECK(!ParseRenderGainDb("-61"));
    CHECK(!ParseRenderGainDb("25"));
    CHECK(!ParseRenderGainDb("nan"));
    return k_success;
}

TEST_CASE(TestConvertToInt24) {
    f32 const samples[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.5f, -2.0f};
    s32 out[ArraySize(samples)];

    SUBCASE("unity gain clamps overs and counts them") {
        auto const stats = ConvertToInt24(samples, 1.0f, out);
        CHECK_EQ(stats.peak, 2.0f);
        CHECK_EQ(stats.num_clipped_samples, 2u);
        CHECK_EQ(out[0], 0);
        CHECK_EQ(out[1], 4194304);
        CHECK_EQ(out[2], -4194304);
        CHECK_EQ(out[3], 8388607);
        CHECK_EQ(out[4], -8388607);
        CHECK_EQ(out[5], 8388607);
        CHECK_EQ(out[6], -8388607);
    }

    SUBCASE("gain is applied before clamping") {
        auto const stats = ConvertToInt24(samples, 0.25f, out);
        CHECK_EQ(stats.peak, 0.5f);
        CHECK_EQ(stats.num_clipped_samples, 0u);
        CHECK_EQ(out[5], 3145728);
        CHECK_EQ(out[6], -4194304);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterOfflineRenderTests) {
    REGISTER_TEST(TestParseOfflineRenderEvents);
    REGISTER_TEST(TestParseRenderTail);
    REGISTER_TEST(TestCheckUntilSilent);
    REGISTER_TEST(TestParseRenderGainDb);
    REGISTER_TEST(TestConvertToInt24);
}
