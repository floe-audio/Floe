// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_common_browser.hpp"

#include "os/filesystem.hpp"

#include "common_infrastructure/tags.hpp"

#include "gui/gui2_actions.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_tips.hpp"
#include "preset_server/preset_server.hpp"

bool RootNodeLessThan(FolderNode const* const& a,
                      DummyValueType const&,
                      FolderNode const* const& b,
                      DummyValueType const&) {
    return a->name < b->name;
}

static prefs::Descriptor ShowPrimaryFilterSectionHeaderDescriptor() {
    return {
        .key = "browser-show-primary-filter-section-header"_s,
        .value_requirements = prefs::ValueType::Bool,
        .default_value = false,
    };
}

bool MatchesFilterSearch(String filter_text, String search_text) {
    if (search_text.size == 0) return true; // Empty search shows all filters
    if (filter_text.size == 0) return false; // Empty filter text doesn't match
    return ContainsCaseInsensitiveAscii(filter_text, search_text);
}

constexpr auto k_right_click_menu_popup_id = (imgui::Id)SourceLocationHash();

void DoRightClickMenuForBox(GuiBoxSystem& box_system,
                            CommonBrowserState& state,
                            Box const& box,
                            u64 item_hash,
                            RightClickMenuState::Function const& do_menu) {
    if (AdditionalClickBehaviour(box_system,
                                 box,
                                 {.right_mouse = true, .triggers_on_mouse_up = true},
                                 &state.right_click_menu_state.absolute_creator_rect)) {
        state.right_click_menu_state.do_menu = do_menu;
        state.right_click_menu_state.item_hash = item_hash;
        box_system.imgui.OpenPopup(k_right_click_menu_popup_id, box.imgui_id);
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

static void BeginFrame(imgui::Context& imgui, BrowserKeyboardNavigation& nav, imgui::Id panel_id) {
    nav.focused_items = nav.temp_focused_items;
    nav.temp_focused_items = {};
    nav.panel_just_focused = false;
    nav.panel_state.select_next_tab_item = false;
    nav.panel_state.select_next_at = 0;
    nav.panel_state.previous_tab_item = {};
    nav.panel_state.item_history.SetBarrier();
    nav.input = {};

    if (imgui.IsKeyboardFocus(panel_id)) {
        imgui.frame_output.wants_keyboard_keys.SetBits(k_navigation_keys);

        auto const key_events = [&](KeyCode key) {
            return imgui.frame_input.Key(key).presses_or_repeats.size;
        };

        for (auto const& e : imgui.frame_input.Key(KeyCode::DownArrow).presses_or_repeats)
            if (e.modifiers.IsOnly(ModifierKey::Modifier))
                nav.input.next_section_presses++;
            else if (e.modifiers.IsNone())
                nav.input.down_presses++;

        for (auto const& e : imgui.frame_input.Key(KeyCode::UpArrow).presses_or_repeats)
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

static void EndFrame(imgui::Context& imgui, BrowserKeyboardNavigation& nav, imgui::Id panel_id) {
    if (imgui.IsKeyboardFocus(panel_id)) {
        auto const key_events = [&](KeyCode key) {
            return imgui.frame_input.Key(key).presses_or_repeats.size;
        };

        if (key_events(KeyCode::End)) {
            nav.panel_state.id_to_select = nav.panel_state.item_history.AtPrevious(1);
            g_show_focus_rectangles = true;
        }

        // 'select_next_at' is a non-wrap-around action, so if there's still pending, we select the last item
        // rather than let it continue counting down on the next frame (from the top of the item list).
        if (nav.panel_state.select_next_at)
            nav.panel_state.id_to_select = nav.panel_state.item_history.AtPrevious(1);

        if (nav.temp_focused_items != nav.focused_items || nav.panel_state.id_to_select)
            imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }
}

struct ItemArgs {
    Box const& box; // Box for button firing.
    Box const* box_for_scrolling; // Use a different box for scrolling into view.
    Optional<Rect> rect_for_drawing; // Use a different rectangle for drawing.
    BrowserKeyboardNavigation::Panel panel;
    imgui::Id const& panel_id;
    u64 id;
    bool is_selected;
    bool is_tab_item;
};

static void DrawFocusBox(GuiBoxSystem& box_system, Rect relative_rect) {
    box_system.imgui.graphics->AddRect(box_system.imgui.GetRegisteredAndConvertedRect(relative_rect),
                                       style::Col(style::Colour::Blue),
                                       box_system.imgui.VwToPixels(style::k_button_rounding),
                                       ~0,
                                       2);
}

static bool DoItem(GuiBoxSystem& box_system, BrowserKeyboardNavigation& nav, ItemArgs const& args) {
    if (!box_system.InputAndRenderPass()) return {};

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
            if (box_system.imgui.frame_input.Key(KeyCode::Enter).presses_or_repeats.size % 2 == 1) {
                button_fired_from_keyboard = true;
                nav.temp_focused_items[panel_index] = args.id;
                g_show_focus_rectangles = true;
            }

            if (g_show_focus_rectangles && box_system.imgui.IsKeyboardFocus(args.panel_id)) {
                auto r = args.rect_for_drawing ? *args.rect_for_drawing : *BoxRect(box_system, args.box);
                DrawFocusBox(box_system, r);
            }
        }

        panel.item_history.Push(args.id);
        if (args.is_tab_item) panel.previous_tab_item = args.id;

        if (button_fired_from_keyboard || (is_focused && nav.panel_just_focused) || focus_this) {
            box_system.imgui.ScrollWindowToShowRectangle(
                BoxRect(box_system, args.box_for_scrolling ? *args.box_for_scrolling : args.box).Value());
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
DoBrowserItem(GuiBoxSystem& box_system, CommonBrowserState& state, BrowserItemOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);

    auto const container = DoBox(box_system,
                                 {
                                     .parent = options.parent,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_direction = layout::Direction::Row,
                                     },
                                 });

    auto item = DoBox(
        box_system,
        {
            .parent = container,
            .background_fill_colours = {options.is_current ? style::Colour::Highlight : style::Colour::None},
            .background_fill_auto_hot_active_overlay = true,
            .round_background_corners = 0b1111,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_direction = layout::Direction::Row,
            },
            .tooltip = options.tooltip,
            .tooltip_avoid_window_id = state.browser_id,
            .tooltip_show_left_or_right = true,
            .behaviour = Behaviour::Button,
            .ignore_double_click = true,
        });

    if (options.icons.size) {
        auto const icon_container = DoBox(box_system,
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
        for (auto const icon : options.icons) {
            switch (icon.tag) {
                case ItemIconType::None: break;
                case ItemIconType::Image: {
                    auto const tex = icon.Get<graphics::ImageID>();
                    DoBox(box_system,
                          {
                              .parent = icon_container,
                              .background_tex = &tex,
                              .layout {
                                  .size = style::k_library_icon_standard_size,
                              },
                          });
                    break;
                }
                case ItemIconType::Font: {
                    DoBox(box_system,
                          {
                              .parent = icon_container,
                              .text = icon.Get<String>(),
                              .size_from_text = true,
                              .font = FontType::Icons,
                          });
                    break;
                }
            }
        }
    }

    DoBox(box_system,
          {
              .parent = item,
              .text = options.text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Body,
          });

    if (AdditionalClickBehaviour(box_system,
                                 item,
                                 {
                                     .left_mouse = true,
                                     .double_click = true,
                                     .triggers_on_mouse_down = true,
                                 })) {
        state.open = false;
    }

    if (item.is_hot) {
        ShowTipIfNeeded(options.notifications,
                        options.store,
                        "You can double-click on items on browsers to load the item and close the panel."_s);
    }

    auto const favourite_toggled =
        !!DoBox(box_system,
                {
                    .parent = container,
                    .text = ICON_FA_STAR,
                    .font = FontType::Icons,
                    .font_size = style::k_font_icons_size * 0.7f,
                    .text_colours =
                        {
                            .base = options.is_favourite ? style::Colour::Highlight400
                                    : item.is_hot        ? style::Colour::Surface2
                                                         : style::Colour::None,
                            .hot = style::Colour::Highlight200,
                            .active = style::Colour::Highlight200,
                        },
                    .text_align_y = TextAlignY::Centre,
                    .layout {
                        .size = {24, layout::k_fill_parent},
                    },
                    .behaviour = Behaviour::Button,
                })
              .button_fired;

    auto const fired_via_keyboard = key_nav::DoItem(box_system,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = item,
                                                        .panel = BrowserKeyboardNavigation::Panel::Items,
                                                        .panel_id = state.browser_id,
                                                        .id = options.item_id,
                                                        .is_selected = options.is_current,
                                                        .is_tab_item = options.is_tab_item,
                                                    });

    return {item, favourite_toggled, item.button_fired || fired_via_keyboard};
}

Box DoBrowserItemsRoot(GuiBoxSystem& box_system) {
    return DoBox(box_system,
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

static void DoFolderFilterAndChildren(GuiBoxSystem& box_system,
                                      CommonBrowserState& state,
                                      Box const& parent,
                                      u8& indent,
                                      FolderNode const* folder,
                                      bool no_lhs_border,
                                      FolderFilterItemInfoLookupTable const& folder_infos,
                                      RightClickMenuState::Function const& do_right_click_menu = nullptr) {
    // We want to stop if we find a preset bank within the preset bank.
    if (folder->user_data.As<PresetFolderListing const>())
        if (auto const bank = PresetBankAtNode(*folder);
            bank && folder->parent && bank != PresetBankAtNode(*folder->parent))
            return;

    bool is_active = false;
    if (!no_lhs_border) {
        for (auto f = folder; f; f = f->parent) {
            // We want to stop if the parent is part of a different preset bank.
            if (f->user_data.As<PresetFolderListing const>())
                if (auto const bank = PresetBankAtNode(*f);
                    bank && f->parent && bank != PresetBankAtNode(*f->parent))
                    break;

            if (state.selected_folder_hashes.Contains(f->Hash())) {
                is_active = true;
                break;
            }
        }
    }
    auto const is_selected = state.selected_folder_hashes.Contains(folder->Hash());

    auto this_info = folder_infos.Find(folder);
    ASSERT(this_info);

    if (this_info->total_available == 0) return;

    auto const button = DoFilterTreeButton(
        box_system,
        state,
        *this_info,
        FilterTreeButtonOptions {
            .common =
                {
                    .parent = parent,
                    .is_selected = is_selected,
                    .text = folder->display_name.size ? folder->display_name : folder->name,
                    .tooltip = folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
                    .hashes = state.selected_folder_hashes,
                    .clicked_hash = folder->Hash(),
                    .filter_mode = state.filter_mode,
                },
            .is_active = is_active,
            .indent = indent,
        });

    if (do_right_click_menu)
        DoRightClickMenuForBox(box_system, state, button, folder->Hash(), do_right_click_menu);

    ++indent;
    for (auto* child = folder->first_child; child; child = child->next) {
        DoFolderFilterAndChildren(box_system,
                                  state,
                                  parent,
                                  indent,
                                  child,
                                  no_lhs_border,
                                  folder_infos,
                                  do_right_click_menu);
    }
    --indent;
}

static void HandleFilterButtonClick(GuiBoxSystem& box_system,
                                    CommonBrowserState& state,
                                    FilterButtonCommonOptions const& options,
                                    bool single_exclusive_mode_for_and = false) {
    state.keyboard_navigation.focused_panel = BrowserKeyboardNavigation::Panel::Filters;
    dyn::Append(box_system.state->deferred_actions,
                [&hashes = options.hashes,
                 &state = state,
                 clicked_hash = options.clicked_hash,
                 display_name = box_system.arena.Clone(options.text),
                 is_selected = options.is_selected,
                 filter_mode = options.filter_mode,
                 single_exclusive_mode_for_and]() {
                    switch (filter_mode) {
                        case FilterMode::Single: {
                            state.ClearAll();
                            if (!is_selected) hashes.Add(clicked_hash, display_name);
                            break;
                        }
                        case FilterMode::MultipleAnd: {
                            if (single_exclusive_mode_for_and) {
                                // In card mode, we assume that each item can only belong to a single card,
                                // so, AND mode is not useful. Instead, we treat it like Single mode, except
                                // we only clear the current hashes, not all state.
                                hashes.Clear();
                                if (!is_selected) hashes.Add(clicked_hash, display_name);
                            } else {
                                if (is_selected)
                                    hashes.Remove(clicked_hash);
                                else
                                    hashes.Add(clicked_hash, display_name);
                            }
                            break;
                        }
                        case FilterMode::MultipleOr: {
                            if (is_selected)
                                hashes.Remove(clicked_hash);
                            else
                                hashes.Add(clicked_hash, display_name);
                            break;
                        }
                        case FilterMode::Count: PanicIfReached();
                    }
                });
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

static NumUsedForFilterString NumUsedForFilterString(GuiBoxSystem& box_system, u32 total_available) {
    // We size to the largest possible number so that the layout doesn't jump around as the num_used changes.
    auto const total_text = fmt::FormatInline<16>("({})"_s, total_available);
    auto const number_size = Max(box_system.fonts[ToInt(FontType::Body)]
                                         ->CalcTextSizeA(style::k_font_body_size, FLT_MAX, 0.0f, total_text) -
                                     f32x2 {4, 0},
                                 f32x2 {0, 0});
    return {total_text, number_size};
}

Box DoFilterButton(GuiBoxSystem& box_system,
                   CommonBrowserState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    f32 const lr_spacing = 4;

    auto const button =
        DoBox(box_system,
              {
                  .parent = options.common.parent,
                  .background_fill_colours = options.common.is_selected
                                                 ? Splat(style::Colour::Highlight)
                                                 : Colours {
                                                    .base = style::Colour::Background2,
                                                    .hot = style::Colour::Surface1,
                                                    .active = style::Colour::Surface1,
                                                 },
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .round_background_fully = true,
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
                        .tooltip_avoid_window_id = state.browser_id,
                        .tooltip_show_left_or_right = true,
                  .behaviour = Behaviour::Button,
              });

    bool grey_out = false;
    if (options.common.filter_mode == FilterMode::MultipleAnd) grey_out = num_used == 0;

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = button,
                  .background_tex = options.icon,
                  .layout {
                      .size = style::k_library_icon_standard_size,
                      .margins = {.r = 3},
                  },
              });
    }

    DoBox(box_system,
          {
              .parent = button,
              .text = options.common.text,
              .size_from_text = true,
              .font = FontType::Body,
              .text_colours {
                  .base = grey_out ? style::Colour::Surface1 : style::Colour::Text,
                  .hot = style::Colour::Text,
                  .active = style::Colour::Text,
              },
              .text_overflow = TextOverflowType::AllowOverflow,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .size = f32x2 {999},
                      .margins = {.l = options.icon ? 0 : k_browser_spacing / 2},
                  },
          });

    auto const total_text = NumUsedForFilterString(box_system, info.total_available);

    DoBox(box_system,
          {
              .parent = button,
              .text = total_text.str,
              .size_from_text = false,
              .font = FontType::Heading3,
              .text_colours {
                  .base = grey_out ? style::Colour::Surface1 : style::Colour::Text,
                  .hot = style::Colour::Text,
                  .active = style::Colour::Text,
              },
              .text_align_y = TextAlignY::Centre,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
              .layout {
                  .size = total_text.size,
                  .margins = {.l = 3},
              },
          });

    if (button.button_fired) HandleFilterButtonClick(box_system, state, options.common);

    if (options.right_click_menu)
        DoRightClickMenuForBox(box_system,
                               state,
                               button,
                               options.common.clicked_hash,
                               options.right_click_menu);

    return button;
}

namespace filter_card_box {
constexpr f32 k_outer_pad = 6.0f;
constexpr f32 k_selection_left_border_width = 6;
constexpr f32 k_tree_indent = 10;
} // namespace filter_card_box

Box DoFilterTreeButton(GuiBoxSystem& box_system,
                       CommonBrowserState& state,
                       FilterItemInfo const& info,
                       FilterTreeButtonOptions const& options) {
    using namespace filter_card_box;
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const button_outer = DoBox(box_system,
                                    {
                                        .parent = options.common.parent,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        },
                                    });

    if (options.is_active) {
        DoBox(box_system,
              {
                  .parent = button_outer,
                  .background_fill_colours = {style::Colour::Highlight},
                  .layout {
                      .size = {k_selection_left_border_width, layout::k_fill_parent},
                  },
              });
    }

    auto const button = DoBox(
        box_system,
        {
            .parent = button_outer,
            .background_fill_colours {
                .base = (options.common.is_selected ? style::Colour::Highlight300 : style::Colour::None) |
                        style::Colour::Alpha15,
                .hot = (options.common.is_selected ? style::Colour::Highlight200
                                                   : style::Colour::Overlay0 | style::Colour::DarkMode) |
                       style::Colour::Alpha15,
                .active = (options.common.is_selected ? style::Colour::Highlight200
                                                      : style::Colour::Overlay0 | style::Colour::DarkMode) |
                          style::Colour::Alpha15,
            },
            .background_fill_auto_hot_active_overlay = false,
            .round_background_corners = 0b1111,
            .round_background_fully = false,
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
            .tooltip_avoid_window_id = state.browser_id,
            .tooltip_show_left_or_right = true,
            .behaviour = Behaviour::Button,
        });

    auto const text_cols = num_used != 0 ? Splat(style::Colour::Text | style::Colour::DarkMode)
                                         : Splat(style::Colour::Overlay2 | style::Colour::DarkMode);

    DoBox(box_system,
          {
              .parent = button,
              .text = options.common.text,
              .size_from_text = false,
              .font = FontType::Body,
              .text_colours = text_cols,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = f32x2 {layout::k_fill_parent, style::k_font_body_size},
              },
          });

    DoBox(box_system,
          {
              .parent = button,
              .text = fmt::FormatInline<16>("({})"_s, info.total_available),
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = text_cols,
              .text_align_y = TextAlignY::Centre,
              .parent_dictates_hot_and_active = true,
              .round_background_corners = 0b1111,
          });

    auto const fired_via_keyboard = key_nav::DoItem(box_system,
                                                    state.keyboard_navigation,
                                                    {
                                                        .box = button,
                                                        .rect_for_drawing = BoxRect(box_system, button_outer),
                                                        .panel = BrowserKeyboardNavigation::Panel::Filters,
                                                        .panel_id = state.browser_id,
                                                        .id = options.common.clicked_hash,
                                                        .is_selected = options.common.is_selected,
                                                        .is_tab_item = false,
                                                    });

    if (button.button_fired || fired_via_keyboard) HandleFilterButtonClick(box_system, state, options.common);

    return button;
}

Box DoFilterCard(GuiBoxSystem& box_system,
                 CommonBrowserState& state,
                 FilterItemInfo const& info,
                 FilterCardOptions const& options) {
    using namespace filter_card_box;
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);
    bool const is_selected = options.common.is_selected;

    auto const num_used = NumUsedForFilter(info, options.common.filter_mode);

    auto const card_outer = DoBox(box_system,
                                  {
                                      .parent = options.common.parent,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .margins = {.b = k_browser_spacing},
                                          .contents_direction = layout::Direction::Row,
                                      },
                                  });

    Optional<graphics::ImageID> background_image1 {};
    Optional<graphics::ImageID> background_image2 {};
    Optional<graphics::ImageID> icon {};
    bool has_icon = false;
    if (options.library_id) {
        auto imgs = GetLibraryImages(options.library_images,
                                     box_system.imgui,
                                     *options.library_id,
                                     options.sample_library_server,
                                     LibraryImagesTypes::All);
        has_icon = imgs.icon.HasValue() && *imgs.icon != graphics::k_invalid_image_id;
        if (box_system.InputAndRenderPass()) {
            if (box_system.imgui.IsRectVisible(
                    box_system.imgui.WindowRectToScreenRect(*BoxRect(box_system, card_outer)))) {
                background_image1 = imgs.blurred_background;
                background_image2 = imgs.background;
                icon = imgs.icon;
            }
        }
    }

    auto const base_background =
        DoBox(box_system,
              {
                  .parent = card_outer,
                  .background_fill_colours = {style::Colour::Background2 | style::Colour::DarkMode},
                  .background_tex = background_image1.NullableValue(),
                  .background_tex_alpha = 180,
                  .background_tex_fill_mode = BackgroundTexFillMode::Cover,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_direction = layout::Direction::Row,
                  },
              });

    auto const card = DoBox(box_system,
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

    if (is_selected) {
        DoBox(box_system,
              {
                  .parent = card,
                  .background_fill_colours = {style::Colour::Highlight},
                  .round_background_corners = 0b1001,
                  .layout {
                      .size = {k_selection_left_border_width, layout::k_fill_parent},
                  },
              });
    }

    auto const card_content = DoBox(box_system,
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

    auto const card_top = DoBox(
        box_system,
        {
            .parent = card_content,
            .background_fill_colours {
                .base = (options.common.is_selected ? style::Colour::Highlight300 : style::Colour::None) |
                        style::Colour::Alpha15,
                .hot = (options.common.is_selected ? style::Colour::Highlight200
                                                   : style::Colour::Overlay2 | style::Colour::DarkMode) |
                       style::Colour::Alpha15,
                .active = (options.common.is_selected ? style::Colour::Highlight200
                                                      : style::Colour::Overlay2 | style::Colour::DarkMode) |
                          style::Colour::Alpha15,
            },
            .round_background_corners = !is_selected ? 0b1111u : 0b0110,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_padding = {.lrtb = k_outer_pad},
                .contents_gap = k_outer_pad,
                .contents_direction = layout::Direction::Row,
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .tooltip = options.common.tooltip,
            .tooltip_avoid_window_id = state.browser_id,
            .tooltip_show_left_or_right = true,
            .behaviour = Behaviour::Button,
        });

    if (options.right_click_menu)
        DoRightClickMenuForBox(box_system,
                               state,
                               card_top,
                               options.common.clicked_hash,
                               options.right_click_menu);

    if (has_icon) {
        DoBox(box_system,
              {
                  .parent = card_top,
                  .background_tex = icon.NullableValue(),
                  .layout {
                      .size = 28,
                  },
              });
    }

    auto const rhs = DoBox(box_system,
                           {
                               .parent = card_top,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Column,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                               },
                           });

    auto const title_box = DoBox(box_system,
                                 {
                                     .parent = rhs,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = 8,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });

    auto const title_text_colours = num_used != 0 ? Splat(style::Colour::Text | style::Colour::DarkMode)
                                                  : Splat(style::Colour::Overlay2 | style::Colour::DarkMode);
    auto const subtitle_text_colours = num_used != 0
                                           ? Splat(style::Colour::Subtext1 | style::Colour::DarkMode)
                                           : Splat(style::Colour::Overlay2 | style::Colour::DarkMode);

    DoBox(box_system,
          {
              .parent = title_box,
              .text = options.common.text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Heading2,
              .text_colours = title_text_colours,
              .parent_dictates_hot_and_active = true,
          });

    DoBox(box_system,
          {
              .parent = title_box,
              .text = fmt::FormatInline<32>("({})"_s, info.total_available),
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = subtitle_text_colours,
              .parent_dictates_hot_and_active = true,
          });

    DoBox(box_system,
          {
              .parent = rhs,
              .text = options.subtext,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours = subtitle_text_colours,
              .parent_dictates_hot_and_active = true,
              .layout {
                  // When there's no LHS border, add a bit of padding so that the text won't jump to
                  // multi-line when clicked on.
                  .margins = {.r = is_selected ? 0 : k_selection_left_border_width},
              },
          });

    auto const fired_via_keyboard =
        key_nav::DoItem(box_system,
                        state.keyboard_navigation,
                        {
                            .box = card_top,
                            .box_for_scrolling = &card,
                            .rect_for_drawing = BoxRect(box_system, card_top).Transform([&](Rect r) {
                                return r.ExpandLeft(is_selected ? k_selection_left_border_width : 0);
                            }),
                            .panel = BrowserKeyboardNavigation::Panel::Filters,
                            .panel_id = state.browser_id,
                            .id = options.common.clicked_hash,
                            .is_selected = options.common.is_selected,
                            .is_tab_item = true,
                        });

    if (card_top.button_fired || fired_via_keyboard)
        HandleFilterButtonClick(box_system, state, options.common);

    if (options.folder && options.folder->first_child) {
        auto const folder_box = DoBox(
            box_system,
            {
                .parent = card_content,
                .background_fill_colours =
                    {
                        .base = style::Colour::Background0 | style::Colour::DarkMode | style::Colour::Alpha50,
                        .hot = style::Colour::Overlay1 | style::Colour::DarkMode | style::Colour::Alpha50,
                        .active = style::Colour::Overlay1 | style::Colour::DarkMode | style::Colour::Alpha50,
                    },
                .round_background_corners = 0b0011,
                .layout {
                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                    .contents_padding = {.tb = 3},
                    .contents_direction = layout::Direction::Column,
                },
            });

        // Do the folder children, not the root folder.
        for (auto* child = options.folder->first_child; child; child = child->next) {
            u8 indent = 0;
            DoFolderFilterAndChildren(box_system,
                                      state,
                                      folder_box,
                                      indent,
                                      child,
                                      options.common.is_selected,
                                      options.folder_infos,
                                      options.right_click_menu);
        }
    }

    return card_top;
}

BrowserSection::Result BrowserSection::Do(GuiBoxSystem& box_system) {
    if (!init) {
        is_collapsed = Contains(state.collapsed_filter_headers, id);
        init = true;
    } else {
        if (is_collapsed) return State::Collapsed;
    }

    if (is_box_init) return box_cache;

    if (num_sections_rendered) {
        auto& n = *num_sections_rendered;
        if (n) DoModalDivider(box_system, parent, {.horizontal = true, .subtle = true});
        ++n;
    }

    auto const container =
        DoBox(box_system,
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
            DoBox(box_system,
                  {
                      .parent = container,
                      .background_fill_auto_hot_active_overlay = true,
                      .layout {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_gap = k_browser_spacing / 2,
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                      },
                      .tooltip = folder ? TooltipString {"Folder"_s} : k_nullopt,
                      .tooltip_avoid_window_id = state.browser_id,
                      .tooltip_show_left_or_right = true,
                      .behaviour = Behaviour::Button,
                  });

        if (heading_container.button_fired) {
            dyn::Append(box_system.state->deferred_actions, [&state = this->state, id = id]() {
                if (Contains(state.collapsed_filter_headers, id))
                    dyn::RemoveValue(state.collapsed_filter_headers, id);
                else
                    dyn::Append(state.collapsed_filter_headers, id);
            });
        }

        if (right_click_menu)
            DoRightClickMenuForBox(box_system, state, heading_container, id, right_click_menu);

        DoBox(box_system,
              {
                  .parent = heading_container,
                  .text = is_collapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN,
                  .font = FontType::Icons,
                  .font_size = style::k_font_icons_size * 0.6f,
                  .text_colours = {style::Colour::Subtext0},
                  .layout {
                      .size = style::k_font_icons_size * 0.4f,
                  },
              });

        if (icon) {
            DoBox(box_system,
                  {
                      .parent = heading_container,
                      .text = *icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = style::k_font_icons_size * 0.7f,
                  });
        }

        {
            DynamicArray<char> buf {box_system.arena};

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
                DoBox(box_system,
                      {
                          .parent = heading_container,
                          .text = text,
                          .wrap_width = k_wrap_to_parent,
                          .size_from_text = true,
                          .font = FontType::Heading3,
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

    box_cache = DoBox(box_system,
                      {
                          .parent = container,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = k_browser_spacing / 2,
                              .contents_direction = layout::Direction::Row,
                              .contents_multiline = true,
                              .contents_align = layout::Alignment::Start,
                          },
                      });
    return box_cache;
}

static void DoLibraryRightClickMenu(GuiBoxSystem& box_system,
                                    BrowserPopupContext& context,
                                    RightClickMenuState const& menu_state,
                                    LibraryFilters const& library_filters) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    auto const find_library = [&](u64 library_hash) -> Optional<sample_lib::LibraryIdRef> {
        for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries)
            if (lib_hash == library_hash) return lib_id;
        return k_nullopt;
    };

    if (MenuItem(box_system,
                 root,
                 {
                     .text = fmt::Format(box_system.arena, "Open Folder in {}", GetFileBrowserAppName()),
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

    if (MenuItem(box_system,
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
                UninstallSampleLibrary(*lib,
                                       library_filters.confirmation_dialog_state,
                                       library_filters.error_notifications,
                                       library_filters.notifications);
                context.state.open = false;
            }
        }
    }
}

bool ShowPrimaryFilterSectionHeader(CommonBrowserState const& state,
                                    prefs::Preferences const& preferences,
                                    u64 section_heading_id) {
    bool v = true;
    if (!prefs::GetBool(preferences, ShowPrimaryFilterSectionHeaderDescriptor())) v = false;

    // If it's currently collapsed, show the heading otherwise it's not intuitive to why there's no
    // items.
    if (Contains(state.collapsed_filter_headers, section_heading_id)) v = true;
    return v;
}

static void DoBrowserLibraryFilters(GuiBoxSystem& box_system,
                                    BrowserPopupContext& context,
                                    Box const& parent,
                                    LibraryFilters const& library_filters,
                                    u8& sections) {
    if (library_filters.libraries.size) {
        BrowserSection section = {
            .state = context.state,
            .num_sections_rendered = &sections,
            .id = context.browser_id ^ HashFnv1a("libraries-section"),
            .parent = parent,
            .heading = !library_filters.card_view ||
                               ShowPrimaryFilterSectionHeader(context.state, context.preferences, section.id)
                           ? Optional<String> {"LIBRARIES"_s}
                           : k_nullopt,
            .multiline_contents = !library_filters.card_view,
        };

        for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
            ASSERT(lib_id.size);

            auto const lib_ptr = library_filters.libraries_table.Find(lib_id, lib_hash);
            if (!lib_ptr) continue;
            auto const& lib = *lib_ptr;

            if (!MatchesFilterSearch(lib->name, context.state.filter_search)) continue;

            Box button;
            if (library_filters.card_view) {
                auto const folder = &lib->root_folders[ToInt(library_filters.resource_type)];

                auto const is_selected = context.state.selected_library_hashes.Contains(lib_hash);

                if (section.Do(box_system) == BrowserSection::State::Collapsed) break;

                button = DoFilterCard(box_system,
                                      context.state,
                                      lib_info,
                                      FilterCardOptions {
                                          .common =
                                              {
                                                  .parent = section.Do(box_system).Get<Box>(),
                                                  .is_selected = is_selected,
                                                  .text = lib->name,
                                                  .tooltip = FunctionRef<String()>([&]() -> String {
                                                      auto lib = sample_lib_server::FindLibraryRetained(
                                                          context.sample_library_server,
                                                          lib_id);
                                                      DEFER { lib.Release(); };

                                                      DynamicArray<char> buf {box_system.arena};
                                                      fmt::Append(buf, "{} by {}.", lib->name, lib->author);
                                                      if (lib) {
                                                          if (lib->description)
                                                              fmt::Append(buf, "\n\n{}", lib->description);
                                                      }
                                                      return buf.ToOwnedSpan();
                                                  }),
                                                  .hashes = context.state.selected_library_hashes,
                                                  .clicked_hash = lib_hash,
                                                  .filter_mode = context.state.filter_mode,
                                              },
                                          .library_id = lib_id,
                                          .library_images = library_filters.library_images,
                                          .sample_library_server = context.sample_library_server,
                                          .subtext = ({
                                              String s;
                                              if (lib) s = box_system.arena.Clone(lib->tagline);
                                              s;
                                          }),
                                          .folder_infos = library_filters.folders,
                                          .folder = folder,
                                      });
            } else {
                if (section.Do(box_system) == BrowserSection::State::Collapsed) break;

                auto const imgs = GetLibraryImages(library_filters.library_images,
                                                   box_system.imgui,
                                                   lib_id,
                                                   context.sample_library_server,
                                                   LibraryImagesTypes::Icon);

                button = DoFilterButton(
                    box_system,
                    context.state,
                    lib_info,
                    FilterButtonOptions {
                        .common =
                            {
                                .parent = section.Do(box_system).Get<Box>(),
                                .is_selected = context.state.selected_library_hashes.Contains(lib_hash),
                                .text = lib->name,
                                .tooltip = FunctionRef<String()>([&]() -> String {
                                    auto lib =
                                        sample_lib_server::FindLibraryRetained(context.sample_library_server,
                                                                               lib_id);
                                    DEFER { lib.Release(); };

                                    DynamicArray<char> buf {box_system.arena};
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
                                .hashes = context.state.selected_library_hashes,
                                .clicked_hash = lib_hash,
                                .filter_mode = context.state.filter_mode,
                            },
                        .icon = ({
                            graphics::ImageID const* tex = nullptr;
                            if (imgs.icon) tex = imgs.icon.NullableValue();
                            tex;
                        }),
                    });
            }

            if (lib_hash != Hash(sample_lib::k_builtin_library_id))
                DoRightClickMenuForBox(
                    box_system,
                    context.state,
                    button,
                    lib_hash,
                    [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                        DoLibraryRightClickMenu(box_system, context, menu_state, library_filters);
                    });
        }

        if (library_filters.additional_pseudo_card) {
            auto options = *library_filters.additional_pseudo_card;
            if (MatchesFilterSearch(options.common.text, context.state.filter_search) &&
                section.Do(box_system) != BrowserSection::State::Collapsed) {
                options.common.parent = section.Do(box_system).Get<Box>();

                DoFilterCard(box_system,
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

static void DoBrowserLibraryAuthorFilters(GuiBoxSystem& box_system,
                                          BrowserPopupContext& context,
                                          Box const& parent,
                                          LibraryFilters const& library_filters,
                                          u8& sections) {
    if (library_filters.library_authors.size) {
        BrowserSection section = {
            .state = context.state,
            .num_sections_rendered = &sections,
            .id = context.browser_id ^ HashFnv1a("library-authors-section"),
            .parent = parent,
            .heading = "LIBRARY AUTHORS"_s,
            .multiline_contents = true,
        };

        for (auto const [author, author_info, author_hash] : library_filters.library_authors) {
            if (!MatchesFilterSearch(author, context.state.filter_search)) continue;
            if (section.Do(box_system) == BrowserSection::State::Collapsed) break;
            auto const is_selected = context.state.selected_library_author_hashes.Contains(author_hash);
            DoFilterButton(box_system,
                           context.state,
                           author_info,
                           {
                               .common =
                                   {
                                       .parent = section.Do(box_system).Get<Box>(),
                                       .is_selected = is_selected,
                                       .text = author,
                                       .hashes = context.state.selected_library_author_hashes,
                                       .clicked_hash = author_hash,
                                       .filter_mode = context.state.filter_mode,
                                   },
                           });
        }
    }
}

void DoBrowserTagsFilters(GuiBoxSystem& box_system,
                          BrowserPopupContext& context,
                          Box const& parent,
                          TagsFilters const& tags_filters,
                          u8& sections) {
    if (!tags_filters.tags.size) return;

    OrderedHashTable<TagCategory, OrderedHashTable<TagType, FilterItemInfo>> standard_tags {};
    OrderedHashTable<String, FilterItemInfo> non_standard_tags {};

    for (auto const [name, info, _] : tags_filters.tags) {
        if (auto const t = LookupTagName(name)) {
            auto& tags_for_category =
                standard_tags.FindOrInsertGrowIfNeeded(box_system.arena, t->category, {}).element.data;
            tags_for_category.InsertGrowIfNeeded(box_system.arena, t->tag, info);
        } else {
            non_standard_tags.InsertGrowIfNeeded(box_system.arena, name, info);
        }
    }

    BrowserSection tags_section {
        .state = context.state,
        .num_sections_rendered = &sections,
        .id = context.browser_id ^ HashFnv1a("tags-section"),
        .parent = parent,
        .heading = "TAGS",
        .multiline_contents = false,
        .bigger_contents_gap = true,
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
        };

        for (auto const [tag, filter_item_info, _] : tags_for_category) {
            auto const tag_info = GetTagInfo(tag);
            if (!MatchesFilterSearch(tag_info.name, context.state.filter_search)) continue;

            if (tags_section.Do(box_system) == BrowserSection::State::Collapsed) break;
            // We now have the outer section. We can give it to the inner section.
            inner_section.parent = tags_section.Do(box_system).Get<Box>();
            if (inner_section.Do(box_system) == BrowserSection::State::Collapsed) break;

            auto const tag_hash = Hash(tag_info.name);
            auto const is_selected = context.state.selected_tags_hashes.Contains(tag_hash);
            DoFilterButton(box_system,
                           context.state,
                           filter_item_info,
                           {
                               .common =
                                   {
                                       .parent = inner_section.Do(box_system).Get<Box>(),
                                       .is_selected = is_selected,
                                       .text = tag_info.name,
                                       .hashes = context.state.selected_tags_hashes,
                                       .clicked_hash = tag_hash,
                                       .filter_mode = context.state.filter_mode,
                                   },
                           });
        }
    }

    if (non_standard_tags.size) {
        BrowserSection inner_section {
            .state = context.state,
            .id = context.browser_id ^ HashFnv1a("tags-section-uncategorised"),
            .parent = {}, // IMPORTANT: set later
            .heading = "UNCATEGORISED",
            .multiline_contents = true,
            .subsection = true,
        };

        for (auto const [name, filter_item_info, _] : non_standard_tags) {
            if (!MatchesFilterSearch(name, context.state.filter_search)) continue;

            if (tags_section.Do(box_system) == BrowserSection::State::Collapsed) break;
            // We now have the outer section. We can give it to the inner section.
            inner_section.parent = tags_section.Do(box_system).Get<Box>();
            if (inner_section.Do(box_system) == BrowserSection::State::Collapsed) break;

            auto const is_selected = context.state.selected_tags_hashes.Contains(Hash(name));
            DoFilterButton(box_system,
                           context.state,
                           filter_item_info,
                           {
                               .common =
                                   {
                                       .parent = inner_section.Do(box_system).Get<Box>(),
                                       .is_selected = is_selected,
                                       .text = name,
                                       .hashes = context.state.selected_tags_hashes,
                                       .clicked_hash = Hash(name),
                                       .filter_mode = context.state.filter_mode,
                                   },
                           });
        }
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

static void DoMoreOptionsMenu(GuiBoxSystem& box_system, BrowserPopupContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const filter_mode : EnumIterator<FilterMode>()) {
        if (MenuItem(box_system,
                     root,
                     {
                         .text = FilterModeText(filter_mode),
                         .subtext = FilterModeDescription(filter_mode),
                         .is_selected = context.state.filter_mode == filter_mode,
                     })
                .button_fired) {
            dyn::Append(
                box_system.state->deferred_actions,
                [&mode = context.state.filter_mode, new_mode = filter_mode, &state = context.state]() {
                    if (mode != FilterMode::Single && new_mode == FilterMode::Single) state.ClearToOne();
                    mode = new_mode;
                });
        }
    }

    DoModalDivider(box_system, root, {.margin = 4, .horizontal = true});

    {
        bool const state = prefs::GetBool(context.preferences, ShowPrimaryFilterSectionHeaderDescriptor());
        if (MenuItem(box_system,
                     root,
                     {
                         .text = "Show Primary Filter Section Header",
                         .is_selected = state,
                     })
                .button_fired) {
            dyn::Append(box_system.state->deferred_actions, [&prefs = context.preferences, state]() {
                prefs::SetValue(prefs, ShowPrimaryFilterSectionHeaderDescriptor(), !state);
            });
        }
    }
}

static void DoBrowserPopupInternal(GuiBoxSystem& box_system,
                                   BrowserPopupContext& context,
                                   BrowserPopupOptions const& options) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = {layout::k_hug_contents, options.height},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const title_container =
            DoBox(box_system,
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
        DoBox(box_system,
              {
                  .parent = title_container,
                  .text = options.title,
                  .size_from_text = true,
                  .size_from_text_preserve_height = true,
                  .font = FontType::Heading2,
                  .layout {
                      .size = style::k_font_heading2_size,
                  },
              });

        {

            auto const rhs_top = DoBox(box_system,
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
                auto const btn_container = DoBox(box_system,
                                                 {
                                                     .parent = rhs_top,
                                                     .layout {
                                                         .size = layout::k_hug_contents,
                                                         .margins = {.r = k_browser_spacing * 2},
                                                     },
                                                 });

                // Custom button with icon, styled like TextButton
                auto const button =
                    DoBox(box_system,
                          {
                              .parent = btn_container,
                              .background_fill_colours = Splat(style::Colour::Background2),
                              .background_fill_auto_hot_active_overlay = !btn->disabled,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_hug_contents, layout::k_hug_contents},
                                  .contents_padding = {.lr = style::k_button_padding_x,
                                                       .tb = style::k_button_padding_y},
                                  .contents_gap = 3,
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                              .tooltip = btn->disabled ? TooltipString {k_nullopt} : btn->tooltip,
                              .behaviour = btn->disabled ? Behaviour::None : Behaviour::Button,
                          });

                // Button text
                DoBox(box_system,
                      {
                          .parent = button,
                          .text = btn->text,
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = {btn->disabled ? style::Colour::Surface1 : style::Colour::Text},
                          .text_align_y = TextAlignY::Centre,
                          .text_overflow = TextOverflowType::AllowOverflow,
                      });

                // X icon
                DoBox(box_system,
                      {
                          .parent = button,
                          .text = ICON_FA_XMARK,
                          .size_from_text = true,
                          .font = FontType::Icons,
                          .font_size = style::k_font_body_size,
                          .text_colours = {btn->disabled ? style::Colour::Surface1 : style::Colour::Subtext0},
                      });

                if (button.button_fired && !btn->disabled)
                    dyn::Append(box_system.state->deferred_actions, [&fn = btn->on_fired]() { fn(); });
            }

            for (auto const& btn : ArrayT<BrowserPopupOptions::Button>({
                     {
                         .text = ICON_FA_CARET_LEFT,
                         .tooltip = fmt::Format(box_system.arena, "Load previous {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_previous,
                     },
                     {
                         .text = ICON_FA_CARET_RIGHT,
                         .tooltip = fmt::Format(box_system.arena, "Load next {}", options.item_type_name),
                         .icon_scaling = 1.0f,
                         .on_fired = options.on_load_next,
                     },
                     {
                         .text = ICON_FA_SHUFFLE,
                         .tooltip = fmt::Format(box_system.arena, "Load random {}", options.item_type_name),
                         .icon_scaling = 0.8f,
                         .on_fired = options.on_load_random,
                     },
                     {
                         .text = ICON_FA_LOCATION_ARROW,
                         .tooltip =
                             fmt::Format(box_system.arena, "Scroll to current {}", options.item_type_name),
                         .icon_scaling = 0.8f,
                         .on_fired = options.on_scroll_to_show_selected,
                     },
                 })) {
                if (!btn.on_fired) continue;
                if (IconButton(box_system,
                               rhs_top,
                               btn.text,
                               btn.tooltip,
                               style::k_font_heading2_size * btn.icon_scaling,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions, [fired = btn.on_fired]() { fired(); });
                }
            }
            {
                auto const btn = IconButton(box_system,
                                            rhs_top,
                                            ICON_FA_ELLIPSIS_VERTICAL,
                                            "More options",
                                            style::k_font_heading2_size * 0.9f,
                                            style::k_font_heading2_size);

                auto const popup_id = box_system.imgui.GetID("moreoptions");

                if (btn.button_fired) box_system.imgui.OpenPopup(popup_id, btn.imgui_id);

                AddPanel(box_system,
                         Panel {
                             .run = [&context](
                                        GuiBoxSystem& box_system) { DoMoreOptionsMenu(box_system, context); },
                             .data =
                                 PopupPanel {
                                     .debug_name = "moreoptions",
                                     .creator_layout_id = btn.layout_id,
                                     .popup_imgui_id = popup_id,
                                     .additional_imgui_window_flags =
                                         imgui::WindowFlags_PositionOnTopOfParentPopup,
                                 },
                         });
            }
        }

        if (auto const close = DoBox(box_system,
                                     {
                                         .parent = title_container,
                                         .text = ICON_FA_XMARK,
                                         .size_from_text = true,
                                         .font = FontType::Icons,
                                         .background_fill_auto_hot_active_overlay = true,
                                         .round_background_corners = 0b1111,
                                         .behaviour = Behaviour::Button,
                                         .extra_margin_for_mouse_events = 8,
                                     });
            close.button_fired) {
            context.state.open = false;
        }
    }

    DoModalDivider(box_system, root, {.horizontal = true});

    auto const main_section = DoBox(box_system,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_hug_contents, layout::k_fill_parent},
                                            .contents_direction = layout::Direction::Row,
                                            .contents_align = layout::Alignment::Start,
                                        },
                                    });

    {
        auto const lhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {options.filters_col_width, layout::k_fill_parent},
                                       .contents_padding = {.t = k_browser_spacing},
                                       .contents_gap = k_browser_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        {
            auto const lhs_top = DoBox(box_system,
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
                DoBox(box_system,
                      {
                          .parent = lhs_top,
                          .background_fill_colours = {style::Colour::Background2},
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_padding = {.lr = k_browser_spacing / 2},
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = filter_search_box,
                      .text = ICON_FA_MAGNIFYING_GLASS,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_browser_item_height * 0.8f,
                      .text_colours = {style::Colour::Subtext0},
                  });

            auto const filter_text_input =
                DoBox(box_system,
                      {
                          .parent = filter_search_box,
                          .text = (String)context.state.filter_search,
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = {layout::k_fill_parent, k_browser_item_height},
                          },
                          .tooltip = "Search filters"_s,
                          .behaviour = Behaviour::TextInput,
                          .text_input_select_all_on_focus = true,
                          .text_input_placeholder_text = options.filter_search_placeholder_text,
                      });
            DrawTextInput(box_system,
                          filter_text_input,
                          {
                              .text_col = style::Colour::Text,
                              .cursor_col = style::Colour::Text,
                              .selection_col = style::Colour::Highlight | style::Colour::Alpha50,
                          });
            if (filter_text_input.text_input_result && filter_text_input.text_input_result->buffer_changed) {
                dyn::Append(box_system.state->deferred_actions,
                            [&s = context.state.filter_search,
                             new_text = filter_text_input.text_input_result->text]() {
                                dyn::AssignFitInCapacity(s, new_text);
                            });
                box_system.imgui.frame_output.ElevateUpdateRequest(
                    GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
            }

            if (context.state.filter_search.size) {
                if (DoBox(box_system,
                          {
                              .parent = filter_search_box,
                              .text = ICON_FA_XMARK,
                              .size_from_text = true,
                              .font = FontType::Icons,
                              .font_size = k_browser_item_height * 0.9f,
                              .text_colours = Splat(style::Colour::Subtext0),
                              .background_fill_auto_hot_active_overlay = true,
                              .tooltip = "Clear search"_s,
                              .behaviour = Behaviour::Button,
                          })
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&s = context.state.filter_search]() { dyn::Clear(s); });
                }
            }

            if (context.state.filter_mode != FilterMode::Single) {
                auto const indicator_box =
                    DoBox(box_system,
                          {
                              .parent = lhs_top,
                              .font = FontType::Body,
                              .border_colours = Splat(style::Colour::Overlay0),
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_hug_contents, layout::k_fill_parent},
                                  .contents_padding = {.lr = k_browser_spacing / 2},
                                  .contents_align = layout::Alignment::Middle,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                              .tooltip = FunctionRef<String()> {[&]() -> String {
                                  return fmt::Format(box_system.arena,
                                                     "Multi-select mode on with \"{}\" behaviour"_s,
                                                     FilterModeTextAbbreviated(context.state.filter_mode));
                              }},
                          });

                DoBox(box_system,
                      {
                          .parent = indicator_box,
                          .text = FilterModeTextAbbreviated(context.state.filter_mode),
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = Splat(style::Colour::Subtext0),
                      });
            }
        }

        AddPanel(
            box_system,
            {
                .run =
                    [&](GuiBoxSystem& box_system) {
                        if (!options.library_filters && !options.tags_filters) return;

                        auto const root = DoBrowserItemsRoot(box_system);

                        u8 num_lhs_sections = 0;

                        if (options.do_extra_filters_top)
                            options.do_extra_filters_top(box_system, root, num_lhs_sections);

                        if (options.library_filters)
                            DoBrowserLibraryFilters(box_system,
                                                    context,
                                                    root,
                                                    *options.library_filters,
                                                    num_lhs_sections);

                        if (options.tags_filters)
                            DoBrowserTagsFilters(box_system,
                                                 context,
                                                 root,
                                                 *options.tags_filters,
                                                 num_lhs_sections);

                        if (options.library_filters)
                            DoBrowserLibraryAuthorFilters(box_system,
                                                          context,
                                                          root,
                                                          *options.library_filters,
                                                          num_lhs_sections);

                        if (options.do_extra_filters_bottom)
                            options.do_extra_filters_bottom(box_system, root, num_lhs_sections);
                    },
                .data =
                    Subpanel {
                        .id = DoBox(box_system,
                                    {
                                        .parent = lhs,
                                        .layout {
                                            .size = layout::k_fill_parent,
                                        },
                                    })
                                  .layout_id,
                        .imgui_id = box_system.imgui.GetID("filters"),
                        .flags = imgui::WindowFlags_ScrollbarInsidePadding | imgui::WindowFlags_NoScrollbarX,
                        .padding = {.lr = k_browser_spacing},
                        .line_height_for_scroll_wheel = k_browser_item_height,
                        .debug_name = "filters",
                    },
            });
    }

    DoModalDivider(box_system, main_section, {.vertical = true});

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
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
            auto const rhs_top = DoBox(box_system,
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
                DoBox(box_system,
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
                    DoBox(box_system,
                          {
                              .parent = search_and_fave_box,
                              .background_fill_colours = {style::Colour::Background2},
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_padding = {.lr = k_browser_spacing / 2},
                                  .contents_direction = layout::Direction::Row,
                                  .contents_align = layout::Alignment::Start,
                                  .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                              },
                          });

                DoBox(box_system,
                      {
                          .parent = search_box,
                          .text = ICON_FA_MAGNIFYING_GLASS,
                          .size_from_text = true,
                          .font = FontType::Icons,
                          .font_size = k_browser_item_height * 0.8f,
                          .text_colours = {style::Colour::Subtext0},
                      });

                auto const text_input =
                    DoBox(box_system,
                          {
                              .parent = search_box,
                              .text = (String)context.state.search,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {layout::k_fill_parent, k_browser_item_height},
                              },
                              .tooltip = "Search (" MODIFIER_KEY_NAME "+F to focus)"_s,
                              .behaviour = Behaviour::TextInput,
                              .text_input_select_all_on_focus = true,
                              .text_input_placeholder_text = options.item_search_placeholder_text,
                          });
                DrawTextInput(box_system,
                              text_input,
                              {
                                  .text_col = style::Colour::Text,
                                  .cursor_col = style::Colour::Text,
                                  .selection_col = style::Colour::Highlight,
                              });
                if (text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&s = context.state.search, new_text = text_input.text_input_result->text]() {
                                    dyn::AssignFitInCapacity(s, new_text);
                                });
                    box_system.imgui.frame_output.ElevateUpdateRequest(
                        GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
                }

                if (auto const r = BoxRect(box_system, search_box);
                    r && box_system.imgui.TextInputHasFocus(text_input.imgui_id))
                    key_nav::DrawFocusBox(box_system, *r);

                if (box_system.InputAndRenderPass() &&
                    box_system.imgui.IsKeyboardFocus(text_input.imgui_id)) {
                    if (box_system.imgui.frame_input.Key(KeyCode::DownArrow).presses.size ||
                        box_system.imgui.frame_input.Key(KeyCode::Tab).presses.size) {
                        box_system.imgui.SetTextInputFocus(0, {}, false);
                        key_nav::FocusPanel(context.state.keyboard_navigation,
                                            BrowserKeyboardNavigation::Panel::Items,
                                            true);
                    }
                }

                if (context.state.search.size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_XMARK,
                                  .size_from_text = true,
                                  .font = FontType::Icons,
                                  .font_size = k_browser_item_height * 0.9f,
                                  .text_colours = Splat(style::Colour::Subtext0),
                                  .background_fill_auto_hot_active_overlay = true,
                                  .tooltip = "Clear search"_s,
                                  .behaviour = Behaviour::Button,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&s = context.state.search]() { dyn::Clear(s); });
                    }
                }

                // CTRL+F focuses the search box.
                if (box_system.InputAndRenderPass() && box_system.imgui.IsKeyboardFocus(context.browser_id)) {
                    box_system.imgui.frame_output.wants_keyboard_keys.Set(ToInt(KeyCode::F));
                    for (auto const& e : box_system.imgui.frame_input.Key(KeyCode::F).presses) {
                        if (e.modifiers.IsOnly(ModifierKey::Modifier)) {
                            box_system.imgui.SetTextInputFocus(text_input.imgui_id,
                                                               context.state.search,
                                                               false);
                            box_system.imgui.TextInputSelectAll();
                            break;
                        }
                    }
                }
            }

            {
                SelectedHashes dummy_hashes {};
                if (DoFilterButton(box_system,
                                   context.state,
                                   options.favourites_filter_info,
                                   {
                                       .common =
                                           {
                                               .parent = search_and_fave_box,
                                               .is_selected = context.state.favourites_only,
                                               .text = "Favourites"_s,
                                               .hashes = dummy_hashes,
                                               .clicked_hash = 1,
                                               .filter_mode = context.state.filter_mode,
                                           },
                                       .no_bottom_margin = true,
                                   })
                        .button_fired) {
                    dyn::Append(
                        box_system.state->deferred_actions,
                        [&favourites_only = context.state.favourites_only,
                         new_state = !context.state.favourites_only]() { favourites_only = new_state; });
                }
            }

            // For each selected hash, we want to show it with a dismissable button, like showing active
            // filters in a web ecommerce store.
            if (context.state.HasFilters() || context.state.search.size) {
                // Multiline container
                auto const container =
                    DoBox(box_system,
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

                auto const do_item = [&](String category, String item, FilterMode mode) {
                    // If not first, we should add an 'AND' or 'OR' label depending on the filter mode.
                    if (!first) {
                        DoBox(box_system,
                              BoxConfig {
                                  .parent = container,
                                  .text = FilterModeTextAbbreviated(mode),
                                  .size_from_text = true,
                                  .size_from_text_preserve_height = true,
                                  .font = FontType::Heading3,
                                  .font_size = style::k_font_heading3_size * 0.8f,
                                  .text_colours = {style::Colour::Subtext0},
                                  .text_align_y = TextAlignY::Centre,
                                  .layout {
                                      .size = {1, k_browser_item_height + (k_browser_spacing / 2)},
                                  },
                              });
                    } else {
                        first = false;
                    }

                    // button container for the text, and the 'x' icon.
                    auto const button =
                        DoBox(box_system,
                              {
                                  .parent = container,
                                  .background_fill_colours = {style::Colour::Background2},
                                  .background_fill_auto_hot_active_overlay = true,
                                  .round_background_corners = 0b1111,
                                  .round_background_fully = true,
                                  .layout {
                                      .size = {layout::k_hug_contents, k_browser_item_height},
                                      .margins {.b = k_browser_spacing / 2},
                                      .contents_padding {.lr = style::k_spacing / 2},
                                      .contents_gap = style::k_spacing / 2,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::Middle,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
                                  .tooltip = "Remove filter"_s,
                                  .behaviour = Behaviour::Button,
                              });
                    // Text
                    DoBox(box_system,
                          {
                              .parent = button,
                              .text = item.size
                                          ? (String)fmt::Format(box_system.arena, "{}: {}", category, item)
                                          : category,
                              .size_from_text = true,
                              .font = FontType::Heading3,
                          });
                    DoBox(box_system,
                          {
                              .parent = button,
                              .text = ICON_FA_XMARK,
                              .font = FontType::Icons,
                              .font_size = style::k_font_icons_size * 0.7f,
                              .text_colours = {style::Colour::Subtext0},
                              .layout {
                                  .size = {style::k_font_icons_size * 0.7f, style::k_font_icons_size * 0.7f},
                              },
                          });

                    return button.button_fired;
                };

                for (auto hashes : context.state.AllHashes()) {
                    for (auto const& hash : *hashes) {
                        if (do_item(hashes->name, hash.display_name, context.state.filter_mode)) {
                            dyn::Append(box_system.state->deferred_actions,
                                        [hashes, hash = hash.hash]() { hashes->Remove(hash); });
                        }
                    }
                }

                if (context.state.favourites_only) {
                    if (do_item("Favourites"_s, {}, context.state.filter_mode)) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&favourites_only = context.state.favourites_only]() {
                                        favourites_only = false;
                                    });
                    }
                }

                if (context.state.search.size)
                    if (do_item("Name contains"_s, context.state.search, FilterMode::MultipleAnd)) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&s = context.state.search]() { dyn::Clear(s); });
                    }
            }
        }

        AddPanel(
            box_system,
            {
                .run = [&](GuiBoxSystem& box_system) { options.rhs_do_items(box_system); },
                .data =
                    Subpanel {
                        .id = DoBox(box_system,
                                    {
                                        .parent = rhs,
                                        .layout {
                                            .size = layout::k_fill_parent,
                                        },
                                    })
                                  .layout_id,
                        .imgui_id = box_system.imgui.GetID("rhs"),
                        .flags = imgui::WindowFlags_ScrollbarInsidePadding | imgui::WindowFlags_NoScrollbarX,
                        .padding = {.lr = k_browser_spacing},
                        .line_height_for_scroll_wheel = k_browser_item_height,
                        .debug_name = "rhs",
                    },
            });
    }

    AddPanel(box_system,
             Panel {
                 .run =
                     [&](GuiBoxSystem& box_system) {
                         context.state.right_click_menu_state.do_menu(box_system,
                                                                      context.state.right_click_menu_state);
                     },
                 .data =
                     PopupPanel {
                         .creator_absolute_rect = context.state.right_click_menu_state.absolute_creator_rect,
                         .popup_imgui_id = k_right_click_menu_popup_id,
                     },
             });
}

void DoBrowserPopup(GuiBoxSystem& box_system,
                    BrowserPopupContext context,
                    BrowserPopupOptions const& options) {
    context.browser_id = (imgui::Id)Hash(options.title);
    context.state.browser_id = context.browser_id;

    key_nav::BeginFrame(box_system.imgui, context.state.keyboard_navigation, context.browser_id);

    RunPanel(
        box_system,
        Panel {
            .run = [&](GuiBoxSystem& box_system) { DoBrowserPopupInternal(box_system, context, options); },
            .data =
                ModalPanel {
                    .r = context.state.absolute_button_rect,
                    .imgui_id = context.browser_id,
                    .on_close = [&state = context.state.open]() { state = false; },
                    .close_on_click_outside = true,
                    .darken_background = true,
                    .disable_other_interaction = true,
                    .auto_width = true,
                    .auto_height = true,
                    .auto_position = true,
                },
        });

    key_nav::EndFrame(box_system.imgui, context.state.keyboard_navigation, context.browser_id);
}
