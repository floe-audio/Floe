// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_imgui.hpp"

#include <stb_sprintf.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "utils/logger/logger.hpp"

#include "gui_frame.hpp"

namespace imgui {

constexpr f64 k_popup_open_and_close_delay_sec {0.2};

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
    // Invalid code point, the max unicode is 0x10FFFF
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
// returns the pixel delta from the xpos of the i'th character to the xpos of the i+1'th char for a line of
// characters starting at character #n (i.e. accounts for kerning with previous char)
// NOLINTNEXTLINE(readability-identifier-naming)
static f32 STB_TEXTEDIT_GETWIDTH(STB_TEXTEDIT_STRING* imgui, int i, int n) {
    (void)i;
    auto c = imgui->textedit_text[(usize)n];
    if (c == '\n') return STB_TEXTEDIT_GETWIDTH_NEWLINE;
    auto font = imgui->graphics->context->CurrentFont();
    return font->GetCharAdvance((graphics::Char16)c);
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
    auto font = imgui->graphics->context->CurrentFont();
    auto line_height = imgui->graphics->context->CurrentFontSize();

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
        *out_offset = f32x2 {
            line_width,
            text_size.y + line_height}; // offset allow for the possibility of sitting after a trailing \n

    if (line_width > 0 || text_size.y == 0.0f) // whereas size.y will ignore the trailing \n
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

void Context::SetHotRaw(Id id) { temp_hot_item = id; }

bool ClickCheck(ButtonFlags flags, GuiFrameInput const& io, Rect const* rect) {
    if (!flags.triggers_on_mouse_down && !flags.triggers_on_mouse_up) return false;

    DynamicArrayBounded<MouseButton, ToInt(MouseButton::Count)> buttons;
    if (flags.left_mouse) dyn::Append(buttons, MouseButton::Left);
    if (flags.right_mouse) dyn::Append(buttons, MouseButton::Right);
    if (flags.middle_mouse) dyn::Append(buttons, MouseButton::Middle);

    Optional<ModifierFlags> required_modifiers {};
    if (flags.requires_modifer || flags.requires_shift || flags.requires_alt) {
        required_modifiers = ModifierFlags {};
        if (flags.requires_modifer) required_modifiers->Set(ModifierKey::Modifier);
        if (flags.requires_shift) required_modifiers->Set(ModifierKey::Shift);
        if (flags.requires_alt) required_modifiers->Set(ModifierKey::Alt);
    }

    for (auto const button : buttons) {
        auto const& mouse_state = io.Mouse(button);
        auto const& events = flags.triggers_on_mouse_down ? mouse_state.presses : mouse_state.releases;
        for (auto const& e : events) {
            if (rect) {
                if (!rect->Contains(e.point)) continue;
                // For a release, the down point must also be inside the rectangle.
                if (flags.triggers_on_mouse_up && !rect->Contains(mouse_state.last_press.point)) continue;
            }
            if (flags.double_click && !e.is_double_click) continue;
            if (!flags.ignore_double_click && !flags.double_click && e.is_double_click) continue;
            if (required_modifiers && *required_modifiers != e.modifiers) continue;
            return true;
        }
    }

    return false;
}

static bool CheckForValidMouseDown(ButtonFlags flags, GuiFrameInput const& io) {
    if (!flags.triggers_on_mouse_down && !flags.triggers_on_mouse_up) return false;

    DynamicArrayBounded<MouseButton, ToInt(MouseButton::Count)> buttons;
    if (flags.left_mouse) dyn::Append(buttons, MouseButton::Left);
    if (flags.right_mouse) dyn::Append(buttons, MouseButton::Right);
    if (flags.middle_mouse) dyn::Append(buttons, MouseButton::Middle);

    Optional<ModifierFlags> required_modifiers {};
    if (flags.requires_modifer || flags.requires_shift || flags.requires_alt) {
        required_modifiers = ModifierFlags {};
        if (flags.requires_modifer) required_modifiers->Set(ModifierKey::Modifier);
        if (flags.requires_shift) required_modifiers->Set(ModifierKey::Shift);
        if (flags.requires_alt) required_modifiers->Set(ModifierKey::Alt);
    }

    for (auto const button : buttons) {
        auto const& mouse_state = io.Mouse(button);
        if (mouse_state.is_down) {
            if (flags.double_click && !mouse_state.is_down->is_double_click) continue;
            if (flags.ignore_double_click && !flags.double_click && mouse_state.is_down->is_double_click)
                continue;
            if (required_modifiers) {
                if (mouse_state.is_down->modifiers == *required_modifiers) return true;
            } else {
                return true;
            }
        }
    }

    return false;
}

Context::ScrollbarResult Context::Scrollbar(Window* window,
                                            bool is_vertical,
                                            f32 window_y,
                                            f32 window_h,
                                            f32 window_right,
                                            f32 content_size_y,
                                            f32 y_scroll_value,
                                            f32 y_scroll_max,
                                            f32 cursor_y) {
    auto id = GetID(is_vertical ? "Vert" : "Horz");

    y_scroll_max = ::Max(0.0f, content_size_y - window_h);

    if (content_size_y > window_h && ((y_scroll_value + window_h) > content_size_y))
        y_scroll_value = (f32)(int)(content_size_y - window_h);

    auto height_ratio = window_h / content_size_y;
    if (height_ratio > 1) height_ratio = 1;
    auto const scrollbar_h = window_h * height_ratio;
    f32 const scrollbar_range = window_h - scrollbar_h;
    f32 scrollbar_rel_y = (y_scroll_value / y_scroll_max) * scrollbar_range;
    if (scrollbar_range == 0) scrollbar_rel_y = 0;

    Rect scroll_r;
    scroll_r.x = window_right + window->style.scrollbar_padding;
    scroll_r.y = window_y + scrollbar_rel_y;
    scroll_r.w = window->style.scrollbar_width;
    scroll_r.h = scrollbar_h;
    auto scrollbar_bb = Rect {.xywh = {scroll_r.x, window_y, window->style.scrollbar_width, window_h}};
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
        ButtonFlags const button_flags {.left_mouse = true,
                                        .triggers_on_mouse_down = true,
                                        .is_non_window_content = true};
        if (ButtonBehavior(scroll_r, id, button_flags)) cached_pos.y = cursor_y - *scroll_y;

        if (IsActive(id)) {
            auto const new_ypos = (cursor_y - cached_pos.y) - window_y;
            scrollbar_rel_y = Clamp(new_ypos, 0.0f, window_h - scrollbar_h);
            *scroll_y = window_y + scrollbar_rel_y;

            f32 const y_scroll_percent = Map(scrollbar_rel_y, 0, scrollbar_range, 0, 1);
            y_scroll_value = (f32)(int)(y_scroll_percent * y_scroll_max);
        }
    }

    if (window->style.draw_routine_scrollbar)
        window->style.draw_routine_scrollbar(*this, scrollbar_bb, scroll_r, id);

    return {
        .new_scroll_value = y_scroll_value,
        .new_scroll_max = y_scroll_max,
    };
}

void Context::HandleHoverPopupOpeningAndClosing(Id id) {
    ASSERT(focused_popup_window != nullptr);
    auto window = curr_window;
    auto this_window_is_apopup =
        (window->flags & WindowFlags_Popup) | (window->flags & WindowFlags_NestedInsidePopup);

    if (IsHot(id) && this_window_is_apopup && focused_popup_window != hovered_window &&
        current_popup_stack.size < persistent_popup_stack.size) {
        auto next_window = persistent_popup_stack[current_popup_stack.size];
        Id const creator_of_next = next_window->creator_of_this_popup;

        if (id != creator_of_next) {
            if (WasJustMadeHot(id))
                AddTimedWakeup(frame_input.current_time + k_popup_open_and_close_delay_sec, "Popup close");
            if (SecondsSpentHot() >= k_popup_open_and_close_delay_sec)
                ClosePopupToLevel((int)current_popup_stack.size);
        }
    }
}

static Rect CalculateScissorStack(DynamicArray<Rect>& s) {
    Rect r = s[0];
    for (usize i = 1; i < s.size; ++i)
        Rect::Intersection(r, s[i]);
    return r;
}

void Context::OnScissorChanged() const {
    if (scissor_rect_is_active)
        graphics->SetClipRect(current_scissor_rect.pos, current_scissor_rect.Max());
    else
        graphics->SetClipRectFullscreen();
}

f32x2 BestPopupPos(Rect base_r, Rect avoid_r, f32x2 window_size, bool find_left_or_right) {
    auto ensure_bottom_fits = [&](f32x2 pos) {
        auto bottom = pos.y + base_r.h;
        if (bottom < window_size.y) {
            return pos;
        } else {
            auto d = window_size.y - bottom;
            pos.y += d;
            if (pos.y < 0) pos.y = 0;
            return pos;
        }
    };

    auto ensure_right_fits = [&](f32x2 pos) {
        auto right = pos.x + base_r.w;
        if (right > window_size.x) {
            auto d = right - window_size.x;
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

    if (find_left_or_right) {
        auto right_outer_most = avoid_r.Right() + base_r.w;
        if (right_outer_most < window_size.x) {
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
        if (below_outer_most < window_size.y) {
            auto pos = f32x2 {base_r.x, avoid_r.Bottom()};
            return ensure_right_fits(ensure_left_fits(pos));
        }

        auto above_outer_most = avoid_r.y - base_r.h;
        if (above_outer_most >= 0) {
            auto pos = f32x2 {base_r.x, above_outer_most};
            return ensure_right_fits(ensure_left_fits(pos));
        }

        return BestPopupPos(base_r, avoid_r, window_size, true);
    }

    return {-1, -1};
}

// from dear-imgui
// Return false to discard a character.
static bool InputTextFilterCharacter(unsigned int* p_char, TextInputFlags flags) {
    unsigned int c = *p_char;

    if (flags.multiline && c == '\n') return true;

    if (c < 128 && c != ' ' && !IsPrintableAscii((char)(c & 0xFF))) {
        bool const pass = false;
        if (!pass) return false;
    }

    if (c >= 0xE000 &&
        c <= 0xF8FF) // Filter private Unicode range. I don't imagine anybody would want to input them. GLFW
                     // on OSX seems to send private characters for special keys like arrow keys.
        return false;

    if (flags.chars_decimal || flags.chars_hexadecimal || flags.chars_uppercase || flags.chars_no_blank) {
        if (flags.chars_decimal)
            if (!(c >= '0' && c <= '9') && (c != '.') && (c != '-') && (c != '+') && (c != '*') && (c != '/'))
                return false;

        if (flags.chars_hexadecimal)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return false;

        if (flags.chars_uppercase)
            if (c >= 'a' && c <= 'z') *p_char = (c += (unsigned int)('A' - 'a'));

        if (flags.chars_no_blank)
            if (IsSpacing((char)c)) return false;
    }

    return true;
}

// Hash function from IMGUI
// Pass data_size==0 for zero-terminated strings
static u32 ImguiHash(void const* data, int data_size, u32 seed = 0) {
    static u32 crc32_lut[256] = {0};
    if (!crc32_lut[1]) {
        u32 const polynomial = 0xEDB88320;
        for (u32 i = 0; i < 256; i++) {
            u32 crc = i;
            for (u32 j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (u32(-int(crc & 1)) & polynomial);
            crc32_lut[i] = crc;
        }
    }

    seed = ~seed;
    u32 crc = seed;
    auto* current = (unsigned char const*)data;

    if (data_size > 0) {
        // Known size
        while (data_size--)
            crc = (crc >> 8) ^ crc32_lut[(crc & 0xFF) ^ *current++];
    } else {
        // Zero-terminated string
        while (unsigned char const c = *current++)
            crc = (crc >> 8) ^ crc32_lut[(crc & 0xFF) ^ c];
    }
    return ~crc;
}

bool Context::WakeupAtTimedInterval(TimePoint& counter, f64 interval_seconds) {
    bool triggered = false;
    if (frame_input.current_time >= counter) {
        counter = frame_input.current_time + interval_seconds;
        triggered = true;
    }
    AddTimedWakeup(counter, __FUNCTION__);
    return triggered;
}

void Context::AddTimedWakeup(TimePoint time, char const* timer_name) {
    (void)timer_name;
    dyn::AppendIfNotAlreadyThere(timed_wakeups, time);
}

void Context::PushID(String str) { dyn::Append(id_stack, GetID(str)); }

void Context::PushID(uintptr num) { dyn::Append(id_stack, GetID(num)); }

void Context::PopID() { dyn::Pop(id_stack); }

Id Context::GetID(String str) {
    auto const seed = Last(id_stack);
    auto const result = ImguiHash(str.data, (int)str.size, seed);
    ASSERT(result != 0 && result != k_imgui_misc_id); // by chance we have landed on one of the reserved ids
    return result;
}

Id Context::GetID(uintptr i) {
    auto const seed = Last(id_stack);
    auto const result = ImguiHash(&i, (int)sizeof(i), seed);
    ASSERT(result != 0 && result != k_imgui_misc_id); // by chance we have landed on one of the reserved ids
    return result;
}

Context::Context(GuiFrameInput& frame_input, GuiFrameResult& frame_output)
    : frame_input(frame_input)
    , frame_output(frame_output) {
    dyn::Append(id_stack, 0u);
    PushScissorStack();
    windows.Reserve(64);
    stb_textedit_initialize_state(&stb_state, true);
    dyn::Resize(textedit_text, Kb(4));
    textedit_text_utf8.Reserve(Kb(4));
}

Context::~Context() {
    for (auto& w : windows)
        delete w;
}

bool Context::IsRectVisible(Rect r) {
    Rect const& c = current_scissor_rect;
    return Rect::DoRectsIntersect(r, c);
}

// Hot
bool Context::IsHot(Id id) const { return hot_item == id; }
bool Context::WasJustMadeHot(Id id) { return IsHot(id) && hot_item_last_frame != hot_item; }
bool Context::WasJustMadeUnhot(Id id) { return !IsHot(id) && hot_item_last_frame == id; }
bool Context::AnItemIsHot() { return hot_item != 0; }

// Active
bool Context::IsActive(Id id) const { return active_item.id == id; }
bool Context::WasJustActivated(Id id) { return IsActive(id) && active_item_last_frame != id; }
bool Context::WasJustDeactivated(Id id) { return !IsActive(id) && active_item_last_frame == id; }
bool Context::AnItemIsActive() { return active_item.id != 0; }

// Hovered
bool Context::IsHovered(Id id) { return hovered_item == id; }
bool Context::WasJustHovered(Id id) { return IsHovered(id) && hovered_item_last_frame != hovered_item; }
bool Context::WasJustUnhovered(Id id) { return !IsHovered(id) && hovered_item_last_frame == hovered_item; }

void Context::Begin(WindowSettings settings) {
    ASSERT_EQ(window_stack.size, 0u);
    ASSERT_EQ(current_popup_stack.size, 0u);

    draw_data.draw_lists = {};
    draw_data.total_vtx_count = 0;
    draw_data.total_idx_count = 0;

    dyn::Clear(mouse_tracked_rects);
    dyn::Clear(clipboard_for_os);

    tab_just_used_to_focus = false;
    frame_counter++;
    window_just_created = nullptr;
    curr_window = nullptr;
    hovered_window_last_frame = hovered_window;
    hovered_window = nullptr;
    hovered_window_content = nullptr;

    for (usize i = sorted_windows.size; i-- > 0;) {
        auto window = sorted_windows[i];
        if (window->visible_bounds.Contains(frame_input.cursor_pos)) {
            if (window->flags & WindowFlags_DrawingOnly) continue;
            if (window->clipping_rect.Contains(frame_input.cursor_pos)) hovered_window_content = window;
            hovered_window = window;
            break;
        }
    }
    dyn::Clear(sorted_windows);

    if (frame_input.mouse_scroll_delta_in_lines != 0 && hovered_window) {
        Window* window = hovered_window;
        Window* final_window = nullptr;
        while (true) {
            if (window->has_yscrollbar) {
                final_window = window;
                break;
            }
            if (window == window->root_window) break;
            window = window->parent_window;
        }
        if (final_window) {
            f32 const k_pixels_per_line =
                VwToPixels(20); // IMPROVE: this should be a setting so, for example, popups
                                // can scroll in increments of each item
            f32 const lines = -frame_input.mouse_scroll_delta_in_lines;
            f32 const new_scroll = (lines * k_pixels_per_line) + final_window->scroll_offset.y;
            final_window->scroll_offset.y = Round(Clamp(new_scroll, 0.0f, final_window->scroll_max.y));
        }
    }

    //
    //
    //

    // Debug
    debug_window_to_inspect = nullptr;
    if (frame_input.Key(KeyCode::A).presses.size) debug_window_to_inspect = hovered_window;

    //
    // Reset stuff
    //

    for (auto& w : windows) {
        w->has_been_sorted = false;
        w->is_open = false;
        w->skip_drawing_this_frame = false;
        dyn::Clear(w->children);
        w->parent_popup = nullptr;
    }

    focused_popup_window = nullptr;
    if (persistent_popup_stack.size != 0) focused_popup_window = Last(persistent_popup_stack);

    // copy over the temp id to the actual id
    active_item = temp_active_item;
    hot_item = temp_hot_item;
    hovered_item = temp_hovered_item;
    if (hot_item != 0) {
        if (WasJustMadeHot(hot_item)) time_when_turned_hot = frame_input.current_time;
    } else {
        time_when_turned_hot = TimePoint {};
    }

    temp_active_item.just_activated = false;
    SetHotRaw(0);
    temp_hovered_item = 0;

    if (GetActive() && active_item.check_for_release &&
        !CheckForValidMouseDown(active_item.button_flags, frame_input))
        SetActiveIDZero();

    next_window_contents_size = {0, 0};

    active_text_input_shown = false;

    overlay_graphics.context = frame_input.graphics_ctx;
    overlay_graphics.BeginDraw();

    BeginWindow(settings,
                k_imgui_app_window_id,
                Rect {.size = frame_input.window_size.ToFloat2()},
                "ApplicationWindow");
}

void Context::End(ArenaAllocator& scratch_arena) {
    EndWindow(); // application window
    ASSERT_EQ(window_stack.size, 0u); // all BeginWindow calls must have an EndWindow
    ASSERT_EQ(current_popup_stack.size, 0u);

    if (!active_text_input_shown) SetTextInputFocus(0, {}, false);

    if (debug_show_register_widget_overlay) {
        for (auto& w : mouse_tracked_rects) {
            auto col = 0xffff00ff;
            if (w.mouse_over) col = 0xff00ffff;
            overlay_graphics.AddRect(w.rect.Min(), w.rect.Max(), col);
        }
    }

    overlay_graphics.EndDraw();

    //
    // Flush buffers with sorting
    //

    dyn::Clear(output_draw_lists);

    auto confirm_window = [&](Window* window, DynamicArray<Window*>& sorted_buf) {
        if (!window->has_been_sorted) {
            window->has_been_sorted = true;
            dyn::Append(sorted_buf, window);
            dyn::Append(output_draw_lists, window->graphics);
            draw_data.total_vtx_count += window->graphics->vtx_buffer.Size();
            draw_data.total_idx_count += window->graphics->idx_buffer.Size();
        }
    };

    // first we get together all windows that are active
    dyn::Clear(active_windows);
    for (auto& window : windows) {
        window->has_been_sorted = false;
        if (window->is_open) dyn::Append(active_windows, window);
    }

    // then we group all windows that are root windows
    DynamicArray<Window*> nesting_roots {scratch_arena}; // internal
    for (auto& window : active_windows)
        if (window->root_window == window && !window->skip_drawing_this_frame)
            dyn::Append(nesting_roots, window);

    // for each of the root windows, we find all the windows that are children of them
    DynamicArray<DynamicArray<Window*>> nested_sorting_bins {scratch_arena}; // internal
    dyn::AssignRepeated(nested_sorting_bins, nesting_roots.size, scratch_arena);
    for (auto const root : Range(nesting_roots.size)) {
        for (auto& window : active_windows)
            if (window->root_window == nesting_roots[root] && window->root_window != window)
                dyn::Append(nested_sorting_bins[root], window);
    }

    // for each bin that contains a whole load of unsorted windows with the same root, we
    // sort them into the correct order
    for (auto const i : Range(nested_sorting_bins.size)) {
        auto& bin = nested_sorting_bins[i];
        if (!bin.size) continue;
        Sort(bin, [](Window const* a, Window const* b) -> bool { return a->nested_level < b->nested_level; });

        for (auto [index, window] : Enumerate(bin)) {
            if (window->skip_drawing_this_frame) {
                dyn::Resize(bin, index);
                break;
            }
        }

        // if its a popup then we dont want to flush yet
        if (nesting_roots[i]->flags & WindowFlags_Popup) continue;
        confirm_window(nesting_roots[i], sorted_windows);
        for (auto& window : bin)
            confirm_window(window, sorted_windows);
    }

    // finally do the popups
    for (auto& popup : persistent_popup_stack) {
        if (DidPopupMenuJustOpen(popup->id)) continue;

        {
            for (auto const j : Range(nesting_roots.size)) {
                Window* root_window = nesting_roots[j];
                if (popup == root_window) {
                    auto& bin = nested_sorting_bins[j];
                    confirm_window(root_window, sorted_windows);
                    for (auto& window : bin)
                        confirm_window(window, sorted_windows);
                    break;
                }
            }
        }
    }
    dyn::Clear(active_windows);

    dyn::Append(output_draw_lists, &overlay_graphics);
    draw_data.total_vtx_count += overlay_graphics.vtx_buffer.Size();
    draw_data.total_idx_count += overlay_graphics.idx_buffer.Size();

    if (frame_input.Mouse(MouseButton::Left).presses.size && temp_active_item.id == 0 && temp_hot_item == 0) {
        if (hovered_window != nullptr) {
            Window* window = hovered_window;
            bool const closes_popups = (window->flags & WindowFlags_NeverClosesPopup) == 0;
            SetActiveID(k_imgui_misc_id,
                        closes_popups,
                        {.left_mouse = true, .triggers_on_mouse_down = true},
                        true); // indicate when the mouse is pressed down, but not over anything important
            focused_popup_window = hovered_window;
        } else {
            SetActiveID(k_imgui_misc_id,
                        false,
                        {.left_mouse = true, .triggers_on_mouse_down = true},
                        true); // indicate when the mouse is pressed down, but not over anything important
        }
    }

    { // close popups if clicked
        if (active_item.just_activated && persistent_popup_stack.size != 0 && popup_menu_just_created == 0 &&
            active_item.closes_popups) {
            Window* focused_wnd = focused_popup_window;
            if (!(focused_wnd->flags & WindowFlags_DontCloseWithExternalClick)) {
                Window* popup_clicked = nullptr;
                if (hovered_window != nullptr) {
                    Window* wnd = hovered_window;

                    if (wnd->flags & WindowFlags_Popup)
                        popup_clicked = hovered_window;
                    else if (wnd->flags & WindowFlags_NestedInsidePopup)
                        popup_clicked = wnd->root_window;
                }

                if (popup_clicked != nullptr) {
                    for (auto const i : Range(persistent_popup_stack.size)) {
                        if (popup_clicked == persistent_popup_stack[i]) {
                            if (i != persistent_popup_stack.size - 1)
                                ClosePopupToLevel((int)i + 1); // close children popups
                            break;
                        }
                    }
                } else {
                    dyn::Clear(persistent_popup_stack); // something unrelated was clicked, close all popups
                }
            }
        }
    }
    prevprev_popup_menu_just_created = prev_popup_menu_just_created;
    prev_popup_menu_just_created = popup_menu_just_created;
    popup_menu_just_created = 0;

    frame_output.wants_keyboard_input = active_text_input != 0;
    frame_output.wants_mouse_capture = AnItemIsActive();
    frame_output.wants_mouse_scroll = true;
    frame_output.wants_all_left_clicks = focused_popup_window != nullptr || GetTextInput();
    frame_output.wants_all_right_clicks = false;
    frame_output.wants_all_middle_clicks = false;

    draw_data.draw_lists = output_draw_lists;
    frame_output.draw_data = draw_data;

    frame_output.mouse_tracked_rects = mouse_tracked_rects;
    frame_output.set_clipboard_text = clipboard_for_os;
    frame_output.timed_wakeups = &timed_wakeups;

    active_item_last_frame = active_item.id;
    hot_item_last_frame = hot_item;
    hovered_item_last_frame = hovered_item;
    prev_active_text_input = active_text_input;

    if (temp_hot_item != hot_item)
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    if (temp_active_item.just_activated) {
        temp_hot_item = 0;
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }
    if (tab_to_focus_next_input)
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
}

bool Context::TextInputHasFocus(Id id) const { return active_text_input && active_text_input == id; }

bool Context::TextInputJustFocused(Id id) const {
    return TextInputHasFocus(id) && prev_active_text_input != id;
}

bool Context::TextInputJustUnfocused(Id id) const {
    return !TextInputHasFocus(id) && prev_active_text_input == id;
}

bool Context::SliderRangeBehavior(Rect r, Id id, f32 min, f32 max, f32& value, SliderFlags flags) {
    f32 const default_value = min;
    return SliderRangeBehavior(r, id, min, max, value, default_value, flags);
}
bool Context::SliderRangeBehavior(Rect r,
                                  Id id,
                                  f32 min,
                                  f32 max,
                                  f32& value,
                                  f32 default_value,
                                  SliderFlags flags) {
    return SliderRangeBehavior(r, id, min, max, value, default_value, 400, flags);
}

bool Context::SliderRangeBehavior(Rect r,
                                  Id id,
                                  f32 min,
                                  f32 max,
                                  f32& value,
                                  f32 default_value,
                                  f32 sensitivity,
                                  SliderFlags flags) {

    f32 percent = Map(value, min, max, 0.0f, 1.0f);
    f32 const default_percent = Map(default_value, min, max, 0.0f, 1.0f);

    // We are now working in the percent space, so we need to scale the sensitivity too so that it still
    // relates to pixels per step of 1.0.
    sensitivity *= Abs(max - min);

    bool const slider_changed = SliderBehavior(r, id, percent, default_percent, sensitivity, flags);

    if (slider_changed) value = Map(percent, 0.0f, 1.0f, min, max);
    return slider_changed;
}

bool Context::SliderBehavior(Rect r, Id id, f32& percent, SliderFlags flags) {
    f32 const default_percent = 0;
    return SliderBehavior(r, id, percent, default_percent, flags);
}

bool Context::SliderBehavior(Rect r, Id id, f32& percent, f32 default_percent, SliderFlags flags) {
    return SliderBehavior(r, id, percent, default_percent, 400, flags);
}

bool Context::SliderRangeBehavior(Rect r,
                                  Id id,
                                  int min,
                                  int max,
                                  int& value,
                                  int default_value,
                                  f32 sensitivity,
                                  SliderFlags flags) {

    bool slider_changed;
    static f32 cache = 0;
    if (!IsActive(id)) {
        auto val = (f32)value;
        slider_changed =
            SliderRangeBehavior(r, id, (f32)min, (f32)max, val, (f32)default_value, sensitivity, flags);
    } else {
        if (WasJustActivated(id)) cache = (f32)value;
        slider_changed =
            SliderRangeBehavior(r, id, (f32)min, (f32)max, cache, (f32)default_value, sensitivity, flags);
        value = (int)cache;
    }

    return slider_changed;
}

bool Context::SliderUnboundedBehavior(Rect r,
                                      Id id,
                                      f32& val,
                                      f32 default_val,
                                      f32 sensitivity,
                                      SliderFlags flags) {
    static f32 val_at_click = 0;
    static f32x2 start_location = {};
    f32 const starting_val = val;

    // NOTE: this slider always responds both vertically and horizontally.

    if (ButtonBehavior(r, id, {.left_mouse = true, .triggers_on_mouse_down = true})) {
        if ((flags.default_on_modifer) && frame_input.modifiers.Get(ModifierKey::Modifier)) val = default_val;
        val_at_click = val;
        start_location = frame_input.cursor_pos;
    }

    if (IsActive(id)) {
        if (flags.slower_with_shift) {
            if (frame_input.Key(KeyCode::ShiftL).presses.size ||
                frame_input.Key(KeyCode::ShiftR).presses.size) {
                val_at_click = val;
                start_location = frame_input.cursor_pos;
            }
            if (frame_input.modifiers.Get(ModifierKey::Shift)) sensitivity *= 4;
        }
        if (All(frame_input.cursor_pos != -1)) {
            auto d = frame_input.cursor_pos - start_location;
            d.x = -d.x;
            auto distance_from_drag_start = d.x + d.y;
            if (d.x > 0 && d.y > 0) distance_from_drag_start = Sqrt(Pow(d.x, 2.0f) + Pow(d.y, 2.0f));
            if (d.x < 0 && d.y < 0) distance_from_drag_start = -Sqrt(Pow(-d.x, 2.0f) + Pow(-d.y, 2.0f));
            val = val_at_click - distance_from_drag_start / sensitivity;
        }
    }

    return val != starting_val;
}

bool Context::SliderBehavior(Rect r,
                             Id id,
                             f32& percent,
                             f32 default_percent,
                             f32 sensitivity,
                             SliderFlags flags) {
    f32 const start = percent;
    SliderUnboundedBehavior(r, id, percent, default_percent, sensitivity, flags);
    percent = Clamp(percent, 0.0f, 1.0f);
    return start != percent;
}

void Context::SetMinimumPopupSize(f32 width, f32 height) {
    auto r = Rect {.w = width, .h = height};
    RegisterAndConvertRect(&r);
}

f32x2 Context::WindowPosToScreenPos(f32x2 rel_pos) {
    Window* window = curr_window;
    return rel_pos + window->bounds.pos - window->scroll_offset;
}

f32x2 Context::ScreenPosToWindowPos(f32x2 screen_pos) {
    Window* window = curr_window;
    return screen_pos - window->bounds.pos + window->scroll_offset;
}

Rect Context::GetRegisteredAndConvertedRect(Rect r) {
    RegisterAndConvertRect(&r);
    return r;
}

void Context::RegisterToWindow(Rect r) {
    auto reg = [](f32 start, f32 size, f32 comparison_size, f32 content_size, bool& is_auto) {
        f32 const end = start + size;
        f32 const epsilon = 0.1f;
        if (end > content_size) {
            if (end > comparison_size + epsilon) is_auto = false;
            return end;
        }
        return content_size;
    };

    Window* window = curr_window;
    if (window == nullptr) return;

    f32 const comparison_size_x =
        (window->flags & WindowFlags_AutoWidth) ? window->prev_content_size.x : window->bounds.w;
    f32 const comparison_size_y =
        (window->flags & WindowFlags_AutoHeight) ? window->prev_content_size.y : window->bounds.h;
    window->prev_content_size.x =
        reg(r.x, r.w, comparison_size_x, window->prev_content_size.x, window->x_contents_was_auto);
    window->prev_content_size.y =
        reg(r.y, r.h, comparison_size_y, window->prev_content_size.y, window->y_contents_was_auto);
}

void Context::RegisterAndConvertRect(Rect* r) {
    RegisterToWindow(*r);
    r->pos = WindowPosToScreenPos(r->pos);

    // Debug: show boxes around registered rectangles.
    // overlay_graphics.AddRect(r->Min(), r->Max(), 0xff0000ff);
}

bool Context::RegisterRegionForMouseTracking(Rect r, bool check_intersection) {
    auto this_window_is_apopup =
        (curr_window->flags & WindowFlags_Popup) | (curr_window->flags & WindowFlags_NestedInsidePopup);
    if (focused_popup_window != nullptr && !this_window_is_apopup) return false;
    if (check_intersection && !Rect::DoRectsIntersect(r, GetCurrentClipRect())) return false;

    MouseTrackedRect widget = {};
    widget.rect = r;
    widget.mouse_over = r.Contains(frame_input.cursor_pos);
    dyn::Append(mouse_tracked_rects, widget);
    return true;
}

Window* Context::GetPopupFromID(Id id) {
    for (auto const i : Range(persistent_popup_stack.size))
        if (id == persistent_popup_stack[i]->id) return persistent_popup_stack[i];
    return nullptr;
}

bool Context::SetHot(Rect r, Id id, bool32 is_not_window_content) {
    // If there is a popup window focused and it is not this window we can leave
    // early as the popup has focus. (We also check that this current window is not
    // nested inside a popup - in that case we proceed as normal).
    if (focused_popup_window != nullptr && focused_popup_window != curr_window) {
        auto const this_window_is_inside_apopup = curr_window->flags & WindowFlags_NestedInsidePopup;
        auto const this_windows_root_is_the_focused_popup = curr_window->root_window == focused_popup_window;
        auto const this_window_is_apopup =
            (curr_window->flags & WindowFlags_Popup) | (curr_window->flags & WindowFlags_NestedInsidePopup);

        HandleHoverPopupOpeningAndClosing(id);

        if (focused_popup_window->flags & WindowFlags_ChildPopup &&
            !(focused_popup_window->flags & WindowFlags_NestedInsidePopup) &&
            (curr_window->flags & WindowFlags_ModalPopup ||
             (curr_window->flags & WindowFlags_NestedInsidePopup &&
              curr_window->root_window->flags & WindowFlags_ModalPopup)))
            return false;

        if (!this_window_is_apopup) {
            if (!(this_window_is_inside_apopup && this_windows_root_is_the_focused_popup)) return false;
        }
    }

    // Only bother to check if the cursor is in the same window.
    if ((curr_window == hovered_window_content || (is_not_window_content && curr_window == hovered_window)) &&
        r.Contains(frame_input.cursor_pos)) {
        temp_hovered_item = id;
        // Only allow it if there is not an active item (for example a disallow when a slider is held down).
        if (GetActive() == 0) {
            SetHotRaw(id);
            return true;
        }
    }

    return false;
}

TextInputResult Context::TextInput(Rect r,
                                   Id id,
                                   String text_unfocused,
                                   TextInputFlags flags,
                                   ButtonFlags button_flags,
                                   bool select_all_on_first_open) {
    ASSERT(!(flags.multiline && flags.centre_align), "not supported");
    if (flags.multiline_wordwrap_hack) ASSERT(flags.multiline);

    TextInputResult result {};

    int const starting_cursor = stb_state.cursor;
    bool reset_cursor = false;

    auto get_rel_click_point = [this](f32x2 pos, f32 offset) {
        f32x2 relative_click = frame_input.cursor_pos - pos;
        relative_click -= f32x2 {offset, 0};
        return relative_click;
    };

    auto get_text_pos = [&](Rect r, f32 offset) {
        auto font_size = graphics->context->CurrentFontSize();
        auto pos = r.pos;
        pos.x += offset;
        if (!flags.multiline) pos.y += (r.h - font_size) / 2; // centre Y
        return pos;
    };

    bool set_focus = false;
    if (tab_to_focus_next_input) {
        tab_to_focus_next_input = false;
        set_focus = true;
    }

    if (!TextInputHasFocus(id)) {
        if (ButtonBehavior(r, id, button_flags)) set_focus = true;
    }

    if (set_focus) {
        SetTextInputFocus(id, text_unfocused, flags.multiline);
        reset_cursor = true;
    }

    auto get_offset = [&](String text) {
        auto x_offset = k_text_xpad_in_input_box;
        if (flags.centre_align) {
            auto font = graphics->context->CurrentFont();
            auto size = font->CalcTextSizeA(font->font_size, FLT_MAX, 0.0f, text).x;
            x_offset = ((r.w / 2) - (size / 2));
        }
        return x_offset;
    };

    auto copy_selection_to_clipboard = [&]() {
        auto const start = (usize)::Min(stb_state.select_start, stb_state.select_end);
        auto const end = (usize)::Max(stb_state.select_start, stb_state.select_end);
        auto const size = end - start;
        if (!size) return;

        dyn::Resize(clipboard_for_os, size * 4); // 1 utf32 could at most be 4 utf8 bytes

        dyn::Resize(clipboard_for_os,
                    (usize)imstring::Narrow(clipboard_for_os.data,
                                            (int)clipboard_for_os.size,
                                            textedit_text.data + start,
                                            textedit_text.data + end));
    };

    if (IsHot(id)) frame_output.cursor_type = CursorType::IBeam;

    if (TextInputHasFocus(id)) {
        if (frame_input.Key(KeyCode::Tab).presses.size && flags.tab_focuses_next_input &&
            !tab_just_used_to_focus) {
            tab_to_focus_next_input = true;
            tab_just_used_to_focus = true;
            SetTextInputFocus(0, {}, false);
        }

        if ((active_item.id && active_item.id != id) || (temp_active_item.id && temp_active_item.id != id))
            SetTextInputFocus(0, {}, false);

        if (!flags.multiline && frame_input.Key(KeyCode::Enter).presses.size) {
            result.enter_pressed = true;
            SetTextInputFocus(0, {}, false);
        }
    }

    if (!TextInputHasFocus(id)) {
        auto const text = TextInputJustUnfocused(id) ? String(textedit_text_utf8) : text_unfocused;
        auto x_offset = get_offset(text);
        result.text_pos = get_text_pos(r, x_offset);
        result.text = text;
        return result;
    }

    active_text_input_shown = true;

    auto const initial_textedit_len = textedit_len;
    auto const x_offset = get_offset(textedit_text_utf8);

    if (ButtonBehavior(r, id, {.left_mouse = true, .triggers_on_mouse_down = true})) {
        auto rel_pos = get_rel_click_point(r.pos, x_offset);
        stb_textedit_click(this, &stb_state, rel_pos.x, rel_pos.y);
        reset_cursor = true;
    }
    if (IsActive(id)) {
        if (!frame_input.mouse_buttons[0].is_down) {
            SetActiveIDZero();
        } else if (!WasJustActivated(id)) {
            if (active_item.button_flags.triggers_on_mouse_down) {
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

    if (IsHotOrActive(id)) frame_output.cursor_type = CursorType::IBeam;

    auto const shift_bit = [](GuiFrameInput::KeyState::Event const& event) -> int {
        return event.modifiers.Get(ModifierKey::Shift) ? STB_TEXTEDIT_K_SHIFT : 0;
    };

    // Select word
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
            stb_textedit_key(
                this,
                &stb_state,
                (event.modifiers.Get(ModifierKey::Modifier) ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT) |
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
                frame_output.wants_clipboard_text_paste = true;
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
        if (flags.multiline) {
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
        dyn::Resize(w_text,
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
            if (InputTextFilterCharacter(&c, flags)) {
                stb_textedit_key(this, &stb_state, (int)c);
                result.buffer_changed = true;
                reset_cursor = true;
            }
        }
    }

    auto const font_size = graphics->context->CurrentFontSize();
    auto font = graphics->context->CurrentFont();

    if (result.buffer_changed) {
        for (u8 iteration = 0; iteration < 2; ++iteration) {
            dyn::Resize(textedit_text_utf8, (usize)textedit_len * 4); // 1 utf32 could at most be 4 utf8 bytes
            dyn::Resize(textedit_text_utf8,
                        (usize)imstring::Narrow(textedit_text_utf8.data,
                                                (int)textedit_text_utf8.size,
                                                textedit_text.data,
                                                textedit_text.data + textedit_len));

            // Word-wrap for when we add 1 character and it goes over the edge. The string will end up with
            // newlines in it. IMPROVE: this is an absolute hack to just cover the most common case.
            if (flags.multiline_wordwrap_hack && stb_state.cursor == textedit_len &&
                textedit_len == (initial_textedit_len + 1)) {

                auto const max_width = r.w - (k_text_xpad_in_input_box * 4);
                if (max_width <= 0) break;

                auto const* line_end = textedit_text_utf8.data + textedit_text_utf8.size - 1;
                auto const* line_start = line_end;
                while (line_start > textedit_text_utf8.data && *line_start != '\n')
                    line_start--;
                if (*line_start == '\n') line_start++;

                if (line_end <= line_start) break;

                auto const line_width = font->CalcTextSizeA(graphics->context->CurrentFontSize(),
                                                            FLT_MAX,
                                                            0,
                                                            {line_start, (usize)(line_end - line_start)})
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
    result.text_pos = get_text_pos(r, get_offset(result.text));

    f32 const y_pad = 2;

    {
        f32 const cursor_width = 2; // IMPROVE: scaling
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
        f32 const cursor_start = font->CalcTextSizeA(font_size, FLT_MAX, 0, text_up_to_cursor).x;

        auto cursor_r = Rect {.x = result.text_pos.x + cursor_start,
                              .y = result.text_pos.y - y_pad + (line_index * font_size),
                              .w = cursor_width,
                              .h = font_size + (y_pad * 2)};

        result.cursor_rect = cursor_r;
    }

    if (!result.HasSelection()) {
        if (starting_cursor != stb_state.cursor || reset_cursor)
            ResetTextInputCursorAnim();
        else if (WakeupAtTimedInterval(cursor_blink_counter, k_text_cursor_blink_rate))
            text_cursor_is_shown = !text_cursor_is_shown;
    }

    result.show_cursor = text_cursor_is_shown && !result.HasSelection();

    // We do this at the end because we might have run stb_click code, and we want to override the value set
    // there with the whole selection
    if (TextInputJustFocused(id) && select_all_on_first_open) TextInputSelectAll();

    return result;
}

Optional<Rect> TextInputResult::NextSelectionRect(TextInputResult::SelectionIterator& it) const {
    ASSERT(HasSelection());
    if (it.reached_end) return {};

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

        auto font = it.draw_ctx.CurrentFont();
        auto font_size = it.draw_ctx.CurrentFontSize();

        Rect const result = {
            .x = text_pos.x + font->CalcTextSizeA(font_size,
                                                  FLT_MAX,
                                                  0,
                                                  {start_line_start, (usize)(start_pos - start_line_start)})
                                  .x,
            .y = text_pos.y - 2 + (start_line_index * font_size),
            .w = font->CalcTextSizeA(font_size, FLT_MAX, 0, {start_pos, (usize)(end_pos - start_pos)}).x,
            .h = font_size + 4};

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

    auto font = it.draw_ctx.CurrentFont();
    auto font_size = it.draw_ctx.CurrentFontSize();

    return Rect {.x = text_pos.x,
                 .y = text_pos.y - 2 + (start_line_index * font_size),
                 .w = font->CalcTextSizeA(font_size, FLT_MAX, 0, {start_pos, (usize)(end_pos - start_pos)}).x,
                 .h = font_size + 4};
}

bool Context::PopupButtonBehavior(Rect r, Id button_id, Id popup_id, ButtonFlags flags) {
    bool just_clicked = false;

    if (!current_popup_stack.size) {
        if (ButtonBehavior(r, button_id, flags)) {
            OpenPopup(popup_id, button_id);
            just_clicked = true;
        }
    } else {
        if (ButtonBehavior(r, button_id, flags)) just_clicked = true;
        if (WasJustMadeHot(button_id))
            AddTimedWakeup(frame_input.current_time + k_popup_open_and_close_delay_sec, "Popup open");
        if ((just_clicked || (IsHot(button_id) && SecondsSpentHot() >= k_popup_open_and_close_delay_sec)) &&
            !IsPopupOpen(popup_id)) {
            ClosePopupToLevel((int)current_popup_stack.size);
            OpenPopup(popup_id, button_id);
        }
    }

    return just_clicked;
}

bool Context::ButtonBehavior(Rect r, Id id, ButtonFlags flags) {
    bool result = false;

    RegisterRegionForMouseTracking(r);

    if (flags.disabled) return false;

    if (flags.hold_to_repeat && IsActive(id)) {
        if (WakeupAtTimedInterval(button_repeat_counter, button_repeat_rate)) result = true;
    }

    if (SetHot(r, id, flags.is_non_window_content)) {
        if (CheckForValidMouseDown(flags, frame_input)) {
            SetActiveID(id, flags.closes_popups, flags, !(flags.dont_check_for_release));

            button_repeat_counter = {};
            if (flags.hold_to_repeat) WakeupAtTimedInterval(button_repeat_counter, button_repeat_rate);
            if (!(flags.triggers_on_mouse_up)) result = true;
        }
    }
    if ((flags.triggers_on_mouse_up) && r.Contains(frame_input.cursor_pos) && WasJustDeactivated(id)) {
        // the cursor is still over the rectangle and and mouse has just been released
        result = true;
    }

    if (IsHotOrActive(id)) frame_output.cursor_type = CursorType::Hand;

    if (result && (flags.closes_popups)) CloseCurrentPopup();

    return result;
}

bool Context::WasWindowJustCreated(Window* window) {
    return window != nullptr && window_just_created == window;
}
bool Context::WasWindowJustCreated(Id id) {
    return id != 0 && window_just_created && window_just_created->id == id;
}

Window* Context::AddWindowIfNotAlreadyThere(Id id) {
    for (auto const i : Range(windows.size))
        if (id == windows[i]->id) return windows[i];

    auto* w = new Window();
    window_just_created = w;
    w->id = id;
    dyn::Append(windows, w);
    return Last(windows);
}

bool Context::WasWindowJustHovered(Window* window) { return WasWindowJustHovered(window->id); }

bool Context::WasWindowJustHovered(Id id) {
    return IsWindowHovered(id) && (hovered_window_last_frame == nullptr ||
                                   (hovered_window_last_frame && hovered_window_last_frame->id != id));
}

bool Context::WasWindowJustUnhovered(Window* window) { return WasWindowJustUnhovered(window->id); }
bool Context::WasWindowJustUnhovered(Id id) {
    return !IsWindowHovered(id) && hovered_window_last_frame != nullptr &&
           hovered_window_last_frame->id == id;
}
bool Context::IsWindowHovered(Window* window) { return IsWindowHovered(window->id); }
bool Context::IsWindowHovered(Id id) { return hovered_window != nullptr && hovered_window->id == id; }

void Context::BeginWindow(WindowSettings settings, Window* window, Rect r) {
    BeginWindow(settings, window, r, "");
}
void Context::BeginWindow(WindowSettings settings, Id id, Rect r) { BeginWindow(settings, id, r, ""); }

void Context::BeginWindow(WindowSettings settings, Rect r, String str) {
    BeginWindow(settings, GetID(str), r, str);
}

void Context::BeginWindow(WindowSettings settings, Id id, Rect r, String str) {
    Window* window = AddWindowIfNotAlreadyThere(id);
    BeginWindow(settings, window, r, str);
}

void Context::BeginWindow(WindowSettings settings, Window* window, Rect r, String str) {
    auto flags = settings.flags;
    auto const no_padding = (flags & WindowFlags_NoPadding);
    auto const is_apopup = (flags & WindowFlags_Popup);
    auto const auto_width = (flags & WindowFlags_AutoWidth);
    auto auto_height = (flags & WindowFlags_AutoHeight);
    auto const auto_pos = (flags & WindowFlags_AutoPosition);
    auto const no_scroll_x = (flags & WindowFlags_NoScrollbarX);
    auto const no_scroll_y = (flags & WindowFlags_NoScrollbarY);
    auto const draw_on_top = (flags & WindowFlags_DrawOnTop);

    ASSERT(r.x >= 0);
    ASSERT(r.y >= 0);

    dyn::Append(active_windows, window);
    dyn::Assign(window->name, str);
    window->user_flags = next_window_user_flags;
    next_window_user_flags = 0;
    window->flags = flags;
    window->is_open = true;
    window->style = settings;
    window->local_graphics.context = frame_input.graphics_ctx;

    if (next_window_contents_size.x != 0) window->prev_content_size.x = next_window_contents_size.x;
    if (next_window_contents_size.y != 0) window->prev_content_size.y = next_window_contents_size.y;
    next_window_contents_size = {0, 0};

    window->prevprev_content_size = window->prev_content_size;

    //
    // Auto pos and sizing
    //
    {
        Rect rect_to_avoid = r;
        if (auto_width) {
            r.w = window->prev_content_size.x;
            if (r.w != 0) {
                if (!no_padding) r.w += window->style.TotalWidthPad();
                if (!auto_height) {
                    bool const needs_yscroll =
                        window->prev_content_size.y > (r.h - window->style.TotalHeightPad());
                    if (needs_yscroll) r.w += window->style.scrollbar_padding + window->style.scrollbar_width;
                }
            }
        }
        if (auto_height) {
            r.h = window->prev_content_size.y;
            if (r.h != 0) {
                if (!no_padding) r.h += window->style.TotalHeightPad();
                if (!auto_width) {
                    bool const needs_xscroll =
                        window->prev_content_size.x > (r.w - window->style.TotalWidthPad());
                    if (needs_xscroll) r.h += window->style.scrollbar_padding + window->style.scrollbar_width;
                }
            }
        }
        if (auto_pos) {
            f32x2 size = r.size;
            auto const scrollbar_size = window->style.scrollbar_width + window->style.scrollbar_padding;

            bool const needs_xscroll = window->prev_content_size.x > r.w;
            bool const needs_yscroll = window->prev_content_size.y > r.h;

            if (needs_yscroll) size.x += scrollbar_size;
            if (needs_xscroll) size.y += scrollbar_size;

            bool const has_parent_popup = curr_window && curr_window->flags & WindowFlags_Popup &&
                                          !(curr_window->flags & WindowFlags_ModalPopup);

            auto base_r = Rect {.pos = r.pos, .size = size};
            if (has_parent_popup && !(flags & WindowFlags_PositionOnTopOfParentPopup)) {
                rect_to_avoid = curr_window->bounds;
                rect_to_avoid.y = 0;
                rect_to_avoid.h = FLT_MAX;
                rect_to_avoid.x += 5; // we want the menus to overlap a little to show the layering
                rect_to_avoid.w -= 10;

                base_r.y -= window->style.pad_top_left.y;
            }

            auto avoid_r = rect_to_avoid;
            auto window_size = frame_input.window_size.ToFloat2();

            r.pos = BestPopupPos(base_r, avoid_r, window_size, has_parent_popup);
            r.pos = Trunc(r.pos);
        }
    }

    bool const has_no_width_or_height = r.h == 0 && r.w == 0;

    //
    // Init bounds
    //

    if (!(is_apopup || draw_on_top) && curr_window) RegisterAndConvertRect(&r);
    if (r.Bottom() > (f32)frame_input.window_size.height && is_apopup) {
        r.SetBottomByResizing((f32)frame_input.window_size.height - 1);
        f32 const scrollbar_size = window->style.scrollbar_width + window->style.scrollbar_padding;
        r.w += scrollbar_size;
        // IMPROVE: test properly sort what happens when a window is bigger than the screen
        auto_height = 0;
    }
    window->unpadded_bounds = r;
    window->visible_bounds = r;
    window->bounds = r;
    if (!no_padding && !has_no_width_or_height) {
        window->bounds.pos += window->style.pad_top_left;
        window->bounds.size -= window->style.TotalPadSize();
    }
    window->clipping_rect = window->bounds;

    //
    // Handle parent
    //

    window->parent_window = curr_window;
    window->root_window = window;
    if (window->parent_window != nullptr) {
        if (!is_apopup && !draw_on_top) {
            if (window->parent_window->root_window->flags & WindowFlags_Popup)
                window->flags |= WindowFlags_NestedInsidePopup;
            window->flags |= WindowFlags_Nested;

            Rect& vb = window->visible_bounds;
            Rect const& parent_clipping_r = window->parent_window->clipping_rect;
            vb.w = ::Min(parent_clipping_r.Right(), vb.Right()) - vb.x;

            f32 const bottom_of_parent = window->parent_window->clipping_rect.Bottom();
            f32 const bottom_of_this = window->visible_bounds.Bottom();
            if (bottom_of_parent < bottom_of_this)
                window->visible_bounds.h = bottom_of_parent - window->visible_bounds.y;
        }

        if (window->flags & (WindowFlags_Nested | WindowFlags_ChildPopup))
            window->root_window = window->parent_window->root_window;

        if (is_apopup || draw_on_top) {
            window->parent_window = nullptr;
            window->root_window = window;
        }
    }
    if (window->parent_window) dyn::Append(window->parent_window->children, window);

    if (window->root_window == window || is_apopup || draw_on_top) {
        window->child_nesting_counter = 0;
        window->nested_level = 0;
        window->graphics = &window->local_graphics;
    } else {
        window->root_window->child_nesting_counter++;
        window->nested_level = window->root_window->child_nesting_counter;
        window->graphics = &window->root_window->local_graphics;
    }
    window->graphics = &window->local_graphics;
    // window->graphics->is_dirty = true;
    window->graphics->BeginDraw();
    graphics = window->graphics;

    curr_window = window;
    dyn::Append(window_stack, window);

    //
    // Start drawing
    //

    if (is_apopup || draw_on_top) PushScissorStack();
    PushRectToCurrentScissorStack(
        window->visible_bounds); // temporarily while we doing drawing in this function
    PushID(window->id);

    if ((window->style.draw_routine_window_background ||
         (window->style.draw_routine_popup_background && is_apopup)) &&
        !DidPopupMenuJustOpen(window->id)) {
        if (is_apopup && window->style.draw_routine_popup_background)
            window->style.draw_routine_popup_background(*this, window);
        else
            window->style.draw_routine_window_background(*this, window);
    }

    //
    // > Scrollbars
    //

    f32 const scrollbar_size = window->style.scrollbar_width + window->style.scrollbar_padding;
    Rect bounds_for_scrollbar = window->bounds;
    f32 const epsilon = 0.75f;
    window->has_yscrollbar =
        window->prev_content_size.y > (bounds_for_scrollbar.h + epsilon) && !window->y_contents_was_auto;
    window->has_xscrollbar =
        window->prev_content_size.x > (bounds_for_scrollbar.w + epsilon) && !window->x_contents_was_auto;
    if (flags & WindowFlags_AlwaysDrawScrollX) {
        if (!window->has_xscrollbar) window->scroll_offset.x = 0;
        window->has_xscrollbar = true;
    }
    if (flags & WindowFlags_AlwaysDrawScrollY) {
        if (!window->has_yscrollbar) window->scroll_offset.y = 0;
        window->has_yscrollbar = true;
    }

    if (window->has_yscrollbar && !window->has_xscrollbar) {
        bounds_for_scrollbar.w -= scrollbar_size;

        if (window->prev_content_size.x > bounds_for_scrollbar.w && !no_scroll_x) {
            if (!window->x_contents_was_auto) {
                window->has_xscrollbar = true;
                bounds_for_scrollbar.h -= scrollbar_size;
            }
        }
    } else if (window->has_xscrollbar && !window->has_yscrollbar) {
        bounds_for_scrollbar.h -= scrollbar_size;

        if (window->prev_content_size.y > bounds_for_scrollbar.h) {
            if (!window->y_contents_was_auto) {
                window->has_yscrollbar = true;
                bounds_for_scrollbar.w -= scrollbar_size;
            }
        }
    } else if (window->has_xscrollbar && window->has_yscrollbar) {
        bounds_for_scrollbar.w -= scrollbar_size;
        bounds_for_scrollbar.h -= scrollbar_size;
    }

    if (window->has_yscrollbar && !auto_height && !no_scroll_y) {
        auto const result = Scrollbar(window,
                                      true,
                                      bounds_for_scrollbar.y + settings.scrollbar_padding_top,
                                      bounds_for_scrollbar.h - (settings.scrollbar_padding_top * 2),
                                      bounds_for_scrollbar.Right(),
                                      window->prev_content_size.y,
                                      window->scroll_offset.y,
                                      window->scroll_max.y,
                                      frame_input.cursor_pos.y);
        window->scroll_offset.y = result.new_scroll_value;
        window->scroll_max.y = result.new_scroll_max;

        window->clipping_rect.w -= scrollbar_size;
        window->bounds.w -= scrollbar_size;
    } else {
        window->scroll_offset.y = 0;
    }

    if (window->has_xscrollbar && !auto_width && !no_scroll_x) {
        auto const result = Scrollbar(window,
                                      false,
                                      bounds_for_scrollbar.x + settings.scrollbar_padding_top,
                                      bounds_for_scrollbar.w - (settings.scrollbar_padding_top * 2),
                                      bounds_for_scrollbar.Bottom(),
                                      window->prev_content_size.x,
                                      window->scroll_offset.x,
                                      window->scroll_max.x,
                                      frame_input.cursor_pos.x);
        window->scroll_offset.x = result.new_scroll_value;
        window->scroll_max.x = result.new_scroll_max;

        window->clipping_rect.h -= scrollbar_size;
        window->bounds.h -= scrollbar_size;
    } else {
        window->scroll_offset.x = 0;
    }

    //
    //
    //

    PopRectFromCurrentScissorStack();
    if (is_apopup || draw_on_top) PopScissorStack();

    if (!is_apopup && !draw_on_top) {
        // calculate the clipping region - we do this at the end because it might be effected by the
        // scrollbars
        if (window->parent_window) {
            window->clipping_rect.w =
                ::Min(window->parent_window->clipping_rect.Right(), window->clipping_rect.Right()) -
                window->clipping_rect.x;
            window->clipping_rect.h =
                ::Min(window->parent_window->clipping_rect.Bottom(), window->clipping_rect.Bottom()) -
                window->clipping_rect.y;
        }
    } else if (is_apopup) {
        dyn::Append(current_popup_stack, window);
        PushScissorStack();
    } else if (draw_on_top) {
        PushScissorStack();
    }
    PushRectToCurrentScissorStack(window->clipping_rect);
    window->prev_content_size = f32x2 {0, 0};
    window->x_contents_was_auto = true;
    window->y_contents_was_auto = true;
    if (auto_width) window->x_contents_was_auto = false;
    if (auto_height) window->y_contents_was_auto = false;

    RegisterRegionForMouseTracking(window->unpadded_bounds, false);
}

Window* Context::FindWindow(Id id) {
    for (auto const i : Range(windows.size))
        if (windows[i]->id == id) return windows[i];
    return nullptr;
}

void Context::EndWindow() {
    Window* window = Last(window_stack);
    if (window->prev_content_size.x != window->prevprev_content_size.x ||
        window->prev_content_size.y != window->prevprev_content_size.y) {
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }

    PopRectFromCurrentScissorStack();
    PopID();
    if (Last(window_stack)->flags & WindowFlags_Popup) {
        PopScissorStack();
        dyn::Pop(current_popup_stack);
    } else if (Last(window_stack)->flags & WindowFlags_DrawOnTop) {
        PopScissorStack();
    }
    window->graphics->EndDraw();
    dyn::Pop(window_stack);
    if (window_stack.size != 0) {
        curr_window = Last(window_stack);
        graphics = curr_window->graphics;
    } else {
        // should only happen in the End() function when the base window is ended
        curr_window = nullptr;
    }
}

bool Context::ScrollWindowToShowRectangle(Rect r) {
    if (!Rect::DoRectsIntersect(GetRegisteredAndConvertedRect(r),
                                CurrentWindow()->clipping_rect.ReducedVertically(r.h))) {
        SetYScroll(CurrentWindow(), Clamp(r.CentreY() - (Height() / 2), 0.0f, CurrentWindow()->scroll_max.y));
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

void Context::DisableScissor() {
    scissor_rect_is_active = false;
    OnScissorChanged();
}
void Context::EnableScissor() {
    scissor_rect_is_active = true;
    OnScissorChanged();
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
    if (id == 0) {
        active_text_input = id;
        stb_textedit_initialize_state(&stb_state, !multiline);
        ZeroMemory(textedit_text.data, sizeof(*textedit_text.data) * textedit_text.size);
    } else if (active_text_input != id) {
        active_text_input = id;
        SetImguiTextEditState(new_text, multiline);
        ResetTextInputCursorAnim();
    }
}

void Context::ResetTextInputCursorAnim() {
    text_cursor_is_shown = true;
    cursor_blink_counter = frame_input.current_time + k_text_cursor_blink_rate;
}

void Context::TextInputSelectAll() {
    stb_state.cursor = 0;
    stb_state.select_start = 0;
    stb_state.select_end = textedit_len;
    frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
}

void Context::SetActiveIDZero() { SetActiveID(0, false, {}, false); }

void Context::SetActiveID(Id id, bool closes_popups, ButtonFlags button_flags, bool check_for_release) {
    frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    temp_active_item.id = id;
    temp_active_item.closes_popups = closes_popups;
    temp_active_item.just_activated = (id != 0);
    temp_active_item.window = curr_window;
    temp_active_item.button_flags = button_flags;
    temp_active_item.check_for_release = check_for_release;

    if (id != 0) {
        // an id has been set so we no longer want to have a hot item
        SetHotRaw(0);
    } else {
        // unlike when activating an item - where we need a frame of lag, when
        // unactivating, we can immediately apply the changes
        active_item = {};
    }
}

// IMPROVE: calling OpenPopup without ever calling BeginWindowPopup causes weird behaviour
Window* Context::OpenPopup(Id id, Id creator_of_this_popup) {
    bool const is_first_popup = persistent_popup_stack.size == 0;
    auto popup = AddWindowIfNotAlreadyThere(id);
    popup->prev_content_size = f32x2 {0, 0};
    popup->creator_of_this_popup = is_first_popup ? 0 : creator_of_this_popup;

    popup_menu_just_created = id;
    dyn::Append(persistent_popup_stack, popup);
    focused_popup_window = popup;
    frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);

    return popup;
}

bool Context::BeginWindowPopup(WindowSettings settings, Id id, Rect r) {
    return BeginWindowPopup(settings, id, r, "");
}

bool Context::BeginWindowPopup(WindowSettings settings, Id id, Rect r, String name) {
    if (!IsPopupOpen(id)) return false;

    Window* popup = GetPopupFromID(id);
    settings.flags |= WindowFlags_Popup;

    auto curr_wnd = curr_window;
    auto is_first_of_wnd_stack = false;
    is_first_of_wnd_stack =
        !((curr_wnd->flags & WindowFlags_Popup) || (curr_wnd->flags & WindowFlags_NestedInsidePopup));

    if (!is_first_of_wnd_stack) {
        settings.flags |= WindowFlags_ChildPopup;
        popup->parent_popup = curr_wnd;
    }

    BeginWindow(settings, popup, r, name);
    return true;
}

bool Context::DidPopupMenuJustOpen(Id id) { return popup_menu_just_created == id; }

bool Context::IsPopupOpen(Id id) {
    bool const result = persistent_popup_stack.size > current_popup_stack.size &&
                        persistent_popup_stack[current_popup_stack.size]->id == id;
    return result;
}

void Context::ClosePopupToLevel(int remaining) {
    if (remaining > 0)
        focused_popup_window = persistent_popup_stack[(usize)remaining - 1];
    else if (persistent_popup_stack.size)
        focused_popup_window = persistent_popup_stack[0]->parent_window;

    ASSERT(remaining <= (int)persistent_popup_stack.size);
    dyn::Resize(persistent_popup_stack, (usize)remaining);
}

void Context::CloseTopPopupOnly() {
    ASSERT(persistent_popup_stack.size != 0);
    ClosePopupToLevel((int)persistent_popup_stack.size - 1);
}

// Close the popup we have begin-ed into.
void Context::CloseCurrentPopup() {
    int popup_index = (int)current_popup_stack.size - 1;
    if (popup_index < 0 || popup_index > (int)persistent_popup_stack.size ||
        current_popup_stack[(usize)popup_index]->id != persistent_popup_stack[(usize)popup_index]->id) {
        return;
    }
    while (popup_index > 0 && persistent_popup_stack[(usize)popup_index] &&
           (persistent_popup_stack[(usize)popup_index]->flags & WindowFlags_ChildPopup)) {
        popup_index--;
    }
    ClosePopupToLevel(popup_index);
}

//
//
//
//
//

void Context::DebugTextItem(char const* label, char const* text, ...) {
    static char buffer[512];
    va_list args;
    va_start(args, text);
    stbsp_vsnprintf(buffer, ::ArraySize(buffer), text, args);
    va_end(args);

    f32 const label_width = 150;
    f32 const x_pad = 10;
    auto const height = graphics->context->CurrentFontSize();

    Rect r = {.xywh {0, debug_y_pos, Width(), height}};
    RegisterAndConvertRect(&r);

    graphics->AddText(graphics->context->CurrentFont(),
                      graphics->context->CurrentFontSize(),
                      WindowPosToScreenPos({x_pad, debug_y_pos}),
                      0xffffffff,
                      FromNullTerminated(label),
                      label_width - 4);

    graphics->AddText(graphics->context->CurrentFont(),
                      graphics->context->CurrentFontSize(),
                      WindowPosToScreenPos({x_pad + label_width, debug_y_pos}),
                      0xffffffff,
                      FromNullTerminated(buffer),
                      Width() - (label_width + x_pad));
    debug_y_pos += height;
}

bool Context::DebugTextHeading(bool& state, char const* text) {
    f32 const height = graphics->context->CurrentFontSize() + 4;

    bool const clicked = Button(DefButton(),
                                {.xywh {0, debug_y_pos, Width(), height}},
                                GetID(FromNullTerminated(text)),
                                FromNullTerminated(text));
    debug_y_pos += height;
    if (clicked) state = !state;
    return state;
}

bool Context::DebugButton(char const* text) {
    f32 const height = graphics->context->CurrentFontSize() + 4;

    bool const clicked = ToggleButton(DefToggleButton(),
                                      {.xywh {0, debug_y_pos, Width(), height}},
                                      GetID(FromNullTerminated(text)),
                                      debug_show_register_widget_overlay,
                                      FromNullTerminated(text));
    debug_y_pos += height;
    return clicked;
}

void Context::DebugWindow(Rect r) {
    auto sets = DefWindow();
    sets.flags = 0;
    BeginWindow(sets, r, "TextWindow");

    frame_output.wants_keyboard_input = true;

    debug_y_pos = 0;

    DebugButton("Toggle Registered Widget Overlay");

    if (DebugTextHeading(debug_general, "General")) {
        DebugTextItem("Update", "%llu", frame_input.update_count);
        DebugTextItem("Key shift", "%d", (int)frame_input.modifiers.Get(ModifierKey::Shift));
        DebugTextItem("Key ctrl", "%d", (int)frame_input.modifiers.Get(ModifierKey::Ctrl));
        DebugTextItem("Key modifer", "%d", (int)frame_input.modifiers.Get(ModifierKey::Modifier));
        DebugTextItem("Key alt", "%d", (int)frame_input.modifiers.Get(ModifierKey::Alt));
        DebugTextItem("Time", "%lld", frame_input.current_time);
        DebugTextItem("WindowSize",
                      "%hu, %hu",
                      frame_input.window_size.width,
                      frame_input.window_size.height);
        DebugTextItem("Widgets", "%d", (int)frame_output.mouse_tracked_rects.size);

        debug_y_pos += graphics->context->CurrentFontSize() * 2;

        DebugTextItem("Timers:", "");
        for (auto& t : timed_wakeups)
            DebugTextItem("Time:", "%lld", t);
    }

    if (DebugTextHeading(debug_ids, "IDs")) {
        DebugTextItem("Active ID", "%u", GetActive());
        DebugTextItem("Hot ID", "%u", GetHot());
        DebugTextItem("Hovered ID", "%u", GetHovered());
        DebugTextItem("TextInput ID", "%u", GetTextInput());
    }

    if (DebugTextHeading(debug_popup, "Popups"))
        DebugTextItem("Persistent popups", "%d", (int)persistent_popup_stack.size);

    if (DebugTextHeading(debug_windows, "Windows")) {
        DebugTextItem("Hovered ID", "%u", HoveredWindow() ? HoveredWindow()->id : 0);
        DebugTextItem("Hovered Name",
                      "%s",
                      HoveredWindow() ? dyn::NullTerminated(HoveredWindow()->name) : "");
        DebugTextItem("Hovered Root", "%u", HoveredWindow() ? HoveredWindow()->root_window->id : 0);
        ArenaAllocatorWithInlineStorage<2000> allocator {Malloc::Instance()};
        DynamicArray<char> buffer {allocator};
        if (HoveredWindow()) {
            auto wnd = HoveredWindow();
            for (int i = 1; i < (int)::ArraySize(k_imgui_window_flag_text); ++i) {
                if (wnd->flags & k_imgui_window_flag_vals[i]) {
                    dyn::AppendSpan(buffer, k_imgui_window_flag_text[i]);
                    dyn::AppendSpan(buffer, " "_s);
                }
            }
        }
        DebugTextItem("Hovered CreatorID",
                      "%u",
                      HoveredWindow() ? HoveredWindow()->creator_of_this_popup : 0);
        if (HoveredWindow())
            DebugTextItem("Hovered Size",
                          "%.1f %.1f %.1f %.1f",
                          (f64)HoveredWindow()->unpadded_bounds.x,
                          (f64)HoveredWindow()->unpadded_bounds.y,
                          (f64)HoveredWindow()->unpadded_bounds.w,
                          (f64)HoveredWindow()->unpadded_bounds.h);
        else
            DebugTextItem("Hovered Size", "0 0 0 0");
        DebugTextItem("Hovered Flags", "%s", dyn::NullTerminated(buffer));
        debug_y_pos += graphics->context->CurrentFontSize() * 3;
    }

    EndWindow();
}

//
//
//
//
//

bool Context::Button(ButtonSettings settings, Rect r, Id id, String str) {
    RegisterAndConvertRect(&r);
    bool const clicked = ButtonBehavior(r, id, settings.flags);
    settings.draw(*this, r, id, str, false);

    return clicked;
}

bool Context::Button(ButtonSettings settings, Rect r, String str) {
    return Button(settings, r, GetID(str), str);
}

//
//
bool Context::ToggleButton(ButtonSettings settings, Rect r, Id id, bool& state, String str) {
    RegisterAndConvertRect(&r);
    bool const clicked = ButtonBehavior(r, id, settings.flags);
    if (clicked) state = !state;
    settings.draw(*this, r, id, str, state);

    return clicked;
}

//
//
bool Context::Slider(SliderSettings settings, Rect r, Id id, f32& percent, f32 def) {
    RegisterAndConvertRect(&r);
    bool const changed = SliderBehavior(r, id, percent, def, settings.sensitivity, settings.flags);
    settings.draw(*this, r, id, percent, &settings);

    return changed;
}

bool Context::SliderRange(SliderSettings settings, Rect r, Id id, f32 min, f32 max, f32& val, f32 def) {
    RegisterAndConvertRect(&r);
    bool const changed = SliderRangeBehavior(r, id, min, max, val, def, settings.sensitivity, settings.flags);
    auto percent = Map(val, min, max, 0, 1);
    settings.draw(*this, r, id, percent, &settings);

    return changed;
}

//
//
bool Context::PopupButton(ButtonFlags flags,
                          WindowSettings window_settings,
                          Rect r,
                          Id button_id,
                          Id popup_id) {
    RegisterAndConvertRect(&r);
    PopupButtonBehavior(r, button_id, popup_id, flags);
    return BeginWindowPopup(window_settings, popup_id, r, "popup button window");
}
bool Context::PopupButton(ButtonSettings settings, Rect r, Id button_id, Id popup_id, String str) {
    RegisterAndConvertRect(&r);
    PopupButtonBehavior(r, button_id, popup_id, settings.flags);

    settings.draw(*this, r, button_id, str, IsPopupOpen(popup_id) && hovered_window != curr_window);
    return BeginWindowPopup(settings.window, popup_id, r, str);
}

bool Context::PopupButton(ButtonSettings settings, Rect r, Id popup_id, String str) {
    return PopupButton(settings, r, GetID(str), popup_id, str);
}

//
//

TextInputResult Context::TextInput(TextInputSettings settings, Rect r, Id id, String str) {
    RegisterAndConvertRect(&r);
    auto edit =
        TextInput(r, id, str, settings.text_flags, settings.button_flags, settings.select_all_on_first_open);

    settings.draw(*this, r, id, edit.text, &edit);
    return edit;
}

Context::DraggerResult Context::TextInputDraggerCustom(TextInputDraggerSettings settings,
                                                       Rect r,
                                                       Id id,
                                                       String display_string,
                                                       f32 min,
                                                       f32 max,
                                                       f32& value,
                                                       f32 default_value) {
    DraggerResult result {};

    RegisterAndConvertRect(&r);

    auto text_edit_result = TextInput(r,
                                      id,
                                      display_string,
                                      settings.text_input_settings.text_flags,
                                      settings.text_input_settings.button_flags,
                                      settings.text_input_settings.select_all_on_first_open);

    if (text_edit_result.enter_pressed) result.new_string_value = text_edit_result.text;

    if (!TextInputHasFocus(id)) {
        if (SliderRangeBehavior(r,
                                id,
                                min,
                                max,
                                value,
                                default_value,
                                settings.slider_settings.sensitivity,
                                settings.slider_settings.flags))
            result.value_changed = true;
    }

    settings.slider_settings.draw(*this, r, id, Map(value, min, max, 0, 1), &settings.slider_settings);
    settings.text_input_settings.draw(*this, r, id, text_edit_result.text, &text_edit_result);

    return result;
}

bool Context::TextInputDraggerInt(TextInputDraggerSettings settings,
                                  Rect r,
                                  Id id,
                                  int min,
                                  int max,
                                  int& value,
                                  int default_value) {
    auto val = (f32)value;
    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};
    auto result = TextInputDraggerCustom(settings,
                                         r,
                                         id,
                                         fmt::Format(allocator, settings.format, value),
                                         (f32)min,
                                         (f32)max,
                                         val,
                                         (f32)default_value);
    if (result.new_string_value) {
        auto o = ParseInt(*result.new_string_value, ParseIntBase::Decimal);
        if (o.HasValue()) {
            value = Clamp((int)o.Value(), min, max);
            return true;
        }
    }

    if (result.value_changed) value = (int)val;
    return result.value_changed;
}

bool Context::TextInputDraggerFloat(TextInputDraggerSettings settings,
                                    Rect r,
                                    Id id,
                                    f32 min,
                                    f32 max,
                                    f32& value,
                                    f32 default_value) {
    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};
    auto result = TextInputDraggerCustom(settings,
                                         r,
                                         id,
                                         fmt::Format(allocator, settings.format, value),
                                         min,
                                         max,
                                         value,
                                         default_value);
    if (result.new_string_value) {
        auto o = ParseInt(*result.new_string_value, ParseIntBase::Decimal);
        if (o.HasValue()) {
            value = Clamp((f32)o.Value(), min, max);
            return true;
        }
    }

    return result.value_changed;
}

//
//
void Context::Text(TextSettings settings, Rect r, String str) {
    RegisterAndConvertRect(&r);
    settings.draw(*this, r, settings.col, str);
}

void Context::Textf(TextSettings settings, Rect r, char const* text, ...) {
    va_list args;
    va_start(args, text);
    Textf(settings, r, text, args);
    va_end(args);
}

void Context::Textf(TextSettings settings, Rect r, char const* text, va_list args) {
    static char buffer[512];
    stbsp_vsnprintf(buffer, ::ArraySize(buffer), text, args);
    Text(settings, r, buffer);
}

//
//
f32 Context::LargestStringWidth(f32 pad, void* items, int num, String (*GetStr)(void* items, int index)) {
    auto font = graphics->context->CurrentFont();
    f32 result = 0;
    for (auto const i : Range(num)) {
        auto str = GetStr(items, i);
        auto len = font->CalcTextSizeA(font->font_size, FLT_MAX, 0.0f, str).x;
        if (len > result) result = len;
    }
    return (f32)(int)(result + (pad * 2));
}

f32 Context::LargestStringWidth(f32 pad, Span<String const> strs) {
    auto str_get = [](void* items, int index) -> String {
        auto strs = (String const*)items;
        return strs[index];
    };

    return LargestStringWidth(pad, (void*)strs.data, (int)strs.size, str_get);
}

LiveEditGui g_live_edit_values {
    .ui_sizes =
        {
#define GUI_SIZE(cat, n, v, unit) v,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
        },
    .ui_sizes_units =
        {
#define GUI_SIZE(cat, n, v, unit) UiSizeUnit::unit,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
        },
    .ui_sizes_names =
        {
#define GUI_SIZE(cat, n, v, unit) #n,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
        },
    .ui_cols =
        {
#define GUI_COL(name, val, based_on, bright, alpha) {String(name), val, String(based_on), bright, alpha},
#include COLOURS_DEF_FILENAME
#undef GUI_COL
        },
    .ui_col_map =
        {
#define GUI_COL_MAP(cat, n, col, high_contrast_col) {String(col), String(high_contrast_col)},
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
        },
};

} // namespace imgui

namespace live_edit {

bool g_high_contrast_gui = false;

} // namespace live_edit
