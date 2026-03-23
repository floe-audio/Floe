// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

constexpr u32 k_block_size_max = 32;
constexpr u32 k_max_num_voice_sound_sources = 4;
constexpr u32 k_num_layers = 3;
constexpr u16 k_max_num_floe_instances = 256;
constexpr u8 k_max_tag_size = 30;
constexpr u8 k_max_num_tags = 30;
constexpr u8 k_max_preset_author_size = 64;
constexpr u8 k_max_preset_description_size = 255;
constexpr u8 k_max_instance_id_size = 16;
constexpr usize k_max_extra_scan_folders {16};
using FloeInstanceIndex = u16;

// TODO: move these to a separate header
constexpr usize k_num_harmony_interval_bits = 97;
constexpr int k_harmony_interval_centre_bit = 48; // unison (0 semitones)
constexpr int k_harmony_interval_min_semitone = -48;
constexpr int k_harmony_interval_max_semitone = 48;

constexpr usize HarmonyIntervalBitIndex(int semitones) {
    return (usize)(semitones + k_harmony_interval_centre_bit);
}

constexpr int HarmonyIntervalSemitones(usize bit_index) {
    return (int)bit_index - k_harmony_interval_centre_bit;
}

using HarmonyIntervalsBitset = Bitset<k_num_harmony_interval_bits>;
