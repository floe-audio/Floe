// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_common_browser.hpp"

#include "os/filesystem.hpp"

#include "common_infrastructure/tags.hpp"

#include "gui/core/gui_actions.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/overlays/gui_tips.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "preset_server/preset_server.hpp"

bool LibraryIdLessThanFilterInfo(sample_lib::LibraryId const& a,
                                 FilterItemInfo const&,
                                 sample_lib::LibraryId const& b,
                                 FilterItemInfo const&) {
    return sample_lib::LibraryIdLessThan(a, b);
}

static constexpr u64 k_untagged_key = ToInt(TagType::Count);

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

auto ScopedEnableTooltips(GuiBuilder& builder, bool enable) {
    struct ScopeGuard {
        GuiBuilder& builder;
        bool old_value;

        ScopeGuard(GuiBuilder& b, bool old) : builder(b), old_value(old) {}
        ~ScopeGuard() { builder.config.show_tooltips = old_value; }
    };
    auto old_value = builder.config.show_tooltips;
    builder.config.show_tooltips = enable;
    return ScopeGuard {builder, old_value};
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

    auto const panel_index = ToInt(args.panel);
    auto const is_focused = nav.focused_items[panel_index] == args.id;

    bool button_fired_from_keyboard = false;

    if (nav.focused_panel == args.panel) {
        auto& panel = nav.panel_state;
        bool focus_this = false;

        if (Exchange(panel.select_next, false)) focus_this = true;

        if (args.is_tab_item && Exchange(panel.select_next_tab_item, false)) focus_this = true;

        if (args.id == panel.id_to_select) {
            panel.id_to_select = 0;
            focus_this = true;
        }

        if (panel.select_next_at) {
            if (--panel.select_next_at == 0) focus_this = true;
        }

        if (focus_this) FocusItem(nav, args.panel, args.id);

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
                nav.temp_focused_items[panel_index] = args.id;
                g_show_focus_rectangles = true;
            }

            if (g_show_focus_rectangles &&
                builder.imgui.IsKeyboardFocus(builder.imgui.curr_viewport->root_viewport->id)) {
                auto r = args.rect_for_drawing ? *args.rect_for_drawing : *BoxRect(builder, args.box);
                DrawFocusBox(builder, r);
            }
        }

        panel.item_history.Push(args.id);
        if (args.is_tab_item) panel.previous_tab_item = args.id;

        if (button_fired_from_keyboard || (is_focused && nav.panel_just_focused) || focus_this) {
            builder.imgui.ScrollViewportToShowRectangle(
                BoxRect(builder, args.box_for_scrolling ? *args.box_for_scrolling : args.box).Value());
        }
    }

    if (args.box.button_fired) {
        nav.focused_panel = args.panel;
        FocusItem(nav, args.panel, args.id);
    }

    if (is_focused && !nav.temp_focused_items[panel_index]) FocusItem(nav, args.panel, args.id);

    return button_fired_from_keyboard;
}

} // namespace key_nav

BrowserItemResult
DoBrowserItem(GuiBuilder& builder, CommonBrowserState& state, BrowserItemOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(builder, true);

    auto const container = DoBox(builder,
                                 {
                                     .parent = options.parent,
                                     .id_extra = options.id_extra,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_direction = layout::Direction::Row,
                                     },
                                 });

    auto item =
        DoBox(builder,
              {
                  .parent = container,
                  .background_fill_colours = Col {.c = options.is_current ? Col::Highlight : Col::None},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_direction = layout::Direction::Row,
                  },
                  .tooltip = options.tooltip,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_justification = TooltipJustification::LeftOrRight,
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
    }

    if (item.button_fired) {
        ShowTipIfNeeded(options.notifications,
                        options.store,
                        0xe9bdfea0aae12dce,
                        "Double-click to load and close; single-click to load only."_s);
    }

    auto const favourite_toggled =
        !!DoBox(builder,
                {
                    .parent = container,
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
                    .text_justification = TextJustification::CentredLeft,
                    .layout {
                        .size = {24, layout::k_fill_parent},
                    },
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

    return {item, favourite_toggled, item.button_fired || fired_via_keyboard};
}

Box DoBrowserItemsRoot(GuiBuilder& builder) {
    return DoBox(builder,
                 {
                     .layout {
                         .size = layout::k_fill_parent,
                         .contents_gap = k_browser_spacing,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

struct FolderFilterTreeContext {
    FolderFilterItemInfoLookupTable const& folder_infos;
    u8 indent {};
};

struct FolderFilterTreeOptions {
    RightClickMenuState::Function do_right_click_menu {};
    bool no_lhs_border {};
    bool parent_card_is_selected {};
};

static void DoFolderFilterAndChildren(GuiBuilder& builder,
                                      CommonBrowserState& state,
                                      Box const& parent,
                                      FolderNode const* folder,
                                      FolderFilterTreeContext& context,
                                      FolderFilterTreeOptions const& options) {
    // We want to stop if we find a preset bank within the preset bank.
    if (folder->user_data.As<PresetFolderListing const>())
        if (auto const bank = PresetBankAtNode(*folder);
            bank && folder->parent && bank != PresetBankAtNode(*folder->parent))
            return;

    bool is_active = options.parent_card_is_selected;
    if (!is_active && !options.no_lhs_border) {
        for (auto f = folder; f; f = f->parent) {
            if (state.Filter(BrowserFilter::Folder).Contains(f->Hash())) {
                is_active = true;
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

    auto this_info = context.folder_infos.Find(folder);
    ASSERT(this_info);

    if (this_info->total_available == 0) return;

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
                    .tooltip = folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
                    .filter = state.Filter(BrowserFilter::Folder),
                    .clicked_key = folder_hash,
                    .filter_mode = state.filter_mode,
                },
            .is_active = is_active,
            .indent = context.indent,
        });

    if (options.do_right_click_menu)
        DoRightClickMenuForBox(builder, state, button, folder->Hash(), options.do_right_click_menu);

    ++context.indent;
    for (auto* child = folder->first_child; child; child = child->next)
        DoFolderFilterAndChildren(builder, state, parent, child, context, options);
    --context.indent;
}

static void HandleFilterButtonClick(GuiBuilder& builder,
                                    CommonBrowserState& state,
                                    FilterButtonCommonOptions const& options,
                                    bool single_exclusive_mode_for_and = false) {
    state.keyboard_navigation.focused_panel = BrowserKeyboardNavigation::Panel::Filters;
    auto display_name = builder.arena.Clone(options.text);
    switch (options.filter_mode) {
        case FilterMode::Single: {
            state.ClearAll();
            if (!options.is_selected) options.filter.Add(options.clicked_key, display_name);
            break;
        }
        case FilterMode::MultipleAnd: {
            if (single_exclusive_mode_for_and) {
                // In card mode, we assume that each item can only belong to a single card,
                // so, AND mode is not useful. Instead, we treat it like Single mode, except
                // we only clear the current filter, not all state.
                options.filter.Clear();
                if (!options.is_selected) options.filter.Add(options.clicked_key, display_name);
            } else {
                if (options.is_selected)
                    options.filter.Remove(options.clicked_key);
                else
                    options.filter.Add(options.clicked_key, display_name);
            }
            break;
        }
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

Box DoFilterButton(GuiBuilder& builder,
                   CommonBrowserState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(builder, true);

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    f32 const lr_spacing = 4;

    auto const button =
        DoBox(builder,
              {
                  .parent = options.common.parent,
                  .id_extra = options.common.id_extra,
                  .background_fill_colours =
                      options.common.is_selected
                          ? Colours {Col {.c = Col::Highlight, .dark_mode = options.dark_mode}}
                          : Colours {ColSet {
                                .base = Col {.c = Col::Background2, .dark_mode = options.dark_mode},
                                .hot = Col {.c = Col::Surface1, .dark_mode = options.dark_mode},
                                .active = Col {.c = Col::Surface1, .dark_mode = options.dark_mode},
                            }},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .corner_rounding = k_corner_rounding,
                  .layout {
                      .size {
                          layout::k_hug_contents,
                          k_browser_item_height,
                      },
                      .margins = {.b = options.no_bottom_margin ? 0 : k_browser_spacing / 2},
                      .contents_padding {
                          .l = !options.icon ? lr_spacing : 0,
                          .r = lr_spacing,
                      },
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = options.common.tooltip,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_justification = TooltipJustification::LeftOrRight,
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
    bool const dark_mode_text = !options.common.is_selected && options.dark_mode;

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

    if (button.button_fired) HandleFilterButtonClick(builder, state, options.common);

    if (options.right_click_menu)
        DoRightClickMenuForBox(builder, state, button, options.common.clicked_key, options.right_click_menu);

    return button;
}

namespace filter_card_box {
constexpr f32 k_outer_pad = 6.0f;
constexpr f32 k_selection_left_border_width = 4;
constexpr f32 k_tree_indent = 10;
} // namespace filter_card_box

Box DoFilterTreeButton(GuiBuilder& builder,
                       CommonBrowserState& state,
                       FilterItemInfo const& info,
                       FilterTreeButtonOptions const& options) {
    using namespace filter_card_box;
    auto const scoped_tooltips = ScopedEnableTooltips(builder, true);

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const button_outer = DoBox(builder,
                                    {
                                        .parent = options.common.parent,
                                        .id_extra = options.common.id_extra,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        },
                                    });

    if (options.is_active) {
        DoBox(builder,
              {
                  .parent = button_outer,
                  .background_fill_colours = Col {.c = Col::Highlight},
                  .layout {
                      .size = {k_selection_left_border_width, layout::k_fill_parent},
                  },
              });
    }

    auto const button =
        DoBox(builder,
              {
                  .parent = button_outer,
                  .id_extra = options.common.id_extra,
                  .background_fill_colours =
                      ColSet {
                          .base {
                              .c = (options.common.is_selected ? Col::Highlight300 : Col::None),
                              .alpha = 37,
                          },
                          .hot {
                              .c = options.common.is_selected ? Col::Highlight200 : Col::Overlay0,
                              .dark_mode = true,
                              .alpha = 37,
                          },
                          .active {
                              .c = options.common.is_selected ? Col::Highlight200 : Col::Overlay0,
                              .dark_mode = true,
                              .alpha = 37,
                          },
                      },
                  .background_fill_auto_hot_active_overlay = false,
                  .round_background_corners =
                      (options.common.is_selected && options.is_active) ? (Corners)0b0110 : (Corners)0b1111,
                  .corner_rounding = k_corner_rounding,
                  .layout {
                      .size {
                          layout::k_fill_parent,
                          k_browser_item_height,
                      },
                      .contents_padding {
                          .l = k_outer_pad + (options.indent * k_tree_indent),
                          .r = k_outer_pad,
                      },
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = options.common.tooltip,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_justification = TooltipJustification::LeftOrRight,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    Col const text_cols = {
        .c = num_used != 0 ? Col::Text : Col::Overlay2,
        .dark_mode = true,
    };

    DoBox(builder,
          {
              .parent = button,
              .text = options.display_text.size ? options.display_text : options.common.text,
              .size_from_text = false,
              .font = options.font_override.ValueOr(FontType::Body),
              .text_colours = text_cols,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = f32x2 {layout::k_fill_parent, k_font_body_size},
              },
          });

    DoBox(builder,
          {
              .parent = button,
              .text = fmt::FormatInline<16>("({})"_s, info.total_available),
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = text_cols,
              .text_justification = TextJustification::CentredLeft,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
          });

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

    if (button.button_fired || fired_via_keyboard) HandleFilterButtonClick(builder, state, options.common);

    return button;
}

Box DoFilterCard(GuiBuilder& builder,
                 CommonBrowserState& state,
                 FilterItemInfo const& info,
                 FilterCardOptions const& options) {
    using namespace filter_card_box;
    auto const scoped_tooltips = ScopedEnableTooltips(builder, true);
    bool const is_selected = options.common.is_selected;

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const collapse_id = options.common.clicked_key ^ HashFnv1a("card-collapse");
    auto& card_toggled_ids =
        options.default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
    if (options.store) LoadCollapseStateFromStore(*options.store, card_toggled_ids, collapse_id);
    bool const card_collapsed = Contains(card_toggled_ids, collapse_id) != options.default_collapsed;

    auto const card_outer =
        DoBox(builder,
              {
                  .parent = options.common.parent,
                  .id_extra = options.common.id_extra,
                  .drop_shadow = !card_collapsed,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .margins = {.t = 2, .b = card_collapsed ? 0 : k_browser_spacing / 2},
                      .contents_direction = layout::Direction::Row,
                  },
              });

    Optional<ImageID> background_image1 {};
    Optional<ImageID> background_image2 {};
    Optional<ImageID> icon {};
    bool has_icon = false;
    if (options.library_id) {
        auto imgs = GetLibraryImages(options.library_images,
                                     builder.imgui,
                                     *options.library_id,
                                     options.sample_library_server,
                                     options.instance_index,
                                     LibraryImagesTypes::All);
        has_icon = imgs.icon.HasValue() && *imgs.icon != k_invalid_image_id;
        if (builder.IsInputAndRenderPass()) {
            if (builder.imgui.IsRectVisible(
                    builder.imgui.ViewportRectToWindowRect(*BoxRect(builder, card_outer)))) {
                if (!card_collapsed) {
                    background_image1 = imgs.blurred_background;
                    background_image2 = imgs.background;
                }
                icon = imgs.icon;
            }
        }
    }

    auto const base_background =
        DoBox(builder,
              {
                  .parent = card_outer,
                  .background_fill_colours =
                      Col {.c = card_collapsed ? Col::None : Col::Background2, .dark_mode = true},
                  .background_tex = background_image1.NullableValue(),
                  .background_tex_alpha = 180,
                  .background_tex_fill_mode = BackgroundTexFillMode::Cover,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_direction = layout::Direction::Row,
                  },
              });

    auto const card = DoBox(builder,
                            {
                                .parent = base_background,
                                .background_tex = background_image2.NullableValue(),
                                .background_tex_alpha = 15,
                                .background_tex_fill_mode = BackgroundTexFillMode::Cover,
                                .round_background_corners = 0b1111,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_direction = layout::Direction::Row,
                                },
                            });

    auto const card_content = DoBox(builder,
                                    {
                                        .parent = card,
                                        .round_background_corners = 0b1111,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

    auto const card_top =
        DoBox(builder,
              {
                  .parent = card_content,
                  .background_fill_colours =
                      ColSet {
                          .base {
                              .c = Col::None,
                          },
                          .hot {
                              .c = Col::Overlay2,
                              .dark_mode = true,
                              .alpha = 37,
                          },
                          .active {
                              .c = Col::Overlay2,
                              .dark_mode = true,
                              .alpha = 37,
                          },
                      },
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.l = 2, .r = 2, .tb = 4},
                      .contents_gap = 2,
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                  },
                  .tooltip = options.common.tooltip,
                  .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                  .tooltip_justification = TooltipJustification::LeftOrRight,
                  .button_behaviour = imgui::ButtonConfig {},
              });

    if (options.right_click_menu)
        DoRightClickMenuForBox(builder,
                               state,
                               card_top,
                               options.common.clicked_key,
                               options.right_click_menu);

    auto const top_row = DoBox(builder,
                               {
                                   .parent = card_top,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_outer_pad,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

    auto const title_row_height = k_font_body_size;

    DoBox(builder,
          {
              .parent = top_row,
              .text = card_collapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN,
              .font = FontType::Icons,
              .font_size = k_font_icons_size * 0.6f,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
              .text_justification = TextJustification::Centred,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {12, title_row_height},
              },
          });

    if (has_icon) {
        DoBox(builder,
              {
                  .parent = top_row,
                  .background_tex = icon.NullableValue(),
                  .layout {
                      .size = 18.0f,
                  },
              });
    }

    auto const title_box = DoBox(
        builder,
        {
            .parent = top_row,
            .layout {
                .size = {layout::k_fill_parent, card_collapsed ? title_row_height : layout::k_hug_contents},
                .contents_gap = 8,
                .contents_direction = layout::Direction::Row,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
            },
        });

    Col const title_text_colours = {
        .c = num_used != 0 ? Col::Text : Col::Overlay2,
        .dark_mode = true,
    };
    Col const subtitle_text_colours = {
        .c = num_used != 0 ? Col::Subtext1 : Col::Overlay2,
        .dark_mode = true,
    };

    DoBox(builder,
          {
              .parent = title_box,
              .text = options.common.text,
              .wrap_width = card_collapsed ? k_no_wrap : k_wrap_to_parent,
              .size_from_text = !card_collapsed,
              .font = FontType::Body,
              .text_colours = title_text_colours,
              .text_overflow =
                  card_collapsed ? TextOverflowType::ShowDotsOnRight : TextOverflowType::AllowOverflow,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = card_collapsed ? f32x2 {layout::k_fill_parent, title_row_height} : f32x2 {},
              },
          });

    bool const has_folder_children = options.folder && options.folder->first_child;
    DoBox(builder,
          {
              .parent = title_box,
              .text = fmt::FormatInline<32>("({})"_s, info.total_available),
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = subtitle_text_colours,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {layout::k_hug_contents, title_row_height},
              },
          });

    if (!card_collapsed) {
        DoBox(builder,
              {
                  .parent = card_top,
                  .text = options.subtext,
                  .wrap_width = k_wrap_to_parent,
                  .size_from_text = true,
                  .font = FontType::Heading3,
                  .text_colours = subtitle_text_colours,
                  .parent_dictates_hot_and_active = true,
                  .layout {.margins = {.l = 2}},
              });
    }

    if (card_top.button_fired) {
        if (Contains(card_toggled_ids, collapse_id))
            dyn::RemoveValue(card_toggled_ids, collapse_id);
        else
            dyn::Append(card_toggled_ids, collapse_id);

        if (options.store) SaveCollapseStateToStore(*options.store, card_toggled_ids, collapse_id);
    }

    if (!card_collapsed) {
        auto const folder_box =
            DoBox(builder,
                  {
                      .parent = card_content,
                      .background_fill_colours =
                          ColSet {
                              .base {.c = Col::Background0, .dark_mode = true, .alpha = 128},
                              .hot {.c = Col::Overlay1, .dark_mode = true, .alpha = 128},
                              .active {.c = Col::Overlay1, .dark_mode = true, .alpha = 128},
                          },
                      .round_background_corners = 0b0011,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.tb = 3},
                          .contents_direction = layout::Direction::Column,
                      },
                  });

        // "All" item: selects the root node (all children).
        DoFilterTreeButton(
            builder,
            state,
            info,
            {
                .common =
                    {
                        .parent = folder_box,
                        .id_extra = options.common.id_extra,
                        .is_selected = options.common.is_selected,
                        .text = fmt::Format(builder.arena,
                                            "All {}{}"_s,
                                            options.common.text,
                                            options.all_items_suffix),
                        .filter = options.common.filter,
                        .clicked_key = options.common.clicked_key,
                        .filter_mode = options.common.filter_mode,
                    },
                .is_active = is_selected,
                .indent = 0,
                .font_override = FontType::BodyItalic,
                .display_text = fmt::Format(builder.arena, "All{}"_s, options.all_items_suffix),
            });

        // Folder children.
        if (has_folder_children) {
            FolderFilterTreeContext context {.folder_infos = options.folder_infos, .indent = 0};
            FolderFilterTreeOptions const folder_options {
                .do_right_click_menu = options.right_click_menu,
                .parent_card_is_selected = is_selected,
            };
            for (auto* child = options.folder->first_child; child; child = child->next)
                DoFolderFilterAndChildren(builder, state, folder_box, child, context, folder_options);
        }

        // Final border around everything.
        if (auto const card_r = BoxRect(builder, base_background)) {
            auto const wnd_r = builder.imgui.ViewportRectToWindowRect(*card_r);
            builder.imgui.draw_list->AddRect(wnd_r, Rgba(255, 255, 255, 0.2f), k_corner_rounding);
        }
    }

    return card_top;
}

BrowserSection::Result BrowserSection::Do(GuiBuilder& builder) {
    if (!init) {
        auto& toggled_ids =
            default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
        if (store) LoadCollapseStateFromStore(*store, toggled_ids, id);
        is_collapsed = Contains(toggled_ids, id) != default_collapsed;
        init = true;
    } else {
        if (is_collapsed) return State::Collapsed;
    }

    if (is_box_init) return box_cache;

    builder.imgui.PushId(id);
    DEFER { builder.imgui.PopId(); };

    auto const container =
        DoBox(builder,
              {
                  .parent = parent,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.l = subsection ? k_browser_spacing / 2 : 0},
                          .contents_gap = f32x2 {0, bigger_contents_gap ? k_browser_spacing * 1.5f : 0},
                          .contents_direction = layout::Direction::Column,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
              });

    if (heading || folder) {
        auto const heading_container =
            DoBox(builder,
                  {
                      .parent = container,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1111,
                      .corner_rounding = k_corner_rounding,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_gap = k_browser_spacing / 2,
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
                      .tooltip = folder ? TooltipString {"Folder"_s} : k_nullopt,
                      .tooltip_avoid_viewport_id = builder.imgui.curr_viewport->root_viewport->id,
                      .tooltip_justification = TooltipJustification::LeftOrRight,
                      .button_behaviour = imgui::ButtonConfig {},
                  });

        if (heading_container.button_fired) {
            auto& toggled_ids =
                default_collapsed ? state.expanded_filter_headers : state.collapsed_filter_headers;
            if (Contains(toggled_ids, id))
                dyn::RemoveValue(toggled_ids, id);
            else
                dyn::Append(toggled_ids, id);

            if (store) SaveCollapseStateToStore(*store, toggled_ids, id);
        }

        if (right_click_menu) DoRightClickMenuForBox(builder, state, heading_container, id, right_click_menu);

        DoBox(builder,
              {
                  .parent = heading_container,
                  .text = is_collapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * 0.6f,
                  .text_colours = Col {.c = Col::Subtext0, .dark_mode = dark_mode},
                  .layout {
                      .size = k_font_icons_size * 0.4f,
                  },
              });

        if (icon) {
            DoBox(builder,
                  {
                      .parent = heading_container,
                      .text = *icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_font_icons_size * 0.7f,
                      .text_colours = Col {.c = Col::Text, .dark_mode = dark_mode},
                  });
        }

        {
            DynamicArray<char> buf {builder.arena};

            String text = heading.ValueOr({});

            if (capitalise) {
                dyn::Resize(buf, text.size);
                for (auto i : Range(text.size))
                    buf[i] = ToUppercaseAscii(text[i]);
                text = buf;
            } else if (folder) {
                DynamicArrayBounded<String, sample_lib::k_max_folders + 1> parts;
                for (auto f = folder; f; f = f->parent)
                    dyn::Append(parts, f->display_name.size ? f->display_name : f->name);

                if (skip_root_folder && parts.size > 1) dyn::Pop(parts);

                // We want to display the last part in a less prominent way.
                Optional<String> top_folder_name {};
                if (parts.size > 1) {
                    top_folder_name = Last(parts);
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
                          .wrap_width = k_wrap_to_parent,
                          .size_from_text = true,
                          .font = FontType::Heading3,
                          .text_colours = Col {.c = Col::Text, .dark_mode = dark_mode},
                          .parent_dictates_hot_and_active = true,
                          .layout {
                              .margins = {.b = k_browser_spacing / 2},
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
                              .contents_padding = {.t = k_browser_spacing / 2},
                              .contents_gap = k_browser_spacing / 2,
                              .contents_direction = layout::Direction::Row,
                              .contents_multiline = true,
                              .contents_align = layout::Alignment::Start,
                          },
                      });
    return box_cache;
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

static void DoBrowserLibraryFilters(GuiBuilder& builder,
                                    BrowserPopupContext& context,
                                    Box const& parent,
                                    LibraryFilters const& library_filters) {
    if (library_filters.libraries.size) {
        BrowserSection section = {
            .state = context.state,
            .id = context.browser_id ^ HashFnv1a("libraries-section"),
            .parent = parent,
            .heading = "LIBRARIES"_s,
            .multiline_contents = !library_filters.card_view,
            .default_collapsed = !library_filters.card_view,
            .dark_mode = true,
            .store = &context.store,
        };

        for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
            ASSERT(lib_id);

            auto const lib_ptr = library_filters.libraries_table.Find(lib_id, lib_hash);
            if (!lib_ptr) continue;
            auto const& lib = *lib_ptr;

            if (!MatchesFilterSearch(lib->name, context.state.filter_search)) continue;

            Box button;
            if (library_filters.card_view) {
                auto const folder = &lib->root_folders[ToInt(library_filters.resource_type)];

                auto const is_selected = context.state.Filter(BrowserFilter::Library).Contains(lib_hash);

                if (section.Do(builder) == BrowserSection::State::Collapsed) break;

                button = DoFilterCard(builder,
                                      context.state,
                                      lib_info,
                                      FilterCardOptions {
                                          .common =
                                              {
                                                  .parent = section.Do(builder).Get<Box>(),
                                                  .id_extra = lib_hash,
                                                  .is_selected = is_selected,
                                                  .text = lib->name,
                                                  .tooltip = FunctionRef<String()>([&]() -> String {
                                                      auto lib = sample_lib_server::FindLibraryRetained(
                                                          context.sample_library_server,
                                                          lib_id);
                                                      DEFER { lib.Release(); };

                                                      DynamicArray<char> buf {builder.arena};
                                                      fmt::Append(buf, "{} by {}.", lib->name, lib->author);
                                                      if (lib) {
                                                          if (lib->description)
                                                              fmt::Append(buf, "\n\n{}", lib->description);
                                                      }
                                                      return buf.ToOwnedSpan();
                                                  }),
                                                  .filter = context.state.Filter(BrowserFilter::Library),
                                                  .clicked_key = lib_hash,
                                                  .filter_mode = context.state.filter_mode,
                                              },
                                          .library_id = lib_id,
                                          .library_images = library_filters.library_images,
                                          .sample_library_server = context.sample_library_server,
                                          .instance_index = library_filters.instance_index,
                                          .subtext = ({
                                              String s;
                                              if (lib) s = builder.arena.Clone(lib->tagline);
                                              s;
                                          }),
                                          .folder_infos = library_filters.folders,
                                          .folder = folder,
                                          .all_items_suffix = library_filters.resource_type ==
                                                                      sample_lib::ResourceType::Instrument
                                                                  ? " Instruments"_s
                                                                  : " IRs"_s,
                                          .default_collapsed = true,
                                          .store = &context.store,
                                      });
            } else {
                if (section.Do(builder) == BrowserSection::State::Collapsed) break;

                auto const imgs = GetLibraryImages(library_filters.library_images,
                                                   builder.imgui,
                                                   lib_id,
                                                   context.sample_library_server,
                                                   library_filters.instance_index,
                                                   LibraryImagesTypes::Icon);

                button = DoFilterButton(
                    builder,
                    context.state,
                    lib_info,
                    FilterButtonOptions {
                        .common =
                            {
                                .parent = section.Do(builder).Get<Box>(),
                                .id_extra = lib_hash,
                                .is_selected =
                                    context.state.Filter(BrowserFilter::Library).Contains(lib_hash),
                                .text = lib->name,
                                .tooltip = FunctionRef<String()>([&]() -> String {
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
                                .filter = context.state.Filter(BrowserFilter::Library),
                                .clicked_key = lib_hash,
                                .filter_mode = context.state.filter_mode,
                            },
                        .icon = ({
                            ImageID const* tex = nullptr;
                            if (imgs.icon) tex = imgs.icon.NullableValue();
                            tex;
                        }),
                    });
            }

            if (lib_hash != sample_lib::k_builtin_library_id)
                DoRightClickMenuForBox(builder,
                                       context.state,
                                       button,
                                       lib_hash,
                                       [](GuiBuilder& builder,
                                          BrowserPopupContext& context,
                                          BrowserPopupOptions const& options) {
                                           if (options.library_filters)
                                               DoLibraryRightClickMenu(builder,
                                                                       context,
                                                                       context.state.right_click_menu_state,
                                                                       *options.library_filters);
                                       });
        }

        if (library_filters.additional_pseudo_card) {
            auto options = *library_filters.additional_pseudo_card;
            if (MatchesFilterSearch(options.common.text, context.state.filter_search) &&
                section.Do(builder) != BrowserSection::State::Collapsed) {
                options.common.parent = section.Do(builder).Get<Box>();

                DoFilterCard(builder,
                             context.state,
                             ({
                                 FilterItemInfo i {};
                                 if (library_filters.additional_pseudo_card_info)
                                     i = *library_filters.additional_pseudo_card_info;
                                 i;
                             }),
                             options);
            }
        }
    }
}

static void DoBrowserLibraryAuthorFilters(GuiBuilder& builder,
                                          BrowserPopupContext& context,
                                          Box const& parent,
                                          LibraryFilters const& library_filters) {
    if (library_filters.library_authors.size) {
        BrowserSection section = {
            .state = context.state,
            .id = context.browser_id ^ HashFnv1a("library-authors-section"),
            .parent = parent,
            .heading = "LIBRARY AUTHORS"_s,
            .multiline_contents = true,
            .default_collapsed = true,
            .dark_mode = true,
            .store = &context.store,
        };

        for (auto const [author, author_info, author_hash] : library_filters.library_authors) {
            if (!MatchesFilterSearch(author, context.state.filter_search)) continue;
            if (section.Do(builder) == BrowserSection::State::Collapsed) break;
            auto const is_selected = context.state.Filter(BrowserFilter::LibraryAuthor).Contains(author_hash);
            DoFilterButton(builder,
                           context.state,
                           author_info,
                           {
                               .common =
                                   {
                                       .parent = section.Do(builder).Get<Box>(),
                                       .id_extra = author_hash,
                                       .is_selected = is_selected,
                                       .text = author,
                                       .filter = context.state.Filter(BrowserFilter::LibraryAuthor),
                                       .clicked_key = author_hash,
                                       .filter_mode = context.state.filter_mode,
                                   },
                           });
        }
    }
}

void DoBrowserTagsFilters(GuiBuilder& builder,
                          BrowserPopupContext& context,
                          Box const& parent,
                          TagsFilters const& tags_filters) {
    if (!tags_filters.available_tags.AnyValuesSet() && !tags_filters.has_untagged) return;

    // Group available tags by category.
    OrderedHashTable<TagCategory, OrderedHashTable<TagType, FilterItemInfo>> standard_tags {};
    tags_filters.available_tags.ForEachSetBit([&](usize bit) {
        auto const tag = (TagType)bit;
        auto const tag_and_cat = LookupTagName(GetTagInfo(tag).name);
        if (tag_and_cat) {
            auto& tags_for_category =
                standard_tags.FindOrInsertGrowIfNeeded(builder.arena, tag_and_cat->category, {}).element.data;
            tags_for_category.InsertGrowIfNeeded(builder.arena, tag, tags_filters.tags[bit]);
        }
    });

    BrowserSection tags_section {
        .state = context.state,
        .id = context.browser_id ^ HashFnv1a("tags-section"),
        .parent = parent,
        .heading = "TAGS",
        .multiline_contents = false,
        .bigger_contents_gap = true,
        .default_collapsed = true,
        .dark_mode = true,
        .store = &context.store,
    };

    for (auto [category, tags_for_category, category_hash] : standard_tags) {
        auto const category_info = Tags(category);

        BrowserSection inner_section {
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
            .store = &context.store,
        };

        for (auto const [tag, filter_item_info, _] : tags_for_category) {
            auto const tag_info = GetTagInfo(tag);
            if (!MatchesFilterSearch(tag_info.name, context.state.filter_search)) continue;

            if (tags_section.Do(builder) == BrowserSection::State::Collapsed) break;
            inner_section.parent = tags_section.Do(builder).Get<Box>();
            if (inner_section.Do(builder) == BrowserSection::State::Collapsed) break;

            bool const is_selected = context.state.Filter(BrowserFilter::Tags).Contains((u64)tag);
            DoFilterButton(builder,
                           context.state,
                           filter_item_info,
                           {
                               .common =
                                   {
                                       .parent = inner_section.Do(builder).Get<Box>(),
                                       .id_extra = (u64)tag,
                                       .is_selected = is_selected,
                                       .text = tag_info.name,
                                       .filter = context.state.Filter(BrowserFilter::Tags),
                                       .clicked_key = (u64)tag,
                                       .filter_mode = context.state.filter_mode,
                                   },
                           });
        }
    }

    if (tags_filters.has_untagged) {
        if (!MatchesFilterSearch(k_untagged_tag_name, context.state.filter_search)) return;

        if (tags_section.Do(builder) == BrowserSection::State::Collapsed) return;

        auto const is_selected = context.state.Filter(BrowserFilter::Tags).Contains(k_untagged_key);
        DoFilterButton(builder,
                       context.state,
                       tags_filters.untagged_info,
                       {
                           .common =
                               {
                                   .parent = tags_section.Do(builder).Get<Box>(),
                                   .id_extra = k_untagged_key,
                                   .is_selected = is_selected,
                                   .text = k_untagged_tag_name,
                                   .filter = context.state.Filter(BrowserFilter::Tags),
                                   .clicked_key = k_untagged_key,
                                   .filter_mode = context.state.filter_mode,
                               },
                       });
    }
}

static String FilterModeText(FilterMode mode) {
    switch (mode) {
        case FilterMode::Single: return "One";
        case FilterMode::MultipleAnd: return "Multiple: AND";
        case FilterMode::MultipleOr: return "Multiple: OR";
        case FilterMode::Count: break;
    }
    PanicIfReached();
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

static String FilterModeDescription(FilterMode mode) {
    switch (mode) {
        case FilterMode::Single: return "Only one filter can be selected at a time.";
        case FilterMode::MultipleAnd: return "Items must match all selected filters.";
        case FilterMode::MultipleOr: return "Items can match any selected filter.";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static void DoMoreOptionsMenu(GuiBuilder& builder, BrowserPopupContext& context) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const filter_mode : EnumIterator<FilterMode>()) {
        if (MenuItem(builder,
                     root,
                     {
                         .text = FilterModeText(filter_mode),
                         .subtext = FilterModeDescription(filter_mode),
                         .is_selected = context.state.filter_mode == filter_mode,
                     },
                     SourceLocationHash() ^ (u64)filter_mode)
                .button_fired) {
            if (context.state.filter_mode != FilterMode::Single && filter_mode == FilterMode::Single)
                context.state.ClearToOne();
            context.state.filter_mode = filter_mode;
        }
    }
}

static void DoBrowserPopupInternal(GuiBuilder& builder,
                                   BrowserPopupContext& context,
                                   BrowserPopupOptions const& options) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = {layout::k_hug_contents, options.height},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const title_container =
            DoBox(builder,
                  {
                      .parent = root,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_padding = {.lrtb = k_browser_spacing},
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                  });
        DoBox(builder,
              {
                  .parent = title_container,
                  .text = options.title,
                  .size_from_text = true,
                  .size_from_text_preserve_height = true,
                  .font = FontType::Heading2,
                  .layout {
                      .size = k_font_heading2_size,
                  },
              });

        {

            auto const rhs_top = DoBox(builder,
                                       {
                                           .parent = title_container,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lr = k_browser_spacing * 2},
                                               .contents_align = layout::Alignment::End,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                           },
                                       });

            if (auto const& btn = options.rhs_top_button) {
                auto const btn_container = DoBox(builder,
                                                 {
                                                     .parent = rhs_top,
                                                     .layout {
                                                         .size = layout::k_hug_contents,
                                                         .margins = {.r = k_browser_spacing * 2},
                                                     },
                                                 });

                // Custom button with icon, styled like TextButton
                auto const button = DoBox(
                    builder,
                    {
                        .parent = btn_container,
                        .background_fill_colours = Col {.c = Col::Background2},
                        .background_fill_auto_hot_active_overlay = !btn->disabled,
                        .round_background_corners = 0b1111,
                        .layout {
                            .size = {layout::k_hug_contents, layout::k_hug_contents},
                            .contents_padding = {.lr = k_button_padding_x, .tb = k_button_padding_y},
                            .contents_gap = 3,
                            .contents_direction = layout::Direction::Row,
                            .contents_align = layout::Alignment::Start,
                            .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                        },
                        .tooltip = btn->disabled ? TooltipString {k_nullopt} : btn->tooltip,
                        .button_behaviour =
                            btn->disabled ? k_nullopt : Optional<imgui::ButtonConfig>(imgui::ButtonConfig {}),
                    });

                // Button text
                DoBox(builder,
                      {
                          .parent = button,
                          .text = btn->text,
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = Col {.c = btn->disabled ? Col::Surface1 : Col::Text},
                          .text_justification = TextJustification::CentredLeft,
                          .text_overflow = TextOverflowType::AllowOverflow,
                      });

                // X icon
                DoBox(builder,
                      {
                          .parent = button,
                          .text = ICON_FA_XMARK,
                          .size_from_text = true,
                          .font = FontType::Icons,
                          .font_size = k_font_body_size,
                          .text_colours = Col {.c = btn->disabled ? Col::Surface1 : Col::Subtext0},
                      });

                if (button.button_fired && !btn->disabled) btn->on_fired();
            }

            for (auto const& btn : ArrayT<BrowserPopupOptions::Button>({
                     {
                         .text = ICON_FA_CARET_LEFT,
                         .tooltip = fmt::Format(builder.arena, "Load previous {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_previous,
                     },
                     {
                         .text = ICON_FA_CARET_RIGHT,
                         .tooltip = fmt::Format(builder.arena, "Load next {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_next,
                     },
                     {
                         .text = ICON_FA_SHUFFLE,
                         .tooltip = fmt::Format(builder.arena, "Load random {}", options.item_type_name),
                         .icon_scaling = 0.8f,
                         .on_fired = options.on_load_random,
                     },
                     {
                         .text = ICON_FA_LOCATION_ARROW,
                         .tooltip =
                             fmt::Format(builder.arena, "Scroll to current {}", options.item_type_name),
                         .icon_scaling = 0.8f,
                         .on_fired = options.on_scroll_to_show_selected,
                     },
                 })) {
                if (!btn.on_fired) continue;
                if (IconButton(builder,
                               rhs_top,
                               btn.text,
                               btn.tooltip,
                               k_font_heading2_size * btn.icon_scaling,
                               k_font_heading2_size,
                               Hash(btn.text))
                        .button_fired) {
                    btn.on_fired();
                }
            }
        }

        DoBox(builder,
              {
                  .parent = title_container,
                  .text = ICON_FA_XMARK,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .button_behaviour = imgui::ButtonConfig {.closes_popup_or_modal = true},
                  .extra_margin_for_mouse_events = 8,
              });
    }

    auto const main_section = DoBox(builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    {
        auto const lhs = DoBox(builder,
                               {
                                   .parent = main_section,
                                   .background_fill_colours = Col {.c = Col::Background1, .dark_mode = true},
                                   .round_background_corners = 0b0001,
                                   .corner_rounding = k_corner_rounding,
                                   .layout {
                                       .size = {options.filters_col_width, layout::k_fill_parent},
                                       .contents_padding = {.t = k_browser_spacing},
                                       .contents_gap = k_browser_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        {
            auto const lhs_top = DoBox(builder,
                                       {
                                           .parent = lhs,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lr = k_browser_spacing},
                                               .contents_gap = k_browser_spacing,
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Start,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                       });

            // Filter search box - always visible
            auto const filter_search_box =
                DoBox(builder,
                      {
                          .parent = lhs_top,
                          .background_fill_colours =
                              Col {
                                  .c = Col::Surface0,
                                  .dark_mode = true,
                              },
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_padding = {.lr = k_browser_spacing / 2},
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(builder,
                  {
                      .parent = filter_search_box,
                      .text = ICON_FA_MAGNIFYING_GLASS,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_browser_item_height * 0.8f,
                      .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                  });

            auto const filter_text_input =
                DoBox(builder,
                      {
                          .parent = filter_search_box,
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = {layout::k_fill_parent, k_browser_item_height},
                          },
                          .tooltip = "Search filters"_s,
                      });

            Optional<imgui::TextInputResult> filter_text_input_result {};
            if (auto const r = BoxRect(builder, filter_text_input)) {
                auto const window_r = builder.imgui.RegisterAndConvertRect(*r);
                filter_text_input_result = builder.imgui.TextInputBehaviour({
                    .rect_in_window_coords = window_r,
                    .id = filter_text_input.imgui_id,
                    .text = (String)context.state.filter_search,
                    .placeholder_text = options.filter_search_placeholder_text,
                    .input_cfg =
                        {
                            .x_padding = WwToPixels(4.0f),
                            .centre_align = false,
                            .escape_unfocuses = true,
                            .select_all_when_opening = true,
                            .multiline = false,
                            .multiline_wordwrap_hack = false,
                        },
                    .button_cfg =
                        {
                            .mouse_button = MouseButton::Left,
                            .event = MouseButtonEvent::Up,
                        },
                });

                DrawTextInput(builder.imgui,
                              *filter_text_input_result,
                              {
                                  .text_col = {.c = Col::Text, .dark_mode = true},
                                  .cursor_col = {.c = Col::Text, .dark_mode = true},
                                  .selection_col = {.c = Col::Highlight, .alpha = 128},
                              });
            }

            if (filter_text_input_result && filter_text_input_result->buffer_changed) {
                dyn::AssignFitInCapacity(context.state.filter_search, filter_text_input_result->text);
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }

            if (context.state.filter_search.size) {
                if (DoBox(builder,
                          {
                              .parent = filter_search_box,
                              .text = ICON_FA_XMARK,
                              .size_from_text = true,
                              .font = FontType::Icons,
                              .font_size = k_browser_item_height * 0.9f,
                              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                              .background_fill_auto_hot_active_overlay = true,
                              .tooltip = "Clear search"_s,
                              .button_behaviour = imgui::ButtonConfig {},
                          })
                        .button_fired) {
                    dyn::Clear(context.state.filter_search);
                }
            }

            {
                auto const filter_mode_button =
                    DoBox(builder,
                          {
                              .parent = lhs_top,
                              .font = FontType::Body,
                              .background_fill_auto_hot_active_overlay = true,
                              .border_colours = Col {.c = Col::Overlay0, .dark_mode = true},
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                  .contents_padding = {.lr = k_browser_spacing / 2},
                                  .contents_align = layout::Alignment::Middle,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                              .tooltip = FunctionRef<String()> {[&]() -> String {
                                  return fmt::Format(builder.arena,
                                                     "Filter mode: {}"_s,
                                                     FilterModeDescription(context.state.filter_mode));
                              }},
                              .button_behaviour = imgui::ButtonConfig {},
                          });

                DoBox(builder,
                      {
                          .parent = filter_mode_button,
                          .text = FilterModeTextAbbreviated(context.state.filter_mode),
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                          .parent_dictates_hot_and_active = true,
                      });

                auto const popup_id = builder.imgui.MakeId("filtermode");

                if (filter_mode_button.button_fired)
                    builder.imgui.OpenPopupMenu(popup_id, filter_mode_button.imgui_id);

                if (builder.imgui.IsPopupMenuOpen(popup_id))
                    DoBoxViewport(
                        builder,
                        {
                            .run = [&context](GuiBuilder& builder) { DoMoreOptionsMenu(builder, context); },
                            .bounds = filter_mode_button,
                            .imgui_id = popup_id,
                            .viewport_config = k_default_popup_menu_viewport,
                            .debug_name = "filtermode",
                        });
            }
        }

        DoBoxViewport(
            builder,
            {
                .run =
                    [&](GuiBuilder& builder) {
                        if (!options.library_filters && !options.tags_filters) return;

                        auto const root = DoBrowserItemsRoot(builder);

                        if (options.do_extra_filters_top) options.do_extra_filters_top(builder, root);

                        if (options.library_filters)
                            DoBrowserLibraryFilters(builder, context, root, *options.library_filters);

                        if (options.tags_filters)
                            DoBrowserTagsFilters(builder, context, root, *options.tags_filters);

                        if (options.library_filters)
                            DoBrowserLibraryAuthorFilters(builder, context, root, *options.library_filters);

                        if (options.do_extra_filters_bottom) options.do_extra_filters_bottom(builder, root);
                    },
                .bounds = DoBox(builder,
                                {
                                    .parent = lhs,
                                    .layout {
                                        .size = layout::k_fill_parent,
                                    },
                                }),
                .imgui_id = builder.imgui.MakeId("filters"),
                .viewport_config = ({
                    auto cfg = k_default_modal_subviewport;
                    cfg.draw_scrollbars = DrawModalScrollbarsDarkMode, cfg.scrollbar_inside_padding = true;
                    cfg.scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                                imgui::ViewportScrollbarVisibility::Auto};
                    cfg.padding = {.lr = k_browser_spacing};
                    cfg.scroll_line_size = k_browser_item_height;
                    cfg;
                }),
                .debug_name = "filters",
            });
    }

    {
        auto const rhs = DoBox(builder,
                               {
                                   .parent = main_section,
                                   .border_colours = Col {.c = Col::Surface1},
                                   .border_edges = 0b0100,
                                   .layout {
                                       .size = {options.rhs_width, layout::k_fill_parent},
                                       .contents_padding = {.t = k_browser_spacing},
                                       .contents_gap = k_browser_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            auto const rhs_top = DoBox(builder,
                                       {
                                           .parent = rhs,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_padding = {.lr = k_browser_spacing},
                                               .contents_gap = k_browser_spacing,
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Start,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                       });

            auto const search_and_fave_box =
                DoBox(builder,
                      {
                          .parent = rhs_top,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = k_browser_spacing / 2,
                              .contents_direction = layout::Direction::Row,
                          },
                      });

            if (options.show_search) {
                auto const search_box =
                    DoBox(builder,
                          {
                              .parent = search_and_fave_box,
                              .background_fill_colours = Col {.c = Col::Background2},
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_padding = {.lr = k_browser_spacing / 2},
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                DoBox(builder,
                      {
                          .parent = search_box,
                          .text = ICON_FA_MAGNIFYING_GLASS,
                          .size_from_text = true,
                          .font = FontType::Icons,
                          .font_size = k_browser_item_height * 0.8f,
                          .text_colours = Col {.c = Col::Subtext0},
                      });

                auto const text_input = DoBox(builder,
                                              {
                                                  .parent = search_box,
                                                  .round_background_corners = 0b1111,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, k_browser_item_height},
                                                  },
                                                  .tooltip = "Search (" MODIFIER_KEY_NAME "+F to focus)"_s,
                                              });

                Optional<imgui::TextInputResult> text_input_result {};
                if (auto const r = BoxRect(builder, text_input)) {
                    auto const window_r = builder.imgui.RegisterAndConvertRect(*r);
                    text_input_result = builder.imgui.TextInputBehaviour({
                        .rect_in_window_coords = window_r,
                        .id = text_input.imgui_id,
                        .text = (String)context.state.search,
                        .placeholder_text = options.item_search_placeholder_text,
                        .input_cfg =
                            {
                                .x_padding = WwToPixels(4.0f),
                                .centre_align = false,
                                .escape_unfocuses = true,
                                .select_all_when_opening = true,
                                .multiline = false,
                                .multiline_wordwrap_hack = false,
                            },
                        .button_cfg =
                            {
                                .mouse_button = MouseButton::Left,
                                .event = MouseButtonEvent::Up,
                            },
                    });

                    DrawTextInput(builder.imgui,
                                  *text_input_result,
                                  {
                                      .text_col = {Col::Text},
                                      .cursor_col = {Col::Text},
                                      .selection_col = {Col::Highlight},
                                  });
                }

                if (text_input_result && text_input_result->buffer_changed) {
                    dyn::AssignFitInCapacity(context.state.search, text_input_result->text);
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                }

                if (auto const r = BoxRect(builder, search_box);
                    r && builder.imgui.TextInputHasFocus(text_input.imgui_id))
                    key_nav::DrawFocusBox(builder, *r);

                if (builder.IsInputAndRenderPass() && builder.imgui.IsKeyboardFocus(text_input.imgui_id)) {
                    auto const& frame_input = GuiIo().in;
                    if (frame_input.Key(KeyCode::DownArrow).presses.size ||
                        frame_input.Key(KeyCode::Tab).presses.size) {
                        builder.imgui.SetTextInputFocus(0, {}, false);
                        key_nav::FocusPanel(context.state.keyboard_navigation,
                                            BrowserKeyboardNavigation::Panel::Items,
                                            true);
                    }
                }

                if (context.state.search.size) {
                    if (DoBox(builder,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_XMARK,
                                  .size_from_text = true,
                                  .font = FontType::Icons,
                                  .font_size = k_browser_item_height * 0.9f,
                                  .text_colours = Col {.c = Col::Subtext0},
                                  .background_fill_auto_hot_active_overlay = true,
                                  .tooltip = "Clear search"_s,
                                  .button_behaviour = imgui::ButtonConfig {},
                              })
                            .button_fired) {
                        dyn::Clear(context.state.search);
                    }
                }

                // CTRL+F focuses the search box.
                if (builder.IsInputAndRenderPass() && builder.imgui.IsKeyboardFocus(context.browser_id)) {
                    auto const& frame_input = GuiIo().in;
                    auto& frame_output = GuiIo().out;
                    frame_output.wants.keyboard_keys.Set(ToInt(KeyCode::F));
                    for (auto const& e : frame_input.Key(KeyCode::F).presses) {
                        if (e.modifiers.IsOnly(ModifierKey::Modifier)) {
                            builder.imgui.SetTextInputFocus(text_input.imgui_id, context.state.search, false);
                            builder.imgui.TextInputSelectAll();
                            break;
                        }
                    }
                }
            }

            {
                DoFilterButton(
                    builder,
                    context.state,
                    options.favourites_filter_info,
                    {
                        .common =
                            {
                                .parent = search_and_fave_box,
                                .is_selected = context.state.Filter(BrowserFilter::Favourites).HasSelected(),
                                .text = "Favourites"_s,
                                .filter = context.state.Filter(BrowserFilter::Favourites),
                                .clicked_key = 1,
                                .filter_mode = context.state.filter_mode,
                            },
                        .no_bottom_margin = true,
                        .dark_mode = false,
                    });
            }

            // For each selected hash, we want to show it with a dismissable button, like showing active
            // filters in a web ecommerce store.
            if (context.state.HasFilters() || context.state.search.size) {
                // Multiline container
                auto const container =
                    DoBox(builder,
                          {
                              .parent = rhs_top,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_gap = k_browser_spacing / 2,
                                  .contents_direction = layout::Direction::Row,
                                  .contents_multiline = true,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                              },
                          });

                bool first = true;

                auto const do_item =
                    [&](String category, String item, FilterMode mode, u64 id_extra = SourceLocationHash()) {
                        builder.imgui.PushId(id_extra);
                        DEFER { builder.imgui.PopId(); };

                        // If not first, we should add an 'AND' or 'OR' label depending on the filter mode.
                        if (!first) {
                            DoBox(builder,
                                  BoxConfig {
                                      .parent = container,
                                      .text = FilterModeTextAbbreviated(mode),
                                      .size_from_text = true,
                                      .size_from_text_preserve_height = true,
                                      .font = FontType::Heading3,
                                      .font_size = k_font_heading3_size * 0.8f,
                                      .text_colours = Col {.c = Col::Subtext0},
                                      .text_justification = TextJustification::CentredLeft,
                                      .layout {
                                          .size = {1, k_browser_item_height + (k_browser_spacing / 2)},
                                      },
                                  });
                        } else {
                            first = false;
                        }

                        // button container for the text, and the 'x' icon.
                        auto const button =
                            DoBox(builder,
                                  {
                                      .parent = container,
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
                                          .margins {.b = k_browser_spacing / 2},
                                          .contents_padding {.lr = k_default_spacing / 2},
                                          .contents_gap = k_default_spacing / 2,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Middle,
                                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                      },
                                      .tooltip = "Remove filter"_s,
                                      .button_behaviour = imgui::ButtonConfig {},
                                  });
                        // Text
                        DoBox(builder,
                              {
                                  .parent = button,
                                  .text = item.size
                                              ? (String)fmt::Format(builder.arena, "{}: {}", category, item)
                                              : category,
                                  .size_from_text = true,
                                  .font = FontType::Heading3,
                              });
                        DoBox(builder,
                              {
                                  .parent = button,
                                  .text = ICON_FA_XMARK,
                                  .font = FontType::Icons,
                                  .font_size = k_font_icons_size * 0.7f,
                                  .text_colours =
                                      ColSet {
                                          .base = Col {.c = Col::Subtext0},
                                          .hot = Col {.c = Col::Text},
                                          .active = Col {.c = Col::Text},
                                      },
                                  .parent_dictates_hot_and_active = true,
                                  .layout {
                                      .size = {k_font_icons_size * 0.7f, k_font_icons_size * 0.7f},
                                  },
                              });

                        return button.button_fired;
                    };

                for (auto& filter : context.state.filters) {
                    filter.ForEachSelected([&](String display_name, u64 key) {
                        if (do_item(filter.name, display_name, context.state.filter_mode, key))
                            filter.Remove(key);
                    });
                }

                if (context.state.search.size)
                    if (do_item("Name contains"_s, context.state.search, FilterMode::MultipleAnd))
                        dyn::Clear(context.state.search);
            }
        }

        DoBoxViewport(builder,
                      {
                          .run = [&](GuiBuilder& builder) { options.rhs_do_items(builder); },
                          .bounds = DoBox(builder,
                                          {
                                              .parent = rhs,
                                              .layout {
                                                  .size = layout::k_fill_parent,
                                              },
                                          }),
                          .imgui_id = builder.imgui.MakeId("rhs"),
                          .viewport_config = ({
                              auto cfg = k_default_modal_subviewport;
                              cfg.scrollbar_inside_padding = true;
                              cfg.scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                                          imgui::ViewportScrollbarVisibility::Auto};
                              cfg.padding = {.lr = k_browser_spacing};
                              cfg.scroll_line_size = k_browser_item_height;
                              cfg;
                          }),
                          .debug_name = "rhs",
                      });
    }

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

void DoBrowserModal(GuiBuilder& builder, BrowserPopupContext context, BrowserPopupOptions const& options) {
    key_nav::BeginFrame(builder.imgui, context.state.keyboard_navigation);

    DoBoxViewport(builder,
                  {
                      .run = [&](GuiBuilder& builder) { DoBrowserPopupInternal(builder, context, options); },
                      .bounds = context.state.absolute_button_rect,
                      .imgui_id = context.browser_id,
                      .viewport_config = ({
                          auto cfg = k_default_modal_viewport;
                          cfg.positioning = imgui::ViewportPositioning::AutoPosition;
                          cfg.auto_size = true;
                          cfg;
                      }),
                  });

    key_nav::EndFrame(builder.imgui, context.state.keyboard_navigation);
}
