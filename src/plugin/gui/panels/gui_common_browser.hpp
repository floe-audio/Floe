// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/error_notifications.hpp"

#include "common_infrastructure/persistent_store.hpp"
#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/tags.hpp"

#include "gui/core/gui_library_images.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui_framework/gui_builder.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct Notifications;

constexpr auto k_browser_item_height = 20.0f;
constexpr auto k_browser_spacing = 7.0f;

constexpr auto k_untagged_tag_name = "<untagged>"_s;

enum class SearchDirection { Forward, Backward };

enum class FilterMode : u8 {
    Single, // Only one filter can be selected at a time.
    MultipleAnd, // AKA "match all", AND
    MultipleOr, // AKA "match any", OR
    Count,
};

struct BrowserPopupContext;
struct BrowserPopupOptions;

struct RightClickMenuState {
    // Use context.state and options.right_click_menu_user_data to get your own data.
    using Function = void (*)(GuiBuilder&, BrowserPopupContext&, BrowserPopupOptions const&);
    Function do_menu {};
    Rect absolute_creator_rect {}; // Absolute rectangle of the item that opened the menu.
    u64 item_hash {}; // The hash of the item that opened the menu.
};

struct FilterSelection {
    using DisplayName = DynamicArrayBounded<char, 24>;

    struct SelectedHash {
        u64 hash {};
        DisplayName display_name {};
    };

    struct HashesData {
        DynamicArrayBounded<SelectedHash, 16> items {};
    };

    struct TagsData {
        TagsBitset bitset {};
        bool selected_untagged {};
    };

    enum class Type : u8 { Hashes, Tags, Bool };

    using Union = TaggedUnion<Type,
                              TypeAndTag<bool, Type::Bool>,
                              TypeAndTag<TagsData, Type::Tags>,
                              TypeAndTag<HashesData, Type::Hashes>>;

    bool HasSelected() const;
    bool Contains(u64 key) const;
    void Add(u64 key, String display_name);
    void Remove(u64 key);
    void Toggle(u64 key, String display_name);
    void Clear();
    void ClearToOne();

    // Calls f(String display_name, u64 key) for each selected item.
    template <typename F>
    void ForEachSelected(F&& f) const {
        switch (data.tag) {
            case Type::Hashes:
                for (auto const& h : data.Get<HashesData>().items)
                    f((String)h.display_name, h.hash);
                break;
            case Type::Tags: {
                auto& tags = data.Get<TagsData>();
                tags.bitset.ForEachSetBit([&](usize bit) { f(GetTagInfo((TagType)bit).name, (u64)bit); });
                if (tags.selected_untagged) f(k_untagged_tag_name, HashFnv1a("untagged"));
                break;
            }
            case Type::Bool:
                if (data.Get<bool>()) f(String {}, 1);
                break;
        }
    }

    static FilterSelection Hashes(String name) { return {.name = name, .data = HashesData {}}; }
    static FilterSelection Tags(String name) { return {.name = name, .data = TagsData {}}; }
    static FilterSelection Bool(String name) { return {.name = name, .data = false}; }

    String name;
    Union data;
};

struct BrowserKeyboardNavigation {
    enum class Panel : u8 {
        None,
        Filters,
        Items,
        Count,
    };

    struct ItemHistory {
        static constexpr usize k_max_items = 8;

        void Push(u64 item) { items[Mask(write++)] = item; }

        // 1 means previous item, 2 means 2 items ago, etc.
        u64 AtPrevious(u32 history_depth) {
            ASSERT(history_depth > 0 && history_depth <= items.size);
            return items[Mask(write - history_depth)];
        }

        u64 AtPreviousOrBarrier(u32 history_depth) {
            if ((write - barrier) == 0) return {};
            if (history_depth > write - barrier) return items[Mask(barrier)];
            return items[Mask(write - history_depth)];
        }

        void SetBarrier() { barrier = write; }

        constexpr u32 Mask(u32 val) const {
            static_assert(IsPowerOfTwo(k_max_items));
            return val & (items.size - 1);
        }

        Array<u64, k_max_items> items {}; // Ring buffer.
        u32 write {}; // Unbounded.
        u32 barrier {};
    };

    struct PanelState {
        ItemHistory item_history {};
        u64 previous_tab_item {};
        u64 id_to_select {};
        bool select_next_tab_item {}; // Doesn't wrap around.
        bool select_next {}; // Wraps around.
        u8 select_next_at {}; // Doesn't wrap around.
    };

    struct Input {
        constexpr bool operator==(Input const& other) const = default;
        u8 down_presses {};
        u8 up_presses {};
        u8 page_down_presses {};
        u8 page_up_presses {};
        u8 next_section_presses {};
        u8 previous_section_presses {};
    };

    Panel focused_panel {Panel::Items};
    bool panel_just_focused {};
    PanelState panel_state {};

    Array<u64, ToInt(Panel::Count)> focused_items {};
    Array<u64, ToInt(Panel::Count)> temp_focused_items {};

    Input input {};
};

enum class BrowserFilter : usize {
    Library,
    LibraryAuthor,
    Folder,
    Tags,
    Favourites,
    CommonCount,
};

// We actually allow more than CommonCount filters to be tracked. If a browser needs more than the standard
// set, it can append them, starting with index ToInt(CommonCount). This way they are all stored in the same
// array and the common state can access them to implement the common behaviour.
static constexpr usize k_max_browser_filters = 8;
static_assert(k_max_browser_filters >= ToInt(BrowserFilter::CommonCount));

struct CommonBrowserState {
    bool HasFilters() const {
        for (auto const& f : filters)
            if (f.HasSelected()) return true;
        return false;
    }

    void ClearAll() {
        for (auto& f : filters)
            f.Clear();
    }

    void ClearToOne() {
        bool found_one = false;
        for (auto& f : filters) {
            if (f.HasSelected()) {
                if (found_one)
                    f.Clear();
                else {
                    found_one = true;
                    f.ClearToOne();
                }
            }
        }
    }

    FilterSelection& Filter(BrowserFilter i) { return filters[(usize)i]; }
    FilterSelection const& Filter(BrowserFilter i) const { return filters[(usize)i]; }

    // Allow browser-specific filter index enums that extend FilterIndex.
    template <Enum EnumT>
    requires(!Same<EnumT, BrowserFilter>)
    FilterSelection& Filter(EnumT i) {
        return filters[(usize)i];
    }
    template <Enum EnumT>
    requires(!Same<EnumT, BrowserFilter>)
    FilterSelection const& Filter(EnumT i) const {
        return filters[(usize)i];
    }

    Rect absolute_button_rect {}; // Absolute rectangle of the button that opened the browser.
    DynamicArrayBounded<FilterSelection, k_max_browser_filters> filters {};

    // We track both states so we know how to handle default_collapsed requests.
    DynamicArray<u64> collapsed_filter_headers {Malloc::Instance()};
    DynamicArray<u64> expanded_filter_headers {Malloc::Instance()};

    DynamicArrayBounded<char, 100> search {};
    DynamicArrayBounded<char, 100> filter_search {};
    FilterMode filter_mode = FilterMode::Single;
    RightClickMenuState right_click_menu_state {};
    BrowserKeyboardNavigation keyboard_navigation {};
};

// Returns true if the item should be hidden. matches_filter(index, filter) should return true if the item
// matches that filter. AND mode: skip if any active filter doesn't match. OR mode: skip if no active filter
// matches.
bool IsFilteredOut(CommonBrowserState const& state, auto&& matches_filter) {
    bool filtering_on = false;
    for (auto const [index, filter] : Enumerate(state.filters)) {
        if (!filter.HasSelected()) continue;
        filtering_on = true;

        bool const matched = matches_filter(index, filter);

        switch (state.filter_mode) {
            case FilterMode::Single:
            case FilterMode::MultipleAnd:
                if (!matched) return true;
                break;
            case FilterMode::MultipleOr:
                if (matched) return false;
                break;
            case FilterMode::Count: PanicIfReached();
        }
    }
    return filtering_on && state.filter_mode == FilterMode::MultipleOr;
}

// Unlike other filters where an item has a single value (e.g. one library), items can have multiple tags
// and the user can select multiple tags. This function resolves the inner AND/OR logic within the Tags
// filter into a single bool for IsFilteredOut.
bool ItemMatchesTagFilter(FilterSelection const& filter, TagsBitset const& item_tags, FilterMode mode);

inline void InitCommonFilters(CommonBrowserState& state) {
    dyn::Append(state.filters, FilterSelection::Hashes("Library"_s));
    dyn::Append(state.filters, FilterSelection::Hashes("Library Author"_s));
    dyn::Append(state.filters, FilterSelection::Hashes("Folder"_s));
    dyn::Append(state.filters, FilterSelection::Tags("Tag"_s));
    dyn::Append(state.filters, FilterSelection::Bool("Favourites"_s));
}

// Ephemeral
struct BrowserPopupContext {
    u64 const& browser_id;
    sample_lib_server::Server& sample_library_server;
    prefs::Preferences& preferences;
    persistent_store::Store& store;
    CommonBrowserState& state;
    FloeInstanceIndex instance_index;
};

struct FilterItemInfo {
    u32 num_used_in_items_lists {};
    u32 total_available {};
};

struct TagsFilters {
    Array<FilterItemInfo, ToInt(TagType::Count)> tags {};
    TagsBitset available_tags {};
    FilterItemInfo untagged_info {};
    bool has_untagged {};
};

bool RootNodeLessThan(FolderNode const* const& a,
                      DummyValueType const&,
                      FolderNode const* const& b,
                      DummyValueType const&);

using FolderRootSet = OrderedSet<FolderNode const*, nullptr, RootNodeLessThan>;
using FolderFilterItemInfoLookupTable = HashTable<FolderNode const*, FilterItemInfo>;

struct FilterButtonCommonOptions {
    Box parent;
    u64 id_extra;
    bool is_selected;
    String text;
    TooltipString tooltip = k_nullopt;
    FilterSelection& filter;
    u64 clicked_key;
    FilterMode filter_mode;
};

struct FilterCardOptions {
    FilterButtonCommonOptions common;
    Optional<sample_lib::LibraryId> library_id; // For images.
    LibraryImagesTable& library_images;
    sample_lib_server::Server& sample_library_server;
    FloeInstanceIndex instance_index;
    String subtext;
    FolderFilterItemInfoLookupTable folder_infos;
    FolderNode const* folder;
    String all_items_suffix {}; // Appended after "All <name>" in the tree, e.g. " Instruments".
    bool default_collapsed {};
    RightClickMenuState::Function right_click_menu {};
    persistent_store::Store* store {};
};

bool LibraryIdLessThanFilterInfo(sample_lib::LibraryId const& a,
                                 FilterItemInfo const&,
                                 sample_lib::LibraryId const& b,
                                 FilterItemInfo const&);

struct LibraryFilters {
    sample_lib_server::LibrariesTable const& libraries_table;
    LibraryImagesTable& library_images;
    FloeInstanceIndex instance_index;
    OrderedHashTable<sample_lib::LibraryId, FilterItemInfo, NoHash, LibraryIdLessThanFilterInfo> libraries;
    OrderedHashTable<String, FilterItemInfo> library_authors;
    bool card_view {};
    sample_lib::ResourceType resource_type {};
    FolderFilterItemInfoLookupTable folders;
    RightClickMenuState::Function folder_do_right_click_menu = {};
    FilterCardOptions const* additional_pseudo_card {};
    FilterItemInfo const* additional_pseudo_card_info {};
    ThreadsafeErrorNotifications& error_notifications;
    Notifications& notifications;
    ConfirmationDialogState& confirmation_dialog_state;
};

// IMPORTANT: we use FunctionRef here, you need to make sure the lifetime of the functions outlives the
// options.
struct BrowserPopupOptions {
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

    Optional<Button> rhs_top_button {};
    TrivialFunctionRef<void(GuiBuilder&)> rhs_do_items {};
    bool show_search {true};
    String filter_search_placeholder_text {"Search filters..."};
    String item_search_placeholder_text {"Search"};

    TrivialFunctionRef<void()> on_load_previous {};
    TrivialFunctionRef<void()> on_load_next {};
    TrivialFunctionRef<void()> on_load_random {};
    TrivialFunctionRef<void()> on_scroll_to_show_selected {};

    Optional<LibraryFilters> library_filters {};
    Optional<TagsFilters> tags_filters {};
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_extra_filters_top {};
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_extra_filters_bottom {};
    bool has_extra_filters {};
    FilterItemInfo const& favourites_filter_info;

    void* right_click_menu_user_data {};
};

Box DoBrowserItemsRoot(GuiBuilder& builder);

struct BrowserItemsSectionOptions {};

static constexpr u64 k_browser_collapse_store_id = HashFnv1a("browser-section-collapse-state");

// Load a persisted section-collapse override into the in-memory toggled_ids array.
inline void
LoadCollapseStateFromStore(persistent_store::Store& store, DynamicArray<u64>& toggled_ids, u64 section_id) {
    if (persistent_store::GetFlag(store, k_browser_collapse_store_id ^ section_id)) {
        if (!Contains(toggled_ids, section_id)) dyn::Append(toggled_ids, section_id);
    }
}

// Persist the current section-collapse state after a toggle.
inline void SaveCollapseStateToStore(persistent_store::Store& store,
                                     DynamicArray<u64> const& toggled_ids,
                                     u64 section_id) {
    persistent_store::SetFlag(store,
                              k_browser_collapse_store_id ^ section_id,
                              Contains(toggled_ids, section_id));
}

struct BrowserSection {
    enum class State : u8 {
        Collapsed,
        Box,
    };

    using Result = TaggedUnion<State, TypeAndTag<Box, State::Box>>;

    // First time this is called, it either returns Collapsed or NormalBoxUninitialised. If
    // NormalBoxUninitialised, any subsequent calls will return a valid Box.
    Result Do(GuiBuilder& builder);

    CommonBrowserState& state;
    u64 id;
    ::Box parent;
    Optional<String> heading;
    Optional<String> icon;
    FolderNode const* folder;
    bool capitalise;
    bool multiline_contents;
    bool subsection;
    bool bigger_contents_gap {false};
    bool default_collapsed {false};
    bool skip_root_folder {};
    bool dark_mode {};
    RightClickMenuState::Function right_click_menu {};
    persistent_store::Store* store {};

    // Don't set these, they are set internally.
    Box box_cache {};
    u8 init : 1 = 0;
    u8 is_collapsed : 1 = 0;
    u8 is_box_init : 1 = 0;
};

enum class ItemIconType { None, Image, Font };
using ItemIcon = TaggedUnion<ItemIconType,
                             TypeAndTag<String, ItemIconType::Font>,
                             TypeAndTag<ImageID, ItemIconType::Image>>;

struct BrowserItemOptions {
    Box parent;
    u64 id_extra {};
    String text;
    TooltipString tooltip = k_nullopt;
    u64 item_id;
    bool is_current;
    bool is_favourite;
    bool is_tab_item; // Is a point where pressing Tab jumps to.
    DynamicArrayBounded<ItemIcon, k_num_layers + 2> icons;
    Notifications& notifications;
    persistent_store::Store& store;
};

struct BrowserItemResult {
    Box box;
    bool favourite_toggled;
    bool fired; // Either clicked or navigated to with the keyboard.
};

BrowserItemResult
DoBrowserItem(GuiBuilder& builder, CommonBrowserState& state, BrowserItemOptions const& options);

struct FilterButtonOptions {
    FilterButtonCommonOptions common;
    ImageID const* icon;
    bool no_bottom_margin;
    bool dark_mode = true;
    RightClickMenuState::Function right_click_menu {nullptr};
};

struct FilterTreeButtonOptions {
    FilterButtonCommonOptions common;
    bool is_active;
    u8 indent;
    Optional<FontType> font_override {};
    String display_text {}; // If set, rendered instead of common.text (common.text is still used for hashes).
};

Box DoFilterButton(GuiBuilder& builder,
                   CommonBrowserState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options);

Box DoFilterTreeButton(GuiBuilder& builder,
                       CommonBrowserState& state,
                       FilterItemInfo const& info,
                       FilterTreeButtonOptions const& options);

Box DoFilterCard(GuiBuilder& builder,
                 CommonBrowserState& state,
                 FilterItemInfo const& info,
                 FilterCardOptions const& options);

void DoBrowserModal(GuiBuilder& builder, BrowserPopupContext context, BrowserPopupOptions const& options);

void DoRightClickMenuForBox(GuiBuilder& builder,
                            CommonBrowserState& state,
                            Box const& box,
                            u64 item_hash,
                            RightClickMenuState::Function const& do_menu);

bool MatchesFilterSearch(String filter_text, String search_text);
