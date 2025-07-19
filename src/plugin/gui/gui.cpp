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
#include "gui/gui2_feedback_panel.hpp"
#include "gui/gui2_info_panel.hpp"
#include "gui/gui2_inst_picker.hpp"
#include "gui/gui2_ir_picker.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui/gui2_package_install.hpp"
#include "gui/gui2_prefs_panel.hpp"
#include "gui/gui_file_picker.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_editors.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_framework/gui_platform.hpp"
#include "gui_framework/image.hpp"
#include "gui_modal_windows.hpp"
#include "gui_prefs.hpp"
#include "gui_widget_helpers.hpp"
#include "plugin/plugin.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static f32 PixelsPerVw(Gui* g) {
    constexpr auto k_points_in_width = 1000.0f; // 1000 just because it's easy to work with
    return (f32)g->frame_input.window_size.width / k_points_in_width;
}

Optional<LibraryImages>
LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id, bool only_icon_needed) {
    return LibraryImagesFromLibraryId(g->library_images,
                                      g->imgui,
                                      library_id,
                                      g->shared_engine_systems.sample_library_server,
                                      g->scratch_arena,
                                      only_icon_needed);
}

Optional<graphics::ImageID> LogoImage(Gui* g) {
    if (!g->imgui.graphics->context->ImageIdIsValid(g->floe_logo_image)) {
        auto const data = EmbeddedLogoImage();
        if (data.size) {
            auto outcome = DecodeImage({data.data, data.size});
            ASSERT(!outcome.HasError());
            auto const pixels = outcome.ReleaseValue();
            g->floe_logo_image = CreateImageIdChecked(*g->imgui.graphics->context, pixels);
        }
    }
    return g->floe_logo_image;
}

Optional<graphics::ImageID>& UnknownLibraryIcon(Gui* g) {
    if (!g->imgui.graphics->context->ImageIdIsValid(g->unknown_library_icon)) {
        auto const data = EmbeddedUnknownLibraryIcon();
        if (data.size) {
            auto outcome = DecodeImage({data.data, data.size});
            ASSERT(!outcome.HasError());
            auto const pixels = outcome.ReleaseValue();
            g->unknown_library_icon = CreateImageIdChecked(*g->imgui.graphics->context, pixels);
        }
    }
    return g->unknown_library_icon;
}

static void SampleLibraryChanged(Gui* g, sample_lib::LibraryIdRef library_id) {
    InvalidateLibraryImages(g->library_images, library_id, *g->frame_input.graphics_ctx);
}

static void CreateFontsIfNeeded(Gui* g) {
    //
    // Fonts
    //
    auto& graphics_ctx = g->frame_input.graphics_ctx;

    if (graphics_ctx->fonts.tex_id == nullptr) {
        graphics_ctx->fonts.Clear();

        LoadFonts(*graphics_ctx, g->fonts, g->imgui.pixels_per_vw);

        auto const outcome = graphics_ctx->CreateFontTexture();
        if (outcome.HasError())
            LogError(ModuleName::Gui, "Failed to create font texture: {}", outcome.Error());
    }
}

Gui::Gui(GuiFrameInput& frame_input, Engine& engine)
    : frame_input(frame_input)
    , engine(engine)
    , shared_engine_systems(engine.shared_engine_systems)
    , prefs(engine.shared_engine_systems.prefs)
    , imgui(frame_input, frame_output)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          engine.shared_engine_systems.sample_library_server,
          {
              .error_notifications = engine.error_notifications,
              .result_added_callback = []() {},
              .library_changed_callback =
                  [gui = this](sample_lib::LibraryIdRef library_id_ref) {
                      sample_lib::LibraryId lib_id {library_id_ref};
                      gui->main_thread_callbacks.Push([gui, lib_id]() { SampleLibraryChanged(gui, lib_id); });
                      gui->frame_input.request_update.Store(true, StoreMemoryOrder::Relaxed);
                  },
          })) {
    Trace(ModuleName::Gui);

    editor.imgui = &imgui;
    imgui.user_callback_data = this;

    ASSERT(!engine.stated_changed_callback);
    engine.stated_changed_callback = [this]() { OnEngineStateChange(save_preset_panel_state, this->engine); };

    // The GUI has opened, we can check for updates if needed. We don't want to do this before because it has
    // no use until the GUI is open.
    check_for_update::FetchLatestIfNeeded(shared_engine_systems.check_for_update_state);
    shared_engine_systems.StartPollingThreadIfNeeded();
}

Gui::~Gui() {
    engine.stated_changed_callback = {};

    sample_lib_server::CloseAsyncCommsChannel(engine.shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);
    Trace(ModuleName::Gui);
    if (midi_keyboard_note_held_with_mouse) {
        engine.processor.events_for_audio_thread.Push(
            GuiNoteClickReleased {.key = midi_keyboard_note_held_with_mouse.Value()});
        engine.host.request_process(&engine.host);
    }
}

bool Tooltip(Gui* g, imgui::Id id, Rect r, char const* fmt, ...);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size) {
    auto const img_w = (f32)img.size.width;
    auto const img_h = (f32)img.size.height;
    auto const window_ratio = container_size.x / container_size.y;
    auto const image_ratio = img_w / img_h;

    f32x2 uv {1, 1};
    if (image_ratio > window_ratio)
        uv.x = window_ratio / image_ratio;
    else
        uv.y = image_ratio / window_ratio;
    return uv;
}

static void DoStandaloneErrorGUI(Gui* g) {
    ASSERT(!PRODUCTION_BUILD);

    auto& engine = g->engine;

    auto const host = engine.host;
    auto const floe_ext = (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
    if (!floe_ext) return;

    g->frame_input.graphics_ctx->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto platform = &g->frame_input;
    static bool error_window_open = true;

    bool const there_is_an_error =
        floe_ext->standalone_midi_device_error || floe_ext->standalone_audio_device_error;
    if (error_window_open && there_is_an_error) {
        auto settings = imgui::DefWindow();
        settings.flags |= imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoWidth;
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
    if (floe_ext->standalone_midi_device_error) {
        imgui.frame_output.wants_keyboard_input = true;
        if (platform->modifiers.Get(ModifierKey::Shift)) {
            auto gen_midi_message = [&](bool on, u7 key) {
                if (on)
                    engine.processor.events_for_audio_thread.Push(
                        GuiNoteClicked({.key = key, .velocity = 0.7f}));
                else
                    engine.processor.events_for_audio_thread.Push(GuiNoteClickReleased({.key = key}));
            };

            struct Key {
                KeyCode key;
                u7 midi_key;
            };
            static Key const keys[] = {
                {KeyCode::LeftArrow, 60},
                {KeyCode::RightArrow, 63},
                {KeyCode::UpArrow, 80},
                {KeyCode::DownArrow, 45},
            };

            for (auto& i : keys) {
                if (platform->Key(i.key).presses.size) gen_midi_message(true, i.midi_key);
                if (platform->Key(i.key).releases.size) gen_midi_message(false, i.midi_key);
            }
        }
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

    imgui.graphics->AddTriangleFilled(r.TopRight(),
                                      r.BottomRight(),
                                      r.BottomLeft(),
                                      LiveCol(imgui, UiColMap::WindowResizeCornerBackground));

    auto const line_gap = LiveSize(imgui, UiSizeId::WindowResizeCornerLineGap);
    imgui.graphics->AddLine(r.TopRight() + f32x2 {0, line_gap},
                            r.BottomLeft() + f32x2 {line_gap, 0},
                            LiveCol(imgui, UiColMap::WindowResizeCornerLine));
    imgui.graphics->AddLine(r.TopRight() + f32x2 {0, line_gap * 2},
                            r.BottomLeft() + f32x2 {line_gap * 2, 0},
                            LiveCol(imgui, UiColMap::WindowResizeCornerLine));

    auto const& desc = SettingDescriptor(GuiSetting::WindowWidth);

    auto const id = imgui.GetID("resize_corner");

    g->imgui.ButtonBehavior(r, id, {.left_mouse = true, .triggers_on_mouse_down = true});

    if (g->imgui.IsHotOrActive(id)) g->imgui.frame_output.cursor_type = CursorType::UpLeftDownRight;

    if (g->imgui.IsActive(id)) {
        g->imgui.frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::Animate);

        auto const cursor = g->imgui.frame_input.cursor_pos;
        UiSize32 const ui_size = {
            (u32)Max(cursor.x, 0.0f),
            (u32)Max(cursor.y, 0.0f),
        };
        if (auto const new_size = NearestAspectRatioSizeInsideSize32(ui_size, k_gui_aspect_ratio))
            prefs::SetValue(g->prefs, desc, (s64)new_size->width);
    }
}

GuiFrameResult GuiUpdate(Gui* g) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);
    g->imgui.SetPixelsPerVw(PixelsPerVw(g));

    g->box_system.show_tooltips = prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::ShowTooltips));

    g->frame_output = {};

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

    CheckForFilePickerResults(g->imgui.frame_input,
                              g->file_picker_state,
                              {
                                  .prefs = g->prefs,
                                  .paths = g->shared_engine_systems.paths,
                                  .package_install_jobs = g->engine.package_install_jobs,
                                  .thread_pool = g->shared_engine_systems.thread_pool,
                                  .scratch_arena = g->scratch_arena,
                                  .sample_lib_server = g->shared_engine_systems.sample_library_server,
                                  .engine = g->engine,
                              });

    CreateFontsIfNeeded(g);

    auto& imgui = g->imgui;

    g->waveforms.StartFrame();
    DEFER { g->waveforms.EndFrame(*g->frame_input.graphics_ctx); };

    auto whole_window_sets = imgui::DefMainWindow();
    whole_window_sets.draw_routine_window_background = [&](IMGUI_DRAW_WINDOW_BG_ARGS_TYPES) {};
    imgui.Begin(whole_window_sets);

    g->frame_input.graphics_ctx->PushFont(g->fonts[ToInt(FontType::Body)]);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };

    auto const top_h = LiveSize(imgui, UiSizeId::Top2Height);
    auto const bot_h = LiveSize(imgui, UiSizeId::BotPanelHeight);
    auto const mid_h = g->frame_input.window_size.height - top_h - bot_h;

    auto draw_top_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        auto const top = LiveCol(imgui, UiColMap::TopPanelBackTop);
        auto const bot = LiveCol(imgui, UiColMap::TopPanelBackBot);
        imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), top, top, bot, bot);
    };
    auto draw_mid_window = [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;

        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::MidPanelBack));

        if (!prefs::GetBool(g->prefs, SettingDescriptor(GuiSetting::HighContrastGui))) {
            auto overall_library = LibraryForOverallBackground(g->engine);
            if (overall_library) {
                auto imgs = LibraryImagesFromLibraryId(g, *overall_library, false);
                if (imgs && imgs->background) {
                    auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(*imgs->background);
                    if (tex) {
                        imgui.graphics->AddImage(*tex,
                                                 r.Min(),
                                                 r.Max(),
                                                 {0, 0},
                                                 GetMaxUVToMaintainAspectRatio(*imgs->background, r.size));
                    }
                }
            }
        }

        imgui.graphics->AddLine(r.TopLeft(), r.TopRight(), LiveCol(imgui, UiColMap::MidPanelTopLine));
    };
    auto draw_bot_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::BotPanelBack));
    };

    {
        auto mid_settings = imgui::DefWindow();
        mid_settings.pad_top_left = {};
        mid_settings.pad_bottom_right = {};
        mid_settings.draw_routine_window_background = draw_mid_window;
        mid_settings.flags = 0;

        auto mid_panel_r = Rect {.x = 0, .y = top_h, .w = imgui.Width(), .h = mid_h};
        imgui.BeginWindow(mid_settings, mid_panel_r, "MidPanel");
        MidPanel(g);
        imgui.EndWindow();
    }

    {
        auto sets = imgui::DefWindow();
        sets.draw_routine_window_background = draw_top_window;
        sets.pad_top_left = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadT)};
        sets.pad_bottom_right = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadB)};
        imgui.BeginWindow(sets, {.xywh {0, 0, imgui.Width(), top_h}}, "TopPanel");
        TopPanel(g);
        imgui.EndWindow();
    }

    auto bot_settings = imgui::DefWindow();
    bot_settings.pad_top_left = {8, 8};
    bot_settings.pad_bottom_right = {8, 8};
    bot_settings.draw_routine_window_background = draw_bot_window;
    imgui.BeginWindow(bot_settings, {.xywh {0, top_h + mid_h, imgui.Width(), bot_h}}, "BotPanel");
    BotPanel(g);
    imgui.EndWindow();

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
            };
            context.Init(g->shared_engine_systems.preset_server, g->scratch_arena);
            DEFER { context.Deinit(g->shared_engine_systems.preset_server); };

            DoPreferencesPanel(g->box_system, context, g->preferences_panel_state);
        }

        {
            FeedbackPanelContext context {
                .notifications = g->notifications,
            };
            DoFeedbackPanel(g->box_system, context, g->feedback_panel_state);
        }

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
                InstPickerContext context {
                    .layer = layer_obj,
                    .sample_library_server = g->shared_engine_systems.sample_library_server,
                    .library_images = g->library_images,
                    .engine = g->engine,
                    .unknown_library_icon = UnknownLibraryIcon(g),
                    .notifications = g->notifications,
                    .persistent_store = g->shared_engine_systems.persistent_store,
                };
                context.Init(g->scratch_arena);
                DEFER { context.Deinit(); };

                auto& state = g->inst_picker_state[layer_obj.index];

                auto& floe_state = state.common_state_floe_libraries;
                auto& mirage_state = g->inst_picker_state[layer_obj.index].common_state_mirage_libraries;

                // Bit of a hack. For instruments, we have 2 sets of common state - each state has its own
                // open bool and rectangle. But we want these to always be in sync - they shouldn't be
                // separate. To ensure this, we make copy over the state before showing the popup.
                mirage_state.open = floe_state.open;
                mirage_state.absolute_button_rect = floe_state.absolute_button_rect;

                DoInstPickerPopup(g->box_system, context, state);

                // If the state changed, we need to copy the open state back to the other.
                if (state.tab == InstPickerState::Tab::MirageLibraries) floe_state.open = mirage_state.open;
            }
        }

        {
            PresetPickerContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .preset_server = g->shared_engine_systems.preset_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            DoPresetPicker(g->box_system, context, g->preset_picker_state);
        }

        {
            IrPickerContext context {
                .sample_library_server = g->shared_engine_systems.sample_library_server,
                .library_images = g->library_images,
                .engine = g->engine,
                .unknown_library_icon = UnknownLibraryIcon(g),
                .notifications = g->notifications,
                .persistent_store = g->shared_engine_systems.persistent_store,
            };
            context.Init(g->scratch_arena);
            DEFER { context.Deinit(); };

            DoIrPickerPopup(g->box_system, context, g->ir_picker_state);
        }

        DoNotifications(g->box_system, g->notifications);

        DoPackageInstallNotifications(g->box_system,
                                      g->engine.package_install_jobs,
                                      g->notifications,
                                      g->engine.error_notifications,
                                      g->shared_engine_systems.thread_pool);
    }

    DoWholeEditor(g);
    imgui.End(g->scratch_arena);

    prefs::WriteIfNeeded(g->prefs);

    return g->frame_output;
}
