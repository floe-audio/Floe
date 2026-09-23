// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "utils/error_notifications.hpp"

#include "common_infrastructure/persistent_store.hpp"
#include "common_infrastructure/preferences.hpp"
#include "common_infrastructure/sample_library/server/sample_library_server.hpp"
#include "common_infrastructure/tags.hpp"

#include "gui/core/gui_library_images.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui_framework/gui_builder.hpp"

struct Notifications;

constexpr auto k_browser_item_height = 20.0f;
constexpr auto k_browser_spacing = 7.0f;

// Both browser panels are laid out like a menu: neither has a gutter, so full-width rows run edge to edge
// and each one insets its own contents by this much to keep icons and text off the panel edges.
constexpr auto k_browser_row_pad_x = 6.0f;

// The filters panel is a tree. A section (LIBRARIES, TAGS, ...) is a top-level branch; a collection or a
// tag category is a branch within one; folder rows and pills are the leaves. Every branch's heading is a
// row the height of any other row, so the whole tree shares one rhythm. A branch within a section pads
// its whole subtree by one indent, so carets step in with depth.
constexpr f32 k_tree_caret_width = 12; // A fixed slot, so carets and icons line up in a column.
// A branch's content (its icon and text) starts past its caret slot; pills beneath it start on that column.
constexpr f32 k_tree_content_inset = k_browser_row_pad_x + k_tree_caret_width + k_browser_row_pad_x;
// Subfolders hang off tree lines. A line drops from the parent folder's text, and each child row takes a
// tick from it that stops a row pad short of its own text, so one indent is a tick plus a row pad. The
// rows directly beneath a collection have no lines.
constexpr f32 k_tree_indent = 12;
constexpr f32 k_tree_line_width = 1;
// Room between the nodes within a section. Sections are twice this apart, so the bigger boundary always
// reads as the bigger one.
constexpr f32 k_tree_inner_gap = k_browser_spacing / 2;

constexpr auto k_untagged_tag_name = "<untagged>"_s;
constexpr u64 k_untagged_key = ToInt(TagType::Count);

enum class SearchDirection : u8 { Forward, Backward };

enum class LoopControl : u8 { Continue, Break };

enum class FilterMode : u8 {
    Single, // Only one filter can be selected at a time.
    MultipleAnd, // AKA "match all", AND
    MultipleOr, // AKA "match any", OR
    Count,
};

// How the filters panel is presented. Browse is a drill-down, one place at a time and FilterMode::Single
// underneath: every entry at the root is a row that opens a page, whether it holds collections (libraries,
// preset banks) or the values of a flat attribute (tags, authors, ...). Filter shows the full tree;
// whether one filter can be selected or several combined with AND or OR is a separate choice (FilterMode)
// within it.
enum class BrowserMode : u8 {
    Browse,
    Filter,
    Count,
};

struct BrowserPopupContext;
struct BrowserPopupOptions;

// How much room the filters panel has, derived from its width each frame rather than measured. Elements
// query it to decide what to show: Compact drops anything an icon or a tooltip can stand in for.
enum class BrowserWidthTier : u8 {
    Compact,
    Full,
};

inline BrowserWidthTier BrowserWidthTierForWidth(f32 filters_col_width) {
    return filters_col_width < 120 ? BrowserWidthTier::Compact : BrowserWidthTier::Full;
}

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

    // Calls f(String display_name, u64 key) for each selected item. The callback must return LoopControl;
    // returning Break stops iteration.
    template <FunctionWithSignature<LoopControl, String, u64> F>
    void ForEachSelected(F&& f) const {
        switch (data.tag) {
            case Type::Hashes:
                for (auto const& h : data.Get<HashesData>().items)
                    if (f((String)h.display_name, h.hash) == LoopControl::Break) return;
                break;
            case Type::Tags: {
                auto& tags = data.Get<TagsData>();
                bool stop = false;
                tags.bitset.ForEachSetBit([&](usize bit) {
                    if (stop) return;
                    if (f(GetTagInfo((TagType)bit).name, (u64)bit) == LoopControl::Break) stop = true;
                });
                if (stop) return;
                if (tags.selected_untagged) f(k_untagged_tag_name, k_untagged_key);
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
        bool select_selected {}; // The panel's selected item, else the first.
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
        u8 left_presses {};
        u8 right_presses {};
    };

    Panel focused_panel {Panel::Items};
    bool panel_just_focused {};
    PanelState panel_state {};
    u8 breadcrumb_cell {}; // Which of the breadcrumb's clickable cells left/right have landed on.

    Array<u64, ToInt(Panel::Count)> focused_items {};
    Array<u64, ToInt(Panel::Count)> temp_focused_items {};

    Input input {};
};

enum class BrowserFilter : u8 {
    Library,
    LibraryAuthor,
    Folder,
    Tags,
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

    // Like the search box, favourites is a refinement of the current results rather than a filter: it
    // always composes with the current selection (AND), in every mode. Keeping it out of the filters
    // array means ClearAll, ClearToOne, HasFilters and the selected-filters row all leave it alone.
    FilterSelection favourites = FilterSelection::Bool("Favourites"_s);

    // We track both states so we know how to handle default_collapsed requests.
    DynamicArray<u64> collapsed_filter_headers {Malloc::Instance()};
    DynamicArray<u64> expanded_filter_headers {Malloc::Instance()};

    DynamicArrayBounded<char, 100> search {};
    DynamicArrayBounded<char, 100> filter_search {};
    // Both synced from preferences each frame; filter_mode follows mode.
    BrowserMode mode = BrowserMode::Browse;
    FilterMode filter_mode = FilterMode::Single;

    // Browse mode navigation. The panel is at its root (a row per entry), inside a collection section
    // listing its collections, inside one of those collections (a library or preset bank, showing its
    // folders), or inside an attribute page listing all of one filter's values. A page is somewhere you
    // stay while trying its values, so unlike the collection level it can't be derived from the selection.
    struct BrowseLocation {
        Optional<u8> open_attribute {}; // Index into filters.
        Optional<u64> open_collection_section {}; // BrowseCollectionSection id.
        // The tags page is two levels: a list of categories, then one category's tags. Only meaningful
        // while the open attribute is the tags filter; a selected tag keeps its category open.
        Optional<TagCategory> open_tag_category {};
    };
    BrowseLocation browse {};
    bool browse_collection_open {}; // Recomputed each frame from the selection.

    // Browse mode's back and forward buttons. Back steps up one breadcrumb, pushing the level it left
    // onto this stack; forward pops it. There's no wider history: navigating anywhere else clears the
    // stack, detected by the navigation hash no longer matching the level back or forward last landed on.
    struct ForwardLevel {
        DynamicArrayBounded<FilterSelection, k_max_browser_filters> filters {};
        BrowseLocation location {};
    };
    DynamicArrayBounded<ForwardLevel, 4> browse_forward_levels {};
    u64 browse_navigation_hash {};

    // Browse mode's place (location and selection) is kept in the persistent store so a new instance opens
    // where the last one left off. Loaded once, the first time the browser is drawn; saved whenever the
    // navigation hash differs from the one last saved.
    bool browse_place_loaded_from_store {};
    u64 browse_place_saved_hash {};

    // Armed when clicking an item loads it. Once armed, moving the cursor off both the browser and the
    // element that opened it closes the browser: the click already did what the browser was opened for.
    bool close_when_cursor_leaves {};

    bool scroll_filters_to_start {};
    bool scroll_items_to_start {};
    bool scroll_to_show_current {}; // Pending request; see ScrollBrowserToShowCurrent.
    bool flash_current_when_shown {}; // Opening the browser just scrolls; the locate button also flashes.
    bool items_still_loading {}; // Set by the browser each frame: a scan is still adding items.
    RightClickMenuState right_click_menu_state {};
    BrowserKeyboardNavigation keyboard_navigation {};

    // The user's resize, in WW; unset means the browser's default. The corner grip sets the filters width
    // and the height the panels share; the border between the panels sets the results width. Clamped each
    // frame to what the window has room for, so a size from a bigger window is kept rather than lost.
    struct Size {
        Optional<f32> results_width {};
        Optional<f32> filters_col_width {};
        Optional<f32> height {};
    };
    Size size {};
    bool size_loaded_from_store {};
    struct ResizeDrag {
        f32x2 cursor_origin {};
        f32x2 size_at_origin {}; // WW: filters width, height.
        f32 results_width_at_origin {}; // WW.
    };
    ResizeDrag resize_drag {};
    BrowserWidthTier filters_width_tier = BrowserWidthTier::Full; // Derived each frame.
};

// Browse mode: how a place you can drill into presents itself, on the row that opens it and as the title
// of its page.
struct BrowseEntry {
    String name;
    String icon;
    u32 count; // Shown on the row; entries with nothing in them are left off the root.
    String tooltip;
};

// A flat filter attribute: tags, authors, preset types, and libraries where they aren't shown as
// collections. Browse mode's root lists them as rows below the collection sections; clicking one opens a
// page of just that attribute's values. Filter mode shows them all at once as sections of the tree.
// do_values runs later in the frame, inside the filters viewport, so it must only reference things that
// outlive the browser modal call.
struct BrowseAttribute {
    u8 filter_index;
    BrowseEntry entry;
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_values;
};

inline bool IsSingleFolderFilterSelected(CommonBrowserState const& state, u64 section_folder_hash) {
    auto const& folder_filter = state.Filter(BrowserFilter::Folder);
    if (folder_filter.data.tag != FilterSelection::Type::Hashes) return false;
    auto const& items = folder_filter.data.Get<FilterSelection::HashesData>().items;
    if (items.size != 1) return false;

    // The section folder must be the one selected; descendant sections (e.g. when the filter matches
    // ancestors) still need their heading to disambiguate.
    if (items[0].hash != section_folder_hash) return false;

    // In OR mode with other active filters, items may match those filters but live outside the folder,
    // so the folder heading is still informative.
    if (state.filter_mode == FilterMode::MultipleOr) {
        for (auto const [index, filter] : Enumerate(state.filters))
            if (index != (usize)BrowserFilter::Folder && filter.HasSelected()) return false;
    }
    return true;
}

// Whether an items-list folder section is collapsed. section_id is the BrowserSection id; folder_hash is
// the folder's own hash, which decides whether the heading is skipped entirely.
inline bool IsBrowserSectionCollapsed(CommonBrowserState const& state, u64 section_id, u64 folder_hash) {
    if (IsSingleFolderFilterSelected(state, folder_hash)) return false;
    return Contains(state.collapsed_filter_headers, section_id);
}

// Combines per-value matches according to the filter mode: AND requires all selected values to match,
// OR requires any. Single mode only has one selected value, so either policy works.
template <typename Predicate>
bool MatchesFilterValues(FilterSelection const& filter, FilterMode mode, Predicate&& matches_value) {
    // OR: start false, flip to true on the first match, then stop.
    // AND: start true, flip to false on the first non-match, then stop.
    bool const is_or = mode == FilterMode::MultipleOr;
    bool result = !is_or;
    filter.ForEachSelected([&](String name, u64 key) {
        bool const matched = matches_value(name, key);
        if (is_or && matched) {
            result = true;
            return LoopControl::Break;
        }
        if (!is_or && !matched) {
            result = false;
            return LoopControl::Break;
        }
        return LoopControl::Continue;
    });
    return result;
}

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

// Persistent-store format of Browse mode's place: the location and every selected filter value, with the
// display name each was selected under. The filters must already be initialised: values are matched to
// them by index, so the format is only meaningful for the browser type that wrote it.
Span<u8 const> EncodeBrowsePlace(CommonBrowserState const& state, ArenaAllocator& arena);
// False leaves the state untouched: the data is truncated, from another version, or names a filter, tag
// or category the state doesn't have.
bool DecodeBrowsePlace(Span<u8 const> data, CommonBrowserState& state);

inline void InitCommonFilters(CommonBrowserState& state) {
    dyn::Append(state.filters, FilterSelection::Hashes("Library"_s));
    dyn::Append(state.filters, FilterSelection::Hashes("Library Author"_s));
    dyn::Append(state.filters, FilterSelection::Hashes("Folder"_s));
    dyn::Append(state.filters, FilterSelection::Tags("Tag"_s));
}

// Ephemeral
struct BrowserPopupContext {
    u64 const& browser_id;
    sample_lib_server::Server& sample_library_server;
    LibraryImagesTable& library_images;
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

// A collection, a library or preset bank, named by the selection that opens it.
struct BrowserCollection {
    BrowserFilter filter; // The selection that names the collection.
    u64 key;
    String name;
    // The section listing the collection. Library collections are resolved without the browser's help.
    Optional<u64> section_id {};

    // Browse mode: what the open collection's title shows. Its icon, its count, and everything it says about
    // itself as the title's value popup.
    Optional<sample_lib::LibraryId> library_id {}; // For the icon.
    u32 num_items {};
    String subtext {};
    Optional<u32> version {}; // Appended to the subtext.
    String description {};
    RightClickMenuState::Function right_click_menu {};
};

// A library as a collection.
BrowserCollection LibraryCollection(ArenaAllocator& arena, sample_lib::Library const& lib, u32 num_items);

// Browse mode: where the drill-down currently is, so the results panel can say what it's listing and step
// back out of it.
struct BrowseScope {
    String collection_noun {}; // What a collection is in this browser: "library", "preset bank".
    Optional<BrowserCollection> collection {}; // The collection drilled into; none at the root.
    String folder_name {}; // A folder inside the collection; empty when the whole collection is shown.
};

// The folder a collection shows: a library's root folder, or a preset bank's own folder.
FolderNode const* CollectionRootFolder(FolderNode const* folder);

struct BrowseScopeResolver {
    String collection_noun; // What a collection is in this browser: "library", "preset bank".
    // folders must hold every folder the browser lists, so the selected one can be traced back to its
    // collection.
    FolderFilterItemInfoLookupTable const& folders;
    // The collection whose own folder is given.
    TrivialFunctionRef<Optional<BrowserCollection>(FolderNode const&)> collection_of_root;
    // The collection a selected library is. Null, or returning nothing, names it by its selection alone:
    // enough for a browser where libraries are attributes rather than collections.
    TrivialFunctionRef<Optional<BrowserCollection>(sample_lib::LibraryId)> collection_of_library {};
};

BrowseScope CurrentBrowseScope(CommonBrowserState const& state, BrowseScopeResolver const& resolver);

struct FilterButtonCommonOptions {
    Box parent;
    u64 id_extra;
    bool is_selected;
    String text;
    TooltipString value_popup = k_nullopt;
    TooltipString tooltip = k_nullopt; // Overrides the tooltip built from match_phrase.
    // Completes "Show only the items ...", e.g. "with this tag", "in this folder". The tooltip is built from
    // it, the mode and the selected state; leave it empty for no tooltip.
    String match_phrase {};
    // Browse mode: what clicking the selected value shows, completing "Click again to show ...". Defaults
    // to "everything".
    String deselect_shows {};
    FilterSelection& filter;
    u64 clicked_key;
    FilterMode filter_mode;
};

// Where a collection's icon comes from: its library's icon, or a placeholder when it has no library.
struct CollectionIconSource {
    Optional<sample_lib::LibraryId> library_id;
    LibraryImagesTable& library_images;
    sample_lib_server::Server& sample_library_server;
    FloeInstanceIndex instance_index;
};

struct FilterCollectionOptions {
    FilterButtonCommonOptions common;
    CollectionIconSource icon;
    FolderFilterItemInfoLookupTable folder_infos;
    FolderNode const* folder;
    String all_items_suffix {}; // Appended after "All <name>" in the tree, e.g. " Instruments".
    String collection_noun {}; // What the collection is: "library", "preset bank".
    bool default_collapsed {};
    RightClickMenuState::Function right_click_menu {};
    persistent_store::Store* store {};
    String name {};
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
    bool collection_view {};
    sample_lib::ResourceType resource_type {};
    FolderFilterItemInfoLookupTable folders;
    FilterCollectionOptions const* additional_pseudo_collection {};
    FilterItemInfo const* additional_pseudo_collection_info {};
    ThreadsafeErrorNotifications& error_notifications;
    Notifications& notifications;
    ConfirmationDialogState& confirmation_dialog_state;
    String collection_name_prefix {};
};

// Browse mode: a group of collections. The root lists it as a row, and opening that row gives the section
// the whole panel. do_collections runs later in the frame, inside the filters viewport, so it must only
// reference things that outlive the browser modal call.
struct BrowseCollectionSection {
    u64 id;
    BrowseEntry entry;
    RightClickMenuState::Function right_click_menu {};
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_collections;
};

constexpr usize k_max_browse_collection_sections = 4;

// Whether the browser's current item (the loaded instrument, preset, etc.) appears in the items list. The
// header shows it as a chip: clicking the chip scrolls to the item (clearing whatever hides it first), and
// its tooltip explains the situation when it can't be shown.
struct CurrentItemStatus {
    enum class Visibility : u8 {
        None, // Nothing is loaded.
        Loading, // Not found yet, but a scan is in progress so it may appear shortly.
        Shown,
        InCollapsedSection,
        HiddenByFilters, // Excluded by the filters or search.
        NotInList, // Not in the browser's data at all, e.g. its library isn't installed.
    };
    Visibility visibility {Visibility::None};
    String name {};
    u64 section_id {}; // Folder section containing the item, so it can be expanded.
    String not_in_list_reason {}; // Completes "isn't listed because ...".

    // The collection the item belongs to. The locate button also shows it in the right panel: Browse mode
    // opens it, Filter mode expands it.
    Optional<BrowserCollection> collection {};
};

// IMPORTANT: we use FunctionRef here, you need to make sure the lifetime of the functions outlives the
// options.
struct BrowserPopupOptions {
    // WW. The height is what the window has room for, so it's the maximum as well as the default. The
    // panel widths are defaults only: the user can resize either panel either way.
    f32 height {};
    f32 results_width {};
    f32 filters_col_width {};
    // Persistent-store key for the user's resize and their Browse mode location. One per browser type, so
    // every place that opens the same browser shares it.
    u64 store_id {};

    // The browser sits flush against the element that opened it (absolute_button_rect), below or above,
    // the two drawn as one shape. The opener is run with DoBrowserOpenerViewport so it floats above the
    // modal's dim and stays interactable, and so it carries the title and navigation controls rather than
    // the browser.
    bool flush_with_opener {};

    String item_type_name {}; // "instrument", "preset", etc.
    String plural_item_type_name {}; // "instruments", "presets", etc.

    TrivialFunctionRef<void(GuiBuilder&)> do_items {};
    bool show_search {true};
    String filter_search_placeholder_text {"Search filters..."};
    String item_search_placeholder_text {"Search"};
    String item_search_tooltip {"Search the current results by name. " MODIFIER_KEY_NAME
                                "+F jumps here from anywhere in the browser."};

    CurrentItemStatus current_item {};

    BrowseScope browse_scope {};

    Optional<LibraryFilters> library_filters {};
    Optional<TagsFilters> tags_filters {};
    // Browse mode: attributes this browser has beyond the common ones, listed after them.
    Span<BrowseAttribute const> extra_browse_attributes {};
    // Browse mode's root lists these as rows. Libraries in collection view are added to the list
    // automatically.
    Span<BrowseCollectionSection const> browse_collection_sections {};
    // Filter mode: the browser's own sections of the tree, above and below the common ones. The
    // collections go at the top.
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_extra_filters_top {};
    TrivialFunctionRef<void(GuiBuilder&, Box const& parent)> do_extra_filters_bottom {};
    FilterItemInfo const& favourites_filter_info;
    u32 num_results {}; // Items the list will show, for the summary line.

    void* right_click_menu_user_data {};
};

// Corners to round on an element that a browser opens flush from (BrowserPopupOptions::flush_with_opener):
// those touching the open browser are square so the two join as one shape. All of them when it's closed.
Corners BrowserOpenerCornersToRound(imgui::Context const& imgui, imgui::Id browser_id, Rect opener_rect);

// Runs the element that opens a browser in its own viewport so that, while the browser is open, it floats
// above the modal's dim, stays interactable, and gets an opaque surface to join the browser. See
// BrowserPopupOptions::flush_with_opener. The bounds must have a fixed size: contents in another viewport
// can't be hugged. The run function is deferred, so capture only what outlives the current viewport's run.
struct BrowserOpenerViewportOptions {
    imgui::Id browser_id;
    imgui::Id viewport_id;
    Box bounds;
    BoxViewportConfig::RunFunction run;
    String debug_name {};
};
void DoBrowserOpenerViewport(GuiBuilder& builder, BrowserOpenerViewportOptions const& options);

Box DoBrowserItemsRoot(GuiBuilder& builder);

void DoBrowserEmptyListMessage(GuiBuilder& builder,
                               CommonBrowserState& state,
                               Box root,
                               String plural_item_type_name);

// Fulfils state.scroll_to_show_current by scrolling the items viewport so that box is visible. Call from
// the items list with the current item's box, or its section heading if the section is collapsed.
void ScrollBrowserToShowCurrent(GuiBuilder& builder, CommonBrowserState& state, Box const& box);

static constexpr u64 k_browser_collapse_store_id = HashFnv1a("browser-section-collapse-state");

// The hashed string is persisted in users' store files, so it keeps its original spelling.
inline u64 CollectionCollapseId(u64 collection_key) { return collection_key ^ HashFnv1a("card-collapse"); }

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

// A collapsible branch of a browser tree: a folder heading in the items list, or a section or tag category in
// the filters panel. The heading is the branch's own row; whatever the caller adds to the returned box hangs
// beneath it.
struct BrowserSection {
    enum class State : u8 {
        Collapsed,
        Box,
    };

    using Result = TaggedUnion<State, TypeAndTag<Box, State::Box>>;

    // Creates the branch on the first call, so a branch that's never asked for never appears. Once
    // collapsed, every call returns Collapsed.
    Result Do(GuiBuilder& builder);

    CommonBrowserState& state;
    u64 id;
    ::Box parent;
    Optional<String> heading;
    Optional<String> icon;
    FolderNode const* folder;
    bool capitalise;
    bool multiline_contents;
    bool subsection; // A branch within a section: indented, and stood off from the node above it.
    bool default_collapsed {false};
    bool skip_root_folder {};
    bool skip_heading {};
    bool dark_mode {};
    bool keyboard_focusable {};
    TooltipPlacement tooltip_placement {TooltipPlacement::LeftThenRight};
    RightClickMenuState::Function right_click_menu {};
    persistent_store::Store* store {};

    // Don't set these, they are set internally.
    Box box_cache {};
    Box heading_box {};
    u8 init : 1 = 0;
    u8 is_collapsed : 1 = 0;
    u8 is_box_init : 1 = 0;
};

enum class ItemIconType : u8 { None, Image, Font };
using ItemIcon = TaggedUnion<ItemIconType,
                             TypeAndTag<String, ItemIconType::Font>,
                             TypeAndTag<ImageID, ItemIconType::Image>>;

struct BrowserItemOptions {
    Box parent;
    u64 id_extra {};
    String text;
    TooltipString value_popup = k_nullopt;
    TooltipString tooltip = k_nullopt;
    String tooltip_footer {};
    u64 item_id;
    bool is_current;
    bool is_favourite;
    bool is_default; // Shown with a marker; only presets use this.
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

// The tooltip every item row shares. item_type_name as it should read mid-sentence: "Instrument", "preset".
inline String BrowserItemLoadTooltip(ArenaAllocator& arena, String item_type_name) {
    return fmt::Format(arena,
                       "Click to load this {}. The browser stays open until you move the mouse away from it, "
                       "so you can try a few in a row. Double-click to load and close straight away.",
                       item_type_name);
}

struct FilterButtonOptions {
    FilterButtonCommonOptions common;
    ImageID const* icon;
};

// Browse mode: a drilled-into collection has no "All" row, so deselecting a folder selects the collection
// instead, keeping the drill-down open with the whole collection shown.
struct DeselectFallback {
    FilterSelection* filter {};
    u64 key {};
    String display_name {};
};

// A folder row's gutter: one level of line per ancestor above the row's own tick. Bit k of continues says
// the level-k line carries on below this row, so the row's tick (level depth) is an elbow when its bit is
// clear. Lines from gold_from down are lit: they lead into a selected node's subtree. An unlined row has
// no ticks or lines; its text sits a row pad past the inset.
struct TreeLines {
    static constexpr u8 k_no_gold = 255;
    f32 inset = 0; // x of the level-0 line, or of an unlined row's padding.
    u8 depth = 0;
    u8 gold_from = k_no_gold;
    u8 continues = 0;
    bool lined = false;
};

struct FilterTreeButtonOptions {
    FilterButtonCommonOptions common;
    TreeLines lines;
    ImageID const* icon {};
    Optional<FontType> font_override {};
    String display_text {}; // If set, rendered instead of common.text (common.text is still used for hashes).
    DeselectFallback deselect_fallback {};
};

Box DoFilterButton(GuiBuilder& builder,
                   CommonBrowserState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options);

Box DoFilterTreeButton(GuiBuilder& builder,
                       CommonBrowserState& state,
                       FilterItemInfo const& info,
                       FilterTreeButtonOptions const& options);

void DoFilterCollection(GuiBuilder& builder,
                        CommonBrowserState& state,
                        FilterItemInfo const& info,
                        FilterCollectionOptions const& options);

void DoBrowserModal(GuiBuilder& builder, BrowserPopupContext context, BrowserPopupOptions const& options);

void DoRightClickMenuForBox(GuiBuilder& builder,
                            CommonBrowserState& state,
                            Box const& box,
                            u64 item_hash,
                            RightClickMenuState::Function const& do_menu);

bool MatchesFilterSearch(String filter_text, String search_text);

// The box a section's values go in, or nothing while it's collapsed. The section creates itself on the
// first call, so a section whose values are all filtered out never appears.
Optional<Box> SectionContents(GuiBuilder& builder, BrowserSection& section);
