// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "check_for_update.hpp"

#include "os/web.hpp"

namespace check_for_update {

constexpr auto k_current_version = ParseVersionString(FLOE_VERSION_STRING).Value();

void Init(State& state, prefs::Preferences const& prefs) {
    state.checking_allowed.Store(prefs::GetBool(prefs, CheckAllowedPrefDescriptor()),
                                 StoreMemoryOrder::Release);
}

constexpr auto k_ignore_updates_until_after_key = "ignore-updates-until-after"_s;

Optional<NewVersion> NewerVersionAvailable(State& state, prefs::Preferences const& prefs) {
    if (!state.checking_allowed.Load(LoadMemoryOrder::Acquire)) return k_nullopt;

    auto const latest =
        (prefs::GetBool(prefs, CheckBetaPrefDescriptor()) ? state.latest_version_edge : state.latest_version)
            .Load(LoadMemoryOrder::Acquire);
    if (latest.version == k_no_version) return k_nullopt;
    if (latest.version <= k_current_version) return k_nullopt;

    NewVersion result {
        .version = latest.version,
        .is_ignored = false,
    };
    if (auto const v_str = prefs::LookupString(prefs, k_ignore_updates_until_after_key)) {
        if (auto const v = ParseVersionString(*v_str)) {
            if (latest.version <= *v) result.is_ignored = true;
        }
    }

    return result;
}

void IgnoreUpdatesUntilAfter(prefs::Preferences& prefs, Version version) {
    auto const str = fmt::FormatInline<64>("{}", version);
    prefs::SetValue(prefs, k_ignore_updates_until_after_key, (String)str);
}

void FetchLatestIfNeeded(State& state) {
    State::StateEnum expected = State::StateEnum::Inactive;
    state.state.CompareExchangeStrong(expected,
                                      State::StateEnum::ShouldCheck,
                                      RmwMemoryOrder::AcquireRelease,
                                      LoadMemoryOrder::Acquire);
}

void CheckForUpdateIfNeeded(State& state) {
    if (!state.checking_allowed.Load(LoadMemoryOrder::Acquire)) return;
    if (state.state.Load(LoadMemoryOrder::Acquire) != State::StateEnum::ShouldCheck) return;

    state.state.Store(State::StateEnum::Checked, StoreMemoryOrder::Release);

    // IMPROVE: the Writer for bounded array will just truncate the data if it doesn't fit. We should return
    // an error.
    DynamicArrayBounded<char, 256> buffer {};
    auto const outcome =
        HttpsGet("https://floe.audio/api/v1/version"_s, dyn::WriterFor(buffer), {.timeout_seconds = 5});
    if (outcome.HasError()) return;

    // version is an INI-like file.
    for (auto line : SplitIterator {.whole = buffer, .token = '\n', .skip_consecutive = true}) {
        line = WhitespaceStrippedStart(line);
        if (line.size == 0 || line[0] == ';') continue;
        auto const equals = Find(line, '=');
        if (!equals) continue;
        auto key = WhitespaceStrippedEnd(line.SubSpan(0, *equals));
        auto const value = WhitespaceStripped(line.SubSpan(*equals + 1));
        if (value.size == 0) continue;

        if (key == "latest"_s) {
            if (auto const version = ParseVersionString(value); version)
                state.latest_version.Store({*version, 0}, StoreMemoryOrder::Release);
        } else if (key == "edge"_s) {
            if (auto const version = ParseVersionString(value); version)
                state.latest_version_edge.Store({*version, 0}, StoreMemoryOrder::Release);
        }
    }
}

void OnPreferenceChanged(State& state, prefs::Key const& key, prefs::Value const* value) {
    ASSERT(g_is_logical_main_thread);
    if (auto const v = prefs::Match(key, value, CheckAllowedPrefDescriptor()))
        state.checking_allowed.Store(v->Get<bool>(), StoreMemoryOrder::Release);
}

prefs::Descriptor CheckAllowedPrefDescriptor() {
    ASSERT(g_is_logical_main_thread);
    return {
        .key = "check-for-updates"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = true,
        .gui_label = "Check for updates"_s,
        .long_description = "Check if there's a new version of Floe available at startup"_s,
    };
}

prefs::Descriptor CheckBetaPrefDescriptor() {
    ASSERT(g_is_logical_main_thread);
    return {
        .key = "check-for-beta-updates"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
        .gui_label = "Include beta versions when checking for updates"_s,
        .long_description =
            "When checking for updates, include beta versions in addition to stable releases"_s,
    };
}

} // namespace check_for_update
