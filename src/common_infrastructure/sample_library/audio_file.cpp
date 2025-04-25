// Copyright 2018-2024 Sam Windell
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
        }
        return writer.WriteChars(str);
    },
};

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
        Span<f32> interleaved_samples {};
        u32 samples_pos {};
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
            auto& context = *((Context*)user_data);

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
            auto& context = *((Context*)user_data);
            context.reader.pos = (usize)absolute_byte_offset;
            return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__uint64* absolute_byte_offset,
           void* user_data) -> FLAC__StreamDecoderTellStatus {
            // Tell callback
            auto& context = *((Context*)user_data);
            *absolute_byte_offset = context.reader.pos;
            return FLAC__STREAM_DECODER_TELL_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__uint64* stream_length,
           void* user_data) -> FLAC__StreamDecoderLengthStatus {
            // Length callback
            auto& context = *((Context*)user_data);
            *stream_length = context.reader.size;
            return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
        },
        [](FLAC__StreamDecoder const*, void* user_data) -> FLAC__bool {
            // Is end-of-file callback
            auto& context = *((Context*)user_data);
            return context.reader.pos == context.reader.size;
        },
        [](FLAC__StreamDecoder const*,
           FLAC__Frame const* frame,
           FLAC__int32 const* const buffer[],
           void* user_data) -> FLAC__StreamDecoderWriteStatus {
            ZoneScopedN("writing");
            // Write callback
            auto& context = *((Context*)user_data);

            auto const start_pos = context.samples_pos;

            if (frame->header.channels == 0 || frame->header.channels > 2) {
                context.error_code = AudioFileError::NotMonoOrStereo;
                return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            }

            u64 bits_per_sample = frame->header.bits_per_sample;
            if (!bits_per_sample) bits_per_sample = context.bits_per_sample;
            if (!bits_per_sample) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
            auto const divisor = (f32)(1ull << (bits_per_sample - 1));

            for (unsigned int chan = 0; chan < frame->header.channels; ++chan) {
                for (unsigned int sample = 0; sample < frame->header.blocksize; ++sample) {
                    auto const val = buffer[chan][sample];
                    context.interleaved_samples[start_pos + chan + (sample * frame->header.channels)] =
                        (f32)val / divisor;
                }
            }
            context.samples_pos += frame->header.blocksize * frame->header.channels;

            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        },
        [](FLAC__StreamDecoder const*, FLAC__StreamMetadata const* metadata, void* user_data) {
            ZoneScopedN("metadata");
            // Metadata callback
            // The StreamInfo block will always be before the stream
            auto& context = *((Context*)user_data);
            if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;
            auto const& info = metadata->data.stream_info;

            if (info.channels == 0 || info.channels > 2) {
                context.error_code = AudioFileError::NotMonoOrStereo;
                return;
            }

            ASSERT_EQ(context.bits_per_sample, 0u);

            context.bits_per_sample = info.bits_per_sample;

            context.hash = Hash(Span<u8 const> {info.md5sum, sizeof(info.md5sum)});
            context.sample_rate = (f32)info.sample_rate;
            context.channels = CheckedCast<u8>(info.channels);
            context.num_frames = CheckedCast<u32>(info.total_samples);
            context.interleaved_samples =
                context.allocator.AllocateExactSizeUninitialised<f32>(info.total_samples * info.channels);
        },
        [](FLAC__StreamDecoder const*, FLAC__StreamDecoderErrorStatus status, void* user_data) {
            // Error callback
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

    if (!process_success || context.flac_error) {
        if (context.interleaved_samples.size) allocator.Free(context.interleaved_samples.ToByteSpan());
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
            switch (origin) {
                case drwav_seek_origin_start: {
                    context.reader.pos = (usize)offset;
                    break;
                }
                case drwav_seek_origin_current: {
                    context.reader.pos = (usize)((s64)context.reader.pos + offset);
                    break;
                }
            }
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

    AudioData result {
        .channels = (u8)wav.channels,
        .sample_rate = (f32)wav.sampleRate,
        .num_frames = (u32)wav.totalPCMFrameCount,
        .interleaved_samples =
            allocator.AllocateExactSizeUninitialised<f32>(wav.totalPCMFrameCount * wav.channels),
    };

    auto const num_read =
        drwav_read_pcm_frames_f32(&wav, wav.totalPCMFrameCount, (f32*)result.interleaved_samples.data);
    if (num_read != wav.totalPCMFrameCount) {
        if (result.interleaved_samples.size) allocator.Free(result.interleaved_samples.ToByteSpan());
        if (context.error_code) return *context.error_code;
        return ErrorCode {AudioFileError::FileHasInvalidData};
    }

    result.hash = XXH3_64bits(result.interleaved_samples.data, result.interleaved_samples.ToByteSpan().size);
    return result;
}

ErrorCodeOr<AudioData> DecodeAudioFile(Reader& reader, String filepath_for_id, Allocator& allocator) {
    auto const file_extension = path::Extension(filepath_for_id);
    if (IsEqualToCaseInsensitiveAscii(file_extension, ".flac"_s)) {
        ZoneScopedN("flac");
        return DecodeFlac(reader, allocator);
    } else if (file_extension == k_raw_16_bit_stereo_44100_format_ext) {
        ZoneScopedN("raw");
        auto const num_samples = reader.size / sizeof(u16);
        AudioData result {
            .channels = 2,
            .sample_rate = 44100,
            .num_frames = (u32)(num_samples / 2),
            .interleaved_samples = allocator.AllocateExactSizeUninitialised<f32>(num_samples),
        };
        u32 sample_pos = 0;
        drwav_int16 buffer[2000];
        while (true) {
            auto const bytes_read = TRY(reader.Read({(u8*)buffer, sizeof(buffer)}));
            ASSERT((bytes_read % sizeof(u16)) == 0);
            auto const sample_read = bytes_read / sizeof(u16);
            drwav_s16_to_f32((f32*)result.interleaved_samples.data + sample_pos,
                             (drwav_int16 const*)buffer,
                             sample_read);
            sample_pos += sample_read;
            if (bytes_read != sizeof(buffer)) break;
        }
        result.hash =
            XXH3_64bits(result.interleaved_samples.data, result.interleaved_samples.ToByteSpan().size);
        return result;
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

    for (auto const name : Array {
             "16bit-mono.flac"_s,
             "16bit-stereo.flac"_s,
             "20bit-mono.flac"_s,
             "24bit-mono.wav"_s,
             "24bit-stereo.wav"_s,
             "raw-pcm-16bit-stereo-44100.r16"_s,
         }) {
        auto p = path::Join(a, Array {dir, name});
        auto reader = TRY(Reader::FromFile(p));
        auto audio = TRY(DecodeAudioFile(reader, p, a));
        CHECK(audio.channels);
        CHECK(audio.sample_rate != 0);
        CHECK(audio.num_frames != 0);
        CHECK(audio.interleaved_samples.size != 0);
        CHECK(audio.interleaved_samples[20] >= -1 && audio.interleaved_samples[20] <= 1);
    }

    for (auto const name : Array {
             "8bit-4chan.wav"_s, // 4 channels are not supported
         }) {
        auto p = path::Join(a, Array {dir, name});
        auto reader = TRY(Reader::FromFile(p));
        auto outcome = DecodeAudioFile(reader, p, a);
        CHECK(outcome.HasError());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterAudioFileTests) { REGISTER_TEST(TestAudioFormats); }
