// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "common_infrastructure/paths.hpp"
#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_inst_picker.hpp"
#include "gui/gui2_ir_picker.hpp"
#include "gui/gui2_parameter_component.hpp"
#include "gui_button_widgets.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "gui_menu.hpp"
#include "gui_modal_windows.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"

[[maybe_unused]] static void PresetsWindowButton(Gui* g, Engine* a, Rect r) {
    auto button_id = g->imgui.GetID("PresetMenu");

    DynamicArrayBounded<char, 100> preset_text {a->last_snapshot.name_or_path.Name()};
    if (StateChangedSinceLastSnapshot(*a)) dyn::AppendSpan(preset_text, " (modified)"_s);

    if (buttons::Button(g, button_id, r, preset_text, buttons::PresetsPopupButton(g->imgui))) {
        g->preset_picker_state.common_state.open = true;
        g->preset_picker_state.common_state.absolute_button_rect = g->imgui.GetRegisteredAndConvertedRect(r);
    }

    if (g->imgui.IsHot(button_id)) StartScanningIfNeeded(g->shared_engine_systems.preset_server);

    Tooltip(g, button_id, r, "Open presets window"_s);
}

[[maybe_unused]] static void DoDotsMenu(Gui* g) {
    String const longest_string_in_menu = "Randomise All Parameters";
    PopupMenuItems top_menu(g, {&longest_string_in_menu, 1});

    if (top_menu.DoButton("Reset State")) SetToDefaultState(g->engine);
    if (top_menu.DoButton("Randomise All Parameters")) {
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
    if (top_menu.DoButton("Legacy Parameters")) g->legacy_params_window_open = true;
    if (top_menu.DoButton("Share Feedback")) g->feedback_panel_state.open = true;
    if (top_menu.DoButton("Library Developer Panel")) g->library_dev_panel_state.open = true;
}

[[maybe_unused]] static void DoTopPanel(GuiBoxSystem& box_system, Gui* g) {
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
                                .background_fill_colours = {style::Colour::DarkModeSurface1},
                                .round_background_corners = 0b1111,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                    .contents_padding = {.lr = 4},
                                    .contents_direction = layout::Direction::Row,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                },
                            });

    auto preset_box_left = DoBox(box_system,
                                 {
                                     .parent = preset_box,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_direction = layout::Direction::Column,
                                     },
                                     .button_behaviour = true,
                                 });
    if (preset_box_left.button_fired) {
        // TODO
    }

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
              .text_colours = {style::Colour::DarkModeSubtext0},
              .layout {
                  .size = {layout::k_fill_parent, style::k_font_body_italic_size},
              },
          });

    auto const icon_button_colour = style::Colour::DarkModeSubtext0;

    auto const do_icon_button = [&](String icon, String tooltip, f32 font_scale) {
        // We use a wrapper so that the interactable area is larger and touches the adjacent buttons.
        auto const button = DoBox(box_system,
                                  {
                                      .parent = preset_box,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_padding = {.lr = 2},
                                      },
                                      .tooltip = tooltip,
                                      .button_behaviour = true,
                                  });
        DoBox(box_system,
              {
                  .parent = button,
                  .text = icon,
                  .size_from_text = true,
                  .font = FontType::Icons,
                  .font_size = style::k_font_icons_size * font_scale,
                  .text_colours {
                      .base = icon_button_colour,
                      .hot = style::Colour::Highlight,
                      .active = style::Colour::Highlight,
                  },
                  .parent_dictates_hot_and_active = true,
              });
        return button;
    };

    {
        auto const preset_next = do_icon_button(ICON_FA_CARET_LEFT, "Load previous preset"_s, 1.0f);
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
        auto const preset_prev = do_icon_button(ICON_FA_CARET_RIGHT, "Load next preset"_s, 1.0f);
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
        auto const preset_random = do_icon_button(ICON_FA_SHUFFLE, "Load a random preset"_s, 0.9f);
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
            do_icon_button(ICON_FA_FLOPPY_DISK, "Save the current state as a preset"_s, 0.8f);
        if (preset_save.button_fired) g->save_preset_panel_state.open = true;
    }

    {
        auto const preset_load = do_icon_button(ICON_FA_FILE_IMPORT, "Load a preset from a file"_s, 0.8f);
        if (preset_load.button_fired) g->preset_picker_state.common_state.open = true;
    }

    auto right_icon_buttons_container = DoBox(box_system,
                                              {
                                                  .parent = root,
                                                  .layout {
                                                      .size = layout::k_hug_contents,
                                                  },
                                              });
    auto const right_icon_margin = 2;

    // preferences
    {
        auto const prefs_button = DoBox(box_system,
                                        {
                                            .parent = right_icon_buttons_container,
                                            .text = ICON_FA_GEAR,
                                            .size_from_text = true,
                                            .font = FontType::Icons,
                                            .text_colours {
                                                .base = icon_button_colour,
                                                .hot = style::Colour::Highlight,
                                                .active = style::Colour::Highlight,
                                            },
                                            .layout {.margins = {.lrtb = right_icon_margin}},
                                            .tooltip = "Open preferences window"_s,
                                            .button_behaviour = true,
                                        });
        if (prefs_button.button_fired) g->preferences_panel_state.open = true;
    }

    // info
    {
        auto const info_button = DoBox(box_system,
                                       {
                                           .parent = right_icon_buttons_container,
                                           .text = ICON_FA_CIRCLE_INFO,
                                           .size_from_text = true,
                                           .font = FontType::Icons,
                                           .text_colours {
                                               .base = icon_button_colour,
                                               .hot = style::Colour::Highlight,
                                               .active = style::Colour::Highlight,
                                           },
                                           .layout {
                                               .margins = {.lrtb = right_icon_margin},
                                               .contents_align = layout::Alignment::End,
                                               .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                           },
                                           .tooltip = "Open info window"_s,
                                           .button_behaviour = true,
                                       });
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
        auto const attribution_button = DoBox(box_system,
                                              {
                                                  .parent = right_icon_buttons_container,
                                                  .text = ICON_FA_FILE_SIGNATURE,
                                                  .size_from_text = true,
                                                  .font = FontType::Icons,
                                                  .text_colours {
                                                      .base = style::Colour::Red,
                                                      .hot = style::Colour::Highlight,
                                                      .active = style::Colour::Highlight,
                                                  },
                                                  .layout {.margins = {.lrtb = right_icon_margin}},
                                                  .tooltip = "Open attribution requirements"_s,
                                                  .button_behaviour = true,
                                              });
        if (attribution_button.button_fired) g->attribution_panel_open = true;
    }

    // dots menu
    {
        auto const dots_button = DoBox(box_system,
                                       {
                                           .parent = right_icon_buttons_container,
                                           .text = ICON_FA_ELLIPSIS_VERTICAL,
                                           .size_from_text = true,
                                           .font = FontType::Icons,
                                           .text_colours {
                                               .base = icon_button_colour,
                                               .hot = style::Colour::Highlight,
                                               .active = style::Colour::Highlight,
                                           },
                                           .layout {.margins = {.lrtb = right_icon_margin}},
                                           .tooltip = "Additional functions and information"_s,
                                           .button_behaviour = true,
                                       });
        if (dots_button.button_fired) DoDotsMenu(g);
    }

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

    DoParameterComponent(box_system,
                         root,
                         g->engine.processor.params[ToInt(ParamIndex::MasterVolume)],
                         g->engine.processor,
                         {});

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
            box_system,
            root,
            g->engine.processor.params[ToInt(ParamIndex::MasterTimbre)],
            g->engine.processor,
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
}

void TopPanel(Gui* g, f32 height) {
    if constexpr (true) {
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

    } else {
        auto draw_top_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
            auto r = window->unpadded_bounds;
            auto const top = LiveCol(imgui, UiColMap::TopPanelBackTop);
            auto const bot = LiveCol(imgui, UiColMap::TopPanelBackBot);
            imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), top, top, bot, bot);
        };
        auto sets = imgui::DefWindow();
        sets.draw_routine_window_background = draw_top_window;
        sets.pad_top_left = {LiveSize(g->imgui, UiSizeId::Top2PadLR), LiveSize(g->imgui, UiSizeId::Top2PadT)};
        sets.pad_bottom_right = {LiveSize(g->imgui, UiSizeId::Top2PadLR),
                                 LiveSize(g->imgui, UiSizeId::Top2PadB)};
        g->imgui.BeginWindow(sets, {.xywh {0, 0, g->imgui.Width(), height}}, "TopPanel");

        DEFER { g->imgui.EndWindow(); };

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

        auto const preset_box_icon_width = LiveSize(g->imgui, UiSizeId::Top2PresetBoxIconWidth);
        auto const preset_lr_button_width = LiveSize(g->imgui, UiSizeId::NextPrevButtonSize);
        auto const icon_width = LiveSize(g->imgui, UiSizeId::Top2IconWidth);
        auto const icon_height = LiveSize(g->imgui, UiSizeId::Top2IconHeight);

        auto root = layout::CreateItem(g->layout,
                                       g->scratch_arena,
                                       {
                                           .size = g->imgui.Size(),
                                           .contents_direction = layout::Direction::Row,
                                           .contents_align = layout::Alignment::Start,
                                       });

        auto left_container = layout::CreateItem(g->layout,
                                                 g->scratch_arena,
                                                 {
                                                     .parent = root,
                                                     .size = {layout::k_hug_contents, layout::k_fill_parent},
                                                     .contents_direction = layout::Direction::Row,
                                                     .contents_align = layout::Alignment::Start,
                                                 });

        auto title = layout::CreateItem(
            g->layout,
            g->scratch_arena,
            {
                .parent = left_container,
                .size = {LiveSize(g->imgui, UiSizeId::Top2TitleWidth), layout::k_fill_parent},
                .margins = {.l = LiveSize(g->imgui, UiSizeId::Top2TitleMarginL),
                            .r = LiveSize(g->imgui, UiSizeId::Top2TitleSubtitleGap)},
            });
        auto subtitle = layout::CreateItem(g->layout,
                                           g->scratch_arena,
                                           {
                                               .parent = left_container,
                                               .size = {layout::k_fill_parent, layout::k_fill_parent},
                                           });

        auto right_container = layout::CreateItem(g->layout,
                                                  g->scratch_arena,
                                                  {
                                                      .parent = root,
                                                      .size = layout::k_fill_parent,
                                                      .contents_direction = layout::Direction::Row,
                                                      .contents_align = layout::Alignment::End,
                                                  });

        auto preset_box =
            layout::CreateItem(g->layout,
                               g->scratch_arena,
                               {
                                   .parent = right_container,
                                   .size = layout::k_hug_contents,
                                   .margins = {.l = LiveSize(g->imgui, UiSizeId::Top2PresetBoxMarginL),
                                               .r = LiveSize(g->imgui, UiSizeId::Top2PresetBoxMarginR)},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                               });

        auto preset_menu =
            layout::CreateItem(g->layout,
                               g->scratch_arena,
                               {
                                   .parent = preset_box,
                                   .size = {LiveSize(g->imgui, UiSizeId::Top2PresetBoxW), icon_height},
                               });

        auto preset_left = layout::CreateItem(g->layout,
                                              g->scratch_arena,
                                              {
                                                  .parent = preset_box,
                                                  .size = {preset_lr_button_width, icon_height},
                                              });
        auto preset_right = layout::CreateItem(g->layout,
                                               g->scratch_arena,
                                               {
                                                   .parent = preset_box,
                                                   .size = {preset_lr_button_width, icon_height},
                                               });
        auto preset_random = layout::CreateItem(g->layout,
                                                g->scratch_arena,
                                                {
                                                    .parent = preset_box,
                                                    .size = {preset_box_icon_width, icon_height},
                                                    .margins = {.r = g->imgui.VwToPixels(2)},
                                                });
        auto preset_save = layout::CreateItem(g->layout,
                                              g->scratch_arena,
                                              {
                                                  .parent = preset_box,
                                                  .size = {preset_box_icon_width, icon_height},
                                              });

        auto preset_load =
            layout::CreateItem(g->layout,
                               g->scratch_arena,
                               {
                                   .parent = preset_box,
                                   .size = {preset_box_icon_width, icon_height},
                                   .margins = {.r = LiveSize(g->imgui, UiSizeId::Top2PresetBoxPadR)},
                               });

        auto cog = layout::CreateItem(g->layout,
                                      g->scratch_arena,
                                      {
                                          .parent = right_container,
                                          .size = {icon_width, icon_height},
                                      });
        auto info = layout::CreateItem(g->layout,
                                       g->scratch_arena,
                                       {
                                           .parent = right_container,
                                           .size = {icon_width, icon_height},
                                       });

        auto attribution_icon =
            g->engine.attribution_requirements.formatted_text.size
                ? Optional<layout::Id> {layout::CreateItem(g->layout,
                                                           g->scratch_arena,
                                                           {
                                                               .parent = right_container,
                                                               .size = {icon_width, icon_height},
                                                           })}
                : k_nullopt;

        auto dots_menu = layout::CreateItem(g->layout,
                                            g->scratch_arena,
                                            {
                                                .parent = right_container,
                                                .size = {icon_width, icon_height},
                                            });

        auto knob_container =
            layout::CreateItem(g->layout,
                               g->scratch_arena,
                               {
                                   .parent = right_container,
                                   .size = layout::k_hug_contents,
                                   .margins = {.l = LiveSize(g->imgui, UiSizeId::Top2KnobsMarginL),
                                               .r = LiveSize(g->imgui, UiSizeId::Top2KnobsMarginR)},
                                   .contents_direction = layout::Direction::Row,
                               });
        LayIDPair dyn;
        LayoutParameterComponent(g,
                                 knob_container,
                                 dyn,
                                 g->engine.processor.params[ToInt(ParamIndex::MasterTimbre)],
                                 UiSizeId::Top2KnobsGapX);
        LayIDPair vol;
        LayoutParameterComponent(g,
                                 knob_container,
                                 vol,
                                 g->engine.processor.params[ToInt(ParamIndex::MasterVolume)],
                                 UiSizeId::Top2KnobsGapX);

        auto level = layout::CreateItem(g->layout,
                                        g->scratch_arena,
                                        {
                                            .parent = right_container,
                                            .size = {LiveSize(g->imgui, UiSizeId::Top2PeakMeterW),
                                                     LiveSize(g->imgui, UiSizeId::Top2PeakMeterH)},
                                        });

        layout::RunContext(g->layout);
        DEFER { layout::ResetContext(g->layout); };

        auto preset_rand_r = layout::GetRect(g->layout, preset_random);
        auto preset_menu_r = layout::GetRect(g->layout, preset_menu);
        auto preset_left_r = layout::GetRect(g->layout, preset_left);
        auto preset_right_r = layout::GetRect(g->layout, preset_right);
        auto preset_save_r = layout::GetRect(g->layout, preset_save);
        auto preset_load_r = layout::GetRect(g->layout, preset_load);
        auto level_r = layout::GetRect(g->layout, level);

        //
        //
        //
        {
            auto back_r = g->imgui.GetRegisteredAndConvertedRect(layout::GetRect(g->layout, preset_box));
            auto const rounding = LiveSize(g->imgui, UiSizeId::CornerRounding);
            g->imgui.graphics->AddRectFilled(back_r,
                                             LiveCol(g->imgui, UiColMap::TopPanelPresetsBack),
                                             rounding);
        }

        {
            auto const title_r = g->imgui.GetRegisteredAndConvertedRect(layout::GetRect(g->layout, title));

            auto const logo = LogoImage(g);
            if (logo) {
                auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(*logo);
                if (tex) {
                    auto logo_size = f32x2 {(f32)logo->size.width, (f32)logo->size.height};

                    logo_size *= title_r.size.y / logo_size.y;
                    if (logo_size.x > title_r.size.x) logo_size *= title_r.size.x / logo_size.x;

                    auto logo_pos = title_r.pos;
                    logo_pos.y += (title_r.size.y - logo_size.y) / 2;

                    g->imgui.graphics->AddImage(*tex, logo_pos, logo_pos + logo_size, {0, 0}, {1, 1});
                }
            }
        }

        {
            auto subtitle_r = layout::GetRect(g->layout, subtitle);
            auto const show_instance_name =
                prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::ShowInstanceName));
            labels::Label(
                g,
                subtitle_r,
                fmt::Format(g->scratch_arena,
                            "v" FLOE_VERSION_STRING "  {}",
                            show_instance_name ? String {InstanceId(g->engine.autosave_state)} : ""_s),
                labels::Title(g->imgui, LiveCol(g->imgui, UiColMap::TopPanelSubtitleText)));
        }

        //
        auto large_icon_button_style = buttons::TopPanelIconButton(g->imgui).WithLargeIcon();
        {
            auto const btn_id = g->imgui.GetID("L");
            if (buttons::Button(g, btn_id, preset_left_r, ICON_FA_CARET_LEFT, large_icon_button_style)) {
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
            if (g->imgui.IsHot(btn_id)) StartScanningIfNeeded(g->shared_engine_systems.preset_server);
            Tooltip(g, btn_id, preset_left_r, "Load previous preset"_s);
        }
        {
            auto const btn_id = g->imgui.GetID("R");
            if (buttons::Button(g, btn_id, preset_right_r, ICON_FA_CARET_RIGHT, large_icon_button_style)) {
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
            if (g->imgui.IsHot(btn_id)) StartScanningIfNeeded(g->shared_engine_systems.preset_server);
            Tooltip(g, btn_id, preset_left_r, "Load next preset"_s);
        }

        {
            g->frame_input.graphics_ctx->PushFont(g->fonts[ToInt(FontType::Icons)]);
            DEFER { g->frame_input.graphics_ctx->PopFont(); };

            {
                auto const btn_id = g->imgui.GetID("rand_pre");
                if (buttons::Button(g,
                                    btn_id,
                                    preset_rand_r,
                                    ICON_FA_SHUFFLE,
                                    large_icon_button_style.WithIconScaling(0.8f))) {
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
                if (g->imgui.IsHot(btn_id)) StartScanningIfNeeded(g->shared_engine_systems.preset_server);

                Tooltip(g, btn_id, preset_rand_r, "Load a random preset"_s);
            }

            {
                auto const btn_id = g->imgui.GetID("save");
                if (buttons::Button(g, btn_id, preset_save_r, ICON_FA_FLOPPY_DISK, large_icon_button_style))
                    g->save_preset_panel_state.open = true;
                Tooltip(g, btn_id, preset_save_r, "Save the current state as a preset"_s);
            }

            {
                auto const btn_id = g->imgui.GetID("load");
                if (buttons::Button(g, btn_id, preset_load_r, ICON_FA_FILE_IMPORT, large_icon_button_style))
                    OpenFilePickerLoadPreset(g->file_picker_state,
                                             g->imgui.frame_output,
                                             g->shared_engine_systems.paths);

                Tooltip(g, btn_id, preset_load_r, "Load a preset from a file"_s);
            }
        }

        {
            auto btn_id = g->imgui.GetID("sets");
            auto btn_r = layout::GetRect(g->layout, cog);
            if (buttons::Button(g, btn_id, btn_r, ICON_FA_GEAR, large_icon_button_style))
                g->preferences_panel_state.open = true;
            Tooltip(g, btn_id, btn_r, "Open preferences window"_s);
        }

        {
            auto btn_id = g->imgui.GetID("info");
            auto btn_r = layout::GetRect(g->layout, info);
            if (buttons::Button(g, btn_id, btn_r, ICON_FA_CIRCLE_INFO, large_icon_button_style))
                g->info_panel_state.open = true;
            if (check_for_update::ShowNewVersionIndicator(g->shared_engine_systems.check_for_update_state,
                                                          g->prefs)) {
                auto const r = g->imgui.GetRegisteredAndConvertedRect(btn_r);
                auto const dot_radius = r.size.x / 8;
                auto const dot_pos = r.pos + f32x2 {r.size.x - dot_radius, dot_radius};
                g->imgui.graphics->AddCircleFilled(dot_pos,
                                                   dot_radius,
                                                   LiveCol(g->imgui, UiColMap::TopPanelIconDot));
            }
            Tooltip(g, btn_id, btn_r, "Open information window"_s);
        }

        if (attribution_icon) {
            auto btn_id = g->imgui.GetID("attribution");
            auto btn_r = layout::GetRect(g->layout, *attribution_icon);
            if (buttons::Button(g,
                                btn_id,
                                btn_r,
                                ICON_FA_FILE_SIGNATURE,
                                buttons::TopPanelAttributionIconButton(g->imgui)))
                g->attribution_panel_open = true;
            Tooltip(g, btn_id, btn_r, "Open attribution requirments"_s);
        }

        {
            auto const additonal_menu_r = layout::GetRect(g->layout, dots_menu);
            auto const additional_menu_id = g->imgui.GetID("Menu");
            auto const popup_id = g->imgui.GetID("MenuPopup");
            if (buttons::Popup(g,
                               additional_menu_id,
                               popup_id,
                               additonal_menu_r,
                               ICON_FA_ELLIPSIS_VERTICAL,
                               large_icon_button_style)) {
                DoDotsMenu(g);
                g->imgui.EndWindow();
            }
            Tooltip(g, additional_menu_id, additonal_menu_r, "Additional functions and information"_s);
        }

        PresetsWindowButton(g, &g->engine, preset_menu_r);

        //

        peak_meters::PeakMeter(g, level_r, g->engine.processor.peak_meter, true);

        KnobAndLabel(g,
                     g->engine.processor.params[ToInt(ParamIndex::MasterVolume)],
                     vol,
                     knobs::DefaultKnob(g->imgui));

        {
            g->timbre_slider_is_held = false;
            auto const id =
                g->imgui.GetID((u64)g->engine.processor.params[ToInt(ParamIndex::MasterTimbre)].info.id);
            if (has_insts_with_timbre_layers) {
                knobs::Knob(g,
                            id,
                            g->engine.processor.params[ToInt(ParamIndex::MasterTimbre)],
                            dyn.control,
                            knobs::DefaultKnob(g->imgui));
                g->timbre_slider_is_held = g->imgui.IsActive(id);
                if (g->imgui.WasJustActivated(id))
                    g->imgui.frame_output.ElevateUpdateRequest(
                        GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
            } else {
                auto knob_r = layout::GetRect(g->layout, dyn.control);
                knobs::FakeKnob(g, knob_r);

                g->imgui.RegisterAndConvertRect(&knob_r);
                g->imgui.ButtonBehavior(knob_r, id, {});
                Tooltip(
                    g,
                    id,
                    knob_r,
                    "Timbre: no currently loaded instruments have timbre information; this knob is inactive"_s);
                if (g->imgui.IsHot(id)) g->imgui.frame_output.cursor_type = CursorType::Default;
            }
            labels::Label(g,
                          g->engine.processor.params[ToInt(ParamIndex::MasterTimbre)],
                          dyn.label,
                          labels::ParameterCentred(g->imgui, !has_insts_with_timbre_layers));
        }
    }
}
