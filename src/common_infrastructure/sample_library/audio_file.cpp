// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_file.hpp"

#include <FLAC/ordinals.h>
#include <FLAC/stream_decoder.h>
#include <dr_wav.h>
#include <xxhash.h>

#include "foundation/foundation.hpp"
#include "tests/framework.hpp"

ErrorCodeCategory const audio_file_error_category {
    .category_id = "AUD",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {};
        switch ((AudioFileError)code.code) {
            case AudioFileError::FileHasInvalidData: str = "file does not contain valid data"; break;
            case AudioFileError::NotFlacOrWav: str = "file must be FLAC or WAV"; break;
            case AudioFileError::NotMonoOrStereo: str = "file must be mono or stereo"; break;
            case AudioFileError::ApiError: str = "bug in FLAC decoder"; break;
        }
        return writer.WriteChars(str);
    },
};

// Integer sources up to 24 bits are stored as integers of the smallest format that holds them. Everything
// else is stored as float.
static AudioSampleFormat FormatForIntegerBitDepth(u32 bits_per_sample) {
    if (!k_store_samples_at_native_bit_depth) return AudioSampleFormat::Float32;
    if (bits_per_sample <= 16) return AudioSampleFormat::Int16;
    if (bits_per_sample <= 24) return AudioSampleFormat::Int24;
    return AudioSampleFormat::Float32;
}

static u32 IntegerBits(AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: return 16;
        case AudioSampleFormat::Int24: return 24;
        case AudioSampleFormat::Float32:
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

static Span<u8> AllocateSampleStorage(Allocator& allocator, usize num_samples, AudioSampleFormat format) {
    auto const sample_bytes = num_samples * BytesPerSample(format);
    auto const storage = allocator.Allocate({
        .size = sample_bytes + k_audio_data_padding_bytes,
        .alignment = alignof(f32),
        .allow_oversized_result = false,
    });
    ZeroMemory(storage.SubSpan(sample_bytes));
    return storage;
}

ALWAYS_INLINE static void WriteInt24(u8* destination, s32 value) {
    destination[0] = (u8)value;
    destination[1] = (u8)(value >> 8);
    destination[2] = (u8)(value >> 16);
}

static ErrorCodeOr<AudioData> DecodeFlac(Reader& reader, Allocator& allocator) {
    auto decoder = FLAC__stream_decoder_new();
    if (decoder == nullptr) Panic("out of memory");
    DEFER { FLAC__stream_decoder_delete(decoder); };

    struct Context {
        Reader& reader;
        Allocator& allocator;
        u64 hash {};
        u8 channels {};
        f32 sample_rate {};
        u32 num_frames {};
        AudioSampleFormat format {};
        Span<u8> interleaved_samples {};
        usize samples_pos {};
        u32 bits_per_sample {};
        Optional<FLAC__StreamDecoderErrorStatus> flac_error {};
        Optional<ErrorCode> error_code {};
    } context {
        .reader = reader,
        .allocator = allocator,
    };

    auto const init_status = FLAC__stream_decoder_init_stream(
        decoder,
        [](FLAC__StreamDecoder const*, FLAC__byte buffer[], usize* bytes, void* user_data)
            -> FLAC__StreamDecoderReadStatus {
            ZoneScopedN("reading file");
            // Read callback
            if (!user_data) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
            auto& context = *((Context*)user_data);

            if (!bytes) {
                context.error_code = AudioFileError::ApiError;
                return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
            }
            auto const requested_bytes = *bytes;

            auto outcome = context.reader.Read({buffer, *bytes});
            if (outcome.HasError()) {
                context.error_code = outcome.Error();
                return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
            }
            *bytes = outcome.Value();

            if (*bytes != requested_bytes) return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;

            return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__uint64 absolute_byte_offset,
           void* user_data) -> FLAC__StreamDecoderSeekStatus {
            // Seek callback
            if (!user_data) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
            auto& context = *((Context*)user_data);
            if (absolute_byte_offset > context.reader.size) return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
            context.reader.pos = (usize)absolute_byte_offset;
            return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__uint64* absolute_byte_offset,
           void* user_data) -> FLAC__StreamDecoderTellStatus {
            // Tell callback
            if (!user_data) return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
            auto& context = *((Context*)user_data);
            if (!absolute_byte_offset) {
                context.error_code = AudioFileError::ApiError;
                return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
            }
            *absolute_byte_offset = context.reader.pos;
            return FLAC__STREAM_DECODER_TELL_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__uint64* stream_length,
           void* user_data) -> FLAC__StreamDecoderLengthStatus {
            // Length callback
            if (!user_data) return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
            auto& context = *((Context*)user_data);
            if (!stream_length) {
                context.error_code = AudioFileError::ApiError;
                return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
            }
            *stream_length = context.reader.size;
            return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*, void* user_data) -> FLAC__bool {
            // Is end-of-file callback
            if (!user_data) return false;
            auto& context = *((Context*)user_data);
            return context.reader.pos == context.reader.size;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__Frame const* frame,
           FLAC__int32 const* const buffer[],
           void* user_data) -> FLAC__StreamDecoderWriteStatus {
            ZoneScopedN("writing");
            // Write callback
            if (!user_data) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            auto& context = *((Context*)user_data);

            if (!frame || !buffer) {
                context.error_code = AudioFileError::ApiError;
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            auto const start_pos = context.samples_pos;

            if (frame->header.channels == 0 || frame->header.channels > 2) {
                context.error_code = AudioFileError::NotMonoOrStereo;
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            if (frame->header.channels != context.channels) {
                context.error_code = ErrorCode(AudioFileError::FileHasInvalidData,
                                               "FLAC frame channels differ from STREAMINFO");
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            if ((u64)start_pos + ((u64)frame->header.blocksize * frame->header.channels) >
                (u64)context.num_frames * context.channels) {
                context.error_code = ErrorCode(AudioFileError::FileHasInvalidData,
                                               "FLAC has more samples than STREAMINFO states");
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            u32 bits_per_sample = frame->header.bits_per_sample;
            if (!bits_per_sample) bits_per_sample = context.bits_per_sample;
            if (!bits_per_sample) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

            auto const num_channels = frame->header.channels;
            auto const out = context.interleaved_samples.data;

            switch (context.format) {
                case AudioSampleFormat::Int16:
                case AudioSampleFormat::Int24: {
                    if (bits_per_sample > IntegerBits(context.format)) {
                        context.error_code =
                            ErrorCode(AudioFileError::FileHasInvalidData,
                                      "FLAC frame bit depth is larger than STREAMINFO states");
                        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
                    }

                    // Shifting up to the storage's bit depth gives exactly the same float value when it's
                    // later scaled by the storage's range.
                    auto const shift = IntegerBits(context.format) - bits_per_sample;

                    for (unsigned int chan = 0; chan < num_channels; ++chan) {
                        for (unsigned int sample = 0; sample < frame->header.blocksize; ++sample) {
                            auto const val = (s32)((u32)buffer[chan][sample] << shift);
                            auto const sample_index = start_pos + chan + ((usize)sample * num_channels);
                            if (context.format == AudioSampleFormat::Int16) {
                                auto const val16 = (s16)val;
                                __builtin_memcpy_inline(out + (sample_index * sizeof(s16)),
                                                        &val16,
                                                        sizeof(val16));
                            } else {
                                WriteInt24(out + (sample_index * 3), val);
                            }
                        }
                    }
                    break;
                }
                case AudioSampleFormat::Float32: {
                    auto const divisor = (f32)(1ull << (bits_per_sample - 1));
                    for (unsigned int chan = 0; chan < num_channels; ++chan) {
                        for (unsigned int sample = 0; sample < frame->header.blocksize; ++sample) {
                            auto const val = (f32)buffer[chan][sample] / divisor;
                            auto const sample_index = start_pos + chan + ((usize)sample * num_channels);
                            __builtin_memcpy_inline(out + (sample_index * sizeof(f32)), &val, sizeof(val));
                        }
                    }
                    break;
                }
                case AudioSampleFormat::Count: PanicIfReached();
            }
            context.samples_pos += frame->header.blocksize * frame->header.channels;

            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        },
        [](FLAC__StreamDecoder const*, FLAC__StreamMetadata const* metadata, void* user_data) {
            ZoneScopedN("metadata");
            // Metadata callback
            // The StreamInfo block will always be before the stream
            if (!user_data) return;
            auto& context = *((Context*)user_data);
            if (!metadata) return;
            if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;
            auto const& info = metadata->data.stream_info;

            if (info.channels == 0 || info.channels > 2) {
                context.error_code = AudioFileError::NotMonoOrStereo;
                return;
            }

            if (info.total_samples == 0 || info.total_samples > LargestRepresentableValue<u32>()) {
                context.error_code = ErrorCode(AudioFileError::FileHasInvalidData,
                                               "FLAC STREAMINFO has unsupported total samples");
                return;
            }

            ASSERT_EQ(context.bits_per_sample, 0u);

            context.bits_per_sample = info.bits_per_sample;

            context.hash = Hash(Span<u8 const> {info.md5sum, sizeof(info.md5sum)});
            context.sample_rate = (f32)info.sample_rate;
            context.channels = CheckedCast<u8>(info.channels);
            context.num_frames = CheckedCast<u32>(info.total_samples);
            context.format = FormatForIntegerBitDepth(info.bits_per_sample);
            context.interleaved_samples = AllocateSampleStorage(context.allocator,
                                                                (usize)info.total_samples * info.channels,
                                                                context.format);
        },
        [](FLAC__StreamDecoder const*, FLAC__StreamDecoderErrorStatus status, void* user_data) {
            // Error callback
            if (!user_data) return;
            auto& context = *((Context*)user_data);
            context.flac_error = status;
        },
        &context);

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        switch (init_status) {
            case FLAC__STREAM_DECODER_INIT_STATUS_OK: break;
            case FLAC__STREAM_DECODER_INIT_STATUS_UNSUPPORTED_CONTAINER: PanicIfReached(); break;
            case FLAC__STREAM_DECODER_INIT_STATUS_INVALID_CALLBACKS: PanicIfReached(); break;
            case FLAC__STREAM_DECODER_INIT_STATUS_MEMORY_ALLOCATION_ERROR: PanicIfReached(); break;
            case FLAC__STREAM_DECODER_INIT_STATUS_ERROR_OPENING_FILE: PanicIfReached(); break;
            case FLAC__STREAM_DECODER_INIT_STATUS_ALREADY_INITIALIZED: PanicIfReached(); break;
        }
        return ErrorCode(AudioFileError::FileHasInvalidData,
                         FLAC__StreamDecoderInitStatusString[init_status]);
    }
    DEFER { FLAC__stream_decoder_finish(decoder); };

    auto const process_success = FLAC__stream_decoder_process_until_end_of_stream(decoder);

    if (!process_success || context.flac_error || context.error_code) {
        if (context.interleaved_samples.size) allocator.Free(context.interleaved_samples);
        if (context.error_code) return *context.error_code;
        if (context.flac_error)
            return ErrorCode(AudioFileError::FileHasInvalidData,
                             FLAC__StreamDecoderErrorStatusString[*context.flac_error]);
        return ErrorCode(AudioFileError::FileHasInvalidData,
                         "FLAC__stream_decoder_process_until_end_of_stream");
    }

    return AudioData {
        .hash = context.hash,
        .channels = context.channels,
        .sample_rate = context.sample_rate,
        .num_frames = context.num_frames,
        .format = context.format,
        .interleaved_samples = context.interleaved_samples,
    };
}

static ErrorCodeOr<AudioData> DecodeWav(Reader& reader, Allocator& allocator) {
    struct Context {
        Reader& reader;
        Optional<ErrorCode> error_code {};
    } context {
        .reader = reader,
    };

    drwav wav;
    auto const init_success = drwav_init(
        &wav,
        [](void* user_data, void* buffer_out, size_t bytes_to_read) -> size_t {
            ZoneScopedN("reading file");
            auto& context = *(Context*)user_data;
            auto outcome = context.reader.Read({(u8*)buffer_out, bytes_to_read});
            if (outcome.HasError()) {
                context.error_code = outcome.Error();
                return 0;
            }
            return outcome.Value();
        },
        [](void* user_data, int offset, drwav_seek_origin origin) -> drwav_bool32 {
            auto& context = *(Context*)user_data;
            auto const new_pos = ({
                s64 pos {};
                switch (origin) {
                    case drwav_seek_origin_start: pos = offset; break;
                    case drwav_seek_origin_current: pos = (s64)context.reader.pos + offset; break;
                }
                pos;
            });
            if (new_pos < 0 || (u64)new_pos > context.reader.size) return DRWAV_FALSE;
            context.reader.pos = (usize)new_pos;
            return DRWAV_TRUE;
        },
        &context,
        nullptr);
    if (!init_success) {
        if (context.error_code) return *context.error_code;
        return ErrorCode {AudioFileError::FileHasInvalidData};
    }
    DEFER { drwav_uninit(&wav); };

    if (wav.channels == 0 || wav.channels > 2) return ErrorCode {AudioFileError::NotMonoOrStereo};

    auto const format =
        wav.translatedFormatTag == DR_WAVE_FORMAT_PCM && (wav.bitsPerSample == 16 || wav.bitsPerSample == 24)
            ? FormatForIntegerBitDepth(wav.bitsPerSample)
            : AudioSampleFormat::Float32;

    auto const num_samples = (usize)wav.totalPCMFrameCount * wav.channels;
    auto const storage = AllocateSampleStorage(allocator, num_samples, format);

    auto const num_frames_read = ({
        u64 n = 0;
        switch (format) {
            case AudioSampleFormat::Int16:
                n = drwav_read_pcm_frames_s16(&wav,
                                              wav.totalPCMFrameCount,
                                              CheckedPointerCast<drwav_int16*>(storage.data));
                break;
            case AudioSampleFormat::Int24: {
                // 24-bit samples are given in the top 3 bytes of each s32.
                drwav_int32 chunk[2048];
                auto const frames_per_chunk = ArraySize(chunk) / wav.channels;
                while (n != wav.totalPCMFrameCount) {
                    auto const frames_read =
                        drwav_read_pcm_frames_s32(&wav,
                                                  Min<u64>(frames_per_chunk, wav.totalPCMFrameCount - n),
                                                  chunk);
                    if (!frames_read) break;
                    auto const out = storage.data + (n * wav.channels * 3);
                    for (auto const sample_index : Range((usize)frames_read * wav.channels))
                        WriteInt24(out + (sample_index * 3), chunk[sample_index] >> 8);
                    n += frames_read;
                }
                break;
            }
            case AudioSampleFormat::Float32:
                n = drwav_read_pcm_frames_f32(&wav,
                                              wav.totalPCMFrameCount,
                                              CheckedPointerCast<f32*>(storage.data));
                break;
            case AudioSampleFormat::Count: PanicIfReached();
        }
        n;
    });

    if (num_frames_read != wav.totalPCMFrameCount) {
        allocator.Free(storage);
        if (context.error_code) return *context.error_code;
        return ErrorCode {AudioFileError::FileHasInvalidData};
    }

    return AudioData {
        .hash = XXH3_64bits(storage.data, num_samples * BytesPerSample(format)),
        .channels = (u8)wav.channels,
        .sample_rate = (f32)wav.sampleRate,
        .num_frames = (u32)wav.totalPCMFrameCount,
        .format = format,
        .interleaved_samples = storage,
    };
}

ErrorCodeOr<AudioData> DecodeAudioFile(Reader& reader, String filepath_for_id, Allocator& allocator) {
    auto const file_extension = path::Extension(filepath_for_id);
    if (IsEqualToCaseInsensitiveAscii(file_extension, ".flac"_s)) {
        ZoneScopedN("flac");
        return DecodeFlac(reader, allocator);
    } else if (file_extension == k_raw_16_bit_stereo_44100_format_ext) {
        ZoneScopedN("raw");
        auto const num_samples = reader.size / sizeof(s16);
        auto const sample_bytes = num_samples * sizeof(s16);
        auto const storage = AllocateSampleStorage(allocator, num_samples, AudioSampleFormat::Int16);
        usize bytes_pos = 0;
        while (bytes_pos != sample_bytes) {
            auto const outcome = reader.Read({storage.data + bytes_pos, sample_bytes - bytes_pos});
            if (outcome.HasError()) {
                allocator.Free(storage);
                return outcome.Error();
            }
            if (!outcome.Value()) {
                allocator.Free(storage);
                return ErrorCode {AudioFileError::FileHasInvalidData};
            }
            bytes_pos += outcome.Value();
        }
        AudioData audio {
            .hash = XXH3_64bits(storage.data, sample_bytes),
            .channels = 2,
            .sample_rate = 44100,
            .num_frames = (u32)(num_samples / 2),
            .format = AudioSampleFormat::Int16,
            .interleaved_samples = storage,
        };
        if (!k_store_samples_at_native_bit_depth) {
            auto const float_storage =
                AllocateSampleStorage(allocator, num_samples, AudioSampleFormat::Float32);
            CopySamplesAsFloat(audio, {CheckedPointerCast<f32*>(float_storage.data), num_samples});
            allocator.Free(storage);
            audio.format = AudioSampleFormat::Float32;
            audio.interleaved_samples = float_storage;
        }
        return audio;
    } else if (IsEqualToCaseInsensitiveAscii(file_extension, ".wav"_s)) {
        ZoneScopedN("wav");
        return DecodeWav(reader, allocator);
    }

    return ErrorCode {AudioFileError::NotFlacOrWav};
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestAudioFormats) {
    auto& a = tester.scratch_arena;
    auto const dir = String(path::Join(a, Array {TestFilesFolder(tester), "audio"}));

    struct TestFile {
        String name;
        AudioSampleFormat expected_format;
    };

    for (auto const& file : Array {
             TestFile {"16bit-mono.flac"_s, AudioSampleFormat::Int16},
             TestFile {"16bit-stereo.flac"_s, AudioSampleFormat::Int16},
             TestFile {"20bit-mono.flac"_s, AudioSampleFormat::Int24},
             TestFile {"24bit-mono.wav"_s, AudioSampleFormat::Int24},
             TestFile {"24bit-stereo.wav"_s, AudioSampleFormat::Int24},
             TestFile {"raw-pcm-16bit-stereo-44100.r16"_s, AudioSampleFormat::Int16},
         }) {
        CAPTURE(file.name);
        auto p = path::Join(a, Array {dir, file.name});
        auto reader = TRY(Reader::FromFile(p));
        auto audio = TRY(DecodeAudioFile(reader, p, a));
        CHECK(audio.channels);
        CHECK(audio.sample_rate != 0);
        CHECK(audio.num_frames != 0);
        CHECK_EQ(audio.format,
                 k_store_samples_at_native_bit_depth ? file.expected_format : AudioSampleFormat::Float32);
        CHECK_EQ(audio.interleaved_samples.size,
                 (audio.NumSamples() * BytesPerSample(audio.format)) + k_audio_data_padding_bytes);

        bool has_signal = false;
        for (auto const sample_index : Range(audio.NumSamples())) {
            auto const sample = SampleAsFloat(audio, sample_index);
            if (sample != 0) has_signal = true;
            if (sample < -1 || sample > 1) {
                CAPTURE(sample_index);
                CHECK(false);
                break;
            }
        }
        CHECK(has_signal);

        // The stored samples must convert to exactly the floats that a direct conversion gives.
        if (path::Extension(file.name) == ".wav"_s) {
            auto const file_data = TRY(ReadEntireFile(p, a));
            drwav wav;
            REQUIRE(drwav_init_memory(&wav, file_data.data, file_data.size, nullptr));
            DEFER { drwav_uninit(&wav); };
            auto const reference = a.AllocateExactSizeUninitialised<f32>(audio.NumSamples());
            REQUIRE_EQ(drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, reference.data),
                       (u64)audio.num_frames);
            for (auto const sample_index : Range(audio.NumSamples())) {
                if (SampleAsFloat(audio, sample_index) != reference[sample_index]) {
                    CAPTURE(sample_index);
                    CHECK(false);
                    break;
                }
            }
        }
    }

    for (auto const name : Array {
             "8bit-4chan.wav"_s, // 4 channels are not supported
         }) {
        auto p = path::Join(a, Array {dir, name});
        auto reader = TRY(Reader::FromFile(p));
        auto outcome = DecodeAudioFile(reader, p, a);
        CHECK(outcome.HasError());
    }

    SUBCASE("FLAC with wrong STREAMINFO total samples") {
        auto const p = path::Join(a, Array {dir, "16bit-mono.flac"_s});
        auto const file_data = TRY(ReadEntireFile(p, a));

        // STREAMINFO's 36-bit total samples field occupies the low 4 bits of byte 21 and bytes 22-25.
        for (auto const total_samples : Array {0u, 1u}) {
            auto data = a.Clone(file_data);
            data[21] = (char)(data[21] & 0xF0);
            data[22] = (char)((total_samples >> 24) & 0xFF);
            data[23] = (char)((total_samples >> 16) & 0xFF);
            data[24] = (char)((total_samples >> 8) & 0xFF);
            data[25] = (char)(total_samples & 0xFF);

            auto reader = Reader::FromMemory(data);
            auto outcome = DecodeAudioFile(reader, p, a);
            CHECK(outcome.HasError());
        }
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAudioFileTests) { REGISTER_TEST(TestAudioFormats); }
