// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_pack_info.hpp"

PresetPackInfo ParsePresetPackInfoFile(String file_data, ArenaAllocator& arena) {
    PresetPackInfo pack {};

    for (auto line : SplitIterator {.whole = file_data, .token = '\n', .skip_consecutive = true}) {
        line = WhitespaceStripped(line);
        if (line.size == 0 || line[0] == ';') continue;

        auto const equals = Find(line, '=');
        if (!equals) continue;

        auto const key = WhitespaceStrippedEnd(line.SubSpan(0, *equals));
        if (key.size == 0) continue;

        auto const value_str = WhitespaceStripped(line.SubSpan(*equals + 1));
        if (value_str.size == 0) continue;

        if (key == "subtitle"_s) {
            pack.subtitle = arena.Clone(value_str);
        } else if (key == "minor_version"_s) {
            usize num_chars_read = {};
            if (auto const v = ParseInt(value_str, ParseIntBase::Decimal, &num_chars_read, false);
                v && num_chars_read == value_str.size &&
                *v <= LargestRepresentableValue<decltype(pack.minor_version)>()) {
                pack.minor_version = (decltype(pack.minor_version))*v;
            }
        } else if (key == "id"_s) {
            pack.id = Hash(value_str);
        }
    }

    return pack;
}
