// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "sample_lib_server/sample_library_server.hpp"

constexpr auto k_picker_item_height = 20.0f;
constexpr auto k_picker_spacing = 8.0f;

constexpr auto k_untagged_tag_name = "<untagged>"_s;

enum class SearchDirection { Forward, Backward };

enum class FilterMode : u8 {
    ProgressiveNarrowing, // AKA "match all", AND
    AdditiveSelection, // AKA "match any", OR
    Count,
};

struct CommonPickerState {
    bool HasFilters() const {
        if (selected_library_hashes.size || selected_library_author_hashes.size ||
            selected_tags_hashes.size || selected_folder_hashes.size)
            return true;
        for (auto const& hashes : other_selected_hashes)
            if (hashes->size) return true;
        return false;
    }

    void ClearAll() {
        dyn::Clear(selected_library_hashes);
        dyn::Clear(selected_library_author_hashes);
        dyn::Clear(selected_tags_hashes);
        dyn::Clear(selected_folder_hashes);
        for (auto other_hashes : other_selected_hashes)
            dyn::Clear(*other_hashes);
    }

    DynamicArray<u64> selected_library_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_library_author_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_tags_hashes {Malloc::Instance()};
    DynamicArray<u64> selected_folder_hashes {Malloc::Instance()};
    DynamicArrayBounded<char, 100> search {};
    DynamicArrayBounded<DynamicArray<u64>*, 2> other_selected_hashes {};
    FilterMode filter_mode = FilterMode::ProgressiveNarrowing;
};

// Ephemeral
struct PickerPopupContext {
    sample_lib_server::Server& sample_library_server;
    sample_lib::LibraryIdRef const* hovering_lib {};
    CommonPickerState& state;
};

struct FilterItemInfo {
    u32 num_used_in_items_lists {};
    u32 total_available {};
};

struct TagsFilters {
    HashTable<String, FilterItemInfo> tags;
};

bool RootNodeLessThan(FolderNode const* const& a,
                      DummyValueType const&,
                      FolderNode const* const& b,
                      DummyValueType const&);

using FolderRootSet = OrderedSet<FolderNode const*, nullptr, RootNodeLessThan>;

struct FolderFilters {
    HashTable<FolderNode const*, FilterItemInfo> folders;
    FolderRootSet root_folders;
};

struct LibraryFilters {
    LibraryImagesArray& library_images;
    OrderedHashTable<sample_lib::LibraryIdRef, FilterItemInfo> libraries;
    OrderedHashTable<String, FilterItemInfo> library_authors;
    Optional<graphics::ImageID> unknown_library_icon;
};

// IMPORTANT: we use FunctionRef here, you need to make sure the lifetime of the functions outlives the
// options.
struct PickerPopupOptions {
    struct Button {
        String text {};
        String tooltip {};
        f32 icon_scaling {};
        TrivialFunctionRef<void()> on_fired {};
    };

    struct Column {
        String title {};
        f32 width {};
    };

    String title {};
    f32 height {}; // VW
    f32 rhs_width {}; // VW
    f32 filters_col_width {}; // VW

    String item_type_name {}; // "instrument", "preset", etc.
    String items_section_heading {}; // "Instruments", "Presets", etc.

    Span<ModalTabConfig const> tab_config {};
    u32* current_tab_index;

    Optional<Button> rhs_top_button {};
    TrivialFunctionRef<void(GuiBoxSystem&)> rhs_do_items {};
    bool show_search {true};

    TrivialFunctionRef<void()> on_load_previous {};
    TrivialFunctionRef<void()> on_load_next {};
    TrivialFunctionRef<void()> on_load_random {};
    TrivialFunctionRef<void()> on_scroll_to_show_selected {};

    Optional<LibraryFilters> library_filters {};
    Optional<TagsFilters> tags_filters {};
    Optional<FolderFilters> folder_filters {};
    TrivialFunctionRef<void(GuiBoxSystem&, Box const& parent, u8& num_sections)> do_extra_filters {};
    bool has_extra_filters {};

    f32 status_bar_height {};
    TrivialFunctionRef<Optional<String>()> status {}; // Set if something is hovering
};

Box DoPickerItemsRoot(GuiBoxSystem& box_system);

struct PickerItemsSectionOptions {
    Box parent;
    Optional<String> heading;
    Optional<String> icon;
    FolderNode const* folder;
    bool capitalise;
    bool multiline_contents;
    bool subsection;
};

Box DoPickerItemsSectionContainer(GuiBoxSystem& box_system, PickerItemsSectionOptions const& options);

struct PickerItemOptions {
    Box parent;
    String text;
    bool is_current;
    Array<graphics::TextureHandle, k_num_layers + 1> icons;
};

Box DoPickerItem(GuiBoxSystem& box_system, PickerItemOptions const& options);

struct FilterButtonOptions {
    enum class FontIconMode : u8 {
        NeverHasIcon,
        HasIcon,
        SometimesHasIcon,
    };

    using FontIcon = TaggedUnion<FontIconMode, TypeAndTag<String, FontIconMode::HasIcon>>;

    Box parent;
    bool is_selected;
    Optional<graphics::TextureHandle> icon;
    String text;
    String tooltip;
    DynamicArray<u64>& hashes;
    u64 clicked_hash;
    FilterMode filter_mode;
    FontIcon font_icon = FontIconMode::NeverHasIcon;
    u8 indent;
    bool full_width;
};

Box DoFilterButton(GuiBoxSystem& box_system,
                   CommonPickerState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options);

void DoPickerStatusBar(GuiBoxSystem& box_system,
                       PickerPopupContext& context,
                       FunctionRef<Optional<String>()> custom_status);

void DoPickerPopup(GuiBoxSystem& box_system,
                   PickerPopupContext context,
                   imgui::Id popup_id,
                   Rect absolute_button_rect,
                   PickerPopupOptions const& options);
