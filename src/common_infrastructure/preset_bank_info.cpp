// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_bank_info.hpp"

#include "tests/framework.hpp"

#include "sample_library/sample_library.hpp"
#include "state/state_coding.hpp"

// Bank-relative preset paths must be portable and stay inside the bank folder.
static String ValidatedDefaultPreset(String value, ArenaAllocator& arena) {
    auto const path = arena.AllocateExactSizeUninitialised<char>(value.size);
    for (auto const index : Range(value.size))
        path[index] = value[index] == '\\' ? '/' : value[index];

    if (path::IsAbsolute(path, path::Format::Posix) || path::IsAbsolute(path, path::Format::Windows))
        return {};
    for (auto const part : SplitIterator {.whole = String(path), .token = '/', .skip_consecutive = true})
        if (part == ".."_s) return {};
    if (!PresetFormatFromPath(path)) return {};

    return path;
}

PresetBank ParsePresetBankFile(String file_data, ArenaAllocator& arena) {
    PresetBank bank {};

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
            bank.subtitle = arena.Clone(value_str);
        } else if (key == "revision"_s || key == "minor_version"_s) {
            usize num_chars_read = {};
            if (auto const v = ParseInt(value_str, ParseIntBase::Decimal, &num_chars_read, false);
                v && num_chars_read == value_str.size &&
                *v <= LargestRepresentableValue<decltype(bank.revision)>()) {
                bank.revision = (decltype(bank.revision))*v;
            }
        } else if (key == "id"_s) {
            bank.id = HashFnv1a(value_str);
        } else if (key == "default_preset"_s) {
            bank.default_preset = ValidatedDefaultPreset(value_str, arena);
        } else if (key == "library_for_visuals"_s) {
            bank.library_for_visuals_id = sample_lib::HashLibraryIdStringWithoutRegistration(value_str);
        } else if (key == "factory"_s) {
            bank.factory = IsEqualToCaseInsensitiveAscii(value_str, "true"_s);
        }
    }

    return bank;
}

TEST_CASE(TestPresetBankInfoParsing) {
    auto& arena = tester.scratch_arena;

    SUBCASE("full file") {
        auto const bank = ParsePresetBankFile("id = org.floe-audio.test\n"
                                              "subtitle = A test bank\n"
                                              "revision = 3\n"
                                              "default_preset = Pads/Init.floe-preset\n"
                                              "factory = true\n"_s,
                                              arena);
        CHECK_EQ(bank.id, HashFnv1a("org.floe-audio.test"_s));
        CHECK_EQ(bank.subtitle, "A test bank"_s);
        CHECK_EQ(bank.revision, (u16)3);
        CHECK_EQ(bank.default_preset, "Pads/Init.floe-preset"_s);
        CHECK(bank.factory);
    }

    SUBCASE("factory defaults to false and rejects unknown values") {
        CHECK(!ParsePresetBankFile("subtitle = A test bank\n"_s, arena).factory);
        CHECK(!ParsePresetBankFile("factory = maybe\n"_s, arena).factory);
        CHECK(ParsePresetBankFile("factory = TRUE\n"_s, arena).factory);
    }

    SUBCASE("backslashes are normalised") {
        auto const bank = ParsePresetBankFile("default_preset = Pads\\Init.floe-preset\n"_s, arena);
        CHECK_EQ(bank.default_preset, "Pads/Init.floe-preset"_s);
    }

    SUBCASE("invalid default_preset values are ignored") {
        for (auto const value : Array {
                 "/absolute/Init.floe-preset"_s,
                 "C:\\absolute\\Init.floe-preset"_s,
                 "../Init.floe-preset"_s,
                 "Pads/../../Init.floe-preset"_s,
                 "Init.txt"_s,
                 "Init"_s,
             }) {
            CAPTURE(value);
            auto const bank = ParsePresetBankFile(fmt::Format(arena, "default_preset = {}\n", value), arena);
            CHECK_EQ(bank.default_preset.size, 0uz);
        }
    }

    SUBCASE("missing default_preset") {
        auto const bank = ParsePresetBankFile("subtitle = No default\n"_s, arena);
        CHECK_EQ(bank.default_preset.size, 0uz);
    }

    return k_success;
}

TEST_REGISTRATION(RegisterPresetBankInfoTests) { REGISTER_TEST(TestPresetBankInfoParsing); }
