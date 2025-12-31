// Copyright 2018-2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/preferences.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_bot_panel.hpp"
#include "gui/gui2_confirmation_dialog_state.hpp"
#include "gui/gui2_feedback_panel_state.hpp"
#include "gui/gui2_info_panel_state.hpp"
#include "gui/gui2_inst_browser_state.hpp"
#include "gui/gui2_ir_browser_state.hpp"
#include "gui/gui2_library_dev_panel.hpp"
#include "gui/gui2_macros.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui/gui2_prefs_panel_state.hpp"
#include "gui/gui2_preset_browser.hpp"
#include "gui/gui2_save_preset_panel.hpp"
#include "gui/gui_library_images.hpp"
#include "gui/gui_modal_windows.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_envelope.hpp"
#include "gui_file_picker.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"
#include "gui_layer.hpp"
#include "gui_waveform_images.hpp"

struct GuiFrameInput;

struct DraggingFX {
    imgui::Id id {};
    Effect* fx {};
    usize drop_slot {};
    f32x2 relative_grab_point {};
};

struct Gui {
    Gui(GuiFrameInput& frame_input, Engine& engine);
    ~Gui();

    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(512)};

    PreferencesPanelState preferences_panel_state {};
    InfoPanelState info_panel_state {};
    bool attribution_panel_open {};
    FeedbackPanelState feedback_panel_state {};
    ConfirmationDialogState confirmation_dialog_state {};
    Notifications notifications {};
    FilePickerState file_picker_state {.data = FilePickerStateType::None};
    Array<InstBrowserState, k_num_layers> inst_browser_state {};
    IrBrowserState ir_browser_state {};
    SavePresetPanelState save_preset_panel_state {};
    PresetBrowserState preset_browser_state {};
    LibraryDevPanelState library_dev_panel_state {};
    bool show_new_version_indicator {};
    BottomPanelState bottom_panel_state {};
    MacrosGuiState macros_gui_state {};

    bool legacy_params_window_open {};

    GuiFrameInput& frame_input;
    GuiFrameResult frame_output;
    Engine& engine;
    SharedEngineSystems& shared_engine_systems;
    prefs::Preferences& prefs;

    layout::Context layout = {};
    imgui::Context imgui {frame_input, frame_output};
    EditorGUI editor = {};
    Fonts fonts {};
    GuiBoxSystem box_system {
        .arena = scratch_arena,
        .imgui = imgui,
        .fonts = fonts,
        .layout = layout,
    };

    layer_gui::LayerLayout layer_gui[k_num_layers] = {};

    WaveformImagesTable waveform_images {};
    Optional<graphics::ImageID> floe_logo_image {};

    LibraryImagesTable library_images {};

    Optional<DraggingFX> dragging_fx_unit {};
    Optional<DraggingFX> dragging_fx_switch {};

    GuiEnvelopeCursor envelope_voice_cursors[ToInt(GuiEnvelopeType::Count)][k_num_voices] {};

    Optional<ParamIndex> param_text_editor_to_open {};

    Optional<u7> midi_keyboard_note_held_with_mouse = {};

    TimePoint redraw_counter = {};

    bool timbre_slider_is_held {};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
};

//
//
//

LibraryImages
LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id, LibraryImagesTypes needed);

Optional<graphics::ImageID> LogoImage(Gui* g);

void GUIPresetLoaded(Gui* g, Engine* a, bool is_first_preset);
GuiFrameResult GuiUpdate(Gui* g);
void TopPanel(Gui* g, f32 height, GuiFrameContext const& frame_context);
void MidPanel(Gui* g, GuiFrameContext const& frame_context);
