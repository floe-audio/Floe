// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/misc.hpp"

#include "common_infrastructure/error_reporting.hpp"

#include "engine/autosave.hpp"
#include "engine/package_installation.hpp"
#include "gui/gui_file_picker.hpp"
#include "gui/gui_prefs.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui2_prefs_panel_state.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "gui_framework/gui_platform.hpp"
#include "processor/processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static void PreferencesLhsTextWidget(GuiBoxSystem& box_system, Box parent, String text) {
    DoBox(box_system,
          {
              .parent = parent,
              .text = text,
              .font = FontType::Body,
              .layout {
                  .size = {style::k_prefs_lhs_width,
                           box_system.imgui.PixelsToVw(box_system.fonts[ToInt(FontType::Body)]->font_size)},
              },
          });
}

static void PreferencesRhsText(GuiBoxSystem& box_system, Box parent, String text) {
    DoBox(box_system,
          {
              .parent = parent,
              .text = text,
              .font = FontType::Body,
              .text_fill = style::Colour::Subtext0,
              .size_from_text = true,
          });
}

static Box PreferencesMenuButton(GuiBoxSystem& box_system, Box parent, String text, String tooltip) {
    auto const button =
        DoBox(box_system,
              {
                  .parent = parent,
                  .background_fill = style::Colour::Background2,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                      .contents_align = layout::Alignment::Justify,
                  },
                  .tooltip = tooltip,
              });

    DoBox(box_system,
          {
              .parent = button,
              .text = text,
              .font = FontType::Body,
              .size_from_text = true,
          });

    DoBox(box_system,
          {
              .parent = button,
              .text = ICON_FA_CARET_DOWN,
              .font = FontType::Icons,
              .size_from_text = true,
          });

    return button;
}

static Box PreferencesRow(GuiBoxSystem& box_system, Box parent) {
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

static Box PreferencesRhsColumn(GuiBoxSystem& box_system, Box parent, f32 gap) {
    return DoBox(box_system,
                 {
                     .parent = parent,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_gap = gap,
                         .contents_direction = layout::Direction::Column,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

struct FolderSelectorResult {
    bool delete_pressed;
    bool open_pressed;
};

static FolderSelectorResult
PreferencesFolderSelector(GuiBoxSystem& box_system, Box parent, String path, String subtext, bool deletable) {
    auto const container = DoBox(box_system,
                                 {
                                     .parent = parent,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = style::k_prefs_small_gap,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

    auto const path_container =
        DoBox(box_system,
              {
                  .parent = container,
                  .background_fill = style::Colour::Background1,
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = style::k_button_padding_x, .tb = style::k_button_padding_y},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Justify,
                  },
              });
    DoBox(box_system,
          {
              .parent = path_container,
              .text = path,
              .font = FontType::Body,
              .size_from_text = true,
          });
    auto const icon_button_container = DoBox(box_system,
                                             {
                                                 .parent = path_container,
                                                 .layout {
                                                     .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                     .contents_gap = style::k_prefs_small_gap,
                                                     .contents_direction = layout::Direction::Row,
                                                 },
                                             });

    FolderSelectorResult result {};
    if (deletable) {
        result.delete_pressed = DoBox(box_system,
                                      {
                                          .parent = icon_button_container,
                                          .text = ICON_FA_TRASH_ALT,
                                          .font = FontType::Icons,
                                          .text_fill = style::Colour::Subtext0,
                                          .text_fill_hot = style::Colour::Subtext0,
                                          .text_fill_active = style::Colour::Subtext0,
                                          .size_from_text = true,
                                          .background_fill_auto_hot_active_overlay = true,
                                          .round_background_corners = 0b1111,
                                          .activate_on_click_button = MouseButton::Left,
                                          .activation_click_event = ActivationClickEvent::Up,
                                          .extra_margin_for_mouse_events = 2,
                                          .tooltip = "Stop scanning this folder"_s,
                                      })
                                    .button_fired;
    }
    result.open_pressed =
        DoBox(box_system,
              {
                  .parent = icon_button_container,
                  .text = ICON_FA_EXTERNAL_LINK_ALT,
                  .font = FontType::Icons,
                  .text_fill = style::Colour::Subtext0,
                  .text_fill_hot = style::Colour::Subtext0,
                  .text_fill_active = style::Colour::Subtext0,
                  .size_from_text = true,
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .activate_on_click_button = MouseButton::Left,
                  .activation_click_event = ActivationClickEvent::Up,
                  .extra_margin_for_mouse_events = 2,
                  .tooltip = fmt::FormatInline<64>("Open folder in {}"_s, GetFileBrowserAppName()),
              })
            .button_fired;

    if (subtext.size) PreferencesRhsText(box_system, container, subtext);

    return result;
}

struct PreferencesPanelContext {
    prefs::Preferences& prefs;
    FloePaths const& paths;
    sample_lib_server::Server& sample_lib_server;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
    FilePickerState& file_picker_state;
};

static void SetFolderSubtext(DynamicArrayBounded<char, 200>& out,
                             String dir,
                             bool is_default,
                             ScanFolderType type,
                             sample_lib_server::Server& server) {
    dyn::Clear(out);
    switch (type) {
        case ScanFolderType::Libraries: {
            u32 num_libs = 0;
            for (auto& l_node : server.libraries) {
                if (auto l = l_node.TryScoped()) {
                    if (path::IsWithinDirectory(l->lib->path, dir)) ++num_libs;
                }
            }

            if (is_default) dyn::AppendSpan(out, "Default. ");
            dyn::AppendSpan(out, "Contains ");
            if (num_libs < 1000 && out.size + 4 < out.Capacity())
                out.size += fmt::IntToString(num_libs, out.data + out.size);
            else if (num_libs)
                dyn::AppendSpan(out, "many");
            else
                dyn::AppendSpan(out, "no"_s);
            fmt::Append(out, " sample librar{}", num_libs == 1 ? "y" : "ies");
            break;
        }
        case ScanFolderType::Presets: {
            if (is_default) dyn::AppendSpan(out, "Default.");
            // IMPROVE: show number of presets in folder
            break;
        }
        case ScanFolderType::Count: break;
    }
}

static void FolderPreferencesPanel(GuiBoxSystem& box_system, PreferencesPanelContext& context) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_prefs_large_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : EnumIterator<ScanFolderType>()) {
        auto const row = PreferencesRow(box_system, root);
        PreferencesLhsTextWidget(box_system, row, ({
                                     String s;
                                     switch ((ScanFolderType)scan_folder_type) {
                                         case ScanFolderType::Libraries: s = "Sample library folders"; break;
                                         case ScanFolderType::Presets: s = "Preset folders"; break;
                                         case ScanFolderType::Count: PanicIfReached();
                                     }
                                     s;
                                 }));

        auto const rhs_column = PreferencesRhsColumn(box_system, row, style::k_prefs_medium_gap);

        DynamicArrayBounded<char, 200> subtext_buffer {};

        {
            auto const dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
            SetFolderSubtext(subtext_buffer,
                             dir,
                             true,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = PreferencesFolderSelector(box_system, rhs_column, dir, subtext_buffer, false);
                o.open_pressed)
                OpenFolderInFileBrowser(dir);
        }

        Optional<String> to_remove {};
        for (auto const dir : ExtraScanFolders(context.paths, context.prefs, scan_folder_type)) {
            SetFolderSubtext(subtext_buffer,
                             dir,
                             false,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server);
            if (auto const o = PreferencesFolderSelector(box_system, rhs_column, dir, subtext_buffer, true);
                o.open_pressed || o.delete_pressed) {
                if (o.open_pressed) OpenFolderInFileBrowser(dir);
                if (o.delete_pressed) to_remove = dir;
            }
        }
        if (to_remove)
            prefs::RemoveValue(context.prefs,
                               ExtraScanFolderDescriptor(context.paths, scan_folder_type).key,
                               *to_remove);

        auto const contents_name = ({
            String s;
            switch ((ScanFolderType)scan_folder_type) {
                case ScanFolderType::Libraries: s = "sample libraries"; break;
                case ScanFolderType::Presets: s = "presets"; break;
                case ScanFolderType::Count: PanicIfReached();
            }
            s;
        });
        if (ExtraScanFolders(context.paths, context.prefs, scan_folder_type).size !=
                k_max_extra_scan_folders &&
            TextButton(box_system,
                       rhs_column,
                       "Add folder",
                       fmt::FormatInline<100>("Add a folder to scan for {}", contents_name))) {
            OpenFilePickerAddExtraScanFolders(
                context.file_picker_state,
                box_system.imgui.frame_output,
                context.prefs,
                context.paths,
                {.type = (ScanFolderType)scan_folder_type, .set_as_install_folder = false});
        }
    }
}

static void InstallLocationMenu(GuiBoxSystem& box_system,
                                PreferencesPanelContext& context,
                                ScanFolderType scan_folder_type) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DynamicArrayBounded<char, 200> subtext_buffer {};

    auto const menu_item = [&](String path, String subtext) {
        auto const item = DoBox(box_system,
                                {
                                    .parent = root,
                                    .background_fill_auto_hot_active_overlay = true,
                                    .activate_on_click_button = MouseButton::Left,
                                    .activation_click_event = ActivationClickEvent::Up,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_direction = layout::Direction::Row,
                                    },
                                });

        if (item.button_fired) {
            prefs::SetValue(context.prefs,
                            InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type),
                            path);
            box_system.imgui.CloseTopPopupOnly();
        }

        auto const current_install_location =
            prefs::GetString(context.prefs,
                             InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type));

        DoBox(box_system,
              {
                  .parent = item,
                  .text = path::Equal(path, current_install_location) ? String(ICON_FA_CHECK) : "",
                  .font = FontType::Icons,
                  .text_fill = style::Colour::Subtext0,
                  .layout {
                      .size = style::k_prefs_icon_button_size,
                      .margins {.l = style::k_menu_item_padding_x},
                  },

              });

        auto const text_container = DoBox(box_system,
                                          {
                                              .parent = item,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_padding = {.lr = style::k_menu_item_padding_x,
                                                                       .tb = style::k_menu_item_padding_y},
                                                  .contents_direction = layout::Direction::Column,
                                                  .contents_align = layout::Alignment::Start,
                                                  .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                              },
                                          });
        DoBox(box_system,
              {
                  .parent = text_container,
                  .text = path,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
        DoBox(box_system,
              {
                  .parent = text_container,
                  .text = subtext,
                  .text_fill = style::Colour::Subtext0,
                  .size_from_text = true,
              });
    };

    {
        auto const dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
        SetFolderSubtext(subtext_buffer, dir, true, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    for (auto const dir : ExtraScanFolders(context.paths, context.prefs, scan_folder_type)) {
        SetFolderSubtext(subtext_buffer, dir, false, scan_folder_type, context.sample_lib_server);
        menu_item(dir, subtext_buffer);
    }

    DoBox(box_system,
          {
              .parent = root,
              .background_fill = style::Colour::Overlay0,
              .layout {
                  .size = {layout::k_fill_parent, box_system.imgui.PixelsToVw(1)},
                  .margins {.tb = style::k_menu_item_padding_y},
              },
          });

    auto const add_button = DoBox(box_system,
                                  {
                                      .parent = root,
                                      .background_fill_auto_hot_active_overlay = true,
                                      .activate_on_click_button = MouseButton::Left,
                                      .activation_click_event = ActivationClickEvent::Up,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_padding = {.l = (style::k_menu_item_padding_x * 2) +
                                                                    style::k_prefs_icon_button_size,
                                                               .r = style::k_menu_item_padding_x,
                                                               .tb = style::k_menu_item_padding_y},
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                      .tooltip = "Select a new folder"_s,
                                  });
    DoBox(box_system,
          {
              .parent = add_button,
              .text = "Add folder",
              .size_from_text = true,
          });

    if (add_button.button_fired) {
        OpenFilePickerAddExtraScanFolders(context.file_picker_state,
                                          box_system.imgui.frame_output,
                                          context.prefs,
                                          context.paths,
                                          {.type = scan_folder_type, .set_as_install_folder = true});
        box_system.imgui.CloseTopPopupOnly();
    }
}

static void PackagesPreferencesPanel(GuiBoxSystem& box_system, PreferencesPanelContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_prefs_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : EnumIterator<ScanFolderType>()) {
        auto const row = PreferencesRow(box_system, root);
        PreferencesLhsTextWidget(box_system, row, ({
                                     String s;
                                     switch ((ScanFolderType)scan_folder_type) {
                                         case ScanFolderType::Libraries:
                                             s = "Sample library install folder";
                                             break;
                                         case ScanFolderType::Presets: s = "Preset install folder"; break;
                                         case ScanFolderType::Count: PanicIfReached();
                                     }
                                     s;
                                 }));

        auto const popup_id = box_system.imgui.GetID(ToInt(scan_folder_type));

        String menu_text =
            prefs::GetString(context.prefs,
                             InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type));
        if (auto const default_dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
            menu_text == default_dir) {
            menu_text = "Default";
        }

        auto const btn = PreferencesMenuButton(box_system, row, menu_text, "Select install location");
        if (btn.button_fired) box_system.imgui.OpenPopup(popup_id, btn.imgui_id);

        AddPanel(box_system,
                 Panel {
                     .run =
                         [scan_folder_type, &context](GuiBoxSystem& box_system) {
                             InstallLocationMenu(box_system, context, (ScanFolderType)scan_folder_type);
                         },
                     .data =
                         PopupPanel {
                             .creator_layout_id = btn.layout_id,
                             .popup_imgui_id = popup_id,
                         },
                 });
    }

    {
        auto const row = PreferencesRow(box_system, root);
        PreferencesLhsTextWidget(box_system, row, "Install");
        auto const rhs = PreferencesRhsColumn(box_system, row, style::k_prefs_small_gap);
        PreferencesRhsText(box_system, rhs, "Install libraries and presets from a ZIP file");
        if (!context.package_install_jobs.Full() &&
            TextButton(box_system, rhs, "Install package", "Install libraries and presets from a ZIP file")) {
            OpenFilePickerInstallPackage(context.file_picker_state, box_system.imgui.frame_output);
        }
    }
}

static void Setting(GuiBoxSystem& box_system,
                    PreferencesPanelContext& context,
                    Box parent,
                    prefs::Descriptor const& info) {
    switch (info.value_requirements.tag) {
        case prefs::ValueType::Int: {
            auto const& int_info = info.value_requirements.Get<prefs::Descriptor::IntRequirements>();
            if (auto const v = IntField(box_system,
                                        parent,
                                        info.gui_label,
                                        30.0f,
                                        prefs::GetValue(context.prefs, info).value.Get<s64>(),
                                        [&int_info](s64 value) {
                                            if (int_info.validator) int_info.validator(value);
                                            return value;
                                        })) {
                prefs::SetValue(context.prefs, info, *v);
            }
            break;
        }
        case prefs::ValueType::Bool: {
            auto const state = prefs::GetValue(context.prefs, info).value.Get<bool>();
            if (CheckboxButton(box_system, parent, info.gui_label, state, info.long_description))
                prefs::SetValue(context.prefs, info, !state);
            break;
        }
        case prefs::ValueType::String: {
            Panic("not implemented");
            break;
        }
    }
}

constexpr s64 AlignTo(s64 value, s64 alignment) {
    s64 remainder = value % alignment;
    if (remainder == 0) return value;
    return value + (alignment - remainder);
}

static void GeneralPreferencesPanel(GuiBoxSystem& box_system, PreferencesPanelContext& context) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_prefs_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const row = PreferencesRow(box_system, root);

        PreferencesLhsTextWidget(box_system, row, "GUI size");

        auto const button_container = DoBox(box_system,
                                            {
                                                .parent = row,
                                                .background_fill = style::Colour::Background2,
                                                .round_background_corners = 0b1111,
                                                .layout {
                                                    .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                    .contents_direction = layout::Direction::Row,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

        Optional<int> width_change {};

        if (DoBox(box_system,
                  {
                      .parent = button_container,
                      .text = ICON_FA_CARET_DOWN,
                      .font = FontType::Icons,
                      .text_align_x = TextAlignX::Centre,
                      .text_align_y = TextAlignY::Centre,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b1001,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout {
                          .size = style::k_prefs_icon_button_size,
                      },
                      .tooltip = "Decrease GUI size"_s,
                  })
                .button_fired) {
            width_change = -1;
        }
        DoBox(box_system,
              {
                  .parent = button_container,
                  .background_fill = style::Colour::Surface2,
                  .layout {
                      .size = {box_system.imgui.PixelsToVw(1), layout::k_fill_parent},
                  },
              });
        if (DoBox(box_system,
                  {
                      .parent = button_container,
                      .text = ICON_FA_CARET_UP,
                      .font = FontType::Icons,
                      .text_align_x = TextAlignX::Centre,
                      .text_align_y = TextAlignY::Centre,
                      .background_fill_auto_hot_active_overlay = true,
                      .round_background_corners = 0b0110,
                      .activate_on_click_button = MouseButton::Left,
                      .activation_click_event = ActivationClickEvent::Up,
                      .layout {
                          .size = style::k_prefs_icon_button_size,
                      },
                      .tooltip = "Increase GUI size"_s,
                  })
                .button_fired) {
            width_change = 1;
        }

        if (width_change) {
            auto const desc = SettingDescriptor(GuiSetting::WindowWidth);
            auto const width = prefs::GetInt(context.prefs, desc);
            auto const new_width = width + (*width_change * AlignTo(100, k_aspect_ratio_with_keyboard.width));
            prefs::SetValue(context.prefs, desc, new_width);
        }
    }

    {
        auto const style_row = PreferencesRow(box_system, root);

        PreferencesLhsTextWidget(box_system, style_row, "Style");
        auto const options_rhs_column = PreferencesRhsColumn(box_system, style_row, style::k_prefs_small_gap);

        for (auto const gui_setting : EnumIterator<GuiSetting>()) {
            if (gui_setting == GuiSetting::WindowWidth) continue;
            Setting(box_system, context, options_rhs_column, SettingDescriptor(gui_setting));
        }
    }

    {
        auto const misc_row = PreferencesRow(box_system, root);

        PreferencesLhsTextWidget(box_system, misc_row, "General");
        auto const options_rhs_column = PreferencesRhsColumn(box_system, misc_row, style::k_prefs_small_gap);

        Setting(box_system, context, options_rhs_column, IsOnlineReportingDisabledDescriptor());
        Setting(box_system,
                context,
                options_rhs_column,
                SettingDescriptor(ProcessorSetting::DefaultCcParamMappings));

        for (auto const autosave_setting : EnumIterator<AutosaveSetting>())
            Setting(box_system, context, options_rhs_column, SettingDescriptor(autosave_setting));
    }
}

static void
PreferencesPanel(GuiBoxSystem& box_system, PreferencesPanelContext& context, PreferencesPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(PreferencesPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<PreferencesPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case PreferencesPanelState::Tab::General:
                    tabs[index] = {.icon = ICON_FA_SLIDERS_H, .text = "General"};
                    break;
                case PreferencesPanelState::Tab::Folders:
                    tabs[index] = {.icon = ICON_FA_FOLDER_OPEN, .text = "Folders"};
                    break;
                case PreferencesPanelState::Tab::Packages:
                    tabs[index] = {.icon = ICON_FA_BOX_OPEN, .text = "Packages"};
                    break;
                case PreferencesPanelState::Tab::Count: PanicIfReached();
            }
        }
        return tabs;
    }();

    auto const root = DoModal(box_system,
                              {
                                  .title = "Preferences"_s,
                                  .on_close = [&state] { state.open = false; },
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBoxSystem&, PreferencesPanelContext&);
    AddPanel(box_system,
             Panel {
                 .run = ({
                     TabPanelFunction f {};
                     switch (state.tab) {
                         case PreferencesPanelState::Tab::General: f = GeneralPreferencesPanel; break;
                         case PreferencesPanelState::Tab::Folders: f = FolderPreferencesPanel; break;
                         case PreferencesPanelState::Tab::Packages: f = PackagesPreferencesPanel; break;
                         case PreferencesPanelState::Tab::Count: PanicIfReached();
                     }
                     [f, &context](GuiBoxSystem& box_system) { f(box_system, context); };
                 }),
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_fill_parent},
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = box_system.imgui.GetID((u64)state.tab + 999999),
                     },
             });
}

PUBLIC void
DoPreferencesPanel(GuiBoxSystem& box_system, PreferencesPanelContext& context, PreferencesPanelState& state) {
    ASSERT(box_system.imgui.Width() > 0);
    if (state.open) {
        RunPanel(box_system,
                 Panel {
                     .run = [&context, &state](GuiBoxSystem& b) { PreferencesPanel(b, context, state); },
                     .data =
                         ModalPanel {
                             .r = CentredRect(
                                 {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                 f32x2 {box_system.imgui.VwToPixels(style::k_prefs_dialog_width),
                                        box_system.imgui.VwToPixels(style::k_prefs_dialog_height)}),
                             .imgui_id = box_system.imgui.GetID("prefs"),
                             .on_close = [&state]() { state.open = false; },
                             .close_on_click_outside = true,
                             .darken_background = true,
                             .disable_other_interaction = true,
                         },
                 });
    }
}
