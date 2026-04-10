// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_keyboard.hpp"

#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processing_utils/key_range.hpp"

enum class NoteEdge { Left, Right };

enum class DisplayType { Minimal, Full };

struct TopDisplayOptions {
    f32x2 start_pos;
    f32 width;
    s32 starting_octave;
    s8 num_octaves;
    DisplayType display_type;
    f32 strip_height;
    f32 strip_gap;
    f32 text_gap = 0.0f;
};

constexpr bool IsNaturalNote(s32 key_in_octave) {
    constexpr u16 k_natural_key_bitset = 0b101011010101;
    return (k_natural_key_bitset & (1 << (11 - key_in_octave))) != 0;
}

struct KeyboardLayout {
    f32 natural_key_width;
    f32 sharp_key_width;
    f32 sharp_key_x_offset[5];
    f32 keyboard_x;
    f32 keyboard_width;
    u7 lowest_key_shown;
    s8 num_octaves;

    static KeyboardLayout Create(f32 keyboard_x, f32 keyboard_w, s32 starting_octave, s8 num_octaves) {
        KeyboardLayout layout = {};
        layout.keyboard_x = keyboard_x;
        layout.keyboard_width = keyboard_w;
        layout.lowest_key_shown = CheckedCast<u7>((starting_octave + k_octave_default_offset) * 12);
        layout.num_octaves = num_octaves;

        auto const k_natural_key_width_factor = 1.0f / (num_octaves * 7.0f);
        layout.natural_key_width = keyboard_w * k_natural_key_width_factor;
        layout.sharp_key_width = (layout.natural_key_width * (0.5f * 118.52f / 100.0f));

        auto const d1 = ((layout.natural_key_width * 3) - (layout.sharp_key_width * 2)) / 3;
        auto const d2 = ((layout.natural_key_width * 4) - (layout.sharp_key_width * 3)) / 4;

        layout.sharp_key_x_offset[0] = d1; // c#
        layout.sharp_key_x_offset[1] = (d1 * 2) + layout.sharp_key_width; // d#
        layout.sharp_key_x_offset[2] = (layout.natural_key_width * 3) + d2; // f#
        layout.sharp_key_x_offset[3] = (layout.natural_key_width * 3) + (d2 * 2) + layout.sharp_key_width; // g#
        layout.sharp_key_x_offset[4] =
            ((layout.natural_key_width * 3) + (d2 * 3) + (layout.sharp_key_width * 2)); // a#

        return layout;
    }

    Rect NaturalKeyRect(s32 natural_key_index, f32 key_y, f32 key_height) const {
        f32 const gap = 2;
        Rect key_r;
        key_r.x = keyboard_x + (f32)natural_key_index * natural_key_width;
        key_r.y = key_y;
        key_r.w = natural_key_width - gap;
        key_r.h = key_height;
        return key_r;
    }

    Rect SharpKeyRect(s32 sharp_key_index_rel_octave, s32 octave, f32 key_y, f32 key_height) const {
        Rect key_r;
        key_r.x = (f32)RoundPositiveFloat(keyboard_x + sharp_key_x_offset[sharp_key_index_rel_octave] +
                                          ((f32)octave * natural_key_width * 7));
        key_r.y = key_y;
        key_r.w = (f32)RoundPositiveFloat(sharp_key_width);
        key_r.h = key_height;
        return key_r;
    }

    f32 KeyTopEdgeX(u8 midi_key, NoteEdge edge) const {
        if (midi_key < lowest_key_shown) return -1;

        auto const rel_key = (s32)midi_key - lowest_key_shown;
        auto const octave = rel_key / 12;
        auto const key_in_octave = rel_key % 12;

        // The index of the key for its key colour.
        constexpr u8 k_key_color_index[] = {0, 0, 1, 1, 2, 3, 2, 4, 3, 5, 4, 6};

        Rect rect;
        if (IsNaturalNote(key_in_octave)) {
            auto const natural_index = k_key_color_index[key_in_octave];
            rect = NaturalKeyRect((octave * 7) + natural_index, 0, 0);

            // We want the top edge of the key, so for natural keys we need to subtract the necessary
            // cut-out that the adjacent sharp key makes.

            bool left_cutout = false;
            bool right_cutout = false;

            switch (natural_index) {
                case 0: { // C
                    right_cutout = true;
                    break;
                }
                case 1: { // D
                    left_cutout = true;
                    right_cutout = true;
                    break;
                }
                case 2: { // E
                    left_cutout = true;
                    break;
                }
                case 3: { // F
                    right_cutout = true;
                    break;
                }
                case 4: { // G
                    left_cutout = true;
                    right_cutout = true;
                    break;
                }
                case 5: { // A
                    left_cutout = true;
                    right_cutout = true;
                    break;
                }
                case 6: { // B
                    left_cutout = true;
                    break;
                }
            }

            if (right_cutout && edge == NoteEdge::Right && midi_key < 127)
                rect.SetRightByResizing(KeyTopEdgeX(midi_key + 1, NoteEdge::Left));
            else if (left_cutout && edge == NoteEdge::Left)
                rect.x = KeyTopEdgeX(midi_key - 1, NoteEdge::Right);
        } else
            rect = SharpKeyRect(k_key_color_index[key_in_octave], octave, 0, 0);

        if (edge == NoteEdge::Right) return rect.Right();
        return rect.x;
    }
};

static Optional<KeyboardGuiKeyPressed>
InternalKeyboardGui(GuiState& g, Rect r, s32 starting_octave, s8 num_octaves) {
    auto& imgui = g.imgui;

    auto const keyboard = g.engine.processor.notes_currently_held.GetBlockwise();
    auto const& voices_per_midi_key = g.engine.processor.voice_pool.voices_per_midi_note_for_gui;

    auto const col_natural_key_top = Hsl(205, 7, 12);
    auto const col_natural_key_bot = Hsl(205, 7, 16);
    auto const col_natural_key_top_hover = Hsl(205, 7, 18);
    auto const col_natural_key_bot_hover = Hsl(205, 7, 22);
    auto const col_natural_key_divider = Hsl(205, 8, 3);

    auto const col_sharp_rim_top = Hsl(205, 7, 34);
    auto const col_sharp_face_top = Hsl(205, 5, 29);
    auto const col_sharp_face_bot = Hsl(205, 6, 23);
    auto const col_sharp_rim_bot = Hsl(205, 7, 16);

    auto const col_sharp_rim_top_hover = Hsl(205, 7, 40);
    auto const col_sharp_face_top_hover = Hsl(205, 5, 35);
    auto const col_sharp_face_bot_hover = Hsl(205, 6, 29);
    auto const col_sharp_rim_bot_hover = Hsl(205, 7, 22);

    auto const col_pressed_accent = ToU32({.c = Col::Highlight200});

    auto const layout = KeyboardLayout::Create(r.x, r.w, starting_octave, num_octaves);

    f32 const natural_height = r.h;
    auto const sharp_height = (f32)RoundPositiveFloat(r.h * 0.65f);
    f32 const active_voice_marker_h = r.h * (11.93f / 100.0f);

    Optional<KeyboardGuiKeyPressed> result {};

    auto const instance_config = g.engine.processor.instance_config.Load(LoadMemoryOrder::Relaxed);
    auto const keyswitch_note =
        instance_config.reset_keyswitch.HasValue() ? (s32)instance_config.reset_keyswitch.Value() : -1;
    auto const col_keyswitch = ToU32(Col {.c = Col::Blue, .dark_mode = true, .alpha = 200});

    auto const draw_keyswitch_marker = [&](s32 key, Rect key_rect, bool) {
        if (key != keyswitch_note) return;
        f32 const marker_h = Max(3.0f, key_rect.h * 0.08f);
        Rect marker_r {
            .x = key_rect.x,
            .y = key_rect.y + key_rect.h - marker_h,
            .w = key_rect.w,
            .h = marker_h,
        };
        imgui.draw_list->AddRectFilled(marker_r, col_keyswitch);
    };

    auto const overlay_key = [&](s32 key, Rect key_rect, UiColMap col_index) {
        auto const num_active_voices = voices_per_midi_key[(usize)key].Load(LoadMemoryOrder::Relaxed);
        if (num_active_voices != 0) {
            auto overlay = FromU32(LiveCol(col_index));
            overlay.a = (u8)Min(255, overlay.a + (40 * num_active_voices));
            auto overlay_u32 = ToU32(overlay);
            imgui.draw_list->AddRectFilled(key_rect.Min(),
                                           f32x2 {key_rect.Right(), key_rect.y + active_voice_marker_h},
                                           overlay_u32);
        }
    };

    constexpr imgui::ButtonConfig k_click_cfg = {.mouse_button = MouseButton::Left,
                                                 .event = MouseButtonEvent::Down};

    // Draw a backdrop in the divider colour. The 1px gaps between natural keys reveal it as thin
    // dark separators.
    imgui.draw_list->AddRectFilled(imgui.RegisterAndConvertRect(r).ExpandLeft(1).ExpandBottom(1).ExpandTop(1),
                                   col_natural_key_divider);

    imgui.PushId("natural");
    for (auto const i : Range(num_octaves * 7)) {
        s32 const this_natural_key = i % 7;
        s32 const this_octave = i / 7;
        constexpr s32 k_natural_key_nums[] = {0, 2, 4, 5, 7, 9, 11};
        s32 const this_rel_key = k_natural_key_nums[this_natural_key] + (this_octave * 12);
        s32 const this_abs_key = layout.lowest_key_shown + this_rel_key;
        if (this_abs_key > 127) continue;

        auto key_r = layout.NaturalKeyRect(i, r.y, natural_height);

        key_r = imgui.RegisterAndConvertRect(key_r);
        auto const id = imgui.MakeId((u32)i);
        if (!keyboard.Get((usize)this_abs_key)) {
            if (imgui.ButtonBehaviour(key_r, id, k_click_cfg)) {
                f32 const rel_yclick_pos = GuiIo().in.cursor_pos.y - key_r.y;
                result = KeyboardGuiKeyPressed {.is_down = true,
                                                .note = CheckedCast<u7>(this_abs_key),
                                                .velocity = Clamp01(rel_yclick_pos / key_r.h)};
            }
        } else {
            imgui.SetHot(key_r, id);
        }
        if (imgui.WasJustDeactivated(id, k_click_cfg.mouse_button))
            result = KeyboardGuiKeyPressed {.is_down = false, .note = CheckedCast<u7>(this_abs_key)};

        bool const is_down =
            imgui.IsActive(id, k_click_cfg.mouse_button) || keyboard.Get((usize)this_abs_key);
        bool const is_hot = imgui.IsHot(id);
        u32 col_top = col_natural_key_top;
        u32 col_bot = col_natural_key_bot;
        if (is_hot) {
            col_top = col_natural_key_top_hover;
            col_bot = col_natural_key_bot_hover;
        }
        f32 const mid_y = key_r.y + (key_r.h * 0.5f);
        imgui.draw_list->AddRectFilled(f32x2 {key_r.x, key_r.y}, f32x2 {key_r.Right(), mid_y}, col_top);
        imgui.draw_list->AddRectFilledMultiColor(f32x2 {key_r.x, mid_y},
                                                 f32x2 {key_r.Right(), key_r.y + key_r.h},
                                                 col_top,
                                                 col_top,
                                                 col_bot,
                                                 col_bot);
        if (is_down) {
            auto highlight = FromU32(col_pressed_accent);
            highlight.a = 0;
            imgui.draw_list->AddRectFilledMultiColor(f32x2 {key_r.x, key_r.y},
                                                     f32x2 {key_r.Right(), key_r.y + key_r.h},
                                                     ToU32(highlight),
                                                     ToU32(highlight),
                                                     col_pressed_accent,
                                                     col_pressed_accent);
        }
        overlay_key(this_abs_key, key_r, UiColMap::KeyboardNaturalVoiceOverlay);
        draw_keyswitch_marker(this_abs_key, key_r, false);

        if (this_abs_key == keyswitch_note && imgui.IsHot(id)) Tooltip(g, id, key_r, "Reset keyswitch"_s, {});

        // Show the octave number if it's middle-C.
        if (this_abs_key == 60) {
            auto const font = g.fonts.atlas[ToInt(FontType::Body)];

            auto const text_height = font->font_size;
            // Bottom rectangle of the key.
            auto text_r = key_r;
            text_r.y += key_r.h - text_height;
            text_r.h = text_height;
            g.imgui.draw_list->AddTextInRect(text_r,
                                             ToU32(Col {.c = Col::Overlay2, .dark_mode = true}),
                                             "C3",
                                             {
                                                 .justification = TextJustification::Centred,
                                                 .overflow_type = TextOverflowType::AllowOverflow,
                                                 .font_scaling = 0.8f,
                                             });
        }
    }
    imgui.PopId();

    imgui.PushId("sharp");
    for (auto const i : Range(num_octaves * 5)) {
        s32 const this_sharp_key = i % 5;
        s32 const this_octave = i / 5;
        constexpr s32 k_sharp_key_nums[] = {1, 3, 6, 8, 10};
        s32 const this_rel_key = k_sharp_key_nums[this_sharp_key] + (this_octave * 12);
        s32 const this_abs_key = layout.lowest_key_shown + this_rel_key;
        if (this_abs_key > 127) continue;

        auto key_r = layout.SharpKeyRect(this_sharp_key, this_octave, r.y, sharp_height);

        key_r = imgui.RegisterAndConvertRect(key_r);
        auto const id = imgui.MakeId((u32)i);
        if (!keyboard.Get((usize)this_abs_key)) {
            if (imgui.ButtonBehaviour(key_r, id, k_click_cfg)) {
                f32 const rel_yclick_pos = GuiIo().in.cursor_pos.y - key_r.y;
                result = KeyboardGuiKeyPressed {.is_down = true,
                                                .note = CheckedCast<u7>(this_abs_key),
                                                .velocity = Clamp01(rel_yclick_pos / key_r.h)};
            }
        } else {
            imgui.SetHot(key_r, id);
        }
        if (imgui.WasJustDeactivated(id, k_click_cfg.mouse_button))
            result = KeyboardGuiKeyPressed {.is_down = false, .note = CheckedCast<u7>(this_abs_key)};

        bool const is_down =
            imgui.IsActive(id, k_click_cfg.mouse_button) || keyboard.Get((usize)this_abs_key);
        bool const is_hot = imgui.IsHot(id);

        u32 rim_top = col_sharp_rim_top;
        u32 face_top = col_sharp_face_top;
        u32 face_bot = col_sharp_face_bot;
        u32 rim_bot = col_sharp_rim_bot;
        if (is_hot) {
            rim_top = col_sharp_rim_top_hover;
            face_top = col_sharp_face_top_hover;
            face_bot = col_sharp_face_bot_hover;
            rim_bot = col_sharp_rim_bot_hover;
        }
        // border
        imgui.draw_list->AddRectFilled(key_r, col_natural_key_divider);
        key_r.x += 1;
        key_r.w -= 2;
        key_r.h -= 1;

        f32 const rim_top_h = 1.5f;
        f32 const rim_bot_h = WwToPixels(4.0f);
        f32 const face_top_y = key_r.y + rim_top_h;
        f32 const face_bot_y = key_r.y + key_r.h - rim_bot_h;
        imgui.draw_list->AddRectFilled(f32x2 {key_r.x, key_r.y}, f32x2 {key_r.Right(), face_top_y}, rim_top);
        imgui.draw_list->AddRectFilledMultiColor(f32x2 {key_r.x, face_top_y},
                                                 f32x2 {key_r.Right(), face_bot_y},
                                                 face_top,
                                                 face_top,
                                                 face_bot,
                                                 face_bot);
        imgui.draw_list->AddRectFilled(f32x2 {key_r.x, face_bot_y},
                                       f32x2 {key_r.Right(), key_r.y + key_r.h},
                                       rim_bot);
        if (is_down) {
            auto highlight = FromU32(col_pressed_accent);
            highlight.a = 0;
            imgui.draw_list->AddRectFilledMultiColor(f32x2 {key_r.x, key_r.y},
                                                     f32x2 {key_r.Right(), key_r.y + key_r.h},
                                                     ToU32(highlight),
                                                     ToU32(highlight),
                                                     col_pressed_accent,
                                                     col_pressed_accent);
        }
        overlay_key(this_abs_key, key_r, UiColMap::KeyboardSharpVoiceOverlay);
        draw_keyswitch_marker(this_abs_key, key_r, true);

        if (this_abs_key == keyswitch_note && imgui.IsHot(id)) Tooltip(g, id, key_r, "Reset keyswitch"_s, {});
    }
    imgui.PopId();

    return result;
}

static Span<sample_lib::NamedKeyRange> NamedKeyRanges(GuiState& g, u8 layer_index) {
    auto& layer = g.engine.Layer(layer_index);
    auto const sampled_inst = layer.instrument.TryGetFromTag<InstrumentType::Sampler>();
    if (!sampled_inst) return {};
    return (*sampled_inst)->instrument.named_key_ranges;
}

static void RenderTopDisplayContent(GuiState& g, TopDisplayOptions const& options) {
    auto& imgui = g.imgui;

    imgui.PushId("keyboard-strips");
    DEFER { imgui.PopId(); };

    auto const layout = KeyboardLayout::Create(imgui.ViewportPosToWindowPos(options.start_pos.x).x,
                                               options.width,
                                               options.starting_octave,
                                               options.num_octaves);
    auto const highest_key_shown =
        CheckedCast<u7>(Min(layout.lowest_key_shown + (layout.num_octaves * 12) - 1, 127));

    constexpr auto k_line_width = 2.0f;
    constexpr auto k_stopper_width = k_line_width;

    auto const strip_h = WwToPixels(options.strip_height);
    auto const k_strip_gap = WwToPixels(options.strip_gap);
    auto const text_gap = WwToPixels(options.text_gap);

    auto const capsule_cols = Array {
        Rgba(80, 90, 105, 1.0f), // Layer 1 - Cool blue-grey
        Rgba(105, 90, 80, 1.0f), // Layer 2 - Warm orange-grey
        Rgba(100, 85, 100, 1.0f), // Layer 3 - Purple-grey
    };
    auto const line_cols = capsule_cols;

    auto y_pos = options.start_pos.y;

    auto const text_pad_x = WwToPixels(6.0f);

    if (options.display_type == DisplayType::Full) {
        // Title
        g.fonts.Push(ToInt(FontType::Heading2));
        DEFER { g.fonts.Pop(); };
        imgui.draw_list->AddText(imgui.ViewportPosToWindowPos({options.start_pos.x + text_pad_x, y_pos}),
                                 ToU32(Col {.c = Col::Text, .dark_mode = true}),
                                 "Key Ranges");
        y_pos += g.fonts.Current()->font_size + text_gap;
    }

    for (auto const layer_idx : Range<u8>(k_num_layers)) {
        if (g.engine.Layer(layer_idx).instrument_id.tag == InstrumentType::None) continue;

        auto const range_start =
            g.engine.processor.main_params.IntValue<u7>(layer_idx, LayerParamIndex::KeyRangeLow);
        auto const range_finish =
            Max(g.engine.processor.main_params.IntValue<u7>(layer_idx, LayerParamIndex::KeyRangeHigh),
                range_start); // Inclusive.
        auto const range_end = (u8)((int)range_finish + 1); // Exclusive.

        if (options.display_type == DisplayType::Full) {
            auto const font = g.fonts.atlas[ToInt(FontType::Body)];
            auto const text_height = font->font_size;

            f32 x_pos = options.start_pos.x + text_pad_x;

            {
                auto const circle_radius = text_height * 0.3f;
                auto const circle_x = x_pos + circle_radius;
                auto const circle_y = y_pos + (text_height * 0.5f);

                imgui.draw_list->AddCircleFilled(imgui.ViewportPosToWindowPos({circle_x, circle_y}),
                                                 circle_radius,
                                                 capsule_cols[layer_idx]);
                x_pos += circle_radius * 2 + WwToPixels(6.0f);
            }

            {
                auto text_r = Rect {.x = x_pos, .y = y_pos, .w = options.width - x_pos, .h = text_height};
                text_r = imgui.RegisterAndConvertRect(text_r);

                auto layer_text = fmt::Format(g.scratch_arena,
                                              "Layer {}  |  {}",
                                              layer_idx + 1,
                                              g.engine.Layer(layer_idx).InstName());
                imgui.draw_list->AddTextInRect(text_r,
                                               ToU32(Col {.c = Col::Subtext1, .dark_mode = true}),
                                               layer_text,
                                               {
                                                   .justification = TextJustification::Left,
                                                   .overflow_type = TextOverflowType::AllowOverflow,
                                               });
            }

            y_pos += text_height + text_gap;
        }

        auto strip_r = Rect {.x = options.start_pos.x, .y = y_pos, .w = options.width, .h = strip_h};

        y_pos += strip_h;
        y_pos += k_strip_gap;

        strip_r = imgui.RegisterAndConvertRect(strip_r);

        if (options.display_type == DisplayType::Full) {
            auto const strip_id = imgui.MakeId(layer_idx);
            imgui.RegisterRectForMouseTracking(strip_r, false);
            imgui.SetHot(strip_r, strip_id);

            Tooltip(g,
                    strip_id,
                    strip_r,
                    fmt::Format(g.scratch_arena,
                                "Layer {}'s playable range: {} to {}",
                                layer_idx + 1,
                                NoteName(range_start),
                                NoteName(range_finish)),
                    {.ignore_show_tooltips_preference = true});
        }

        auto const container_left = strip_r.x;
        auto const container_right = strip_r.x + options.width;

        auto const strip_y = strip_r.y;
        auto const strip_center_y = strip_y + (strip_h * 0.5f);
        auto const line_y = strip_center_y - (k_line_width * 0.5f);

        auto const layer_start_x = layout.KeyTopEdgeX(range_start, NoteEdge::Left);
        auto const layer_end_x = layout.KeyTopEdgeX(range_finish, NoteEdge::Right);

        auto const line_draw_start = (f32)Round(Max(layer_start_x, container_left));
        auto const line_draw_end = (f32)Round(Min(layer_end_x, container_right));
        auto const line_y_rounded = (f32)RoundPositiveFloat(line_y);

        auto const midi_transpose =
            g.engine.processor.main_params.IntValue<s8>(layer_idx, LayerParamIndex::MidiTranspose);

        auto const capsule_height = (f32)RoundPositiveFloat(strip_h);
        auto const capsule_y = (f32)RoundPositiveFloat(strip_y);
        auto const capsule_radius = capsule_height * 0.5f;

        auto const fade_in =
            g.engine.processor.main_params.IntValue<u8>(layer_idx, LayerParamIndex::KeyRangeLowFade);
        auto const fade_out =
            g.engine.processor.main_params.IntValue<u8>(layer_idx, LayerParamIndex::KeyRangeHighFade);

        if (line_draw_end > line_draw_start) {
            auto const key_at_left_edge = Max(range_start, layout.lowest_key_shown);
            auto const key_at_right_edge = Min(range_finish, highest_key_shown);

            if (!fade_in && !fade_out) {
                imgui.draw_list->AddRectFilled(f32x2 {line_draw_start, line_y_rounded},
                                               f32x2 {line_draw_end, line_y_rounded + k_line_width},
                                               line_cols[layer_idx]);
            } else {
                f32 x_pos = line_draw_start;
                u8 key = key_at_left_edge;
                do {
                    f32 const next_x_pos = Round(layout.KeyTopEdgeX(key + 1, NoteEdge::Left));

                    f32 y_start = line_y_rounded;
                    f32 y_end = line_y_rounded + k_line_width;
                    u4 corner_flags = 0;
                    f32 rounding = 0;

                    f32 extra_offset = 0;

                    for (auto const [named_range_index, named_range] :
                         Enumerate(NamedKeyRanges(g, layer_idx))) {
                        auto const constrained_start =
                            CheckedCast<u7>(Clamp<int>(named_range.key_range.start - midi_transpose,
                                                       range_start,
                                                       range_finish));
                        auto const constrained_end = CheckedCast<u8>(
                            Clamp<int>(named_range.key_range.end - midi_transpose, range_start, range_end));
                        if (constrained_start >= constrained_end) continue;

                        if (key < constrained_start) continue;
                        if (key >= constrained_end) continue;

                        // This segment is within a named range, so we need to draw it as a capsule.
                        y_start = capsule_y;
                        y_end = capsule_y + capsule_height;
                        rounding = capsule_radius;

                        // If the segment is the start of a named range, we need to round the left edge.
                        if (key == constrained_start) corner_flags |= 0b1001;

                        // If the segment is the end of a named range, we need to round the right edge.
                        if (key + 1 == constrained_end) {
                            corner_flags |= 0b0110;
                            extra_offset = 1; // We want a 1px gap so adjacent capsules look good.
                        }

                        // We don't handle the case where there's multiple named ranges on the same key.
                        break;
                    }

                    imgui.draw_list->AddRectFilled(
                        f32x2 {x_pos, y_start},
                        f32x2 {next_x_pos - extra_offset, y_end},
                        WithAlphaU8(line_cols[layer_idx],
                                    (u8)(KeyRangeFadeIn(key, range_start, fade_in) *
                                         KeyRangeFadeOut(key, range_finish, fade_out) * 255.0f)),
                        rounding,
                        corner_flags);
                    x_pos = next_x_pos;
                    ++key;
                } while (key <= key_at_right_edge);
            }
        }

        for (auto const [named_range_index, named_range] : Enumerate(NamedKeyRanges(g, layer_idx))) {
            auto const constrained_start = CheckedCast<u7>(
                Clamp<int>(named_range.key_range.start - midi_transpose, range_start, range_finish));
            auto const constrained_end = CheckedCast<u8>(
                Clamp<int>(named_range.key_range.end - midi_transpose, range_start, range_end));

            if (constrained_start >= constrained_end) continue;

            auto const range_start_x = layout.KeyTopEdgeX(constrained_start, NoteEdge::Left);
            auto const range_end_x = layout.KeyTopEdgeX(constrained_end, NoteEdge::Left) - 1.0f; // 1px gap

            // IMPROVE: this is far more complex than it needs to be.
            // Determine what portion of the capsule is visible and should be drawn
            f32 capsule_start_x;
            f32 capsule_end_x;
            bool should_draw = false;

            if (range_start_x >= 0.0f && range_end_x >= 0.0f) {
                // Both ends visible
                capsule_start_x = range_start_x;
                capsule_end_x = range_end_x;
                should_draw = true;
            } else if (range_start_x >= 0.0f) {
                // Only start visible - draw from start to right edge
                capsule_start_x = range_start_x;
                capsule_end_x = container_right;
                should_draw = true;
            } else if (range_end_x >= 0.0f) {
                // Only end visible - draw from left edge to end
                capsule_start_x = container_left;
                capsule_end_x = range_end_x;
                should_draw = true;
            } else if (constrained_start <= highest_key_shown && constrained_end >= layout.lowest_key_shown) {
                // Region spans entire visible area
                capsule_start_x = container_left;
                capsule_end_x = container_right;
                should_draw = true;
            }

            if (should_draw && capsule_end_x > capsule_start_x) {
                auto const clipped_start_x = (f32)RoundPositiveFloat(Max(capsule_start_x, container_left));
                auto const clipped_end_x = (f32)RoundPositiveFloat(Min(capsule_end_x, container_right));

                if (clipped_end_x > clipped_start_x) {
                    u4 corner_flags = 0;
                    if (range_start_x >= container_left) corner_flags |= 0b1001;
                    if (range_end_x <= container_right) corner_flags |= 0b0110;

                    Rect const capsule_rect {
                        .x = clipped_start_x,
                        .y = capsule_y,
                        .w = clipped_end_x - clipped_start_x,
                        .h = capsule_height,
                    };

                    if (options.display_type == DisplayType::Full) {
                        auto hash = HashInit();
                        HashUpdate(hash, SourceLocationHash());
                        HashUpdate(hash, layer_idx);
                        HashUpdate(hash, named_range_index);

                        auto const capsule_id = imgui.MakeId(hash);
                        imgui.RegisterRectForMouseTracking(capsule_rect, false);
                        imgui.SetHot(capsule_rect, capsule_id);

                        Tooltip(g,
                                capsule_id,
                                capsule_rect,
                                fmt::Format(g.scratch_arena,
                                            "{}: {} to {}. From {} on Layer {}.",
                                            named_range.name,
                                            NoteName(CheckedCast<u7>(named_range.key_range.start)),
                                            NoteName(CheckedCast<u7>(named_range.key_range.end - 1)),
                                            g.engine.Layer(layer_idx).InstName(),
                                            layer_idx + 1),
                                {.ignore_show_tooltips_preference = true});
                    }

                    if (!fade_in && !fade_out) {
                        imgui.draw_list->AddRectFilled(capsule_rect,
                                                       capsule_cols[layer_idx],
                                                       capsule_radius > 1.0f ? capsule_radius : 0.0f,
                                                       corner_flags);
                    }

                    if (options.display_type == DisplayType::Full) {
                        Rect range_text_r {.x = clipped_start_x,
                                           .y = capsule_y,
                                           .w = clipped_end_x - clipped_start_x,
                                           .h = capsule_height};

                        imgui.draw_list->AddTextInRect(range_text_r,
                                                       ToU32(Col {.c = Col::Text, .dark_mode = true}),
                                                       named_range.name,
                                                       {
                                                           .justification = TextJustification::Centred,
                                                           .overflow_type = TextOverflowType::ShowDotsOnRight,
                                                       });
                    }
                }
            }
        }

        {
            auto const stopper_top = (f32)RoundPositiveFloat(strip_y);
            auto const stopper_bottom = (f32)RoundPositiveFloat(strip_y + strip_h);
            auto const chevron_x_delta = WwToPixels(5.0f);

            if (layer_start_x >= container_left) {
                auto const stopper_x = (f32)RoundPositiveFloat(layer_start_x);
                imgui.draw_list->AddRectFilled(f32x2 {stopper_x, stopper_top},
                                               f32x2 {stopper_x + k_stopper_width, stopper_bottom},
                                               line_cols[layer_idx]);
            } else {
                auto const chevron_left_x = (f32)RoundPositiveFloat(container_left);
                auto const chevron_right_x = chevron_left_x + chevron_x_delta;
                auto const chevron_point = f32x2 {chevron_left_x, strip_y + (0.5f * strip_h)};

                imgui.draw_list->AddLine(chevron_point,
                                         f32x2 {chevron_right_x, stopper_top},
                                         line_cols[layer_idx],
                                         k_line_width);
                imgui.draw_list->AddLine(chevron_point,
                                         f32x2 {chevron_right_x, stopper_bottom},
                                         line_cols[layer_idx],
                                         k_line_width);
            }

            if (layer_end_x <= container_right) {
                auto const stopper_x = (f32)Round(layer_end_x);
                imgui.draw_list->AddRectFilled(f32x2 {stopper_x - k_stopper_width, stopper_top},
                                               f32x2 {stopper_x, stopper_bottom},
                                               line_cols[layer_idx]);
            } else {
                auto const chevron_right_x = (f32)RoundPositiveFloat(container_right);
                auto const chevron_left_x = chevron_right_x - chevron_x_delta;
                auto const chevron_point = f32x2 {chevron_right_x, strip_y + (0.5f * strip_h)};

                imgui.draw_list->AddLine(chevron_point,
                                         f32x2 {chevron_left_x, stopper_top},
                                         line_cols[layer_idx],
                                         k_line_width);
                imgui.draw_list->AddLine(chevron_point,
                                         f32x2 {chevron_left_x, stopper_bottom},
                                         line_cols[layer_idx],
                                         k_line_width);
            }
        }
    }
}

constexpr auto k_minimal_strip_height_ww = 6.0f; // Ww units
constexpr auto k_minimal_strip_gap_px = 1.0f; // pixels

static void TopDisplay(GuiState& g, Rect r, s32 starting_octave, s8 num_octaves, Rect keyboard_rect) {
    auto& imgui = g.imgui;

    auto const abs_r = imgui.RegisterAndConvertRect(r);

    auto const id = imgui.MakeId("keyboard-top-display"_s);
    auto const popup_id = imgui.MakeId("keyboard-top-display-popup"_s);
    imgui.RegisterRectForMouseTracking(abs_r, false);
    imgui.SetHot(abs_r, id);

    constexpr auto k_seconds_delay_before_enlarge = 0.1;

    if (imgui.WasJustMadeHot(id))
        GuiIo().out.SetTimedWakeup(SourceLocationHash(), TimePoint::Now() + k_seconds_delay_before_enlarge);

    if (imgui.IsHot(id) && !imgui.IsPopupMenuOpen(popup_id) &&
        imgui.SecondsSpentHot() > k_seconds_delay_before_enlarge)
        imgui.OpenPopupMenu(popup_id, id);

    auto const enlarged_viewport_padding = WwToPixels(4.0f);

    keyboard_rect = imgui.RegisterAndConvertRect(keyboard_rect);
    if (imgui.IsPopupMenuOpen(popup_id)) {
        imgui.BeginViewport(
            {
                .mode = imgui::ViewportMode::PopupMenu,
                .positioning = imgui::ViewportPositioning::AutoPosition,
                .draw_background =
                    [](imgui::Context const& imgui) {
                        imgui.draw_list->AddRectFilled(imgui.curr_viewport->unpadded_bounds,
                                                       ToU32(Col {.c = Col::Background1, .dark_mode = true}),
                                                       WwToPixels(k_corner_rounding));
                    },
                .padding = {.lr = 0, .tb = enlarged_viewport_padding},
                .auto_size = true,
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
            },
            popup_id,
            keyboard_rect,
            "Enlarged keyboard display");
        DEFER { imgui.EndViewport(); };
        RenderTopDisplayContent(g,
                                {
                                    .start_pos = 0,
                                    .width = keyboard_rect.w,
                                    .starting_octave = starting_octave,
                                    .num_octaves = num_octaves,
                                    .display_type = DisplayType::Full,
                                    .strip_height = 18, // Ww units
                                    .strip_gap = 8, // Ww units
                                    .text_gap = 4, // Ww units
                                });

        if (auto const bounds = g.imgui.curr_viewport->unpadded_bounds;
            All(bounds.size > 0.0f) && !bounds.Contains(GuiIo().in.cursor_pos)) {
            imgui.ClosePopupToLevel(0);
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }
    } else {
        RenderTopDisplayContent(g,
                                {
                                    .start_pos = r.pos,
                                    .width = r.w,
                                    .starting_octave = starting_octave,
                                    .num_octaves = num_octaves,
                                    .display_type = DisplayType::Minimal,
                                    .strip_height = k_minimal_strip_height_ww,
                                    .strip_gap = PixelsToWw(k_minimal_strip_gap_px),
                                    .text_gap = 0,
                                });
    }
}

Optional<KeyboardGuiKeyPressed> KeyboardGui(GuiState& g, Rect r, s32 starting_octave, s8 num_octaves) {
    if (auto const num_active_layers = ({
            u8 n = 0;
            for (auto const& layer : g.engine.processor.layer_processors)
                if (layer.instrument_id.tag != InstrumentType::None) n++;
            n;
        })) {
        if (auto const all_default = ({
                bool b = true;
                for (auto const layer_idx : Range<u8>(k_num_layers)) {
                    auto const& named_ranges = NamedKeyRanges(g, layer_idx);
                    auto const range_start =
                        g.engine.processor.main_params.IntValue<u7>(layer_idx, LayerParamIndex::KeyRangeLow);
                    auto const range_finish = g.engine.processor.main_params.IntValue<u7>(
                        layer_idx,
                        LayerParamIndex::KeyRangeHigh); // Inclusive.
                    if (range_start != 0 || range_finish != 127) {
                        b = false;
                        break;
                    }
                    if (named_ranges.size) {
                        b = false;
                        break;
                    }
                }
                b;
            });
            !all_default) {
            auto const top_display_r =
                rect_cut::CutTop(r,
                                 WwToPixels(num_active_layers * k_minimal_strip_height_ww) +
                                     ((num_active_layers - 1) * k_minimal_strip_gap_px));

            TopDisplay(g, top_display_r, starting_octave, num_octaves, r);
        }
    }

    rect_cut::CutTop(r, WwToPixels(4.0f));

    return InternalKeyboardGui(g, r, starting_octave, num_octaves);
}
