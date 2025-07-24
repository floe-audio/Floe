// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_save_preset_panel.hpp"

#include "common_infrastructure/tags.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui_file_picker.hpp"

void OnEngineStateChange(SavePresetPanelState& state, Engine const& engine) {
    state.metadata = engine.state_metadata;
    state.scroll_to_start = true;
}

static prefs::Descriptor RememberedAuthorPrefsDescriptor() {
    prefs::Descriptor const desc {
        .key = "preset-author"_s,
        .value_requirements =
            prefs::Descriptor::StringRequirements {
                .validator =
                    [](String& value) {
                        if (value.size > k_max_preset_author_size) return false;
                        if (!IsValidUtf8(value)) return false;
                        return true;
                    },
            },
        .default_value = "Unknown"_s,
    };
    return desc;
}

bool DoTagsGui(GuiBoxSystem& box_system,
               DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags>& tags,
               Box const& root) {
    Bitset<ToInt(TagType::Count)> selected_tags = {};
    for (auto const tag : tags) {
        for (auto const category : EnumIterator<TagCategory>()) {
            if (category == TagCategory::ReverbType) continue;

            for (auto const& category_tag : Tags(category).tags) {
                if (GetTagInfo(category_tag).name == tag) {
                    selected_tags.Set(ToInt(category_tag));
                    break;
                }
            }
        }
    }

    bool result = false;

    for (auto const category : EnumIterator<TagCategory>()) {
        if (category == TagCategory::ReverbType) continue;

        auto const category_box = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = style::k_spacing / 3,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });

        auto const info = Tags(category);

        {

            auto const heading_box =
                DoBox(box_system,
                      {
                          .parent = category_box,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = style::k_spacing / 3,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });
            DoBox(box_system,
                  {
                      .parent = heading_box,
                      .text = info.font_awesome_icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                  });

            DoBox(box_system,
                  {
                      .parent = heading_box,
                      .text = fmt::FormatInline<k_max_tag_size + 3>("{}:", info.name),
                      .size_from_text = true,
                      .font = FontType::Body,
                      .layout {
                          .line_break = true,
                      },
                  });
        }

        auto const tags_list = DoBox(box_system,
                                     {
                                         .parent = category_box,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = style::k_spacing / 2.5f,
                                             .contents_direction = layout::Direction::Row,
                                             .contents_multiline = true,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });

        bool category_disallow_more_selection = ShouldGreyOutTagCategory(category, selected_tags);

        for (auto const& tag : info.tags) {
            auto const tag_info = GetTagInfo(tag);
            auto const is_selected = selected_tags.Get(ToInt(tag));

            bool grey_out = category_disallow_more_selection;
            if (is_selected) grey_out = false;

            auto const button =
                DoBox(box_system,
                      BoxConfig {
                          .parent = tags_list,
                          .text = tag_info.name,
                          .size_from_text = true,
                          .font = FontType::Body,
                          .text_colours = {grey_out ? style::Colour::Overlay2 : style::Colour::Text},
                          .background_fill_colours = {is_selected ? style::Colour::Highlight
                                                                  : style::Colour::Background1},
                          .background_fill_auto_hot_active_overlay = true,
                          .round_background_corners = 0b1100,
                          .tooltip = tag_info.description,
                          .button_behaviour = true,
                      });

            if (button.button_fired) {
                result = true;
                if (is_selected)
                    dyn::RemoveValue(tags, tag_info.name);
                else
                    dyn::Append(tags, tag_info.name);
            }
        }
    }

    return result;
}

static void
SavePresetPanel(GuiBoxSystem& box_system, SavePresetPanelContext& context, SavePresetPanelState& state) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(
        box_system,
        {
            .parent = root,
            .text =
                "Save the current state of Floe to a preset file. Its name is determined by its file name."_s,
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
            .font = FontType::Body,
        });

    {
        auto const author_box = DoBox(box_system,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = style::k_spacing / 3,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                          },
                                      });
        DoBox(box_system,
              {
                  .parent = author_box,
                  .text = "Author:"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        if (auto const input = TextInput(box_system,
                                         author_box,
                                         {
                                             .text = state.metadata.author,
                                             .tooltip = "Creator of this preset"_s,
                                             .size = f32x2 {200, style::k_font_body_size * 1.3f},
                                             .type = TextInputBox::SingleLine,
                                         });
            input.text_input_result && input.text_input_result->buffer_changed) {
            ASSERT(input.text_input_result->text.size < 10000);
            dyn::AssignFitInCapacity(state.metadata.author, input.text_input_result->text);
        }

        if (IconButton(box_system,
                       author_box,
                       ICON_FA_FLOPPY_DISK,
                       "Remember this author"_s,
                       style::k_font_body_size,
                       style::k_font_body_size)
                .button_fired) {
            dyn::Append(box_system.state->deferred_actions, [&context, &state]() {
                prefs::SetValue(context.prefs,
                                RememberedAuthorPrefsDescriptor(),
                                (String)state.metadata.author);
            });
        }

        if (auto const remembered_name = prefs::GetValue(context.prefs, RememberedAuthorPrefsDescriptor());
            !remembered_name.is_default) {
            if (IconButton(box_system,
                           author_box,
                           ICON_FA_FILE_IMPORT,
                           fmt::Format(box_system.arena, "Use saved author: {}"_s, remembered_name.value),
                           style::k_font_body_size,
                           style::k_font_body_size)
                    .button_fired) {
                dyn::Assign(state.metadata.author, remembered_name.value.Get<String>());
            }
        }
    }

    {
        auto const container = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = style::k_spacing / 3,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });
        DoBox(box_system,
              {
                  .parent = container,
                  .text = "Description:"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        if (auto const description_field = TextInput(box_system,
                                                     container,
                                                     {
                                                         .text = state.metadata.description,
                                                         .size = f32x2 {layout::k_fill_parent, 60},
                                                         .type = TextInputBox::MultiLine,
                                                     });
            description_field.text_input_result && description_field.text_input_result->buffer_changed)
            dyn::AssignFitInCapacity(state.metadata.description, description_field.text_input_result->text);
    }

    DoTagsGui(box_system, state.metadata.tags, root);
}

constexpr u32 k_save_panel_contents_imgui_id = (u32)SourceLocationHash();

static void CommitMetadataToEngine(Engine& engine, SavePresetPanelState const& state) {
    engine.state_metadata = state.metadata;
}

void DoSavePresetPanel(GuiBoxSystem& box_system,
                       SavePresetPanelContext& context,
                       SavePresetPanelState& state) {
    if (!state.open) return;

    if (Exchange(state.scroll_to_start, false)) {
        if (auto w = box_system.imgui.FindWindow(k_save_panel_contents_imgui_id))
            box_system.imgui.SetYScroll(w, 0.0f);
    }

    RunPanel(
        box_system,
        Panel {
            .run =
                [&context, &state](GuiBoxSystem& box_system) {
                    auto const root = DoModalRootBox(box_system);

                    DoModalHeader(box_system,
                                  {
                                      .parent = root,
                                      .title = "Save Preset",
                                      .on_close = [&state]() { state.open = false; },
                                      .modeless = &state.modeless,
                                  });

                    DoModalDivider(box_system, root, DividerType::Horizontal);

                    AddPanel(
                        box_system,
                        Panel {
                            .run =
                                [&](GuiBoxSystem& box_system) {
                                    SavePresetPanel(box_system, context, state);
                                },
                            .data =
                                Subpanel {
                                    .id =
                                        DoBox(box_system,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                  },
                                              })
                                            .layout_id,
                                    .imgui_id = k_save_panel_contents_imgui_id,
                                },

                        });

                    DoModalDivider(box_system, root, DividerType::Horizontal);

                    // bottom buttons
                    auto const button_container =
                        DoBox(box_system,
                              {
                                  .parent = root,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                      .contents_padding = {.lrtb = style::k_spacing},
                                      .contents_gap = style::k_spacing,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::End,
                                  },
                              });

                    if (TextButton(box_system,
                                   button_container,
                                   {.text = "Cancel"_s, .tooltip = "Cancel and close"_s}))
                        state.open = false;

                    if (auto const existing_path = context.engine.last_snapshot.name_or_path.Path()) {
                        if (TextButton(
                                box_system,
                                button_container,
                                {.text = "Overwrite"_s, .tooltip = "Overwrite the existing preset"_s})) {
                            CommitMetadataToEngine(context.engine, state);
                            SaveCurrentStateToFile(context.engine, *existing_path);
                            state.open = false;
                        }

                        if (TextButton(
                                box_system,
                                button_container,
                                {.text = "Save As New"_s, .tooltip = "Save the preset as a new file"_s})) {
                            CommitMetadataToEngine(context.engine, state);
                            OpenFilePickerSavePreset(context.file_picker_state,
                                                     box_system.imgui.frame_output,
                                                     context.paths);
                            state.open = false;
                        }
                    } else if (TextButton(box_system,
                                          button_container,
                                          {.text = "Save"_s, .tooltip = "Save the preset to a new file"_s})) {
                        CommitMetadataToEngine(context.engine, state);
                        OpenFilePickerSavePreset(context.file_picker_state,
                                                 box_system.imgui.frame_output,
                                                 context.paths);
                        state.open = false;
                    }
                },
            .data =
                ModalPanel {
                    .r = CentredRect(
                        {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                        f32x2 {box_system.imgui.VwToPixels(640), box_system.imgui.VwToPixels(600)}),
                    .imgui_id = box_system.imgui.GetID("save-preset"),
                    .on_close = [&state]() { state.open = false; },
                    .close_on_click_outside = !state.modeless,
                    .darken_background = !state.modeless,
                    .disable_other_interaction = !state.modeless,
                },
        });
}
