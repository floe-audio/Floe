// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/misc.hpp"

#include "common_infrastructure/autosave.hpp"
#include "common_infrastructure/error_reporting.hpp"

#include "engine/check_for_update.hpp"
#include "engine/package_installation.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/elements/gui2_modal.hpp"
#include "gui/elements/gui2_popup_menu.hpp"
#include "gui/panels/gui2_prefs_panel_state.hpp"
#include "gui_framework/app_window.hpp"
#include "gui_framework/gui_builder.hpp"
#include "processor/processor.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static void
PreferencesLhsTextWidget(GuiBuilder& builder, Box parent, String text, u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .font = FontType::Body,
              .layout {
                  .size = {190.0f, k_font_body_size},
              },
          });
}

static void
PreferencesRhsText(GuiBuilder& builder, Box parent, String text, u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .size_from_text = true,
              .font = FontType::BodyItalic,
              .text_colours = Col {.c = Col::Subtext0},
          });
}

static Box PreferencesRow(GuiBuilder& builder, Box parent, u64 id_extra = SourceLocationHash()) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                     },
                 });
}

static Box
PreferencesRhsColumn(GuiBuilder& builder, Box parent, f32 gap, u64 id_extra = SourceLocationHash()) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
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

static FolderSelectorResult PreferencesFolderSelector(GuiBuilder& builder,
                                                      Box parent,
                                                      String path,
                                                      String subtext,
                                                      bool deletable,
                                                      u64 id_extra = SourceLocationHash()) {
    auto const container = DoBox(builder,
                                 {
                                     .parent = parent,
                                     .id_extra = id_extra,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_gap = k_small_gap,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                     },
                                 });

    auto const path_container =
        DoBox(builder,
              {
                  .parent = container,
                  .background_fill_colours = Col {.c = Col::Background1},
                  .round_background_corners = 0b1111,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.lr = k_button_padding_x, .tb = k_button_padding_y},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Justify,
                  },
              });

    auto const display_path = path::MakeDisplayPath(path, {.compact_middle_sections = true}, builder.arena);
    DoBox(builder,
          {
              .parent = path_container,
              .text = display_path,
              .size_from_text = true,
              .font = FontType::Body,
              .tooltip = display_path.data == path.data ? TooltipString(k_nullopt) : path,
          });
    auto const icon_button_container = DoBox(builder,
                                             {
                                                 .parent = path_container,
                                                 .layout {
                                                     .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                     .contents_gap = k_small_gap,
                                                     .contents_direction = layout::Direction::Row,
                                                 },
                                             });

    FolderSelectorResult result {};
    if (deletable) {
        result.delete_pressed = DoBox(builder,
                                      {
                                          .parent = icon_button_container,
                                          .text = ICON_FA_TRASH,
                                          .size_from_text = true,
                                          .font = FontType::Icons,
                                          .text_colours = Col {.c = Col::Subtext0},
                                          .background_fill_auto_hot_active_overlay = true,
                                          .round_background_corners = 0b1111,
                                          .tooltip = "Stop scanning this folder"_s,
                                          .button_behaviour = imgui::ButtonConfig {},
                                          .extra_margin_for_mouse_events = 2,
                                      })
                                    .button_fired;
    }
    result.open_pressed =
        DoBox(builder,
              {
                  .parent = icon_button_container,
                  .text = ICON_FA_UP_RIGHT_FROM_SQUARE,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .text_colours = Col {.c = Col::Subtext0},
                  .background_fill_auto_hot_active_overlay = true,
                  .round_background_corners = 0b1111,
                  .tooltip = (String)fmt::FormatInline<64>("Open folder in {}"_s, GetFileBrowserAppName()),
                  .button_behaviour = imgui::ButtonConfig {},
                  .extra_margin_for_mouse_events = 2,
              })
            .button_fired;

    if (subtext.size) PreferencesRhsText(builder, container, subtext);

    return result;
}

struct PreferencesPanelContext {
    prefs::Preferences& prefs;
    FloePaths const& paths;
    sample_lib_server::Server& sample_lib_server;
    package::InstallJobs& package_install_jobs;
    ThreadPool& thread_pool;
    FilePickerState& file_picker_state;
    PresetServer& presets_server;
    Optional<BeginReadFoldersResult> presets {};
};

static void SetFolderSubtext(DynamicArrayBounded<char, 200>& out,
                             String dir,
                             bool is_default,
                             ScanFolderType type,
                             sample_lib_server::Server& server,
                             PresetsSnapshot const& snapshot) {
    dyn::Clear(out);
    switch (type) {
        case ScanFolderType::Libraries: {
            if (is_default) dyn::AppendSpan(out, "Default. ");

            u32 num_libs = 0;
            for (auto& l_node : sample_lib_server::LibrariesList(server)) {
                if (auto l = l_node.TryScoped()) {
                    if (path::IsWithinDirectory(l->lib->path, dir)) ++num_libs;
                }
            }

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
            if (is_default) dyn::AppendSpan(out, "Default. ");

            usize num_presets = 0;
            for (auto const folder : snapshot.folders)
                if (path::Equal(folder->folder->scan_folder, dir))
                    num_presets += folder->folder->presets.size;

            dyn::AppendSpan(out, "Contains ");
            if (num_presets < 10000 && out.size + 5 < out.Capacity())
                out.size += fmt::IntToString(num_presets, out.data + out.size);
            else if (num_presets)
                dyn::AppendSpan(out, "many");
            else
                dyn::AppendSpan(out, "no"_s);
            fmt::Append(out, " preset{}", num_presets == 1 ? "" : "s");
            break;
        }
        case ScanFolderType::Count: break;
    }
}

static void FolderPreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = GuiIo().PixelsToWw(builder.imgui.CurrentVpSize()),
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_large_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : EnumIterator<ScanFolderType>()) {
        builder.imgui.PushId((u64)scan_folder_type);
        DEFER { builder.imgui.PopId(); };
        auto const row = PreferencesRow(builder, root);
        PreferencesLhsTextWidget(builder, row, ({
                                     String s;
                                     switch ((ScanFolderType)scan_folder_type) {
                                         case ScanFolderType::Libraries: s = "Sample library folders"; break;
                                         case ScanFolderType::Presets: s = "Preset folders"; break;
                                         case ScanFolderType::Count: PanicIfReached();
                                     }
                                     s;
                                 }));

        auto const rhs_column = PreferencesRhsColumn(builder, row, k_medium_gap);

        DynamicArrayBounded<char, 200> subtext_buffer {};

        {
            auto const dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
            SetFolderSubtext(subtext_buffer,
                             dir,
                             true,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server,
                             context.presets->snapshot);
            if (auto const o = PreferencesFolderSelector(builder, rhs_column, dir, subtext_buffer, false);
                o.open_pressed)
                OpenFolderInFileBrowser(dir);
        }

        Optional<String> to_remove {};
        for (auto const dir : ExtraScanFolders(context.paths, context.prefs, scan_folder_type)) {
            SetFolderSubtext(subtext_buffer,
                             dir,
                             false,
                             (ScanFolderType)scan_folder_type,
                             context.sample_lib_server,
                             context.presets->snapshot);
            if (auto const o =
                    PreferencesFolderSelector(builder, rhs_column, dir, subtext_buffer, true, Hash(dir));
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
            TextButton(
                builder,
                rhs_column,
                {
                    .text = "Add folder",
                    .tooltip = (String)fmt::FormatInline<100>("Add a folder to scan for {}", contents_name),
                })) {
            OpenFilePickerAddExtraScanFolders(
                context.file_picker_state,
                context.prefs,
                context.paths,
                {.type = (ScanFolderType)scan_folder_type, .set_as_install_folder = false});
        }
    }
}

static void
InstallLocationMenu(GuiBuilder& builder, PreferencesPanelContext& context, ScanFolderType scan_folder_type) {
    sample_lib_server::RequestScanningOfUnscannedFolders(context.sample_lib_server);

    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    DynamicArrayBounded<char, 200> subtext_buffer {};

    auto const current_install_location =
        prefs::GetString(context.prefs,
                         InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type));

    {
        auto const dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
        SetFolderSubtext(subtext_buffer,
                         dir,
                         true,
                         scan_folder_type,
                         context.sample_lib_server,
                         context.presets->snapshot);
        if (MenuItem(builder,
                     root,
                     {
                         .text = dir,
                         .subtext = subtext_buffer,
                         .is_selected = path::Equal(dir, current_install_location),
                     })
                .button_fired) {
            prefs::SetValue(context.prefs,
                            InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type),
                            dir);
        }
    }

    for (auto const dir : ExtraScanFolders(context.paths, context.prefs, scan_folder_type)) {
        SetFolderSubtext(subtext_buffer,
                         dir,
                         false,
                         scan_folder_type,
                         context.sample_lib_server,
                         context.presets->snapshot);
        if (MenuItem(builder,
                     root,
                     {
                         .text = dir,
                         .subtext = subtext_buffer,
                         .is_selected = path::Equal(dir, current_install_location),
                     },
                     Hash(dir))
                .button_fired) {
            prefs::SetValue(context.prefs,
                            InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type),
                            dir);
        }
    }

    DoBox(builder,
          {
              .parent = root,
              .background_fill_colours = Col {.c = Col::Overlay0},
              .layout {
                  .size = {layout::k_fill_parent, GuiIo().PixelsToWw(1.0f)},
                  .margins {.tb = k_menu_item_padding_y},
              },
          });

    auto const add_button =
        DoBox(builder,
              {
                  .parent = root,
                  .background_fill_auto_hot_active_overlay = true,
                  .layout {
                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                      .contents_padding = {.l = (k_menu_item_padding_x * 2) + k_icon_button_size,
                                           .r = k_menu_item_padding_x,
                                           .tb = k_menu_item_padding_y},
                      .contents_direction = layout::Direction::Row,
                      .contents_align = layout::Alignment::Start,
                  },
                  .tooltip = "Select a new folder"_s,
                  .button_behaviour = imgui::ButtonConfig {},
              });
    DoBox(builder,
          {
              .parent = add_button,
              .text = "Add folder",
              .size_from_text = true,
          });

    if (add_button.button_fired) {
        OpenFilePickerAddExtraScanFolders(context.file_picker_state,
                                          context.prefs,
                                          context.paths,
                                          {.type = scan_folder_type, .set_as_install_folder = true});
        builder.imgui.CloseTopPopupOnly();
    }
}

static void PackagesPreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = GuiIo().PixelsToWw(builder.imgui.CurrentVpSize()),
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    for (auto const scan_folder_type : EnumIterator<ScanFolderType>()) {
        builder.imgui.PushId((u64)scan_folder_type);
        DEFER { builder.imgui.PopId(); };

        auto const row = PreferencesRow(builder, root);
        PreferencesLhsTextWidget(builder, row, ({
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

        auto const popup_id = builder.imgui.MakeId(ToInt(scan_folder_type));

        String menu_text =
            prefs::GetString(context.prefs,
                             InstallLocationDescriptor(context.paths, context.prefs, scan_folder_type));
        if (auto const default_dir = context.paths.always_scanned_folder[ToInt(scan_folder_type)];
            menu_text == default_dir) {
            menu_text = "Default";
        } else {
            menu_text = path::MakeDisplayPath(menu_text, {.compact_middle_sections = true}, builder.arena);
        }

        auto const btn = MenuOpenButton(builder,
                                        row,
                                        {
                                            .text = menu_text,
                                            .tooltip = "Select install location"_s,
                                            .width = layout::k_fill_parent,
                                        });
        if (btn.button_fired) builder.imgui.OpenPopupMenu(popup_id, btn.imgui_id);

        if (builder.imgui.IsPopupMenuOpen(popup_id))
            DoBoxViewport(builder,
                          {
                              .run =
                                  [scan_folder_type, &context](GuiBuilder& builder) {
                                      InstallLocationMenu(builder, context, (ScanFolderType)scan_folder_type);
                                  },
                              .bounds = btn,
                              .imgui_id = popup_id,
                              .viewport_config = k_default_popup_menu_viewport,
                          });
    }

    {
        auto const row = PreferencesRow(builder, root);
        PreferencesLhsTextWidget(builder, row, "Install");
        auto const rhs = PreferencesRhsColumn(builder, row, k_small_gap);
        PreferencesRhsText(builder, rhs, "Install libraries and presets from a ZIP file");
        if (!context.package_install_jobs.Full() &&
            TextButton(
                builder,
                rhs,
                {.text = "Install package", .tooltip = "Install libraries and presets from a ZIP file"_s})) {
            OpenFilePickerInstallPackage(context.file_picker_state);
        }
    }
}

constexpr f32 k_settings_int_field_width = 30.0f;

static void
Setting(GuiBuilder& builder, PreferencesPanelContext& context, Box parent, prefs::Descriptor const& info) {
    builder.imgui.PushId(HashKey(info.key));
    DEFER { builder.imgui.PopId(); };

    switch (info.value_requirements.tag) {
        case prefs::ValueType::Int: {
            auto const& int_info = info.value_requirements.Get<prefs::Descriptor::IntRequirements>();
            if (auto const v = IntField(builder,
                                        parent,
                                        {
                                            .label = info.gui_label,
                                            .tooltip = info.long_description,
                                            .width = k_settings_int_field_width,
                                            .value = prefs::GetValue(context.prefs, info).value.Get<s64>(),
                                            .constrainer =
                                                [&int_info](s64 value) {
                                                    if (int_info.validator) int_info.validator(value);
                                                    return value;
                                                },
                                        })) {
                prefs::SetValue(context.prefs, info, *v);
            }
            break;
        }
        case prefs::ValueType::Bool: {
            auto const state = prefs::GetValue(context.prefs, info).value.Get<bool>();
            if (CheckboxButton(builder, parent, info.gui_label, state, info.long_description))
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

// We want a step that is a multiple of the GUI aspect ratio width, but a large enough step so that doing +1
// step feels like a reasonable change.
constexpr u16 k_prefs_window_width_step = []() {
    u16 step = k_gui_aspect_ratio.width;
    while (step < 100)
        step += k_gui_aspect_ratio.width;
    return step;
}();

static void GeneralPreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = GuiIo().PixelsToWw(builder.imgui.CurrentVpSize()),
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_medium_gap,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const style_row = PreferencesRow(builder, root);

        PreferencesLhsTextWidget(builder, style_row, "UI");
        auto const options_rhs_column = PreferencesRhsColumn(builder, style_row, k_small_gap);

        for (auto const gui_setting : EnumIterator<GuiPreference>()) {
            auto const desc = SettingDescriptor(gui_setting);
            if (gui_setting == GuiPreference::WindowWidth) {
                auto const& int_info = desc.value_requirements.Get<prefs::Descriptor::IntRequirements>();
                if (auto const v =
                        IntField(builder,
                                 options_rhs_column,
                                 {
                                     .label = "Window size",
                                     .tooltip = desc.long_description,
                                     .width = k_settings_int_field_width,
                                     .value = prefs::GetValue(context.prefs, desc).value.Get<s64>() /
                                              k_prefs_window_width_step,
                                     .constrainer =
                                         [&int_info](s64 value) {
                                             value *= k_prefs_window_width_step;
                                             if (int_info.validator) int_info.validator(value);
                                             return value;
                                         },
                                 })) {
                    prefs::SetValue(context.prefs, desc, *v);
                }
                continue;
            }
            Setting(builder, context, options_rhs_column, desc);
        }
    }

    {
        auto const misc_row = PreferencesRow(builder, root);

        PreferencesLhsTextWidget(builder, misc_row, "General");
        auto const options_rhs_column = PreferencesRhsColumn(builder, misc_row, k_small_gap);

        Setting(builder, context, options_rhs_column, IsOnlineReportingDisabledDescriptor());
        Setting(builder,
                context,
                options_rhs_column,
                SettingDescriptor(ProcessorSetting::DefaultCcParamMappings));

        for (auto const autosave_setting : EnumIterator<AutosaveSetting>())
            Setting(builder, context, options_rhs_column, SettingDescriptor(autosave_setting));

        Setting(builder, context, options_rhs_column, check_for_update::CheckAllowedPrefDescriptor());
        Setting(builder, context, options_rhs_column, check_for_update::CheckBetaPrefDescriptor());
    }
}

static void
PreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context, PreferencesPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(PreferencesPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<PreferencesPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case PreferencesPanelState::Tab::General:
                    tabs[index] = {.icon = ICON_FA_SLIDERS, .text = "General"};
                    break;
                case PreferencesPanelState::Tab::Folders:
                    tabs[index] = {.icon = ICON_FA_FOLDER_OPEN, .text = "Folders"};
                    break;
                case PreferencesPanelState::Tab::Packages:
                    tabs[index] = {.icon = ICON_FA_BOX_OPEN, .text = "Packages"};
                    break;
                case PreferencesPanelState::Tab::Count: PanicIfReached();
            }
            tabs[index].index = index;
        }
        return tabs;
    }();

    auto const root = DoModal(builder,
                              {
                                  .title = "Preferences"_s,
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBuilder&, PreferencesPanelContext&);
    DoBoxViewport(builder,
                  {
                      .run = ({
                          TabPanelFunction f {};
                          switch (state.tab) {
                              case PreferencesPanelState::Tab::General: f = GeneralPreferencesPanel; break;
                              case PreferencesPanelState::Tab::Folders: f = FolderPreferencesPanel; break;
                              case PreferencesPanelState::Tab::Packages: f = PackagesPreferencesPanel; break;
                              case PreferencesPanelState::Tab::Count: PanicIfReached();
                          }
                          [f, &context](GuiBuilder& builder) { f(builder, context); };
                      }),
                      .bounds = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          },
                                      }),
                      .imgui_id = builder.imgui.MakeId((u64)state.tab + 999999),
                      .viewport_config = k_default_modal_subviewport,
                  });
}

PUBLIC void
DoPreferencesPanel(GuiBuilder& builder, PreferencesPanelContext& context, PreferencesPanelState& state) {
    ASSERT(builder.imgui.CurrentVpWidth() > 0);
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) return;

    bool init = false;
    if (!context.presets) {
        context.presets = BeginReadFolders(context.presets_server, builder.arena);
        init = true;
    }
    DEFER {
        if (init) EndReadFolders(context.presets_server, context.presets->handle);
    };

    DoBoxViewport(builder,
                  {
                      .run = [&context, &state](GuiBuilder& b) { PreferencesPanel(b, context, state); },
                      .bounds = Rect {.pos = 0, .size = GuiIo().in.window_size.ToFloat2()}.CentredRect(
                          GuiIo().WwToPixels(f32x2 {625, 443})),
                      .imgui_id = state.k_panel_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
