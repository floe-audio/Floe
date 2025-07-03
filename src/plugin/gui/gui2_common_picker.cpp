// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_common_picker.hpp"

#include "common_infrastructure/tags.hpp"

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
    if (AdditionalClickBehaviour(
            box_system,
            box,
            {.button = MouseButton::Right, .activation_click_event = ActivationClickEvent::Up},
            &state.right_click_menu_state.absolute_creator_rect)) {
        state.right_click_menu_state.do_menu = do_menu;
        state.right_click_menu_state.item_hash = item_hash;
        box_system.imgui.OpenPopup(k_right_click_menu_popup_id, box.imgui_id);
    }
}

Box DoPickerItem(GuiBoxSystem& box_system, CommonPickerState& state, PickerItemOptions const& options) {
    auto item =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .background_fill = options.is_current ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                          .contents_direction = layout::Direction::Row,
                      },
                  .tooltip = options.tooltip,
              });

    for (auto tex : options.icons) {
        if (!tex) continue;
        DoBox(box_system,
              {
                  .parent = item,
                  .background_tex = tex,
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
              .font = FontType::Body,
              .size_from_text = true,
          });

    if (options.is_current &&
        AdditionalClickBehaviour(box_system,
                                 item,
                                 {
                                     .button = MouseButton::Left,
                                     .use_double_click = true,
                                     .activation_click_event = ActivationClickEvent::Down,
                                 })) {
        state.open = false;
    }

    return item;
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

Box DoFilterButton(GuiBoxSystem& box_system,
                   CommonPickerState& state,
                   FilterItemInfo const& info,
                   FilterButtonOptions const& options) {
    auto const num_used = ({
        u32 n = 0;
        switch (options.filter_mode) {
            case FilterMode::ProgressiveNarrowing: n = info.num_used_in_items_lists; break;
            case FilterMode::AdditiveSelection: n = info.total_available; break;
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

    auto const button =
        DoBox(box_system,
              {
                  .parent = options.parent,
                  .background_fill = options.is_selected ? style::Colour::Highlight : style::Colour::None,
                  .background_fill_active = style::Colour::Highlight,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {options.full_width ? layout::k_fill_parent : layout::k_hug_contents,
                               k_picker_item_height},
                      .contents_padding =
                          {
                              .l = (options.indent * k_indent_size) + ({
                                       f32 extra = 0;
                                       switch (options.font_icon.tag) {
                                           case FilterButtonOptions::FontIconMode::NeverHasIcon:
                                               extra = 0;
                                               break;
                                           case FilterButtonOptions::FontIconMode::HasIcon: extra = 0; break;
                                           case FilterButtonOptions::FontIconMode::SometimesHasIcon:
                                               extra = font_icon_width + k_font_icon_gap * 2;
                                               break;
                                       }
                                       extra;
                                   }),
                              .r = options.full_width ? 0 : style::k_spacing / 2,
                          },
                      .contents_align = layout::Alignment::Start,
                      .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                  },
                  .tooltip = options.tooltip,
              });

    bool grey_out = false;
    if (options.filter_mode == FilterMode::ProgressiveNarrowing) grey_out = num_used == 0;

    if (auto const icon = options.font_icon.TryGet<String>()) {
        DoBox(box_system,
              {
                  .parent = button,
                  .text = *icon,
                  .font_size = k_font_icons_font_size,
                  .font = FontType::Icons,
                  .text_fill = grey_out ? style::Colour::Overlay1 : style::Colour::Subtext0,
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
              .font = FontType::Body,
              .text_fill = grey_out ? style::Colour::Surface1 : style::Colour::Text,
              .text_fill_hot = style::Colour::Text,
              .text_fill_active = style::Colour::Text,
              .size_from_text = !options.full_width,
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
        !options.full_width ? box_system.fonts[ToInt(FontType::Body)]->CalcTextSizeA(style::k_font_body_size,
                                                                                     FLT_MAX,
                                                                                     0.0f,
                                                                                     total_text) -
                                  f32x2 {4, 0}
                            : f32x2 {};
    DoBox(
        box_system,
        {
            .parent = button,
            .text = num_used == info.total_available ? total_text : fmt::FormatInline<32>("({})"_s, num_used),
            .font = FontType::Body,
            .text_fill = grey_out ? style::Colour::Surface1 : style::Colour::Text,
            .text_fill_hot = style::Colour::Text,
            .text_fill_active = style::Colour::Text,
            .size_from_text = options.full_width,
            .round_background_corners = 0b1111,
            .parent_dictates_hot_and_active = true,
            .layout =
                {
                    .size = number_size,
                    .margins = {.l = options.full_width ? 0.0f : 3},
                },
        });

    if (button.button_fired) {
        dyn::Append(box_system.state->deferred_actions,
                    [&hashes = options.hashes,
                     &state = state,
                     clicked_hash = options.clicked_hash,
                     num_used = num_used,
                     is_selected = options.is_selected,
                     filter_mode = options.filter_mode]() {
                        switch (filter_mode) {
                            case FilterMode::ProgressiveNarrowing: {
                                if (is_selected) {
                                    dyn::RemoveValue(hashes, clicked_hash);
                                } else {
                                    if (num_used == 0) state.ClearAll();
                                    dyn::Append(hashes, clicked_hash);
                                }
                                break;
                            }
                            case FilterMode::AdditiveSelection: {
                                if (is_selected)
                                    dyn::RemoveValue(hashes, clicked_hash);
                                else
                                    dyn::Append(hashes, clicked_hash);
                                break;
                            }
                            case FilterMode::Count: PanicIfReached();
                        }
                    });
    }

    return button;
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

    auto const heading_container = DoBox(box_system,
                                         {
                                             .parent = container,
                                             .background_fill_auto_hot_active_overlay = true,
                                             .activate_on_click_button = MouseButton::Left,
                                             .activation_click_event = ActivationClickEvent::Up,
                                             .layout {
                                                 .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                 .contents_gap = k_picker_spacing / 2,
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                                 .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                             },
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
              .font_size = style::k_font_icons_size * 0.6f,
              .font = FontType::Icons,
              .text_fill = style::Colour::Subtext0,
              .layout =
                  {
                      .size = style::k_font_icons_size * 0.4f,
                  },
          });

    if (options.icon) {
        DoBox(box_system,
              {
                  .parent = heading_container,
                  .text = *options.icon,
                  .font_size = style::k_font_icons_size * 0.7f,
                  .font = FontType::Icons,
                  .size_from_text = true,
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
                      .font = FontType::Heading3,
                      .size_from_text = true,
                      .parent_dictates_hot_and_active = true,
                      .layout {
                          .margins = {.b = k_picker_spacing / 2},
                      },
                      .tooltip = options.folder ? TooltipString {"Folder"_s} : k_nullopt,
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
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Start,
                     },
                 });
}

static void DoFolderFilterAndChildren(GuiBoxSystem& box_system,
                                      CommonPickerState& state,
                                      Box const& parent,
                                      u8& indent,
                                      FolderNode const* folder,
                                      FolderFilters const& folder_filters) {

    bool is_selected = false;
    bool is_current = false;
    for (auto f = folder; f; f = f->parent) {
        if (Contains(state.selected_folder_hashes, f->Hash())) {
            is_current = true;
            if (f == folder) is_selected = true;
            break;
        }
    }

    auto this_info = folder_filters.folders.Find(folder);
    ASSERT(this_info);

    auto const button = DoFilterButton(
        box_system,
        state,
        *this_info,
        {
            .parent = parent,
            .is_selected = is_selected,
            .text = folder->display_name.size ? folder->display_name : folder->name,
            .tooltip = folder->display_name.size ? TooltipString {folder->name} : k_nullopt,
            .hashes = state.selected_folder_hashes,
            .clicked_hash = folder->Hash(),
            .filter_mode = state.filter_mode,
            .font_icon = is_current ? FilterButtonOptions::FontIcon(String(ICON_FA_FOLDER_OPEN))
                                    : FilterButtonOptions::FontIcon(String(ICON_FA_FOLDER_CLOSED)),
            .indent = indent,
            .full_width = true,
        });

    if (folder_filters.do_right_click_menu) {
        DoRightClickForBox(box_system,
                           state,
                           button,
                           folder->Hash(),
                           [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                               folder_filters.do_right_click_menu(box_system, menu_state);
                           });
    }

    ++indent;
    for (auto* child = folder->first_child; child; child = child->next)
        DoFolderFilterAndChildren(box_system, state, parent, indent, child, folder_filters);
    --indent;
}

static void DoPickerFolderFilters(GuiBoxSystem& box_system,
                                  PickerPopupContext& context,
                                  Box const& parent,
                                  FolderFilters const& folder_filters,
                                  u8& sections) {
    if (sections) DoModalDivider(box_system, parent, DividerType::Horizontal);
    ++sections;

    auto const section = DoPickerSectionContainer(box_system,
                                                  context.picker_id + SourceLocationHash(),
                                                  context.state,
                                                  {
                                                      .parent = parent,
                                                      .heading = "FOLDER",
                                                      .multiline_contents = false,
                                                      .right_click_menu = folder_filters.do_right_click_menu,
                                                  });

    if (section) {
        for (auto const [root, _] : folder_filters.root_folders) {
            u8 indent = 0;
            DoFolderFilterAndChildren(box_system, context.state, *section, indent, root, folder_filters);
        }
    }
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

    auto const find_library = [&](u64 library_hash) -> Optional<sample_lib::LibraryIdRef> {
        for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries)
            if (lib_hash == library_hash) return lib_id;
        return k_nullopt;
    };

    if (MenuItem(box_system,
                 root,
                 {
                     .text = "Open Containing Folder",
                     .is_selected = false,
                 })) {
        if (auto const lib_id = find_library(menu_state.item_hash)) {
            auto lib = sample_lib_server::FindLibraryRetained(context.sample_library_server, *lib_id);
            DEFER { lib.Release(); };

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
                                                          .multiline_contents = false,
                                                      });

        if (section) {
            for (auto const& [lib_id, lib_info, lib_hash] : library_filters.libraries) {
                auto const is_selected = Contains(context.state.selected_library_hashes, lib_hash);
                ASSERT(lib_id.name.size);
                ASSERT(lib_id.author.size);

                auto const button = DoFilterButton(
                    box_system,
                    context.state,
                    lib_info,
                    FilterButtonOptions {
                        .parent = *section,
                        .is_selected = is_selected,
                        .icon = box_system.imgui.frame_input.graphics_ctx->GetTextureFromImage(({
                            Optional<graphics::ImageID> tex = library_filters.unknown_library_icon;
                            if (auto imgs = LibraryImagesFromLibraryId(library_filters.library_images,
                                                                       box_system.imgui,
                                                                       lib_id,
                                                                       context.sample_library_server,
                                                                       box_system.arena,
                                                                       true);
                                imgs && !imgs->icon_missing) {
                                tex = imgs->icon;
                            }
                            tex;
                        })),
                        .text = lib_id.name,
                        .tooltip = FunctionRef<String()>([&]() -> String {
                            auto lib =
                                sample_lib_server::FindLibraryRetained(context.sample_library_server, lib_id);
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
                        .full_width = true,
                    });

                DoRightClickForBox(
                    box_system,
                    context.state,
                    button,
                    lib_hash,
                    [&](GuiBoxSystem& box_system, RightClickMenuState const& menu_state) {
                        DoLibraryRightClickMenu(box_system, context, menu_state, library_filters);
                    });
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
                auto const is_selected = Contains(context.state.selected_library_author_hashes, author_hash);
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
                auto const is_selected = Contains(context.state.selected_tags_hashes, tag_hash);
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
                    auto const is_selected = Contains(context.state.selected_tags_hashes, Hash(name));
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
        case FilterMode::ProgressiveNarrowing: return "Match all";
        case FilterMode::AdditiveSelection: return "Match any";
        case FilterMode::Count: break;
    }
    PanicIfReached();
}

static String FilterModeDescription(FilterMode mode) {
    switch (mode) {
        case FilterMode::ProgressiveNarrowing: return "AND logic: progressively narrow down the items.";
        case FilterMode::AdditiveSelection: return "OR logic: broaden the list of items.";
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
            dyn::Append(box_system.state->deferred_actions,
                        [&mode = context.state.filter_mode, new_mode = filter_mode]() { mode = new_mode; });
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
                                         .font = FontType::Icons,
                                         .size_from_text = true,
                                         .background_fill_auto_hot_active_overlay = true,
                                         .round_background_corners = 0b1111,
                                         .activate_on_click_button = MouseButton::Left,
                                         .activation_click_event = ActivationClickEvent::Up,
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
                                                      .tooltip = "Select filtering mode",
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

                             if (options.folder_filters)
                                 DoPickerFolderFilters(box_system,
                                                       context,
                                                       root,
                                                       *options.folder_filters,
                                                       num_lhs_sections);

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

                             if (options.folder_filters)
                                 DoPickerLibraryAuthorFilters(box_system,
                                                              context,
                                                              root,
                                                              *options.library_filters,
                                                              num_lhs_sections);

                             if (options.do_extra_filters)
                                 options.do_extra_filters(box_system, root, num_lhs_sections);
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

            if (options.show_search) {
                auto const search_box =
                    DoBox(box_system,
                          {
                              .parent = rhs,
                              .background_fill = style::Colour::Background2,
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
                          .font_size = k_picker_item_height * 0.9f,
                          .font = FontType::Icons,
                          .text_fill = style::Colour::Subtext0,
                          .size_from_text = true,
                      });

                if (auto const text_input =
                        DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = context.state.search,
                                  .text_input_box = TextInputBox::SingleLine,
                                  .text_input_cursor = style::Colour::Text,
                                  .text_input_selection = style::Colour::Highlight,
                                  .layout {
                                      .size = {layout::k_fill_parent, k_picker_item_height},
                                  },
                              });
                    text_input.text_input_result && text_input.text_input_result->buffer_changed) {
                    dyn::Append(box_system.state->deferred_actions,
                                [&s = context.state.search, new_text = text_input.text_input_result->text]() {
                                    dyn::AssignFitInCapacity(s, new_text);
                                });
                }

                if (context.state.search.size) {
                    if (DoBox(box_system,
                              {
                                  .parent = search_box,
                                  .text = ICON_FA_XMARK,
                                  .font_size = k_picker_item_height * 0.9f,
                                  .font = FontType::Icons,
                                  .text_fill = style::Colour::Subtext0,
                                  .size_from_text = true,
                                  .background_fill_auto_hot_active_overlay = true,
                                  .activate_on_click_button = MouseButton::Left,
                                  .activation_click_event = ActivationClickEvent::Up,
                              })
                            .button_fired) {
                        dyn::Append(box_system.state->deferred_actions,
                                    [&s = context.state.search]() { dyn::Clear(s); });
                    }
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
