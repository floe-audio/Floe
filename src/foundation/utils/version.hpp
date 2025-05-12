// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/optional.hpp"
#include "foundation/utils/format.hpp"

PUBLIC constexpr u32 PackVersionIntoU32(u16 maj, u8 min, u8 patch) {
    return ((u32)maj << 16) | ((u32)min << 8) | ((u32)patch);
}
PUBLIC constexpr u16 ExtractMajorFromPackedVersion(u32 packed) { return (packed & 0xffff0000) >> 16; }
PUBLIC constexpr u8 ExtractMinorFromPackedVersion(u32 packed) { return (packed & 0x0000ff00) >> 8; }
PUBLIC constexpr u8 ExtractPatchFromPackedVersion(u32 packed) { return packed & 0x000000ff; }

// WARNING: only major, minor, and patch are tracked. Semantic Versioning pre-release or build metadata is
// ignored.
struct Version {
    constexpr Version() = default;
    constexpr Version(u8 mj, u8 mn, u8 p) : major(mj), minor(mn), patch(p) {}
    constexpr explicit Version(u32 packed_version) {
        major = CheckedCast<u8>(ExtractMajorFromPackedVersion(packed_version));
        minor = ExtractMinorFromPackedVersion(packed_version);
        patch = ExtractPatchFromPackedVersion(packed_version);
    }

    u8& operator[](usize const index) const {
        ASSERT(index < k_num_version_subdivisions);
        return ((u8*)&major)[index];
    }

    bool IsEmpty() const { return !major && !minor && !patch; }
    u32 Packed() const { return PackVersionIntoU32(major, minor, patch); }

    friend bool operator==(Version const& a, Version const& b) {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
    }
    friend bool operator<(Version const& a, Version const& b) {
        if (a.major < b.major) return true;
        if (a.major > b.major) return false;
        if (a.minor < b.minor) return true;
        if (a.minor > b.minor) return false;
        if (a.patch < b.patch) return true;
        if (a.patch > b.patch) return false;
        return false;
    }
    friend bool operator!=(Version const& lhs, Version const& rhs) { return !(lhs == rhs); }
    friend bool operator>(Version const& lhs, Version const& rhs) { return rhs < lhs; }
    friend bool operator<=(Version const& lhs, Version const& rhs) { return !(lhs > rhs); }
    friend bool operator>=(Version const& lhs, Version const& rhs) { return !(lhs < rhs); }

    static constexpr usize k_num_version_subdivisions = 3;
    u8 major {}, minor {}, patch {};
};

PUBLIC constexpr Optional<Version> ParseVersionString(String str) {
    auto const first_dot = Find(str, '.');
    if (!first_dot) return {};

    auto const second_dot = Find(str, '.', *first_dot + 1);
    if (!second_dot) return {};

    if (second_dot == str.size - 1) return {};

    auto major_text = str.SubSpan(0, *first_dot);
    auto minor_text = str.SubSpan(*first_dot + 1, *second_dot - *first_dot - 1);
    auto patch_text = str.SubSpan(*second_dot + 1);

    Version result {};
    usize num_chars_read = 0;
    if (auto n = ParseInt(major_text, ParseIntBase::Decimal, &num_chars_read, false);
        n.HasValue() && num_chars_read == major_text.size && n.Value() >= 0 &&
        n.Value() <= LargestRepresentableValue<decltype(result.major)>())
        result.major = (u8)n.Value();
    else
        return k_nullopt;

    if (auto n = ParseInt(minor_text, ParseIntBase::Decimal, &num_chars_read, false);
        n.HasValue() && num_chars_read == minor_text.size && n.Value() >= 0 &&
        n.Value() <= LargestRepresentableValue<decltype(result.minor)>())
        result.minor = (u8)n.Value();
    else
        return k_nullopt;

    if (auto n = ParseInt(patch_text, ParseIntBase::Decimal, nullptr, false);
        n.HasValue() && n.Value() >= 0 && n.Value() <= LargestRepresentableValue<decltype(result.patch)>())
        result.patch = (u8)n.Value();
    else
        return k_nullopt;

    return result;
}

PUBLIC ErrorCodeOr<void>
CustomValueToString(Writer writer, Version const& version, fmt::FormatOptions options) {
    ASSERT(!options.required_width);
    TRY(ValueToString(writer, version.major, options));
    TRY(writer.WriteChar('.'));
    TRY(ValueToString(writer, version.minor, options));
    TRY(writer.WriteChar('.'));
    TRY(ValueToString(writer, version.patch, options));
    return k_success;
}
