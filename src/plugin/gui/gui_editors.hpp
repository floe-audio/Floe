// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stb_image_write.h>

#include "os/filesystem.hpp"

#include "gui.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_framework/gui_live_edit.hpp"

#define FLOE_EDITOR_ENABLED !PRODUCTION_BUILD
constexpr bool k_editor_enabled = FLOE_EDITOR_ENABLED;

static void DoProfileGUI(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto& engine = g->engine;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "AudioDebug");

    EditorReset(&g->editor);
    // NOTE: not really thread-safe to access engine.processor
    auto const max_ms = (f32)engine.processor.audio_processing_context.process_block_size_max /
                        engine.processor.audio_processing_context.sample_rate * 1000.0f;
    EditorText(&g->editor,
               fmt::Format(g->scratch_arena,
                           "FS: {} Block: {} Max MS Allowed: {.3}",
                           engine.processor.audio_processing_context.sample_rate,
                           engine.processor.audio_processing_context.process_block_size_max,
                           max_ms));

    imgui.EndWindow();
}

static void DoAudioDebugPanel(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto& engine = g->engine;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "AudioDebug");

    EditorReset(&g->editor);

    EditorText(&g->editor,
               fmt::Format(g->scratch_arena,
                           "Voices: {}",
                           engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)));
    EditorText(&g->editor,
               fmt::Format(g->scratch_arena,
                           "Master Audio Processing: {}",
                           engine.processor.fx_need_another_frame_of_processing));

    EditorText(&g->editor, fmt::Format(g->scratch_arena, "State diff: {}", engine.state_change_description));
    imgui.EndWindow();
}

static void DoGUIColourEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Colours");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    ColoursGUISliders(&g->editor, search);

    imgui.EndWindow();
}

static void DoGUIColourMapEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Colours");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    static EditorTextInputBuffer colour_search;
    EditorTextInput(&g->editor, "Colour Search:", colour_search);

    static bool show_high_contrast = false;
    if (EditorButton(&g->editor,
                     show_high_contrast ? "Current: On"_s : "Current: Off",
                     "Show colours for high contrast mode"))
        show_high_contrast = !show_high_contrast;

    ColourMapGUIMenus(&g->editor, search, colour_search, show_high_contrast);

    imgui.EndWindow();
}

static void DoGUISizeEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Sizes");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    SizesGUISliders(&g->editor, search);

    imgui.EndWindow();
}

static bool g_show_editor = false;
static bool g_show_editor_on_left = true;

static void TakeScreenshot(Gui* g) {
    for (auto wnd : g->imgui.windows) {
        if (wnd->name == "DebugR") {
            wnd->skip_drawing_this_frame = true;
            break;
        }
    }

    g->frame_input.graphics_ctx->RequestScreenshotImage([g](u8 const* data, int width, int height) {
        auto path = DynamicArray<char>::FromOwnedSpan(
            KnownDirectoryWithSubdirectories(g->scratch_arena,
                                             KnownDirectoryType::Documents,
                                             Array {"Floe"_s, "Screenshots"},
                                             k_nullopt,
                                             {.create = true}),
            g->scratch_arena);

        int num = 1;
        while (true) {
            auto const initial_size = path.size;
            fmt::Append(path, "-{}.jpg", num);
            ++num;

            if (auto o = GetFileType(path); o.HasValue() && o.Value() == FileType::File)
                dyn::Resize(path, initial_size);
            else
                break;
        }

        if (!stbi_write_jpg(dyn::NullTerminated(path), width, height, 3, data, 95)) PanicIfReached();
    });
}

static void DoCommandPanel(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "Commands");
    EditorReset(&g->editor);

    if (EditorButton(&g->editor, "Show Editor", "Show editor: F1")) g_show_editor = !g_show_editor;
    if (EditorButton(&g->editor, "Editor Left", "Editor position left: F2"))
        g_show_editor_on_left = !g_show_editor_on_left;

    if (EditorButton(&g->editor, "Take Screenshot", "Save Screenshot: F3")) TakeScreenshot(g);

    imgui.EndWindow();
}

PUBLIC void DoWholeEditor(Gui* g) {
    if constexpr (!k_editor_enabled) return;
    auto& imgui = g->imgui;

    g->frame_output.wants_text_input = true; // for debug panel open/close

    if (g->frame_input.Key(KeyCode::F1).presses.size) g_show_editor = !g_show_editor;

    if (g_show_editor) {
        if (g->frame_input.Key(KeyCode::F2).presses.size) g_show_editor_on_left = !g_show_editor_on_left;
        auto const half_w = (f32)(int)(imgui.Width() / 2);
        Rect debug_r;
        if (g_show_editor_on_left)
            debug_r = {.xywh {half_w + 1, 0, half_w - 1, imgui.Height()}};
        else
            debug_r = {.xywh {0, 0, half_w - 1, imgui.Height()}};
        imgui.BeginWindow(imgui::DefWindow(), debug_r, "DebugR");

        static String const tab_text[] = {
            "Commands",
            "Audio",
            "Colours",
            "ColMap",
            "Sizes",
            "GUI Dbg",
            "Profile",
        };
        static auto const num_tabs = (int)ArraySize(tab_text);
        static int selected_tab = 0;
        auto tab_h = imgui.graphics->context->CurrentFontSize() * 2;
        for (auto const i : Range(num_tabs)) {
            auto third = imgui.Width() / (f32)num_tabs;
            bool v = i == selected_tab;
            auto id = imgui.GetID(tab_text[i]);
            if (imgui.ToggleButton(imgui::DefToggleButton(),
                                   {.xywh {(f32)i * third, 0, third, tab_h}},
                                   id,
                                   v,
                                   tab_text[i])) {
                selected_tab = i;
            }
        }
        Rect const selected_r = {.xywh {0, tab_h, imgui.Width(), imgui.Height() - tab_h}};
        switch (selected_tab) {
            case 0: DoCommandPanel(g, selected_r); break;
            case 1: DoAudioDebugPanel(g, selected_r); break;
            case 2: DoGUIColourEditor(g, selected_r); break;
            case 3: DoGUIColourMapEditor(g, selected_r); break;
            case 4: DoGUISizeEditor(g, selected_r); break;
            case 5: imgui.DebugWindow(selected_r); break;
            case 6: DoProfileGUI(g, selected_r); break;
            default: PanicIfReached();
        }

        imgui.EndWindow();
    }
}
