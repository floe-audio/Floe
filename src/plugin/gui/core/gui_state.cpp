// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_state.hpp"

#include <IconsFontAwesome6.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/core/gui_frame_context.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_prefs.hpp"
#include "gui/debug/gui_developer_panel.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/overlays/gui_loading_overlay.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui/panels/gui_attribution_panel.hpp"
#include "gui/panels/gui_bot_panel.hpp"
#include "gui/panels/gui_errors_panel.hpp"
#include "gui/panels/gui_feedback_panel.hpp"
#include "gui/panels/gui_info_panel.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_legacy_params_panel.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui/panels/gui_package_install.hpp"
#include "gui/panels/gui_prefs_panel.hpp"
#include "gui/panels/gui_top_panel.hpp"
#include "gui_framework/app_window.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/renderer.hpp"
#include "plugin/plugin.hpp"
#include "sample_lib_server/sample_library_server.hpp"

static void SampleLibraryChanged(GuiState& g, sample_lib::LibraryIdRef library_id) {
    InvalidateLibraryImages(g.library_images, library_id, *GuiIo().in.renderer);
}

static void CreateFontsIfNeeded(FontAtlas& fonts) {
    auto& renderer = *GuiIo().in.renderer;

    if (renderer.font_texture == renderer.invalid_texture) {
        fonts.Clear();

        auto const load_font = [&](BinaryData ttf, f32 font_size, GlyphRanges ranges) {
            font_size *= GuiIo().in.pixels_per_ww;
            FontConfig config {};
            config.font_data_reference_only = true;
            fonts.AddFontFromMemoryTTF((void*)ttf.data, ttf.size, font_size, config, ranges);
        };

        auto const def_ranges = fonts.GetGlyphRangesDefaultAudioPlugin();
        auto const roboto_ttf = EmbeddedRoboto();
        auto const roboto_italic_ttf = EmbeddedRobotoItalic();

        for (auto const font_type : EnumIterator<FontType>()) {
            switch (font_type) {
                case FontType::Body: load_font(roboto_ttf, k_font_body_size, def_ranges); break;
                case FontType::BodyItalic:
                    load_font(roboto_italic_ttf, k_font_body_italic_size, def_ranges);
                    break;
                case FontType::Heading1: load_font(roboto_ttf, k_font_heading1_size, def_ranges); break;
                case FontType::Heading2: load_font(roboto_ttf, k_font_heading2_size, def_ranges); break;
                case FontType::Heading3: load_font(roboto_ttf, k_font_heading3_size, def_ranges); break;
                case FontType::Icons: {
                    auto const icons_ttf = EmbeddedFontAwesome();
                    auto constexpr k_icon_ranges = Array {GlyphRange {ICON_MIN_FA, ICON_MAX_FA}};
                    load_font(icons_ttf, k_font_icons_size, k_icon_ranges);
                    break;
                }
                case FontType::Count: PanicIfReached();
            }
        }

        auto const outcome = renderer.CreateFontTexture(fonts);
        if (outcome.HasError())
            LogError(ModuleName::Gui, "Failed to create font texture: {}", outcome.Error());
    }
}

GuiState::GuiState(Engine& engine)
    : engine(engine)
    , shared_engine_systems(engine.shared_engine_systems)
    , prefs(engine.shared_engine_systems.prefs)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          engine.shared_engine_systems.sample_library_server,
          {
              .error_notifications = engine.error_notifications,
              .result_added_callback = []() {},
              .library_changed_callback =
                  [&gui = *this](sample_lib::LibraryIdRef library_id_ref) {
                      sample_lib::LibraryId lib_id {library_id_ref};
                      gui.main_thread_callbacks.Push([&gui, lib_id]() { SampleLibraryChanged(gui, lib_id); });
                      RequestGuiUpdate(gui.engine.instance_index);
                  },
          })) {
    Trace(ModuleName::Gui);

    ASSERT(!engine.listener);
    engine.listener = this;

    // The GUI has opened, we can check for updates if needed. We don't want to do this before because it has
    // no use until the GUI is open.
    check_for_update::FetchLatestIfNeeded(shared_engine_systems.check_for_update_state);
    shared_engine_systems.StartPollingThreadIfNeeded();
}

GuiState::~GuiState() {
    Shutdown(library_images);
    Shutdown(waveform_images);

    engine.listener = nullptr;

    sample_lib_server::CloseAsyncCommsChannel(engine.shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);
    Trace(ModuleName::Gui);
    if (engine.processor.gui_note_click_state.Load(LoadMemoryOrder::Relaxed).is_held) {
        engine.processor.gui_note_click_state.Store({.is_held = false}, StoreMemoryOrder::Release);
        engine.host.request_process(&engine.host);
    }
}

void GuiState::OnEngineChange() {
    RequestGuiUpdate(engine.instance_index);
    OnEngineStateChange(save_preset_panel_state, engine);
}

bool Tooltip(GuiState& g, imgui::Id id, Rect r, char const* fmt, ...);

static void DoStandaloneErrorGUI(GuiState& g) {
    ASSERT(!PRODUCTION_BUILD);

    auto& engine = g.engine;

    auto const host = engine.host;
    auto const floe_ext = (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
    if (!floe_ext) return;

    g.fonts.Push(ToInt(FontType::Body));
    DEFER { g.fonts.Pop(); };
    auto& imgui = g.imgui;
    static bool error_viewport_open = true;

    bool const there_is_an_error =
        floe_ext->standalone_midi_device_error || floe_ext->standalone_audio_device_error;
    if (error_viewport_open && there_is_an_error) {
        imgui.BeginViewport(
            {
                .padding = {.lrtb = 4},
                .auto_size = true,
            },
            {.xywh {0, 0, 200, 0}},
            "StandaloneErrors");
        DEFER { imgui.EndViewport(); };

        f32 y_pos = 0;
        if (floe_ext->standalone_midi_device_error) {
            DoBasicWhiteText(g.imgui, {.xywh {0, y_pos, 100, 20}}, "No MIDI input");
            y_pos += 20;
        }
        if (floe_ext->standalone_audio_device_error) {
            DoBasicWhiteText(g.imgui, {.xywh {0, y_pos, 100, 20}}, "No audio devices");
            y_pos += 20;
        }
        if (DoBasicTextButton(imgui,
                              imgui::ButtonConfig {},
                              {.xywh {0, y_pos, 100, 20}},
                              imgui.MakeId("closeErr"),
                              "Close"))
            error_viewport_open = false;
    }
}

static void DoResizeCorner(GuiState& g) {
    auto& imgui = g.imgui;
    auto const& frame_input = GuiIo().in;
    auto& frame_output = GuiIo().out;

    auto const corner_size = WwToPixels(14.37f);
    imgui.BeginViewport(
        {
            .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
        },
        Rect {
            .pos = imgui.CurrentVpSize() - corner_size,
            .size = corner_size,
        },
        "resize-corner");
    DEFER { imgui.EndViewport(); };

    auto const r = imgui.RegisterAndConvertRect({.pos = 0, .size = imgui.CurrentVpSize()});

    auto const& desc = SettingDescriptor(GuiPreference::WindowWidth);

    auto const id = imgui.MakeId("resize_corner");

    static f32x2 size_at_start {};
    if (g.imgui.ButtonBehaviour(r, id, imgui::SliderConfig::k_activation_cfg))
        size_at_start = frame_input.window_size.ToFloat2();

    if (g.imgui.IsHotOrActive(id, MouseButton::Left))
        frame_output.wants.cursor_type = CursorType::UpLeftDownRight;

    if (g.imgui.IsActive(id, MouseButton::Left)) {
        frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);

        auto const cursor = frame_input.cursor_pos;
        auto const delta = cursor - frame_input.Mouse(MouseButton::Left).last_press.point;
        auto const desired_new_size = Max(size_at_start + delta, f32x2(0.0f));

        UiSize32 const ui_size {
            (u32)desired_new_size.x,
            (u32)desired_new_size.y,
        };

        if (auto const new_size = NearestAspectRatioSizeInsideSize32(ui_size, k_gui_aspect_ratio))
            prefs::SetValue(g.prefs, desc, (s64)new_size->width);
    }

    imgui.draw_list->AddTriangleFilled(r.TopRight(),
                                       r.BottomRight(),
                                       r.BottomLeft(),
                                       ToU32(Col {.c = Col::Background0, .dark_mode = true}));

    auto const line_col = ToU32(
        Col {.c = imgui.IsHotOrActive(id, MouseButton::Left) ? Col::Text : Col::Overlay2, .dark_mode = true});
    auto const line_gap = WwToPixels(3.55f);
    imgui.draw_list->AddLine(r.TopRight() + f32x2 {0, line_gap},
                             r.BottomLeft() + f32x2 {line_gap, 0},
                             line_col);
    imgui.draw_list->AddLine(r.TopRight() + f32x2 {0, line_gap * 2},
                             r.BottomLeft() + f32x2 {line_gap * 2, 0},
                             line_col);
}

void GuiUpdate(GuiState& g) {
    ZoneScoped;
    ASSERT(g_is_logical_main_thread);

    auto const& frame_input = GuiIo().in;

    BeginFrame(g.library_images);
    BeginFrame(g.builder,
               {
                   .show_tooltips = prefs::GetBool(g.prefs, SettingDescriptor(GuiPreference::ShowTooltips)),
                   .draw_tooltip = DrawOverlayTooltipForRect,
                   .draw_drop_shadow = DrawDropShadow,
               });

    g.show_new_version_indicator =
        check_for_update::ShowNewVersionIndicator(g.shared_engine_systems.check_for_update_state, g.prefs);

    g.scratch_arena.ResetCursorAndConsolidateRegions();

    layout::ReserveItemsCapacity(g.layout, g.scratch_arena, 2048);
    DEFER {
        // We use the scratch arena for the layout, so we can just reset it to zero rather than having to do
        // the deallocations.
        g.layout = {};
    };

    while (auto function = g.main_thread_callbacks.TryPop(g.scratch_arena))
        (*function)();

    GuiFrameContext frame_context;
    DEFER { sample_lib_server::ReleaseAll(frame_context.libraries); };
    {
        auto libs = sample_lib_server::AllLibrariesRetained(g.shared_engine_systems.sample_library_server,
                                                            g.scratch_arena);
        Sort(libs, [](auto const& a, auto const& b) { return a->name < b->name; });
        auto libs_table = sample_lib_server::MakeTable(libs, g.scratch_arena);
        frame_context = {
            .libraries = libs,
            .lib_table = libs_table,
        };
    }

    CheckForFilePickerResults(frame_input,
                              g.file_picker_state,
                              {
                                  .prefs = g.prefs,
                                  .paths = g.shared_engine_systems.paths,
                                  .package_install_jobs = g.engine.package_install_jobs,
                                  .thread_pool = g.shared_engine_systems.thread_pool,
                                  .scratch_arena = g.scratch_arena,
                                  .sample_lib_server = g.shared_engine_systems.sample_library_server,
                                  .preset_server = g.shared_engine_systems.preset_server,
                                  .engine = g.engine,
                              });

    CreateFontsIfNeeded(g.fonts.atlas);

    auto& imgui = g.imgui;

    DynamicArrayBounded<Instrument const*, k_num_layers> available_instruments;
    for (auto const layer_index : Range<u32>(k_num_layers)) {
        auto& l = g.engine.Layer(layer_index);
        dyn::Append(available_instruments, &l.instrument);
    }

    StartFrame(g.waveform_images, *frame_input.renderer);
    DEFER { EndFrame(g.waveform_images, *frame_input.renderer, available_instruments); };

    imgui.BeginFrame(
        {
            .draw_background =
                [](imgui::Context const& imgui) {
                    auto r = imgui.curr_viewport->unpadded_bounds;
                    imgui.draw_list->AddRectFilled(r, 0xff151515);
                },
            .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
        },
        g.fonts);
    DEFER { imgui.EndFrame(); };

    g.fonts.Push(ToInt(FontType::Body));
    DEFER { g.fonts.Pop(); };

    MacroGuiBeginFrame(g);
    DEFER { MacroGuiEndFrame(g); };

    {
        Rect remaining {.pos = 0, .size = frame_input.window_size.ToFloat2()};
        auto const top = rect_cut::CutTop(remaining, Round(WwToPixels(52.61f)));
        auto const bot = rect_cut::CutBottom(remaining, Round(WwToPixels(72.82f)));
        TopPanel(g, top, frame_context);
        MidPanel(g, remaining, frame_context);
        BotPanel(g, bot);
    }

    DoResizeCorner(g);

    if (!PRODUCTION_BUILD && NullTermStringsEqual(g.engine.host.name, k_floe_standalone_host_name))
        DoStandaloneErrorGUI(g);

    DoLegacyParamsPanel(g.builder, g);

    {
        LibraryDevPanelContext context {
            .engine = g.engine,
            .notifications = g.notifications,
        };
        DoLibraryDevPanel(g.builder, context, g.library_dev_panel_state);
    }

    {
        MidiCcPanelContext context {
            .processor = g.engine.processor,
            .prefs = g.prefs,
        };
        DoMidiCcPanel(g.builder, context, g.midi_cc_panel_state);
    }

    {
        PreferencesPanelContext context {
            .prefs = g.prefs,
            .paths = g.shared_engine_systems.paths,
            .sample_lib_server = g.shared_engine_systems.sample_library_server,
            .package_install_jobs = g.engine.package_install_jobs,
            .thread_pool = g.shared_engine_systems.thread_pool,
            .file_picker_state = g.file_picker_state,
            .presets_server = g.shared_engine_systems.preset_server,
        };

        DoPreferencesPanel(g.builder, context, g.preferences_panel_state);
    }

    {
        FeedbackPanelContext context {
            .notifications = g.notifications,
        };
        DoFeedbackPanel(g.builder, context, g.feedback_panel_state);
    }

    DoConfirmationDialog(g.builder, g.confirmation_dialog_state);

    {
        SavePresetPanelContext context {
            .engine = g.engine,
            .file_picker_state = g.file_picker_state,
            .paths = g.shared_engine_systems.paths,
            .prefs = g.prefs,
        };
        DoSavePresetPanel(g.builder, context, g.save_preset_panel_state);
    }

    {
        InfoPanelContext context {
            .server = g.shared_engine_systems.sample_library_server,
            .voice_pool = g.engine.processor.voice_pool,
            .scratch_arena = g.scratch_arena,
            .check_for_update_state = g.shared_engine_systems.check_for_update_state,
            .prefs = g.prefs,
            .libraries =
                sample_lib_server::AllLibrariesRetained(g.shared_engine_systems.sample_library_server,
                                                        g.scratch_arena),
            .error_notifications = g.engine.error_notifications,
            .notifications = g.notifications,
            .confirmation_dialog_state = g.confirmation_dialog_state,
        };
        DEFER { sample_lib_server::ReleaseAll(context.libraries); };

        DoInfoPanel(g.builder, context, g.info_panel_state);
    }

    {
        AttributionPanelContext context {
            .attribution_text = g.engine.attribution_requirements.formatted_text,
        };

        DoAttributionPanel(g.builder, context);
    }

    {
        for (auto& layer_obj : g.engine.processor.layer_processors) {
            imgui.PushId(layer_obj.index);
            DEFER { imgui.PopId(); };
            InstBrowserContext context {
                .layer = layer_obj,
                .sample_library_server = g.shared_engine_systems.sample_library_server,
                .library_images = g.library_images,
                .engine = g.engine,
                .prefs = g.prefs,
                .notifications = g.notifications,
                .persistent_store = g.shared_engine_systems.persistent_store,
                .confirmation_dialog_state = g.confirmation_dialog_state,
                .frame_context = frame_context,
            };

            auto& state = g.inst_browser_state[layer_obj.index];
            DoInstBrowserPopup(g.builder, context, state);
        }
    }

    {
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
        DoPresetBrowser(g.builder, context, g.preset_browser_state);
    }

    {
        IrBrowserContext context {
            .sample_library_server = g.shared_engine_systems.sample_library_server,
            .library_images = g.library_images,
            .engine = g.engine,
            .prefs = g.prefs,
            .notifications = g.notifications,
            .persistent_store = g.shared_engine_systems.persistent_store,
            .confirmation_dialog_state = g.confirmation_dialog_state,
            .frame_context = frame_context,
        };

        DoIrBrowserPopup(g.builder, context, g.ir_browser_state);
    }

    DoLoadingOverlay(g.builder, g.engine.pending_state_change.HasValue());

    {
        auto const notifs =
            Array {&g.engine.error_notifications, &g.shared_engine_systems.error_notifications};
        DoErrorsPanel(g.builder, notifs);
    }

    DoNotifications(g.builder, g.notifications);

    DoPackageInstallNotifications(g.builder,
                                  g.engine.package_install_jobs,
                                  g.notifications,
                                  g.engine.error_notifications,
                                  g.shared_engine_systems.thread_pool);

    DoDeveloperPanel(g.dev_gui);

    prefs::WriteIfNeeded(g.prefs);
}
