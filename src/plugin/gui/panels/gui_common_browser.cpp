// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_common_browser.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

#include "common_infrastructure/tags.hpp"

#include "gui/core/gui_actions.hpp"
#include "gui/core/gui_screenshot.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/overlays/gui_tips.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "preset_server/preset_server.hpp"

bool LibraryIdLessThanFilterInfo(sample_lib::LibraryId const& a,
                                 FilterItemInfo const&,
                                 sample_lib::LibraryId const& b,
                                 FilterItemInfo const&) {
    return sample_lib::LibraryIdLessThan(a, b);
}

bool FilterSelection::HasSelected() const {
    switch (data.tag) {
        case Type::Hashes: return data.Get<HashesData>().items.size;
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            return tags.bitset.AnyValuesSet() || tags.selected_untagged;
        }
        case Type::Bool: return data.Get<bool>();
    }
    return false;
}

bool FilterSelection::Contains(u64 key) const {
    switch (data.tag) {
        case Type::Hashes:
            for (auto const& h : data.Get<HashesData>().items)
                if (h.hash == key) return true;
            return false;
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            if (key == k_untagged_key) return tags.selected_untagged;
            return tags.bitset.Get(key);
        }
        case Type::Bool: return data.Get<bool>();
    }
    return false;
}

void FilterSelection::Add(u64 key, String display_name) {
    switch (data.tag) {
        case Type::Hashes: {
            auto& hashes = data.Get<HashesData>();
            if (hashes.items.size >= hashes.items.Capacity()) return;
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
            dyn::Append(hashes.items, {.hash = key, .display_name = n});
            break;
        }
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            if (key == k_untagged_key)
                tags.selected_untagged = true;
            else
                tags.bitset.Set(key);
            break;
        }
        case Type::Bool: data.Get<bool>() = true; break;
    }
}

void FilterSelection::Remove(u64 key) {
    switch (data.tag) {
        case Type::Hashes:
            dyn::RemoveValueIfSwapLast(data.Get<HashesData>().items,
                                       [key](SelectedHash const& h) { return h.hash == key; });
            break;
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            if (key == k_untagged_key)
                tags.selected_untagged = false;
            else
                tags.bitset.Clear(key);
            break;
        }
        case Type::Bool: data.Get<bool>() = false; break;
    }
}

void FilterSelection::Toggle(u64 key, String display_name) {
    if (Contains(key))
        Remove(key);
    else
        Add(key, display_name);
}

void FilterSelection::Clear() {
    switch (data.tag) {
        case Type::Hashes: dyn::Clear(data.Get<HashesData>().items); break;
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            tags.bitset.ClearAll();
            tags.selected_untagged = false;
            break;
        }
        case Type::Bool: data.Get<bool>() = false; break;
    }
}

void FilterSelection::ClearToOne() {
    switch (data.tag) {
        case Type::Hashes: {
            auto& hashes = data.Get<HashesData>();
            if (hashes.items.size > 1) dyn::Resize(hashes.items, 1);
            break;
        }
        case Type::Tags: {
            auto& tags = data.Get<TagsData>();
            if (tags.selected_untagged) {
                tags.bitset.ClearAll();
            } else {
                auto const first = tags.bitset.FirstSetBit();
                tags.bitset.ClearAll();
                tags.bitset.Set(first);
            }
            break;
        }
        case Type::Bool: break;
    }
}

bool ItemMatchesTagFilter(FilterSelection const& filter, TagsBitset const& item_tags, FilterMode mode) {
    ASSERT(filter.data.tag == FilterSelection::Type::Tags);
    auto const& td = filter.data.Get<FilterSelection::TagsData>();
    bool const untagged_matched = td.selected_untagged && !item_tags.AnyValuesSet();
    auto const intersection = td.bitset & item_tags;

    switch (mode) {
        case FilterMode::Single:
        case FilterMode::MultipleAnd:
            if (td.selected_untagged && !untagged_matched) return false;
            if (intersection != td.bitset) return false;
            return true;
        case FilterMode::MultipleOr: return untagged_matched || intersection.AnyValuesSet();
        case FilterMode::Count: PanicIfReached();
    }
    return false;
}

bool RootNodeLessThan(FolderNode const* const& a,
                      DummyValueType const&,
                      FolderNode const* const& b,
                      DummyValueType const&) {
    return a->name < b->name;
}

bool MatchesFilterSearch(String filter_text, String search_text) {
    if (search_text.size == 0) return true; // Empty search shows all filters
    if (filter_text.size == 0) return false; // Empty filter text doesn't match
    return ContainsCaseInsensitiveAscii(filter_text, search_text);
}

constexpr auto k_right_click_menu_popup_id = (imgui::Id)SourceLocationHash();

void DoRightClickMenuForBox(GuiBuilder& builder,
                            CommonBrowserState& state,
                            Box const& box,
                            u64 item_hash,
                            RightClickMenuState::Function const& do_menu) {
    if (auto const rect = BoxRect(builder, box)) {
        auto const window_rect = builder.imgui.ViewportRectToWindowRect(*rect);
        if (builder.imgui.ButtonBehaviour(
                window_rect,
                box.imgui_id,
                {.mouse_button = MouseButton::Right, .event = MouseButtonEvent::Up, .dont_set_hot = true})) {
            state.right_click_menu_state.absolute_creator_rect = window_rect;
            state.right_click_menu_state.do_menu = do_menu;
            state.right_click_menu_state.item_hash = item_hash;
            builder.imgui.OpenPopupMenu(k_right_click_menu_popup_id, box.imgui_id);
        }
    }
}

namespace key_nav {

constexpr u32 k_num_items_in_page = BrowserKeyboardNavigation::ItemHistory::k_max_items;

static bool g_show_focus_rectangles = false;

static void
FocusPanel(BrowserKeyboardNavigation& nav, BrowserKeyboardNavigation::Panel panel, bool always_select_first) {
    nav.focused_panel = panel;
    nav.panel_state = {};
    if (always_select_first || !nav.focused_items[ToInt(nav.focused_panel)])
        nav.panel_state.select_next = true;
    nav.panel_just_focused = true;
    g_show_focus_rectangles = true;
}

static void FocusItem(BrowserKeyboardNavigation& nav, BrowserKeyboardNavigation::Panel panel, u64 item_id) {
    nav.temp_focused_items[ToInt(panel)] = item_id;
}

static void BeginFrame(imgui::Context& imgui, BrowserKeyboardNavigation& nav) {
    nav.focused_items = nav.temp_focused_items;
    nav.temp_focused_items = {};
    nav.panel_just_focused = false;
    nav.panel_state.select_next_tab_item = false;
    nav.panel_state.select_next_at = 0;
    nav.panel_state.previous_tab_item = {};
    nav.panel_state.item_history.SetBarrier();
    nav.input = {};

    if (imgui.exclusive_focus_viewport && imgui.IsKeyboardFocus(imgui.exclusive_focus_viewport->id)) {
        auto const& frame_input = GuiIo().in;
        auto& frame_output = GuiIo().out;

        frame_output.wants.keyboard_keys.SetBits(k_navigation_keys);

        auto const key_events = [&](KeyCode key) { return frame_input.Key(key).presses_or_repeats.size; };

        for (auto const& e : frame_input.Key(KeyCode::DownArrow).presses_or_repeats)
            if (e.modifiers.IsOnly(ModifierKey::Modifier))
                nav.input.next_section_presses++;
            else if (e.modifiers.IsNone())
                nav.input.down_presses++;

        for (auto const& e : frame_input.Key(KeyCode::UpArrow).presses_or_repeats)
            if (e.modifiers.IsOnly(ModifierKey::Modifier))
                nav.input.previous_section_presses++;
            else if (e.modifiers.IsNone())
                nav.input.up_presses++;

        nav.input.page_down_presses = CheckedCast<u8>(key_events(KeyCode::PageDown));
        nav.input.page_up_presses = CheckedCast<u8>(key_events(KeyCode::PageUp));

        if (nav.input != BrowserKeyboardNavigation::Input {}) g_show_focus_rectangles = true;

        // There's only 2 panels so right/left or tab/shift-tab do the same thing since we wrap around.
        static_assert(ToInt(BrowserKeyboardNavigation::Panel::Count) == 2 + 1);
        for (auto const _ : Range(key_events(KeyCode::Tab) + key_events(KeyCode::RightArrow) +
                                  key_events(KeyCode::LeftArrow))) {
            switch (nav.focused_panel) {
                case BrowserKeyboardNavigation::Panel::None:
                case BrowserKeyboardNavigation::Panel::Filters:
                    FocusPanel(nav, BrowserKeyboardNavigation::Panel::Items, false);
                    break;
                case BrowserKeyboardNavigation::Panel::Items:
                    FocusPanel(nav, BrowserKeyboardNavigation::Panel::Filters, false);
                    break;
                case BrowserKeyboardNavigation::Panel::Count: PanicIfReached();
            }
        }

        if (key_events(KeyCode::Home)) nav.panel_state.select_next = true;

        if (nav.focused_items[ToInt(nav.focused_panel)] == 0) {
            if (key_events(KeyCode::DownArrow) || key_events(KeyCode::UpArrow) || key_events(KeyCode::PageUp))
                nav.panel_state.select_next = true;
        }
    }
}

static void EndFrame(imgui::Context& imgui, BrowserKeyboardNavigation& nav) {
    if (imgui.exclusive_focus_viewport && imgui.IsKeyboardFocus(imgui.exclusive_focus_viewport->id)) {
        auto const& frame_input = GuiIo().in;
        auto& frame_output = GuiIo().out;

        auto const key_events = [&](KeyCode key) { return frame_input.Key(key).presses_or_repeats.size; };

        if (key_events(KeyCode::End)) {
            nav.panel_state.id_to_select = nav.panel_state.item_history.AtPrevious(1);
            g_show_focus_rectangles = true;
        }

        // 'select_next_at' is a non-wrap-around action, so if there's still pending, we select the last item
        // rather than let it continue counting down on the next frame (from the top of the item list).
        if (nav.panel_state.select_next_at)
            nav.panel_state.id_to_select = nav.panel_state.item_history.AtPrevious(1);

        if (nav.temp_focused_items != nav.focused_items || nav.panel_state.id_to_select)
            frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
}

struct ItemArgs {
    Box const& box; // Box for button firing.
    Box const* box_for_scrolling; // Use a different box for scrolling into view.
    Optional<Rect> rect_for_drawing; // Use a different rectangle for drawing.
    BrowserKeyboardNavigation::Panel panel;
    u64 id;
    bool is_selected;
    bool is_tab_item;
};

static void DrawFocusBox(GuiBuilder& builder, Rect relative_rect) {
    builder.imgui.draw_list->AddRect(builder.imgui.RegisterAndConvertRect(relative_rect),
                                     ToU32({.c = Col::Blue}),
                                     WwToPixels(k_corner_rounding),
                                     0b1111,
                                     2);
}

static bool DoItem(GuiBuilder& builder, BrowserKeyboardNavigation& nav, ItemArgs const& args) {
    if (!builder.IsInputAndRenderPass()) return {};

    // 0 means 'nothing' throughout the navigation state, but a caller's id can legitimately be 0: filter
    // values are keyed by enum values that start at zero. Shifting is bijective, so it frees 0 for the
    // sentinel without making any two items collide.
    auto const item_id = args.id + 1;

    auto const panel_index = ToInt(args.panel);
    auto const is_focused = nav.focused_items[panel_index] == item_id;

    bool button_fired_from_keyboard = false;

    if (nav.focused_panel == args.panel) {
        auto& panel = nav.panel_state;
        bool focus_this = false;

        if (Exchange(panel.select_next, false)) focus_this = true;

        if (args.is_tab_item && Exchange(panel.select_next_tab_item, false)) focus_this = true;

        if (item_id == panel.id_to_select) {
            panel.id_to_select = 0;
            focus_this = true;
        }

        if (panel.select_next_at) {
            if (--panel.select_next_at == 0) focus_this = true;
        }

        if (focus_this) FocusItem(nav, args.panel, item_id);

        if (is_focused) {
            auto& input = nav.input;
            // Page-up/down.
            // NOTE: we don't support multiple page-ups or page-downs in a single frame.
            if (input.page_up_presses) {
                input.page_up_presses = 0;
                panel.id_to_select = panel.item_history.AtPreviousOrBarrier(k_num_items_in_page);
            }
            if (input.page_down_presses) {
                input.page_down_presses = 0;
                panel.select_next_at = k_num_items_in_page;
            }

            // Up/down arrows.
            if (input.up_presses) {
                --input.up_presses;
                panel.id_to_select = panel.item_history.AtPrevious(1);
            }
            if (input.down_presses) {
                --input.down_presses;
                panel.select_next = true;
            }

            // Section jumps.
            if (input.previous_section_presses) {
                --input.previous_section_presses;
                panel.id_to_select = panel.previous_tab_item;
            }
            if (input.next_section_presses) {
                --input.next_section_presses;
                panel.select_next_tab_item = true;
            }

            // Enter key.
            if (GuiIo().in.Key(KeyCode::Enter).presses_or_repeats.size % 2 == 1) {
                button_fired_from_keyboard = true;
                nav.temp_focused_items[panel_index] = item_id;
                g_show_focus_rectangles = true;
            }

            if (g_show_focus_rectangles &&
                builder.imgui.IsKeyboardFocus(builder.imgui.curr_viewport->root_viewport->id)) {
                auto r = args.rect_for_drawing ? *args.rect_for_drawing : *BoxRect(builder, args.box);
                DrawFocusBox(builder, r);
            }
        }

        panel.item_history.Push(item_id);
        if (args.is_tab_item) panel.previous_tab_item = item_id;

        if (button_fired_from_keyboard || (is_focused && nav.panel_just_focused) || focus_this) {
            builder.imgui.ScrollViewportToShowRectangle(
                BoxRect(builder, args.box_for_scrolling ? *args.box_for_scrolling : args.box).Value());
        }
    }

    if (args.box.button_fired) {
        nav.focused_panel = args.panel;
        FocusItem(nav, args.panel, item_id);
    }

    if (is_focused && !nav.temp_focused_items[panel_index]) FocusItem(nav, args.panel, item_id);

    return button_fired_from_keyboard;
}

} // namespace key_nav

// Brief flash after a locate button scrolls here, so you can see where the jump landed. The animation's
// ease-out decays as (1-t)^2, which is nearly invisible for the second half of its duration. Sqrt undoes
// that to a linear fade, and the clamped scale holds it at full for the first ~150ms so the eye can land
// on it before it starts fading.
static void DrawLocateFlash(GuiBuilder& builder, Box const& box) {
    auto const rect = BoxRect(builder, box);
    if (!rect) return;
    auto const flash = Min(1.0f, Sqrt(builder.imgui.GetAnimatedValue(box.imgui_id, 0.0f)) * 1.3f);
    if (flash <= 0.001f) return;
    builder.imgui.draw_list->AddRectFilled(builder.imgui.ViewportRectToWindowRect(*rect),
                                           ToU32(Col {.c = Col::White, .alpha = (u8)(flash * 120.0f)}));
    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
}

// Why the current item can't be jumped to; empty when it can be.
static String CurrentItemUnlistedTooltip(ArenaAllocator& arena, String type, CurrentItemStatus const& item) {
    using Visibility = CurrentItemStatus::Visibility;
    switch (item.visibility) {
        case Visibility::NotInList:
            return fmt::Format(arena,
                               "The current {} ({}) isn't listed because {}.",
                               type,
                               item.name,
                               item.not_in_list_reason);
        case Visibility::Loading: return fmt::Format(arena, "Still scanning for the current {}.", type);
        case Visibility::None:
        case Visibility::Shown:
        case Visibility::InCollapsedSection:
        case Visibility::HiddenByFilters: break;
    }
    return {};
}

BrowserItemResult
DoBrowserItem(GuiBuilder& builder, CommonBrowserState& state, BrowserItemOptions const& options) {

    auto item =
        DoBox(builder,
              {
                  .parent = options.parent,
                  .id_extra = options.id_extra,
                  .background_fill_colours = Col {.c = options.is_current ? Col::Highlight : Col::None},
                  .background_fill_auto_hot_active_overlay = true,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.l = k_browser_row_pad_x},
                      .contents_direction = layout::Direction::Row,
                  },
                  .value_popup = options.value_popup,
                  .value_popup_delay_secs = 0.4,
                  .tooltip = options.tooltip,
                  .tooltip_footer = options.tooltip_footer,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_placement = TooltipPlacement::LeftThenRight,
                  .button_behaviour = imgui::ButtonConfig {.dont_fire_on_double_click = true},
              });

    if (options.icons.size) {
        auto const icon_container = DoBox(builder,
                                          {
                                              .parent = item,
                                              .layout {
                                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                                  .margins = {.r = k_browser_spacing / 2},
                                                  .contents_gap = {1, 0},
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                              },
                                          });
        for (auto const [i, icon] : Enumerate(options.icons)) {
            switch (icon.tag) {
                case ItemIconType::None: break;
                case ItemIconType::Image: {
                    auto const tex = icon.Get<ImageID>();
                    DoBox(builder,
                          {
                              .parent = icon_container,
                              .id_extra = i,
                              .background_tex = &tex,
                              .layout {
                                  .size = k_library_icon_standard_size,
                              },
                          });
                    break;
                }
                case ItemIconType::Font: {
                    DoBox(builder,
                          {
                              .parent = icon_container,
                              .id_extra = i,
                              .text = icon.Get<String>(),
                              .size_from_text = true,
                              .font = FontType::Icons,
                          });
                    break;
                }
            }
        }
    }

    DoBox(builder,
          {
              .parent = item,
              .text = options.text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Body,
          });

    if (auto const rect = BoxRect(builder, item)) {
        auto const window_rect = builder.imgui.ViewportRectToWindowRect(*rect);
        builder.imgui.ButtonBehaviour(window_rect,
                                      item.imgui_id,
                                      {
                                          .mouse_button = MouseButton::Left,
                                          .event = MouseButtonEvent::DoubleClick,
                                          .closes_popup_or_modal = true,
                                      });

        if (options.is_current) DrawLocateFlash(builder, item);
    }

    if (item.button_fired) {
        ShowTipIfNeeded(options.notifications,
                        options.store,
                        0xe9bdfea0aae12dce,
                        "Double-click to load and close; single-click to load only."_s);
    }

    if (options.is_default) {
        DoBox(
            builder,
            {
                .parent = item,
                .text = ICON_FA_HOUSE,
                .font = FontType::Icons,
                .font_size = k_font_icons_size * 0.7f,
                .text_colours = Col {.c = Col::Subtext0},
                .text_justification = TextJustification::CentredLeft,
                .layout {
                    .size = {16, layout::k_fill_parent},
                },
                .tooltip =
                    "Your default preset. Floe loads it whenever you open a new instance. Right-click any preset to make it the default."_s,
            });
    }

    auto const favourite_toggled =
        !!DoBox(
              builder,
              {
                  .parent = item,
                  .text = ICON_FA_STAR,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.7f,
                  .text_colours =
                      ColSet {
                          .base = Col {.c = options.is_favourite ? Col::Highlight400
                                            : item.is_hot        ? Col::Surface2
                                                                 : Col::None},
                          .hot = Col {.c = Col::Highlight200},
                          .active = Col {.c = Col::Highlight200},
                      },
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {5 + (k_browser_row_pad_x * 2), layout::k_fill_parent},
                  },
                  .tooltip =
                      "Mark this as a favourite. Favourites are shared by every Floe instance, and the star button in the toolbar shows only them."_s,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_placement = TooltipPlacement::LeftThenRight,
                  .button_behaviour = imgui::ButtonConfig {},
              })
              .button_fired;

    auto const fired_via_keyboard = key_nav::DoItem(builder,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = item,
                                                        .panel = BrowserKeyboardNavigation::Panel::Items,
                                                        .id = options.item_id,
                                                        .is_selected = options.is_current,
                                                        .is_tab_item = options.is_tab_item,
                                                    });

    // Only a mouse click arms it: after a keyboard-driven load the cursor is likely already outside, so the
    // browser would close as soon as it twitched.
    if (item.button_fired && !options.is_current) state.close_when_cursor_leaves = true;

    return {item, favourite_toggled, item.button_fired || fired_via_keyboard};
}

Box DoBrowserItemsRoot(GuiBuilder& builder) {
    return DoBox(builder,
                 {
                     .layout {
                         .size = layout::k_fill_parent,
                         // Between folder sections only: the gap is not applied before the first, which
                         // would just be empty space at the top of the list.
                         .contents_gap = k_browser_spacing,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

// Browse mode doesn't present the selection as filters.
static String SelectionNoun(CommonBrowserState const& state) {
    return state.mode == BrowserMode::Browse ? "selection"_s : "filters"_s;
}

static String FolderDisplayName(FolderNode const& folder) {
    return folder.display_name.size ? folder.display_name : folder.name;
}

FolderNode const* CollectionRootFolder(FolderNode const* folder) {
    for (; folder->parent; folder = folder->parent) {
        // A preset bank nested in another folder tree is a collection of its own. Subfolders of a bank share
        // its bank, so the walk continues through them up to the bank's own folder.
        if (folder->user_data.As<PresetFolderListing const>() &&
            ContainingPresetBank(folder) != ContainingPresetBank(folder->parent))
            break;
    }
    return folder;
}

BrowseScope CurrentBrowseScope(CommonBrowserState const& state, BrowseScopeResolver const& resolver) {
    BrowseScope result {.collection_noun = resolver.collection_noun};
    if (state.mode != BrowserMode::Browse) return result;

    // A collection that is a library is named by its own filter.
    state.Filter(BrowserFilter::Library).ForEachSelected([&](String display_name, u64 key) {
        if (resolver.collection_of_library) result.collection = resolver.collection_of_library(key);
        if (!result.collection)
            result.collection =
                BrowserCollection {.filter = BrowserFilter::Library, .key = key, .name = display_name};
        return LoopControl::Break;
    });

    Optional<u64> selected_folder {};
    state.Filter(BrowserFilter::Folder).ForEachSelected([&](String, u64 key) {
        selected_folder = key;
        return LoopControl::Break;
    });
    if (!selected_folder) return result;

    for (auto const [folder, _, _] : resolver.folders) {
        if (folder->Hash() != *selected_folder) continue;
        auto const root = CollectionRootFolder(folder);
        result.collection = resolver.collection_of_root(*root);
        if (root != folder) result.folder_name = FolderDisplayName(*folder);
        break;
    }

    return result;
}

void DoBrowserEmptyListMessage(GuiBuilder& builder,
                               CommonBrowserState& state,
                               Box root,
                               String plural_item_type_name) {
    auto const searching = state.search.size != 0;
    auto const favourites_only = state.favourites.HasSelected();
    auto const scoped = state.HasFilters();

    auto const items = favourites_only
                           ? (String)fmt::Format(builder.arena, "favourite {}", plural_item_type_name)
                           : plural_item_type_name;

    auto const text = ({
        String t {};
        if (searching && scoped)
            t = fmt::Format(builder.arena,
                            "No {} match \"{}\" within the current {}.",
                            items,
                            state.search,
                            SelectionNoun(state));
        else if (searching)
            t = fmt::Format(builder.arena, "No {} match \"{}\".", items, state.search);
        else if (favourites_only && scoped)
            t = fmt::Format(builder.arena, "No {} within the current {}.", items, SelectionNoun(state));
        else if (favourites_only)
            t = fmt::Format(builder.arena, "No {}.", items);
        else if (scoped)
            t = fmt::Format(builder.arena, "No {} match your {}.", items, SelectionNoun(state));
        else
            t = fmt::Format(builder.arena, "No {} available.", items);
        t;
    });

    DoBox(builder,
          {
              .parent = root,
              .text = text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Subtext0},
              .layout {
                  .margins = {.lr = k_browser_row_pad_x, .tb = k_browser_spacing},
              },
          });

    // The search and Favourites only refine what the filters or selection let through, so with those
    // cleared the same refinement covers everything.
    if ((searching || favourites_only) && scoped) {
        auto const button =
            DoBox(builder,
                  {
                      .parent = root,
                      .background_fill_colours =
                          ColSet {
                              .base = Col {.c = Col::Highlight},
                              .hot = Col {.c = Col::Highlight100},
                              .active = Col {.c = Col::Highlight},
                          },
                      .round_background_corners = 0b1111,
                      .corner_rounding = k_corner_rounding,
                      .layout {
                          .size = {layout::k_hug_contents, k_browser_item_height},
                          .margins = {.l = k_browser_row_pad_x, .b = k_browser_spacing},
                          .contents_padding {.lr = k_default_spacing / 2},
                          .contents_align = layout::Alignment::Middle,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                      .tooltip = (String)fmt::Format(builder.arena,
                                                     "Clear the {} but keep your {}, so it covers all {}.",
                                                     SelectionNoun(state),
                                                     searching && favourites_only ? "search and Favourites"_s
                                                     : searching                  ? "search"_s
                                                                                  : "Favourites"_s,
                                                     plural_item_type_name),
                      .button_behaviour = imgui::ButtonConfig {},
                      .name = "browser.search-all-button"_s,
                  });
        DoBox(builder,
              {
                  .parent = button,
                  .text = fmt::Format(builder.arena, searching ? "Search all {}"_s : "Show all {}"_s, items),
                  .size_from_text = true,
                  .font = FontType::Heading3,
              });
        if (button.button_fired) {
            state.ClearAll();
            state.browse = {};
        }
    }
}

// Scrolls the current viewport so the box is visible. False means it couldn't yet, so the request should
// stay pending: a freshly opened modal and its viewports need a frame or two before their sizes are known,
// and the scroll limit comes from the previous frame's content height, which lags while a scan is still
// adding items. Scrolling before the target is within that height lands short.
static bool ScrollViewportToShowBox(GuiBuilder& builder, Box const& box) {
    auto const r = BoxRect(builder, box);
    if (!r) return false;
    auto const viewport = builder.imgui.curr_viewport;
    if (viewport->root_viewport->size_resolution != imgui::Viewport::SizeResolutionState::NotPending)
        return false;
    if (r->Bottom() > viewport->prevprev_content_size.y) return false;
    builder.imgui.ScrollViewportToShowRectangle(*r);
    return true;
}

void ScrollBrowserToShowCurrent(GuiBuilder& builder, CommonBrowserState& state, Box const& box) {
    if (!state.scroll_to_show_current) return;
    if (!ScrollViewportToShowBox(builder, box)) return;

    // Items added by a scan shift the list, so keep re-applying until it settles.
    if (!state.items_still_loading) {
        state.scroll_to_show_current = false;
        if (Exchange(state.flash_current_when_shown, false))
            builder.imgui.StartAnimation(box.imgui_id, 1.0f, 0.6f, true);
    }
}

struct FolderFilterTreeContext {
    FolderFilterItemInfoLookupTable const& folder_infos;
    TreeLines lines {}; // For the rows at the current depth.
};

struct FolderFilterTreeOptions {
    RightClickMenuState::Function do_right_click_menu {};
    bool parent_collection_is_selected {};
    // Selection of this folder, or anything above it, doesn't light the lines into descendants.
    FolderNode const* excluded_ancestor {};
    DeselectFallback deselect_fallback {};
    String collection_noun {}; // Names what deselecting a folder goes back to showing.
};

// A folder with nothing in it is left out, as is a preset bank within the preset bank.
static bool FolderIsListed(FolderNode const& folder, FolderFilterItemInfoLookupTable const& folder_infos) {
    if (folder.user_data.As<PresetFolderListing const>())
        if (auto const bank = PresetBankAtNode(folder);
            bank && folder.parent && bank != PresetBankAtNode(*folder.parent))
            return false;
    auto const info = folder_infos.Find(&folder);
    return info && info->total_available != 0;
}

static bool DoFolderFilterChildren(GuiBuilder& builder,
                                   CommonBrowserState& state,
                                   Box const& parent,
                                   FolderNode const* first_child,
                                   FolderFilterTreeContext context,
                                   FolderFilterTreeOptions const& options);

static void DoFolderFilterAndChildren(GuiBuilder& builder,
                                      CommonBrowserState& state,
                                      Box const& parent,
                                      FolderNode const* folder,
                                      FolderFilterTreeContext const& context,
                                      FolderFilterTreeOptions const& options) {
    bool subtree_selected = options.parent_collection_is_selected;
    if (!subtree_selected) {
        for (auto f = folder; f && f != options.excluded_ancestor; f = f->parent) {
            if (state.Filter(BrowserFilter::Folder).Contains(f->Hash())) {
                subtree_selected = true;
                break;
            }

            // We want to stop if the parent is part of a different preset bank.
            if (f->user_data.As<PresetFolderListing const>())
                if (auto const bank = PresetBankAtNode(*f);
                    bank && f->parent && bank != PresetBankAtNode(*f->parent))
                    break;
        }
    }
    auto const is_selected = state.Filter(BrowserFilter::Folder).Contains(folder->Hash());

    auto const this_info = context.folder_infos.Find(folder);
    ASSERT(this_info);

    auto const folder_hash = folder->Hash();

    auto const button = DoFilterTreeButton(
        builder,
        state,
        *this_info,
        FilterTreeButtonOptions {
            .common =
                {
                    .parent = parent,
                    .id_extra = folder_hash,
                    .is_selected = is_selected,
                    .text = folder->display_name.size ? folder->display_name : folder->name,
                    .value_popup = folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
                    .match_phrase = "in this folder, subfolders included"_s,
                    .deselect_shows =
                        options.deselect_fallback.filter
                            ? (String)fmt::Format(builder.arena, "the whole {}", options.collection_noun)
                            : String {},
                    .filter = state.Filter(BrowserFilter::Folder),
                    .clicked_key = folder_hash,
                    .filter_mode = state.filter_mode,
                },
            .lines = context.lines,
            .deselect_fallback = options.deselect_fallback,
        });

    if (options.do_right_click_menu)
        DoRightClickMenuForBox(builder, state, button, folder->Hash(), options.do_right_click_menu);

    auto child_context = context;
    auto& child_lines = child_context.lines;
    if (context.lines.lined) {
        ++child_lines.depth;
    } else {
        child_lines.inset += k_browser_row_pad_x;
        child_lines.lined = true;
    }
    if (subtree_selected) child_lines.gold_from = Min(child_lines.gold_from, child_lines.depth);
    DoFolderFilterChildren(builder, state, parent, folder->first_child, child_context, options);
}

// Lists the folders in a sibling chain. Returns whether any were listed. The last listed sibling is found
// first so each row knows whether the line beside it carries on.
static bool DoFolderFilterChildren(GuiBuilder& builder,
                                   CommonBrowserState& state,
                                   Box const& parent,
                                   FolderNode const* first_child,
                                   FolderFilterTreeContext context,
                                   FolderFilterTreeOptions const& options) {
    FolderNode const* last_listed = nullptr;
    for (auto* child = first_child; child; child = child->next)
        if (FolderIsListed(*child, context.folder_infos)) last_listed = child;
    if (!last_listed) return false;

    ASSERT(context.lines.depth < 8);
    auto const level_bit = (u8)(1 << context.lines.depth);
    for (auto* child = first_child; child; child = child->next) {
        if (!FolderIsListed(*child, context.folder_infos)) continue;
        if (child != last_listed)
            context.lines.continues |= level_bit;
        else
            context.lines.continues &= (u8)~level_bit;
        DoFolderFilterAndChildren(builder, state, parent, child, context, options);
    }
    return true;
}

static void HandleFilterButtonClick(GuiBuilder& builder,
                                    CommonBrowserState& state,
                                    FilterButtonCommonOptions const& options) {
    state.keyboard_navigation.focused_panel = BrowserKeyboardNavigation::Panel::Filters;
    auto display_name = builder.arena.Clone(options.text);
    switch (options.filter_mode) {
        case FilterMode::Single: {
            state.ClearAll();
            if (!options.is_selected) options.filter.Add(options.clicked_key, display_name);
            state.scroll_items_to_start = true;
            break;
        }
        case FilterMode::MultipleAnd:
        case FilterMode::MultipleOr: {
            if (options.is_selected)
                options.filter.Remove(options.clicked_key);
            else
                options.filter.Add(options.clicked_key, display_name);
            break;
        }
        case FilterMode::Count: PanicIfReached();
    }
}

static u32 NumUsedForFilter(FilterItemInfo const& info, FilterMode mode) {
    switch (mode) {
        case FilterMode::MultipleAnd: return info.num_used_in_items_lists;
        case FilterMode::MultipleOr: return info.total_available;
        case FilterMode::Single: return info.total_available;
        case FilterMode::Count: PanicIfReached();
    }
    return 0;
}

// What clicking a filter value does: it depends on the mode, whether the value is already selected, and
// whether it's greyed out. An explicit tooltip wins; no match phrase means no tooltip.
static TooltipString FilterValueTooltip(ArenaAllocator& arena,
                                        CommonBrowserState const& state,
                                        FilterButtonCommonOptions const& options,
                                        u32 num_used) {
    if (options.tooltip.tag != TooltipStringType::None) return options.tooltip;
    if (!options.match_phrase.size) return k_nullopt;

    if (options.filter_mode == FilterMode::MultipleAnd && num_used == 0 && !options.is_selected)
        return "Greyed out because nothing in the current results matches it, so selecting it would leave the list empty."_s;

    auto const show_only = fmt::Format(arena, "Show only the items {}.", options.match_phrase);

    switch (state.mode) {
        case BrowserMode::Browse:
            if (options.is_selected)
                return (String)fmt::Format(arena,
                                           "Click again to show {}.",
                                           options.deselect_shows.size ? options.deselect_shows
                                                                       : "everything"_s);
            return (String)fmt::Format(arena, "{} Click another to switch.", show_only);
        case BrowserMode::Filter:
            switch (options.filter_mode) {
                case FilterMode::Single:
                    if (options.is_selected) return "Click again to clear this filter."_s;
                    return (String)fmt::Format(
                        arena,
                        "{} Selecting a filter replaces the previous one. Switch to Match all or Match any below to combine filters.",
                        show_only);
                case FilterMode::MultipleAnd:
                    if (options.is_selected) return "Remove this filter."_s;
                    return (String)fmt::Format(
                        arena,
                        "Add a filter for the items {}. Items must match every selected filter.",
                        options.match_phrase);
                case FilterMode::MultipleOr:
                    if (options.is_selected) return "Remove this filter."_s;
                    return (String)fmt::Format(
                        arena,
                        "Add a filter for the items {}. Items matching any selected filter are shown.",
                        options.match_phrase);
                case FilterMode::Count: break;
            }
            break;
        case BrowserMode::Count: break;
    }
    PanicIfReached();
}

struct NumUsedForFilterString {
    DynamicArrayBounded<char, 16> str;
    f32x2 size;
};

static NumUsedForFilterString
NumUsedForFilterString(GuiBuilder& builder, u32 total_available, FontType font_type) {
    // We size to the largest possible number so that the layout doesn't jump around as the num_used changes.
    auto const total_text = fmt::FormatInline<16>("({})"_s, total_available);
    auto const number_size =
        Max(builder.fonts.atlas[ToInt(font_type)]->CalcTextSize(total_text, {}), f32x2 {0, 0});
    return {total_text, number_size};
}

static String UppercaseAscii(ArenaAllocator& arena, String text) {
    auto const result = arena.AllocateExactSizeUninitialised<char>(text.size);
    for (auto const [index, c] : Enumerate(text))
        result[index] = ToUppercaseAscii(c);
    return result;
}

// A full-width row of the filters panel: a faint highlight when selected, and a faint hover.
static Colours BrowserRowColours(bool is_selected) {
    return ColSet {
        .base {.c = is_selected ? Col::Highlight300 : Col::None, .alpha = 37},
        .hot {.c = is_selected ? Col::Highlight200 : Col::Overlay0, .dark_mode = true, .alpha = 37},
        .active {.c = is_selected ? Col::Highlight200 : Col::Overlay0, .dark_mode = true, .alpha = 37},
    };
}

// A row's text is dimmed when there's nothing behind it to show.
static Col BrowserRowTextColour(bool has_items) {
    return {.c = has_items ? Col::Text : Col::Overlay2, .dark_mode = true};
}

// The name fills the row, ellipsised if it must, and the count sits after it.
static void DoRowNameAndCount(GuiBuilder& builder,
                              Box const& row,
                              String name,
                              u32 count,
                              Col text_colours,
                              FontType name_font = FontType::Body) {
    DoBox(builder,
          {
              .parent = row,
              .text = name,
              .size_from_text = false,
              .font = name_font,
              .text_colours = text_colours,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = f32x2 {layout::k_fill_parent, k_font_body_size},
              },
          });

    DoBox(builder,
          {
              .parent = row,
              .text = fmt::FormatInline<16>("({})"_s, count),
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = text_colours,
              .parent_dictates_hot_and_active = true,
          });
}

// A caret in a fixed-width slot, so carets line up in a column whatever they point at.
static void DoRowCaret(GuiBuilder& builder, Box const& row, String icon, f32 width, bool dark_mode = true) {
    DoBox(builder,
          {
              .parent = row,
              .text = icon,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.6f,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = dark_mode},
              .text_justification = TextJustification::Centred,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {width, k_font_body_size},
              },
          });
}

Box DoFilterButton(GuiBuilder& builder,
                   CommonBrowserState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options) {
    // Browse mode is a column of full-width rows, the same shape an open collection's folders take. The pill,
    // with its wrapping layout, belongs to Filter mode's tree.
    if (state.mode == BrowserMode::Browse) {
        return DoFilterTreeButton(builder,
                                  state,
                                  info,
                                  {
                                      .common = options.common,
                                      .lines = {},
                                      .icon = options.icon,
                                  });
    }

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    f32 const lr_spacing = 4;

    auto const button =
        DoBox(builder,
              {
                  .parent = options.common.parent,
                  .id_extra = options.common.id_extra,
                  .background_fill_colours = options.common.is_selected
                                                 ? Colours {Col {.c = Col::Highlight, .dark_mode = true}}
                                                 : Colours {ColSet {
                                                       .base = Col {.c = Col::Background2, .dark_mode = true},
                                                       .hot = Col {.c = Col::Surface1, .dark_mode = true},
                                                       .active = Col {.c = Col::Surface1, .dark_mode = true},
                                                   }},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .corner_rounding = k_corner_rounding,
                  .layout {
                      .size {
                          layout::k_hug_contents,
                          k_browser_item_height,
                      },
                      .contents_padding {
                          .l = !options.icon ? lr_spacing : 0,
                          .r = lr_spacing,
                      },
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .value_popup = options.common.value_popup,
                  .tooltip = FilterValueTooltip(builder.arena, state, options.common, num_used),
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_placement = TooltipPlacement::RightThenLeft,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    bool grey_out = false;
    if (options.common.filter_mode == FilterMode::MultipleAnd) grey_out = num_used == 0;

    if (options.icon) {
        DoBox(builder,
              {
                  .parent = button,
                  .background_tex = options.icon,
                  .layout {
                      .size = k_library_icon_standard_size,
                      .margins = {.r = 3},
                  },
              });
    }

    // When selected, the background is a bright Highlight colour, we need to account for that.
    bool const dark_mode_text = !options.common.is_selected;

    DoBox(builder,
          {
              .parent = button,
              .text = options.common.text,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours =
                  ColSet {
                      .base = Col {.c = grey_out ? Col::Surface1 : Col::Text, .dark_mode = dark_mode_text},
                      .hot = Col {.c = Col::Text, .dark_mode = dark_mode_text},
                      .active = Col {.c = Col::Text, .dark_mode = dark_mode_text},
                  },
              .text_overflow = TextOverflowType::AllowOverflow,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .size = f32x2 {999},
                      .margins = {.l = options.icon ? 0 : k_browser_spacing / 2},
                  },
          });

    auto const k_numbering_font = FontType::Heading3;

    auto const total_text = NumUsedForFilterString(builder, info.total_available, k_numbering_font);

    DoBox(builder,
          {
              .parent = button,
              .text = total_text.str,
              .size_from_text = false,
              .font = k_numbering_font,
              .text_colours =
                  ColSet {
                      .base = Col {.c = grey_out ? Col::Surface1 : Col::Text, .dark_mode = dark_mode_text},
                      .hot = Col {.c = Col::Text, .dark_mode = dark_mode_text},
                      .active = Col {.c = Col::Text, .dark_mode = dark_mode_text},
                  },
              .text_justification = TextJustification::CentredLeft,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
              .layout {
                  .size = {total_text.size.x, layout::k_fill_parent},
                  .margins = {.l = 3},
              },
          });

    auto const fired_via_keyboard = key_nav::DoItem(builder,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = button,
                                                        .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                        .id = options.common.clicked_key,
                                                        .is_selected = options.common.is_selected,
                                                        .is_tab_item = false,
                                                    });

    if (button.button_fired || fired_via_keyboard) HandleFilterButtonClick(builder, state, options.common);

    return button;
}

Box DoFilterTreeButton(GuiBuilder& builder,
                       CommonBrowserState& state,
                       FilterItemInfo const& info,
                       FilterTreeButtonOptions const& options) {
    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const& lines = options.lines;
    auto const gutter_width =
        lines.inset +
        (lines.lined ? (lines.depth * k_tree_indent) + (k_tree_indent - k_browser_row_pad_x) : 0);

    auto const button_outer = DoBox(builder,
                                    {
                                        .parent = options.common.parent,
                                        .id_extra = options.common.id_extra,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding = {.l = gutter_width},
                                        },
                                    });

    auto const button =
        DoBox(builder,
              {
                  .parent = button_outer,
                  .id_extra = options.common.id_extra,
                  .background_fill_colours = BrowserRowColours(options.common.is_selected),
                  .background_fill_auto_hot_active_overlay = false,
                  .layout {
                      .size {
                          layout::k_fill_parent,
                          k_browser_item_height,
                      },
                      .contents_padding {.lr = k_browser_row_pad_x},
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .value_popup = options.common.value_popup,
                  .tooltip = FilterValueTooltip(builder.arena, state, options.common, num_used),
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_placement = TooltipPlacement::RightThenLeft,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    if (options.icon) {
        DoBox(builder,
              {
                  .parent = button,
                  .background_tex = options.icon,
                  .layout {
                      .size = k_library_icon_standard_size,
                      .margins = {.r = 3},
                  },
              });
    }

    DoRowNameAndCount(builder,
                      button,
                      options.display_text.size ? options.display_text : options.common.text,
                      info.total_available,
                      BrowserRowTextColour(num_used != 0),
                      options.font_override.ValueOr(FontType::Body));

    // The lines are drawn rather than laid out: the layout rounds each edge of a box to a pixel on its own,
    // which makes a hairline waver between one and two pixels and leaves gaps where rows meet.
    if (auto const viewport_rect = BoxRect(builder, button_outer); lines.lined && viewport_rect) {
        auto const row = builder.imgui.ViewportRectToWindowRect(*viewport_rect);
        auto const top = Round(row.y);
        auto const bottom = Round(row.y + row.h);
        auto const thickness = Max(1.0f, Round(WwToPixels(k_tree_line_width)));
        auto const line_x = [&](u8 level) {
            return Round(row.x + WwToPixels(lines.inset + (level * k_tree_indent)));
        };
        auto const continues = [&](u8 level) { return (lines.continues & (1 << level)) != 0; };
        auto const colour = [&](u8 level) -> u32 {
            if (level >= lines.gold_from) return ToU32(Col {.c = Col::Highlight});
            return ToU32(Col {.c = Col::Overlay1, .dark_mode = true, .alpha = 100});
        };
        auto& dl = *builder.imgui.draw_list;

        for (auto const level : Range(lines.depth))
            if (continues((u8)level))
                dl.AddRectFilled(Rect {.xywh = {line_x((u8)level), top, thickness, bottom - top}},
                                 colour((u8)level));

        // This row's tick: the line comes down from above, turns into the text, and carries on below unless
        // this is the last sibling.
        auto const x = line_x(lines.depth);
        auto const tick_end = Round(row.x + WwToPixels(gutter_width));
        auto const mid_y = Round(row.y + (row.h / 2));
        auto const col = colour(lines.depth);
        dl.AddRectFilled(Rect {.xywh = {x,
                                        top,
                                        thickness,
                                        continues(lines.depth) ? bottom - top : (mid_y - top) + thickness}},
                         col);
        dl.AddRectFilled(Rect {.xywh = {x, mid_y, tick_end - x, thickness}}, col);
    }

    auto const fired_via_keyboard = key_nav::DoItem(builder,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = button,
                                                        .rect_for_drawing = BoxRect(builder, button_outer),
                                                        .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                        .id = options.common.clicked_key,
                                                        .is_selected = options.common.is_selected,
                                                        .is_tab_item = false,
                                                    });

    if (button.button_fired || fired_via_keyboard) {
        HandleFilterButtonClick(builder, state, options.common);
        if (options.common.is_selected && options.deselect_fallback.filter) {
            options.deselect_fallback.filter->Add(
                options.deselect_fallback.key,
                builder.arena.Clone(options.deselect_fallback.display_name));
        }
    }

    return button;
}

static prefs::Descriptor BrowserModePrefsDescriptor() {
    return {
        .key = "browser-mode"_s,
        .value_requirements =
            prefs::Descriptor::IntRequirements {
                .validator = [](s64& value) { return value >= 0 && value < ToInt(BrowserMode::Count); },
            },
        .default_value = (s64)BrowserMode::Browse,
    };
}

static prefs::Descriptor BrowserFilterModePrefsDescriptor() {
    return {
        .key = "browser-filter-mode"_s,
        .value_requirements =
            prefs::Descriptor::IntRequirements {
                .validator = [](s64& value) { return value >= 0 && value < ToInt(FilterMode::Count); },
            },
        .default_value = (s64)FilterMode::MultipleAnd,
    };
}

static void SetBrowserMode(CommonBrowserState& state, BrowserMode mode, FilterMode filter_mode_for_filter) {
    state.mode = mode;
    switch (mode) {
        case BrowserMode::Browse:
            state.filter_mode = FilterMode::Single;
            // Browse mode has no filter search box: its root is a short list and an attribute page shows
            // every value it has.
            dyn::Clear(state.filter_search);
            break;
        case BrowserMode::Filter: state.filter_mode = filter_mode_for_filter; break;
        case BrowserMode::Count: PanicIfReached();
    }
}

static bool AnyDescendantFolderSelected(CommonBrowserState const& state, FolderNode const* folder) {
    if (!folder) return false;
    auto const& folder_filter = state.Filter(BrowserFilter::Folder);
    for (auto* child = folder->first_child; child; child = child->next)
        if (folder_filter.Contains(child->Hash()) || AnyDescendantFolderSelected(state, child)) return true;
    return false;
}

// Browse mode: the collection that is drilled into is the one selected, or the one containing the selected
// folder.
static bool IsBrowseModeOpenCollection(CommonBrowserState const& state,
                                       FilterCollectionOptions const& options) {
    return options.common.is_selected || AnyDescendantFolderSelected(state, options.folder);
}

// The library icon once it's loaded. Collections without a library have none.
static Optional<ImageID> LoadedCollectionIcon(GuiBuilder& builder, CollectionIconSource const& source) {
    if (!source.library_id) return k_nullopt;
    auto const imgs = GetLibraryImages(source.library_images,
                                       builder.imgui,
                                       *source.library_id,
                                       source.sample_library_server,
                                       source.instance_index,
                                       LibraryImagesTypes::Icon);
    if (imgs.icon && *imgs.icon != k_invalid_image_id) return imgs.icon;
    return k_nullopt;
}

// Library icon, or a two-tone circle placeholder for collections that have no library (e.g. preset folders).
static void DoCollectionIcon(GuiBuilder& builder,
                             Box const& parent,
                             CollectionIconSource const& source,
                             f32 size = k_library_icon_standard_size) {
    if (auto const icon = LoadedCollectionIcon(builder, source)) {
        DoBox(builder,
              {
                  .parent = parent,
                  .background_tex = &*icon,
                  .layout {
                      .size = size,
                  },
              });
    } else if (!source.library_id) {
        auto const placeholder = DoBox(builder,
                                       {
                                           .parent = parent,
                                           .layout {
                                               .size = size,
                                           },
                                       });
        if (auto const rect = BoxRect(builder, placeholder)) {
            auto const window_rect = builder.imgui.ViewportRectToWindowRect(*rect);
            auto const centre = window_rect.Centre();
            auto const radius = window_rect.w * 0.5f;
            auto const split = -k_pi<f32> / 4.0f;
            auto* dl = builder.imgui.draw_list;
            dl->PathArcTo(centre, radius, split, split + k_pi<f32>);
            dl->PathFillConvex(ToU32({.c = Col::Overlay2, .dark_mode = true, .alpha = 90}));
            dl->PathArcTo(centre, radius, split + k_pi<f32>, split + (k_pi<f32> * 2.0f));
            dl->PathFillConvex(ToU32({.c = Col::Overlay1, .dark_mode = true, .alpha = 70}));
        }
    }
}

constexpr f32 k_browse_row_height = k_browser_item_height + 4;

struct BrowseDrillDownRowOptions {
    Box parent;
    u64 id;
    BrowseEntry entry;
    // A collection's row shows its artwork where other rows show entry.icon.
    CollectionIconSource const* collection_icon {};
    bool greyed {}; // Nothing in it to show.
    u64 keyboard_id;
    TooltipString value_popup = k_nullopt;
    RightClickMenuState::Function right_click_menu {};
    String name {};
};

// Browse mode: a row that opens a page. Returns whether it was clicked.
static bool DoBrowseDrillDownRow(GuiBuilder& builder,
                                 CommonBrowserState& state,
                                 BrowseDrillDownRowOptions const& options) {
    auto const& entry = options.entry;
    // Compact rows show only the icon, so the tooltip is where the name goes.
    auto const compact = state.filters_width_tier == BrowserWidthTier::Compact;
    auto const row = DoBox(
        builder,
        {
            .parent = options.parent,
            .id_extra = options.id,
            .background_fill_colours = BrowserRowColours(false),
            .layout {
                .size = {layout::k_fill_parent, k_browse_row_height},
                .contents_padding = {.lr = k_browser_row_pad_x},
                .contents_gap = k_browser_row_pad_x,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .value_popup = options.value_popup,
            .tooltip = compact ? (String)fmt::Format(builder.arena, "{}\n\n{}", entry.name, entry.tooltip)
                               : entry.tooltip,
            .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
            .tooltip_placement = TooltipPlacement::RightThenLeft,
            .button_behaviour = imgui::ButtonConfig {},
            .name = options.name,
        });

    if (options.right_click_menu)
        DoRightClickMenuForBox(builder, state, row, options.id, options.right_click_menu);

    if (options.collection_icon) {
        DoCollectionIcon(builder, row, *options.collection_icon);
    } else {
        DoBox(builder,
              {
                  .parent = row,
                  .text = entry.icon,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.8f,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  .text_justification = TextJustification::Centred,
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = k_library_icon_standard_size,
                  },
              });
    }

    DoRowNameAndCount(builder,
                      row,
                      compact ? String {} : entry.name,
                      entry.count,
                      BrowserRowTextColour(!options.greyed));
    DoRowCaret(builder, row, ICON_FA_CARET_RIGHT, k_font_icons_size * 0.6f);

    auto const fired_via_keyboard = key_nav::DoItem(builder,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = row,
                                                        .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                        .id = options.keyboard_id,
                                                        .is_selected = false,
                                                        .is_tab_item = true,
                                                    });

    if (!row.button_fired && !fired_via_keyboard) return false;
    state.keyboard_navigation.focused_panel = BrowserKeyboardNavigation::Panel::Filters;
    state.scroll_filters_to_start = true;
    return true;
}

// Browse mode, list level: a collection is a row. Clicking it selects the collection, which drills in so
// only that collection is shown.
static void DoBrowseModeCollectionRow(GuiBuilder& builder,
                                      CommonBrowserState& state,
                                      FilterItemInfo const& info,
                                      FilterCollectionOptions const& options) {
    if (DoBrowseDrillDownRow(
            builder,
            state,
            {
                .parent = options.common.parent,
                .id = options.common.id_extra,
                .entry =
                    {
                        .name = options.common.text,
                        .count = info.total_available,
                        .tooltip = fmt::Format(
                            builder.arena,
                            "Show only this {}'s{}. Its folders are then listed here so you can narrow down further.",
                            options.collection_noun,
                            options.all_items_suffix),
                    },
                .collection_icon = &options.icon,
                .greyed = NumUsedForFilter(info, options.common.filter_mode) == 0,
                .keyboard_id = options.common.clicked_key,
                .value_popup = options.common.value_popup,
                .right_click_menu = options.right_click_menu,
                .name = options.name,
            }))
        HandleFilterButtonClick(builder, state, options.common);
}

// Browse mode: where the drill-down is. Each level is a page of its own except a collection, which is
// the selection itself.
enum class BrowseLevel : u8 { Root, CollectionSection, Collection, Attribute, TagCategory };

// A collection is only a level once its section is known. The section is resolved from the selection at
// the start of the frame, so a selection made later in the frame (the locate button, say) reads as the
// root until the next frame rather than as a collection with nowhere to go back to.
static BrowseLevel CurrentBrowseLevel(CommonBrowserState const& state) {
    if (state.browse.open_collection_section)
        return state.browse_collection_open ? BrowseLevel::Collection : BrowseLevel::CollectionSection;
    if (state.browse.open_tag_category) return BrowseLevel::TagCategory;
    if (state.browse.open_attribute) return BrowseLevel::Attribute;
    return BrowseLevel::Root;
}

// The attributes every browser has. Libraries are only one where they aren't collections.
static BrowseEntry CommonAttributeEntry(BrowserFilter filter, u32 count) {
    switch (filter) {
        case BrowserFilter::Library:
            return {
                .name = "Libraries used"_s,
                .icon = ICON_FA_BOOK_OPEN,
                .count = count,
                .tooltip =
                    "Find presets by the library their sounds come from. A preset can use several libraries, so it can appear under more than one."_s,
            };
        case BrowserFilter::LibraryAuthor:
            return {
                .name = "Library authors"_s,
                .icon = ICON_FA_USERS,
                .count = count,
                .tooltip = "Find items by who made their library."_s,
            };
        case BrowserFilter::Tags:
            return {
                .name = "Tags"_s,
                .icon = ICON_FA_TAG,
                .count = count,
                .tooltip =
                    "Find items by tag. Tags are grouped into categories such as Mood and Sound source. Open a category to see its tags."_s,
            };
        case BrowserFilter::Folder:
        case BrowserFilter::CommonCount: break;
    }
    PanicIfReached();
}

// Browse mode, one or more levels down: where a crumb takes you if you click it. The last crumb (where
// you are now) has no action.
enum class BreadcrumbAction : u8 { Home, SectionRoot, AttributeRoot };

struct BreadcrumbSegment {
    String label;
    Optional<BreadcrumbAction> action {};
};

// What a browse level is called, and the icon that goes with it. The same pair names the row that opens
// the level, its crumb in the breadcrumb, and the title at the top of its page.
struct BrowsePageTitle {
    String text {};
    String icon {};
};

static BrowsePageTitle TitleOfEntry(BrowseEntry const& entry) {
    return {.text = entry.name, .icon = entry.icon};
}

// An attribute page's name (e.g. "Tags"), not FilterSelection::name (e.g. "Tag"), which is grammatical
// for chips and summaries instead ("with Tag: Ambient").
static BrowsePageTitle AttributeTitle(BrowserPopupOptions const& options, u8 filter_index) {
    switch ((BrowserFilter)filter_index) {
        case BrowserFilter::Library:
        case BrowserFilter::LibraryAuthor:
        case BrowserFilter::Tags: return TitleOfEntry(CommonAttributeEntry((BrowserFilter)filter_index, 0));
        case BrowserFilter::Folder:
        case BrowserFilter::CommonCount: break;
    }
    for (auto const& attribute : options.extra_browse_attributes)
        if (attribute.filter_index == filter_index) return TitleOfEntry(attribute.entry);
    return {};
}

// The common code owns the libraries collection section rather than the browser, so its id is derived
// rather than given.
static u64 LibrariesCollectionSectionId(u64 browser_id) {
    return browser_id ^ HashFnv1a("libraries-section");
}

static BrowseEntry LibrariesCollectionSectionEntry(u32 count) {
    return {
        .name = "Libraries"_s,
        .icon = ICON_FA_BOOK_OPEN,
        .count = count,
        .tooltip =
            "Your installed libraries, one per row. Open one to see just its items, with its folders to narrow down further."_s,
    };
}

static BrowsePageTitle
CollectionSectionTitle(BrowserPopupContext const& context, BrowserPopupOptions const& options, u64 id) {
    for (auto const& section : options.browse_collection_sections)
        if (section.id == id) return TitleOfEntry(section.entry);
    if (id == LibrariesCollectionSectionId(context.browser_id))
        return TitleOfEntry(LibrariesCollectionSectionEntry(0));
    return {};
}

// Browse mode: the name of the page the filters panel is showing. A drilled-into collection names itself,
// so it has none here. The root has a heading per group of rows instead of one for the whole page.
static BrowsePageTitle CurrentBrowsePageTitle(BrowserPopupContext const& context,
                                              BrowserPopupOptions const& options) {
    auto const& state = context.state;
    switch (CurrentBrowseLevel(state)) {
        case BrowseLevel::Root:
        case BrowseLevel::Collection: return {};
        case BrowseLevel::CollectionSection:
            return CollectionSectionTitle(context, options, *state.browse.open_collection_section);
        case BrowseLevel::Attribute: return AttributeTitle(options, *state.browse.open_attribute);
        case BrowseLevel::TagCategory: {
            auto const category = Tags(*state.browse.open_tag_category);
            return {.text = category.name, .icon = category.font_awesome_icon};
        }
    }
    PanicIfReached();
}

struct BrowsePageTitleOptions {
    Box parent;
    BrowsePageTitle title;
    // A collection's page shows its icon where other pages show a font-awesome one.
    CollectionIconSource const* collection_icon {};
    Optional<u32> count {}; // Right-aligned, like the count on the row that opens the page.
    TooltipString value_popup = k_nullopt;
};

// Browse mode: the title at the top of a page in the filters panel. Mirrors the items list's section
// headings — same font, case and spacing — minus the caret, since a page isn't something you collapse.
static Box DoBrowsePageTitle(GuiBuilder& builder, BrowsePageTitleOptions const& options) {
    auto const row = DoBox(builder,
                           {
                               .parent = options.parent,
                               .id_extra = HashFnv1a(options.title.text),
                               .layout {
                                   .size = {layout::k_fill_parent, k_browser_item_height},
                                   .contents_padding = {.lr = k_browser_row_pad_x},
                                   .contents_gap = k_browser_row_pad_x,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .value_popup = options.value_popup,
                               .tooltip_placement = TooltipPlacement::RightThenLeft,
                           });

    if (options.collection_icon) {
        // Sized to the title text rather than the standard icon size, so the line sits at the same height on
        // every page.
        DoCollectionIcon(builder, row, *options.collection_icon, k_font_heading3_size);
    } else if (options.title.icon.size) {
        DoBox(builder,
              {
                  .parent = row,
                  .text = options.title.icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.7f,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
              });
    }

    DoBox(builder,
          {
              .parent = row,
              .text = UppercaseAscii(builder.arena, options.title.text),
              .size_from_text = false,
              .font = FontType::Heading3,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {layout::k_fill_parent, k_font_heading3_size},
              },
          });

    if (options.count) {
        DoBox(builder,
              {
                  .parent = row,
                  .text = fmt::FormatInline<16>("({})"_s, *options.count),
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
              });
    }

    return row;
}

constexpr usize k_max_breadcrumb_segments = 3;

// Icon font scales for the breadcrumb's cells, shared with the fitting code so that it measures the widths
// that are actually laid out.
constexpr f32 k_breadcrumb_arrow_icon_scale = 0.8f;
constexpr f32 k_breadcrumb_home_icon_scale = 0.75f;

// Widths are in WW. The home crumb (always first) is an icon, so its label width is unused.
struct BreadcrumbLayout {
    // Crumbs between the home crumb and this index are folded into a single '…' crumb.
    usize first_shown_index;
    DynamicArrayBounded<f32, k_max_breadcrumb_segments> label_widths;
};

// Each label takes what it needs up to an equal share of the available width, and whatever the short ones
// leave over is handed back to the long ones, which get ellipsised. Returns the width the ellipsised ones
// ended up with, or nullopt if everything fit.
static Optional<f32> ShareWidth(Span<f32 const> natural_widths, f32 available, Span<f32> out_widths) {
    ASSERT(natural_widths.size == out_widths.size);
    Array<bool, k_max_breadcrumb_segments> settled {};

    auto num_unsettled = natural_widths.size;
    auto remaining = available;
    for (bool settled_one = true; settled_one && num_unsettled;) {
        settled_one = false;
        auto const share = remaining / (f32)num_unsettled;
        for (auto const [index, natural] : Enumerate(natural_widths)) {
            if (settled[index] || natural > share) continue;
            out_widths[index] = natural;
            settled[index] = true;
            remaining -= natural;
            --num_unsettled;
            settled_one = true;
        }
    }

    if (!num_unsettled) return k_nullopt;
    auto const share = Max(remaining / (f32)num_unsettled, 1.0f);
    for (auto const [index, natural] : Enumerate(natural_widths))
        if (!settled[index]) out_widths[index] = share;
    return share;
}

// Fits the trail into the row so it never runs past the right edge: the arrows, home icon, separators and
// cell padding take fixed widths, and the labels share what's left. When that would leave the labels too
// short to read, the earliest levels (never the current one) fold into a single '…' crumb, one at a time,
// until the rest are legible.
static BreadcrumbLayout FitBreadcrumb(GuiBuilder& builder, Span<String const> upper_labels, f32 row_width) {
    auto const pixels_per_ww = builder.state->viewport_cache.pixels_per_ww;
    auto const text_width = [&](String text, FontType font, f32 font_size = 0) {
        return builder.fonts.atlas[ToInt(font)]
                   ->CalcTextSize(text, {.font_size = font_size * pixels_per_ww})
                   .x /
               pixels_per_ww;
    };

    auto const cell_padding = k_browser_spacing * 2;
    // The icon cells are measured rather than estimated: a row wider than the panel puts boxes past its
    // right edge, which grows the auto-sized browser viewport, leaving its background sticking out beyond
    // the panels.
    auto const icon_cell_width = [&](String icon, f32 font_scale) {
        return text_width(icon, FontType::Icons, k_font_icons_size * font_scale) + cell_padding;
    };
    auto const icon_cells_width = icon_cell_width(ICON_FA_ARROW_LEFT, k_breadcrumb_arrow_icon_scale) +
                                  icon_cell_width(ICON_FA_ARROW_RIGHT, k_breadcrumb_arrow_icon_scale) +
                                  icon_cell_width(ICON_FA_HOUSE, k_breadcrumb_home_icon_scale);
    auto const separator_width = text_width("›"_s, FontType::Body);
    auto const folded_cell_width = separator_width + cell_padding + text_width("…"_s, FontType::Heading3);

    DynamicArrayBounded<f32, k_max_breadcrumb_segments> natural_widths {};
    for (auto const label : upper_labels)
        dyn::Append(natural_widths, text_width(label, FontType::Heading3));

    BreadcrumbLayout layout {.first_shown_index = 1};
    dyn::Resize(layout.label_widths, upper_labels.size);
    for (;; ++layout.first_shown_index) {
        auto const first = layout.first_shown_index;
        auto const num_shown = upper_labels.size - first;
        auto const fixed_width = icon_cells_width + (first > 1 ? folded_cell_width : 0) +
                                 ((f32)num_shown * (separator_width + cell_padding));
        auto const share = ShareWidth(natural_widths.Items().SubSpan(first),
                                      row_width - fixed_width,
                                      layout.label_widths.Items().SubSpan(first));
        if (!share || *share >= k_font_heading3_size * 4 || num_shown == 1) break;
    }
    return layout;
}

// The trail from the root to the page you're on now. Only levels that are pages of their own get a crumb:
// selecting a value on a page — a folder inside a collection, a tag on a category page — narrows what the
// page shows without moving you anywhere, so it isn't one. The collection path (a section, then a
// collection) and the attribute path (an attribute, then one of its categories) are mutually exclusive.
// Browse mode's root, as the breadcrumb and the back row name it.
static String BrowseRootName(ArenaAllocator& arena, BrowserPopupOptions const& options) {
    return fmt::Format(arena, "All {}", options.plural_item_type_name);
}

static DynamicArrayBounded<BreadcrumbSegment, k_max_breadcrumb_segments>
BreadcrumbSegments(ArenaAllocator& arena,
                   BrowserPopupContext const& context,
                   BrowserPopupOptions const& options) {
    auto const& state = context.state;
    DynamicArrayBounded<BreadcrumbSegment, k_max_breadcrumb_segments> segments {};
    auto const level = CurrentBrowseLevel(state);
    dyn::Append(segments,
                {.label = BrowseRootName(arena, options),
                 .action = level == BrowseLevel::Root ? k_nullopt
                                                      : Optional<BreadcrumbAction> {BreadcrumbAction::Home}});

    switch (level) {
        case BrowseLevel::Root: break;
        case BrowseLevel::CollectionSection:
        case BrowseLevel::Collection: {
            auto const in_collection = level == BrowseLevel::Collection;
            dyn::Append(
                segments,
                {.label =
                     CollectionSectionTitle(context, options, *state.browse.open_collection_section).text,
                 .action =
                     in_collection ? Optional<BreadcrumbAction> {BreadcrumbAction::SectionRoot} : k_nullopt});
            if (in_collection)
                dyn::Append(segments,
                            {.label = options.browse_scope.collection ? options.browse_scope.collection->name
                                                                      : String {}});
            break;
        }
        case BrowseLevel::Attribute:
        case BrowseLevel::TagCategory: {
            auto const in_category = level == BrowseLevel::TagCategory;
            dyn::Append(segments,
                        {.label = AttributeTitle(options, *state.browse.open_attribute).text,
                         .action = in_category ? Optional<BreadcrumbAction> {BreadcrumbAction::AttributeRoot}
                                               : k_nullopt});
            if (in_category) dyn::Append(segments, {.label = Tags(*state.browse.open_tag_category).name});
            break;
        }
    }

    return segments;
}

static void ApplyBreadcrumbAction(CommonBrowserState& state, BreadcrumbAction action) {
    switch (action) {
        case BreadcrumbAction::Home:
            state.ClearAll();
            state.browse = {};
            break;
        case BreadcrumbAction::SectionRoot: state.ClearAll(); break;
        case BreadcrumbAction::AttributeRoot:
            if (auto const attribute = state.browse.open_attribute) state.filters[*attribute].Clear();
            state.browse.open_tag_category = k_nullopt;
            break;
    }
    state.scroll_items_to_start = true;
    state.scroll_filters_to_start = true;
}

// Everything the breadcrumb is derived from, so that any change of level is detectable.
static u64 BrowseNavigationHash(CommonBrowserState const& state) {
    auto hash = HashInit();
    for (auto const [filter_index, filter] : Enumerate<u64>(state.filters)) {
        HashUpdate(hash, filter_index);
        filter.ForEachSelected([&](String, u64 key) {
            HashUpdate(hash, key);
            return LoopControl::Continue;
        });
    }
    HashUpdate(hash, state.browse.open_attribute.ValueOr((u8)k_max_browser_filters));
    HashUpdate(hash, state.browse.open_collection_section.ValueOr(0));
    HashUpdate(hash, (u8)state.browse.open_tag_category.ValueOr(TagCategory::Count));
    return hash;
}

static constexpr u8 k_browse_place_version = 1;
static constexpr u64 k_browser_browse_place_store_id = HashFnv1a("browser-browse-place");

Span<u8 const> EncodeBrowsePlace(CommonBrowserState const& state, ArenaAllocator& arena) {
    DynamicArray<u8> out {arena};
    auto const write = [&]<Arithmetic T>(T value) {
        dyn::AppendSpan(out, Span<u8 const> {(u8 const*)&value, sizeof(T)});
    };
    write(k_browse_place_version);
    write(state.browse.open_attribute.ValueOr((u8)k_max_browser_filters));
    write(state.browse.open_collection_section.ValueOr(0));
    write((u8)state.browse.open_tag_category.ValueOr(TagCategory::Count));

    u16 num_selected = 0;
    for (auto const& filter : state.filters) {
        filter.ForEachSelected([&](String, u64) {
            ++num_selected;
            return LoopControl::Continue;
        });
    }
    write(num_selected);
    for (auto const [filter_index, filter] : Enumerate<u8>(state.filters)) {
        filter.ForEachSelected([&](String display_name, u64 key) {
            // Only a hash carries its name in the selection; tags and bools are named from the key.
            auto const name = filter.data.tag == FilterSelection::Type::Hashes ? display_name : String {};
            ASSERT(name.size <= FilterSelection::DisplayName::Capacity());
            write(filter_index);
            write(key);
            write((u8)name.size);
            dyn::AppendSpan(out, name.ToConstByteSpan());
            return LoopControl::Continue;
        });
    }
    return out.ToOwnedSpan();
}

bool DecodeBrowsePlace(Span<u8 const> data, CommonBrowserState& state) {
    usize pos = 0;
    auto const read = [&]<Arithmetic T>(T& out) {
        if (pos + sizeof(T) > data.size) return false;
        __builtin_memcpy_inline(&out, data.data + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    };

    u8 version;
    if (!read(version) || version != k_browse_place_version) return false;

    u8 open_attribute;
    u64 open_collection_section;
    u8 open_tag_category;
    if (!read(open_attribute) || !read(open_collection_section) || !read(open_tag_category)) return false;
    CommonBrowserState::BrowseLocation location {};
    if (open_attribute != k_max_browser_filters) {
        if (open_attribute >= state.filters.size) return false;
        location.open_attribute = open_attribute;
    }
    if (open_collection_section) location.open_collection_section = open_collection_section;
    if (open_tag_category != (u8)TagCategory::Count) {
        if (open_tag_category > (u8)TagCategory::Count) return false;
        location.open_tag_category = (TagCategory)open_tag_category;
    }

    auto filters = state.filters;
    for (auto& filter : filters)
        filter.Clear();
    u16 num_selected;
    if (!read(num_selected)) return false;
    for (auto _ : Range((usize)num_selected)) {
        u8 filter_index;
        u64 key;
        u8 name_size;
        if (!read(filter_index) || !read(key) || !read(name_size)) return false;
        if (filter_index >= filters.size) return false;
        if (name_size > FilterSelection::DisplayName::Capacity()) return false;
        if (pos + name_size > data.size) return false;
        String const name {(char const*)data.data + pos, name_size};
        pos += name_size;

        auto& filter = filters[filter_index];
        switch (filter.data.tag) {
            case FilterSelection::Type::Hashes: break;
            case FilterSelection::Type::Tags:
                if (key != k_untagged_key && key >= ToInt(TagType::Count)) return false;
                break;
            case FilterSelection::Type::Bool:
                if (key != 1) return false;
                break;
        }
        filter.Add(key, name);
    }
    if (pos != data.size) return false;

    state.browse = location;
    state.filters = filters;
    return true;
}

static void BrowseBack(CommonBrowserState& state, BreadcrumbAction action) {
    if (state.browse_forward_levels.size == state.browse_forward_levels.Capacity())
        dyn::Remove(state.browse_forward_levels, 0);
    dyn::Append(state.browse_forward_levels, {.filters = state.filters, .location = state.browse});
    ApplyBreadcrumbAction(state, action);
    state.browse_navigation_hash = BrowseNavigationHash(state);
}

static void BrowseForward(CommonBrowserState& state) {
    auto const& level = Last(state.browse_forward_levels);
    state.filters = level.filters;
    state.browse = level.location;
    dyn::Pop(state.browse_forward_levels);
    state.browse_navigation_hash = BrowseNavigationHash(state);
    state.scroll_items_to_start = true;
    state.scroll_filters_to_start = true;
}

// Browse mode: back and forward arrows, then a clickable trail from the root to here. A house icon
// starts the trail, '›' separates crumbs, and every crumb but the last (where you are now) jumps
// straight to that level.
static void DoBrowseBreadcrumb(GuiBuilder& builder,
                               CommonBrowserState& state,
                               Box const& parent,
                               f32 row_width,
                               Span<BreadcrumbSegment const> segments,
                               String home_tooltip) {
    // Every crumb (clickable or not) shares the same font, size, case and padding. The home crumb is the
    // icon alone.
    DynamicArrayBounded<String, k_max_breadcrumb_segments> upper_labels {};
    for (auto const [index, segment] : Enumerate(segments))
        dyn::Append(upper_labels, index ? UppercaseAscii(builder.arena, segment.label) : String {});
    auto const fit = FitBreadcrumb(builder, upper_labels, row_width);

    // Every element owns its own horizontal padding rather than the row spacing them out, so hit boxes run
    // full-height and edge-to-edge, and a crumb's text sits in the same place whether or not it's a button.
    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .layout {
                                   .size = {layout::k_fill_parent, k_browser_item_height},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                               .name = "browser.breadcrumb-row"_s,
                           });

    auto const do_icon = [&](Box const& parent,
                             String icon,
                             Colours colours,
                             f32 font_scale = k_breadcrumb_home_icon_scale,
                             u64 id_extra = 0) {
        DoBox(builder,
              {
                  .parent = parent,
                  .id_extra = id_extra,
                  .text = icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * font_scale,
                  .text_colours = colours,
                  .parent_dictates_hot_and_active = true,
              });
    };

    auto const do_cell = [&](u64 id_extra, bool clickable, TooltipString tooltip) {
        return DoBox(builder,
                     {
                         .parent = row,
                         .id_extra = id_extra,
                         .background_fill_auto_hot_active_overlay = clickable,
                         .round_background_corners = 0b1111,
                         .corner_rounding = k_corner_rounding,
                         .layout {
                             .size = {layout::k_hug_contents, k_browser_item_height},
                             .contents_padding = {.lr = k_browser_spacing},
                             .contents_align = layout::Alignment::Middle,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                         },
                         .tooltip = tooltip,
                         .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                         .tooltip_placement = TooltipPlacement::RightThenLeft,
                         .button_behaviour =
                             clickable ? Optional<imgui::ButtonConfig> {imgui::ButtonConfig {}} : k_nullopt,
                         .name = "browser.breadcrumb"_s,
                     });
    };

    auto const clickable_colours = Colours {ColSet {
        .base = Col {.c = Col::Subtext0, .dark_mode = true},
        .hot = Col {.c = Col::Text, .dark_mode = true},
        .active = Col {.c = Col::Text, .dark_mode = true},
    }};

    auto const do_arrow_button =
        [&](String icon, String tooltip, bool enabled, u64 id_extra, u64 key_nav_id) {
            auto const button = do_cell(id_extra, enabled, tooltip);
            do_icon(button,
                    icon,
                    enabled ? clickable_colours
                            : Colours {Col {.c = Col::Subtext0, .dark_mode = true, .alpha = 60}},
                    k_breadcrumb_arrow_icon_scale);

            bool fired_via_keyboard = false;
            if (enabled) {
                fired_via_keyboard = key_nav::DoItem(builder,
                                                     state.keyboard_navigation,
                                                     {
                                                         .box = button,
                                                         .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                         .id = key_nav_id,
                                                         .is_selected = false,
                                                         .is_tab_item = true,
                                                     });
            }
            return enabled && (button.button_fired || fired_via_keyboard);
        };

    // The last crumb is where you are; the one before it is the level above.
    auto const back_action =
        segments.size >= 2 ? segments[segments.size - 2].action : Optional<BreadcrumbAction> {};
    if (do_arrow_button(ICON_FA_ARROW_LEFT,
                        back_action ? "Back up a level"_s : "Already at the top level"_s,
                        back_action.HasValue(),
                        HashFnv1a("back"),
                        HashFnv1a("breadcrumb-back")))
        BrowseBack(state, *back_action);

    auto const can_go_forward = state.browse_forward_levels.size != 0;
    if (do_arrow_button(ICON_FA_ARROW_RIGHT,
                        can_go_forward ? "Forward to the level you stepped back from"_s
                                       : "Forward is only available after stepping back"_s,
                        can_go_forward,
                        HashFnv1a("forward"),
                        HashFnv1a("breadcrumb-forward")))
        BrowseForward(state);

    auto const handle_crumb_click = [&](Box const& crumb, u64 id_extra, BreadcrumbAction action) {
        auto const fired_via_keyboard =
            key_nav::DoItem(builder,
                            state.keyboard_navigation,
                            {
                                .box = crumb,
                                .panel = BrowserKeyboardNavigation::Panel::Filters,
                                .id = HashFnv1a("breadcrumb") ^ id_extra,
                                .is_selected = false,
                                .is_tab_item = true,
                            });
        if (crumb.button_fired || fired_via_keyboard) ApplyBreadcrumbAction(state, action);
    };

    auto const do_separator = [&](u64 id_extra) {
        DoBox(builder,
              {
                  .parent = row,
                  .id_extra = id_extra,
                  .text = "›"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
              });
    };

    // The current crumb (last) is brightest; the ones you can jump to are dimmer, underlined, and brighten
    // on hover. A label wider than its width gets ellipsised; no width means the text sizes itself.
    auto const do_label_crumb = [&](u64 id_extra,
                                    String text,
                                    Optional<f32> width,
                                    Optional<BreadcrumbAction> action,
                                    TooltipString tooltip) {
        auto const clickable = action.HasValue();
        auto const crumb = do_cell(id_extra, clickable, tooltip);
        DoBox(builder,
              {
                  .parent = crumb,
                  .text = text,
                  .size_from_text = !width.HasValue(),
                  .font = FontType::Heading3,
                  .text_colours =
                      clickable ? clickable_colours : Colours {Col {.c = Col::Text, .dark_mode = true}},
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .border_colours = Colours {ColSet {
                      .base = Col {.c = clickable ? Col::Overlay1 : Col::None, .dark_mode = true},
                      .hot = Col {.c = clickable ? Col::Subtext0 : Col::None, .dark_mode = true},
                      .active = Col {.c = clickable ? Col::Subtext0 : Col::None, .dark_mode = true},
                  }},
                  .parent_dictates_hot_and_active = true,
                  .border_edges = 0b0001,
                  .layout {
                      .size = {width.ValueOr(0), k_font_heading3_size},
                  },
              });
        if (clickable) handle_crumb_click(crumb, id_extra, *action);
    };

    {
        auto const& home = segments[0];
        auto const clickable = home.action.HasValue();
        auto const crumb =
            do_cell(0, clickable, clickable ? TooltipString {home_tooltip} : TooltipString {k_nullopt});
        do_icon(crumb,
                ICON_FA_HOUSE,
                clickable ? clickable_colours : Colours {Col {.c = Col::Text, .dark_mode = true}});
        if (clickable) handle_crumb_click(crumb, 0, *home.action);
    }

    // The folded crumb stands in for the hidden levels and jumps to the nearest of them: the parent of the
    // first crumb shown after it.
    if (fit.first_shown_index > 1) {
        auto const hidden = segments.SubSpan(1, fit.first_shown_index - 1);
        DynamicArray<char> tooltip {builder.arena};
        dyn::AppendSpan(tooltip, "Back to "_s);
        for (auto const [hidden_index, hidden_segment] : Enumerate(hidden)) {
            if (hidden_index) dyn::AppendSpan(tooltip, " › "_s);
            dyn::AppendSpan(tooltip, hidden_segment.label);
        }
        do_separator(HashFnv1a("folded"));
        do_label_crumb(HashFnv1a("folded"),
                       "…"_s,
                       k_nullopt,
                       Last(hidden).action,
                       TooltipString {String {tooltip.ToOwnedSpan()}});
    }

    for (auto const index : Range(fit.first_shown_index, segments.size)) {
        auto const& segment = segments[index];
        do_separator(index);
        do_label_crumb(index,
                       upper_labels[index],
                       fit.label_widths[index],
                       segment.action,
                       segment.action ? TooltipString {"Back to this level"_s} : TooltipString {k_nullopt});
    }
}

// Browse mode's root: one row per place you can drill into. Sections of collections (libraries, preset
// banks) come first, then the flat attributes.
struct BrowseRootRow {
    u64 id; // A collection section's own id, or a hash of the attribute's filter index.
    BrowseEntry entry;
    // Unset for a collection section, whose page lists collections rather than one filter's values.
    Optional<u8> attribute_filter_index {};
    RightClickMenuState::Function right_click_menu {};
};

constexpr usize k_max_browse_root_rows = k_max_browse_collection_sections + k_max_browser_filters;

struct BrowseRootRowList {
    DynamicArrayBounded<BrowseRootRow, k_max_browse_root_rows> rows {};
    usize num_collection_sections {}; // The first rows; the rest are attributes.
};

static void
DoBrowseRootRow(GuiBuilder& builder, CommonBrowserState& state, Box const& parent, BrowseRootRow const& row) {
    if (!DoBrowseDrillDownRow(builder,
                              state,
                              {
                                  .parent = parent,
                                  .id = row.id,
                                  .entry = row.entry,
                                  .keyboard_id = HashFnv1a("browse-root") ^ row.id,
                                  .right_click_menu = row.right_click_menu,
                              }))
        return;
    if (auto const attribute = row.attribute_filter_index)
        state.browse.open_attribute = *attribute;
    else
        state.browse.open_collection_section = row.id;
}

// Browse mode's root: the whole panel is this one list, which is what keeps its scrolling simple. The
// collection sections come first under their own heading, then the attributes under theirs. The
// collection sections are the usual way in; the attributes are there for when you know what you're after.
static void DoBrowseRootRows(GuiBuilder& builder,
                             CommonBrowserState& state,
                             Box const& parent,
                             BrowseRootRowList const& list) {
    for (auto const [index, row] : Enumerate(list.rows)) {
        if (index == 0 && list.num_collection_sections)
            DoBrowsePageTitle(builder, {.parent = parent, .title = {.text = "Browse by collection"_s}});
        if (index == list.num_collection_sections) {
            if (index) {
                DoBox(builder,
                      {
                          .parent = parent,
                          .layout {.size = {layout::k_fill_parent, k_browser_spacing}},
                      });
            }
            DoBrowsePageTitle(builder, {.parent = parent, .title = {.text = "Browse by attribute"_s}});
        }
        DoBrowseRootRow(builder, state, parent, row);
    }
}

// Filter mode: a collection is a tree. The header is its collapsible top node; the "All" row and the
// folders are the leaves beneath it.
static void DoFilterModeCollection(GuiBuilder& builder,
                                   CommonBrowserState& state,
                                   FilterItemInfo const& info,
                                   FilterCollectionOptions const& options) {
    auto const is_selected = options.common.is_selected;
    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const collapse_id = CollectionCollapseId(options.common.clicked_key);
    auto& collection_toggled_ids =
        options.default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
    if (options.store) LoadCollapseStateFromStore(*options.store, collection_toggled_ids, collapse_id);
    bool collapsed = Contains(collection_toggled_ids, collapse_id) != options.default_collapsed;

    // For certain screenshots, force a collection to be expanded.
    if (collapsed && options.name == "browser.library.Lost Reveries"_s &&
        IsScreenshotRequest("browser-full"_s))
        collapsed = false;

    auto const collection = DoBox(builder,
                                  {
                                      .parent = options.common.parent,
                                      .id_extra = options.common.id_extra,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_padding = {.l = k_tree_indent},
                                          .contents_direction = layout::Direction::Column,
                                          .contents_align = layout::Alignment::Start,
                                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                      },
                                      .name = options.name,
                                  });

    auto const header = DoBox(
        builder,
        {
            .parent = collection,
            .background_fill_colours = BrowserRowColours(false),
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lr = k_browser_row_pad_x, .tb = 2},
                .contents_gap = k_browser_row_pad_x,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .value_popup = options.common.value_popup,
            .tooltip = (String)fmt::Format(
                builder.arena,
                "Expand to see the {}'s folders. The All row selects the whole {}, and the rows below it select single folders.",
                options.collection_noun,
                options.collection_noun),
            .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
            .tooltip_placement = TooltipPlacement::RightThenLeft,
            .button_behaviour = imgui::ButtonConfig {},
            .name =
                options.name.size ? (String)fmt::Format(builder.arena, "{}.header", options.name) : String {},
        });

    if (options.right_click_menu) {
        DoRightClickMenuForBox(builder, state, header, options.common.clicked_key, options.right_click_menu);

        if (options.name == "preset-browser.first-bank"_s && IsScreenshotRequest("uninstall-preset-bank"_s) &&
            !builder.imgui.IsPopupMenuOpen(k_right_click_menu_popup_id)) {
            if (auto const rect = BoxRect(builder, header)) {
                auto const window_rect = builder.imgui.ViewportRectToWindowRect(*rect);
                state.right_click_menu_state.absolute_creator_rect = window_rect;
                state.right_click_menu_state.do_menu = options.right_click_menu;
                state.right_click_menu_state.item_hash = options.common.clicked_key;
                builder.imgui.OpenPopupMenu(k_right_click_menu_popup_id, header.imgui_id);
            }
        }
    }

    DoRowCaret(builder, header, collapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN, k_tree_caret_width);
    DoCollectionIcon(builder, header, options.icon);
    DoRowNameAndCount(builder,
                      header,
                      options.common.text,
                      info.total_available,
                      BrowserRowTextColour(num_used != 0));

    auto const fired_via_keyboard = key_nav::DoItem(builder,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = header,
                                                        .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                        .id = collapse_id,
                                                        .is_selected = is_selected,
                                                        .is_tab_item = true,
                                                    });

    if (header.button_fired || fired_via_keyboard) {
        if (Contains(collection_toggled_ids, collapse_id))
            dyn::RemoveValue(collection_toggled_ids, collapse_id);
        else
            dyn::Append(collection_toggled_ids, collapse_id);

        if (options.store) SaveCollapseStateToStore(*options.store, collection_toggled_ids, collapse_id);
    }

    if (collapsed) return;

    // The gap at the foot closes the open collection before the next one's header.
    auto const body =
        DoBox(builder,
              {
                  .parent = collection,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.b = k_tree_inner_gap},
                      .contents_direction = layout::Direction::Column,
                  },
                  .name = options.name.size ? (String)fmt::Format(builder.arena, "{}.body", options.name)
                                            : String {},
              });

    // Top-level rows have no lines; their text sits on the collection's content column.
    TreeLines const lines {
        .inset = k_tree_content_inset - k_browser_row_pad_x,
        .gold_from = is_selected ? (u8)0 : TreeLines::k_no_gold,
    };

    // "All" leaf: selects the root node (all children).
    DoFilterTreeButton(
        builder,
        state,
        info,
        {
            .common =
                {
                    .parent = body,
                    .id_extra = options.common.id_extra,
                    .is_selected = is_selected,
                    .text = fmt::Format(builder.arena,
                                        "All {}{}"_s,
                                        options.common.text,
                                        options.all_items_suffix),
                    .match_phrase = fmt::Format(builder.arena, "in this {}", options.collection_noun),
                    .filter = options.common.filter,
                    .clicked_key = options.common.clicked_key,
                    .filter_mode = options.common.filter_mode,
                },
            .lines = lines,
            .font_override = FontType::BodyItalic,
            .display_text = fmt::Format(builder.arena, "All{}"_s, options.all_items_suffix),
        });

    if (options.folder) {
        FolderFilterTreeContext const context {.folder_infos = options.folder_infos, .lines = lines};
        FolderFilterTreeOptions const folder_options {
            .do_right_click_menu = options.right_click_menu,
            .parent_collection_is_selected = is_selected,
        };
        DoFolderFilterChildren(builder, state, body, options.folder->first_child, context, folder_options);
    }
}

// Browse mode, inside a collection: the collection is the whole page. Its name is the page title, in the
// same style as every other level's, and everything the collection says about itself hangs off that title
// as a value popup. The title sits above the filters' scroll region, so it's drawn from the browse scope
// rather than by the collection itself, which draws only its body in the region.
static void DoBrowseOpenCollectionTitle(GuiBuilder& builder,
                                        BrowserPopupContext& context,
                                        BrowserCollection const& collection,
                                        Box const& parent) {
    CollectionIconSource const icon {
        .library_id = collection.library_id,
        .library_images = context.library_images,
        .sample_library_server = context.sample_library_server,
        .instance_index = context.instance_index,
    };
    auto const header = DoBrowsePageTitle(builder,
                                          {
                                              .parent = parent,
                                              .title = {.text = collection.name},
                                              .collection_icon = &icon,
                                              .count = collection.num_items,
                                              .value_popup = ({
                                                  DynamicArray<char> buf {builder.arena};
                                                  dyn::AppendSpan(buf, collection.subtext);
                                                  if (collection.version) {
                                                      if (buf.size) dyn::AppendSpan(buf, " · "_s);
                                                      fmt::Append(buf, "v1.{}"_s, *collection.version);
                                                  }
                                                  if (collection.description.size) {
                                                      if (buf.size) dyn::AppendSpan(buf, "\n\n"_s);
                                                      dyn::AppendSpan(buf, collection.description);
                                                  }
                                                  buf.size ? TooltipString {(String)buf.ToOwnedSpan()}
                                                           : TooltipString {k_nullopt};
                                              }),
                                          });

    if (collection.right_click_menu)
        DoRightClickMenuForBox(builder, context.state, header, collection.key, collection.right_click_menu);
}

static void DoBrowseModeOpenCollection(GuiBuilder& builder,
                                       CommonBrowserState& state,
                                       FilterCollectionOptions const& options) {
    auto const body = DoBox(builder,
                            {
                                .parent = options.common.parent,
                                .id_extra = options.common.id_extra,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_fill_or_hug},
                                    .contents_padding = {.tb = k_tree_inner_gap},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                                .name = options.name,
                            });

    bool any_folders = false;
    if (options.folder) {
        // Top-level folders run from the panel edge, rather than reading as indented under the page title.
        // Their subfolders hang from their text like anywhere else.
        FolderFilterTreeContext const context {.folder_infos = options.folder_infos};
        FolderFilterTreeOptions const folder_options {
            .do_right_click_menu = options.right_click_menu,
            // The collection being selected is the default state, not a choice, so only actually selected
            // folders light the lines beneath them.
            .excluded_ancestor = options.folder,
            .deselect_fallback =
                {
                    .filter = &options.common.filter,
                    .key = options.common.clicked_key,
                    .display_name = options.common.text,
                },
            .collection_noun = options.collection_noun,
        };
        any_folders = DoFolderFilterChildren(builder,
                                             state,
                                             body,
                                             options.folder->first_child,
                                             context,
                                             folder_options);
    }

    // The collection is the whole page here, so an empty body would otherwise just look like something
    // failed to load.
    if (!any_folders) {
        DoBox(builder,
              {
                  .parent = body,
                  .text = fmt::Format(builder.arena, "This {} has no subfolders.", options.collection_noun),
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
                  .layout {
                      .margins = {.lr = k_browser_row_pad_x, .tb = 3},
                  },
              });
    }
}

void DoFilterCollection(GuiBuilder& builder,
                        CommonBrowserState& state,
                        FilterItemInfo const& info,
                        FilterCollectionOptions const& options) {
    switch (state.mode) {
        case BrowserMode::Browse:
            if (!state.browse_collection_open)
                DoBrowseModeCollectionRow(builder, state, info, options);
            else if (IsBrowseModeOpenCollection(state, options))
                DoBrowseModeOpenCollection(builder, state, options);
            break;
        case BrowserMode::Filter: DoFilterModeCollection(builder, state, info, options); break;
        case BrowserMode::Count: PanicIfReached();
    }
}

BrowserSection::Result BrowserSection::Do(GuiBuilder& builder) {
    if (!init) {
        if (skip_heading) {
            is_collapsed = 0;
        } else {
            auto& toggled_ids =
                default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
            if (store) LoadCollapseStateFromStore(*store, toggled_ids, id);
            is_collapsed = Contains(toggled_ids, id) != default_collapsed;
            // Screenshots show the whole tree, whatever collapse state the user has saved.
            if (IsAnyScreenshotInProgress()) is_collapsed = 0;
        }
        init = true;
    } else {
        if (is_collapsed) return State::Collapsed;
    }

    if (is_box_init) return box_cache;

    builder.imgui.PushId(id);
    DEFER { builder.imgui.PopId(); };

    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .layout =
                                         {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .margins = {.t = subsection ? k_tree_inner_gap : 0},
                                             .contents_padding = {.l = subsection ? k_tree_indent : 0},
                                             .contents_direction = layout::Direction::Column,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                 });

    if (!skip_heading && (heading || folder)) {
        auto const heading_container = DoBox(
            builder,
            {
                .parent = container,
                .background_fill_auto_hot_active_overlay = true,
                .layout {
                    .size = {layout::k_fill_parent, k_browser_item_height},
                    .contents_padding = {.lr = k_browser_row_pad_x},
                    .contents_gap = k_browser_row_pad_x,
                    .contents_direction = layout::Direction::Row,
                    .contents_align = layout::Alignment::Start,
                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                },
                .tooltip =
                    folder ? TooltipString {"Collapse or expand this folder.\n\nTip: hold " MODIFIER_KEY_NAME
                                            " and press the up or down arrow to jump between folders."_s}
                           : k_nullopt,
                .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                .tooltip_placement = tooltip_placement,
                .button_behaviour = imgui::ButtonConfig {},
            });
        heading_box = heading_container;

        auto const heading_fired_via_keyboard =
            keyboard_focusable ? key_nav::DoItem(builder,
                                                 state.keyboard_navigation,
                                                 {
                                                     .box = heading_container,
                                                     .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                     .id = id,
                                                     .is_selected = false,
                                                     .is_tab_item = true,
                                                 })
                               : false;

        if (heading_container.button_fired || heading_fired_via_keyboard) {
            auto& toggled_ids =
                default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
            if (Contains(toggled_ids, id))
                dyn::RemoveValue(toggled_ids, id);
            else
                dyn::Append(toggled_ids, id);

            if (store) SaveCollapseStateToStore(*store, toggled_ids, id);
        }

        if (right_click_menu) DoRightClickMenuForBox(builder, state, heading_container, id, right_click_menu);

        DoRowCaret(builder,
                   heading_container,
                   is_collapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN,
                   k_tree_caret_width,
                   dark_mode);

        if (icon) {
            DoBox(builder,
                  {
                      .parent = heading_container,
                      .text = *icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_font_icons_size * 0.7f,
                      .text_colours = Col {.c = Col::Subtext0, .dark_mode = dark_mode},
                  });
        }

        {
            DynamicArray<char> buf {builder.arena};

            String text = heading.ValueOr({});

            if (capitalise) {
                text = UppercaseAscii(builder.arena, text);
            } else if (folder) {
                DynamicArrayBounded<String, sample_lib::k_max_folders + 1> parts;
                for (auto f = folder; f; f = f->parent)
                    dyn::Append(parts, f->display_name.size ? f->display_name : f->name);

                if (skip_root_folder && parts.size > 1) dyn::Pop(parts);

                // We want to display the last part in a less prominent way. Drilled in, it's the library or
                // bank already shown in the filters panel, so it's dropped.
                Optional<String> top_folder_name {};
                if (parts.size > 1) {
                    if (!state.browse_collection_open) top_folder_name = Last(parts);
                    dyn::Pop(parts);
                }

                auto const last_index = (s32)parts.size - 1;
                for (s32 part_index = last_index; part_index >= 0; --part_index) {
                    if (part_index != last_index) dyn::AppendSpan(buf, " / "_s);
                    for (auto const c : parts[(usize)part_index])
                        dyn::Append(buf, ToUppercaseAscii(c));
                }

                if (top_folder_name) {
                    dyn::AppendSpan(buf, " ("_s);
                    dyn::AppendSpan(buf, *top_folder_name);
                    dyn::AppendSpan(buf, ")"_s);
                }

                text = buf;
            }

            if (text.size) {
                DoBox(builder,
                      {
                          .parent = heading_container,
                          .text = text,
                          .font = FontType::Heading3,
                          .text_colours = Col {.c = Col::Subtext0, .dark_mode = dark_mode},
                          .text_overflow = TextOverflowType::ShowDotsOnRight,
                          .parent_dictates_hot_and_active = true,
                          .layout {
                              .size = {layout::k_fill_parent, k_font_heading3_size},
                          },
                      });
            }
        }

        if (is_collapsed) return State::Collapsed;
    }

    is_box_init = true;

    if (!multiline_contents) {
        box_cache = container;
        return box_cache;
    }

    box_cache = DoBox(builder,
                      {
                          .parent = container,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_padding = {.l = k_tree_content_inset,
                                                   .r = k_browser_row_pad_x,
                                                   .tb = k_tree_inner_gap},
                              .contents_gap = k_tree_inner_gap,
                              .contents_direction = layout::Direction::Row,
                              .contents_multiline = true,
                              .contents_align = layout::Alignment::Start,
                          },
                      });
    return box_cache;
}

Optional<Box> SectionContents(GuiBuilder& builder, BrowserSection& section) {
    if (section.Do(builder) == BrowserSection::State::Collapsed) return k_nullopt;
    return section.Do(builder).Get<Box>();
}

static void DoLibraryRightClickMenu(GuiBuilder& builder,
                                    BrowserPopupContext& context,
                                    RightClickMenuState const& menu_state,
                                    LibraryFilters const& library_filters) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    auto const find_library = [&](u64 library_hash) -> Optional<sample_lib::LibraryId> {
        if (library_filters.libraries.Find(library_hash)) return library_hash;
        return k_nullopt;
    };

    if (MenuItem(builder,
                 root,
                 {
                     .text = fmt::Format(builder.arena, "Open Folder in {}", GetFileBrowserAppName()),
                     .is_selected = false,
                     .no_icon_gap = true,
                 })
            .button_fired) {
        if (auto const lib_id = find_library(menu_state.item_hash)) {
            auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, *lib_id);
            DEFER { lib.Release(); };

            if (lib)
                if (auto const dir = path::Directory(lib->path)) OpenFolderInFileBrowser(*dir);
        }
    }

    if (MenuItem(builder,
                 root,
                 {
                     .text = "Uninstall (Send library to " TRASH_NAME ")",
                     .is_selected = false,
                     .no_icon_gap = true,
                 })
            .button_fired) {
        if (auto const lib_id = find_library(menu_state.item_hash)) {
            auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, *lib_id);
            DEFER { lib.Release(); };

            if (lib) {
                builder.imgui.CloseTopModal();
                UninstallSampleLibrary(builder.imgui,
                                       *lib,
                                       library_filters.confirmation_dialog_state,
                                       library_filters.error_notifications,
                                       library_filters.notifications);
            }
        }
    }
}

// Calls f(lib_id, lib_info, lib_hash, lib) for each library the browser can list: installed, and matching
// the filter search. f returns LoopControl.
template <typename F>
static void
ForEachListedLibrary(CommonBrowserState const& state, LibraryFilters const& library_filters, F&& f) {
    for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
        ASSERT(lib_id);
        auto const lib_ptr = library_filters.libraries_table.Find(lib_id, lib_hash);
        if (!lib_ptr) continue;
        auto const& lib = *lib_ptr;
        if (!MatchesFilterSearch(lib->name, state.filter_search)) continue;
        if (f(lib_id, lib_info, lib_hash, lib) == LoopControl::Break) break;
    }
}

static u32 NumListedLibraries(CommonBrowserState const& state, LibraryFilters const& library_filters) {
    u32 count = 0;
    ForEachListedLibrary(state, library_filters, [&](auto const&, auto const&, u64, auto const&) {
        ++count;
        return LoopControl::Continue;
    });
    if (library_filters.additional_pseudo_collection) ++count;
    return count;
}

static RightClickMenuState::Function LibraryRightClickMenu(u64 lib_hash) {
    if (lib_hash == sample_lib::k_builtin_library_id) return nullptr;
    return [](GuiBuilder& builder, BrowserPopupContext& context, BrowserPopupOptions const& options) {
        if (options.library_filters)
            DoLibraryRightClickMenu(builder,
                                    context,
                                    context.state.right_click_menu_state,
                                    *options.library_filters);
    };
}

static String LibraryDescription(ArenaAllocator& arena, sample_lib::Library const& lib) {
    DynamicArray<char> buf {arena};
    if (lib.description) fmt::Append(buf, "{}\n\n", lib.description);
    fmt::Append(buf, "{} is a library by {}.", lib.name, lib.author);
    return buf.ToOwnedSpan();
}

BrowserCollection LibraryCollection(ArenaAllocator& arena, sample_lib::Library const& lib, u32 num_items) {
    return {
        .filter = BrowserFilter::Library,
        .key = lib.id,
        .name = lib.name,
        .library_id = lib.id,
        .num_items = num_items,
        .subtext = arena.Clone(lib.tagline),
        .version = lib.revision,
        .description = LibraryDescription(arena, lib),
        .right_click_menu = LibraryRightClickMenu(lib.id),
    };
}

// The libraries as collections. get_parent gives the box to put each one in: Browse mode's page, or
// Filter mode's section, which gives nothing back when collapsed.
static void DoLibraryCollections(GuiBuilder& builder,
                                 BrowserPopupContext& context,
                                 LibraryFilters const& library_filters,
                                 TrivialFunctionRef<Optional<Box>()> get_parent) {
    ForEachListedLibrary(
        context.state,
        library_filters,
        [&](auto const& lib_id, FilterItemInfo const& lib_info, u64 lib_hash, auto const& lib) {
            auto const parent = get_parent();
            if (!parent) return LoopControl::Break;

            DoFilterCollection(
                builder,
                context.state,
                lib_info,
                FilterCollectionOptions {
                    .common =
                        {
                            .parent = *parent,
                            .id_extra = lib_hash,
                            .is_selected = context.state.Filter(BrowserFilter::Library).Contains(lib_hash),
                            .text = lib->name,
                            .value_popup = LibraryDescription(builder.arena, *lib),
                            .filter = context.state.Filter(BrowserFilter::Library),
                            .clicked_key = lib_hash,
                            .filter_mode = context.state.filter_mode,
                        },
                    .icon =
                        {
                            .library_id = lib_id,
                            .library_images = library_filters.library_images,
                            .sample_library_server = context.sample_library_server,
                            .instance_index = library_filters.instance_index,
                        },
                    .folder_infos = library_filters.folders,
                    .folder = &lib->root_folders[ToInt(library_filters.resource_type)],
                    .all_items_suffix = library_filters.resource_type == sample_lib::ResourceType::Instrument
                                            ? " Instruments"_s
                                            : " IRs"_s,
                    .collection_noun = "library"_s,
                    .default_collapsed = true,
                    .right_click_menu = LibraryRightClickMenu(lib_hash),
                    .store = &context.store,
                    .name = library_filters.collection_name_prefix.size
                                ? (String)fmt::Format(builder.arena,
                                                      "{}{}",
                                                      library_filters.collection_name_prefix,
                                                      lib->name)
                                : String {},
                });
            return LoopControl::Continue;
        });

    if (library_filters.additional_pseudo_collection) {
        auto options = *library_filters.additional_pseudo_collection;
        if (!MatchesFilterSearch(options.common.text, context.state.filter_search)) return;
        auto const parent = get_parent();
        if (!parent) return;
        options.common.parent = *parent;
        DoFilterCollection(builder,
                           context.state,
                           ({
                               FilterItemInfo i {};
                               if (library_filters.additional_pseudo_collection_info)
                                   i = *library_filters.additional_pseudo_collection_info;
                               i;
                           }),
                           options);
    }
}

// The libraries as plain values, where the item merely uses them.
static void DoLibraryValues(GuiBuilder& builder,
                            BrowserPopupContext& context,
                            LibraryFilters const& library_filters,
                            TrivialFunctionRef<Optional<Box>()> get_parent) {
    ForEachListedLibrary(
        context.state,
        library_filters,
        [&](auto const& lib_id, FilterItemInfo const& lib_info, u64 lib_hash, auto const& lib) {
            auto const parent = get_parent();
            if (!parent) return LoopControl::Break;

            auto const imgs = GetLibraryImages(library_filters.library_images,
                                               builder.imgui,
                                               lib_id,
                                               context.sample_library_server,
                                               library_filters.instance_index,
                                               LibraryImagesTypes::Icon);

            auto const button = DoFilterButton(
                builder,
                context.state,
                lib_info,
                FilterButtonOptions {
                    .common =
                        {
                            .parent = *parent,
                            .id_extra = lib_hash,
                            .is_selected = context.state.Filter(BrowserFilter::Library).Contains(lib_hash),
                            .text = lib->name,
                            .value_popup = FunctionRef<String()>([&]() -> String {
                                auto lib =
                                    sample_lib_server::FindLibraryRetained(context.sample_library_server,
                                                                           lib_id);
                                DEFER { lib.Release(); };

                                DynamicArray<char> buf {builder.arena};
                                fmt::Append(buf, "{} by {}.", lib->name, lib->author);
                                if (lib) {
                                    if (lib->description) fmt::Append(buf, "\n\n{}", lib->description);
                                } else {
                                    fmt::Append(
                                        buf,
                                        "\n\nThis library is not installed, but some presets require it.");
                                }
                                return buf.ToOwnedSpan();
                            }),
                            .match_phrase = "from this library"_s,
                            .filter = context.state.Filter(BrowserFilter::Library),
                            .clicked_key = lib_hash,
                            .filter_mode = context.state.filter_mode,
                        },
                    .icon = imgs.icon.NullableValue(),
                });

            if (auto const menu = LibraryRightClickMenu(lib_hash))
                DoRightClickMenuForBox(builder, context.state, button, lib_hash, menu);
            return LoopControl::Continue;
        });
}

// Filter mode: the libraries section, holding the libraries as collections or as values.
static void DoFilterTreeLibraries(GuiBuilder& builder,
                                  BrowserPopupContext& context,
                                  Box const& parent,
                                  LibraryFilters const& library_filters) {
    if (!library_filters.libraries.size) return;

    BrowserSection section = {
        .state = context.state,
        .id = LibrariesCollectionSectionId(context.browser_id),
        .parent = parent,
        .heading = library_filters.collection_view ? LibrariesCollectionSectionEntry(0).name
                                                   : CommonAttributeEntry(BrowserFilter::Library, 0).name,
        .icon = ICON_FA_BOOK_OPEN,
        .capitalise = true,
        .multiline_contents = !library_filters.collection_view,
        .default_collapsed = !library_filters.collection_view,
        .dark_mode = true,
        .keyboard_focusable = true,
        .store = &context.store,
    };

    auto const get_parent = [&]() -> Optional<Box> { return SectionContents(builder, section); };
    if (library_filters.collection_view)
        DoLibraryCollections(builder, context, library_filters, get_parent);
    else
        DoLibraryValues(builder, context, library_filters, get_parent);
}

// Browse mode's root: the collection sections, then the flat attributes, each one a row that opens a
// page. Anything with nothing in it is left out.
static BrowseRootRowList BrowseRootRows(BrowserPopupContext const& context,
                                        BrowserPopupOptions const& options) {
    BrowseRootRowList result {};

    for (auto const& section : options.browse_collection_sections) {
        if (!section.entry.count) continue;
        dyn::Append(result.rows,
                    {.id = section.id, .entry = section.entry, .right_click_menu = section.right_click_menu});
    }

    if (options.library_filters && options.library_filters->collection_view) {
        if (auto const count = NumListedLibraries(context.state, *options.library_filters)) {
            dyn::Append(result.rows,
                        {
                            .id = LibrariesCollectionSectionId(context.browser_id),
                            .entry = LibrariesCollectionSectionEntry(count),
                        });
        }
    }

    result.num_collection_sections = result.rows.size;

    auto const add_attribute = [&](u8 filter_index, BrowseEntry const& entry) {
        if (!entry.count) return;
        dyn::Append(result.rows,
                    {
                        .id = HashFnv1a("browse-attribute") ^ filter_index,
                        .entry = entry,
                        .attribute_filter_index = filter_index,
                    });
    };

    if (options.tags_filters) {
        auto const num_tags = (u32)options.tags_filters->available_tags.NumSet() +
                              (options.tags_filters->has_untagged ? 1u : 0u);
        add_attribute((u8)BrowserFilter::Tags, CommonAttributeEntry(BrowserFilter::Tags, num_tags));
    }

    if (options.library_filters) {
        add_attribute((u8)BrowserFilter::LibraryAuthor,
                      CommonAttributeEntry(BrowserFilter::LibraryAuthor,
                                           (u32)options.library_filters->library_authors.size));
    }

    for (auto const& attribute : options.extra_browse_attributes)
        add_attribute(attribute.filter_index, attribute.entry);

    // Where libraries aren't collections the item merely uses them, so they're the last way of looking at
    // things.
    if (options.library_filters && !options.library_filters->collection_view) {
        add_attribute(
            (u8)BrowserFilter::Library,
            CommonAttributeEntry(BrowserFilter::Library, (u32)options.library_filters->libraries.size));
    }

    return result;
}

// Browse mode: the collections of the open section, as rows on the section's own page, or just the one
// collection once it's been drilled into.
static void DoBrowseOpenCollectionSection(GuiBuilder& builder,
                                          BrowserPopupContext& context,
                                          BrowserPopupOptions const& options,
                                          Box const& root) {
    auto const open = *context.state.browse.open_collection_section;

    for (auto const& section : options.browse_collection_sections) {
        if (section.id != open) continue;
        section.do_collections(builder, root);
        return;
    }

    if (options.library_filters && options.library_filters->collection_view &&
        open == LibrariesCollectionSectionId(context.browser_id))
        DoLibraryCollections(builder, context, *options.library_filters, [&]() -> Optional<Box> {
            return root;
        });
}

static void DoLibraryAuthorValues(GuiBuilder& builder,
                                  BrowserPopupContext& context,
                                  LibraryFilters const& library_filters,
                                  TrivialFunctionRef<Optional<Box>()> get_parent) {
    for (auto const [author, author_info, author_hash] : library_filters.library_authors) {
        if (!MatchesFilterSearch(author, context.state.filter_search)) continue;
        auto const parent = get_parent();
        if (!parent) break;
        DoFilterButton(
            builder,
            context.state,
            author_info,
            {
                .common =
                    {
                        .parent = *parent,
                        .id_extra = author_hash,
                        .is_selected =
                            context.state.Filter(BrowserFilter::LibraryAuthor).Contains(author_hash),
                        .text = author,
                        .match_phrase = "from libraries by this author"_s,
                        .filter = context.state.Filter(BrowserFilter::LibraryAuthor),
                        .clicked_key = author_hash,
                        .filter_mode = context.state.filter_mode,
                    },
            });
    }
}

static void DoFilterTreeLibraryAuthors(GuiBuilder& builder,
                                       BrowserPopupContext& context,
                                       Box const& parent,
                                       LibraryFilters const& library_filters) {
    if (!library_filters.library_authors.size) return;

    BrowserSection section = {
        .state = context.state,
        .id = context.browser_id ^ HashFnv1a("library-authors-section"),
        .parent = parent,
        .heading = CommonAttributeEntry(BrowserFilter::LibraryAuthor, 0).name,
        .icon = ICON_FA_USERS,
        .capitalise = true,
        .multiline_contents = true,
        .default_collapsed = true,
        .dark_mode = true,
        .keyboard_focusable = true,
        .store = &context.store,
    };

    DoLibraryAuthorValues(builder, context, library_filters, [&]() -> Optional<Box> {
        return SectionContents(builder, section);
    });
}

using TagsByCategory = OrderedHashTable<TagCategory, OrderedHashTable<TagType, FilterItemInfo>>;

static TagsByCategory GroupTagsByCategory(ArenaAllocator& arena, TagsFilters const& tags_filters) {
    TagsByCategory result {};
    tags_filters.available_tags.ForEachSetBit([&](usize bit) {
        auto const tag = (TagType)bit;
        auto const tag_and_cat = LookupTagName(GetTagInfo(tag).name);
        if (tag_and_cat) {
            auto& tags_for_category =
                result.FindOrInsertGrowIfNeeded(arena, tag_and_cat->category, {}).element.data;
            tags_for_category.InsertGrowIfNeeded(arena, tag, tags_filters.tags[bit]);
        }
    });
    return result;
}

static void DoTagValues(GuiBuilder& builder,
                        BrowserPopupContext& context,
                        OrderedHashTable<TagType, FilterItemInfo> const& tags,
                        TrivialFunctionRef<Optional<Box>()> get_parent) {
    for (auto const [tag, filter_item_info, _] : tags) {
        auto const tag_info = GetTagInfo(tag);
        if (!MatchesFilterSearch(tag_info.name, context.state.filter_search)) continue;
        auto const parent = get_parent();
        if (!parent) break;
        DoFilterButton(
            builder,
            context.state,
            filter_item_info,
            {
                .common =
                    {
                        .parent = *parent,
                        .id_extra = (u64)tag,
                        .is_selected = context.state.Filter(BrowserFilter::Tags).Contains((u64)tag),
                        .text = tag_info.name,
                        .match_phrase = "with this tag"_s,
                        .filter = context.state.Filter(BrowserFilter::Tags),
                        .clicked_key = (u64)tag,
                        .filter_mode = context.state.filter_mode,
                    },
            });
    }
}

static void DoUntaggedValue(GuiBuilder& builder,
                            BrowserPopupContext& context,
                            TagsFilters const& tags_filters,
                            TrivialFunctionRef<Optional<Box>()> get_parent) {
    if (!MatchesFilterSearch(k_untagged_tag_name, context.state.filter_search)) return;
    auto const parent = get_parent();
    if (!parent) return;
    DoFilterButton(
        builder,
        context.state,
        tags_filters.untagged_info,
        {
            .common =
                {
                    .parent = *parent,
                    .id_extra = k_untagged_key,
                    .is_selected = context.state.Filter(BrowserFilter::Tags).Contains(k_untagged_key),
                    .text = k_untagged_tag_name,
                    .match_phrase = "with no tags"_s,
                    .filter = context.state.Filter(BrowserFilter::Tags),
                    .clicked_key = k_untagged_key,
                    .filter_mode = context.state.filter_mode,
                },
        });
}

// Filter mode: the tags section, with a subsection per category and the untagged pill beneath them.
static void DoFilterTreeTags(GuiBuilder& builder,
                             BrowserPopupContext& context,
                             Box const& parent,
                             TagsFilters const& tags_filters) {
    if (!tags_filters.available_tags.AnyValuesSet() && !tags_filters.has_untagged) return;

    auto grouped = GroupTagsByCategory(builder.arena, tags_filters);

    BrowserSection tags_section {
        .state = context.state,
        .id = context.browser_id ^ HashFnv1a("tags-section"),
        .parent = parent,
        .heading = CommonAttributeEntry(BrowserFilter::Tags, 0).name,
        .icon = ICON_FA_TAG,
        .capitalise = true,
        .multiline_contents = false,
        .default_collapsed = true,
        .dark_mode = true,
        .keyboard_focusable = true,
        .store = &context.store,
    };

    for (auto [category, tags_for_category, category_hash] : grouped) {
        auto const category_info = Tags(category);

        BrowserSection category_section {
            .state = context.state,
            .id = context.browser_id ^ HashFnv1a("tags-section") ^ category_hash,
            .parent = {}, // IMPORTANT: set later
            .heading = category_info.name,
            .icon = category_info.font_awesome_icon,
            .capitalise = true,
            .multiline_contents = true,
            .subsection = true,
            .default_collapsed = true,
            .dark_mode = true,
            .keyboard_focusable = true,
            .store = &context.store,
        };

        DoTagValues(builder, context, tags_for_category, [&]() -> Optional<Box> {
            auto const outer = SectionContents(builder, tags_section);
            if (!outer) return k_nullopt;
            category_section.parent = *outer;
            return SectionContents(builder, category_section);
        });
    }

    // Untagged has no category. The pill gets the same inset and stand-off as a section's box of pills.
    if (tags_filters.has_untagged) {
        DoUntaggedValue(builder, context, tags_filters, [&]() -> Optional<Box> {
            auto const outer = SectionContents(builder, tags_section);
            if (!outer) return k_nullopt;
            return DoBox(builder,
                         {
                             .parent = *outer,
                             .layout {
                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                 .contents_padding = {.l = k_tree_content_inset,
                                                      .r = k_browser_row_pad_x,
                                                      .tb = k_tree_inner_gap},
                                 .contents_align = layout::Alignment::Start,
                             },
                         });
        });
    }
}

// Browse mode's tags page: a list of categories, each a row that opens a page of just its tags. Untagged
// has no category, so it sits below the category list rather than on any category's page.
static void DoBrowseTagsPage(GuiBuilder& builder,
                             BrowserPopupContext& context,
                             Box const& root,
                             TagsFilters const& tags_filters) {
    auto grouped = GroupTagsByCategory(builder.arena, tags_filters);
    auto const into_root = [&]() -> Optional<Box> { return root; };

    if (auto const open_category = context.state.browse.open_tag_category) {
        if (auto const tags = grouped.Find(*open_category)) DoTagValues(builder, context, *tags, into_root);
        return;
    }

    for (auto [category, tags_for_category, category_hash] : grouped) {
        auto const category_info = Tags(category);
        if (DoBrowseDrillDownRow(builder,
                                 context.state,
                                 {
                                     .parent = root,
                                     .id = category_hash,
                                     .entry =
                                         {
                                             .name = category_info.name,
                                             .icon = category_info.font_awesome_icon,
                                             .count = (u32)tags_for_category.size,
                                         },
                                     .keyboard_id = HashFnv1a("browse-tag-category") ^ category_hash,
                                 }))
            context.state.browse.open_tag_category = category;
    }

    if (tags_filters.has_untagged) DoUntaggedValue(builder, context, tags_filters, into_root);
}

// Browse mode: the page of one attribute's values.
static void DoBrowseAttributePage(GuiBuilder& builder,
                                  BrowserPopupContext& context,
                                  BrowserPopupOptions const& options,
                                  Box const& root) {
    auto const filter_index = *context.state.browse.open_attribute;
    auto const into_root = [&]() -> Optional<Box> { return root; };

    if (filter_index < (u8)BrowserFilter::CommonCount) {
        switch ((BrowserFilter)filter_index) {
            case BrowserFilter::Library:
                if (options.library_filters)
                    DoLibraryValues(builder, context, *options.library_filters, into_root);
                break;
            case BrowserFilter::LibraryAuthor:
                if (options.library_filters)
                    DoLibraryAuthorValues(builder, context, *options.library_filters, into_root);
                break;
            case BrowserFilter::Tags:
                if (options.tags_filters) DoBrowseTagsPage(builder, context, root, *options.tags_filters);
                break;
            case BrowserFilter::Folder:
            case BrowserFilter::CommonCount: PanicIfReached();
        }
        return;
    }

    for (auto const& attribute : options.extra_browse_attributes)
        if (attribute.filter_index == filter_index) attribute.do_values(builder, root);
}

// Browse mode: the filters panel shows one page, a gapless menu of rows.
static void DoBrowsePage(GuiBuilder& builder,
                         BrowserPopupContext& context,
                         BrowserPopupOptions const& options,
                         BrowseRootRowList const& root_rows) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    switch (CurrentBrowseLevel(context.state)) {
        case BrowseLevel::Root: DoBrowseRootRows(builder, context.state, root, root_rows); break;
        case BrowseLevel::CollectionSection:
        case BrowseLevel::Collection: DoBrowseOpenCollectionSection(builder, context, options, root); break;
        case BrowseLevel::Attribute:
        case BrowseLevel::TagCategory: DoBrowseAttributePage(builder, context, options, root); break;
    }
}

// Filter mode: the whole tree at once, its top-level branches spaced apart. The collections keep their
// place at the top.
static void
DoFilterTree(GuiBuilder& builder, BrowserPopupContext& context, BrowserPopupOptions const& options) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_gap = k_tree_inner_gap * 2,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    if (options.do_extra_filters_top) options.do_extra_filters_top(builder, root);

    if (options.library_filters && options.library_filters->collection_view)
        DoFilterTreeLibraries(builder, context, root, *options.library_filters);

    if (options.tags_filters) DoFilterTreeTags(builder, context, root, *options.tags_filters);

    if (options.library_filters) DoFilterTreeLibraryAuthors(builder, context, root, *options.library_filters);

    if (options.do_extra_filters_bottom) options.do_extra_filters_bottom(builder, root);

    if (options.library_filters && !options.library_filters->collection_view)
        DoFilterTreeLibraries(builder, context, root, *options.library_filters);
}

static String FilterModeTextAbbreviated(FilterMode mode) {
    switch (mode) {
        case FilterMode::Single: return "One";
        case FilterMode::MultipleAnd: return "AND";
        case FilterMode::MultipleOr: return "OR";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static String BrowserModeText(BrowserMode mode) {
    switch (mode) {
        case BrowserMode::Browse: return "Browse";
        case BrowserMode::Filter: return "Filter";
        case BrowserMode::Count: break;
    }
    PanicIfReached();
}

static String
BrowserModeDescription(ArenaAllocator& arena, BrowserMode mode, BrowserPopupOptions const& options) {
    switch (mode) {
        case BrowserMode::Browse:
            return fmt::Format(arena,
                               "One {}, folder or tag at a time.",
                               options.browse_scope.collection_noun);
        case BrowserMode::Filter: return "Combine multiple filters to narrow down the results.";
        case BrowserMode::Count: break;
    }
    PanicIfReached();
}

static String
BrowserModeTooltip(ArenaAllocator& arena, BrowserMode mode, BrowserPopupOptions const& options) {
    switch (mode) {
        case BrowserMode::Browse:
            return fmt::Format(
                arena,
                "Switch to Browse mode. Start from a short list of places to look and drill down, one {}, folder or tag at a time. Only one of your selected filters is kept.",
                options.browse_scope.collection_noun);
        case BrowserMode::Filter:
            return fmt::Format(
                arena,
                "Switch to Filter mode. Every {}, folder and tag is shown at once as a tree, so you can combine several, such as a tag within one {}.",
                options.browse_scope.collection_noun,
                options.browse_scope.collection_noun);
        case BrowserMode::Count: break;
    }
    PanicIfReached();
}

static String FilterModeMenuText(FilterMode mode) {
    switch (mode) {
        case FilterMode::Single: return "One";
        case FilterMode::MultipleAnd: return "Match all (AND)";
        case FilterMode::MultipleOr: return "Match any (OR)";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static String FilterModeDescription(FilterMode mode) {
    switch (mode) {
        case FilterMode::Single: return "Only one filter can be selected at a time.";
        case FilterMode::MultipleAnd: return "Items must match all selected filters.";
        case FilterMode::MultipleOr: return "Items can match any selected filter.";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

// Browse mode shows libraries and preset banks as collections at its root; every other filter lives in an
// attribute page.
static bool IsCollectionFilter(BrowserPopupOptions const& options, usize filter_index) {
    if (filter_index == (usize)BrowserFilter::Folder) return true;
    if (filter_index == (usize)BrowserFilter::Library)
        return options.library_filters && options.library_filters->collection_view;
    return false;
}

static Optional<u8> SelectedNonCollectionFilterIndex(CommonBrowserState const& state,
                                                     BrowserPopupOptions const& options) {
    for (auto const [index, filter] : Enumerate(state.filters))
        if (filter.HasSelected() && !IsCollectionFilter(options, index)) return (u8)index;
    return k_nullopt;
}

static void
SwitchBrowserMode(BrowserPopupContext& context, BrowserPopupOptions const& options, BrowserMode mode) {
    prefs::SetValue(context.preferences, BrowserModePrefsDescriptor(), (s64)mode);
    context.state.browse = {};
    if (mode == BrowserMode::Browse) {
        context.state.ClearToOne();
        // Browse mode can only show one thing at a time, and the survivor might be a tag or an author
        // rather than a collection, in which case its attribute page is where it lives. A surviving
        // collection opens its own section, which is worked out from the selection each frame.
        context.state.browse.open_attribute = SelectedNonCollectionFilterIndex(context.state, options);
    }
    SetBrowserMode(context.state,
                   mode,
                   (FilterMode)prefs::GetInt(context.preferences, BrowserFilterModePrefsDescriptor()));
    context.state.scroll_filters_to_start = true;
}

static void DoBrowserModeToggle(GuiBuilder& builder,
                                BrowserPopupContext& context,
                                BrowserPopupOptions const& options,
                                Box const& parent) {
    auto const container =
        DoBox(builder,
              {
                  .parent = parent,
                  .background_fill_colours = Col {.c = Col::Background2, .dark_mode = true},
                  .round_background_corners = 0b1111,
                  .corner_rounding = k_corner_rounding,
                  .layout {
                      .size = layout::k_hug_contents,
                      .contents_padding = {.lrtb = 2},
                      .contents_gap = 2,
                      .contents_direction = layout::Direction::Row,
                  },
                  .name = "browser.mode-toggle"_s,
              });

    for (auto const mode : EnumIterator<BrowserMode>()) {
        auto const is_selected = context.state.mode == mode;

        auto const segment = DoBox(
            builder,
            {
                .parent = container,
                .id_extra = (u64)mode,
                .background_fill_colours = is_selected ? Colours {Col {.c = Col::Surface1, .dark_mode = true}}
                                                       : Colours {Col {.c = Col::None}},
                .background_fill_auto_hot_active_overlay = !is_selected,
                .round_background_corners = 0b1111,
                .corner_rounding = k_corner_rounding,
                .layout {
                    .size = {k_browser_item_height * 1.3f, k_browser_item_height - 4},
                    .contents_align = layout::Alignment::Middle,
                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                },
                .tooltip = BrowserModeTooltip(builder.arena, mode, options),
                .button_behaviour = imgui::ButtonConfig {},
            });

        DoBox(builder,
              {
                  .parent = segment,
                  .text = mode == BrowserMode::Browse ? ICON_FA_LIST : ICON_FA_FILTER,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.7f,
                  .text_colours = Col {.c = is_selected ? Col::Text : Col::Subtext0, .dark_mode = true},
                  .parent_dictates_hot_and_active = true,
              });

        if (segment.button_fired && !is_selected) SwitchBrowserMode(context, options, mode);
    }
}

static Box DoBrowserMenuRoot(GuiBuilder& builder, String name) {
    return DoBox(builder,
                 {
                     .layout {
                         .size = layout::k_hug_contents,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                     },
                     .name = name,
                 });
}

static void DoMatchModeMenuItems(GuiBuilder& builder, BrowserPopupContext& context, Box const& root) {
    for (auto const mode : EnumIterator<FilterMode>()) {
        if (MenuItem(builder,
                     root,
                     {
                         .text = FilterModeMenuText(mode),
                         .subtext = FilterModeDescription(mode),
                         .is_selected = context.state.filter_mode == mode,
                     },
                     SourceLocationHash() ^ (u64)mode)
                .button_fired) {
            prefs::SetValue(context.preferences, BrowserFilterModePrefsDescriptor(), (s64)mode);
            if (mode == FilterMode::Single && context.state.filter_mode != FilterMode::Single)
                context.state.ClearToOne();
            SetBrowserMode(context.state, BrowserMode::Filter, mode);
        }
    }
}

static Box ToolbarIconButton(GuiBuilder& builder,
                             Box parent,
                             String icon,
                             TooltipString tooltip,
                             bool dark_mode,
                             u64 id_extra = SourceLocationHash()) {
    auto const button = DoBox(builder,
                              {
                                  .parent = parent,
                                  .id_extra = id_extra,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .round_background_corners = 0b1111,
                                  .layout {
                                      .size = {k_browser_item_height, k_browser_item_height},
                                      .contents_align = layout::Alignment::Middle,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                                  .tooltip = tooltip,
                                  .button_behaviour = imgui::ButtonConfig {},
                              });

    DoBox(builder,
          {
              .parent = button,
              .text = icon,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.8f,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = dark_mode},
              .parent_dictates_hot_and_active = true,
          });

    return button;
}

// The narrow-panel stand-in for the mode toggle and the match button: one button, and a menu with the
// same choices as items.
static void DoBrowserToolbarOverflowMenu(GuiBuilder& builder,
                                         BrowserPopupContext& context,
                                         BrowserPopupOptions const& options,
                                         Box const& parent) {
    auto const button = ToolbarIconButton(builder,
                                          parent,
                                          ICON_FA_ELLIPSIS,
                                          "Choose Browse or Filter mode, and how filters combine."_s,
                                          true);
    auto const popup_id = builder.imgui.MakeId("toolbar-overflow");
    if (button.button_fired) builder.imgui.OpenPopupMenu(popup_id, button.imgui_id);
    if (!builder.imgui.IsPopupMenuOpen(popup_id)) return;

    DoBoxViewport(
        builder,
        {
            .run =
                [&](GuiBuilder& builder) {
                    auto const root = DoBrowserMenuRoot(builder, "browser.toolbar-overflow-menu"_s);
                    for (auto const mode : EnumIterator<BrowserMode>()) {
                        auto const is_selected = context.state.mode == mode;
                        if (MenuItem(
                                builder,
                                root,
                                {
                                    .text =
                                        (String)fmt::Format(builder.arena, "{} mode", BrowserModeText(mode)),
                                    .subtext = BrowserModeDescription(builder.arena, mode, options),
                                    .is_selected = is_selected,
                                },
                                SourceLocationHash() ^ (u64)mode)
                                .button_fired &&
                            !is_selected)
                            SwitchBrowserMode(context, options, mode);
                    }
                    if (context.state.mode == BrowserMode::Filter) {
                        MenuDivider(builder, root);
                        DoMatchModeMenuItems(builder, context, root);
                    }
                },
            .bounds = button,
            .imgui_id = popup_id,
            .viewport_config = k_default_popup_menu_viewport,
            .debug_name = "toolbar-overflow",
        });
}

struct ToolbarSearchOptions {
    Box parent;
    imgui::Id text_input_id;
    DynamicArrayBounded<char, 100>& text;
    String placeholder;
    TooltipString tooltip;
    bool dark_mode;
};

// The search is open while the input has focus or there's text, so an active search never hides behind
// the icon.
static bool ToolbarSearchIsOpen(GuiBuilder& builder, imgui::Id text_input_id, String text) {
    return builder.imgui.TextInputHasFocus(text_input_id) || text.size;
}

// The width a toolbar must keep free for its search, gap included: the icon button when closed, else
// enough for a handful of characters beside the icon and clear button. The search is laid out to fill the
// row, so its neighbours must leave it at least this much or it shrinks below use - and to nothing if they
// take the whole row.
static f32 ToolbarSearchWidth(GuiBuilder& builder, imgui::Id text_input_id, String text) {
    return (k_browser_spacing / 2) +
           (ToolbarSearchIsOpen(builder, text_input_id, text) ? 80.0f : k_browser_item_height);
}

// A magnifying-glass button that opens into a search box. Place it last in its toolbar: it grows into the
// free space without moving the buttons before it.
static void DoToolbarSearch(GuiBuilder& builder, ToolbarSearchOptions const& options) {
    auto const dm = options.dark_mode;
    auto const id = options.text_input_id;

    if (!ToolbarSearchIsOpen(builder, id, options.text)) {
        if (ToolbarIconButton(builder, options.parent, ICON_FA_MAGNIFYING_GLASS, options.tooltip, dm)
                .button_fired) {
            builder.imgui.SetTextInputFocus(id, options.text, false);
            builder.imgui.TextInputSelectAll();
        }
        return;
    }

    auto const box = DoBox(builder,
                           {
                               .parent = options.parent,
                               .background_fill_colours = Col {.c = Col::Background2, .dark_mode = dm},
                               .round_background_corners = 0b1111,
                               .layout {
                                   .size = {layout::k_fill_parent, k_browser_item_height},
                                   .contents_padding = {.lr = k_browser_spacing / 2},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    DoBox(builder,
          {
              .parent = box,
              .text = ICON_FA_MAGNIFYING_GLASS,
              .size_from_text = true,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.8f,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = dm},
          });

    auto const text_input = DoBox(builder,
                                  {
                                      .parent = box,
                                      .layout {
                                          .size = {layout::k_fill_parent, k_browser_item_height},
                                      },
                                      .tooltip = options.tooltip,
                                  });

    if (auto const r = BoxRect(builder, text_input)) {
        auto const result = builder.imgui.TextInputBehaviour({
            .rect_in_window_coords = builder.imgui.RegisterAndConvertRect(*r),
            .id = id,
            .text = (String)options.text,
            .placeholder_text = options.placeholder,
            .input_cfg =
                {
                    .x_padding = WwToPixels(4.0f),
                    .centre_align = false,
                    .escape_unfocuses = true,
                    .select_all_when_opening = true,
                    .multiline = false,
                },
            .button_cfg =
                {
                    .mouse_button = MouseButton::Left,
                    .event = MouseButtonEvent::Down,
                },
        });

        DrawTextInput(builder.imgui,
                      result,
                      {
                          .text_col = {.c = Col::Text, .dark_mode = dm},
                          .cursor_col = {.c = Col::Text, .dark_mode = dm},
                          .selection_col = {.c = Col::Highlight, .dark_mode = dm, .alpha = 128},
                      });

        if (result.buffer_changed) {
            dyn::AssignFitInCapacity(options.text, result.text);
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }
    }

    if (auto const r = BoxRect(builder, box); r && builder.imgui.TextInputHasFocus(id))
        key_nav::DrawFocusBox(builder, *r);

    if (options.text.size) {
        auto const clear_button =
            DoBox(builder,
                  {
                      .parent = box,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1111,
                      .layout {
                          .size = {k_browser_item_height - 4, k_browser_item_height - 4},
                          .contents_align = layout::Alignment::Middle,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                      .tooltip = "Clear the search."_s,
                      .button_behaviour = imgui::ButtonConfig {},
                  });
        DoBox(builder,
              {
                  .parent = clear_button,
                  .text = ICON_FA_XMARK,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.8f,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = dm},
                  .parent_dictates_hot_and_active = true,
              });
        if (clear_button.button_fired) dyn::Clear(options.text);
    }
}

// The line under the results, mirroring the breadcrumb's place at the foot of the filters panel: a sentence
// naming everything that narrows the list. Each narrowing is a button that removes it.
static void DoResultsFooter(GuiBuilder& builder,
                            BrowserPopupContext& context,
                            BrowserPopupOptions const& options,
                            Box const& parent) {
    auto& state = context.state;
    auto const row = DoBox(
        builder,
        {
            .parent = parent,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lr = k_browser_spacing},
                // The segments are words, so the gap between them is a space.
                .contents_gap =
                    {PixelsToWw(builder.fonts.atlas[ToInt(FontType::BodyItalic)]->CalcTextSize(" "_s, {}).x),
                     2},
                .contents_direction = layout::Direction::Row,
                .contents_multiline = true,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .name = "browser.results-summary"_s,
        });

    u64 segment_index = 0;

    auto const do_words = [&](String text) {
        DoBox(builder,
              {
                  .parent = row,
                  .id_extra = segment_index++,
                  .text = text,
                  .size_from_text = true,
                  .size_from_text_preserve_height = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Subtext0},
                  .text_justification = TextJustification::CentredLeft,
                  .layout {.size = {1, k_browser_item_height}},
              });
    };

    // Reads on from the words around it; hovering shows it as a button, and the cross removes it.
    auto const do_removable = [&](String text, TooltipString tooltip) -> bool {
        auto const button = DoBox(builder,
                                  {
                                      .parent = row,
                                      .id_extra = segment_index++,
                                      .background_fill_colours =
                                          ColSet {
                                              .base = Col {.c = Col::Background2},
                                              .hot = Col {.c = Col::Surface0},
                                              .active = Col {.c = Col::Surface1},
                                          },
                                      .round_background_corners = 0b1111,
                                      .corner_rounding = k_corner_rounding,
                                      .layout {
                                          .size = {layout::k_hug_contents, k_browser_item_height - 4},
                                          .contents_padding = {.lr = 3},
                                          .contents_gap = 3,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Middle,
                                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                      },
                                      .tooltip = tooltip,
                                      .button_behaviour = imgui::ButtonConfig {},
                                  });
        DoBox(builder,
              {
                  .parent = button,
                  .text = text,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours =
                      ColSet {
                          .base = Col {.c = Col::Subtext0},
                          .hot = Col {.c = Col::Text},
                          .active = Col {.c = Col::Text},
                      },
                  .parent_dictates_hot_and_active = true,
              });
        DoBox(builder,
              {
                  .parent = button,
                  .text = ICON_FA_XMARK,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.6f,
                  .text_colours =
                      ColSet {
                          .base = Col {.c = Col::Overlay1},
                          .hot = Col {.c = Col::Text},
                          .active = Col {.c = Col::Text},
                      },
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = k_font_icons_size * 0.5f,
                  },
              });
        return button.button_fired;
    };

    do_words("Showing"_s);

    // Favourites and the search refine whatever else is selected, so they come first.
    bool any_refinement = false;
    if (state.favourites.HasSelected()) {
        if (do_removable("favourites"_s, "Stop showing only favourites."_s)) state.favourites.Clear();
        any_refinement = true;
    }
    if (state.search.size) {
        auto const text = fmt::Format(builder.arena,
                                      any_refinement ? "matching \"{}\""_s : "matches for \"{}\""_s,
                                      state.search);
        if (do_removable(text, "Clear the search."_s)) dyn::Clear(state.search);
        any_refinement = true;
    }

    // Browse mode inside a collection. A folder names itself: the collection holding it is the level above
    // in the breadcrumb, so repeating it here would only make the line longer.
    auto const& scope = options.browse_scope;
    if (scope.collection) {
        do_words("from"_s);
        if (scope.folder_name.size) {
            if (do_removable(scope.folder_name,
                             (String)fmt::Format(builder.arena,
                                                 "Go back to showing the whole {}.",
                                                 scope.collection_noun))) {
                state.Filter(BrowserFilter::Folder).Clear();
                state.Filter(scope.collection->filter).Add(scope.collection->key, scope.collection->name);
                state.scroll_items_to_start = true;
            }
        } else if (do_removable(scope.collection->name,
                                (String)fmt::Format(builder.arena,
                                                    "Go back to the list of {}s.",
                                                    scope.collection_noun))) {
            ApplyBreadcrumbAction(state, BreadcrumbAction::SectionRoot);
        }
        return;
    }

    // The selected filter values: one in Browse mode's attribute pages, any number in Filter mode.
    auto const joining_word = ({
        String w {};
        switch (state.filter_mode) {
            case FilterMode::Single:
            case FilterMode::MultipleAnd: w = "and"_s; break;
            case FilterMode::MultipleOr: w = "or"_s; break;
            case FilterMode::Count: PanicIfReached();
        }
        w;
    });
    bool any_filter = false;
    for (auto& filter : state.filters) {
        filter.ForEachSelected([&](String display_name, u64 key) {
            if (any_filter)
                do_words(joining_word);
            else if (any_refinement)
                do_words("with"_s);
            any_filter = true;
            auto const text = display_name.size
                                  ? (String)fmt::Format(builder.arena, "{}: {}", filter.name, display_name)
                                  : filter.name;
            if (do_removable(text, "Remove this filter."_s)) filter.Remove(key);
            return LoopControl::Continue;
        });
    }

    // The item noun only appears when nothing narrows the list: everything else on the line already says
    // what kind of thing is shown.
    if (!any_filter && !any_refinement)
        do_words(fmt::Format(builder.arena, "all {}", options.plural_item_type_name));
}

static Corners CornersNotTouching(Rect shape, Rect other) {
    auto const tolerance = WwToPixels(1.0f);
    auto const other_below = Abs(other.y - shape.Bottom()) <= tolerance;
    auto const other_above = Abs(other.Bottom() - shape.y) <= tolerance;
    auto const spans = [&](f32 x) { return x >= other.x - tolerance && x <= other.Right() + tolerance; };
    Corners corners = 0b1111;
    if (other_below) {
        if (spans(shape.x)) corners = (Corners)(corners & 0b1110);
        if (spans(shape.Right())) corners = (Corners)(corners & 0b1101);
    }
    if (other_above) {
        if (spans(shape.x)) corners = (Corners)(corners & 0b0111);
        if (spans(shape.Right())) corners = (Corners)(corners & 0b1011);
    }
    return corners;
}

struct BrowserSize {
    f32 results_width; // WW
    f32 filters_col_width; // WW
    f32 height; // WW
};

static constexpr u64 k_browser_results_width_store_id = HashFnv1a("browser-results-width");
static constexpr u64 k_browser_width_store_id = HashFnv1a("browser-filters-width");
static constexpr u64 k_browser_height_store_id = HashFnv1a("browser-height");

// The panels can't grow past the window, and the height's default is already what the window has room for.
// The floors are the least that still fits the toolbar and a few rows. The results panel is settled first
// and the filters panel gets what's left, so neither can push the other off the window.
static BrowserSize ClampBrowserSize(BrowserSize size, BrowserPopupOptions const& options) {
    constexpr f32 k_min_results_width = 150;
    constexpr f32 k_min_filters_width = 100;
    constexpr f32 k_window_edge_space = 40;
    auto const window_width = PixelsToWw((f32)GuiIo().in.window_size.width);
    auto const results_width =
        Clamp(size.results_width,
              k_min_results_width,
              Max(k_min_results_width, window_width - k_min_filters_width - k_window_edge_space));
    return {
        .results_width = results_width,
        .filters_col_width =
            Clamp(size.filters_col_width,
                  k_min_filters_width,
                  Max(k_min_filters_width, window_width - results_width - k_window_edge_space)),
        .height = Clamp(size.height, Min(150.0f, options.height), options.height),
    };
}

// Screenshots always use the default so the docs images don't depend on a stored size.
static BrowserSize CurrentBrowserSize(BrowserPopupContext& context, BrowserPopupOptions const& options) {
    auto& state = context.state;
    if (!Exchange(state.size_loaded_from_store, true)) {
        state.size.results_width =
            persistent_store::GetValueAs<f32>(context.store,
                                              k_browser_results_width_store_id ^ options.store_id);
        state.size.filters_col_width =
            persistent_store::GetValueAs<f32>(context.store, k_browser_width_store_id ^ options.store_id);
        state.size.height =
            persistent_store::GetValueAs<f32>(context.store, k_browser_height_store_id ^ options.store_id);
    }
    if (IsAnyScreenshotInProgress())
        return {options.results_width, options.filters_col_width, options.height};
    return ClampBrowserSize(
        {.results_width = state.size.results_width.ValueOr(options.results_width),
         .filters_col_width = state.size.filters_col_width.ValueOr(options.filters_col_width),
         .height = state.size.height.ValueOr(options.height)},
        options);
}

static void SaveBrowserSize(BrowserPopupContext& context, BrowserPopupOptions const& options) {
    auto const save = [&](u64 id, Optional<f32> value) {
        persistent_store::RemoveValue(context.store, id, k_nullopt);
        if (value) persistent_store::AddValue(context.store, id, *value);
    };
    save(k_browser_results_width_store_id ^ options.store_id, context.state.size.results_width);
    save(k_browser_width_store_id ^ options.store_id, context.state.size.filters_col_width);
    save(k_browser_height_store_id ^ options.store_id, context.state.size.height);
}

// The filters panel's bottom-right corner is the only one the browser doesn't share with the opener or the
// results panel, so it's the handle: drag it to resize, double-click it to reset. The results width is the
// border's to set (DoBrowserPanelSplitter), so this leaves it alone. The grip overlays the corner itself
// rather than sitting in the toolbar's flow, so it's flush with the browser's edges.
static void DoBrowserResizeGrip(GuiBuilder& builder,
                                BrowserPopupContext& context,
                                BrowserPopupOptions const& options,
                                Box const& overlay_parent,
                                BrowserSize current_size) {
    auto& state = context.state;
    auto const grip = DoBox(
        builder,
        {
            .parent = overlay_parent,
            .layout {
                .size = k_browser_spacing * 2,
                .anchor = layout::Anchor::Right | layout::Anchor::Bottom,
            },
            .tooltip =
                "Drag to resize the browser. Double-click to reset its size. Floe remembers the size."_s,
            .button_behaviour =
                imgui::ButtonConfig {
                    .event = MouseButtonEvent::Down,
                    .cursor_type = CursorType::UpLeftDownRight,
                },
            .name = "browser.resize-grip"_s,
        });

    auto const& input = GuiIo().in;

    if (grip.button_fired) {
        state.resize_drag = {
            .cursor_origin = input.cursor_pos,
            .size_at_origin = {current_size.filters_col_width, current_size.height},
        };
        if (input.Mouse(MouseButton::Left).last_press.is_double_click) {
            state.size.filters_col_width = k_nullopt;
            state.size.height = k_nullopt;
            state.resize_drag.size_at_origin = {options.filters_col_width, options.height};
        }
    }

    if (grip.is_active && All(input.cursor_pos != -1) &&
        Any(input.cursor_pos != state.resize_drag.cursor_origin)) {
        auto const delta = PixelsToWw(input.cursor_pos - state.resize_drag.cursor_origin);
        // When the browser sits above its opener its bottom edge is anchored and the top edge is what
        // moves, so dragging down means less height rather than more.
        auto const above_opener = builder.imgui.curr_viewport->unpadded_bounds.Bottom() <=
                                  state.absolute_button_rect.y + WwToPixels(1.0f);
        auto const origin = state.resize_drag.size_at_origin;
        auto const size = ClampBrowserSize({.results_width = current_size.results_width,
                                            .filters_col_width = origin.x + delta.x,
                                            .height = origin.y + (above_opener ? -delta.y : delta.y)},
                                           options);
        state.size.filters_col_width = size.filters_col_width;
        state.size.height = size.height;
    }

    if (builder.imgui.WasJustDeactivated(grip.imgui_id)) SaveBrowserSize(context, options);

    if (auto const viewport_r = BoxRect(builder, grip)) {
        // Inset so the glyph clears the panel's rounded corner.
        auto const inset = WwToPixels(1.5f);
        DrawResizeCornerGrip(
            builder.imgui,
            builder.imgui.ViewportRectToWindowRect(*viewport_r).CutRight(inset).CutBottom(inset),
            ToU32(Col {
                .c = builder.imgui.IsHotOrActive(grip.imgui_id) ? Col::Text : Col::Subtext0,
                .dark_mode = true,
            }));
    }
}

// The hit strip straddles the panel border. It reaches less into the results panel, whose scrollbar sits
// against the border, than into the filters panel, where only row padding does.
static constexpr f32 k_browser_splitter_reach_into_results = 3;
static constexpr f32 k_browser_splitter_reach_into_filters = 5;

// The border between the panels sets the results width: drag it, double-click it to reset. It's a viewport
// of its own, made after the panels' list viewports, so that it's what the cursor hovers where its strip
// overlaps them. Nothing is drawn until it's hovered: the border alone marks the place.
static void DoBrowserPanelSplitter(GuiBuilder& builder,
                                   BrowserPopupContext& context,
                                   BrowserPopupOptions const& options,
                                   BrowserSize current_size) {
    auto& state = context.state;
    auto const splitter = DoBox(
        builder,
        {
            .layout {
                .size = layout::k_fill_parent,
            },
            .tooltip =
                "Drag to resize the results panel. Double-click to reset its width. Floe remembers the size."_s,
            .button_behaviour =
                imgui::ButtonConfig {
                    .event = MouseButtonEvent::Down,
                    .cursor_type = CursorType::HorizontalArrows,
                },
            .name = "browser.panel-splitter"_s,
        });

    auto const& input = GuiIo().in;

    if (splitter.button_fired) {
        state.resize_drag = {
            .cursor_origin = input.cursor_pos,
            .results_width_at_origin = current_size.results_width,
        };
        if (input.Mouse(MouseButton::Left).last_press.is_double_click) {
            state.size.results_width = k_nullopt;
            state.resize_drag.results_width_at_origin = options.results_width;
        }
    }

    if (splitter.is_active && All(input.cursor_pos != -1) &&
        Any(input.cursor_pos != state.resize_drag.cursor_origin)) {
        auto const delta = PixelsToWw(input.cursor_pos - state.resize_drag.cursor_origin);
        state.size.results_width =
            ClampBrowserSize({.results_width = state.resize_drag.results_width_at_origin + delta.x,
                              .filters_col_width = current_size.filters_col_width,
                              .height = current_size.height},
                             options)
                .results_width;
    }

    if (builder.imgui.WasJustDeactivated(splitter.imgui_id)) SaveBrowserSize(context, options);

    if (auto const viewport_r = BoxRect(builder, splitter);
        viewport_r && builder.imgui.IsHotOrActive(splitter.imgui_id)) {
        auto const r = builder.imgui.ViewportRectToWindowRect(*viewport_r);
        auto const border_x = r.x + WwToPixels(k_browser_splitter_reach_into_results);
        builder.imgui.draw_list->AddRectFilled(
            Rect {.xywh = {border_x - WwToPixels(1.0f), r.y, WwToPixels(2.0f), r.h}},
            ToU32(Col {
                .c = builder.imgui.IsActive(splitter.imgui_id) ? Col::Subtext0 : Col::Overlay0,
                .dark_mode = true,
            }));
    }
}

static void DoBrowserPopupInternal(GuiBuilder& builder,
                                   BrowserPopupContext& context,
                                   BrowserPopupOptions const& options) {
    using Visibility = CurrentItemStatus::Visibility;

    if (builder.imgui.modal_just_opened == context.browser_id) context.state.scroll_to_show_current = true;

    SetBrowserMode(context.state,
                   ({
                       BrowserMode mode;
                       if (IsAnyScreenshotInProgress()) {
                           mode = (IsScreenshotRequest("browser-browse"_s) ||
                                   IsScreenshotRequest("browser-browse-section"_s) ||
                                   IsScreenshotRequest("browser-browse-collection"_s) ||
                                   IsScreenshotRequest("browser-browse-attribute"_s) ||
                                   IsScreenshotRequest("browser-preset-browse"_s))
                                      ? BrowserMode::Browse
                                      : BrowserMode::Filter;
                       } else {
                           mode =
                               (BrowserMode)prefs::GetInt(context.preferences, BrowserModePrefsDescriptor());
                       }
                       mode;
                   }),
                   IsAnyScreenshotInProgress()
                       ? FilterMode::MultipleAnd
                       : (FilterMode)prefs::GetInt(context.preferences, BrowserFilterModePrefsDescriptor()));

    // Screenshots never touch the store, so the docs images don't depend on it and don't change it. The
    // place is restored before the checks below so a stale one is corrected the same way as any other.
    if (!IsAnyScreenshotInProgress() && !Exchange(context.state.browse_place_loaded_from_store, true) &&
        context.state.mode == BrowserMode::Browse) {
        auto const stored =
            persistent_store::Get(context.store, k_browser_browse_place_store_id ^ options.store_id);
        if (stored.tag == persistent_store::GetResult::Found)
            DecodeBrowsePlace(stored.Get<persistent_store::Value const*>()->data, context.state);
        context.state.browse_place_saved_hash = BrowseNavigationHash(context.state);
    }

    auto& browse = context.state.browse;

    if (context.state.mode != BrowserMode::Browse) {
        browse = {};
        dyn::Clear(context.state.browse_forward_levels);
    }

    // A tag can become the selection without its category page having been opened, most often by
    // switching out of Filter mode, so the category is worked out from the tag itself.
    if (browse.open_attribute.ValueOr(k_max_browser_filters) != (u8)BrowserFilter::Tags) {
        browse.open_tag_category = k_nullopt;
    } else {
        context.state.Filter(BrowserFilter::Tags).ForEachSelected([&](String, u64 key) {
            if (key == k_untagged_key) return LoopControl::Continue;
            if (auto const tag_and_cat = LookupTagName(GetTagInfo((TagType)key).name))
                browse.open_tag_category = tag_and_cat->category;
            return LoopControl::Break;
        });
    }
    if (IsScreenshotRequest("browser-browse-section"_s) && !browse.open_collection_section) {
        auto const rows = BrowseRootRows(context, options);
        if (rows.num_collection_sections) browse.open_collection_section = rows.rows[0].id;
    }

    // A section can stop existing while it's open, such as when preset banks are no longer split into
    // factory and user groups. Not while a scan is still adding items: the sections are derived from the
    // items, so a restored section may simply not have appeared yet.
    if (browse.open_collection_section && !context.state.items_still_loading &&
        !CollectionSectionTitle(context, options, *browse.open_collection_section).text.size) {
        browse.open_collection_section = k_nullopt;
        context.state.ClearAll();
    }

    context.state.browse_collection_open =
        context.state.mode == BrowserMode::Browse && !browse.open_attribute && context.state.HasFilters();

    // A collection can become the selection without its section having been opened, most often by
    // switching out of Filter mode, so the level above it is worked out from the collection itself.
    if (context.state.browse_collection_open && !browse.open_collection_section) {
        if (options.library_filters && options.library_filters->collection_view &&
            context.state.Filter(BrowserFilter::Library).HasSelected())
            browse.open_collection_section = LibrariesCollectionSectionId(context.browser_id);
        else
            browse.open_collection_section =
                options.browse_scope.collection ? options.browse_scope.collection->section_id : k_nullopt;

        // A selection we can't trace back to a section has no level to return to, so it's dropped rather
        // than left stranded.
        if (!browse.open_collection_section) {
            context.state.ClearAll();
            context.state.browse_collection_open = false;
        }
    }

    auto const navigation_hash = BrowseNavigationHash(context.state);

    // Forward only undoes back: any other navigation since then makes the stacked levels stale.
    if (navigation_hash != context.state.browse_navigation_hash)
        dyn::Clear(context.state.browse_forward_levels);

    if (context.state.mode == BrowserMode::Browse && !IsAnyScreenshotInProgress() &&
        !context.state.items_still_loading && navigation_hash != context.state.browse_place_saved_hash) {
        context.state.browse_place_saved_hash = navigation_hash;
        persistent_store::SetValue(context.store,
                                   k_browser_browse_place_store_id ^ options.store_id,
                                   EncodeBrowsePlace(context.state, builder.arena));
    }

    // A pending scroll is only meaningful while the item can be drawn. Otherwise it would fire unexpectedly
    // later, e.g. when a filter is removed.
    switch (options.current_item.visibility) {
        case Visibility::Loading:
        case Visibility::Shown:
        case Visibility::InCollapsedSection: break;
        case Visibility::None:
        case Visibility::HiddenByFilters:
        case Visibility::NotInList: context.state.scroll_to_show_current = false; break;
    }
    auto const size = CurrentBrowserSize(context, options);
    context.state.filters_width_tier = BrowserWidthTierForWidth(size.filters_col_width);

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = {layout::k_hug_contents, size.height},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                                .name = "browser.modal"_s,
                            });

    // The panels sit in a row. The splitter strip lies over them, across their border, so that both keep
    // running edge to edge.
    auto const main_section = DoBox(builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Overlay,
                                        },
                                    });

    auto const panels_row = DoBox(builder,
                                  {
                                      .parent = main_section,
                                      .layout {
                                          .size = layout::k_fill_parent,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                  });

    // A strip along the bottom of each panel. The two halves share a height and a rule above them, so they
    // read as one toolbar split by the panel border: each half's controls act on the panel it sits in.
    auto const do_toolbar = [&](Box const& panel, bool dark_mode, String name) {
        DoBox(builder,
              {
                  .parent = panel,
                  .background_fill_colours = Col {.c = Col::Surface1, .dark_mode = dark_mode},
                  .layout {
                      .size = {layout::k_fill_parent, PixelsToWw(1.0f)},
                  },
              });

        return DoBox(builder,
                     {
                         .parent = panel,
                         .layout {
                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                             .contents_padding = {.lr = k_browser_spacing / 2},
                             .contents_gap = k_browser_spacing / 2,
                             .contents_direction = layout::Direction::Row,
                             .contents_align = layout::Alignment::Start,
                             .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                         },
                         .name = name,
                     });
    };

    // Each panel fills its own half with its own colour rather than one background spanning the browser
    // with the other drawn over it: a panel's anti-aliased edge then blends with what's behind the browser
    // instead of letting a lighter layer show through along it.
    auto const browser_corners =
        CornersNotTouching(builder.imgui.curr_viewport->unpadded_bounds, context.state.absolute_button_rect);

    // The results come first so they sit under the box that opened the browser: picking an item is a
    // straight move down, like a menu. The filters are beside them, past a border the user can drag.
    auto const results_panel = DoBox(builder,
                                     {
                                         .parent = panels_row,
                                         .background_fill_colours = Col {.c = Col::Background0},
                                         // The panel owns the browser's left corners.
                                         .round_background_corners = (Corners)(browser_corners & 0b1001),
                                         .corner_rounding = k_corner_rounding,
                                         .layout {
                                             .size = {size.results_width, layout::k_fill_parent},
                                             .contents_padding = {.b = k_browser_spacing},
                                             .contents_gap = k_browser_spacing,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                         .name = "browser.results-panel"_s,
                                     });

    auto const splitter_strip =
        DoBox(builder,
              {
                  .parent = main_section,
                  .layout {
                      .size = {k_browser_splitter_reach_into_results + k_browser_splitter_reach_into_filters,
                               layout::k_fill_parent},
                      .margins = {.l = size.results_width - k_browser_splitter_reach_into_results},
                      .anchor = layout::Anchor::Left,
                  },
              });

    {
        auto const filters_panel =
            DoBox(builder,
                  {
                      .parent = panels_row,
                      .background_fill_colours = Col {.c = Col::Background1, .dark_mode = true},
                      // The panel's top-right corner is the browser's, so it rounds only when
                      // the opener isn't flush against it.
                      .round_background_corners = (Corners)(0b0010 | (browser_corners & 0b0100)),
                      .corner_rounding = k_corner_rounding,
                      .layout {
                          .size = {size.filters_col_width, layout::k_fill_parent},
                          .margins = {.lrtb = 0},
                          .contents_padding = {.b = k_browser_spacing},
                          .contents_gap = k_browser_spacing,
                          .contents_direction = layout::Direction::Column,
                          .contents_align = layout::Alignment::Start,
                      },
                      .name = "browser.filters-panel"_s,
                  });

        // Each browse level uses its own viewport, which is how a level keeps its scroll position while
        // you're inside another one: going back lands where you left off. Entering a level always
        // requests a scroll to the start, so levels that share a viewport can't inherit each other's
        // position.
        auto const in_browse_mode = context.state.mode == BrowserMode::Browse;
        auto const browse_level = CurrentBrowseLevel(context.state);
        auto const filters_viewport_id = builder.imgui.MakeId(({
            String id = "filters"_s;
            if (in_browse_mode) {
                switch (browse_level) {
                    case BrowseLevel::Root: break;
                    case BrowseLevel::CollectionSection: id = "filters-browse-section"_s; break;
                    case BrowseLevel::Collection: id = "filters-browse-collection"_s; break;
                    case BrowseLevel::Attribute: id = "filters-browse-attribute"_s; break;
                    case BrowseLevel::TagCategory: id = "filters-browse-tag-category"_s; break;
                }
            }
            id;
        }));

        if (Exchange(context.state.scroll_filters_to_start, false)) {
            if (auto w = builder.imgui.FindViewport(filters_viewport_id)) builder.imgui.SetYScroll(w, 0.0f);
        }

        auto const root_rows = ({
            BrowseRootRowList r {};
            if (in_browse_mode && browse_level == BrowseLevel::Root) r = BrowseRootRows(context, options);
            r;
        });

        // The tree and the rule marking its foot share a gapless column, so the rule sits on the bottom of
        // the scrolling region instead of floating between it and the breadcrumb.
        auto const filters_column = DoBox(builder,
                                          {
                                              .parent = filters_panel,
                                              .layout {
                                                  .size = layout::k_fill_parent,
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Start,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                              },
                                          });

        // Browse mode: every level names itself, so where the drill-down has landed is readable without
        // tracing the breadcrumb. The title sits above the scrolling rows rather than among them, so it
        // stays put while they scroll. The root is the exception: its heading per group of rows scrolls
        // with the rows.
        if (in_browse_mode) {
            bool has_title = false;
            if (browse_level == BrowseLevel::Collection && options.browse_scope.collection) {
                DoBrowseOpenCollectionTitle(builder,
                                            context,
                                            *options.browse_scope.collection,
                                            filters_column);
                has_title = true;
            } else if (auto const title = CurrentBrowsePageTitle(context, options); title.text.size) {
                DoBrowsePageTitle(builder, {.parent = filters_column, .title = title});
                has_title = true;
            }
            if (has_title)
                DoModalDivider(
                    builder,
                    filters_column,
                    {.horizontal = true, .subtle = true, .dark_mode = true, .snap_to_start = true});
        }

        DoBoxViewport(builder,
                      {
                          .run =
                              [&, root_rows](GuiBuilder& builder) {
                                  if (!options.library_filters && !options.tags_filters) return;
                                  switch (context.state.mode) {
                                      case BrowserMode::Browse:
                                          DoBrowsePage(builder, context, options, root_rows);
                                          break;
                                      case BrowserMode::Filter:
                                          DoFilterTree(builder, context, options);
                                          break;
                                      case BrowserMode::Count: PanicIfReached();
                                  }
                              },
                          .bounds = DoBox(builder,
                                          {
                                              .parent = filters_column,
                                              .layout {
                                                  .size = layout::k_fill_parent,
                                              },
                                          }),
                          .imgui_id = filters_viewport_id,
                          // No padding: rows run edge to edge like a menu, and the scrollbar takes width from
                          // them only when it's there.
                          .viewport_config = ({
                              auto cfg = k_default_modal_subviewport;
                              cfg.draw_scrollbars = DrawModalScrollbarsDarkMode;
                              cfg.scroll_line_size = k_browser_item_height;
                              cfg.scrollbar_padding = 0;
                              cfg;
                          }),
                          .debug_name = "filters",
                      });

        // Browse mode's breadcrumb is a menu row like the ones below it, so it runs edge to edge. Filter mode
        // has nothing above its tree: its search and match controls live in the toolbar at the bottom.
        if (in_browse_mode) {
            DoModalDivider(builder,
                           filters_column,
                           {.horizontal = true, .subtle = true, .dark_mode = true, .snap_to_start = true});
            DoBrowseBreadcrumb(builder,
                               context.state,
                               filters_panel,
                               size.filters_col_width,
                               BreadcrumbSegments(builder.arena, context, options),
                               fmt::Format(builder.arena,
                                           "Back to the starting page, showing all {}.",
                                           options.plural_item_type_name));
        }

        // The controls stop short of the row's end so the resize grip overlaying the corner never sits on
        // top of them, even when the search has grown to fill the row.
        auto const toolbar_row = do_toolbar(filters_panel, true, "browser.filters-toolbar"_s);
        auto const toolbar = DoBox(builder,
                                   {
                                       .parent = toolbar_row,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .margins = {.r = k_browser_spacing},
                                           .contents_gap = k_browser_spacing / 2,
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                           .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                       },
                                   });

        // Browse mode searches nothing: its root is a short list and an attribute page shows every value it
        // has.
        auto const filter_mode = context.state.mode == BrowserMode::Filter;
        auto const filter_search_id = builder.imgui.MakeId("filter-search");

        // Measured up front so a panel too narrow for the mode toggle and match button gets one menu
        // button in their place instead of a toolbar that runs off its edge. An open search comes first:
        // it keeps its useful width, and if even the menu button would take that, the search has the row to
        // itself until it's closed.
        auto const search_width =
            filter_mode ? ToolbarSearchWidth(builder, filter_search_id, context.state.filter_search) : 0.0f;
        // Less the row padding and the margin the controls keep clear of the resize grip.
        auto const available_width = size.filters_col_width - (k_browser_spacing * 2);
        auto const controls_fit = ({
            auto const mode_toggle_width = (k_browser_item_height * 1.3f * 2) + 2 + 4;
            auto needed = mode_toggle_width + search_width;
            if (filter_mode) {
                auto const match_text_width =
                    PixelsToWw(builder.fonts.atlas[ToInt(FontType::Body)]
                                   ->CalcTextSize(FilterModeTextAbbreviated(context.state.filter_mode), {})
                                   .x);
                needed += (k_browser_spacing / 2) + match_text_width + k_browser_spacing;
            }
            needed <= available_width;
        });
        auto const overflow_fits = k_browser_item_height + search_width <= available_width;

        if (controls_fit)
            DoBrowserModeToggle(builder, context, options, toolbar);
        else if (overflow_fits)
            DoBrowserToolbarOverflowMenu(builder, context, options, toolbar);

        if (filter_mode && controls_fit) {
            auto const match_button =
                DoBox(builder,
                      {
                          .parent = toolbar,
                          .background_fill_auto_hot_active_overlay = true,
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = {layout::k_hug_contents, k_browser_item_height},
                              .contents_padding = {.lr = k_browser_spacing / 2},
                              .contents_align = layout::Alignment::Middle,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                          .tooltip = (String)fmt::Format(builder.arena,
                                                         "How selected filters combine. {} Click to change.",
                                                         FilterModeDescription(context.state.filter_mode)),
                          .button_behaviour = imgui::ButtonConfig {},
                          .name = "browser.match-button"_s,
                      });

            DoBox(builder,
                  {
                      .parent = match_button,
                      .text = FilterModeTextAbbreviated(context.state.filter_mode),
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                      .parent_dictates_hot_and_active = true,
                  });

            auto const match_popup_id = builder.imgui.MakeId("matchmode");

            if (match_button.button_fired) builder.imgui.OpenPopupMenu(match_popup_id, match_button.imgui_id);

            if (IsScreenshotRequest("browser-menu"_s) && !builder.imgui.IsPopupMenuOpen(match_popup_id))
                builder.imgui.OpenPopupMenu(match_popup_id, match_button.imgui_id);

            if (builder.imgui.IsPopupMenuOpen(match_popup_id))
                DoBoxViewport(builder,
                              {
                                  .run =
                                      [&](GuiBuilder& builder) {
                                          DoMatchModeMenuItems(
                                              builder,
                                              context,
                                              DoBrowserMenuRoot(builder, "browser.match-menu"_s));
                                      },
                                  .bounds = match_button,
                                  .imgui_id = match_popup_id,
                                  .viewport_config = k_default_popup_menu_viewport,
                                  .debug_name = "matchmode",
                              });
        }

        if (filter_mode) {
            DoToolbarSearch(
                builder,
                {
                    .parent = toolbar,
                    .text_input_id = filter_search_id,
                    .text = context.state.filter_search,
                    .placeholder = options.filter_search_placeholder_text,
                    .tooltip = "Find a filter by name. Only filters whose names match stay in the tree."_s,
                    .dark_mode = true,
                });
        }

        DoBrowserResizeGrip(builder, context, options, main_section, size);
    }

    {
        auto const results_viewport_id = builder.imgui.MakeId("results");
        if (Exchange(context.state.scroll_items_to_start, false)) {
            if (auto w = builder.imgui.FindViewport(results_viewport_id)) builder.imgui.SetYScroll(w, 0.0f);
        }

        // The list and the rule marking its foot share a gapless column, so the rule sits on the bottom of
        // the scrolling region instead of floating between it and the footer.
        auto const results_column = DoBox(builder,
                                          {
                                              .parent = results_panel,
                                              .layout {
                                                  .size = layout::k_fill_parent,
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Start,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                              },
                                          });

        DoBoxViewport(builder,
                      {
                          .run = [&](GuiBuilder& builder) { options.do_items(builder); },
                          .bounds = DoBox(builder,
                                          {
                                              .parent = results_column,
                                              .layout {
                                                  .size = layout::k_fill_parent,
                                              },
                                          }),
                          .imgui_id = results_viewport_id,
                          // No padding: rows run edge to edge like a menu, and the scrollbar takes width
                          // from them only when it's there.
                          .viewport_config = ({
                              auto cfg = k_default_modal_subviewport;
                              cfg.scroll_line_size = k_browser_item_height;
                              cfg.scrollbar_padding = 0;
                              cfg;
                          }),
                          .debug_name = "results",
                      });

        DoModalDivider(builder, results_column, {.horizontal = true, .subtle = true, .snap_to_start = true});

        DoResultsFooter(builder, context, options, results_panel);

        auto const toolbar = do_toolbar(results_panel, false, "browser.results-toolbar"_s);

        auto const search_input_id = builder.imgui.MakeId("items-search");
        auto const favourites_count = NumUsedForFilterString(builder,
                                                             options.favourites_filter_info.total_available,
                                                             FontType::Heading3);

        // The search comes first: it keeps its useful width, and the buttons before it go when the row can't
        // hold them too - favourites first, then jump-to-current. Closing the search brings them back.
        auto const has_current_item = options.current_item.visibility != Visibility::None;
        auto const search_width =
            options.show_search ? ToolbarSearchWidth(builder, search_input_id, context.state.search) : 0.0f;
        auto const current_item_width =
            has_current_item ? k_browser_item_height + (k_browser_spacing / 2) : 0.0f;
        auto const favourites_width = ({
            auto const pixels_per_ww = builder.state->viewport_cache.pixels_per_ww;
            auto const star_width =
                builder.fonts.atlas[ToInt(FontType::Icons)]
                    ->CalcTextSize(ICON_FA_STAR, {.font_size = k_font_icons_size * 0.8f * pixels_per_ww})
                    .x /
                pixels_per_ww;
            k_browser_spacing + star_width + 3 + favourites_count.size.x;
        });
        auto const available_width = size.results_width - k_browser_spacing;
        auto const show_favourites = current_item_width + favourites_width + search_width <= available_width;
        auto const show_current_item =
            has_current_item && current_item_width + search_width <= available_width;

        // Jumps to the current item, clearing whatever hides it first. In Browse mode it also opens the
        // collection the item belongs to in the right panel.
        if (show_current_item) {
            auto const& current_item = options.current_item;
            auto const type = options.item_type_name;
            auto const shows_collection =
                current_item.collection.HasValue() && context.state.mode == BrowserMode::Browse;
            auto const collection_note = shows_collection
                                             ? (String)fmt::Format(builder.arena,
                                                                   " Also shows its {} in the right panel.",
                                                                   options.browse_scope.collection_noun)
                                             : ""_s;

            auto const tooltip = ({
                String s {};
                switch (current_item.visibility) {
                    case Visibility::Shown:
                        s = fmt::Format(builder.arena,
                                        "Scroll to the current {}: {}.{}",
                                        type,
                                        current_item.name,
                                        collection_note);
                        break;
                    case Visibility::InCollapsedSection:
                        s = fmt::Format(builder.arena,
                                        "Scroll to the current {} (expands its folder): {}.{}",
                                        type,
                                        current_item.name,
                                        collection_note);
                        break;
                    case Visibility::HiddenByFilters:
                        s = fmt::Format(
                            builder.arena,
                            "The current {} ({}) is hidden by your {} or search. Click to clear and show it.{}",
                            type,
                            current_item.name,
                            SelectionNoun(context.state),
                            collection_note);
                        break;
                    case Visibility::NotInList:
                    case Visibility::Loading:
                        s = CurrentItemUnlistedTooltip(builder.arena, type, current_item);
                        break;
                    case Visibility::None: break;
                }
                s;
            });

            if (IconButton(builder,
                           toolbar,
                           ICON_FA_LOCATION_ARROW,
                           tooltip,
                           k_font_icons_size * 0.8f,
                           f32x2 {k_browser_item_height, k_browser_item_height},
                           SourceLocationHash(),
                           false,
                           current_item.visibility == Visibility::NotInList)
                    .button_fired &&
                current_item.visibility != Visibility::NotInList) {
                switch (current_item.visibility) {
                    case Visibility::Shown:
                    case Visibility::InCollapsedSection:
                        dyn::RemoveValue(context.state.collapsed_filter_headers, current_item.section_id);
                        break;
                    case Visibility::HiddenByFilters:
                        context.state.ClearAll();
                        context.state.browse = {};
                        context.state.favourites.Clear();
                        dyn::Clear(context.state.search);
                        dyn::RemoveValue(context.state.collapsed_filter_headers, current_item.section_id);
                        break;
                    case Visibility::Loading:
                    case Visibility::NotInList:
                    case Visibility::None: break;
                }
                context.state.scroll_to_show_current = true;
                context.state.flash_current_when_shown = true;

                if (shows_collection) {
                    auto const& collection = *current_item.collection;
                    auto const already_open =
                        context.state.browse_collection_open &&
                        context.state.Filter(collection.filter).Contains(collection.key);
                    if (!already_open) {
                        context.state.ClearAll();
                        context.state.Filter(collection.filter).Add(collection.key, collection.name);
                        context.state.browse = {}; // The section is resolved from the selection.
                        context.state.scroll_filters_to_start = true;
                    }
                }
            }
        }

        if (show_favourites) {
            // Like the search box, favourites refines whatever is currently shown, so clicking is a pure
            // toggle and the count is scoped to the results.
            auto const& info = options.favourites_filter_info;
            auto const is_selected = context.state.favourites.HasSelected();
            auto const grey_out = !is_selected && info.num_used_in_items_lists == 0;

            auto const button = DoBox(
                builder,
                {
                    .parent = toolbar,
                    .background_fill_colours =
                        is_selected ? Colours {Col {.c = Col::Highlight}} : Colours {Col {.c = Col::None}},
                    .background_fill_auto_hot_active_overlay = true,
                    .round_background_corners = 0b1111,
                    .layout {
                        .size = {layout::k_hug_contents, k_browser_item_height},
                        .contents_padding = {.lr = k_browser_spacing / 2},
                        .contents_gap = 3,
                        .contents_align = layout::Alignment::Middle,
                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                    },
                    .tooltip =
                        "Show only your favourites, within whatever the list is currently showing. Hover an item and click its star to make it a favourite."_s,
                    .button_behaviour = imgui::ButtonConfig {},
                    .name = "browser.favourites-button"_s,
                });

            auto const text_colours = ColSet {
                .base = Col {.c = grey_out ? Col::Surface1 : (is_selected ? Col::Text : Col::Subtext0)},
                .hot = Col {.c = Col::Text},
                .active = Col {.c = Col::Text},
            };

            DoBox(builder,
                  {
                      .parent = button,
                      .text = ICON_FA_STAR,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_font_icons_size * 0.8f,
                      .text_colours = text_colours,
                      .parent_dictates_hot_and_active = true,
                  });

            DoBox(builder,
                  {
                      .parent = button,
                      .text = favourites_count.str,
                      .font = FontType::Heading3,
                      .text_colours = text_colours,
                      .text_justification = TextJustification::CentredLeft,
                      .parent_dictates_hot_and_active = true,
                      .layout {
                          .size = {favourites_count.size.x, layout::k_fill_parent},
                      },
                  });

            if (button.button_fired) context.state.favourites.Toggle(1, "Favourites"_s);
        }

        if (options.show_search) {
            DoToolbarSearch(builder,
                            {
                                .parent = toolbar,
                                .text_input_id = search_input_id,
                                .text = context.state.search,
                                .placeholder = options.item_search_placeholder_text,
                                .tooltip = options.item_search_tooltip,
                                .dark_mode = false,
                            });

            if (builder.IsInputAndRenderPass() && builder.imgui.IsKeyboardFocus(search_input_id)) {
                auto const& frame_input = GuiIo().in;
                if (frame_input.Key(KeyCode::DownArrow).presses.size ||
                    frame_input.Key(KeyCode::Tab).presses.size) {
                    builder.imgui.SetTextInputFocus(0, {}, false);
                    key_nav::FocusPanel(context.state.keyboard_navigation,
                                        BrowserKeyboardNavigation::Panel::Items,
                                        true);
                }
            }

            // CTRL+F focuses the search box.
            if (builder.IsInputAndRenderPass() && builder.imgui.IsKeyboardFocus(context.browser_id)) {
                auto const& frame_input = GuiIo().in;
                auto& frame_output = GuiIo().out;
                frame_output.wants.keyboard_keys.Set(ToInt(KeyCode::F));
                for (auto const& e : frame_input.Key(KeyCode::F).presses) {
                    if (e.modifiers.IsOnly(ModifierKey::Modifier)) {
                        builder.imgui.SetTextInputFocus(search_input_id, context.state.search, false);
                        builder.imgui.TextInputSelectAll();
                        break;
                    }
                }
            }
        }
    }

    // Queued after both panels' list viewports so it sits above them.
    DoBoxViewport(
        builder,
        {
            .run = [&,
                    size](GuiBuilder& builder) { DoBrowserPanelSplitter(builder, context, options, size); },
            .bounds = splitter_strip,
            .imgui_id = builder.imgui.MakeId("panel-splitter"),
            .viewport_config = ({
                auto cfg = k_default_modal_subviewport;
                cfg.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
                cfg;
            }),
            .debug_name = "panel-splitter",
        });

    if (builder.imgui.IsPopupMenuOpen(k_right_click_menu_popup_id))
        DoBoxViewport(builder,
                      {
                          .run =
                              [&](GuiBuilder& builder) {
                                  context.state.right_click_menu_state.do_menu(builder, context, options);
                              },
                          .bounds = context.state.right_click_menu_state.absolute_creator_rect,
                          .imgui_id = k_right_click_menu_popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });
}

// A corner is square when the other shape butts against that edge and spans past the corner, so that the two
// join as one shape.
Corners BrowserOpenerCornersToRound(imgui::Context const& imgui, imgui::Id browser_id, Rect opener_rect) {
    if (!imgui.IsModalOpen(browser_id)) return 0b1111;
    auto const browser = imgui.FindViewport(browser_id);
    if (!browser) return 0b1111;
    return CornersNotTouching(opener_rect, browser->unpadded_bounds);
}

// A faint hairline around the opener and browser together, so the opener reads as part of what's in play
// rather than something the dim happened to miss. One closed path around the union: the seam between the
// two is skipped, the corners on it are square (concave where one is wider than the other) and the outer
// corners round to match their fills. Drawn over the opener's contents, which paint their own fills to
// their edges.
static void DrawBrowserAndOpenerOutline(imgui::Context const& imgui, Rect opener, Rect browser) {
    auto const tolerance = WwToPixels(1.0f);
    auto const rounding = WwToPixels(k_corner_rounding);
    auto const colour = ToU32(Col {.c = Col::White, .alpha = 28});
    auto& draw_list = *imgui.draw_list;

    draw_list.PushClipRectFullScreen();
    DEFER { draw_list.PopClipRect(); };

    auto const opener_above = Abs(opener.Bottom() - browser.y) <= tolerance;
    auto const browser_above = Abs(browser.Bottom() - opener.y) <= tolerance;
    if (!opener_above && !browser_above) {
        draw_list.AddRect(opener, colour, rounding);
        draw_list.AddRect(browser, colour, rounding);
        return;
    }

    auto const upper = opener_above ? opener : browser;
    auto const lower = opener_above ? browser : opener;
    auto const upper_corners = CornersNotTouching(upper, lower);
    auto const lower_corners = CornersNotTouching(lower, upper);
    constexpr Corners k_top_left = 0b1000;
    constexpr Corners k_top_right = 0b0100;
    constexpr Corners k_bottom_right = 0b0010;
    constexpr Corners k_bottom_left = 0b0001;
    auto const radius = [rounding](Corners corners, Corners corner) {
        return (corners & corner) ? rounding : 0.0f;
    };

    // Pixel centres, so a 1px stroke lands on the edge pixel row/column.
    auto const u = Rect {.pos = upper.pos + 0.5f, .size = upper.size - 1.0f};
    auto const l = Rect {.pos = lower.pos + 0.5f, .size = lower.size - 1.0f};
    auto const aligned = [tolerance](f32 a, f32 b) { return Abs(a - b) <= tolerance; };

    // Clockwise from the upper rect's top-left.
    {
        auto const r = radius(upper_corners, k_top_left);
        draw_list.PathArcToFast({u.x + r, u.y + r}, r, 6, 9);
    }
    {
        auto const r = radius(upper_corners, k_top_right);
        draw_list.PathArcToFast({u.Right() - r, u.y + r}, r, 9, 12);
    }
    if (aligned(upper.Right(), lower.Right())) {
        draw_list.PathLineTo({u.Right(), u.Bottom()});
        draw_list.PathLineTo({l.Right(), l.y});
    } else if (lower.Right() > upper.Right()) {
        draw_list.PathLineTo({u.Right(), l.y});
        auto const r = radius(lower_corners, k_top_right);
        draw_list.PathArcToFast({l.Right() - r, l.y + r}, r, 9, 12);
    } else {
        auto const r = radius(upper_corners, k_bottom_right);
        draw_list.PathArcToFast({u.Right() - r, u.Bottom() - r}, r, 0, 3);
        draw_list.PathLineTo({l.Right(), u.Bottom()});
    }
    {
        auto const r = radius(lower_corners, k_bottom_right);
        draw_list.PathArcToFast({l.Right() - r, l.Bottom() - r}, r, 0, 3);
    }
    {
        auto const r = radius(lower_corners, k_bottom_left);
        draw_list.PathArcToFast({l.x + r, l.Bottom() - r}, r, 3, 6);
    }
    if (aligned(upper.x, lower.x)) {
        draw_list.PathLineTo({l.x, l.y});
        draw_list.PathLineTo({u.x, u.Bottom()});
    } else if (lower.x < upper.x) {
        auto const r = radius(lower_corners, k_top_left);
        draw_list.PathArcToFast({l.x + r, l.y + r}, r, 6, 9);
        draw_list.PathLineTo({u.x, l.y});
    } else {
        draw_list.PathLineTo({l.x, u.Bottom()});
        auto const r = radius(upper_corners, k_bottom_left);
        draw_list.PathArcToFast({u.x + r, u.Bottom() - r}, r, 3, 6);
    }
    draw_list.PathStroke(colour, true, 1.0f);
}

void DoBrowserOpenerViewport(GuiBuilder& builder, BrowserOpenerViewportOptions const& options) {
    imgui::ViewportConfig cfg {
        .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
    };

    auto const browser_id = options.browser_id;
    auto const browser_open = builder.imgui.IsModalOpen(browser_id);
    auto const draw_background = [browser_id](imgui::Context const& imgui) {
        auto const r = imgui.curr_viewport->unpadded_bounds;
        imgui.draw_list->AddRectFilled(r,
                                       ToU32({.c = Col::Surface0, .dark_mode = true}),
                                       WwToPixels(k_corner_rounding),
                                       BrowserOpenerCornersToRound(imgui, browser_id, r));
    };
    if (browser_open) {
        cfg.mode = imgui::ViewportMode::Floating;
        cfg.draw_background = draw_background;
        cfg.z_order = 100;
        cfg.ignore_exclusive_focus = true;
    }

    // The run function is deferred and cloned by DoBoxViewport, so the caller's is cloned first for the
    // wrapper to hold onto.
    auto const inner_run = browser_open ? options.run.CloneObject(builder.arena) : options.run;
    auto const outlined_run = [inner_run, browser_id](GuiBuilder& builder) {
        inner_run(builder);
        if (!builder.IsInputAndRenderPass()) return;
        auto const browser = builder.imgui.FindViewport(browser_id);
        if (!browser) return;
        DrawBrowserAndOpenerOutline(builder.imgui,
                                    builder.imgui.curr_viewport->unpadded_bounds,
                                    browser->unpadded_bounds);
    };

    DoBoxViewport(builder,
                  {
                      .run = browser_open ? BoxViewportConfig::RunFunction {outlined_run} : options.run,
                      .bounds = options.bounds,
                      .imgui_id = options.viewport_id,
                      .viewport_config = cfg,
                      .debug_name = options.debug_name,
                  });
}

// Fulfils state.close_when_cursor_leaves. The browser and its opener are separate viewports drawn as one
// shape, so the cursor has to be outside both before the browser is considered left.
static void CloseBrowserIfCursorLeft(imgui::Context& imgui, CommonBrowserState& state, imgui::Id browser_id) {
    if (!state.close_when_cursor_leaves) return;

    if (!imgui.IsModalOpen(browser_id)) {
        state.close_when_cursor_leaves = false;
        return;
    }

    auto const browser = imgui.FindViewport(browser_id);
    if (!browser) return;

    // A right-click menu or a dialog opened from the browser takes exclusive focus and lives outside the
    // browser's bounds.
    if (imgui.exclusive_focus_viewport != browser) return;

    // Dragging a scrollbar or a divider can take the cursor outside while the interaction is still going.
    if (imgui.GetActive() != imgui::k_null_id) return;

    auto const cursor = GuiIo().in.cursor_pos;
    if (browser->unpadded_bounds.Contains(cursor) || state.absolute_button_rect.Contains(cursor)) return;

    imgui.CloseModal(browser_id);
    state.close_when_cursor_leaves = false;
}

void DoBrowserModal(GuiBuilder& builder, BrowserPopupContext context, BrowserPopupOptions const& options) {
    key_nav::BeginFrame(builder.imgui, context.state.keyboard_navigation);

    auto const opener_rect = context.state.absolute_button_rect;
    DoBoxViewport(
        builder,
        {
            .run = [&](GuiBuilder& builder) { DoBrowserPopupInternal(builder, context, options); },
            .bounds = opener_rect,
            .imgui_id = context.browser_id,
            .viewport_config = ({
                auto cfg = k_default_modal_viewport;
                cfg.positioning = imgui::ViewportPositioning::AutoPosition;
                cfg.auto_size = true;
                if (options.flush_with_opener) {
                    // The panels fill the browser's area themselves, so this only lays down the dim and
                    // the shadow. The opener redraws itself above this, so only the shadow needs to cover
                    // it for the two to cast as one shape: the opener's shadow is drawn everywhere but the
                    // browser's vertical span.
                    cfg.draw_background =
                        imgui::DrawViewportBackgroundFunction([opener_rect](imgui::Context const& imgui) {
                            DrawFullscreenDim(imgui);
                            auto const rounding = WwToPixels(k_panel_rounding);
                            auto const r = imgui.curr_viewport->unpadded_bounds;
                            DrawDropShadow(imgui, r, rounding);
                            auto const window_size = GuiIo().in.window_size.ToFloat2();
                            imgui.draw_list->PushClipRect(opener_rect.y < r.y
                                                              ? Rect {.pos = 0, .size = {window_size.x, r.y}}
                                                              : Rect {.xywh = {0,
                                                                               r.Bottom(),
                                                                               window_size.x,
                                                                               window_size.y - r.Bottom()}});
                            DrawDropShadow(imgui, opener_rect, rounding);
                            imgui.draw_list->PopClipRect();
                        }).CloneObject(builder.arena);
                }
                cfg;
            }),
        });

    key_nav::EndFrame(builder.imgui, context.state.keyboard_navigation);

    CloseBrowserIfCursorLeft(builder.imgui, context.state, context.browser_id);
}

TEST_CASE(TestBrowsePlaceCoding) {
    auto const make_state = [] {
        CommonBrowserState state {};
        InitCommonFilters(state);
        dyn::Append(state.filters, FilterSelection::Bool("Extra"_s));
        return state;
    };

    auto source = make_state();
    source.browse.open_attribute = (u8)BrowserFilter::Tags;
    source.browse.open_collection_section = 12345u;
    source.browse.open_tag_category = TagCategory::RealInstrument;
    source.Filter(BrowserFilter::Library).Add(HashFnv1a("abyss"), "Abyss"_s);
    source.Filter(BrowserFilter::Folder).Add(HashFnv1a("pads"), "Pads"_s);
    source.Filter(BrowserFilter::Tags).Add(ToInt(TagType::FieldRecording), {});
    source.Filter(BrowserFilter::Tags).Add(k_untagged_key, {});
    source.filters[ToInt(BrowserFilter::CommonCount)].Add(1, {});

    auto const encoded = EncodeBrowsePlace(source, tester.scratch_arena);

    SUBCASE("round trip") {
        auto decoded = make_state();
        REQUIRE(DecodeBrowsePlace(encoded, decoded));
        CHECK_EQ(*decoded.browse.open_attribute, (u8)BrowserFilter::Tags);
        CHECK_EQ(*decoded.browse.open_collection_section, 12345u);
        CHECK(*decoded.browse.open_tag_category == TagCategory::RealInstrument);
        for (auto const [filter_index, filter] : Enumerate(source.filters)) {
            auto const& decoded_filter = decoded.filters[filter_index];
            filter.ForEachSelected([&](String name, u64 key) {
                CHECK(decoded_filter.Contains(key));
                if (filter.data.tag == FilterSelection::Type::Hashes) {
                    bool found = false;
                    decoded_filter.ForEachSelected([&](String decoded_name, u64 decoded_key) {
                        if (decoded_key == key) found = decoded_name == name;
                        return LoopControl::Continue;
                    });
                    CHECK(found);
                }
                return LoopControl::Continue;
            });
        }
        CHECK(!decoded.Filter(BrowserFilter::LibraryAuthor).HasSelected());
    }

    SUBCASE("root place") {
        auto const root = make_state();
        auto decoded = make_state();
        decoded.browse.open_attribute = (u8)0;
        decoded.Filter(BrowserFilter::Library).Add(1, "Stale"_s);
        REQUIRE(DecodeBrowsePlace(EncodeBrowsePlace(root, tester.scratch_arena), decoded));
        CHECK(!decoded.browse.open_attribute);
        CHECK(!decoded.browse.open_collection_section);
        CHECK(!decoded.HasFilters());
    }

    SUBCASE("rejected data leaves the state untouched") {
        auto decoded = make_state();
        decoded.browse.open_collection_section = 99u;

        SUBCASE("truncated") {
            for (auto const size : Range(encoded.size))
                CHECK(!DecodeBrowsePlace(encoded.SubSpan(0, size), decoded));
        }
        SUBCASE("trailing bytes") {
            DynamicArray<u8> longer {tester.scratch_arena};
            dyn::AppendSpan(longer, encoded);
            dyn::Append(longer, (u8)0);
            CHECK(!DecodeBrowsePlace(longer, decoded));
        }
        SUBCASE("other version") {
            auto other = tester.scratch_arena.Clone(encoded);
            other[0] = k_browse_place_version + 1;
            CHECK(!DecodeBrowsePlace(other, decoded));
        }
        SUBCASE("filter the state doesn't have") {
            dyn::Pop(decoded.filters);
            CHECK(!DecodeBrowsePlace(encoded, decoded));
        }

        CHECK_EQ(*decoded.browse.open_collection_section, 99u);
        CHECK(!decoded.HasFilters());
    }

    return k_success;
}

TEST_REGISTRATION(RegisterBrowsePlaceTests) { REGISTER_TEST(TestBrowsePlaceCoding); }
