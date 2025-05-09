// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

constexpr u32 k_num_layers = 3;
constexpr u16 k_max_num_floe_instances = 256;
constexpr u8 k_max_tag_size = 30;
constexpr u8 k_max_num_tags = 30;
constexpr u8 k_max_preset_author_size = 64;
constexpr u8 k_max_preset_description_size = 255;
constexpr u8 k_max_instance_id_size = 16;
constexpr usize k_max_extra_scan_folders {16};
using FloeInstanceIndex = u16;
