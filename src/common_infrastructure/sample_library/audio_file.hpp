// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/reader.hpp"

#include "common_infrastructure/audio_data.hpp"

constexpr String k_raw_16_bit_stereo_44100_format_ext = ".r16";

enum class AudioFileError {
    FileHasInvalidData,
    NotFlacOrWav,
    NotMonoOrStereo,
    ApiError,
};
extern ErrorCodeCategory const audio_file_error_category;
inline ErrorCodeCategory const& ErrorCategoryForEnum(AudioFileError) { return audio_file_error_category; }

// reader is used to get the file data, not the path argument
ErrorCodeOr<AudioData> DecodeAudioFile(Reader& reader, String filepath_for_id, Allocator& allocator);
