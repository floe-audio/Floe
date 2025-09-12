// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common_infrastructure/persistent_store.hpp"

#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_library_images.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct Notifications;

constexpr auto k_picker_item_height = 20.0f;
constexpr auto k_picker_spacing = 8.0f;

constexpr auto k_untagged_tag_name = "<untagged>"_s;

enum class SearchDirection { Forward, Backward };

enum class FilterMode : u8 {
    ProgressiveNarrowing, // AKA "match all", AND
    AdditiveSelection, // AKA "match any", OR
    Count,
};

struct RightClickMenuState {
    using Function = TrivialFixedSizeFunction<32, void(GuiBoxSystem&, RightClickMenuState const&)>;
    Function do_menu {};
    Rect absolute_creator_rect {}; // Absolute rectangle of the item that opened the menu.
    u64 item_hash {}; // The hash of the item that opened the menu.
};

struct SelectedHashes {
    using DisplayName = DynamicArrayBounded<char, 24>;

    struct SelectedHash {
        u64 hash {};
        DisplayName display_name {};
    };

    void Clear() { dyn::Clear(hashes); }
    bool Contains(u64 hash) const {
        for (auto const& h : hashes)
            if (h.hash == hash) return true;
        return false;
    }
    void Remove(u64 hash) {
        dyn::RemoveValueIfSwapLast(hashes, [hash](SelectedHash const& h) { return h.hash == hash; });
    }
    void Add(u64 hash, String display_name) {
        if (hashes.size >= hashes.Capacity()) return;
        dyn::Append(
            hashes,
            {
                .hash = hash,
                .display_name = ({
                    DisplayName n;
                    if (display_name.size > DisplayName::Capacity()) {
                        constexpr auto k_ellipsis = "…"_s;
                        display_name = display_name.SubSpan(
                            0,
                            FindUtf8TruncationPoint(display_name, DisplayName::Capacity() - k_ellipsis.size));
                        n = display_name;
                        dyn::AppendSpan(n, k_ellipsis);
                    } else {
                        n = display_name;
                    }
                    n;
                }),
            });
    }
    auto HasSelected() const { return hashes.size; }

    auto begin() { return hashes.begin(); }
    auto end() { return hashes.end(); }
    auto begin() const { return hashes.begin(); }
    auto end() const { return hashes.end(); }

    String name;
    DynamicArrayBounded<SelectedHash, 16> hashes {};
};

struct CommonPickerState {
    bool HasFilters() const {
        if (selected_library_hashes.HasSelected() || selected_library_author_hashes.HasSelected() ||
            selected_tags_hashes.HasSelected() || selected_folder_hashes.HasSelected())
            return true;
        for (auto const& hashes : other_selected_hashes)
            if (hashes->hashes.size) return true;
        return false;
    }

    void ClearAll() {
        selected_library_hashes.Clear();
        selected_library_author_hashes.Clear();
        selected_tags_hashes.Clear();
        selected_folder_hashes.Clear();
        for (auto other_hashes : other_selected_hashes)
            other_hashes->Clear();
    }

    bool open {};
    Rect absolute_button_rect {}; // Absolute rectangle of the button that opened the picker.
    SelectedHashes selected_library_hashes {"Library"};
    SelectedHashes selected_library_author_hashes {"Library Author"};
    SelectedHashes selected_tags_hashes {"Tag"};
    SelectedHashes selected_folder_hashes {"Folder"};
    DynamicArrayBounded<u64, 16> hidden_filter_headers {};
    DynamicArrayBounded<char, 100> search {};
    DynamicArrayBounded<SelectedHashes*, 2> other_selected_hashes {};
    FilterMode filter_mode = FilterMode::ProgressiveNarrowing;
    RightClickMenuState right_click_menu_state {};
};

// Ephemeral
struct PickerPopupContext {
    sample_lib_server::Server& sample_library_server;
    CommonPickerState& state;
    imgui::Id picker_id;
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
    RightClickMenuState::Function do_right_click_menu = {};
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
        bool disabled {};
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
};

Box DoPickerItemsRoot(GuiBoxSystem& box_system, CommonPickerState& state, bool filters_visible);

struct PickerItemsSectionOptions {
    Box parent;
    Optional<String> heading;
    Optional<String> icon;
    FolderNode const* folder;
    bool capitalise;
    bool multiline_contents;
    bool subsection;
    bool bigger_contents_gap {false};
    RightClickMenuState::Function right_click_menu {};
};

Optional<Box> DoPickerSectionContainer(GuiBoxSystem& box_system,
                                       u64 id,
                                       CommonPickerState& state,
                                       PickerItemsSectionOptions const& options);

struct PickerItemOptions {
    Box parent;
    String text;
    TooltipString tooltip = k_nullopt;
    bool is_current;
    Array<graphics::TextureHandle, k_num_layers + 1> icons;
    Notifications& notifications;
    persistent_store::Store& store;
};

Box DoPickerItem(GuiBoxSystem& box_system, CommonPickerState& state, PickerItemOptions const& options);

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
    TooltipString tooltip = k_nullopt;
    SelectedHashes& hashes;
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

void DoPickerPopup(GuiBoxSystem& box_system, PickerPopupContext context, PickerPopupOptions const& options);

void DoRightClickForBox(GuiBoxSystem& box_system,
                        CommonPickerState& state,
                        Box const& box,
                        u64 item_hash,
                        RightClickMenuState::Function const& do_menu);
