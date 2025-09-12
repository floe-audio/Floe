// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_common_picker.hpp"

#include "common_infrastructure/tags.hpp"

#include "gui_tips.hpp"

bool RootNodeLessThan(FolderNode const* const& a,
                      DummyValueType const&,
                      FolderNode const* const& b,
                      DummyValueType const&) {
    return a->name < b->name;
}

constexpr auto k_right_click_menu_popup_id = (imgui::Id)SourceLocationHash();

void DoRightClickForBox(GuiBoxSystem& box_system,
                        CommonPickerState& state,
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

PickerItemResult
DoPickerItem(GuiBoxSystem& box_system, CommonPickerState& state, PickerItemOptions const& options) {
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
            .behaviour = Behaviour::Button,
            .ignore_double_click = true,
        });

    for (auto const tex : options.icons) {
        if (!tex) continue;
        DoBox(box_system,
              {
                  .parent = item,
                  .background_tex = tex.NullableValue(),
                  .layout {
                      .size = style::k_library_icon_standard_size,
                      .margins = {.r = k_picker_spacing / 2},
                  },
              });
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
        ShowTipIfNeeded(
            options.notifications,
            options.store,
            "You can double-click on items on picker panels to load the item and close the panel."_s);
    }

    auto const favourite_toggled =
        !!DoBox(
              box_system,
              {
                  .parent = container,
                  .text = ICON_FA_STAR,
                  .font = FontType::Icons,
                  .font_size = style::k_font_icons_size * 0.7f,
                  .text_colours =
                      {
                          .base = options.is_favourite ? style::Colour::Highlight
                                  : item.is_hot        ? style::Colour::Overlay0
                                                       : style::Colour::None,
                          .hot = options.is_favourite ? style::Colour::Surface0 : style::Colour::Subtext0,
                          .active = options.is_favourite ? style::Colour::Surface0 : style::Colour::Subtext0,
                      },
                  .text_align_y = TextAlignY::Centre,
                  .layout {
                      .size = {24, layout::k_fill_parent},
                  },
                  .behaviour = Behaviour::Button,
              })
              .button_fired;

    return {item, favourite_toggled};
}

Box DoPickerItemsRoot(GuiBoxSystem& box_system) {
    return DoBox(box_system,
                 {
                     .layout {
                         .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                         .contents_gap = k_picker_spacing,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

static void DoFolderFilterAndChildren(GuiBoxSystem& box_system,
                                      CommonPickerState& state,
                                      Box const& parent,
                                      u8& indent,
                                      FolderNode const* folder,
                                      FolderFilterItemInfoLookupTable const& folder_infos,
                                      RightClickMenuState::Function const& do_right_click_menu = nullptr) {

    bool is_selected = false;
    for (auto f = folder; f; f = f->parent) {
        if (state.selected_folder_hashes.Contains(f->Hash())) {
            if (f == folder) is_selected = true;
            break;
        }
    }

    auto this_info = folder_infos.Find(folder);
    ASSERT(this_info);

    auto const button =
        DoFilterButton(box_system,
                       state,
                       *this_info,
                       {
                           .parent = parent,
                           .is_selected = is_selected,
                           .text = folder->display_name.size ? folder->display_name : folder->name,
                           .tooltip = folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
                           .hashes = state.selected_folder_hashes,
                           .clicked_hash = folder->Hash(),
                           .filter_mode = ({
                               auto m = state.filter_mode;
                               if (m == FilterMode::MultipleAnd) m = FilterMode::Single;
                               m;
                           }),
                           .indent = indent,
                           .full_width = true,
                       });

    if (do_right_click_menu)
        DoRightClickForBox(box_system, state, button, folder->Hash(), do_right_click_menu);

    ++indent;
    for (auto* child = folder->first_child; child; child = child->next)
        DoFolderFilterAndChildren(box_system,
                                  state,
                                  parent,
                                  indent,
                                  child,
                                  folder_infos,
                                  do_right_click_menu);
    --indent;
}

Box DoFilterButton(GuiBoxSystem& box_system,
                   CommonPickerState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);

    auto const num_used = ({
        u32 n = 0;
        switch (options.filter_mode) {
            case FilterMode::MultipleAnd: n = info.num_used_in_items_lists; break;
            case FilterMode::MultipleOr: n = info.total_available; break;
            case FilterMode::Single: n = info.total_available; break;
            case FilterMode::Count: PanicIfReached();
        }
        n;
    });

    constexpr f32 k_indent_size = 10;

    constexpr f32 k_font_icon_scale = 0.6f;
    auto const font_icon_width =
        (box_system.fonts[ToInt(FontType::Icons)]->GetCharAdvance(Utf8CharacterToUtf32(ICON_FA_CHECK)) *
         k_font_icon_scale) -
        4; // It seems the character advance isn't very accurate so we subtract a bit to make it fit better.
    constexpr f32 k_font_icons_font_size = style::k_font_icons_size * k_font_icon_scale;
    constexpr f32 k_font_icon_gap = 5;
    f32 const lr_spacing = options.full_width ? 6 : 4;

    auto const button = DoBox(
        box_system,
        {
            .parent = options.parent,
            .background_fill_colours =
                {
                    .base = options.is_selected  ? style::Colour::Highlight
                            : options.full_width ? style::Colour::None
                                                 : style::Colour::Background2,
                    .hot = options.is_selected ? style::Colour::Highlight : style::Colour::DarkModeOverlay0,
                    .active =
                        options.is_selected ? style::Colour::Highlight : style::Colour::DarkModeOverlay0,
                },
            .background_fill_alpha = options.full_width && !options.is_selected ? (u8)60 : (u8)255,
            .background_fill_auto_hot_active_overlay = !options.full_width,
            .round_background_corners = 0b1111,
            .round_background_fully = !options.full_width,
            .layout {
                .size {
                    options.full_width ? layout::k_fill_parent : layout::k_hug_contents,
                    k_picker_item_height,
                },
                .margins = {.b = options.no_bottom_margin ? 0
                                 : options.full_width     ? 0
                                                          : k_picker_spacing / 2},
                .contents_padding {
                    .l = (options.indent * k_indent_size) + ({
                             f32 extra = 0;
                             switch (options.font_icon.tag) {
                                 case FilterButtonOptions::FontIconMode::NeverHasIcon:
                                     if (!options.icon) extra = lr_spacing;
                                     break;
                                 case FilterButtonOptions::FontIconMode::HasIcon:
                                     extra = lr_spacing - 2;
                                     break;
                                 case FilterButtonOptions::FontIconMode::SometimesHasIcon:
                                     extra = font_icon_width + k_font_icon_gap * 2;
                                     break;
                             }
                             extra;
                         }),
                    .r = lr_spacing,
                },
                .contents_align = layout::Alignment::Start,
                .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
            },
            .tooltip = options.tooltip,
            .behaviour = Behaviour::Button,
        });

    bool grey_out = false;
    if (options.filter_mode == FilterMode::MultipleAnd) grey_out = num_used == 0;

    if (auto const icon = options.font_icon.TryGet<String>()) {
        DoBox(box_system,
              {
                  .parent = button,
                  .text = *icon,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_font_size,
                  .text_colours = {options.full_width
                                       ? style::Colour::DarkModeSubtext0
                                       : (grey_out ? style::Colour::Overlay1 : style::Colour::Subtext0)},
                  .layout {
                      .size = {font_icon_width, k_font_icons_font_size},
                      .margins = {.lr = k_font_icon_gap},
                  },
              });
    }

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
              .text = options.text,
              .size_from_text = !options.full_width,
              .font = FontType::Body,
              .text_colours {
                  .base = options.full_width
                              ? options.is_selected ? style::Colour::Text : style::Colour::DarkModeText
                              : (grey_out ? style::Colour::Surface1 : style::Colour::Text),
                  .hot = options.full_width && !options.is_selected ? style::Colour::DarkModeText
                                                                    : style::Colour::Text,
                  .active = options.full_width && !options.is_selected ? style::Colour::DarkModeText
                                                                       : style::Colour::Text,
              },
              .text_overflow =
                  options.full_width ? TextOverflowType::ShowDotsOnRight : TextOverflowType::AllowOverflow,
              .parent_dictates_hot_and_active = true,
              .layout =
                  {
                      .size = options.full_width ? f32x2 {layout::k_fill_parent, style::k_font_body_size}
                                                 : f32x2 {999},
                      .margins = {.l = options.icon ? 0 : k_picker_spacing / 2},
                  },
          });

    // We size to the largest possible number so that the layout doesn't jump around as the num_used changes.
    auto const total_text = fmt::FormatInline<32>("({})"_s, info.total_available);
    auto const number_size =
        !options.full_width
            ? Max(box_system.fonts[ToInt(FontType::Body)]->CalcTextSizeA(style::k_font_body_size,
                                                                         FLT_MAX,
                                                                         0.0f,
                                                                         total_text) -
                      f32x2 {4, 0},
                  f32x2 {0, 0})
            : f32x2 {};
    DoBox(
        box_system,
        {
            .parent = button,
            .text = num_used == info.total_available ? total_text : fmt::FormatInline<32>("({})"_s, num_used),
            .size_from_text = options.full_width,
            .font = FontType::Heading3,
            .text_colours {
                .base = options.full_width
                            ? options.is_selected ? style::Colour::Text : style::Colour::DarkModeText
                            : (grey_out ? style::Colour::Surface1 : style::Colour::Text),
                .hot = options.full_width && !options.is_selected ? style::Colour::DarkModeText
                                                                  : style::Colour::Text,
                .active = options.full_width && !options.is_selected ? style::Colour::DarkModeText
                                                                     : style::Colour::Text,
            },
            .text_align_y = TextAlignY::Centre,
            .parent_dictates_hot_and_active = true,
            .round_background_corners = 0b1111,
            .layout {
                .size = number_size,
                .margins = {.l = options.full_width ? 0.0f : 3},
            },
        });

    if (button.button_fired) {
        dyn::Append(box_system.state->deferred_actions,
                    [&hashes = options.hashes,
                     &state = state,
                     clicked_hash = options.clicked_hash,
                     display_name = box_system.arena.Clone(options.text),
                     is_selected = options.is_selected,
                     filter_mode = options.filter_mode]() {
                        switch (filter_mode) {
                            case FilterMode::Single: {
                                state.ClearAll();
                                if (!is_selected) hashes.Add(clicked_hash, display_name);
                                break;
                            }
                            case FilterMode::MultipleAnd: {
                                if (is_selected)
                                    hashes.Remove(clicked_hash);
                                else
                                    hashes.Add(clicked_hash, display_name);
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

    return button;
}

// Similar to DoFilterButton but a larger full-width rounded box that contains a dark background with a
// translucent background image overlayed, a larger icon, title text, subtext, and the number of items. These
// are the large cards that will be used to select the sample library or preset bank.
Box DoFilterCard(GuiBoxSystem& box_system,
                 CommonPickerState& state,
                 FilterItemInfo const& info,
                 FilterCardOptions const& options) {
    auto const scoped_tooltips = ScopedEnableTooltips(box_system, true);

    auto const num_used = info.total_available;

    bool const is_selected = options.is_selected;

    constexpr f32 k_card_padding = 6.0f;
    constexpr f32 k_icon_size = 28.0f;
    constexpr f32 k_text_spacing = 8.0f;
    constexpr f32 k_selected_line_width = 6;

    auto const card_outer = DoBox(box_system,
                                  {
                                      .parent = options.parent,
                                      .background_fill_colours = {style::Colour::DarkModeBackground0},
                                      .background_tex = options.background_image1,
                                      .background_tex_alpha = 180,
                                      .background_tex_fill_mode = BackgroundTexFillMode::Cover,
                                      .round_background_corners = 0b1111,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .margins = {.b = k_picker_spacing},
                                          .contents_direction = layout::Direction::Row,
                                      },
                                  });
    auto const card = DoBox(box_system,
                            {
                                .parent = card_outer,
                                .background_tex = options.background_image2,
                                .background_tex_alpha = 15,
                                .background_tex_fill_mode = BackgroundTexFillMode::Cover,
                                .round_background_corners = 0b1111,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_direction = layout::Direction::Row,
                                },
                            });

    if (is_selected) {
        // Selected highlight bar on the left side of the card
        DoBox(box_system,
              {
                  .parent = card,
                  .background_fill_colours = {style::Colour::Highlight},
                  .round_background_corners = 0b1001,
                  .layout {
                      .size = {k_selected_line_width, layout::k_fill_parent},
                  },
              });
    }

    auto const card_content = DoBox(box_system,
                                    {
                                        .parent = card,
                                        // .background_fill_colours = {style::Colour::DarkModeBackground0},
                                        .round_background_corners = 0b1111,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_direction = layout::Direction::Column,
                                            .contents_align = layout::Alignment::Start,
                                            .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                        },
                                    });

    auto const card_top =
        DoBox(box_system,
              {
                  .parent = card_content,
                  .background_fill_colours =
                      {
                          .base = style::Colour::None,
                          .hot = style::Colour::DarkModeOverlay2,
                          .active = style::Colour::DarkModeOverlay2,
                      },
                  .background_fill_alpha = 50,
                  .round_background_corners = !is_selected ? 0b1111u : 0b0110,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding =
                          {
                              .l = k_card_padding + (is_selected ? k_selected_line_width : 0),
                              .r = k_card_padding,
                              .tb = k_card_padding,
                          },
                      .contents_gap = k_card_padding,
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = options.tooltip,
                  .behaviour = Behaviour::Button,
              });

    // Icon
    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = card_top,
                  .background_tex = options.icon,
                  .layout {
                      .size = k_icon_size,
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

    // Title text
    auto const title_box = DoBox(box_system,
                                 {
                                     .parent = rhs,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = k_text_spacing / 2,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                     },
                                 });
    DoBox(box_system,
          {
              .parent = title_box,
              .text = options.text,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Heading2,
              .text_colours {
                  .base = style::Colour::DarkModeText,
                  .hot = style::Colour::DarkModeText,
                  .active = style::Colour::DarkModeText,
              },
              .parent_dictates_hot_and_active = true,
          });
    // Number of items
    auto const total_text = fmt::FormatInline<32>("({})"_s, info.total_available);
    DoBox(
        box_system,
        {
            .parent = title_box,
            .text = num_used == info.total_available ? total_text : fmt::FormatInline<32>("({})"_s, num_used),
            .size_from_text = true,
            .font = FontType::Heading3,
            .text_colours {
                .base = style::Colour::DarkModeSubtext1,
                .hot = style::Colour::DarkModeText,
                .active = style::Colour::DarkModeText,
            },
            .parent_dictates_hot_and_active = true,
        });

    // Subtext
    DoBox(box_system,
          {
              .parent = rhs,
              .text = options.subtext,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::Heading3,
              .text_colours {
                  .base = style::Colour::DarkModeSubtext1,
                  .hot = style::Colour::DarkModeSubtext0,
                  .active = style::Colour::DarkModeSubtext0,
              },
              .parent_dictates_hot_and_active = true,
          });

    // Handle click behavior
    if (card_top.button_fired) {
        dyn::Append(box_system.state->deferred_actions,
                    [&hashes = options.hashes,
                     &state = state,
                     clicked_hash = options.clicked_hash,
                     display_name = box_system.arena.Clone(options.text),
                     is_selected = is_selected,
                     filter_mode = options.filter_mode]() {
                        switch (filter_mode) {
                            case FilterMode::Single: {
                                state.ClearAll();
                                if (!is_selected) hashes.Add(clicked_hash, display_name);
                                break;
                            }
                            case FilterMode::MultipleAnd: {
                                // In card mode, we assume that each item can only belong to a single card, so
                                // AND mode is not useful. Instead, we treat it like Single mode, except we
                                // only clear the current hashes, not all state.
                                hashes.Clear();
                                if (!is_selected) hashes.Add(clicked_hash, display_name);
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

    if (options.folder && options.folder->first_child) {
        // Carat down
        // DoBox(box_system,
        //       {
        //           .parent = card_top,
        //           .text = ICON_FA_CARET_DOWN,
        //           .font = FontType::Icons,
        //           .font_size = style::k_font_icons_size * 0.6f,
        //           .text_colours = {style::Colour::DarkModeSubtext0},
        //           .text_align_x = TextAlignX::Centre,
        //           .text_align_y = TextAlignY::Centre,
        //           .layout {
        //               .size = style::k_font_icons_size * 0.4f,
        //           },
        //       });

        auto const folder_box = DoBox(box_system,
                                      {
                                          .parent = card_content,
                                          .background_fill_colours =
                                              {
                                                  .base = style::Colour::DarkModeBackground0,
                                                  .hot = style::Colour::DarkModeOverlay1,
                                                  .active = style::Colour::DarkModeOverlay1,
                                              },
                                          .background_fill_alpha = 150,
                                          .round_background_corners = 0b0011,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_padding = {.tb = k_card_padding / 2},
                                              .contents_direction = layout::Direction::Column,
                                          },
                                      });

        // Do the folder children, not the root folder.
        for (auto* child = options.folder->first_child; child; child = child->next) {
            u8 indent = 0;
            DoFolderFilterAndChildren(box_system, state, folder_box, indent, child, options.folder_infos);
        }
    }

    return card_top;
}

Optional<Box> DoPickerSectionContainer(GuiBoxSystem& box_system,
                                       u64 id,
                                       CommonPickerState& state,
                                       PickerItemsSectionOptions const& options) {
    auto const container = DoBox(
        box_system,
        {
            .parent = options.parent,
            .layout =
                {
                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                    .contents_padding = {.l = options.subsection ? k_picker_spacing / 2 : 0},
                    .contents_gap = f32x2 {0, options.bigger_contents_gap ? k_picker_spacing * 1.5f : 0},
                    .contents_direction = layout::Direction::Column,
                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                },
        });

    auto const heading_container =
        DoBox(box_system,
              {
                  .parent = container,
                  .background_fill_auto_hot_active_overlay = true,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_gap = k_picker_spacing / 2,
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                  },
                  .tooltip = options.folder ? TooltipString {"Folder"_s} : k_nullopt,
                  .behaviour = Behaviour::Button,
              });

    if (heading_container.button_fired) {
        dyn::Append(box_system.state->deferred_actions, [&state, id]() {
            if (Contains(state.hidden_filter_headers, id))
                dyn::RemoveValue(state.hidden_filter_headers, id);
            else
                dyn::Append(state.hidden_filter_headers, id);
        });
    }

    if (options.right_click_menu)
        DoRightClickForBox(box_system, state, heading_container, id, options.right_click_menu);

    bool const is_hidden = Contains(state.hidden_filter_headers, id);

    DoBox(box_system,
          {
              .parent = heading_container,
              .text = is_hidden ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN,
              .font = FontType::Icons,
              .font_size = style::k_font_icons_size * 0.6f,
              .text_colours = {style::Colour::Subtext0},
              .layout {
                  .size = style::k_font_icons_size * 0.4f,
              },
          });

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = heading_container,
                  .text = *options.icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = style::k_font_icons_size * 0.7f,
              });
    }

    {
        DynamicArray<char> buf {box_system.arena};

        String text = options.heading.ValueOr({});

        if (options.capitalise) {
            dyn::Resize(buf, text.size);
            for (auto i : Range(text.size))
                buf[i] = ToUppercaseAscii(text[i]);
            text = buf;
        } else if (options.folder) {
            DynamicArrayBounded<String, sample_lib::k_max_folders + 1> parts;
            for (auto f = options.folder; f; f = f->parent)
                dyn::Append(parts, f->display_name.size ? f->display_name : f->name);

            if (options.skip_root_folder && parts.size > 1) dyn::Pop(parts);

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
                          .margins = {.b = k_picker_spacing / 2},
                      },
                  });
        }
    }

    if (is_hidden) return k_nullopt;

    if (!options.multiline_contents) return container;

    return DoBox(box_system,
                 {
                     .parent = container,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_gap = k_picker_spacing / 2,
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

static void DoLibraryRightClickMenu(GuiBoxSystem& box_system,
                                    PickerPopupContext& context,
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

    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Open Containing Folder",
                     .is_selected = false,
                 })) {
        auto const find_library = [&](u64 library_hash) -> Optional<sample_lib::LibraryIdRef> {
            for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries)
                if (lib_hash == library_hash) return lib_id;
            return k_nullopt;
        };

        if (auto const lib_id = find_library(menu_state.item_hash)) {
            auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, *lib_id);
            DEFER { lib.Release(); };

            if (lib)
                if (auto const dir = path::Directory(lib->path)) OpenFolderInFileBrowser(*dir);
        }
    }
}

static void DoPickerLibraryFilters(GuiBoxSystem& box_system,
                                   PickerPopupContext& context,
                                   Box const& parent,
                                   LibraryFilters const& library_filters,
                                   u8& sections) {
    if (library_filters.libraries.size) {
        if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++sections;

        auto const section = DoPickerSectionContainer(box_system,
                                                      context.picker_id + SourceLocationHash(),
                                                      context.state,
                                                      {
                                                          .parent = parent,
                                                          .heading = "LIBRARIES"_s,
                                                          .multiline_contents = !library_filters.card_view,
                                                      });

        if (section) {
            for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
                ASSERT(lib_id.name.size);
                ASSERT(lib_id.author.size);

                Box button;
                if (library_filters.card_view) {
                    // TODO: we probably want to cache this somewhere.
                    auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, lib_id);
                    DEFER { lib.Release(); };
                    if (!lib) continue;

                    auto const folder = &lib->root_folders[ToInt(library_filters.resource_type)];

                    auto const is_selected = context.state.selected_library_hashes.Contains(lib_hash);

                    Optional<graphics::ImageID> icon = library_filters.unknown_library_icon;
                    Optional<graphics::ImageID> background1 = {};
                    Optional<graphics::ImageID> background2 = {};
                    if (auto imgs = LibraryImagesFromLibraryId(library_filters.library_images,
                                                               box_system.imgui,
                                                               lib_id,
                                                               context.sample_library_server,
                                                               box_system.arena,
                                                               false)) {
                        if (!imgs->background_missing) {
                            background1 = imgs->blurred_background;
                            background2 = imgs->background;
                        }
                        if (!imgs->icon_missing) icon = imgs->icon;
                    }

                    button = DoFilterCard(box_system,
                                          context.state,
                                          lib_info,
                                          FilterCardOptions {
                                              .parent = *section,
                                              .is_selected = is_selected,
                                              .icon = icon.NullableValue(),
                                              .background_image1 = background1.NullableValue(),
                                              .background_image2 = background2.NullableValue(),
                                              .text = lib_id.name,
                                              .subtext = ({
                                                  String s;
                                                  if (lib) s = box_system.arena.Clone(lib->tagline);
                                                  s;
                                              }),
                                              .tooltip = FunctionRef<String()>([&]() -> String {
                                                  auto lib = sample_lib_server::FindLibraryRetained(
                                                      context.sample_library_server,
                                                      lib_id);
                                                  DEFER { lib.Release(); };

                                                  DynamicArray<char> buf {box_system.arena};
                                                  fmt::Append(buf, "{} by {}.", lib_id.name, lib_id.author);
                                                  if (lib) {
                                                      if (lib->description)
                                                          fmt::Append(buf, "\n\n{}", lib->description);
                                                  }
                                                  return buf.ToOwnedSpan();
                                              }),
                                              .hashes = context.state.selected_library_hashes,
                                              .clicked_hash = lib_hash,
                                              .filter_mode = context.state.filter_mode,
                                              .folder_infos = library_filters.folders,
                                              .folder = folder,
                                          });
                } else {
                    // TODO: huge duplicate code with above
                    button = DoFilterButton(
                        box_system,
                        context.state,
                        lib_info,
                        FilterButtonOptions {
                            .parent = *section,
                            .is_selected = context.state.selected_library_hashes.Contains(lib_hash),
                            .icon = ({
                                graphics::ImageID const* tex =
                                    library_filters.unknown_library_icon.NullableValue();
                                if (auto imgs = LibraryImagesFromLibraryId(library_filters.library_images,
                                                                           box_system.imgui,
                                                                           lib_id,
                                                                           context.sample_library_server,
                                                                           box_system.arena,
                                                                           true);
                                    imgs && !imgs->icon_missing) {
                                    tex = imgs->icon.NullableValue();
                                }
                                tex;
                            }),
                            .text = lib_id.name,
                            .tooltip = FunctionRef<String()>([&]() -> String {
                                auto lib =
                                    sample_lib_server::FindLibraryRetained(context.sample_library_server,
                                                                           lib_id);
                                DEFER { lib.Release(); };

                                DynamicArray<char> buf {box_system.arena};
                                fmt::Append(buf, "{} by {}.", lib_id.name, lib_id.author);
                                if (lib) {
                                    if (lib->description) fmt::Append(buf, "\n\n{}", lib->description);
                                }
                                return buf.ToOwnedSpan();
                            }),
                            .hashes = context.state.selected_library_hashes,
                            .clicked_hash = lib_hash,
                            .filter_mode = context.state.filter_mode,
                        });
                }

                DoRightClickForBox(
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
                options.parent = *section;

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

static void DoPickerLibraryAuthorFilters(GuiBoxSystem& box_system,
                                         PickerPopupContext& context,
                                         Box const& parent,
                                         LibraryFilters const& library_filters,
                                         u8& sections) {
    if (library_filters.library_authors.size) {
        if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
        ++sections;

        auto const section = DoPickerSectionContainer(box_system,
                                                      context.picker_id + SourceLocationHash(),
                                                      context.state,
                                                      {
                                                          .parent = parent,
                                                          .heading = "LIBRARY AUTHORS"_s,
                                                          .multiline_contents = true,
                                                      });

        if (section) {
            for (auto const [author, author_info, author_hash] : library_filters.library_authors) {
                auto const is_selected = context.state.selected_library_author_hashes.Contains(author_hash);
                DoFilterButton(box_system,
                               context.state,
                               author_info,
                               {
                                   .parent = *section,
                                   .is_selected = is_selected,
                                   .text = author,
                                   .hashes = context.state.selected_library_author_hashes,
                                   .clicked_hash = author_hash,
                                   .filter_mode = context.state.filter_mode,
                               });
            }
        }
    }
}

void DoPickerTagsFilters(GuiBoxSystem& box_system,
                         PickerPopupContext& context,
                         Box const& parent,
                         TagsFilters const& tags_filters,
                         u8& sections) {
    if (!tags_filters.tags.size) return;

    if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
    ++sections;

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

    auto const tags_container = DoPickerSectionContainer(box_system,
                                                         context.picker_id + SourceLocationHash(),
                                                         context.state,
                                                         {
                                                             .parent = parent,
                                                             .heading = "TAGS",
                                                             .multiline_contents = false,
                                                             .bigger_contents_gap = true,
                                                         });

    if (tags_container) {
        for (auto [category, tags_for_category, category_hash] : standard_tags) {
            auto const category_info = Tags(category);
            auto const section = DoPickerSectionContainer(box_system,
                                                          context.picker_id + category_hash,
                                                          context.state,
                                                          {
                                                              .parent = *tags_container,
                                                              .heading = category_info.name,
                                                              .icon = category_info.font_awesome_icon,
                                                              .capitalise = true,
                                                              .multiline_contents = true,
                                                              .subsection = true,
                                                          });

            if (!section) continue;

            for (auto const [tag, filter_item_info, _] : tags_for_category) {
                auto const tag_info = GetTagInfo(tag);
                auto const tag_hash = Hash(tag_info.name);
                auto const is_selected = context.state.selected_tags_hashes.Contains(tag_hash);
                DoFilterButton(box_system,
                               context.state,
                               filter_item_info,
                               {
                                   .parent = *section,
                                   .is_selected = is_selected,
                                   .text = tag_info.name,
                                   .hashes = context.state.selected_tags_hashes,
                                   .clicked_hash = tag_hash,
                                   .filter_mode = context.state.filter_mode,
                               });
            }
        }

        if (non_standard_tags.size) {
            auto const section = DoPickerSectionContainer(box_system,
                                                          context.picker_id + SourceLocationHash(),
                                                          context.state,
                                                          {
                                                              .parent = *tags_container,
                                                              .heading = "UNCATEGORISED",
                                                              .multiline_contents = true,
                                                              .subsection = true,
                                                          });

            if (section) {
                for (auto const [name, filter_item_info, _] : non_standard_tags) {
                    auto const is_selected = context.state.selected_tags_hashes.Contains(Hash(name));
                    DoFilterButton(box_system,
                                   context.state,
                                   filter_item_info,
                                   {
                                       .parent = *section,
                                       .is_selected = is_selected,
                                       .text = name,
                                       .hashes = context.state.selected_tags_hashes,
                                       .clicked_hash = Hash(name),
                                       .filter_mode = context.state.filter_mode,
                                   });
                }
            }
        }
    }
}

static String FilterModeText(FilterMode mode) {
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

static void DoFilterModeMenu(GuiBoxSystem& box_system, PickerPopupContext& context) {
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
                     })) {
            dyn::Append(
                box_system.state->deferred_actions,
                [&mode = context.state.filter_mode, new_mode = filter_mode, &state = context.state]() {
                    if (mode != FilterMode::Single && new_mode == FilterMode::Single) state.ClearToOne();
                    mode = new_mode;
                });
        }
    }
}

static void DoPickerPopupInternal(GuiBoxSystem& box_system,
                                  PickerPopupContext& context,
                                  PickerPopupOptions const& options) {
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
                          .contents_padding = {.lrtb = k_picker_spacing},
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                  });
        DoBox(box_system,
              {
                  .parent = title_container,
                  .text = options.title,
                  .font = FontType::Heading2,
                  .layout {
                      .size = {layout::k_fill_parent, style::k_font_heading2_size},
                  },
              });
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

    if (options.current_tab_index) {
        ASSERT(options.tab_config.size > 0);
        DoModalTabBar(box_system,
                      {
                          .parent = root,
                          .tabs = options.tab_config,
                          .current_tab_index = *options.current_tab_index,
                      });
    }

    {
        auto const headings_row = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });

        {
            auto const lhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.filters_col_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_gap = k_picker_spacing / 2,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = lhs_top,
                      .text = "Filters",
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            if (options.library_filters || options.has_extra_filters) {
                auto const popup_id = box_system.imgui.GetID("filtermode");
                auto const popup_btn = MenuButton(box_system,
                                                  lhs_top,
                                                  {
                                                      .text = FilterModeText(context.state.filter_mode),
                                                      .tooltip = "Select filtering mode"_s,
                                                  });
                if (popup_btn.button_fired) box_system.imgui.OpenPopup(popup_id, popup_btn.imgui_id);

                AddPanel(box_system,
                         Panel {
                             .run = [&context](
                                        GuiBoxSystem& box_system) { DoFilterModeMenu(box_system, context); },
                             .data =
                                 PopupPanel {
                                     .debug_name = "filtermode",
                                     .creator_layout_id = popup_btn.layout_id,
                                     .popup_imgui_id = popup_id,
                                     .additional_imgui_window_flags =
                                         imgui::WindowFlags_PositionOnTopOfParentPopup,
                                 },
                         });
            }

            if (context.state.HasFilters()) {
                if (IconButton(box_system,
                               lhs_top,
                               ICON_FA_XMARK,
                               "Clear all filters",
                               style::k_font_heading2_size * 0.9f,
                               style::k_font_heading2_size)
                        .button_fired) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&context]() { context.state.ClearAll(); });
                }
            }
        }

        DoModalDivider(box_system, headings_row, DividerType::Vertical);

        {
            auto const rhs_top =
                DoBox(box_system,
                      {
                          .parent = headings_row,
                          .layout {
                              .size = {options.rhs_width, layout::k_hug_contents},
                              .contents_padding = {.lr = k_picker_spacing, .tb = k_picker_spacing / 2},
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

            DoBox(box_system,
                  {
                      .parent = rhs_top,
                      .text = options.items_section_heading,
                      .font = FontType::Heading2,
                      .layout {
                          .size = {layout::k_fill_parent, style::k_font_heading2_size},
                      },
                  });

            for (auto const& btn : ArrayT<PickerPopupOptions::Button>({
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
        }
    }

    DoModalDivider(box_system, root, DividerType::Horizontal);

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
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                   },
                               });

        AddPanel(box_system,
                 {
                     .run =
                         [&](GuiBoxSystem& box_system) {
                             if (!options.library_filters && !options.tags_filters) return;

                             auto const root = DoPickerItemsRoot(box_system);

                             u8 num_lhs_sections = 0;

                             if (options.do_extra_filters_top)
                                 options.do_extra_filters_top(box_system, root, num_lhs_sections);

                             if (options.library_filters)
                                 DoPickerLibraryFilters(box_system,
                                                        context,
                                                        root,
                                                        *options.library_filters,
                                                        num_lhs_sections);

                             if (options.tags_filters)
                                 DoPickerTagsFilters(box_system,
                                                     context,
                                                     root,
                                                     *options.tags_filters,
                                                     num_lhs_sections);

                             if (options.library_filters)
                                 DoPickerLibraryAuthorFilters(box_system,
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
                             .flags = imgui::WindowFlags_NoScrollbarX,
                             .debug_name = "filters",
                         },
                 });
    }

    DoModalDivider(box_system, main_section, DividerType::Vertical);

    {
        auto const rhs = DoBox(box_system,
                               {
                                   .parent = main_section,
                                   .layout {
                                       .size = {options.rhs_width, layout::k_fill_parent},
                                       .contents_padding = {.lr = k_picker_spacing, .t = k_picker_spacing},
                                       .contents_gap = k_picker_spacing,
                                       .contents_direction = layout::Direction::Column,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                   },
                               });

        {
            if (auto const& btn = options.rhs_top_button) {
                if (TextButton(box_system,
                               rhs,
                               {
                                   .text = btn->text,
                                   .tooltip = btn->tooltip,
                                   .fill_x = true,
                                   .disabled = options.rhs_top_button->disabled,
                               }))
                    dyn::Append(box_system.state->deferred_actions, [&]() { btn->on_fired(); });
            }

            auto const search_and_fave_box =
                DoBox(box_system,
                      {
                          .parent = rhs,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = k_picker_spacing / 2,
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
                                  .contents_padding = {.lr = k_picker_spacing / 2},
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
                          .font_size = k_picker_item_height * 0.8f,
                          .text_colours = {style::Colour::Subtext0},
                      });

                auto const text_input = DoBox(box_system,
                                              {
                                                  .parent = search_box,
                                                  .text = context.state.search,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                                  },
                                                  .behaviour = Behaviour::TextInput,
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

                if (context.state.search.size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_XMARK,
                                  .size_from_text = true,
                                  .font = FontType::Icons,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .text_colours = {style::Colour::Subtext0},
                                  .background_fill_auto_hot_active_overlay = true,
                                  .behaviour = Behaviour::Button,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&s = context.state.search]() { dyn::Clear(s); });
                    }
                }
            }

            {
                SelectedHashes dummy_hashes {};
                if (DoFilterButton(box_system,
                                   context.state,
                                   options.favourites_filter_info,
                                   {
                                       .parent = search_and_fave_box,
                                       .is_selected = context.state.favourites_only,
                                       .text = "Favourites"_s,
                                       .hashes = dummy_hashes,
                                       .clicked_hash = 1,
                                       .filter_mode = context.state.filter_mode,
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
                              .parent = rhs,
                              .layout {
                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                  .contents_gap = k_picker_spacing / 2,
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
                                  .text = mode == FilterMode::MultipleOr ? "OR"_s : "AND",
                                  .size_from_text = true,
                                  .size_from_text_preserve_height = true,
                                  .font = FontType::Heading3,
                                  .font_size = style::k_font_heading3_size * 0.8f,
                                  .text_colours = {style::Colour::Subtext0},
                                  .text_align_y = TextAlignY::Centre,
                                  .layout {
                                      .size = {1, k_picker_item_height + (k_picker_spacing / 2)},
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
                                      .size = {layout::k_hug_contents, k_picker_item_height},
                                      .margins {.b = k_picker_spacing / 2},
                                      .contents_padding {.lr = style::k_spacing / 2},
                                      .contents_gap = style::k_spacing / 2,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::Middle,
                                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                  },
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
                    // 'x' icon using font awesome
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

        AddPanel(box_system,
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

void DoPickerPopup(GuiBoxSystem& box_system, PickerPopupContext context, PickerPopupOptions const& options) {
    context.picker_id = (imgui::Id)Hash(options.title);
    RunPanel(
        box_system,
        Panel {
            .run = [&](GuiBoxSystem& box_system) { DoPickerPopupInternal(box_system, context, options); },
            .data =
                ModalPanel {
                    .r = context.state.absolute_button_rect,
                    .imgui_id = context.picker_id,
                    .on_close = [&state = context.state.open]() { state = false; },
                    .close_on_click_outside = true,
                    .darken_background = true,
                    .disable_other_interaction = true,
                    .auto_width = true,
                    .auto_height = true,
                    .auto_position = true,
                },
        });
}
