// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// Samples are kept in memory at the bit depth of the file they came from and converted to float as they're
// read. Integers up to 24 bits are exactly representable as f32, so nothing is lost.
enum class AudioSampleFormat : u8 {
    Int16,
    Int24, // Packed: 3 bytes per sample, little-endian.
    Float32,
    Count,
};

constexpr usize BytesPerSample(AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: return 2;
        case AudioSampleFormat::Int24: return 3;
        case AudioSampleFormat::Float32: return 4;
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

// When false, everything is stored as Float32. Integer storage saves RAM but costs CPU when reading samples.
constexpr bool k_store_samples_at_native_bit_depth = false;

// Multiply an unscaled sample by this to get it in the -1 to 1 range. Always a power of 2, so scaling is
// exact and can be done at any point in a chain of linear operations without changing the result.
constexpr f32 SampleScale(AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: return 1.0f / 32768.0f;
        case AudioSampleFormat::Int24: return 1.0f / 2147483648.0f;
        case AudioSampleFormat::Float32: return 1.0f;
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

// Int24 samples are read using loads that are wider than the sample, so this many bytes must be readable
// after the final sample.
constexpr usize k_audio_data_padding_bytes = 4;

struct AudioData {
    usize RamUsageBytes() const { return interleaved_samples.size; }
    usize NumSamples() const { return (usize)num_frames * channels; }

    u64 hash {};
    u8 channels {};
    f32 sample_rate {};
    u32 num_frames {};
    AudioSampleFormat format {};
    Span<u8 const> interleaved_samples {}; // In the given format. May include trailing padding.
};

// The Load* functions give 'unscaled' values: for Int16 that's the integer itself, and for Int24 it's the
// integer shifted up to the top of an s32, which is cheaper to produce than a sign-extended value and is
// still exact as a float. Pass the format as a compile-time constant from hot loops so that the switch is
// folded away.

template <typename Type>
ALWAYS_INLINE NO_UBSAN inline Type LoadUnalignedBytes(u8 const* bytes) {
    Type value;
    __builtin_memcpy_inline(&value, bytes, sizeof(value));
    return value;
}

// Takes 2 adjacent Int24 samples in the low 6 bytes of a u64 (or of each u64 in a vector), and moves each to
// the top 3 bytes of its own s32, zeroing the low byte.
template <typename Type>
ALWAYS_INLINE inline Type SpreadInt24Pair(Type raw) {
    return ((raw << 40) >> 32) | ((raw >> 24) << 40);
}

ALWAYS_INLINE NO_UBSAN inline f32 LoadUnscaledSample(u8 const* sample, AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: return (f32)LoadUnalignedBytes<s16>(sample);
        case AudioSampleFormat::Int24: return (f32)(s32)(LoadUnalignedBytes<u32>(sample) << 8);
        case AudioSampleFormat::Float32: return LoadUnalignedBytes<f32>(sample);
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

ALWAYS_INLINE NO_UBSAN inline f32x2 LoadUnscaledStereoFrame(u8 const* frame, AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: return ConvertVector(LoadUnalignedBytes<s16x2>(frame), f32x2);
        case AudioSampleFormat::Int24:
            return ConvertVector(__builtin_bit_cast(s32x2, SpreadInt24Pair(LoadUnalignedBytes<u64>(frame))),
                                 f32x2);
        case AudioSampleFormat::Float32: return LoadUnalignedBytes<f32x2>(frame);
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

// Mono frames are duplicated to both channels.
ALWAYS_INLINE NO_UBSAN inline f32x2
LoadUnscaledFrame(u8 const* frame, u8 channels, AudioSampleFormat format) {
    ASSERT_HOT(channels == 1 || channels == 2);
    if (channels == 1) return f32x2(LoadUnscaledSample(frame, format));
    return LoadUnscaledStereoFrame(frame, format);
}

// Two stereo frames packed as {L0, R0, L1, R1}.
ALWAYS_INLINE NO_UBSAN inline f32x4
LoadUnscaledStereoFramePair(u8 const* frame_0, u8 const* frame_1, AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: {
            s16x4 const values = __builtin_shufflevector(LoadUnalignedBytes<s16x2>(frame_0),
                                                         LoadUnalignedBytes<s16x2>(frame_1),
                                                         0,
                                                         1,
                                                         2,
                                                         3);
            return ConvertVector(values, f32x4);
        }
        case AudioSampleFormat::Int24: {
            u64x2 const raw {LoadUnalignedBytes<u64>(frame_0), LoadUnalignedBytes<u64>(frame_1)};
#if defined(__SSSE3__) || defined(__aarch64__)
            // A single byte shuffle does the same job as SpreadInt24Pair where the CPU has one.
            // clang-format off
            u8x16 const spread = __builtin_shufflevector((u8x16)raw, u8x16(0),
                                                         16, 0, 1, 2, 16, 3, 4, 5,
                                                         16, 8, 9, 10, 16, 11, 12, 13);
            // clang-format on
            return ConvertVector((s32x4)spread, f32x4);
#else
            return ConvertVector((s32x4)SpreadInt24Pair(raw), f32x4);
#endif
        }
        case AudioSampleFormat::Float32:
            return __builtin_shufflevector(LoadUnalignedBytes<f32x2>(frame_0),
                                           LoadUnalignedBytes<f32x2>(frame_1),
                                           0,
                                           1,
                                           2,
                                           3);
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

ALWAYS_INLINE NO_UBSAN inline f32x4 LoadUnscaledSampleQuad(u8 const* sample_0,
                                                           u8 const* sample_1,
                                                           u8 const* sample_2,
                                                           u8 const* sample_3,
                                                           AudioSampleFormat format) {
    switch (format) {
        case AudioSampleFormat::Int16: {
            s16x4 const values {LoadUnalignedBytes<s16>(sample_0),
                                LoadUnalignedBytes<s16>(sample_1),
                                LoadUnalignedBytes<s16>(sample_2),
                                LoadUnalignedBytes<s16>(sample_3)};
            return ConvertVector(values, f32x4);
        }
        case AudioSampleFormat::Int24: {
            u32x4 const packed {LoadUnalignedBytes<u32>(sample_0),
                                LoadUnalignedBytes<u32>(sample_1),
                                LoadUnalignedBytes<u32>(sample_2),
                                LoadUnalignedBytes<u32>(sample_3)};
            return ConvertVector((s32x4)(packed << 8), f32x4);
        }
        case AudioSampleFormat::Float32: {
            return f32x4 {LoadUnalignedBytes<f32>(sample_0),
                          LoadUnalignedBytes<f32>(sample_1),
                          LoadUnalignedBytes<f32>(sample_2),
                          LoadUnalignedBytes<f32>(sample_3)};
        }
        case AudioSampleFormat::Count: break;
    }
    PanicIfReached();
}

// Not for hot paths.
inline f32 SampleAsFloat(AudioData const& audio, usize sample_index) {
    ASSERT(sample_index < audio.NumSamples());
    return LoadUnscaledSample(audio.interleaved_samples.data + (sample_index * BytesPerSample(audio.format)),
                              audio.format) *
           SampleScale(audio.format);
}

inline void CopySamplesAsFloat(AudioData const& audio, Span<f32> out) {
    ASSERT_EQ(out.size, audio.NumSamples());
    for (auto const sample_index : Range(out.size))
        out[sample_index] = SampleAsFloat(audio, sample_index);
}
