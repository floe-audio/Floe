// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "common_infrastructure/preferences.hpp"

#include "engine/engine.hpp"
#include "gui/controls/gui_envelope.hpp"
#include "gui/controls/gui_waveform.hpp"
#include "gui/core/gui_file_picker.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_waveform_images.hpp"
#include "gui/debug/gui_developer_panel.hpp"
#include "gui/overlays/gui_confirmation_dialog.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui/panels/gui_bot_panel.hpp"
#include "gui/panels/gui_feedback_panel.hpp"
#include "gui/panels/gui_info_panel.hpp"
#include "gui/panels/gui_inst_browser.hpp"
#include "gui/panels/gui_instance_config_panel.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_layer_subtabbed.hpp"
#include "gui/panels/gui_library_dev_panel.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui/panels/gui_mid_panel.hpp"
#include "gui/panels/gui_midi_cc_panel.hpp"
#include "gui/panels/gui_prefs_panel.hpp"
#include "gui/panels/gui_preset_browser.hpp"
#include "gui/panels/gui_save_preset_panel.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"
#include "gui_framework/renderer.hpp"

struct GuiFrameInput;

struct DraggingFX {
    imgui::Id id {};
    Effect* fx {};
    usize drop_slot {};
    f32x2 relative_grab_point {};
};

struct GuiState : EngineListener {
    NON_COPYABLE(GuiState);
    GuiState(Engine& engine);
    ~GuiState();

    void OnEngineChange() override;

    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator, Kb(512)};

    Fonts fonts;

    PreferencesPanelState preferences_panel_state {};
    InfoPanelState info_panel_state {};
    FeedbackPanelState feedback_panel_state {};
    ConfirmationDialogState confirmation_dialog_state {};
    Notifications notifications {};
    FilePickerState file_picker_state {.data = FilePickerStateType::None};
    Array<InstBrowserState, k_num_layers> inst_browser_state {{
        {.id = HashFnv1a("inst-browser-1")},
        {.id = HashFnv1a("inst-browser-2")},
        {.id = HashFnv1a("inst-browser-3")},
    }};
    IrBrowserState ir_browser_state {};
    SavePresetPanelState save_preset_panel_state {};
    PresetBrowserState preset_browser_state {};
    LibraryDevPanelState library_dev_panel_state {};
    MidiCcPanelState midi_cc_panel_state {};
    InstanceConfigPanelState instance_config_panel_state {};
    bool show_new_version_indicator {};
    BottomPanelState bottom_panel_state {};
    MidPanelState mid_panel_state {};
    MacrosGuiState macros_gui_state {};
    f32x2 curve_map_add_point_click_pos {};

    Engine& engine;
    SharedEngineSystems& shared_engine_systems;
    prefs::Preferences& prefs;

    layout::Context layout = {};
    imgui::Context imgui {scratch_arena};
    DeveloperPanel dev_gui = {imgui, engine};
    GuiBuilder builder {
        .arena = scratch_arena,
        .imgui = imgui,
        .fonts = fonts,
    };

    Array<LayerPanelState, k_num_layers> layer_panel_states;

    WaveformImagesTable waveform_images {};
    Array<WaveformHashDebounce, k_num_layers> waveform_hash_debounce {};
    Optional<ImageID> floe_logo_image {};

    LibraryImagesTable library_images {};

    Optional<DraggingFX> dragging_fx_unit {};
    Optional<DraggingFX> dragging_fx_switch {};

    GuiEnvelopeCursor envelope_voice_cursors[ToInt(GuiEnvelopeType::Count)][k_num_voices] {};

    Optional<ParamIndex> param_text_editor_to_open {};

    TimePoint redraw_counter = {};

    bool timbre_slider_is_held {};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
};

void GuiUpdate(GuiState& g);
