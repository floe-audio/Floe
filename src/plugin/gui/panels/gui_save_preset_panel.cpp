// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_save_preset_panel.hpp"

#include "common_infrastructure/tags.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"

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

bool DoTagsGui(GuiBuilder& builder,
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

        auto const category_box = DoBox(builder,
                                        {
                                            .parent = root,
                                            .id_extra = (u64)category,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = k_default_spacing / 3,
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Start,
                                                .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                            },
                                        });

        auto const info = Tags(category);

        {

            auto const heading_box =
                DoBox(builder,
                      {
                          .parent = category_box,
                          .layout {
                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                              .contents_gap = k_default_spacing / 3,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });
            DoBox(builder,
                  {
                      .parent = heading_box,
                      .text = info.font_awesome_icon,
                      .size_from_text = true,
                      .font = FontType::Icons,
                  });

            DoBox(builder,
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

        auto const tags_list = DoBox(builder,
                                     {
                                         .parent = category_box,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = k_default_spacing / 2.5f,
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

            auto const button = DoBox(
                builder,
                BoxConfig {
                    .parent = tags_list,
                    .id_extra = (u64)tag,
                    .text = tag_info.name,
                    .size_from_text = true,
                    .font = FontType::Body,
                    .text_colours = Col {.c = grey_out ? Col::Overlay2 : Col::Text},
                    .background_fill_colours = Col {.c = is_selected ? Col::Highlight : Col::Background1},
                    .background_fill_auto_hot_active_overlay = true,
                    .round_background_corners = 0b1100,
                    .tooltip = tag_info.description.size ? TooltipString(tag_info.description) : k_nullopt,
                    .button_behaviour = imgui::ButtonConfig {},
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
SavePresetPanel(GuiBuilder& builder, SavePresetPanelContext& context, SavePresetPanelState& state) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(
        builder,
        {
            .parent = root,
            .text =
                "Save the current state of Floe to a preset file. Its name is determined by its file name."_s,
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
            .font = FontType::Body,
        });

    {
        auto const author_box = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = k_default_spacing / 3,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                              .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                          },
                                      });
        DoBox(builder,
              {
                  .parent = author_box,
                  .text = "Author:"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        if (auto const input = TextInput(builder,
                                         author_box,
                                         {
                                             .text = state.metadata.author,
                                             .tooltip = "Creator of this preset"_s,
                                             .size = f32x2 {200, k_font_body_size * 1.3f},
                                         });
            input.result && input.result->buffer_changed) {
            ASSERT(input.result->text.size < 10000);
            dyn::AssignFitInCapacity(state.metadata.author, input.result->text);
        }

        if (IconButton(builder,
                       author_box,
                       ICON_FA_FLOPPY_DISK,
                       "Remember this author"_s,
                       k_font_body_size,
                       k_font_body_size)
                .button_fired) {
            prefs::SetValue(context.prefs, RememberedAuthorPrefsDescriptor(), (String)state.metadata.author);
        }

        if (auto const remembered_name = prefs::GetValue(context.prefs, RememberedAuthorPrefsDescriptor());
            !remembered_name.is_default) {
            if (IconButton(builder,
                           author_box,
                           ICON_FA_FILE_IMPORT,
                           fmt::Format(builder.arena, "Use saved author: {}"_s, remembered_name.value),
                           k_font_body_size,
                           k_font_body_size)
                    .button_fired) {
                dyn::Assign(state.metadata.author, remembered_name.value.Get<String>());
            }
        }
    }

    {
        auto const container = DoBox(builder,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .contents_gap = k_default_spacing / 3,
                                             .contents_direction = layout::Direction::Column,
                                             .contents_align = layout::Alignment::Start,
                                             .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                         },
                                     });
        DoBox(builder,
              {
                  .parent = container,
                  .text = "Description:"_s,
                  .size_from_text = true,
                  .font = FontType::Body,
              });

        if (auto const description_field = TextInput(builder,
                                                     container,
                                                     {
                                                         .text = state.metadata.description,
                                                         .size = f32x2 {layout::k_fill_parent, 60},
                                                         .multiline = true,
                                                     });
            description_field.result && description_field.result->buffer_changed)
            dyn::AssignFitInCapacity(state.metadata.description, description_field.result->text);
    }

    DoTagsGui(builder, state.metadata.tags, root);
}

constexpr u32 k_save_panel_contents_imgui_id = (u32)SourceLocationHash();

static void CommitMetadataToEngine(Engine& engine, SavePresetPanelState const& state) {
    engine.state_metadata = state.metadata;
}

void DoSavePresetPanel(GuiBuilder& builder, SavePresetPanelContext& context, SavePresetPanelState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    if (Exchange(state.scroll_to_start, false)) {
        if (auto w = builder.imgui.FindViewport(k_save_panel_contents_imgui_id))
            builder.imgui.SetYScroll(w, 0.0f);
    }

    DoBoxViewport(
        builder,
        {
            .run =
                [&context, &state](GuiBuilder& builder) {
                    auto const root = DoModalRootBox(builder);

                    DoModalHeader(builder,
                                  {
                                      .parent = root,
                                      .title = "Save Preset",
                                      .modeless = &state.modeless,
                                  });

                    DoModalDivider(builder, root, {.horizontal = true});

                    DoBoxViewport(
                        builder,
                        {
                            .run = [&](GuiBuilder& builder) { SavePresetPanel(builder, context, state); },
                            .bounds = DoBox(builder,
                                            {
                                                .parent = root,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                                },
                                            }),
                            .imgui_id = k_save_panel_contents_imgui_id,
                            .viewport_config = k_default_modal_subviewport,
                        });

                    DoModalDivider(builder, root, {.horizontal = true});

                    // bottom buttons
                    auto const button_container =
                        DoBox(builder,
                              {
                                  .parent = root,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                      .contents_padding = {.lrtb = k_default_spacing},
                                      .contents_gap = k_default_spacing,
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::End,
                                  },
                              });

                    if (TextButton(builder,
                                   button_container,
                                   {.text = "Cancel"_s, .tooltip = "Cancel and close"_s}))
                        builder.imgui.CloseTopModal();

                    if (auto const existing_path = context.engine.last_snapshot.name_or_path.Path()) {
                        if (TextButton(
                                builder,
                                button_container,
                                {.text = "Overwrite"_s, .tooltip = "Overwrite the existing preset"_s})) {
                            CommitMetadataToEngine(context.engine, state);
                            SaveCurrentStateToFile(context.engine, *existing_path);
                            if (!state.modeless) builder.imgui.CloseTopModal();
                        }

                        if (TextButton(
                                builder,
                                button_container,
                                {.text = "Save As New"_s, .tooltip = "Save the preset as a new file"_s})) {
                            CommitMetadataToEngine(context.engine, state);
                            OpenFilePickerSavePreset(context.file_picker_state, context.paths);
                            if (!state.modeless) builder.imgui.CloseTopModal();
                        }
                    } else if (TextButton(builder,
                                          button_container,
                                          {.text = "Save"_s, .tooltip = "Save the preset to a new file"_s})) {
                        CommitMetadataToEngine(context.engine, state);
                        OpenFilePickerSavePreset(context.file_picker_state, context.paths);
                        if (!state.modeless) builder.imgui.CloseTopModal();
                    }
                },
            .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                WwToPixels(f32x2 {640, 600})),
            .imgui_id = state.k_panel_id,
            .viewport_config = ({
                auto cfg = k_default_modal_viewport;
                if (state.modeless) cfg.draw_background = DrawOverlayViewportBackground;
                cfg.exclusive_focus = !state.modeless;
                cfg.close_on_click_outside = !state.modeless;
                cfg.close_on_escape = !state.modeless;
                cfg;
            }),
        });
}
