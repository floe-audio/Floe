// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>

#include "common_infrastructure/paths.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_inst_picker.hpp"
#include "gui/gui2_ir_picker.hpp"
#include "gui_button_widgets.hpp"
#include "gui_menu.hpp"
#include "gui_modal_windows.hpp"
#include "gui_peak_meter_widget.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_compounds.hpp"
#include "gui_widget_helpers.hpp"
#include "state/state_snapshot.hpp"

static void PresetsWindowButton(Gui* g, Engine* a, Rect r) {
    auto button_id = g->imgui.GetID("PresetMenu");

    DynamicArrayBounded<char, 100> preset_text {a->last_snapshot.name_or_path.Name()};
    if (StateChangedSinceLastSnapshot(*a)) dyn::AppendSpan(preset_text, " (modified)"_s);

    auto const popup_id = button_id + 1;
    if (buttons::Button(g, button_id, r, preset_text, buttons::PresetsPopupButton(g->imgui)))
        g->imgui.OpenPopup(popup_id, button_id);

    if (g->imgui.IsHot(button_id)) StartScanningIfNeeded(g->shared_engine_systems.preset_server);

    PresetPickerContext context {
        .sample_library_server = g->shared_engine_systems.sample_library_server,
        .preset_server = g->shared_engine_systems.preset_server,
        .library_images = g->library_images,
        .engine = g->engine,
        .unknown_library_icon = UnknownLibraryIcon(g),
    };
    DoPresetPicker(g->box_system,
                   popup_id,
                   g->imgui.GetRegisteredAndConvertedRect(r),
                   context,
                   g->preset_picker_state);

    Tooltip(g, button_id, r, "Open presets window"_s);
}

static void DoDotsMenu(Gui* g) {
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
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };
            LoadRandomInstrument(context, g->inst_picker_state, false);
        }
        {
            IrPickerContext ir_context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
            };
            ir_context.Init(g->scratch_arena);
            DEFER { ir_context.Deinit(); };
            LoadRandomIr(ir_context, g->ir_picker_state);
        }
    }
    if (top_menu.DoButton("Share Feedback")) g->feedback_panel_state.open = true;
    if (top_menu.DoButton("Library Developer Panel")) g->library_dev_panel_state.open = true;
}

void TopPanel(Gui* g) {
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
                                   {
                                       .size = g->imgui.Size(),
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                   });

    auto left_container = layout::CreateItem(g->layout,
                                             {
                                                 .parent = root,
                                                 .size = {layout::k_hug_contents, layout::k_fill_parent},
                                                 .contents_direction = layout::Direction::Row,
                                                 .contents_align = layout::Alignment::Start,
                                             });

    auto title =
        layout::CreateItem(g->layout,
                           {
                               .parent = left_container,
                               .size = {LiveSize(g->imgui, UiSizeId::Top2TitleWidth), layout::k_fill_parent},
                               .margins = {.l = LiveSize(g->imgui, UiSizeId::Top2TitleMarginL),
                                           .r = LiveSize(g->imgui, UiSizeId::Top2TitleSubtitleGap)},
                           });
    auto subtitle = layout::CreateItem(g->layout,
                                       {
                                           .parent = left_container,
                                           .size = {layout::k_fill_parent, layout::k_fill_parent},
                                       });

    auto right_container = layout::CreateItem(g->layout,
                                              {
                                                  .parent = root,
                                                  .size = layout::k_fill_parent,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::End,
                                              });

    auto preset_box =
        layout::CreateItem(g->layout,
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
                           {
                               .parent = preset_box,
                               .size = {LiveSize(g->imgui, UiSizeId::Top2PresetBoxW), icon_height},
                           });

    auto preset_left = layout::CreateItem(g->layout,
                                          {
                                              .parent = preset_box,
                                              .size = {preset_lr_button_width, icon_height},
                                          });
    auto preset_right = layout::CreateItem(g->layout,
                                           {
                                               .parent = preset_box,
                                               .size = {preset_lr_button_width, icon_height},
                                           });
    auto preset_random = layout::CreateItem(g->layout,
                                            {
                                                .parent = preset_box,
                                                .size = {preset_box_icon_width, icon_height},
                                                .margins = {.r = g->imgui.VwToPixels(2)},
                                            });
    auto preset_save = layout::CreateItem(g->layout,
                                          {
                                              .parent = preset_box,
                                              .size = {preset_box_icon_width, icon_height},
                                          });

    auto preset_load =
        layout::CreateItem(g->layout,
                           {
                               .parent = preset_box,
                               .size = {preset_box_icon_width, icon_height},
                               .margins = {.r = LiveSize(g->imgui, UiSizeId::Top2PresetBoxPadR)},
                           });

    auto cog = layout::CreateItem(g->layout,
                                  {
                                      .parent = right_container,
                                      .size = {icon_width, icon_height},
                                  });
    auto info = layout::CreateItem(g->layout,
                                   {
                                       .parent = right_container,
                                       .size = {icon_width, icon_height},
                                   });

    auto attribution_icon =
        g->engine.attribution_requirements.formatted_text.size
            ? Optional<layout::Id> {layout::CreateItem(g->layout,
                                                       {
                                                           .parent = right_container,
                                                           .size = {icon_width, icon_height},
                                                       })}
            : k_nullopt;

    auto dots_menu = layout::CreateItem(g->layout,
                                        {
                                            .parent = right_container,
                                            .size = {icon_width, icon_height},
                                        });

    auto knob_container =
        layout::CreateItem(g->layout,
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
    LayIDPair velo;
    LayoutParameterComponent(g,
                             knob_container,
                             velo,
                             g->engine.processor.params[ToInt(ParamIndex::MasterVelocity)],
                             UiSizeId::Top2KnobsGapX);
    LayIDPair vol;
    LayoutParameterComponent(g,
                             knob_container,
                             vol,
                             g->engine.processor.params[ToInt(ParamIndex::MasterVolume)],
                             UiSizeId::Top2KnobsGapX);

    auto level = layout::CreateItem(g->layout,
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
        g->imgui.graphics->AddRectFilled(back_r, LiveCol(g->imgui, UiColMap::TopPanelPresetsBack), rounding);
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
        labels::Label(g,
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
    KnobAndLabel(g,
                 g->engine.processor.params[ToInt(ParamIndex::MasterVelocity)],
                 velo,
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
                g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
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
