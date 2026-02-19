// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_imgui.hpp"

#include <stb_sprintf.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "gui_frame.hpp"
#include "gui_framework/fonts.hpp"

namespace imgui {

// No-op. Used when something we don't care about was clicked (e.g. background).
constexpr Id k_no_op_id = 1;

// Viewport ID of the full size root viewport created when the IMGUI system begins.
constexpr Id k_root_viewport_id = 4;

constexpr f64 k_popup_open_and_close_delay_sec {0.2};
static constexpr f64 k_text_cursor_blink_rate {0.5};
static constexpr f64 k_button_repeat_rate {0.5};

static bool WantsCloseOnEscape(ViewportConfig const& cfg) {
    switch (cfg.mode) {
        case ViewportMode::PopupMenu: return true;
        case ViewportMode::Floating:
        case ViewportMode::Modal: return cfg.close_on_escape;
        case ViewportMode::Contained: return false;
    }
}

// namespace imstring is based on dear imgui code
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
// Modified and adapted to fit the rest of the codebase.
namespace imstring {

int Widen(Char32* buf,
          int buf_size,
          char const* in_text,
          char const* in_text_end,
          char const** in_text_remaining) {
    if (in_text == nullptr) return 0;
    Char32* buf_out = buf;
    Char32* buf_end = buf + buf_size;
    while (buf_out < buf_end - 1 && (!in_text_end || in_text < in_text_end) && *in_text) {
        unsigned int c;
        in_text += Utf8CharacterToUtf32(&c, in_text, in_text_end);
        if (c == 0) break;
        *buf_out++ = (Char32)c;
    }
    *buf_out = 0;
    if (in_text_remaining) *in_text_remaining = in_text;
    return (int)(buf_out - buf);
}

// stb_to_utf8() from github.com/nothings/stb/
static inline int NarrowCharacter(char* buf, int buf_size, unsigned int c) {
    if (c < 0x80) {
        buf[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        if (buf_size < 2) return 0;
        buf[0] = (char)(0xc0 + (c >> 6));
        buf[1] = (char)(0x80 + (c & 0x3f));
        return 2;
    }
    if (c < 0x10000) {
        if (buf_size < 3) return 0;
        buf[0] = (char)(0xe0 + (c >> 12));
        buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[2] = (char)(0x80 + ((c) & 0x3f));
        return 3;
    }
    if (c <= 0x10FFFF) {
        if (buf_size < 4) return 0;
        buf[0] = (char)(0xf0 + (c >> 18));
        buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
        buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[3] = (char)(0x80 + ((c) & 0x3f));
        return 4;
    }
    // Invalid code point, the max Unicode is 0x10FFFF
    return 0;
}

int Narrow(char* out_buf, int out_buf_size, Char32 const* in_text, Char32 const* in_text_end) {
    char* buf_p = out_buf;
    char const* buf_end = out_buf + out_buf_size;
    while (buf_p < buf_end - 1 && (!in_text_end || in_text < in_text_end) && *in_text) {
        auto c = (unsigned int)(*in_text++);
        if (c < 0x80)
            *buf_p++ = (char)c;
        else
            buf_p += NarrowCharacter(buf_p, (int)(buf_end - buf_p - 1), c);
    }
    *buf_p = 0;
    return (int)(buf_p - out_buf);
}

} // namespace imstring

// namespace stb is based on dear imgui code
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT
// Modified and adapted to fit the rest of the codebase.
namespace stb {

#define STB_TEXTEDIT_GETWIDTH_NEWLINE -1.0f

// NOLINTNEXTLINE(readability-identifier-naming)
static int STB_TEXTEDIT_STRINGLEN(const STB_TEXTEDIT_STRING* imgui) { return imgui->textedit_len; }

// NOLINTNEXTLINE(readability-identifier-naming)
static Char32 STB_TEXTEDIT_GETCHAR(STB_TEXTEDIT_STRING* imgui, int idx) {
    return imgui->textedit_text[(usize)idx];
}
// Returns the pixel delta from the x-position of the i'th character to the x-position of the i+1'th char for
// a line of characters starting at character #n (i.e. accounts for kerning with previous char)
// NOLINTNEXTLINE(readability-identifier-naming)
static f32 STB_TEXTEDIT_GETWIDTH(STB_TEXTEDIT_STRING* imgui, int i, int n) {
    (void)i;
    auto c = imgui->textedit_text[(usize)n];
    if (c == '\n') return STB_TEXTEDIT_GETWIDTH_NEWLINE;
    auto font = imgui->draw_list->fonts.Current();
    return font->GetCharAdvance((Char16)c);
}

// NOLINTNEXTLINE(readability-identifier-naming)
static int STB_TEXTEDIT_KEYTOTEXT(int key) { return key >= 0x10000 ? 0 : key; }

// NOLINTNEXTLINE(readability-identifier-naming)
static Char32 const STB_TEXTEDIT_NEWLINE = '\n';

static f32x2 InputTextCalcTextSizeW(Context* imgui,
                                    Char32 const* text_begin,
                                    Char32 const* text_end,
                                    Char32 const** remaining,
                                    f32x2* out_offset,
                                    bool stop_on_new_line) {
    auto font = imgui->draw_list->fonts.Current();
    auto line_height = font->font_size;

    auto text_size = f32x2 {0, 0};
    f32 line_width = 0.0f;

    Char32 const* s = text_begin;
    while (s < text_end) {
        auto c = (unsigned int)(*s++);
        if (c == '\n') {
            text_size.x = Max(text_size.x, line_width);
            text_size.y += line_height;
            line_width = 0.0f;
            if (stop_on_new_line) break;
            continue;
        }
        if (c == '\r') continue;

        f32 const char_width = font->GetCharAdvance((unsigned short)c);
        line_width += char_width;
    }

    if (text_size.x < line_width) text_size.x = line_width;

    if (out_offset)
        // Offset to allow for the possibility of sitting after a trailing newline.
        *out_offset = f32x2 {line_width, text_size.y + line_height};

    if (line_width > 0 || text_size.y == 0.0f) // Whereas size.y will ignore the trailing newline.
        text_size.y += line_height;

    if (remaining) *remaining = s;

    return text_size;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* r, STB_TEXTEDIT_STRING* imgui, int start_index) {
    Char32 const* text = imgui->textedit_text.data;
    Char32 const* text_remaining = nullptr;
    auto size = InputTextCalcTextSizeW(imgui,
                                       text + start_index,
                                       text + imgui->textedit_len,
                                       &text_remaining,
                                       nullptr,
                                       true);

    r->x0 = 0.0f;
    r->x1 = size.x;
    r->baseline_y_delta = size.y;
    r->ymin = 0.0f;
    r->ymax = size.y;
    r->num_chars = (int)(text_remaining - (text + start_index));
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void STB_TEXTEDIT_DELETECHARS(STB_TEXTEDIT_STRING* imgui, int char_pos, int num_to_del) {
    imgui->textedit_len -= num_to_del;

    Char32* dest = imgui->textedit_text.data + char_pos;
    Char32 const* source = imgui->textedit_text.data + char_pos + num_to_del;

    while (Char32 const c = *source++)
        *dest++ = c;
    *dest = '\0';
}

// NOLINTNEXTLINE(readability-identifier-naming)
static bool STB_TEXTEDIT_INSERTCHARS(STB_TEXTEDIT_STRING* imgui, //
                                     int pos,
                                     Char32 const* new_text,
                                     int num_chars) {
    int const textedit_len = imgui->textedit_len;
    ASSERT(pos <= textedit_len);
    if (num_chars + textedit_len + 1 > (int)imgui->textedit_text.size) return false;

    Char32* text = imgui->textedit_text.data;
    if (pos != textedit_len)
        MoveMemory(text + pos + num_chars, text + pos, (usize)(textedit_len - pos) * sizeof(Char32));
    CopyMemory(text + pos, new_text, (usize)num_chars * sizeof(Char32));

    imgui->textedit_len += num_chars;
    imgui->textedit_text[(usize)imgui->textedit_len] = '\0';

    return true;
}

#define STB_TEXTEDIT_K_LEFT      0x10000 // keyboard input to move cursor left
#define STB_TEXTEDIT_K_RIGHT     0x10001 // keyboard input to move cursor right
#define STB_TEXTEDIT_K_UP        0x10002 // keyboard input to move cursor up
#define STB_TEXTEDIT_K_DOWN      0x10003 // keyboard input to move cursor down
#define STB_TEXTEDIT_K_LINESTART 0x10004 // keyboard input to move cursor to start of line
#define STB_TEXTEDIT_K_LINEEND   0x10005 // keyboard input to move cursor to end of line
#define STB_TEXTEDIT_K_TEXTSTART 0x10006 // keyboard input to move cursor to start of text
#define STB_TEXTEDIT_K_TEXTEND   0x10007 // keyboard input to move cursor to end of text
#define STB_TEXTEDIT_K_DELETE    0x10008 // keyboard input to delete selection or character under cursor
#define STB_TEXTEDIT_K_BACKSPACE 0x10009 // keyboard input to delete selection or character left of cursor
#define STB_TEXTEDIT_K_UNDO      0x1000A // keyboard input to perform undo
#define STB_TEXTEDIT_K_REDO      0x1000B // keyboard input to perform redo
#define STB_TEXTEDIT_K_WORDLEFT  0x1000C // keyboard input to move cursor left one word
#define STB_TEXTEDIT_K_WORDRIGHT 0x1000D // keyboard input to move cursor right one word
#define STB_TEXTEDIT_K_PGUP      0x1000E // keyboard input to move cursor up a page
#define STB_TEXTEDIT_K_PGDOWN    0x1000F // keyboard input to move cursor down a page
#define STB_TEXTEDIT_K_SHIFT     0x20000

#define STB_TEXTEDIT_IMPLEMENTATION

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#include <stb_textedit.h>
#pragma clang diagnostic pop

} // namespace stb

struct ScrollbarResult {
    f32 new_scroll_value;
    f32 new_scroll_max;
    ViewportScrollbar bar;
};

static ScrollbarResult Scrollbar(Context& im,
                                 Viewport* viewport,
                                 bool is_vertical,
                                 f32 viewport_y,
                                 f32 viewport_h,
                                 f32 viewport_right,
                                 f32 content_size_y,
                                 f32 y_scroll_value,
                                 f32 y_scroll_max,
                                 f32 cursor_y) {
    auto id = im.MakeId(is_vertical ? "Vert" : "Horz");

    y_scroll_max = ::Max(0.0f, content_size_y - viewport_h);

    if (content_size_y > viewport_h && ((y_scroll_value + viewport_h) > content_size_y))
        y_scroll_value = (f32)(int)(content_size_y - viewport_h);

    auto height_ratio = viewport_h / content_size_y;
    if (height_ratio > 1) height_ratio = 1;
    auto const scrollbar_h = viewport_h * height_ratio;
    f32 const scrollbar_range = viewport_h - scrollbar_h;
    f32 scrollbar_rel_y = (y_scroll_value / y_scroll_max) * scrollbar_range;
    if (scrollbar_range == 0) scrollbar_rel_y = 0;

    Rect scroll_r;
    scroll_r.x = viewport_right + viewport->cfg.scrollbar_padding;
    scroll_r.y = viewport_y + scrollbar_rel_y;
    scroll_r.w = viewport->cfg.scrollbar_width;
    scroll_r.h = scrollbar_h;
    auto scrollbar_bb = Rect {.xywh = {scroll_r.x, viewport_y, viewport->cfg.scrollbar_width, viewport_h}};
    f32* scroll_y = &scroll_r.y;

    if (!is_vertical) {
        f32 w;
        f32 x;

        x = scrollbar_bb.x;
        scrollbar_bb.x = scrollbar_bb.y;
        scrollbar_bb.y = x;
        w = scrollbar_bb.w;
        scrollbar_bb.w = scrollbar_bb.h;
        scrollbar_bb.h = w;

        x = scroll_r.x;
        scroll_r.x = scroll_r.y;
        scroll_r.y = x;
        w = scroll_r.w;
        scroll_r.w = scroll_r.h;
        scroll_r.h = w;

        scroll_y = &scroll_r.x;
    }

    if (scrollbar_range != 0) {
        ButtonConfig const button_cfg {.mouse_button = MouseButton::Left,
                                       .event = MouseButtonEvent::Down,
                                       .is_non_viewport_content = true};
        static f32x2 cached_pos {};
        if (im.ButtonBehaviour(scroll_r, id, button_cfg)) cached_pos.y = cursor_y - *scroll_y;

        if (im.IsActive(id, MouseButton::Left)) {
            auto const new_ypos = (cursor_y - cached_pos.y) - viewport_y;
            scrollbar_rel_y = Clamp(new_ypos, 0.0f, viewport_h - scrollbar_h);
            *scroll_y = viewport_y + scrollbar_rel_y;

            f32 const y_scroll_percent = Map(scrollbar_rel_y, 0, scrollbar_range, 0, 1);
            y_scroll_value = (f32)(int)(y_scroll_percent * y_scroll_max);
        }
    }

    // Cuts all dimensions to integer bounds, but always shrinks the rectangle, never expands it.
    auto const integer_bounds = [](Rect r) {
        auto min = Ceil(r.pos);
        auto max = Floor(r.Max());
        return Rect::FromMinMax(min, max);
    };

    return {
        .new_scroll_value = y_scroll_value,
        .new_scroll_max = y_scroll_max,
        .bar =
            {
                .strip = integer_bounds(scrollbar_bb),
                .handle = integer_bounds(scroll_r),
                .id = id,
            },
    };
}

static Rect CalculateScissorStack(DynamicArray<Rect>& s) {
    Rect r = s[0];
    for (usize i = 1; i < s.size; ++i)
        Rect::Intersection(r, s[i]);
    return r;
}

void Context::OnScissorChanged() const {
    if (scissor_rect_is_active)
        draw_list->SetClipRect(current_scissor_rect.pos, current_scissor_rect.Max());
    else
        draw_list->SetClipRectFullscreen();
}

f32x2 BestPopupPos(Rect base_r, Rect avoid_r, f32x2 viewport_size, PopupJustification justification) {
    auto ensure_bottom_fits = [&](f32x2 pos) {
        auto bottom = pos.y + base_r.h;
        if (bottom < viewport_size.y) {
            return pos;
        } else {
            auto d = viewport_size.y - bottom;
            pos.y += d;
            if (pos.y < 0) pos.y = 0;
            return pos;
        }
    };

    auto ensure_right_fits = [&](f32x2 pos) {
        auto right = pos.x + base_r.w;
        if (right > viewport_size.x) {
            auto d = right - viewport_size.x;
            pos.x -= d;
        }
        return pos;
    };

    auto ensure_left_fits = [](f32x2 pos) {
        if (pos.x < 0) pos.x = 0;
        return pos;
    };

    auto ensure_top_fits = [](f32x2 pos) {
        if (pos.y < 0) pos.y = 0;
        return pos;
    };

    if (justification == PopupJustification::LeftOrRight) {
        auto right_outer_most = avoid_r.Right() + base_r.w;
        if (right_outer_most < viewport_size.x) {
            auto pos = f32x2 {avoid_r.Right(), base_r.y};
            return ensure_bottom_fits(ensure_top_fits(pos));
        }

        auto left_outer_most = avoid_r.x - base_r.w;
        if (left_outer_most >= 0) {
            auto pos = f32x2 {left_outer_most, base_r.y};
            return ensure_bottom_fits(ensure_top_fits(pos));
        }

    } else {
        auto below_outer_most = avoid_r.Bottom() + base_r.h;
        if (below_outer_most < viewport_size.y) {
            auto pos = f32x2 {base_r.x, avoid_r.Bottom()};
            return ensure_right_fits(ensure_left_fits(pos));
        }

        auto above_outer_most = avoid_r.y - base_r.h;
        if (above_outer_most >= 0) {
            auto pos = f32x2 {base_r.x, above_outer_most};
            return ensure_right_fits(ensure_left_fits(pos));
        }

        return BestPopupPos(base_r, avoid_r, viewport_size, PopupJustification::LeftOrRight);
    }

    return {-1, -1};
}

// From dear-imgui.
// Return false to discard a character.
static bool InputTextFilterCharacter(unsigned int* p_char, TextInputConfig cfg) {
    unsigned int c = *p_char;

    if (cfg.multiline && c == '\n') return true;

    if (c < 128 && c != ' ' && !IsPrintableAscii((char)(c & 0xFF))) return false;

    if (c >= 0xE000 && c <= 0xF8FF) // Filter private Unicode range.
        return false;

    if (cfg.chars_decimal || cfg.chars_hexadecimal || cfg.chars_uppercase || cfg.chars_no_blank ||
        cfg.chars_note_names) {
        if (cfg.chars_decimal)
            if (!(c >= '0' && c <= '9') && (c != '.') && (c != '-') && (c != '+') && (c != '*') && (c != '/'))
                return false;

        if (cfg.chars_hexadecimal)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;

        if (cfg.chars_uppercase)
            if (c >= 'a' && c <= 'z') *p_char = (c += (unsigned int)('A' - 'a'));

        if (cfg.chars_no_blank)
            if (IsSpacing((char)c)) return false;

        // Allow 0123456789+-#abcdefgABCDEFG.
        if (cfg.chars_note_names)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'g') && !(c >= 'A' && c <= 'G') && (c != '-') &&
                (c != '+') && (c != '#'))
                return false;
    }

    return true;
}

void Context::PushId(String str) { dyn::Append(id_stack, MakeId(str)); }

void Context::PushId(uintptr num) { dyn::Append(id_stack, MakeId(num)); }

void Context::PopId() { dyn::Pop(id_stack); }

// Simple seeded FNV-1a.
static u64 SeededHash(Span<u8 const> data, u64 seed) {
    auto hash = HashInit();
    for (auto const d : Array {AsBytes(seed), data})
        HashUpdate(hash, d);
    return hash;
}

Id Context::MakeId(String str) const {
    auto const seed = Last(id_stack);
    auto const result = SeededHash(str.ToConstByteSpan(), seed);
    ASSERT(result != k_null_id && result != k_no_op_id);
    return result;
}

Id Context::MakeId(uintptr i) const {
    auto const seed = Last(id_stack);
    auto const result = SeededHash(AsBytes(i), seed);
    ASSERT(result != k_null_id && result != k_no_op_id);
    return result;
}

Context::Context(ArenaAllocator& scratch) : scratch_arena(scratch) {
    dyn::Append(id_stack, 0u);
    PushScissorStack();
    viewports.Reserve(64);
    stb_textedit_initialize_state(&stb_state, true);
    dyn::Resize(textedit_text, Kb(4));
    textedit_text_utf8.Reserve(Kb(4));
}

bool Context::IsRectVisible(Rect r) const {
    Rect const& c = current_scissor_rect;
    return Rect::DoRectsIntersect(r, c);
}

// Hot
bool Context::IsHot(Id id) const { return hot_item == id; }
bool Context::WasJustMadeHot(Id id) const { return IsHot(id) && hot_item_last_frame != hot_item; }
bool Context::WasJustMadeUnhot(Id id) const { return !IsHot(id) && hot_item_last_frame == id; }
bool Context::AnItemIsHot() const { return hot_item != k_null_id; }

// Active
static bool MatchesActive(Context::ActiveItem const& item, Id id, Optional<MouseButton> btn) {
    return item.id == id && (!btn || *btn == item.mouse_button);
}
bool Context::IsActive(Id id, Optional<MouseButton> via_mouse_button) const {
    return MatchesActive(active_item, id, via_mouse_button);
}
bool Context::WasJustActivated(Id id, Optional<MouseButton> via_mouse_button) const {
    return IsActive(id, via_mouse_button) && active_item_last_frame.id != id;
}
bool Context::WasJustDeactivated(Id id, Optional<MouseButton> via_mouse_button) const {
    return active_item.id != id && MatchesActive(active_item_last_frame, id, via_mouse_button);
}
bool Context::AnItemIsActive() const { return active_item.id != k_null_id; }

// Hovered
bool Context::IsHovered(Id id) const { return hovered_item == id; }
bool Context::WasJustHovered(Id id) const { return IsHovered(id) && hovered_item_last_frame != hovered_item; }
bool Context::WasJustUnhovered(Id id) const {
    return !IsHovered(id) && hovered_item_last_frame == hovered_item;
}

void Context::BeginFrame(ViewportConfig cfg, Fonts& fonts) {
    ASSERT_EQ(viewport_stack.size, 0u);
    ASSERT_EQ(current_popup_stack.size, 0u);

    tab_just_used_to_focus = false;
    viewport_just_created = nullptr;
    curr_viewport = nullptr;
    hovered_viewport_last_frame = hovered_viewport;
    hovered_viewport = nullptr;
    hovered_viewport_content = nullptr;
    floating_exclusive_viewport_last_frame = floating_exclusive_viewport;
    floating_exclusive_viewport = nullptr;

    auto const& frame_input = GuiIo().in;

    for (usize i = sorted_viewports.size; i-- > 0;) {
        auto viewport = sorted_viewports[i];
        if (viewport->visible_bounds.Contains(frame_input.cursor_pos)) {
            if (viewport->clipping_rect.Contains(frame_input.cursor_pos)) hovered_viewport_content = viewport;
            hovered_viewport = viewport;
            break;
        }
    }
    dyn::Clear(sorted_viewports);

    if (frame_input.mouse_scroll_delta_in_lines != 0 && hovered_viewport) {
        Viewport* viewport = hovered_viewport;
        Viewport* final_viewport = nullptr;
        while (true) {
            if (viewport->has_scrollbar[1]) {
                final_viewport = viewport;
                break;
            }
            if (viewport == viewport->root_viewport) break;
            viewport = viewport->parent_viewport;
        }
        if (final_viewport) {
            f32 const pixels_per_line = final_viewport->cfg.scroll_line_size > 0
                                            ? final_viewport->cfg.scroll_line_size
                                            : GuiIo().WwToPixels(20.0f);
            f32 const lines = -frame_input.mouse_scroll_delta_in_lines;
            f32 const new_scroll = (lines * pixels_per_line) + final_viewport->scroll_offset.y;
            final_viewport->scroll_offset.y = Round(Clamp(new_scroll, 0.0f, final_viewport->scroll_max.y));
        }
    }

    //
    // Reset stuff
    //

    for (auto& v : viewports) {
        if (!v->active) v->prev_content_size = {};
        v->active = false;
    }

    UpdateExclusiveFocusViewport();

    // Copy over the temp IDs to the actual IDs.
    active_item = temp_active_item;
    hot_item = temp_hot_item;
    hovered_item = temp_hovered_item;
    if (hot_item != k_null_id) {
        if (WasJustMadeHot(hot_item)) time_when_turned_hot = frame_input.current_time;
    } else {
        time_when_turned_hot = TimePoint {};
    }
    keyboard_focus_item = temp_keyboard_focus_item;
    temp_keyboard_focus_item = k_null_id;
    temp_keyboard_focus_item_is_popup = false;

    temp_active_item.just_activated = false;
    temp_hot_item = k_null_id;
    temp_hovered_item = k_null_id;

    if (AnItemIsActive()) {
        if (!GuiIo().in.Mouse(active_item.mouse_button).is_down) ClearActive();
    }

    active_text_input_shown = false;

    if (exclusive_focus_viewport && !active_text_input && WantsCloseOnEscape(exclusive_focus_viewport->cfg)) {
        GuiIo().out.wants.keyboard_keys.Set(ToInt(KeyCode::Escape));
        temp_keyboard_focus_item = exclusive_focus_viewport->id;
        temp_keyboard_focus_item_is_popup = true;
        if (GuiIo().in.Key(KeyCode::Escape).presses.size) {
            switch (exclusive_focus_viewport->cfg.mode) {
                case ViewportMode::Modal: CloseModal(exclusive_focus_viewport->id); break;
                case ViewportMode::PopupMenu: CloseTopPopupOnly(); break;
                case ViewportMode::Contained: PanicIfReached(); break;
                case ViewportMode::Floating: break; // No close lifecycle for floating viewports.
            }
        }
    }

    if (!overlay_draw_list)
        overlay_draw_list = GuiIo().out.draw_list_allocator.Allocate(*frame_input.renderer, fonts);
    overlay_draw_list->BeginDraw();

    BeginViewport(cfg,
                  k_root_viewport_id,
                  Rect {.pos = 0, .size = frame_input.window_size.ToFloat2()},
                  "ApplicationViewport");
}

void Context::EndFrame() {
    EndViewport(); // k_root_viewport_id

    ASSERT_EQ(viewport_stack.size, 0u); // All BeginViewport calls must have an EndViewport.
    ASSERT_EQ(current_popup_stack.size, 0u);

    if (!active_text_input_shown) SetTextInputFocus(k_null_id, {}, false);

    if (debug_show_register_widget_overlay) {
        for (auto& w : GuiIo().out.mouse_tracked_rects) {
            auto col = 0xffff00ff;
            if (w.mouse_over) col = 0xff00ffff;
            overlay_draw_list->AddRect(w.rect, col);
        }
    }

    overlay_draw_list->EndDraw();

    //
    // Flush buffers with sorting
    //

    ASSERT(GuiIo().out.draw_lists.size == 0);

    auto has_been_sorted = Set<Viewport*>::Create(scratch_arena, viewports.size);

    auto const confirm_viewport = [&](Viewport* viewport) {
        auto const viewport_ptr_hash = has_been_sorted.Hash(viewport);
        if (!has_been_sorted.Contains(viewport, viewport_ptr_hash)) {
            dyn::Append(sorted_viewports, viewport);
            dyn::AppendIfNotAlreadyThere(GuiIo().out.draw_lists, viewport->draw_list);
            has_been_sorted.InsertWithoutGrowing(viewport, viewport_ptr_hash);
        }
    };

    // We group all viewports that are root viewports.
    DynamicArray<Viewport*> nesting_roots {scratch_arena};
    for (auto& viewport : viewports)
        if (viewport->active && viewport->root_viewport == viewport) dyn::Append(nesting_roots, viewport);

    // For each of the root viewports, we find all the viewports that are children of them.
    DynamicArray<DynamicArray<Viewport*>> nested_sorting_bins {scratch_arena};
    dyn::AssignRepeated(nested_sorting_bins, nesting_roots.size, scratch_arena);
    for (auto const root : Range(nesting_roots.size)) {
        for (auto& viewport : viewports)
            if (viewport->active && viewport->root_viewport == nesting_roots[root] &&
                viewport->root_viewport != viewport)
                dyn::Append(nested_sorting_bins[root], viewport);
    }

    // For each bin that contains a whole load of unsorted viewports with the same root, we
    // sort them into the correct order.
    for (auto const i : Range(nested_sorting_bins.size)) {
        auto& bin = nested_sorting_bins[i];
        if (!bin.size) continue;
        Sort(bin,
             [](Viewport const* a, Viewport const* b) -> bool { return a->nested_level < b->nested_level; });

        // If it's a floating/modal/popup viewport then we don't want to flush yet in the contained pass.
        if (nesting_roots[i]->cfg.mode != ViewportMode::Contained) continue;
        confirm_viewport(nesting_roots[i]);
        for (auto& viewport : bin)
            confirm_viewport(viewport);
    }

    DynamicArray<Viewport*> floating {scratch_arena};

    for (auto const i : Range(nested_sorting_bins.size)) {
        if (nesting_roots[i]->cfg.mode == ViewportMode::Contained) continue;
        if (Contains(open_modals, nesting_roots[i])) continue;
        if (Contains(open_popups, nesting_roots[i])) continue;
        dyn::AppendIfNotAlreadyThere(floating, nesting_roots[i]);
    }

    for (auto& modal : open_modals) {
        if (modal_just_opened == modal->id) continue;

        for (auto const j : Range(nesting_roots.size)) {
            if (modal == nesting_roots[j]) {
                dyn::AppendIfNotAlreadyThere(floating, nesting_roots[j]);
                break;
            }
        }
    }

    Sort(floating, [](Viewport* a, Viewport* b) { return a->cfg.z_order < b->cfg.z_order; });

    for (auto vp : floating) {
        for (auto const j : Range(nesting_roots.size)) {
            if (vp == nesting_roots[j]) {
                confirm_viewport(nesting_roots[j]);
                for (auto& viewport : nested_sorting_bins[j])
                    confirm_viewport(viewport);
                break;
            }
        }
    }

    // Finally, do the popup viewports in open_popups order.
    for (auto& popup : open_popups) {
        if (DidPopupMenuJustOpen(popup->id)) continue;

        for (auto const j : Range(nesting_roots.size)) {
            if (popup == nesting_roots[j]) {
                confirm_viewport(nesting_roots[j]);
                for (auto& viewport : nested_sorting_bins[j])
                    confirm_viewport(viewport);
                break;
            }
        }
    }

    dyn::Append(GuiIo().out.draw_lists, overlay_draw_list);

    if (GuiIo().in.Mouse(MouseButton::Left).presses.size && temp_active_item.id == k_null_id &&
        temp_hot_item == k_null_id) {
        // Indicate when the mouse is pressed down, but not over anything important.
        SetActive(k_no_op_id, MouseButton::Left);
    }

    // Close popups/modals if clicked outside.
    if (active_item.just_activated) {
        if (open_popups.size && popup_menu_just_opened == k_null_id) {
            auto const popup_clicked =
                (hovered_viewport && hovered_viewport->root_viewport->cfg.mode == ViewportMode::PopupMenu)
                    ? hovered_viewport->root_viewport
                    : nullptr;

            if (popup_clicked != nullptr) {
                for (auto const i : Range(open_popups.size)) {
                    if (popup_clicked == open_popups[i]) {
                        if (i != open_popups.size - 1) ClosePopupToLevel(i + 1);
                        break;
                    }
                }
            } else {
                ClosePopupToLevel(0);
            }
        } else if (open_modals.size && modal_just_opened == k_null_id) {
            if (exclusive_focus_viewport && exclusive_focus_viewport->cfg.mode == ViewportMode::Modal &&
                exclusive_focus_viewport->cfg.close_on_click_outside &&
                (!hovered_viewport || exclusive_focus_viewport != hovered_viewport->root_viewport)) {
                CloseTopModal();
            }
        }
    }

    popup_menu_just_opened = k_null_id;
    modal_just_opened = k_null_id;

    auto& frame_output = GuiIo().out;

    frame_output.wants.text_input = active_text_input != k_null_id;
    frame_output.wants.mouse_capture = AnItemIsActive();
    frame_output.wants.mouse_scroll = true;

    active_item_last_frame = active_item;
    hot_item_last_frame = hot_item;
    hovered_item_last_frame = hovered_item;
    prev_active_text_input = active_text_input;

    if (temp_hot_item != hot_item)
        frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    if (temp_active_item.just_activated) {
        temp_hot_item = k_null_id;
        frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
    if (tab_to_focus_next_input)
        frame_output.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

bool Context::TextInputHasFocus(Id id) const { return active_text_input && active_text_input == id; }

bool Context::TextInputJustFocused(Id id) const {
    return TextInputHasFocus(id) && prev_active_text_input != id;
}

bool Context::TextInputJustUnfocused(Id id) const {
    return !TextInputHasFocus(id) && prev_active_text_input == id;
}

bool Context::SliderBehaviourRange(SliderBehaviourRangeArgs const& args) {
    f32 fraction = MapTo01(args.value, args.min, args.max);
    f32 const default_fraction = MapTo01(args.default_value, args.min, args.max);

    bool const slider_changed = SliderBehaviourFraction({
        .rect_in_window_coords = args.rect_in_window_coords,
        .id = args.id,
        .fraction = fraction,
        .default_fraction = default_fraction,
        .cfg = ({
            auto f = args.cfg;
            // We are now working in the fraction space, so we need to scale the sensitivity too so that
            // it still relates to pixels per step of 1.0.
            f.sensitivity *= Abs(args.max - args.min);
            f;
        }),
    });

    if (slider_changed) args.value = MapFrom01(fraction, args.min, args.max);
    return slider_changed;
}

bool Context::SliderBehaviourFraction(SliderBehaviourFractionArgs const& args) {
    ASSERT(args.fraction >= 0 && args.fraction <= 1);
    ASSERT(args.default_fraction >= 0 && args.default_fraction <= 1);
    f32 const start = args.fraction;

    static f32 val_at_click = 0;
    static f32x2 start_location = {};

    auto const& frame_input = GuiIo().in;

    if (ButtonBehaviour(args.rect_in_window_coords, args.id, SliderConfig::k_activation_cfg)) {
        if ((args.cfg.default_on_modifer) && frame_input.modifiers.Get(ModifierKey::Modifier))
            args.fraction = args.default_fraction;
        val_at_click = args.fraction;
        start_location = frame_input.cursor_pos;
    }

    if (IsActive(args.id, SliderConfig::k_activation_cfg.mouse_button)) {
        f32 sensitivity = args.cfg.sensitivity;
        if (args.cfg.slower_with_shift) {
            if (frame_input.Key(KeyCode::ShiftL).presses.size ||
                frame_input.Key(KeyCode::ShiftR).presses.size) {
                val_at_click = args.fraction;
                start_location = frame_input.cursor_pos;
            }
            if (frame_input.modifiers.Get(ModifierKey::Shift)) sensitivity *= 4;
        }
        if (All(frame_input.cursor_pos != -1)) {
            auto d = frame_input.cursor_pos - start_location;
            d.x = -d.x;
            // Change value regardless of if dragged horizontally or vertically.
            auto distance_from_drag_start = d.x + d.y;
            if (d.x > 0 && d.y > 0) distance_from_drag_start = Sqrt(Pow(d.x, 2.0f) + Pow(d.y, 2.0f));
            if (d.x < 0 && d.y < 0) distance_from_drag_start = -Sqrt(Pow(-d.x, 2.0f) + Pow(-d.y, 2.0f));
            args.fraction = val_at_click - distance_from_drag_start / sensitivity;
        }
    }

    args.fraction = Clamp(args.fraction, 0.0f, 1.0f);
    return start != args.fraction;
}

void Context::SetViewportMinimumAutoSize(f32x2 size) { auto _ = RegisterAndConvertRect({.size = size}); }

f32x2 Context::ViewportPosToWindowPos(f32x2 rel_pos) const {
    return rel_pos + curr_viewport->bounds.pos - curr_viewport->scroll_offset;
}

f32x2 Context::WindowPosToViewportPos(f32x2 window_pos) const {
    return window_pos - curr_viewport->bounds.pos + curr_viewport->scroll_offset;
}

Rect Context::RegisterAndConvertRect(Rect r) {
    if (curr_viewport == nullptr) return r;

    auto const reg =
        [](f32 start, f32 size, f32 comparison_size, f32 content_size, b8x2& is_auto, usize dim) {
            f32 const end = start + size;
            f32 const epsilon = 0.1f;
            if (end > content_size) {
                if (end > comparison_size + epsilon) is_auto[dim] = false;
                return end;
            }
            return content_size;
        };

    f32 const comparison_size_x =
        curr_viewport->cfg.auto_size.x ? curr_viewport->prev_content_size.x : curr_viewport->bounds.w;
    curr_viewport->prev_content_size.x = reg(r.x,
                                             r.w,
                                             comparison_size_x,
                                             curr_viewport->prev_content_size.x,
                                             curr_viewport->contents_was_auto,
                                             0);
    f32 const comparison_size_y =
        curr_viewport->cfg.auto_size.y ? curr_viewport->prev_content_size.y : curr_viewport->bounds.h;
    curr_viewport->prev_content_size.y = reg(r.y,
                                             r.h,
                                             comparison_size_y,
                                             curr_viewport->prev_content_size.y,
                                             curr_viewport->contents_was_auto,
                                             1);

    r.pos = ViewportPosToWindowPos(r.pos);

    // Debug: show boxes around registered rectangles.
    // `overlay_draw_list.AddRect(r, 0xff0000ff);`

    return r;
}

bool Context::RegisterRectForMouseTracking(Rect r_in_window_coords, bool check_intersection) {
    if (exclusive_focus_viewport && curr_viewport->root_viewport != exclusive_focus_viewport) return false;
    if (check_intersection && !Rect::DoRectsIntersect(r_in_window_coords, GetCurrentClipRect())) return false;

    dyn::Append(GuiIo().out.mouse_tracked_rects,
                {
                    .rect = r_in_window_coords,
                    .mouse_over = r_in_window_coords.Contains(GuiIo().in.cursor_pos),
                });

    // overlay_draw_list->AddRect(r_in_window_coords, 0xff0000ff);

    return true;
}

bool Context::RequestKeyboardFocus(Id id) {
    auto const inside_exclusive_focus_viewport = curr_viewport->root_viewport == exclusive_focus_viewport;

    if (!inside_exclusive_focus_viewport && temp_keyboard_focus_item_is_popup) {
        // We can never have focus because there's a popup open and that always has priority.
        return false;
    }

    temp_keyboard_focus_item = id;
    temp_keyboard_focus_item_is_popup = inside_exclusive_focus_viewport;

    return IsKeyboardFocus(id);
}

// When we're in a popup viewport, we want to close children viewports when we hover for a while on an
// item in a parent viewport. This is common GUI behaviour for something like a menu with sub-menus.
static void HandleHoverPopupMenuClosing(Context& imgui, Id id) {
    ASSERT(imgui.exclusive_focus_viewport != nullptr);
    auto const curr = imgui.curr_viewport;
    auto const curr_is_popup = curr->root_viewport->cfg.mode == ViewportMode::PopupMenu;

    if (imgui.IsHot(id) && curr_is_popup && imgui.exclusive_focus_viewport != imgui.hovered_viewport &&
        imgui.current_popup_stack.size < imgui.open_popups.size) {
        auto const next_viewport = imgui.open_popups[imgui.current_popup_stack.size];
        auto const creator_of_next = next_viewport->creator_of_this_popup_menu;

        if (id != creator_of_next) {
            if (imgui.WasJustMadeHot(id))
                GuiIo().out.AddTimedWakeup(GuiIo().in.current_time + k_popup_open_and_close_delay_sec,
                                           "popup close");
            if (imgui.SecondsSpentHot() >= k_popup_open_and_close_delay_sec)
                imgui.ClosePopupToLevel(imgui.current_popup_stack.size);
        }
    }
}

void Context::SetHot(Rect r, Id id, bool32 is_not_viewport_content) {
    if (temp_hovered_item == id) return; // Already called SetHot this frame for this ID.

    if (exclusive_focus_viewport && curr_viewport->root_viewport != exclusive_focus_viewport) return;

    if (curr_viewport != (is_not_viewport_content ? hovered_viewport : hovered_viewport_content)) return;

    if (!r.Contains(GuiIo().in.cursor_pos)) return;

    temp_hovered_item = id;

    // Only allow it if there is not an active item (for example a disallow when a slider is held
    // down).
    if (!AnItemIsActive()) temp_hot_item = id;
}

TextInputResult Context::TextInputBehaviour(TextInputBehaviourArgs const& args) {
    auto const& r = args.rect_in_window_coords;
    auto const& id = args.id;
    auto const& text_unfocused = args.text;
    auto const& placeholder_text = args.placeholder_text;
    auto const& cfg = args.input_cfg;
    auto const& button_cfg = args.button_cfg;

    ASSERT(!(cfg.multiline && cfg.centre_align), "not supported");
    if (cfg.multiline_wordwrap_hack) ASSERT(cfg.multiline);

    TextInputResult result {};

    int const starting_cursor = stb_state.cursor;
    bool reset_cursor = false;

    auto get_rel_click_point = [](f32x2 pos, f32 offset) {
        f32x2 relative_click = GuiIo().in.cursor_pos - pos;
        relative_click -= f32x2 {offset, 0};
        return relative_click;
    };

    bool set_focus = false;
    if (tab_to_focus_next_input) {
        tab_to_focus_next_input = false;
        set_focus = true;
    }

    if (!TextInputHasFocus(id)) {
        if (ButtonBehaviour(r, id, button_cfg)) set_focus = true;
    }

    if (set_focus) {
        SetTextInputFocus(id, text_unfocused, cfg.multiline);
        reset_cursor = true;
    }

    auto copy_selection_to_clipboard = [&]() {
        auto const start = (usize)::Min(stb_state.select_start, stb_state.select_end);
        auto const end = (usize)::Max(stb_state.select_start, stb_state.select_end);
        auto const size = end - start;
        if (!size) return;

        auto& clipboard = GuiIo().out.set_clipboard_text;

        dyn::Resize(clipboard, size * 4); // 1 UTF32 could at most be 4 UTF8 bytes

        dyn::Resize(clipboard,
                    (usize)imstring::Narrow(clipboard.data,
                                            (int)clipboard.size,
                                            textedit_text.data + start,
                                            textedit_text.data + end));
    };

    auto const& frame_input = GuiIo().in;
    auto& frame_output = GuiIo().out;

    if (IsHot(id)) frame_output.wants.cursor_type = CursorType::IBeam;

    if (TextInputHasFocus(id)) {
        RequestKeyboardFocus(id);
        if (IsKeyboardFocus(id) && frame_input.Key(KeyCode::Tab).presses.size && cfg.tab_focuses_next_input &&
            !tab_just_used_to_focus) {
            tab_to_focus_next_input = true;
            tab_just_used_to_focus = true;
            SetTextInputFocus(k_null_id, {}, false);
        }

        if ((active_item.id && active_item.id != id) || (temp_active_item.id && temp_active_item.id != id))
            SetTextInputFocus(k_null_id, {}, false);

        if (IsKeyboardFocus(id) && !cfg.multiline &&
            (frame_input.Key(KeyCode::Enter).presses.size ||
             (cfg.escape_unfocuses && frame_input.Key(KeyCode::Escape).presses.size))) {
            result.enter_pressed = true;
            SetTextInputFocus(k_null_id, {}, false);
        }
    }

    if (!TextInputHasFocus(id)) {
        if (TextInputJustUnfocused(id)) {
            result.text = textedit_text_utf8;
        } else if (text_unfocused.size) {
            result.text = text_unfocused;
        } else {
            result.text = placeholder_text;
            result.is_placeholder = true;
        }
        result.text_pos = TextInputTextPos(result.text, r, cfg, draw_list->fonts);
        return result;
    }

    active_text_input_shown = true;

    auto const initial_textedit_len = textedit_len;
    auto const x_offset = TextInputTextPos(textedit_text_utf8, r, cfg, draw_list->fonts).x - r.pos.x;

    if (ButtonBehaviour(r, id, {.mouse_button = MouseButton::Left, .event = MouseButtonEvent::Down})) {
        auto rel_pos = get_rel_click_point(r.pos, x_offset);
        stb_textedit_click(this, &stb_state, rel_pos.x, rel_pos.y);
        reset_cursor = true;
    }
    if (IsActive(id, button_cfg.mouse_button)) {
        if (!frame_input.mouse_buttons[0].is_down) {
            ClearActive();
        } else if (!WasJustActivated(id, button_cfg.mouse_button)) {
            if (button_cfg.event == MouseButtonEvent::Down) {
                if (frame_input.Mouse(MouseButton::Left).dragging_started) {
                    auto rel_pos = get_rel_click_point(r.pos, x_offset);
                    stb_textedit_click(this, &stb_state, rel_pos.x, rel_pos.y);
                    reset_cursor = true;
                } else if (frame_input.mouse_buttons[0].is_dragging) {
                    auto rel_pos = get_rel_click_point(r.pos, x_offset);
                    stb_textedit_drag(this, &stb_state, rel_pos.x, rel_pos.y);
                }
            }
        }
    }

    if (IsHotOrActive(id, button_cfg.mouse_button)) frame_output.wants.cursor_type = CursorType::IBeam;

    // Select word
    if (!(cfg.select_all_when_opening && TextInputJustFocused(id))) {
        for (auto const press : frame_input.Mouse(MouseButton::Left).presses) {
            if (!press.is_double_click) continue;

            int start = stb_state.cursor;
            for (; start-- > 0;) {
                auto const c = textedit_text.data[start];
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
            }
            ASSERT(start >= -1);
            stb_state.select_start = start + 1;

            int end = stb_state.cursor;
            for (; end < textedit_len; end++) {
                auto const c = textedit_text.data[end];
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
            }
            stb_state.select_end = end;
            break;
        }
    }

    if (IsKeyboardFocus(id)) {
        auto const shift_bit = [](GuiFrameInput::KeyState::Event const& event) -> int {
            return event.modifiers.Get(ModifierKey::Shift) ? STB_TEXTEDIT_K_SHIFT : 0;
        };

        if (auto const backspaces = frame_input.Key(KeyCode::Backspace).presses_or_repeats; backspaces.size) {
            for (auto const& event : backspaces)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_BACKSPACE | shift_bit(event));
            result.buffer_changed = true;
            reset_cursor = true;
        }
        if (auto const deletes = frame_input.Key(KeyCode::Delete).presses_or_repeats; deletes.size) {
            for (auto const& event : deletes)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_DELETE | shift_bit(event));
            result.buffer_changed = true;
            reset_cursor = true;
        }
        if (auto const ends = frame_input.Key(KeyCode::End).presses_or_repeats; ends.size) {
            for (auto const& event : ends)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_LINEEND | shift_bit(event));
            result.buffer_changed = true;
        }
        if (auto const homes = frame_input.Key(KeyCode::Home).presses_or_repeats; homes.size) {
            for (auto const& event : homes)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_LINESTART | shift_bit(event));
            result.buffer_changed = true;
        }
        if (auto const zs = frame_input.Key(KeyCode::Z).presses_or_repeats; zs.size) {
            for (auto const& event : zs)
                if (event.modifiers.Get(ModifierKey::Modifier))
                    stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_UNDO | shift_bit(event));
            result.buffer_changed = true;
        }
        if (auto const ys = frame_input.Key(KeyCode::Y).presses_or_repeats; ys.size) {
            for (auto const& event : ys)
                if (event.modifiers.Get(ModifierKey::Modifier))
                    stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_REDO | shift_bit(event));
            result.buffer_changed = true;
        }
        if (auto const lefts = frame_input.Key(KeyCode::LeftArrow).presses_or_repeats; lefts.size) {
            reset_cursor = true;
            for (auto const event : lefts)
                stb_textedit_key(this,
                                 &stb_state,
                                 (event.modifiers.Get(ModifierKey::Modifier) ? STB_TEXTEDIT_K_WORDLEFT
                                                                             : STB_TEXTEDIT_K_LEFT) |
                                     shift_bit(event));
        }
        if (auto const rights = frame_input.Key(KeyCode::RightArrow).presses_or_repeats; rights.size) {
            reset_cursor = true;
            for (auto const event : rights)
                stb_textedit_key(this,
                                 &stb_state,
                                 (event.modifiers.Get(ModifierKey::Modifier) ? STB_TEXTEDIT_K_WORDRIGHT
                                                                             : STB_TEXTEDIT_K_RIGHT) |
                                     shift_bit(event));
        }
        if (auto const ups = frame_input.Key(KeyCode::UpArrow).presses_or_repeats; ups.size) {
            reset_cursor = true;
            for (auto const event : ups)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_UP | shift_bit(event));
        }
        if (auto const downs = frame_input.Key(KeyCode::DownArrow).presses_or_repeats; downs.size) {
            reset_cursor = true;
            for (auto const event : downs)
                stb_textedit_key(this, &stb_state, STB_TEXTEDIT_K_DOWN | shift_bit(event));
        }
        if (auto const vs = frame_input.Key(KeyCode::V).presses_or_repeats; vs.size) {
            for (auto const event : vs) {
                if (event.modifiers.Get(ModifierKey::Modifier)) {
                    frame_output.wants.clipboard_text_paste = true;
                    break;
                }
            }
        }
        if (auto const cs = frame_input.Key(KeyCode::C).presses_or_repeats; cs.size) {
            for (auto const event : cs)
                if (event.modifiers.Get(ModifierKey::Modifier)) {
                    copy_selection_to_clipboard();
                    break;
                }
        }
        if (auto const xs = frame_input.Key(KeyCode::X).presses_or_repeats; xs.size) {
            for (auto const event : xs)
                if (event.modifiers.Get(ModifierKey::Modifier)) {
                    copy_selection_to_clipboard();
                    stb_textedit_cut(this, &stb_state);
                    result.buffer_changed = true;
                    break;
                }
        }
        if (auto const as = frame_input.Key(KeyCode::A).presses_or_repeats; as.size) {
            for (auto const event : as)
                if (event.modifiers.Get(ModifierKey::Modifier)) {
                    TextInputSelectAll();
                    break;
                }
        }
        if (auto const enters = frame_input.Key(KeyCode::Enter).presses_or_repeats; enters.size) {
            if (cfg.multiline) {
                for (auto event : enters) {
                    if (event.modifiers.flags) continue;
                    result.enter_pressed = true;
                    result.buffer_changed = true;
                    reset_cursor = true;
                    stb_textedit_key(this, &stb_state, (int)'\n');
                }
            }
        }

        if (frame_input.clipboard_text.size) {
            ArenaAllocatorWithInlineStorage<2000> allocator {Malloc::Instance()};
            DynamicArray<Char32> w_text {allocator};
            dyn::Resize(w_text, frame_input.clipboard_text.size + 1);
            dyn::Resize(
                w_text,
                (usize)imstring::Widen(w_text.data,
                                       (int)w_text.size,
                                       frame_input.clipboard_text.data,
                                       frame_input.clipboard_text.data + frame_input.clipboard_text.size,
                                       nullptr));

            stb_textedit_paste(this, &stb_state, w_text.data, (int)w_text.size);
            result.buffer_changed = true;
        }

        if (frame_input.input_utf32_chars.size && !frame_input.modifiers.Get(ModifierKey::Modifier)) {
            for (auto c : frame_input.input_utf32_chars) {
                if (InputTextFilterCharacter(&c, cfg)) {
                    stb_textedit_key(this, &stb_state, (int)c);
                    result.buffer_changed = true;
                    reset_cursor = true;
                }
            }
        }
    }

    auto font = draw_list->fonts.Current();
    auto const font_size = font->font_size;

    if (result.buffer_changed) {
        for (u8 iteration = 0; iteration < 2; ++iteration) {
            dyn::Resize(textedit_text_utf8,
                        (usize)textedit_len * 4); // 1 utf32 could at most be 4 utf8 bytes
            dyn::Resize(textedit_text_utf8,
                        (usize)imstring::Narrow(textedit_text_utf8.data,
                                                (int)textedit_text_utf8.size,
                                                textedit_text.data,
                                                textedit_text.data + textedit_len));

            // Word-wrap for when we add 1 character and it goes over the edge. The string will end up
            // with newlines in it. IMPROVE: this is an absolute hack to just cover the most common case.
            if (cfg.multiline_wordwrap_hack && stb_state.cursor == textedit_len &&
                textedit_len == (initial_textedit_len + 1)) {

                auto const max_width = r.w - (cfg.x_padding * 4);
                if (max_width <= 0) break;

                auto const* line_end = textedit_text_utf8.data + textedit_text_utf8.size - 1;
                auto const* line_start = line_end;
                while (line_start > textedit_text_utf8.data && *line_start != '\n')
                    line_start--;
                if (*line_start == '\n') line_start++;

                if (line_end <= line_start) break;

                auto const line_width =
                    font->CalcTextSize({line_start, (usize)(line_end - line_start)}, {.font_size = font_size})
                        .x;

                if (line_width < max_width) break;

                auto* word_start = line_end;
                while (word_start > line_start && *word_start != ' ')
                    word_start--;
                if (*word_start == ' ') ++word_start;
                if (word_start != line_start) {
                    u32 num_codepoints = 0;
                    for (auto* c = word_start; c <= line_end; c = IncrementUTF8Characters(c, 1))
                        num_codepoints++;
                    stb_state.cursor -= num_codepoints;
                    ASSERT(stb_state.cursor >= 0);
                    stb_textedit_key(this, &stb_state, '\n');
                    stb_state.cursor = textedit_len;
                    continue; // loop again to re-narrow the text
                }
            }
            break;
        }
    }

    result.cursor = stb_state.cursor;
    result.selection_start = ::Min(stb_state.select_start, stb_state.select_end);
    result.selection_end = ::Max(stb_state.select_start, stb_state.select_end);
    result.text = textedit_text_utf8.Items();
    result.text_pos = TextInputTextPos(result.text, r, cfg, draw_list->fonts);

    if (!result.HasSelection()) {
        if (starting_cursor != stb_state.cursor || reset_cursor)
            ResetTextInputCursorAnim();
        else if (GuiIo().WakeupAtTimedInterval(cursor_blink_counter, k_text_cursor_blink_rate))
            text_cursor_is_shown = !text_cursor_is_shown;
    }

    if (text_cursor_is_shown && !result.HasSelection()) {
        constexpr u8 k_cursor_width = 1;
        u32 line_index = 0;
        auto cursor_ptr = result.text.data;
        auto line_start = result.text.data;
        for (auto _ : Range(result.cursor)) {
            if (*cursor_ptr == '\n') {
                line_start = cursor_ptr + 1;
                line_index++;
            }
            cursor_ptr = IncrementUTF8Characters(cursor_ptr, 1);
        }
        ASSERT(cursor_ptr >= line_start);
        auto const text_up_to_cursor = String {line_start, (usize)(cursor_ptr - line_start)};
        ASSERT(!Contains(text_up_to_cursor, '\n'));
        f32 const cursor_start = font->CalcTextSize(text_up_to_cursor, {.font_size = font_size}).x;

        auto const cursor_r = Rect {.x = Round(result.text_pos.x + cursor_start) - k_cursor_width,
                                    .y = result.text_pos.y + (line_index * font_size),
                                    .w = k_cursor_width,
                                    .h = font_size};

        result.cursor_rect = cursor_r;
    }

    // We do this at the end because we might have run stb_click code; we want to override the value set
    // with the whole selection.
    if (TextInputJustFocused(id) && cfg.select_all_when_opening) TextInputSelectAll();

    return result;
}

Optional<Rect> TextInputResult::NextSelectionRect(TextInputResult::SelectionIterator& it) const {
    ASSERT(HasSelection());
    if (it.reached_end) return {};

    auto const& font = *it.imgui.draw_list->fonts.Current();

    if (!it.pos) {
        // First call.
        it.pos = text.data;
        auto line_start = it.pos;

        for (auto _ : Range(selection_start)) {
            if (*it.pos == '\n') {
                line_start = it.pos + 1;
                it.line_index++;
                ++it.pos;
            } else {
                it.pos = IncrementUTF8Characters(it.pos, 1);
            }
        }
        auto const start_pos = it.pos;
        auto const start_line_start = line_start;
        auto const start_line_index = it.line_index;

        it.remaining_chars = (u32)(selection_end - selection_start);

        // We have the start of the selection and the line index. To complete this rect we need to iterate
        // until either the end of the line or the end of the selection.
        for (auto _ : Range(it.remaining_chars)) {
            ASSERT(it.remaining_chars);
            --it.remaining_chars;

            if (*it.pos == '\n') {
                line_start = it.pos + 1;
                it.line_index++;
                ++it.pos;
                break;
            }
            it.pos = IncrementUTF8Characters(it.pos, 1);
        }

        char const* end_pos = it.pos;

        Rect const result = {
            .x = text_pos.x + font.CalcTextSize({start_line_start, (usize)(start_pos - start_line_start)},
                                                {.font_size = font.font_size})
                                  .x,
            .y = text_pos.y - 2 + (start_line_index * font.font_size),
            .w =
                font.CalcTextSize({start_pos, (usize)(end_pos - start_pos)}, {.font_size = font.font_size}).x,
            .h = font.font_size + 4};

        return result;
    }

    if (it.remaining_chars == 0) return k_nullopt;

    auto const start_pos = it.pos;
    auto const start_line_index = it.line_index;

    for (auto _ : Range(it.remaining_chars)) {
        ASSERT(it.remaining_chars);
        --it.remaining_chars;

        if (*it.pos == '\n') {
            it.line_index++;
            ++it.pos;
            break;
        }
        it.pos = IncrementUTF8Characters(it.pos, 1);
    }

    auto const end_pos = it.pos;

    return Rect {
        .x = text_pos.x,
        .y = text_pos.y - 2 + (start_line_index * font.font_size),
        .w = font.CalcTextSize({start_pos, (usize)(end_pos - start_pos)}, {.font_size = font.font_size}).x,
        .h = font.font_size + 4};
}

Context::PopupMenuButtonBehaviourResult
Context::PopupMenuButtonBehaviour(Rect r, Id button_id, Id popup_id, ButtonConfig cfg) {
    auto const button_fired = ButtonBehaviour(r, button_id, cfg);

    if (!current_popup_stack.size) {
        if (button_fired) OpenPopupMenu(popup_id, button_id);
    } else {
        // We're already in a popup viewport. We support auto-opening child popups when hovering. This is
        // common behaviour for quickly navigating through nested menus.
        if (WasJustMadeHot(button_id))
            GuiIo().out.AddTimedWakeup(GuiIo().in.current_time + k_popup_open_and_close_delay_sec,
                                       "Popup open");
        if ((button_fired || (IsHot(button_id) && SecondsSpentHot() >= k_popup_open_and_close_delay_sec)) &&
            !IsPopupMenuOpen(popup_id)) {
            ClosePopupToLevel(current_popup_stack.size);
            OpenPopupMenu(popup_id, button_id);
        }
    }

    return {
        .clicked = button_fired,
        .show_as_active = IsPopupMenuOpen(popup_id) && hovered_viewport != curr_viewport,
    };
}

static bool MatchesModifiers(ModifierFlags required, ModifierFlags actual) {
    return required.IsNone() || required == actual;
}

bool Context::ButtonBehaviour(Rect r, Id id, ButtonConfig cfg) {
    ASSERT(id != k_null_id);
    if constexpr (RUNTIME_SAFETY_CHECKS_ON) ASSERT(All(r.size > 0.0f));
    ASSERT(!(cfg.event == MouseButtonEvent::Up && !cfg.required_modifiers.IsNone()),
           "modifiers for up events are currently not supported");

    auto const mouse_down = GuiIo().in.Mouse(cfg.mouse_button).is_down;

    // If we haven't run ButtonBehaviour on this ID before, track the rectangle. Multiple calls to this
    // function is supported but we don't want to register the same rectangle multiple times.
    if (temp_hot_item != id) RegisterRectForMouseTracking(r);

    // Set the hot/active states if necessary.
    SetHot(r, id, cfg.is_non_viewport_content);
    auto const is_hot = IsHot(id);

    if (IsHot(id) && mouse_down) {
        SetActive(id, cfg.mouse_button);
        int b = 0;
        (void)b;
    }
    auto const is_active = IsActive(id, cfg.mouse_button);

    auto button_fired = ({
        bool fired = false;

        switch (cfg.event) {
            case MouseButtonEvent::Down: {
                if (!mouse_down) break;
                if (cfg.dont_fire_on_double_click && mouse_down->is_double_click) break;
                if (is_hot && MatchesModifiers(cfg.required_modifiers, mouse_down->modifiers)) fired = true;
                break;
            }

            case MouseButtonEvent::DoubleClick: {
                if (!mouse_down) break;
                if (mouse_down->is_double_click && is_hot &&
                    MatchesModifiers(cfg.required_modifiers, mouse_down->modifiers))
                    fired = true;
                break;
            }

            case MouseButtonEvent::Up: {
                if (WasJustDeactivated(id, cfg.mouse_button) && r.Contains(GuiIo().in.cursor_pos))
                    fired = true;
                break;
            }

            case MouseButtonEvent::Count: PanicIfReached();
        }

        fired;
    });

    if (cfg.hold_to_repeat) {
        if (WasJustActivated(id, cfg.mouse_button))
            button_repeat_counter = GuiIo().in.current_time + k_button_repeat_rate;
        else if (is_active) {
            if (GuiIo().WakeupAtTimedInterval(button_repeat_counter, k_button_repeat_rate))
                button_fired = true;
        }
    }

    if (exclusive_focus_viewport && exclusive_focus_viewport->cfg.mode == ViewportMode::PopupMenu)
        HandleHoverPopupMenuClosing(*this, id);

    if (is_hot || is_active) GuiIo().out.wants.cursor_type = cfg.cursor_type;

    if (button_fired && cfg.closes_popup_or_modal) {
        switch (curr_viewport->root_viewport->cfg.mode) {
            case ViewportMode::PopupMenu: CloseAllPopups(); break;
            case ViewportMode::Modal: CloseTopModal(); break;
            case ViewportMode::Contained:
            case ViewportMode::Floating: break;
        }
    }

    return button_fired;
}

bool Context::WasViewportJustCreated(Id id) const {
    return id != k_null_id && viewport_just_created && viewport_just_created->id == id;
}

Viewport* Context::FindOrCreateViewport(Id id) {
    for (auto const i : Range(viewports.size))
        if (id == viewports[i]->id) return viewports[i];

    auto* w = viewport_arena.New<Viewport>();
    viewport_just_created = w;
    w->id = id;
    dyn::Append(viewports, w);
    return Last(viewports);
}

bool Context::WasViewportJustHovered(Id id) const {
    return IsViewportHovered(id) && (hovered_viewport_last_frame == nullptr ||
                                     (hovered_viewport_last_frame && hovered_viewport_last_frame->id != id));
}

bool Context::WasViewportJustUnhovered(Id id) const {
    return !IsViewportHovered(id) && hovered_viewport_last_frame != nullptr &&
           hovered_viewport_last_frame->id == id;
}
bool Context::IsViewportHovered(Viewport* viewport) const { return IsViewportHovered(viewport->id); }
bool Context::IsViewportHovered(Id id) const {
    return hovered_viewport != nullptr && hovered_viewport->id == id;
}

void Context::BeginViewport(ViewportConfig const& cfg, Rect r, String unqiue_name) {
    BeginViewport(cfg, MakeId(unqiue_name), r, unqiue_name);
}

void Context::BeginViewport(ViewportConfig const& cfg, Id id, Rect r, String debug_name) {
    auto viewport = FindOrCreateViewport(id);
    BeginViewport(cfg, viewport, r, debug_name);
}

void Context::BeginViewport(ViewportConfig const& cfg, Viewport* viewport, Rect r, String debug_name) {
    auto const is_floating = cfg.mode != ViewportMode::Contained;
    auto const auto_width = cfg.auto_size.x;
    auto auto_height = cfg.auto_size.y;
    auto const auto_pos = cfg.positioning == ViewportPositioning::AutoPosition;
    auto const no_scroll_x = cfg.scrollbar_visibility.x == ViewportScrollbarVisibility::Never;
    auto const no_scroll_y = cfg.scrollbar_visibility.y == ViewportScrollbarVisibility::Never;
    auto const scrollbar_inside_padding = cfg.scrollbar_inside_padding;

    // For Floating+ParentRelative, convert from viewport-relative to window-relative.
    if (is_floating && cfg.positioning == ViewportPositioning::ParentRelative && curr_viewport)
        r.pos = ViewportPosToWindowPos(r.pos);
    // After this point and the auto-pos/auto-size calculations, all floating viewports
    // have window-relative coords. Contained+ParentRelative remains viewport-relative.
    auto const is_window_coordinates = is_floating || cfg.positioning != ViewportPositioning::ParentRelative;

    ASSERT(r.x >= 0);
    ASSERT(r.y >= 0);

    dyn::Assign(viewport->debug_name, debug_name);
    viewport->active = true;
    viewport->cfg = cfg;

    viewport->prevprev_content_size = viewport->prev_content_size;

    //
    // Auto pos and sizing
    //
    {
        Rect rect_to_avoid = r;
        if (auto_width) {
            r.w = viewport->prev_content_size.x;
            if (r.w != 0) {
                r.w += viewport->cfg.TotalWidthPad();
                if (!auto_height) {
                    bool const needs_yscroll =
                        viewport->prev_content_size.y > (r.h - viewport->cfg.TotalHeightPad());
                    if (needs_yscroll && !scrollbar_inside_padding)
                        r.w += viewport->cfg.scrollbar_padding + viewport->cfg.scrollbar_width;
                }
            }
        }
        if (auto_height) {
            r.h = viewport->prev_content_size.y;
            if (r.h != 0) {
                r.h += viewport->cfg.TotalHeightPad();
                if (!auto_width) {
                    bool const needs_xscroll =
                        viewport->prev_content_size.x > (r.w - viewport->cfg.TotalWidthPad());
                    if (needs_xscroll && !scrollbar_inside_padding)
                        r.h += viewport->cfg.scrollbar_padding + viewport->cfg.scrollbar_width;
                }
            }
        }
        if (auto_pos) {
            f32x2 size = r.size;

            if (!scrollbar_inside_padding) {
                auto const scrollbar_size = viewport->cfg.scrollbar_width + viewport->cfg.scrollbar_padding;

                bool const needs_xscroll = viewport->prev_content_size.x > r.w;
                bool const needs_yscroll = viewport->prev_content_size.y > r.h;

                if (needs_yscroll) size.x += scrollbar_size;
                if (needs_xscroll) size.y += scrollbar_size;
            }

            bool const has_parent_popup = curr_viewport && curr_viewport->cfg.mode == ViewportMode::PopupMenu;

            auto base_r = Rect {.pos = r.pos, .size = size};

            // We want to position next to the parent popup_menu with a little overlap to visually show
            // the z-order layering.
            if (has_parent_popup) {
                rect_to_avoid = curr_viewport->bounds;
                rect_to_avoid.y = 0;
                rect_to_avoid.h = FLT_MAX;
                rect_to_avoid.x += 5;
                rect_to_avoid.w -= 10;

                base_r.y -= viewport->cfg.padding.t;
            }

            auto avoid_r = rect_to_avoid;
            auto window_size = GuiIo().in.window_size.ToFloat2();

            r.pos = BestPopupPos(base_r,
                                 avoid_r,
                                 window_size,
                                 has_parent_popup ? PopupJustification::LeftOrRight
                                                  : PopupJustification::AboveOrBelow);
            r.pos = Trunc(r.pos);
        }
    }

    bool const has_no_width_or_height = r.h == 0 && r.w == 0;

    //
    // Init bounds
    //

    if (!is_window_coordinates && curr_viewport) r = RegisterAndConvertRect(r);
    if (is_window_coordinates && r.Bottom() > (f32)GuiIo().in.window_size.height) {
        r.SetBottomByResizing((f32)GuiIo().in.window_size.height - 1);
        if (!scrollbar_inside_padding) {
            f32 const scrollbar_size = viewport->cfg.scrollbar_width + viewport->cfg.scrollbar_padding;
            r.w += scrollbar_size;
        }
        // IMPROVE: test properly sort what happens when a viewport is bigger than the screen
        auto_height = 0;
    }
    viewport->unpadded_bounds = r;
    viewport->visible_bounds = r;
    viewport->bounds = r;
    if (!has_no_width_or_height) {
        viewport->bounds.pos += f32x2 {viewport->cfg.padding.l, viewport->cfg.padding.t};
        viewport->bounds.size -= viewport->cfg.TotalPadSize();
    }
    auto constexpr k_clipping_expansion = 1.0f;
    viewport->clipping_rect = viewport->bounds.Expanded(k_clipping_expansion);

    //
    // Handle parent
    //

    viewport->parent_viewport = is_floating ? nullptr : curr_viewport;
    viewport->root_viewport = is_floating || !curr_viewport ? viewport : curr_viewport->root_viewport;

    if (viewport->parent_viewport) {
        Rect& vb = viewport->visible_bounds;
        Rect const& parent_clipping_r = viewport->parent_viewport->clipping_rect;
        vb.w = ::Min(parent_clipping_r.Right(), vb.Right()) - vb.x;

        f32 const bottom_of_parent = viewport->parent_viewport->clipping_rect.Bottom();
        f32 const bottom_of_this = viewport->visible_bounds.Bottom();
        if (bottom_of_parent < bottom_of_this)
            viewport->visible_bounds.h = bottom_of_parent - viewport->visible_bounds.y;

        viewport->root_viewport->child_nesting_counter = ({
            u16 v;
            if (__builtin_add_overflow(viewport->root_viewport->child_nesting_counter, 1, &v)) [[unlikely]]
                Panic("viewport nesting too deep");
            v;
        });
        viewport->nested_level = viewport->root_viewport->child_nesting_counter;

        viewport->draw_list = viewport->root_viewport->owned_draw_list;
        ASSERT(viewport->draw_list);
    } else {
        viewport->child_nesting_counter = 0;
        viewport->nested_level = 0;

        if (!viewport->owned_draw_list)
            viewport->owned_draw_list = GuiIo().out.draw_list_allocator.Allocate(overlay_draw_list->renderer,
                                                                                 overlay_draw_list->fonts);
        viewport->draw_list = viewport->owned_draw_list;
    }

    if (viewport->draw_list == viewport->owned_draw_list) viewport->draw_list->BeginDraw();
    draw_list = viewport->draw_list;

    curr_viewport = viewport;
    if (cfg.mode == ViewportMode::Floating && cfg.exclusive_focus) {
        if (!floating_exclusive_viewport)
            floating_exclusive_viewport = viewport;
        else if (cfg.z_order >= floating_exclusive_viewport->cfg.z_order)
            floating_exclusive_viewport = viewport;
    }
    dyn::Append(viewport_stack, viewport);

    //
    // > Scrollbars and background
    //
    PushId(viewport->id);
    {
        if (viewport->root_viewport == viewport) PushScissorStack();
        DEFER {
            if (viewport->root_viewport == viewport) PopScissorStack();
        };

        PushRectToCurrentScissorStack(viewport->visible_bounds.Expanded(k_clipping_expansion));
        DEFER { PopRectFromCurrentScissorStack(); };

        ViewportScrollbars scrollbar_bounds {};

        f32 const scrollbar_size =
            !scrollbar_inside_padding ? viewport->cfg.scrollbar_width + viewport->cfg.scrollbar_padding : 0;
        Rect bounds_for_scrollbar = viewport->bounds;
        f32 const epsilon = 0.01f;

        for (auto const i : Range(2uz)) {
            viewport->has_scrollbar[i] =
                viewport->prev_content_size[i] > (bounds_for_scrollbar.size[i] + epsilon) &&
                !viewport->contents_was_auto[i];

            if (cfg.scrollbar_visibility[i] == ViewportScrollbarVisibility::Always) {
                if (!viewport->has_scrollbar[i]) viewport->scroll_offset[i] = 0;
                viewport->has_scrollbar[i] = true;
            }
        }

        if (viewport->has_scrollbar.y) viewport->clipping_rect.h -= 2;

        if (viewport->has_scrollbar.y && !viewport->has_scrollbar.x) {
            bounds_for_scrollbar.w -= scrollbar_size;

            if (viewport->prev_content_size.x > bounds_for_scrollbar.w && !no_scroll_x) {
                if (!viewport->contents_was_auto.x) {
                    viewport->has_scrollbar.x = true;
                    bounds_for_scrollbar.h -= scrollbar_size;
                }
            }
        } else if (viewport->has_scrollbar.x && !viewport->has_scrollbar.y) {
            bounds_for_scrollbar.h -= scrollbar_size;

            if (viewport->prev_content_size.y > bounds_for_scrollbar.h) {
                if (!viewport->contents_was_auto.y) {
                    viewport->has_scrollbar.y = true;
                    bounds_for_scrollbar.w -= scrollbar_size;
                }
            }
        } else if (viewport->has_scrollbar.x && viewport->has_scrollbar.y) {
            bounds_for_scrollbar.w -= scrollbar_size;
            bounds_for_scrollbar.h -= scrollbar_size;
        }

        if (viewport->has_scrollbar.y && !auto_height && !no_scroll_y) {
            if (scrollbar_inside_padding) {
                viewport->cfg.scrollbar_width = viewport->cfg.padding.r;
                viewport->cfg.scrollbar_padding = 0;
            }
            auto const result = Scrollbar(*this,
                                          viewport,
                                          true,
                                          bounds_for_scrollbar.y,
                                          bounds_for_scrollbar.h,
                                          bounds_for_scrollbar.Right(),
                                          viewport->prev_content_size.y,
                                          viewport->scroll_offset.y,
                                          viewport->scroll_max.y,
                                          GuiIo().in.cursor_pos.y);
            scrollbar_bounds[1] = result.bar;
            viewport->scroll_offset.y = result.new_scroll_value;
            viewport->scroll_max.y = result.new_scroll_max;

            viewport->clipping_rect.w -= scrollbar_size;
            viewport->bounds.w -= scrollbar_size;
        } else {
            viewport->scroll_offset.y = 0;
        }

        if (viewport->has_scrollbar.x && !auto_width && !no_scroll_x) {
            if (scrollbar_inside_padding) {
                viewport->cfg.scrollbar_width = viewport->cfg.padding.b;
                viewport->cfg.scrollbar_padding = 0;
            }
            auto const result = Scrollbar(*this,
                                          viewport,
                                          false,
                                          bounds_for_scrollbar.x,
                                          bounds_for_scrollbar.w,
                                          bounds_for_scrollbar.Bottom(),
                                          viewport->prev_content_size.x,
                                          viewport->scroll_offset.x,
                                          viewport->scroll_max.x,
                                          GuiIo().in.cursor_pos.x);
            scrollbar_bounds[0] = result.bar;
            viewport->scroll_offset.x = result.new_scroll_value;
            viewport->scroll_max.x = result.new_scroll_max;

            viewport->clipping_rect.h -= scrollbar_size;
            viewport->bounds.h -= scrollbar_size;
        } else {
            viewport->scroll_offset.x = 0;
        }

        if (!PRODUCTION_BUILD &&
            (viewport->cfg.scrollbar_visibility.x != ViewportScrollbarVisibility::Never ||
             viewport->cfg.scrollbar_visibility.y != ViewportScrollbarVisibility::Never))
            ASSERT(cfg.draw_scrollbars,
                   "the viewport may have scrollbars, but no function is set to draw them");
        if (cfg.draw_background) cfg.draw_background(*this);
        if (cfg.draw_scrollbars) cfg.draw_scrollbars(*this, scrollbar_bounds);
    }

    //
    //
    //

    if (viewport->parent_viewport) {
        // Calculate the clipping rect - we do this at the end because it might be effected by the
        // scrollbars.
        viewport->clipping_rect.w =
            ::Min(viewport->parent_viewport->clipping_rect.Right(), viewport->clipping_rect.Right()) -
            viewport->clipping_rect.x;
        viewport->clipping_rect.h =
            ::Min(viewport->parent_viewport->clipping_rect.Bottom(), viewport->clipping_rect.Bottom()) -
            viewport->clipping_rect.y;
    } else {
        PushScissorStack();
    }
    if (cfg.mode == ViewportMode::PopupMenu) dyn::Append(current_popup_stack, viewport);
    PushRectToCurrentScissorStack(viewport->clipping_rect);

    viewport->prev_content_size = f32x2 {0, 0};
    viewport->contents_was_auto[0] = !auto_width;
    viewport->contents_was_auto[1] = !auto_height;

    RegisterRectForMouseTracking(viewport->unpadded_bounds, false);
}

Viewport* Context::FindViewport(Id id) const {
    for (auto const i : Range(viewports.size))
        if (viewports[i]->id == id) return viewports[i];
    return nullptr;
}

void Context::EndViewport() {
    auto const viewport = Last(viewport_stack);
    if (viewport->prev_content_size.x != viewport->prevprev_content_size.x ||
        viewport->prev_content_size.y != viewport->prevprev_content_size.y) {
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }

    PopRectFromCurrentScissorStack();
    PopId();
    if (!viewport->parent_viewport) PopScissorStack();
    if (viewport->cfg.mode == ViewportMode::PopupMenu) dyn::Pop(current_popup_stack);
    if (viewport->draw_list == viewport->owned_draw_list) viewport->draw_list->EndDraw();
    dyn::Pop(viewport_stack);
    if (viewport_stack.size) {
        curr_viewport = Last(viewport_stack);
        draw_list = curr_viewport->draw_list;
    } else {
        // This should only happen in the End() function when the root viewport is ended.
        curr_viewport = nullptr;
        draw_list = nullptr;
    }
}

bool Context::ScrollViewportToShowRectangle(Rect r) {
    if (!Rect::DoRectsIntersect(RegisterAndConvertRect(r),
                                curr_viewport->clipping_rect.ReducedVertically(r.h))) {
        SetYScroll(curr_viewport,
                   Clamp(r.CentreY() - (CurrentVpHeight() / 2), 0.0f, curr_viewport->scroll_max.y));
        return true;
    }
    return false;
}

void Context::PushScissorStack() { dyn::Append(scissor_stacks, DynamicArray<Rect>(Malloc::Instance())); }

void Context::PopScissorStack() {
    ASSERT(scissor_stacks.size > 1); // needs to always be at least one
    dyn::Pop(scissor_stacks);

    DynamicArray<Rect>& current_stack = Last(scissor_stacks);
    if (current_stack.size != 0) {
        current_scissor_rect = CalculateScissorStack(current_stack);
        scissor_rect_is_active = true;
        OnScissorChanged();
    } else {
        scissor_rect_is_active = false;
        OnScissorChanged();
    }
}

void Context::PushRectToCurrentScissorStack(Rect const& new_r) {
    auto& current_stack = Last(scissor_stacks);
    dyn::Append(current_stack, new_r);
    current_scissor_rect = CalculateScissorStack(current_stack);
    scissor_rect_is_active = true;
    OnScissorChanged();
}

void Context::PopRectFromCurrentScissorStack() {
    auto& current_stack = Last(scissor_stacks);
    dyn::Pop(current_stack);
    if (current_stack.size != 0) {
        current_scissor_rect = CalculateScissorStack(current_stack);
        scissor_rect_is_active = true;
        OnScissorChanged();
    } else {
        scissor_rect_is_active = false;
        OnScissorChanged();
    }
}

void Context::SetImguiTextEditState(String new_text, bool multiline) {
    stb_textedit_initialize_state(&stb_state, !multiline);
    ZeroMemory(textedit_text.data, sizeof(*textedit_text.data) * textedit_text.size);

    textedit_len = imstring::Widen(textedit_text.data,
                                   (int)textedit_text.size,
                                   new_text.data,
                                   new_text.data + new_text.size,
                                   nullptr);
    dyn::Assign(textedit_text_utf8, new_text);

    text_cursor_is_shown = true;
}

void Context::SetTextInputFocus(Id id, String new_text, bool multiline) {
    bool update_needed = false;

    if (id == k_null_id) {
        update_needed = active_text_input != k_null_id;
        active_text_input = id;
        stb_textedit_initialize_state(&stb_state, !multiline);
        ZeroMemory(textedit_text.data, sizeof(*textedit_text.data) * textedit_text.size);
    } else if (active_text_input != id) {
        active_text_input = id;
        SetImguiTextEditState(new_text, multiline);
        ResetTextInputCursorAnim();
        active_text_input_shown = true;
        update_needed = true;
    }

    if (update_needed) GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

void Context::ResetTextInputCursorAnim() {
    text_cursor_is_shown = true;
    cursor_blink_counter = GuiIo().in.current_time + k_text_cursor_blink_rate;
}

f32x2 TextInputTextPos(String text, Rect r, TextInputConfig cfg, Fonts const& fonts) {
    auto const& font = *fonts.Current();

    auto const x_offset = ({
        auto x = cfg.x_padding;
        if (cfg.centre_align) {
            auto const text_width = font.CalcTextSize(text, {.font_size = font.font_size}).x;
            x = ((r.w / 2) - (text_width / 2));
        }
        x;
    });

    auto pos = r.pos;
    pos.x += x_offset;
    if (!cfg.multiline) pos.y += (r.h - font.font_size) / 2; // centre Y
    return pos;
}

void Context::TextInputSelectAll() {
    stb_state.cursor = 0;
    stb_state.select_start = 0;
    stb_state.select_end = textedit_len;
    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

void Context::ClearActive() {
    temp_active_item.id = k_null_id;
    temp_active_item.just_activated = false;
    temp_active_item.viewport = nullptr;

    // Unlike when activating an item - where we need a frame of lag, when deactivating, we can
    // immediately apply the changes.
    active_item = {};

    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

void Context::SetActive(Id id, MouseButton mouse_button) {
    ASSERT(id != k_null_id);
    temp_active_item.id = id;
    temp_active_item.just_activated = true;
    temp_active_item.viewport = curr_viewport;
    temp_active_item.mouse_button = mouse_button;

    // An active item has been set so we no longer want to have a hot item.
    temp_hot_item = k_null_id;

    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

void Context::OpenPopupMenu(Id id, Id creator_of_this_popup) {
    if (IsPopupMenuOpen(id)) return;

    bool const is_first_popup = open_popups.size == 0;
    auto popup = FindOrCreateViewport(id);
    popup->cfg.mode = ViewportMode::PopupMenu;
    popup->prev_content_size = f32x2 {0, 0};
    popup->creator_of_this_popup_menu = is_first_popup ? k_null_id : creator_of_this_popup;

    popup_menu_just_opened = id;
    dyn::Append(open_popups, popup);
    UpdateExclusiveFocusViewport();
    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

bool Context::DidPopupMenuJustOpen(Id id) { return popup_menu_just_opened == id; }

bool Context::IsPopupMenuOpen(Id id) {
    return open_popups.size > current_popup_stack.size && open_popups[current_popup_stack.size]->id == id;
}

bool Context::IsAnyPopupMenuOpen() { return open_popups.size; }

void Context::ClosePopupToLevel(usize level) {
    ASSERT(level <= open_popups.size);
    dyn::Resize(open_popups, level);
    UpdateExclusiveFocusViewport();
}

void Context::CloseTopPopupOnly() {
    ASSERT(open_popups.size != 0);
    ClosePopupToLevel(open_popups.size - 1);
}

// Close the popup we have begin-ed into.
void Context::CloseAllPopups() {
    // int popup_index = (int)current_popup_stack.size - 1;
    // if (popup_index < 0 || popup_index > (int)open_popups.size ||
    //     current_popup_stack[(usize)popup_index]->id != open_popups[(usize)popup_index]->id) {
    //     return;
    // }
    // while (popup_index > 0 && open_popups[(usize)popup_index] &&
    //        open_popups[(usize)popup_index]->cfg.child_popup) {
    //     popup_index--;
    // }
    ClosePopupToLevel(0);
}

void Context::OpenModalViewport(Id id) {
    if (IsModalOpen(id)) return;
    auto viewport = FindOrCreateViewport(id);
    viewport->cfg.mode = ViewportMode::Modal;
    viewport->prev_content_size = f32x2 {0, 0};
    modal_just_opened = id;
    dyn::Append(open_modals, viewport);
    UpdateExclusiveFocusViewport();
    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
}

bool Context::IsModalOpen(Id id) {
    for (auto& m : open_modals)
        if (m->id == id) return true;
    return false;
}

bool Context::IsAnyModalOpen() { return open_modals.size; }

void Context::CloseModal(Id id) {
    for (auto const i : Range(open_modals.size)) {
        if (open_modals[i]->id == id) {
            ClosePopupToLevel(0);
            dyn::Resize(open_modals, i);
            UpdateExclusiveFocusViewport();
            return;
        }
    }
}

void Context::CloseTopModal() {
    ASSERT(open_modals.size != 0);
    ClosePopupToLevel(0);
    dyn::Pop(open_modals);
    UpdateExclusiveFocusViewport();
}

void Context::CloseAllModals() {
    ClosePopupToLevel(0);
    dyn::Clear(open_modals);
    UpdateExclusiveFocusViewport();
}

void Context::UpdateExclusiveFocusViewport() {
    if (open_popups.size) {
        // Popups always have exclusive focus.
        exclusive_focus_viewport = Last(open_popups);
    } else {
        exclusive_focus_viewport = floating_exclusive_viewport_last_frame;
        for (auto m : open_modals) {
            if (m->cfg.exclusive_focus) {
                if (!exclusive_focus_viewport || m->cfg.z_order >= exclusive_focus_viewport->cfg.z_order)
                    exclusive_focus_viewport = m;
            }
        }
    }
}

Context::DraggerResult Context::DraggerBehaviour(DraggerBehaviourArgs const& args) {
    DraggerResult result {};

    auto const input = TextInputBehaviour({
        .rect_in_window_coords = args.rect_in_window_coords,
        .id = args.id,
        .text = args.text,
        .placeholder_text = ""_s,
        .input_cfg = args.text_input_cfg,
        .button_cfg = args.text_input_button_cfg,
    });

    if (input.enter_pressed) result.new_string_value = input.text;

    if (!TextInputHasFocus(args.id)) {
        if (SliderBehaviourRange({
                .rect_in_window_coords = args.rect_in_window_coords,
                .id = args.id,
                .min = args.min,
                .max = args.max,
                .value = args.value,
                .default_value = args.default_value,
                .cfg = args.slider_cfg,
            }))
            result.value_changed = true;
    } else {
        result.text_input_result = input;
    }

    return result;
}

bool Context::TooltipBehaviour(Rect rect_in_window_coords, imgui::Id id) {
    SetHot(rect_in_window_coords, id);

    constexpr auto k_delay_secs = 0.5;

    if (WasJustMadeHot(id)) GuiIo().out.AddTimedWakeup(GuiIo().in.current_time + k_delay_secs, "Tooltip");

    return IsHot(id) && SecondsSpentHot() >= k_delay_secs;
}

} // namespace imgui
