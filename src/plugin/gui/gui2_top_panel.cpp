// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/engine.hpp"
#include "gui.hpp"
#include "gui/gui2_common_modal_panel.hpp"
#include "gui/gui2_inst_picker.hpp"
#include "gui/gui2_ir_picker.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "gui_modal_windows.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_helpers.hpp"

static void DoDotsMenu(Gui* g) {
    auto const root = DoBox(g->box_system,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Reset State",
                     .tooltip = "Set all parameters to their default values, clear all instruments and IRs"_s,
                 })) {
        SetToDefaultState(g->engine);
    }

    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Randomise All Parameters",
                     .tooltip = "Randomise all parameters and load random instruments and IRs"_s,
                 })) {
        RandomiseAllParameterValues(g->engine.processor);
        for (auto& layer : g->engine.processor.layer_processors) {
            InstPickerContext context {
                .layer = layer,
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };
            LoadRandomInstrument(context, g->inst_picker_state[layer.index], false);
        }
        {
            IrPickerContext ir_context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            ir_context.Init(g->scratch_arena);
            DEFER { ir_context.Deinit(); };
            LoadRandomIr(ir_context, g->ir_picker_state);
        }
    }

    if (MenuItem(
            g->box_system,
            root,
            {
                .text = "Legacy Parameters",
                .tooltip =
                    "Open the legacy parameters window to edit parameters that are not shown in the main UI"_s,
            })) {
        g->legacy_params_window_open = true;
    }

    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Share Feedback",
                     .tooltip = "Open the feedback panel to share your thoughts about Floe"_s,
                 })) {
        g->feedback_panel_state.open = true;
    }

    if (MenuItem(g->box_system,
                 root,
                 {
                     .text = "Library Developer Panel",
                     .tooltip = "Open the developer panel for tools to help develop libraries"_s,
                 })) {
        g->library_dev_panel_state.open = true;
    }
}

static void DoTopPanel(GuiBoxSystem& box_system, Gui* g) {
    auto const root_size = box_system.imgui.PixelsToVw(box_system.imgui.Size());
    auto root = DoBox(box_system,
                      {
                          .background_fill_colours = {style::Colour::DarkModeBackground0},
                          .layout {
                              .size = root_size,
                              .contents_padding = {.lr = style::k_spacing},
                              .contents_gap = style::k_spacing,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                      });

    // Scales the size keeping the aspect ratio, so that it fits within the given height.
    auto scale_size_to_fit_height = [&](f32x2 size, f32 height) {
        return f32x2 {size.x * (height / size.y), height};
    };

    auto live_size = [&](UiSizeId id) { return box_system.imgui.PixelsToVw(LiveSize(box_system.imgui, id)); };

    auto const logo_image = LogoImage(g);
    DoBox(box_system,
          {
              .parent = root,
              .background_tex = box_system.imgui.graphics->context->GetTextureFromImage(logo_image),
              .layout {
                  .size = scale_size_to_fit_height(logo_image->size.ToFloat2(), root_size.y * 0.5f),
              },
          });

    DoBox(box_system,
          {
              .parent = root,
              .text = fmt::Format(box_system.arena,
                                  "v" FLOE_VERSION_STRING "  {}",
                                  prefs::GetBool(g->engine.shared_engine_systems.prefs,
                                                 SettingDescriptor(GuiSetting::ShowInstanceName))
                                      ? String {InstanceId(g->engine.autosave_state)}
                                      : ""_s),
              .size_from_text = true,
              .text_colours = {style::Colour::DarkModeSubtext0},
          });

    auto preset_box = DoBox(box_system,
                            {
                                .parent = root,
                                .background_fill_colours = {style::Colour::DarkModeSurface0},
                                .round_background_corners = 0b1111,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_padding = {.l = 7, .r = 4, .tb = 2},
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    auto preset_box_left = DoBox(
        box_system,
        {
            .parent = preset_box,
            .layout {
                .size = {layout::k_fill_parent, layout::k_hug_contents},
                .contents_direction = layout::Direction::Column,
            },
            .tooltip = FunctionRef<String()> {[&arena = box_system.arena, &engine = g->engine]() -> String {
                DynamicArray<char> buffer {arena};
                dyn::Assign(buffer, "Open presets window"_s);
                fmt::Append(buffer, "\nCurrent preset: {}", engine.last_snapshot.name_or_path.Name());
                if (engine.last_snapshot.state.metadata.description.size) {
                    dyn::AppendSpan(buffer, "\n\n"_s);
                    dyn::AppendSpan(buffer, engine.last_snapshot.state.metadata.description);
                }
                return buffer.ToOwnedSpan();
            }},
            .behaviour = Behaviour::Button,
        });

    if (preset_box_left.button_fired) {
        g->preset_picker_state.common_state.open = true;
        g->preset_picker_state.common_state.absolute_button_rect =
            g->imgui.WindowRectToScreenRect(*BoxRect(box_system, preset_box_left));
    }
    if (preset_box_left.is_hot) StartScanningIfNeeded(g->engine.shared_engine_systems.preset_server);

    DoBox(box_system,
          {
              .parent = preset_box_left,
              .text = fmt::Format(box_system.arena,
                                  "{}{}",
                                  g->engine.last_snapshot.name_or_path.Name(),
                                  StateChangedSinceLastSnapshot(g->engine) ? " (modified)"_s : ""_s),
              .text_colours =
                  {
                      .base = style::Colour::DarkModeText,
                      .hot = style::Colour::Highlight,
                      .active = style::Colour::Highlight,
                  },
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {layout::k_fill_parent, style::k_font_body_size},
              },
          });

    // IMPROVE: should this be a text input that changes the description?
    DoBox(box_system,
          {
              .parent = preset_box_left,
              .text = g->engine.last_snapshot.state.metadata.description.size
                          ? (String)g->engine.last_snapshot.state.metadata.description
                          : "No description"_s,
              .font = FontType::BodyItalic,
              .text_colours {
                  .base = style::Colour::DarkModeSubtext0,
                  .hot = style::Colour::DarkModeSubtext1,
                  .active = style::Colour::DarkModeSubtext1,
              },
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {layout::k_fill_parent, style::k_font_body_italic_size},
              },
          });

    auto const do_icon_button = [&](Box parent,
                                    String icon,
                                    String tooltip,
                                    f32 font_scale,
                                    f32 padding_x,
                                    style::Colour colour = style::Colour::DarkModeSubtext1) {
        // We use a wrapper so that the interactable area is larger and touches the adjacent buttons.
        auto const button = DoBox(box_system,
                                  {
                                      .parent = parent,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_padding = {.lr = padding_x, .tb = 3},
                                      },
                                      .tooltip = tooltip,
                                      .behaviour = Behaviour::Button,
                                  });
        DoBox(box_system,
              {
                  .parent = button,
                  .text = icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = style::k_font_icons_size * font_scale,
                  .text_colours {
                      .base = colour,
                      .hot = style::Colour::Highlight,
                      .active = style::Colour::Highlight,
                  },
                  .parent_dictates_hot_and_active = true,
              });
        return button;
    };

    {
        auto const preset_next =
            do_icon_button(preset_box, ICON_FA_CARET_LEFT, "Load previous preset"_s, 1.0f, 3);
        if (preset_next.button_fired) {
            PresetPickerContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .preset_server = g->shared_engine_systems.preset_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };

            LoadAdjacentPreset(context, g->preset_picker_state, SearchDirection::Backward);
        }
        if (preset_next.is_hot) StartScanningIfNeeded(g->engine.shared_engine_systems.preset_server);
    }

    {
        auto const preset_prev =
            do_icon_button(preset_box, ICON_FA_CARET_RIGHT, "Load next preset"_s, 1.0f, 3);
        if (preset_prev.button_fired) {
            PresetPickerContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .preset_server = g->shared_engine_systems.preset_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };

            LoadAdjacentPreset(context, g->preset_picker_state, SearchDirection::Forward);
        }
        if (preset_prev.is_hot) StartScanningIfNeeded(g->engine.shared_engine_systems.preset_server);
    }

    {
        auto const preset_random =
            do_icon_button(preset_box, ICON_FA_SHUFFLE, "Load a random preset"_s, 0.9f, 3);
        if (preset_random.button_fired) {
            PresetPickerContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .preset_server = g->shared_engine_systems.preset_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };

            LoadRandomPreset(context, g->preset_picker_state);
        }
        if (preset_random.is_hot) StartScanningIfNeeded(g->engine.shared_engine_systems.preset_server);
    }

    {
        auto const preset_save =
            do_icon_button(preset_box, ICON_FA_FLOPPY_DISK, "Save the current state as a preset"_s, 0.8f, 3);
        if (preset_save.button_fired) g->save_preset_panel_state.open = true;
    }

    {
        auto const preset_load =
            do_icon_button(preset_box, ICON_FA_FILE_IMPORT, "Load a preset from a file"_s, 0.8f, 3);
        if (preset_load.button_fired) g->preset_picker_state.common_state.open = true;
    }

    auto right_icon_buttons_container = DoBox(box_system,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = layout::k_hug_contents,
                                                  },
                                              });
    // preferences
    {
        auto const prefs_button =
            do_icon_button(right_icon_buttons_container, ICON_FA_GEAR, "Open preferences window"_s, 0.9f, 5);
        if (prefs_button.button_fired) g->preferences_panel_state.open = true;
    }

    // info
    {
        auto const info_button =
            do_icon_button(right_icon_buttons_container, ICON_FA_CIRCLE_INFO, "Open info window"_s, 0.9f, 5);
        if (info_button.button_fired) g->info_panel_state.open = true;

        if (g->show_new_version_indicator) {
            DoBox(box_system,
                  {
                      .parent = info_button,
                      .background_fill_colours {style::Colour::Red},
                      .background_shape = BackgroundShape::Circle,
                      .layout {
                          .size = 7,
                      },
                  });
        }
    }

    // attribution
    if (g->engine.attribution_requirements.formatted_text.size) {
        auto const attribution_button = do_icon_button(right_icon_buttons_container,
                                                       ICON_FA_FILE_SIGNATURE,
                                                       "Open attribution requirements"_s,
                                                       0.9f,
                                                       5,
                                                       style::Colour::Red);
        if (attribution_button.button_fired) g->attribution_panel_open = true;
    }

    // dots menu
    {
        auto const dots_button = do_icon_button(right_icon_buttons_container,
                                                ICON_FA_ELLIPSIS_VERTICAL,
                                                "Additional functions and information"_s,
                                                1.0f,
                                                6);
        auto const popup_id = box_system.imgui.GetID("DotsMenu");
        if (dots_button.button_fired) box_system.imgui.OpenPopup(popup_id, dots_button.imgui_id);

        if (box_system.imgui.IsPopupOpen(popup_id))
            AddPanel(box_system,
                     Panel {
                         .run = [g](GuiBoxSystem&) { DoDotsMenu(g); },
                         .data =
                             PopupPanel {
                                 .creator_layout_id = dots_button.layout_id,
                                 .popup_imgui_id = popup_id,
                             },
                     });
    }

    auto const knob_container = DoBox(box_system,
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
            for (auto const& layer : g->engine.processor.layer_processors) {
                if (layer.UsesTimbreLayering()) {
                    r = true;
                    break;
                }
            }
            r;
        });
        auto const box = DoParameterComponent(
            g,
            knob_container,
            g->engine.processor.main_params.DescribedValue(ParamIndex::MasterTimbre),
            {
                .greyed_out = !has_insts_with_timbre_layers,
                .is_fake = !has_insts_with_timbre_layers,
                .override_tooltip =
                    has_insts_with_timbre_layers
                        ? ""_s
                        : "Timbre: no currently loaded instruments have timbre information; this knob is inactive"_s,
            });

        g->timbre_slider_is_held = box.is_active;

        if (box_system.imgui.WasJustActivated(box.imgui_id))
            box_system.imgui.frame_output.ElevateUpdateRequest(
                GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }

    DoParameterComponent(g,
                         knob_container,
                         g->engine.processor.main_params.DescribedValue(ParamIndex::MasterVolume),
                         {});

    // peak meter
    {
        auto const peak_meter =
            DoBox(box_system,
                  {
                      .parent = root,
                      .layout {
                          .size = {live_size(UiSizeId::Top2PeakMeterW), live_size(UiSizeId::Top2PeakMeterH)},
                      },
                  });
        if (auto const r = BoxRect(box_system, peak_meter))
            peak_meters::PeakMeter(g, *r, g->engine.processor.peak_meter, true);
    }
}

void TopPanel(Gui* g, f32 height) {
    RunPanel(g->box_system,
             {
                 .run = [&](GuiBoxSystem& box_system) { DoTopPanel(box_system, g); },
                 .data =
                     Subpanel {
                         .rect = Rect {.xywh {0, 0, g->imgui.Width(), height}},
                         .imgui_id = g->imgui.GetID("TopPanel"),
                         .flags = imgui::WindowFlags_NoScrollbarX | imgui::WindowFlags_NoScrollbarY,
                     },
             });
}
