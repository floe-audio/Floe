// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "common_infrastructure/state/state_snapshot.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "engine/loop_modes.hpp"
#include "gui/controls/gui_pinned_view_toggle.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/core/gui_waveform_images.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_attribution_panel.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_legacy_params_panel.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/layout.hpp"

static Optional<ImageID> LogoImage(GuiState& g) {
    if (!g.imgui.draw_list->renderer.ImageIdIsValid(g.floe_logo_image)) {
        auto const data = EmbeddedLogoImage();
        if (data.size) {
            auto outcome = DecodeImage({data.data, data.size}, g.scratch_arena);
            ASSERT(!outcome.HasError());
            auto const pixels = outcome.ReleaseValue();
            g.floe_logo_image = CreateImageIdChecked(g.imgui.draw_list->renderer, pixels);
        }
    }
    return g.floe_logo_image;
}

static void DoDotsMenu(GuiState& g) {
    auto const root = DoBox(g.builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    // State
    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Load Blank Preset",
                     .tooltip = "Set all parameters to their default values, clear all Instruments and IRs"_s,
                 })
            .button_fired) {
        SetToDefaultState(g.engine);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Panic (Stop All Sound)",
                     .tooltip = "Stops all audio and clears all playing notes"_s,
                 })
            .button_fired) {
        ResetAudioProcessing(g.engine.processor);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Rescan Libraries & Presets",
                     .tooltip = "Force a full rescan of all sample library and preset folders"_s,
                 })
            .button_fired) {
        sample_lib_server::RescanAllFolders(g.shared_engine_systems.sample_library_server);
        RescanAllFolders(g.shared_engine_systems.preset_server);
        InvalidateAllLibraryImages(g.library_images, g.imgui.draw_list->renderer);
        InvalidateAllWaveformImages(g.waveform_images, g.imgui.draw_list->renderer);
    }

    MenuDivider(g.builder, root);

    // Windows
    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Preferences",
                     .tooltip = "Open the Preferences window"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.preferences_panel_state.k_panel_id);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Performance Controls",
                     .tooltip = "Open the Performance Controls window"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.performance_controls_panel_state.k_panel_id);
    }

    {
        auto const info_item = MenuItem(g.builder,
                                        root,
                                        {
                                            .text = "Info",
                                            .tooltip = "Open the info window"_s,
                                        });
        if (info_item.button_fired) g.imgui.OpenModalViewport(g.info_panel_state.k_panel_id);

        if (g.show_new_version_indicator) {
            if (auto const r = BoxRect(g.builder, info_item)) {
                f32 const radius = 3.5f;
                f32x2 const centre {r->Right() - 10, r->CentreY()};
                g.imgui.draw_list->AddCircleFilled(g.imgui.ViewportPosToWindowPos(centre),
                                                   radius,
                                                   ToU32({.c = Col::Red}));
            }
        }
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Share Feedback",
                     .tooltip = "Open the feedback panel to share your thoughts about Floe"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.feedback_panel_state.k_panel_id);
    }

    MenuDivider(g.builder, root);

    // Advanced
    if (MenuItem(
            g.builder,
            root,
            {
                .text = "Legacy Parameters",
                .tooltip =
                    "Open the legacy parameters window to edit parameters that are not shown in the main UI"_s,
            })
            .button_fired) {
        g.imgui.OpenModalViewport(k_legacy_params_panel_id);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Library Developer Tools",
                     .tooltip = "Open the developer panel for tools to help develop libraries"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.library_dev_panel_state.k_panel_id);
    }

    if constexpr (!IS_LINUX) {
        if (MenuItem(g.builder,
                     root,
                     {
                         .text = "Add Mirage Folders",
                         .tooltip = "Add sample library/preset folders from Mirage if needed"_s,
                     })
                .button_fired) {
            g.shared_engine_systems.AddMirageFoldersIfNeeded();
        }
    }

    MenuDivider(g.builder, root);

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Help",
                     .tooltip = "Open Floe's documentation website"_s,
                 })
            .button_fired) {
        OpenUrlInBrowser("https://floe.audio/docs/getting-started/quick-start-guide");
    }
}

static void DoTopPanel(GuiBuilder& builder, GuiState& g, GuiFrameContext const& frame_context) {
    auto const root_size = PixelsToWw(builder.imgui.CurrentVpSize());
    auto root = DoBox(builder,
                      {
                          .background_fill_colours = Col {.c = Col::Background0, .dark_mode = true},
                          .layout {
                              .size = root_size,
                              .contents_padding = {.lr = k_default_spacing},
                              .contents_gap = k_default_spacing,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                          .name = "top-panel"_s,
                      });

    // Scales the size keeping the aspect ratio, so that it fits within the given height.
    auto scale_size_to_fit_height = [&](f32x2 size, f32 height) {
        return f32x2 {size.x * (height / size.y), height};
    };

    auto const logo_image = LogoImage(g);
    if (logo_image && All(logo_image->size.ToFloat2() > f32x2(0)))
        DoBox(builder,
              {
                  .parent = root,
                  .background_tex = logo_image.NullableValue(),
                  .layout {
                      .size = scale_size_to_fit_height(logo_image->size.ToFloat2(), root_size.y * 0.5f),
                  },
              });

    DoBox(builder,
          {
              .parent = root,
              .text = fmt::Format(builder.arena,
                                  "v" FLOE_VERSION_STRING "  {}",
                                  prefs::GetBool(g.engine.shared_engine_systems.prefs,
                                                 SettingDescriptor(GuiPreference::ShowInstanceName))
                                      ? String {InstanceId(g.engine.autosave_state)}
                                      : ""_s),
              .size_from_text = true,
              .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
          });

    auto const do_icon_button = [&](Box parent,
                                    String icon,
                                    String tooltip,
                                    f32 font_scale,
                                    f32 padding_x,
                                    Col colour = {.c = Col::Subtext1, .dark_mode = true},
                                    u64 id_extra = SourceLocationHash(),
                                    bool disabled = false,
                                    TooltipString value_popup = k_nullopt) {
        // We use a wrapper so that the interactable area is larger and touches the adjacent buttons.
        auto const button = DoBox(builder,
                                  {
                                      .parent = parent,
                                      .id_extra = id_extra,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_padding = {.lr = padding_x, .tb = 3},
                                      },
                                      .value_popup = value_popup,
                                      .tooltip = tooltip,
                                      .button_behaviour = imgui::ButtonConfig {},
                                  });
        DoBox(builder,
              {
                  .parent = button,
                  .text = icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = k_font_icons_size * font_scale,
                  .text_colours =
                      ColSet {
                          .base = colour,
                          .hot = disabled ? colour : Col {.c = Col::Highlight},
                          .active = disabled ? colour : Col {.c = Col::Highlight},
                      },
                  .parent_dictates_hot_and_active = true,
              });
        return button;
    };

    {
        auto preset_box = DoBox(builder,
                                {
                                    .parent = root,
                                    .background_fill_colours = Col {.c = Col::Surface0, .dark_mode = true},
                                    .round_background_corners = 0b1111,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.l = 7, .r = 4, .tb = 2},
                                        .contents_direction = layout::Direction::Row,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                    },
                                });

        // Don't allow multi-line description to overflow.
        bool pop_clip_rect = false;
        if (auto const r = BoxRect(g.builder, preset_box)) {
            g.imgui.draw_list->PushClipRect(g.imgui.ViewportRectToWindowRect(*r));
            pop_clip_rect = true;
        }
        DEFER {
            if (pop_clip_rect) g.imgui.draw_list->PopClipRect();
        };

        auto preset_box_left = DoBox(
            builder,
            {
                .parent = preset_box,
                .layout {
                    .size = {layout::k_fill_parent, k_font_body_size + k_font_body_italic_size},
                    .contents_direction = layout::Direction::Column,
                },
                .tooltip =
                    "Open the Preset Browser, where you can choose from all the presets installed.\n\nThis area shows the name of the current preset, with a short description underneath. If you've changed anything since loading it, the name is marked as '(modified)'. Tip: the COMPARE toggle on the PERFORM page lets you flick between the original preset and your modified version."_s,
                .button_behaviour = imgui::ButtonConfig {},
            });

        if (preset_box_left.button_fired) {
            g.imgui.OpenModalViewport(g.preset_browser_state.k_panel_id);
            g.preset_browser_state.common_state.absolute_button_rect =
                g.imgui.ViewportRectToWindowRect(*BoxRect(builder, preset_box_left));
        }
        if (preset_box_left.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);

        DoRightClickMenu(g, preset_box_left, builder.imgui.MakeId("PresetRClick"), [&](Box root) {
            if (MenuItem(
                    g.builder,
                    root,
                    {
                        .text = "Load Blank Preset",
                        .tooltip =
                            "Set all parameters to their default values, clear all Instruments and IRs"_s,
                        .no_icon_gap = true,
                    })
                    .button_fired) {
                SetToDefaultState(g.engine);
            }

            String const preset_path = g.engine.pinned_snapshot.preset_path;
            auto const containing_folder = preset_path.size ? path::Directory(preset_path) : k_nullopt;
            if (MenuItem(g.builder,
                         root,
                         {
                             .text = "Open Containing Folder",
                             .mode = containing_folder ? MenuItemOptions::Mode::Active
                                                       : MenuItemOptions::Mode::Disabled,
                             .no_icon_gap = true,
                         })
                    .button_fired) {
                if (containing_folder) OpenFolderInFileBrowser(*containing_folder);
            }
        });

        DoBox(builder,
              {
                  .parent = preset_box_left,
                  .text = fmt::Format(builder.arena,
                                      "{}{}",
                                      g.engine.pinned_snapshot.state.extras.display_name,
                                      StateModifiedFromPinned(g.engine) ? " (modified)"_s : ""_s),
                  .text_colours =
                      ColSet {
                          .base {.c = Col::Text, .dark_mode = true},
                          .hot {.c = Col::Highlight},
                          .active {.c = Col::Highlight},
                      },
                  .parent_dictates_hot_and_active = true,
                  .layout {
                      .size = {layout::k_fill_parent, k_font_body_size},
                  },
              });

        {
            // IMPROVE: should this be a text input that changes the description?
            auto const& snapshot = g.engine.pinned_snapshot;
            auto const seed = Hash(snapshot.state.extras.display_name);
            auto const folder = PinnedPresetFolderName(g.engine);

            Array<AutoDescriptionLayerInfo, k_num_layers> layer_info {};
            for (auto const i : Range(k_num_layers)) {
                auto& layer = g.engine.processor.layer_processors[i];
                layer_info[i].inst_name = layer.InstName();
                auto const desired_loop_mode =
                    g.engine.processor.main_params.IntValue<param_values::LoopMode>(
                        layer.index,
                        LayerParamIndex::LoopMode);
                layer_info[i].actual_loop_behaviour =
                    ActualLoopBehaviour(layer.instrument,
                                        desired_loop_mode,
                                        layer.VolumeEnvelopeIsOn(g.engine.processor.main_params));
            }

            auto const write_auto = [&](AutoDescriptionForm form) {
                return WriteAutoDescription(builder.arena,
                                            snapshot.state,
                                            layer_info,
                                            {.form = form, .random_seed = seed, .folder_name = folder});
            };
            AutoDescriptionTexts const auto_texts {
                .headline = write_auto(AutoDescriptionForm::Headline),
                .full_block = write_auto(AutoDescriptionForm::FullBlock),
                .detail = write_auto(AutoDescriptionForm::Detail),
            };
            auto const* italic_font = builder.fonts.atlas[ToInt(FontType::BodyItalic)];
            auto const display = SplitPresetDescriptionForDisplay(snapshot.state.metadata.description,
                                                                  auto_texts,
                                                                  *italic_font,
                                                                  g.top_panel_description_width);
            g.preset_description_display = display;
            String const desc_text = display.kind == LongDescriptionKind::UserContinued
                                         ? (String)fmt::Format(builder.arena, "{}…", display.top_text)
                                         : display.top_text;
            auto const desc_box = DoBox(builder,
                                        {
                                            .parent = preset_box_left,
                                            .text = desc_text,
                                            .font = FontType::BodyItalic,
                                            .text_colours =
                                                ColSet {
                                                    .base {.c = Col::Subtext0, .dark_mode = true},
                                                    .hot {.c = Col::Subtext1, .dark_mode = true},
                                                    .active {.c = Col::Subtext1, .dark_mode = true},
                                                },
                                            .text_overflow = TextOverflowType::ShowDotsOnRight,
                                            .parent_dictates_hot_and_active = true,
                                            .layout {
                                                .size = {layout::k_fill_parent, k_font_body_italic_size},
                                            },
                                        });
            if (auto const r = BoxRect(builder, desc_box)) g.top_panel_description_width = r->w;
        }

        {
            auto const preset_next = do_icon_button(
                preset_box,
                ICON_FA_CARET_LEFT,
                "Step to the previous preset. A quick way to audition sounds without opening the browser.\n\n" PRESET_BROWSER_FILTERS_TOOLTIP_NOTE
                ""_s,
                1.0f,
                3);
            if (preset_next.button_fired) {
                PresetBrowserContext context {
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .preset_server = g.shared_engine_systems.preset_server,
                    .library_images = g.library_images,
                    .prefs = g.prefs,
                    .engine = g.engine,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                context.Init(g.scratch_arena);
                DEFER { context.Deinit(); };

                LoadAdjacentPreset(context, g.preset_browser_state, SearchDirection::Backward);
            }
            if (preset_next.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
        }

        {
            auto const preset_prev = do_icon_button(
                preset_box,
                ICON_FA_CARET_RIGHT,
                "Step to the next preset. A quick way to audition sounds without opening the browser.\n\n" PRESET_BROWSER_FILTERS_TOOLTIP_NOTE
                ""_s,
                1.0f,
                3);
            if (preset_prev.button_fired) {
                PresetBrowserContext context {
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .preset_server = g.shared_engine_systems.preset_server,
                    .library_images = g.library_images,
                    .prefs = g.prefs,
                    .engine = g.engine,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                context.Init(g.scratch_arena);
                DEFER { context.Deinit(); };

                LoadAdjacentPreset(context, g.preset_browser_state, SearchDirection::Forward);
            }
            if (preset_prev.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
        }

        {
            auto const preset_random = do_icon_button(
                preset_box,
                ICON_FA_SHUFFLE,
                "Jump to a random preset. A quick way to stumble upon sounds you might not have picked yourself.\n\n" PRESET_BROWSER_FILTERS_TOOLTIP_NOTE
                ""_s,
                0.9f,
                3);
            if (preset_random.button_fired) {
                PresetBrowserContext context {
                    .sample_library_server = g.shared_engine_systems.sample_library_server,
                    .preset_server = g.shared_engine_systems.preset_server,
                    .library_images = g.library_images,
                    .prefs = g.prefs,
                    .engine = g.engine,
                    .notifications = g.notifications,
                    .persistent_store = g.shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g.confirmation_dialog_state,
                    .frame_context = frame_context,
                };
                context.Init(g.scratch_arena);
                DEFER { context.Deinit(); };

                LoadRandomPreset(context, g.preset_browser_state);
            }
            if (preset_random.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);
        }

        {
            auto const preset_save = do_icon_button(
                preset_box,
                ICON_FA_FLOPPY_DISK,
                "Open the save panel.\n\nFrom there you can set the preset's name, tags, description and other details, then either overwrite the existing preset or save it as a new file."_s,
                0.8f,
                3);
            if (preset_save.button_fired) g.imgui.OpenModalViewport(g.save_preset_panel_state.k_panel_id);
        }

        {
            auto const preset_load = do_icon_button(
                preset_box,
                ICON_FA_FILE_IMPORT,
                "Open a file browser to load a preset file from anywhere on your computer. The file doesn't need to be in one of Floe's preset folders."_s,
                0.8f,
                3);
            if (preset_load.button_fired)
                OpenFilePickerLoadPreset(g.file_picker_state,
                                         g.shared_engine_systems.paths,
                                         g.shared_engine_systems.persistent_store);
        }
    }

    auto right_icon_buttons_container = DoBox(builder,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = layout::k_hug_contents,
                                                  },
                                              });
    DoExperimentalModeIndicatorIfNeeded(builder, right_icon_buttons_container, g.prefs);

    // preferences
    {
        auto const prefs_button = do_icon_button(
            right_icon_buttons_container,
            ICON_FA_GEAR,
            "Open the Preferences window.\n\nPreferences are settings for Floe itself rather than for your sound: how the interface looks, which folders Floe scans for libraries and presets, and where you install packages of sample libraries and presets. They're saved on your computer and apply to every instance of Floe."_s,
            0.9f,
            5);
        if (prefs_button.button_fired) g.imgui.OpenModalViewport(g.preferences_panel_state.k_panel_id);
    }

    // performance controls
    {
        auto const perf_config_button = do_icon_button(
            right_icon_buttons_container,
            ICON_FA_GAUGE,
            "Open the Performance Controls window.\n\nPerformance Controls shape how you play Floe: mostly MIDI settings, plus options for making performances exactly reproducible. They're saved with this instance of Floe in your DAW project. Loading a preset never changes them, so you can set up your MIDI controls once and flick through presets freely."_s,
            0.9f,
            5);
        if (perf_config_button.button_fired)
            g.imgui.OpenModalViewport(g.performance_controls_panel_state.k_panel_id);
    }

    {
        auto const can_undo = g.engine.undo_history.CanUndo();
        auto const next = g.engine.undo_history.NextUndoName();
        auto const value_popup =
            next ? (String)fmt::Format(builder.arena, "Undo: {}", *next) : "Nothing to undo"_s;
        auto const undo_button = do_icon_button(
            right_icon_buttons_container,
            ICON_FA_ARROW_ROTATE_LEFT,
            fmt::Format(
                builder.arena,
                "Undo your most recent change.\n\nFloe keeps a history of changes to its sound, including parameter tweaks and loading Instruments or effects, going back up to {} steps. That means you can experiment freely and step back at any point..",
                k_undo_max_entries),
            0.9f,
            5,
            Col {.c = Col::Subtext1, .dark_mode = true, .alpha = can_undo ? (u8)255 : (u8)60},
            SourceLocationHash(),
            !can_undo,
            value_popup);
        if (undo_button.button_fired && can_undo) Undo(g.engine);
    }

    {
        auto const can_redo = g.engine.undo_history.CanRedo();
        auto const next = g.engine.undo_history.NextRedoName();
        auto const value_popup =
            next ? (String)fmt::Format(builder.arena, "Redo: {}", *next) : "Nothing to redo"_s;
        auto const redo_button = do_icon_button(
            right_icon_buttons_container,
            ICON_FA_ARROW_ROTATE_RIGHT,
            "Redo the change you just undid.\n\nRedo is only available after using undo. If you make a new change instead, the redo history is cleared."_s,
            0.9f,
            5,
            Col {.c = Col::Subtext1, .dark_mode = true, .alpha = can_redo ? (u8)255 : (u8)60},
            SourceLocationHash(),
            !can_redo,
            value_popup);
        if (redo_button.button_fired && can_redo) Redo(g.engine);
    }

    // attribution
    if (g.engine.attribution_requirements.formatted_text.size) {
        auto const attribution_button = do_icon_button(right_icon_buttons_container,
                                                       ICON_FA_FILE_SIGNATURE,
                                                       "Open attribution requirements"_s,
                                                       0.9f,
                                                       5,
                                                       Col {.c = Col::Red});
        if (attribution_button.button_fired) g.imgui.OpenModalViewport(AttributionPanelContext::k_panel_id);
    }

    // dots menu
    {
        bool const screenshot_update_indicator = IsScreenshotRequest("update-indicator"_s);
        if (screenshot_update_indicator) {
            check_for_update::State::PaddedVersion const fake {.version = Version {9, 9, 9}};
            g.shared_engine_systems.check_for_update_state.checking_allowed.Store(true,
                                                                                  StoreMemoryOrder::Release);
            g.shared_engine_systems.check_for_update_state.latest_version.Store(fake,
                                                                                StoreMemoryOrder::Release);
            g.show_new_version_indicator = true;
        }

        auto const dots_button = do_icon_button(right_icon_buttons_container,
                                                ICON_FA_ELLIPSIS_VERTICAL,
                                                "Open the Main Menu, with more options and information."_s,
                                                1.0f,
                                                6);
        if (g.show_new_version_indicator) {
            DoBox(builder,
                  {
                      .parent = dots_button,
                      .background_fill_colours = Col {.c = Col::Red},
                      .background_shape = BackgroundShape::Circle,
                      .layout {
                          .size = 7,
                      },
                  });
        }

        auto const popup_id = builder.imgui.MakeId("DotsMenu");
        if (dots_button.button_fired) builder.imgui.OpenPopupMenu(popup_id, dots_button.imgui_id);

        if (builder.imgui.IsPopupMenuOpen(popup_id))
            DoBoxViewport(builder,
                          {
                              .run = [&g](GuiBuilder&) { DoDotsMenu(g); },
                              .bounds = dots_button,
                              .imgui_id = popup_id,
                              .viewport_config = k_default_popup_menu_viewport,
                          });

        if (screenshot_update_indicator) {
            if (auto const r = BoxRect(g.builder, dots_button))
                g.imgui.RegisterNamedRect("top-panel.dots-button"_s, g.imgui.ViewportRectToWindowRect(*r));
        }
    }

    auto const knob_container = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = layout::k_hug_contents,
                                              .contents_gap = 15,
                                              .contents_direction = layout::Direction::Row,
                                          },
                                      });

    {
        bool const has_insts_with_timbre_layers = ({
            bool r = false;
            for (auto const& layer : g.engine.processor.layer_processors) {
                if (layer.UsesTimbreLayering()) {
                    r = true;
                    break;
                }
            }
            r;
        });
        auto const timbre_param = g.engine.processor.main_params.DescribedValue(ParamIndex::MasterTimbre);
        auto const box = DoKnobParameter(
            g,
            knob_container,
            timbre_param,
            {
                .width = k_small_knob_width,
                .greyed_out = !has_insts_with_timbre_layers,
                .is_fake = !has_insts_with_timbre_layers,
                .override_tooltip =
                    has_insts_with_timbre_layers
                        ? ""_s
                        : "Timbre is inactive because none of the loaded instruments have crossfade layers.\n\nSome instruments are made from several layers of samples, such as soft-to-hard or dark-to-bright variations of the same sound. When one of those is loaded, this knob sweeps between its layers so you can shape the tone. Instruments that respond to it are highlighted while you drag the knob."_s,
                .override_value_popup = has_insts_with_timbre_layers ? ""_s : "Inactive"_s,
                .voice_blips_01 =
                    VoiceBlips01(g, k_nullopt, param_values::MpeDestination::Timbre, timbre_param),
            });

        g.timbre_slider_is_held = box.is_active;

        if (builder.imgui.WasJustActivated(box.imgui_id, MouseButton::Left))
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }

    DoKnobParameter(g,
                    knob_container,
                    g.engine.processor.main_params.DescribedValue(ParamIndex::MasterVolume),
                    {
                        .width = k_small_knob_width,
                    });

    auto const meter_box = DoBox(builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_hug_contents, 37},
                                         .contents_gap = 8,
                                         .contents_direction = layout::Direction::Row,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    // peak meter
    {
        auto const options = DrawPeakMeterOptions {
            .flash_when_clipping = true,
            .show_min_max_markers = true,
            .min_db = -36,
            .max_db = 6,
            .marker_interval_db = 6,
            .low_signal_threshold_db = -60.0f,
        };
        auto const peak_meter_box = DoBox(
            builder,
            {
                .parent = meter_box,
                .layout {
                    .size = {k_peak_meter_standard_width, layout::k_fill_parent},
                },
                .value_popup = FunctionRef<String()> {[&]() -> String {
                    return PeakMeterTooltipText(builder.arena, g.engine.processor.peak_meter, options)
                        .value_popup;
                }},
                .tooltip = FunctionRef<String()> {[&]() -> String {
                    return fmt::Format(
                        builder.arena,
                        "Level of the audio leaving Floe, measured after the Master Volume. The meter flashes red if the signal clips above 0 dB.\n\n{}",
                        PeakMeterTooltipText(builder.arena, g.engine.processor.peak_meter, options).tooltip);
                }},
            });
        if (auto const viewport_r = BoxRect(builder, peak_meter_box))
            DrawPeakMeter(g.imgui,
                          builder.imgui.RegisterAndConvertRect(*viewport_r),
                          &g.engine.processor.peak_meter,
                          options);
    }

    // loudness meter
    if (prefs::GetBool(g.engine.shared_engine_systems.prefs,
                       SettingDescriptor(GuiPreference::ShowLufsMeter))) {
        constexpr f32 k_loudness_target_lufs = -22.0f;
        constexpr f32 k_loudness_target_tolerance_lu = 1.0f;
        constexpr f32 k_loudness_readout_width = 40;

        auto const snapshot = g.engine.processor.lufs_meter.GetSnapshot();
        auto const loudness_options = DrawLoudnessMeterOptions {
            .short_term_lufs = snapshot.short_term_lufs,
            .momentary_lufs = snapshot.momentary_lufs,
            .target_min_lufs = k_loudness_target_lufs - k_loudness_target_tolerance_lu,
            .target_max_lufs = k_loudness_target_lufs + k_loudness_target_tolerance_lu,
        };

        auto const container = DoBox(
            builder,
            {
                .parent = meter_box,
                .layout {
                    .size = {layout::k_hug_contents, layout::k_fill_parent},
                    .contents_gap = 4,
                    .contents_direction = layout::Direction::Row,
                },
                .value_popup = FunctionRef<String()> {[&]() -> String {
                    return LoudnessMeterTooltipText(builder.arena, loudness_options).value_popup;
                }},
                .tooltip = FunctionRef<String()> {[&]() -> String {
                    return fmt::Format(
                        builder.arena,
                        "Perceived loudness of the audio leaving Floe, measured in LUFS after the Master Volume. This reflects how loud the sound actually feels rather than its highest sample values.\n\nThe coloured bar shows short-term loudness (S, the last 3 seconds) and the white marker line shows momentary loudness (M, the last 400 ms).\n\nThe green region is a rough guide of a sensible level for Floe to be sending into your mix.\n\n{}",
                        LoudnessMeterTooltipText(builder.arena, loudness_options).tooltip);
                }},
            });

        if (auto const viewport_r = BoxRect(builder,
                                            DoBox(builder,
                                                  {
                                                      .parent = container,
                                                      .layout {
                                                          .size = {11, layout::k_fill_parent},
                                                      },
                                                  })))
            DrawLoudnessMeter(g.imgui, builder.imgui.RegisterAndConvertRect(*viewport_r), loudness_options);

        constexpr f32 k_readout_font_size = k_font_body_size * 0.81f;

        // Fixed width: the readouts change every frame while playing and a hugging column would jitter the
        // whole row.
        auto const readout_box = DoBox(builder,
                                       {
                                           .parent = container,
                                           .layout {
                                               .size = {k_loudness_readout_width, layout::k_fill_parent},
                                               .contents_direction = layout::Direction::Column,
                                               .contents_align = layout::Alignment::Middle,
                                           },
                                       });

        DoBox(builder,
              {
                  .parent = readout_box,
                  .text = "LUFS"_s,
                  .font_size = k_readout_font_size,
                  .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
                  .text_justification = TextJustification::CentredLeft,
                  .layout {.size = {layout::k_fill_parent, k_readout_font_size}},
              });

        auto const readout_row = DoBox(builder,
                                       {
                                           .parent = readout_box,
                                           .layout {
                                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                                               .contents_direction = layout::Direction::Row,
                                               .contents_align = layout::Alignment::Middle,
                                           },
                                       });

        auto const prefix_column = DoBox(builder,
                                         {
                                             .parent = readout_row,
                                             .layout {
                                                 .size = {layout::k_hug_contents, layout::k_hug_contents},
                                                 .contents_direction = layout::Direction::Column,
                                                 .contents_align = layout::Alignment::Middle,
                                             },
                                         });

        auto const value_column = DoBox(builder,
                                        {
                                            .parent = readout_row,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_direction = layout::Direction::Column,
                                                .contents_align = layout::Alignment::Middle,
                                            },
                                        });

        auto const do_readout = [&](u64 index, String prefix, f32 lufs) {
            DoBox(builder,
                  {
                      .parent = prefix_column,
                      .id_extra = index,
                      .text = prefix,
                      .font_size = k_readout_font_size,
                      .text_colours = Col {.c = Col::Overlay2, .dark_mode = true},
                      .layout {.size = {k_readout_font_size, k_readout_font_size}},
                  });
            DoBox(builder,
                  {
                      .parent = value_column,
                      .id_extra = index,
                      .text = lufs <= LufsMeter::k_silence_floor_lufs
                                  ? String {"-∞"}
                                  : String {fmt::Format(builder.arena, "{.1}", lufs)},
                      .font_size = k_readout_font_size,
                      .text_colours = Col {.c = Col::Subtext0, .dark_mode = true},
                      .text_justification = TextJustification::CentredLeft,
                      .layout {.size = {layout::k_fill_parent, k_readout_font_size}},
                  });
        };
        do_readout(0, "M"_s, snapshot.momentary_lufs);
        do_readout(1, "S"_s, snapshot.short_term_lufs);
    }
}

void TopPanel(GuiState& g, Rect bounds, GuiFrameContext const& frame_context) {
    DoBoxViewport(g.builder,
                  {
                      .run = [&](GuiBuilder& builder) { DoTopPanel(builder, g, frame_context); },
                      .bounds = bounds,
                      .imgui_id = g.imgui.MakeId("TopPanel"),
                      .viewport_config = ({
                          auto cfg = k_default_modal_subviewport;
                          cfg.scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never;
                          cfg;
                      }),
                  });
}
