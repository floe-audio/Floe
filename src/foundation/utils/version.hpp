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

// Not full semantic version spec.
// Major, minor, patch, and beta are tracked. We support the specific syntax -beta.X for beta versions
// (e.g., 1.0.0-beta.1). All other suffixes after major.minor.patch are ignored.
struct Version {
    constexpr Version() = default;
    constexpr Version(u8 mj, u8 mn, u8 p) : major(mj), minor(mn), patch(p) {}
    constexpr Version(u8 mj, u8 mn, u8 p, u8 b) : major(mj), minor(mn), patch(p), beta(b) {}
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
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch && a.beta == b.beta;
    }
    friend bool operator<(Version const& a, Version const& b) {
        if (a.major < b.major) return true;
        if (a.major > b.major) return false;
        if (a.minor < b.minor) return true;
        if (a.minor > b.minor) return false;
        if (a.patch < b.patch) return true;
        if (a.patch > b.patch) return false;

        // Beta versions are less than release versions
        if (a.beta.HasValue() && !b.beta.HasValue()) return true;
        if (!a.beta.HasValue() && b.beta.HasValue()) return false;

        // Both are beta versions, compare beta numbers
        if (a.beta.HasValue() && b.beta.HasValue()) return a.beta.Value() < b.beta.Value();

        return false;
    }
    friend bool operator!=(Version const& lhs, Version const& rhs) { return !(lhs == rhs); }
    friend bool operator>(Version const& lhs, Version const& rhs) { return rhs < lhs; }
    friend bool operator<=(Version const& lhs, Version const& rhs) { return !(lhs > rhs); }
    friend bool operator>=(Version const& lhs, Version const& rhs) { return !(lhs < rhs); }

    static constexpr usize k_num_version_subdivisions = 3;
    u8 major {}, minor {}, patch {};
    Optional<u8> beta {};
};

PUBLIC constexpr Optional<Version> ParseVersionString(String str) {
    auto const first_dot = Find(str, '.');
    if (!first_dot) return {};

    auto const second_dot = Find(str, '.', *first_dot + 1);
    if (!second_dot) return {};

    if (second_dot == str.size - 1) return {};

    auto major_text = str.SubSpan(0, *first_dot);
    auto minor_text = str.SubSpan(*first_dot + 1, *second_dot - *first_dot - 1);
    auto patch_and_beta_text = str.SubSpan(*second_dot + 1);

    auto patch_text = patch_and_beta_text;
    Optional<u8> beta_version {};

    auto const dash = Find(patch_and_beta_text, '-');
    if (dash) {
        auto const suffix = patch_and_beta_text.SubSpan(*dash + 1);
        constexpr auto k_beta_suffix = "beta."_s;
        if (StartsWithSpan(suffix, k_beta_suffix)) {
            auto const beta_number_text = suffix.SubSpan(k_beta_suffix.size);
            usize num_chars_read = 0;
            if (auto n = ParseInt(beta_number_text, ParseIntBase::Decimal, &num_chars_read, false);
                n.HasValue() && num_chars_read == beta_number_text.size && n.Value() >= 0 &&
                n.Value() <= LargestRepresentableValue<u8>())
                beta_version = (u8)n.Value();
        }
    }

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

    result.beta = beta_version;
    return result;
}

constexpr Version k_floe_version = ParseVersionString(FLOE_VERSION_STRING).Value();

PUBLIC ErrorCodeOr<void>
CustomValueToString(Writer writer, Version const& version, fmt::FormatOptions options) {
    ASSERT(!options.required_width);
    TRY(ValueToString(writer, version.major, options));
    TRY(writer.WriteChar('.'));
    TRY(ValueToString(writer, version.minor, options));
    TRY(writer.WriteChar('.'));
    TRY(ValueToString(writer, version.patch, options));
    if (version.beta.HasValue()) {
        TRY(writer.WriteChars("-beta."));
        TRY(ValueToString(writer, version.beta.Value(), options));
    }
    return k_success;
}
