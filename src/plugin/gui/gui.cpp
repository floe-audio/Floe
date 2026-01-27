// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui.hpp"

#include <IconsFontAwesome6.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui/gui2_attribution_panel.hpp"
#include "gui/gui2_bot_panel.hpp"
#include "gui/gui2_confirmation_dialog.hpp"
#include "gui/gui2_feedback_panel.hpp"
#include "gui/gui2_info_panel.hpp"
#include "gui/gui2_inst_browser.hpp"
#include "gui/gui2_ir_browser.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui/gui2_package_install.hpp"
#include "gui/gui2_prefs_panel.hpp"
#include "gui/gui_file_picker.hpp"
#include "gui/gui_frame_context.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_editors.hpp"
#include "gui_framework/graphics.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/image.hpp"
#include "gui_modal_windows.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_helpers.hpp"
#include "plugin/plugin.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static f32 PixelsPerVw() {
    constexpr auto k_points_in_width = 1000.0f; // 1000 just because it's easy to work with
    return (f32)GuiIo().in.window_size.width / k_points_in_width;
}

LibraryImages
LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id, LibraryImagesTypes needed) {
    return GetLibraryImages(g->library_images,
                            g->imgui,
                            library_id,
                            g->shared_engine_systems.sample_library_server,
                            needed);
}

Optional<graphics::ImageID> LogoImage(Gui* g) {
    if (!g->imgui.draw_list->renderer->ImageIdIsValid(g->floe_logo_image)) {
        auto const data = EmbeddedLogoImage();
        if (data.size) {
            auto outcome = DecodeImage({data.data, data.size}, g->scratch_arena);
            ASSERT(!outcome.HasError());
            auto const pixels = outcome.ReleaseValue();
            g->floe_logo_image = CreateImageIdChecked(*g->imgui.draw_list->renderer, pixels);
        }
    }
    return g->floe_logo_image;
}

static void SampleLibraryChanged(Gui* g, sample_lib::LibraryIdRef library_id) {
    InvalidateLibraryImages(g->library_images, library_id, *GuiIo().in.renderer);
}

static void CreateFontsIfNeeded(Gui* g) {
    //
    // Fonts
    //
    auto& renderer = *GuiIo().in.renderer;

    if (!renderer.HasFontTexture()) {
        renderer.fonts.Clear();

        LoadFonts(renderer.fonts, g->fonts, g->imgui.pixels_per_vw);

        auto const outcome = renderer.CreateFontTexture();
        if (outcome.HasError())
            LogError(ModuleName::Gui, "Failed to create font texture: {}", outcome.Error());
    }
}

Gui::Gui(Engine& engine)
    : engine(engine)
    , shared_engine_systems(engine.shared_engine_systems)
    , prefs(engine.shared_engine_systems.prefs)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          engine.shared_engine_systems.sample_library_server,
          {
              .error_notifications = engine.error_notifications,
              .result_added_callback = []() {},
              .library_changed_callback =
                  [gui = this](sample_lib::LibraryIdRef library_id_ref) {
                      sample_lib::LibraryId lib_id {library_id_ref};
                      gui->main_thread_callbacks.Push([gui, lib_id]() { SampleLibraryChanged(gui, lib_id); });
                      g_request_gui_update.Store(true, StoreMemoryOrder::Relaxed);
                  },
          })) {
    Trace(ModuleName::Gui);

    editor.imgui = &imgui;

    ASSERT(!engine.stated_changed_callback);
    engine.stated_changed_callback = [this]() { OnEngineStateChange(save_preset_panel_state, this->engine); };

    // The GUI has opened, we can check for updates if needed. We don't want to do this before because it has
    // no use until the GUI is open.
    check_for_update::FetchLatestIfNeeded(shared_engine_systems.check_for_update_state);
    shared_engine_systems.StartPollingThreadIfNeeded();
}

Gui::~Gui() {
    Shutdown(library_images);
    Shutdown(waveform_images);

    engine.stated_changed_callback = {};

    sample_lib_server::CloseAsyncCommsChannel(engine.shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);
    Trace(ModuleName::Gui);
    if (engine.processor.gui_note_click_state.Load(LoadMemoryOrder::Relaxed).is_held) {
        engine.processor.gui_note_click_state.Store({.is_held = false}, StoreMemoryOrder::Release);
        engine.host.request_process(&engine.host);
    }
}

bool Tooltip(Gui* g, imgui::Id id, Rect r, char const* fmt, ...);

static void DoStandaloneErrorGUI(Gui* g) {
    ASSERT(!PRODUCTION_BUILD);

    auto& engine = g->engine;

    auto const host = engine.host;
    auto const floe_ext = (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
    if (!floe_ext) return;

    auto const& frame_input = GuiIo().in;

    frame_input.renderer->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { frame_input.renderer->PopFont(); };
    auto& imgui = g->imgui;
    static bool error_window_open = true;

    bool const there_is_an_error =
        floe_ext->standalone_midi_device_error || floe_ext->standalone_audio_device_error;
    if (error_window_open && there_is_an_error) {
        auto settings = imgui::DefWindow();
        settings.flags.auto_height = true;
        settings.flags.auto_width = true;
        imgui.BeginWindow(settings, {.xywh {0, 0, 200, 0}}, "StandaloneErrors");
        DEFER { imgui.EndWindow(); };
        f32 y_pos = 0;
        if (floe_ext->standalone_midi_device_error) {
            imgui.Text(imgui::DefText(), {.xywh {0, y_pos, 100, 20}}, "No MIDI input");
            y_pos += 20;
        }
        if (floe_ext->standalone_audio_device_error) {
            imgui.Text(imgui::DefText(), {.xywh {0, y_pos, 100, 20}}, "No audio devices");
            y_pos += 20;
        }
        if (imgui.Button(imgui::DefButton(), {.xywh {0, y_pos, 100, 20}}, imgui.GetID("closeErr"), "Close"))
            error_window_open = false;
    }
}

static bool HasAnyErrorNotifications(Gui* g) {
    for (auto& err_notifications :
         Array {&g->engine.error_notifications, &g->shared_engine_systems.error_notifications}) {
        if (err_notifications->HasErrors()) return true;
    }
    return false;
}

static void DoResizeCorner(Gui* g) {
    auto& imgui = g->imgui;
    auto const& frame_input = GuiIo().in;
    auto& frame_output = GuiIo().out;

    auto const corner_size = LiveSize(imgui, UiSizeId::WindowResizeCornerSize);
    imgui::WindowSettings settings {
        .draw_routine_window_background = [](IMGUI_DRAW_WINDOW_BG_ARGS_TYPES) {},
    };
    imgui.BeginWindow(settings,
                      Rect {
                          .pos = imgui.Size() - corner_size,
                          .size = corner_size,
                      },
                      "ResizeCorner");
    DEFER { imgui.EndWindow(); };

    auto const r = imgui.GetRegisteredAndConvertedRect({.pos = 0, .size = imgui.Size()});

    auto const& desc = SettingDescriptor(GuiSetting::WindowWidth);

    auto const id = imgui.GetID("resize_corner");

    static f32x2 size_at_start {};
    if (g->imgui.ButtonBehavior(r, id, {.left_mouse = true, .triggers_on_mouse_down = true}))
        size_at_start = frame_input.window_size.ToFloat2();

    if (g->imgui.IsHotOrActive(id)) frame_output.wants.cursor_type = CursorType::UpLeftDownRight;

    if (g->imgui.IsActive(id)) {
        frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);

        auto const cursor = frame_input.cursor_pos;
        auto const delta = cursor - frame_input.Mouse(MouseButton::Left).last_press.point;
        auto const desired_new_size = Max(size_at_start + delta, f32x2(0.0f));

        UiSize32 const ui_size {
            (u32)desired_new_size.x,
            (u32)desired_new_size.y,
        };

        if (auto const new_size = NearestAspectRatioSizeInsideSize32(ui_size, k_gui_aspect_ratio))
            prefs::SetValue(g->prefs, desc, (s64)new_size->width);
    }

    imgui.draw_list->AddTriangleFilled(r.TopRight(),
                                       r.BottomRight(),
                                       r.BottomLeft(),
                                       style::Col(style::Colour::Background0 | style::Colour::DarkMode));

    auto const line_col =
        style::Col(imgui.IsHotOrActive(id) ? style::Colour::Text | style::Colour::DarkMode
                                           : style::Colour::Overlay2 | style::Colour::DarkMode);
    auto const line_gap = LiveSize(imgui, UiSizeId::WindowResizeCornerLineGap);
    imgui.draw_list->AddLine(r.TopRight() + f32x2 {0, line_gap},
                             r.BottomLeft() + f32x2 {line_gap, 0},
                             line_col);
    imgui.draw_list->AddLine(r.TopRight() + f32x2 {0, line_gap * 2},
                             r.BottomLeft() + f32x2 {line_gap * 2, 0},
                             line_col);
}

void GuiUpdate(Gui* g) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);
    g->imgui.SetPixelsPerVw(PixelsPerVw());

    auto const& frame_input = GuiIo().in;

    BeginFrame(g->library_images);
    BeginFrame(g->box_system, prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::ShowTooltips)));

    g->show_new_version_indicator =
        check_for_update::ShowNewVersionIndicator(g->shared_engine_systems.check_for_update_state, g->prefs);

    live_edit::g_high_contrast_gui =
        prefs::GetBool(g->prefs,
                       SettingDescriptor(GuiSetting::HighContrastGui)); // IMPROVE: hacky
    g->scratch_arena.ResetCursorAndConsolidateRegions();

    layout::ReserveItemsCapacity(g->layout, g->scratch_arena, 2048);
    DEFER {
        // We use the scratch arena for the layout, so we can just reset it to zero rather than having to do
        // the deallocations.
        g->layout = {};
    };

    while (auto function = g->main_thread_callbacks.TryPop(g->scratch_arena))
        (*function)();

    GuiFrameContext frame_context;
    DEFER { sample_lib_server::ReleaseAll(frame_context.libraries); };
    {
        auto libs = sample_lib_server::AllLibrariesRetained(g->shared_engine_systems.sample_library_server,
                                                            g->scratch_arena);
        Sort(libs, [](auto const& a, auto const& b) { return a->name < b->name; });
        auto libs_table = sample_lib_server::MakeTable(libs, g->scratch_arena);
        frame_context = {
            .libraries = libs,
            .lib_table = libs_table,
        };
    }

    CheckForFilePickerResults(frame_input,
                              g->file_picker_state,
                              {
                                  .prefs = g->prefs,
                                  .paths = g->shared_engine_systems.paths,
                                  .package_install_jobs = g->engine.package_install_jobs,
                                  .thread_pool = g->shared_engine_systems.thread_pool,
                                  .scratch_arena = g->scratch_arena,
                                  .sample_lib_server = g->shared_engine_systems.sample_library_server,
                                  .preset_server = g->shared_engine_systems.preset_server,
                                  .engine = g->engine,
                              });

    CreateFontsIfNeeded(g);

    auto& imgui = g->imgui;

    MacroGuiBeginFrame(g);

    StartFrame(g->waveform_images, *frame_input.renderer);
    DEFER { EndFrame(g->waveform_images, *frame_input.renderer); };

    auto whole_window_sets = imgui::DefMainWindow();
    whole_window_sets.draw_routine_window_background = [&](IMGUI_DRAW_WINDOW_BG_ARGS_TYPES) {};
    imgui.Begin(whole_window_sets);

    frame_input.renderer->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { frame_input.renderer->PopFont(); };

    auto const top_h = Round(LiveSize(imgui, UiSizeId::Top2Height));
    auto const bot_h = Round(LiveSize(imgui, UiSizeId::BotPanelHeight));
    auto const mid_h = frame_input.window_size.height - top_h - bot_h;

    auto draw_mid_window = [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;

        imgui.draw_list->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::MidPanelBack));

        if (!prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::HighContrastGui))) {
            auto overall_library = LibraryForOverallBackground(g->engine);
            if (overall_library) {
                auto imgs = LibraryImagesFromLibraryId(g, *overall_library, LibraryImagesTypes::Backgrounds);
                if (imgs.background) {
                    auto tex = GuiIo().in.renderer->GetTextureFromImage(*imgs.background);
                    if (tex) {
                        imgui.draw_list->AddImage(*tex,
                                                  r.Min(),
                                                  r.Max(),
                                                  {0, 0},
                                                  GetMaxUVToMaintainAspectRatio(*imgs.background, r.size));
                    }
                }
            }
        }

        imgui.draw_list->AddLine(r.TopLeft(), r.TopRight(), LiveCol(imgui, UiColMap::MidPanelTopLine));
    };

    {
        auto mid_settings = imgui::DefWindow();
        mid_settings.pad_top_left = {};
        mid_settings.pad_bottom_right = {};
        mid_settings.draw_routine_window_background = draw_mid_window;
        mid_settings.flags = {};

        auto mid_panel_r = Rect {.x = 0, .y = top_h, .w = imgui.Width(), .h = mid_h};
        imgui.BeginWindow(mid_settings, mid_panel_r, "MidPanel");
        MidPanel(g, frame_context);
        imgui.EndWindow();
    }

    TopPanel(g, top_h, frame_context);

    BotPanel(g, {.xywh {0, top_h + mid_h, imgui.Width(), bot_h}});

    DoResizeCorner(g);

    if (!PRODUCTION_BUILD && NullTermStringsEqual(g->engine.host.name, k_floe_standalone_host_name))
        DoStandaloneErrorGUI(g);

    if (HasAnyErrorNotifications(g)) OpenModalIfNotAlready(imgui, ModalWindowType::LoadError);

    DoModalWindows(g);

    // GUI2 panels. This is the future.
    {
        {
            LibraryDevPanelContext context {
                .engine = g->engine,
                .notifications = g->notifications,
            };
            DoLibraryDevPanel(g->box_system, context, g->library_dev_panel_state);
        }

        {
            PreferencesPanelContext context {
                .prefs = g->prefs,
                .paths = g->shared_engine_systems.paths,
                .sample_lib_server = g->shared_engine_systems.sample_library_server,
                .package_install_jobs = g->engine.package_install_jobs,
                .thread_pool = g->shared_engine_systems.thread_pool,
                .file_picker_state = g->file_picker_state,
                .presets_server = g->shared_engine_systems.preset_server,
            };

            DoPreferencesPanel(g->box_system, context, g->preferences_panel_state);
        }

        {
            FeedbackPanelContext context {
                .notifications = g->notifications,
            };
            DoFeedbackPanel(g->box_system, context, g->feedback_panel_state);
        }

        DoConfirmationDialog(g->box_system, g->confirmation_dialog_state);

        {
            SavePresetPanelContext context {
                .engine = g->engine,
                .file_picker_state = g->file_picker_state,
                .paths = g->shared_engine_systems.paths,
                .prefs = g->prefs,
            };
            DoSavePresetPanel(g->box_system, context, g->save_preset_panel_state);
        }

        {
            InfoPanelContext context {
                .server = g->shared_engine_systems.sample_library_server,
                .voice_pool = g->engine.processor.voice_pool,
                .scratch_arena = g->scratch_arena,
                .check_for_update_state = g->shared_engine_systems.check_for_update_state,
                .prefs = g->prefs,
                .libraries =
                    sample_lib_server::AllLibrariesRetained(g->shared_engine_systems.sample_library_server,
                                                            g->scratch_arena),
                .error_notifications = g->engine.error_notifications,
                .notifications = g->notifications,
                .confirmation_dialog_state = g->confirmation_dialog_state,
            };
            DEFER { sample_lib_server::ReleaseAll(context.libraries); };

            DoInfoPanel(g->box_system, context, g->info_panel_state);
        }

        {
            AttributionPanelContext context {
                .attribution_text = g->engine.attribution_requirements.formatted_text,
            };

            DoAttributionPanel(g->box_system, context, g->attribution_panel_open);
        }

        {
            for (auto& layer_obj : g->engine.processor.layer_processors) {
                imgui.PushID(layer_obj.index);
                DEFER { imgui.PopID(); };
                InstBrowserContext context {
                    .layer = layer_obj,
                    .sample_library_server = g->shared_engine_systems.sample_library_server,
                    .library_images = g->library_images,
                    .engine = g->engine,
                    .prefs = g->prefs,
                    .notifications = g->notifications,
                    .persistent_store = g->shared_engine_systems.persistent_store,
                    .confirmation_dialog_state = g->confirmation_dialog_state,
                    .frame_context = frame_context,
                };

                auto& state = g->inst_browser_state[layer_obj.index];
                DoInstBrowserPopup(g->box_system, context, state);
            }
        }

        {
            PresetBrowserContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .preset_server = g->shared_engine_systems.preset_server,
                .library_images = g->library_images,
                .prefs = g->prefs,
                .engine = g->engine,
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g->confirmation_dialog_state,
                .frame_context = frame_context,
            };
            DoPresetBrowser(g->box_system, context, g->preset_browser_state);
        }

        {
            IrBrowserContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .prefs = g->prefs,
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g->confirmation_dialog_state,
                .frame_context = frame_context,
            };

            DoIrBrowserPopup(g->box_system, context, g->ir_browser_state);
        }

        DoNotifications(g->box_system, g->notifications);

        DoPackageInstallNotifications(g->box_system,
                                      g->engine.package_install_jobs,
                                      g->notifications,
                                      g->engine.error_notifications,
                                      g->shared_engine_systems.thread_pool);
    }

    DoWholeEditor(g);

    MacroGuiEndFrame(g);

    imgui.End(g->scratch_arena);

    prefs::WriteIfNeeded(g->prefs);
}
