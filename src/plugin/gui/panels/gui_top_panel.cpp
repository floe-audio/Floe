// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "common_infrastructure/auto_description.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/core/gui_state.hpp"
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

static void DoDotsMenu(GuiState& g, GuiFrameContext const& frame_context) {
    auto const root = DoBox(g.builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Reset State",
                     .tooltip = "Set all parameters to their default values, clear all instruments and IRs"_s,
                 })
            .button_fired) {
        SetToDefaultState(g.engine);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Reset Audio Engine",
                     .tooltip = "Stops all audio and clears all playing notes"_s,
                 })
            .button_fired) {
        ResetAudioProcessing(g.engine.processor);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "MIDI CC Assignments",
                     .tooltip = "View and manage all MIDI CC-to-parameter assignments"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.midi_cc_panel_state.k_panel_id);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Instance Config",
                     .tooltip = "Configure per-instance settings such as randomisation behaviour"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.instance_config_panel_state.k_panel_id);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Randomise All Parameters",
                     .tooltip = "Randomise all parameters and load random instruments and IRs"_s,
                 })
            .button_fired) {
        RandomiseAllParameterValues(g.engine.processor);
        for (auto& layer : g.engine.processor.layer_processors) {
            InstBrowserContext context {
                .layer = layer,
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadRandomInstrument(context, g.inst_browser_state[layer.index]);
        }
        {
            IrBrowserContext ir_context {
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };
            LoadRandomIr(ir_context, g.ir_browser_state);
        }
    }

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
                     .text = "Share Feedback",
                     .tooltip = "Open the feedback panel to share your thoughts about Floe"_s,
                 })
            .button_fired) {
        g.imgui.OpenModalViewport(g.feedback_panel_state.k_panel_id);
    }

    if (MenuItem(g.builder,
                 root,
                 {
                     .text = "Library Developer Panel",
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
                                    u64 id_extra = SourceLocationHash()) {
        // We use a wrapper so that the interactable area is larger and touches the adjacent buttons.
        auto const button = DoBox(builder,
                                  {
                                      .parent = parent,
                                      .id_extra = id_extra,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_padding = {.lr = padding_x, .tb = 3},
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
                  .font_size = k_font_icons_size * font_scale,
                  .text_colours =
                      ColSet {
                          .base = colour,
                          .hot {.c = Col::Highlight},
                          .active {.c = Col::Highlight},
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
                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                    .contents_direction = layout::Direction::Column,
                },
                .tooltip = FunctionRef<String()> {[&arena = builder.arena, &engine = g.engine]() -> String {
                    DynamicArray<char> buffer {arena};
                    dyn::Assign(buffer, "Open presets window"_s);
                    fmt::Append(buffer, "\nCurrent preset: {}", engine.last_snapshot.name_or_path.Name());
                    if (engine.last_snapshot.state.metadata.description.size) {
                        dyn::AppendSpan(buffer, "\n\n"_s);
                        dyn::AppendSpan(buffer, engine.last_snapshot.state.metadata.description);
                    }
                    return buffer.ToOwnedSpan();
                }},
                .button_behaviour = imgui::ButtonConfig {},
            });

        if (preset_box_left.button_fired) {
            g.imgui.OpenModalViewport(g.preset_browser_state.k_panel_id);
            g.preset_browser_state.common_state.absolute_button_rect =
                g.imgui.ViewportRectToWindowRect(*BoxRect(builder, preset_box_left));
        }
        if (preset_box_left.is_hot) StartScanningIfNeeded(g.engine.shared_engine_systems.preset_server);

        DoBox(builder,
              {
                  .parent = preset_box_left,
                  .text = fmt::Format(builder.arena,
                                      "{}{}",
                                      g.engine.last_snapshot.name_or_path.Name(),
                                      StateChangedSinceLastSnapshot(g.engine) ? " (modified)"_s : ""_s),
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

        auto const has_desc = g.engine.last_snapshot.state.metadata.description.size;
        auto const auto_desc = has_desc ? AutoDescriptionString {} : AutoDescription(g.engine);

        // IMPROVE: should this be a text input that changes the description?
        DoBox(builder,
              {
                  .parent = preset_box_left,
                  .text = has_desc ? (String)g.engine.last_snapshot.state.metadata.description
                                   : (String)auto_desc,
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

        {
            auto const preset_next =
                do_icon_button(preset_box,
                               ICON_FA_CARET_LEFT,
                               "Load previous preset\n\nThis is based on the currently selected filters."_s,
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
            auto const preset_prev =
                do_icon_button(preset_box,
                               ICON_FA_CARET_RIGHT,
                               "Load next preset\n\nThis is based on the currently selected filters."_s,
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
            auto const preset_random =
                do_icon_button(preset_box,
                               ICON_FA_SHUFFLE,
                               "Load a random preset\n\nThis is based on the currently selected filters."_s,
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
            auto const preset_save = do_icon_button(preset_box,
                                                    ICON_FA_FLOPPY_DISK,
                                                    "Save the current state as a preset"_s,
                                                    0.8f,
                                                    3);
            if (preset_save.button_fired) g.imgui.OpenModalViewport(g.save_preset_panel_state.k_panel_id);
        }

        {
            auto const preset_load =
                do_icon_button(preset_box, ICON_FA_FILE_IMPORT, "Load a preset from a file"_s, 0.8f, 3);
            if (preset_load.button_fired)
                OpenFilePickerLoadPreset(g.file_picker_state, g.shared_engine_systems.paths);
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
        auto const prefs_button =
            do_icon_button(right_icon_buttons_container, ICON_FA_GEAR, "Open preferences window"_s, 0.9f, 5);
        if (prefs_button.button_fired) g.imgui.OpenModalViewport(g.preferences_panel_state.k_panel_id);
    }

    // info
    {
        auto const info_button =
            do_icon_button(right_icon_buttons_container, ICON_FA_CIRCLE_INFO, "Open info window"_s, 0.9f, 5);
        if (info_button.button_fired) g.imgui.OpenModalViewport(g.info_panel_state.k_panel_id);

        if (g.show_new_version_indicator) {
            DoBox(builder,
                  {
                      .parent = info_button,
                      .background_fill_colours = Col {.c = Col::Red},
                      .background_shape = BackgroundShape::Circle,
                      .layout {
                          .size = 7,
                      },
                  });
        }
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
        auto const dots_button = do_icon_button(right_icon_buttons_container,
                                                ICON_FA_ELLIPSIS_VERTICAL,
                                                "Additional functions and information"_s,
                                                1.0f,
                                                6);
        auto const popup_id = builder.imgui.MakeId("DotsMenu");
        if (dots_button.button_fired) builder.imgui.OpenPopupMenu(popup_id, dots_button.imgui_id);

        if (builder.imgui.IsPopupMenuOpen(popup_id))
            DoBoxViewport(builder,
                          {
                              .run = [&g, &frame_context](GuiBuilder&) { DoDotsMenu(g, frame_context); },
                              .bounds = dots_button,
                              .imgui_id = popup_id,
                              .viewport_config = k_default_popup_menu_viewport,
                          });
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
        auto const box = DoKnobParameter(
            g,
            knob_container,
            g.engine.processor.main_params.DescribedValue(ParamIndex::MasterTimbre),
            {
                .width = k_small_knob_width,
                .greyed_out = !has_insts_with_timbre_layers,
                .is_fake = !has_insts_with_timbre_layers,
                .override_tooltip =
                    has_insts_with_timbre_layers
                        ? ""_s
                        : "Timbre: no currently loaded instruments have timbre information; this knob is inactive"_s,
            });

        g.timbre_slider_is_held = box.is_active;
        if (g.timbre_slider_is_held) {
            int b = 0;
            (void)b;
        }

        if (builder.imgui.WasJustActivated(box.imgui_id, MouseButton::Left))
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }

    DoKnobParameter(g,
                    knob_container,
                    g.engine.processor.main_params.DescribedValue(ParamIndex::MasterVolume),
                    {
                        .width = k_small_knob_width,
                    });

    // peak meter
    if (auto const viewport_r = BoxRect(builder,
                                        DoBox(builder,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = {22.0f, 37.06f},
                                                  },
                                              })))
        DrawPeakMeter(g.imgui,
                      builder.imgui.RegisterAndConvertRect(*viewport_r),
                      g.engine.processor.peak_meter,
                      {.flash_when_clipping = true});
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
