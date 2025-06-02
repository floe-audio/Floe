// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "gui2_common_picker.hpp"

struct InstPickerState {
    enum class Tab : u32 {
        FloeLibaries,
        MirageLibraries,
        Waveforms,
        Count,
    };

    Optional<sample_lib::FileFormat> FileFormatForCurrentTab() const {
        switch (tab) {
            case InstPickerState::Tab::FloeLibaries: return sample_lib::FileFormat::Lua;
            case InstPickerState::Tab::MirageLibraries: return sample_lib::FileFormat::Mdata;
            case InstPickerState::Tab::Waveforms: return k_nullopt;
            case InstPickerState::Tab::Count: PanicIfReached();
        }
        return k_nullopt;
    }

    Tab tab {Tab::FloeLibaries};
    CommonPickerState common_state_floe_libraries;
    CommonPickerState common_state_mirage_libraries;
    bool scroll_to_show_selected = false;
};
