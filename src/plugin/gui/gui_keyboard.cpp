// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_keyboard.hpp"

#include "gui.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"

Optional<KeyboardGuiKeyPressed> KeyboardGui(Gui* g, Rect r, int starting_octave) {
    auto& imgui = g->imgui;

    auto const keyboard = g->engine.processor.notes_currently_held.GetBlockwise();
    auto const& voices_per_midi_note = g->engine.processor.voice_pool.voices_per_midi_note_for_gui;

    auto const col_black_key = LiveCol(imgui, UiColMap::KeyboardBlackKey);
    auto const col_black_key_outline = LiveCol(imgui, UiColMap::KeyboardBlackKeyOutline);
    auto const col_black_key_hover = LiveCol(imgui, UiColMap::KeyboardBlackKeyHover);
    auto const col_black_key_down = LiveCol(imgui, UiColMap::KeyboardBlackKeyDown);
    auto const col_white_key = LiveCol(imgui, UiColMap::KeyboardWhiteKey);
    auto const col_white_key_hover = LiveCol(imgui, UiColMap::KeyboardWhiteKeyHover);
    auto const col_white_key_down = LiveCol(imgui, UiColMap::KeyboardWhiteKeyDown);

    starting_octave += k_octave_default_offset;
    ASSERT(starting_octave >= k_lowest_starting_oct);
    ASSERT(starting_octave <= k_highest_starting_oct);
    int const num_octaves = k_num_octaves_shown;
    int const lowest_note_shown = starting_octave * 12;

    f32 const gap = 1;
    f32 const white_height = r.h;

    auto const black_height = (f32)RoundPositiveFloat(r.h * 0.65f);

    f32 const white_note_width = (r.w / (num_octaves * 7.0f));
    f32 const black_note_width =
        (white_note_width *
         (0.5f * imgui::g_live_edit_values.ui_sizes[ToInt(UiSizeId::MidiKeyboardBlackNoteWidth)] / 100.0f));
    f32 const active_voice_marker_h =
        r.h * (imgui::g_live_edit_values.ui_sizes[ToInt(UiSizeId::MidiKeyboardActiveMarkerH)] / 100.0f);

    f32 const d1 = ((white_note_width * 3) - (black_note_width * 2)) / 3;
    f32 const d2 = ((white_note_width * 4) - (black_note_width * 3)) / 4;

    f32 const black_note_offset[] = {
        (d1), // c#
        (d1 * 2) + black_note_width, // d#
        (white_note_width * 3) + d2, // f#
        (white_note_width * 3) + (d2 * 2) + black_note_width, // g#
        ((white_note_width * 3) + (d2 * 3) + (black_note_width * 2)) // a#
    };
    constexpr int k_white_note_nums[] = {0, 2, 4, 5, 7, 9, 11};
    constexpr int k_black_note_nums[] = {1, 3, 6, 8, 10};

    Optional<KeyboardGuiKeyPressed> result {};

    auto overlay_key = [&](int key, Rect key_rect, UiColMap col_index) {
        auto const num_active_voices = voices_per_midi_note[(usize)key].Load(LoadMemoryOrder::Relaxed);
        if (num_active_voices != 0) {
            auto overlay = colours::FromU32(LiveCol(imgui, col_index));
            overlay.a = (uint8_t)Min(255, overlay.a + (40 * num_active_voices));
            auto overlay_u32 = colours::ToU32(overlay);
            imgui.graphics->AddRectFilled(key_rect.Min(),
                                          f32x2 {key_rect.Right(), key_rect.y + active_voice_marker_h},
                                          overlay_u32);
        }
    };

    imgui.PushID("white");
    for (auto const i : Range(num_octaves * 7)) {
        int const this_white_note = i % 7;
        int const this_octave = i / 7;
        int const this_rel_note = k_white_note_nums[this_white_note] + (this_octave * 12);
        int const this_abs_note = lowest_note_shown + this_rel_note;
        if (this_abs_note > 127) continue;

        Rect key_r;
        key_r.x = (r.x + (f32)i * white_note_width);
        key_r.y = r.y;
        key_r.w = (white_note_width - gap);
        key_r.h = white_height;

        imgui.RegisterAndConvertRect(&key_r);
        auto const id = imgui.GetID((uintptr)i);
        if (!keyboard.Get((usize)this_abs_note)) {
            if (imgui.ButtonBehavior(key_r, id, {.left_mouse = true, .triggers_on_mouse_down = true})) {
                g->midi_keyboard_note_held_with_mouse = CheckedCast<u7>(this_abs_note);
                f32 const rel_yclick_pos = imgui.frame_input.cursor_pos.y - key_r.y;
                result = KeyboardGuiKeyPressed {.is_down = true,
                                                .note = CheckedCast<u7>(this_abs_note),
                                                .velocity = (rel_yclick_pos / key_r.h)};
            }
        }

        uint32_t col = col_white_key;
        if (imgui.IsActive(id) || keyboard.Get((usize)this_abs_note)) col = col_white_key_down;
        if (imgui.IsHot(id)) col = col_white_key_hover;
        imgui.graphics->AddRectFilled(key_r.Min(), key_r.Max(), col);
        overlay_key(this_abs_note, key_r, UiColMap::KeyboardWhiteVoiceOverlay);
    }
    imgui.PopID();

    imgui.PushID("black");
    for (auto const i : Range(num_octaves * 5)) {
        int const this_black_note = i % 5;
        int const this_octave = i / 5;
        int const this_rel_note = k_black_note_nums[this_black_note] + (this_octave * 12);
        int const this_abs_note = lowest_note_shown + this_rel_note;
        if (this_abs_note > 127) continue;

        Rect key_r;
        key_r.x = (f32)RoundPositiveFloat(r.x + black_note_offset[this_black_note] +
                                          ((f32)this_octave * white_note_width * 7));
        key_r.y = r.y;
        key_r.w = (f32)RoundPositiveFloat(black_note_width);
        key_r.h = black_height;

        imgui.RegisterAndConvertRect(&key_r);
        auto const id = imgui.GetID((uintptr)i);
        if (!keyboard.Get((usize)this_abs_note)) {
            if (imgui.ButtonBehavior(key_r, id, {.left_mouse = true, .triggers_on_mouse_down = true})) {
                g->midi_keyboard_note_held_with_mouse = CheckedCast<u7>(this_abs_note);
                f32 const rel_yclick_pos = imgui.frame_input.cursor_pos.y - key_r.y;
                result = KeyboardGuiKeyPressed {.is_down = true,
                                                .note = CheckedCast<u7>(this_abs_note),
                                                .velocity = (rel_yclick_pos / key_r.h)};
            }
        }

        uint32_t col = col_black_key;
        if (imgui.IsActive(id) || keyboard.Get((usize)this_abs_note)) col = col_black_key_down;
        if (imgui.IsHot(id)) col = col_black_key_hover;

        if (col != col_black_key) {
            imgui.graphics->AddRectFilled(key_r.Min(), key_r.Max(), col_black_key_outline);
            key_r.x += 1;
            key_r.w -= 2;
            key_r.h -= 1;
        }
        imgui.graphics->AddRectFilled(key_r.Min(), key_r.Max(), col);
        overlay_key(this_abs_note, key_r, UiColMap::KeyboardBlackVoiceOverlay);
    }
    imgui.PopID();

    if (!imgui.frame_input.mouse_buttons[0].is_down && g->midi_keyboard_note_held_with_mouse) {
        result =
            KeyboardGuiKeyPressed {.is_down = false, .note = g->midi_keyboard_note_held_with_mouse.Value()};
        g->midi_keyboard_note_held_with_mouse = {};
    }
    return result;
}
