// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "draw_list.hpp"
#include "gui_frame.hpp"
#include "gui_live_edit.hpp"

namespace imgui {

struct Context;
struct Window;
struct SliderSettings;
struct TextInputResult;

using Id = u32;
using Char32 = u32;
using WindowFlags = u32;

#undef STB_TEXTEDIT_STRING
#undef STB_TEXTEDIT_CHARTYPE
#define STB_TEXTEDIT_STRING   Context
#define STB_TEXTEDIT_CHARTYPE Char32
#define STB_TEXTEDIT_IS_SPACE IsSpaceU32
#undef INCLUDE_STB_TEXTEDIT_H
#undef STB_TEXTEDIT_IMPLEMENTATION
namespace stb {
#include <stb_textedit.h>
}

constexpr Id k_imgui_misc_id = 1; // something we dont care about was clicked (eg. background)
constexpr Id k_imgui_app_window_id = 4; // id of the full size window created with Begin()

// TODO: refactor into a bitfield probably
#define IMGUI_WINDOW_FLAGS                                                                                   \
    X(None, 0, 0)                                                                                            \
    X(NoPadding, 1, 1)                                                                                       \
    X(NeverClosesPopup, 1, 2)                                                                                \
    X(AutoWidth, 1, 3)                                                                                       \
    X(AutoHeight, 1, 4)                                                                                      \
    X(AutoPosition, 1, 5)                                                                                    \
    X(DontCloseWithExternalClick, 1, 6)                                                                      \
    X(NoScrollbarX, 1, 7)                                                                                    \
    X(NoScrollbarY, 1, 8)                                                                                    \
    X(DrawOnTop, 1, 9)                                                                                       \
    X(DrawingOnly, 1, 10)                                                                                    \
    X(NestedInsidePopup, 1, 11)                                                                              \
    X(Popup, 1, 12)                                                                                          \
    X(ChildPopup, 1, 13)                                                                                     \
    X(Nested, 1, 14)                                                                                         \
    X(AlwaysDrawScrollX, 1, 15)                                                                              \
    X(AlwaysDrawScrollY, 1, 16)                                                                              \
    X(PositionOnTopOfParentPopup, 1, 17)                                                                     \
    X(ModalPopup, 1, 18)

enum WindowFlagsEnum : u32 {
#define X(name, val, num) WindowFlags_##name = val << num,
    IMGUI_WINDOW_FLAGS
#undef X
};

constexpr u32 k_imgui_window_flag_vals[] = {
#define X(name, val, num) val << num,
    IMGUI_WINDOW_FLAGS
#undef X
};

constexpr String k_imgui_window_flag_text[] = {
#define X(name, val, num) #name,
    IMGUI_WINDOW_FLAGS
#undef X
};

struct ButtonFlags {
    bool32 closes_popups : 1; // when inside a popup, close it when this button is clicked
    bool32 left_mouse : 1;
    bool32 right_mouse : 1;
    bool32 middle_mouse : 1;
    bool32 double_click : 1;
    bool32 ignore_double_click : 1;
    bool32 triggers_on_mouse_down : 1;
    bool32 triggers_on_mouse_up : 1;
    bool32 requires_modifer : 1;
    bool32 requires_shift : 1;
    bool32 requires_alt : 1;
    bool32 disabled : 1;
    bool32 is_non_window_content : 1; // is something that does not live inside a window (e.g. scrollbar)
    bool32 hold_to_repeat : 1;
    bool32 dont_check_for_release : 1;
    bool32 padding : 17 = 0; // We want to cast this to bool32 so we need to ensure padding is 0.
};
static_assert(sizeof(ButtonFlags) == 4, "Adjust padding");

bool ClickCheck(ButtonFlags flags, GuiFrameInput const& io, Rect const* rect = nullptr);

struct SliderFlags {
    bool32 slower_with_shift : 1;
    bool32 default_on_modifer : 1;
};

struct TextInputFlags {
    f32 x_padding = 4;
    bool32 chars_decimal : 1; // Allow 0123456789.+-*/
    bool32 chars_hexadecimal : 1; // Allow 0123456789ABCDEFabcdef
    bool32 chars_uppercase : 1; // Turn a..z into A..Z
    bool32 chars_no_blank : 1; // Filter out spaces, tabs
    bool32 tab_focuses_next_input : 1;
    bool32 centre_align : 1;
    bool32 multiline : 1;
    bool32 multiline_wordwrap_hack : 1; // quick and dirty word wrap
};

//
//
//

#define IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS                                                                     \
    MAYBE_UNUSED const imgui::Context &imgui, MAYBE_UNUSED Rect bounds, MAYBE_UNUSED Rect handle_rect,       \
        MAYBE_UNUSED imgui::Id id
#define IMGUI_DRAW_WINDOW_SCROLLBAR(name) void name(IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS)
using DrawWindowScrollbar = void(IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS);

#define IMGUI_DRAW_WINDOW_BG_ARGS                                                                            \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wshadow\"")                        \
        MAYBE_UNUSED const imgui::Context &imgui,                                                            \
        MAYBE_UNUSED imgui::Window *window _Pragma("clang diagnostic pop")
#define IMGUI_DRAW_WINDOW_BG_ARGS_TYPES const imgui::Context&, imgui::Window*
#define IMGUI_DRAW_WINDOW_BG(name)      void name(IMGUI_DRAW_WINDOW_BG_ARGS)
using DrawWindowBackground = void(IMGUI_DRAW_WINDOW_BG_ARGS);

#define IMGUI_DRAW_BUTTON_ARGS                                                                               \
    MAYBE_UNUSED const imgui::Context &imgui, MAYBE_UNUSED Rect r, MAYBE_UNUSED imgui::Id id,                \
        MAYBE_UNUSED String str, MAYBE_UNUSED bool state
#define IMGUI_DRAW_BUTTON(name) void name(IMGUI_DRAW_BUTTON_ARGS)
using DrawButton = void(IMGUI_DRAW_BUTTON_ARGS);

#define IMGUI_DRAW_SLIDER_ARGS                                                                               \
    MAYBE_UNUSED const imgui::Context &imgui, MAYBE_UNUSED Rect r, MAYBE_UNUSED imgui::Id id,                \
        MAYBE_UNUSED f32 percent, MAYBE_UNUSED const imgui::SliderSettings *settings
#define IMGUI_DRAW_SLIDER(name) void name(IMGUI_DRAW_SLIDER_ARGS)
using DrawSlider = void(IMGUI_DRAW_SLIDER_ARGS);

#define IMGUI_DRAW_TEXT_INPUT_ARGS                                                                           \
    MAYBE_UNUSED const imgui::Context &imgui, MAYBE_UNUSED Rect r, MAYBE_UNUSED imgui::Id id,                \
        MAYBE_UNUSED String text, MAYBE_UNUSED imgui::TextInputResult *result
#define IMGUI_DRAW_TEXT_INPUT(name) void name(IMGUI_DRAW_TEXT_INPUT_ARGS)
using DrawTextInput = void(IMGUI_DRAW_TEXT_INPUT_ARGS);

#define IMGUI_DRAW_TEXT_ARGS  const imgui::Context &imgui, Rect r, u32 col, String str
#define IMGUI_DRAW_TEXT(name) void name(IMGUI_DRAW_TEXT_ARGS)
using DrawText = void(IMGUI_DRAW_TEXT_ARGS);

struct WindowSettings {
    f32 TotalWidthPad() { return pad_top_left.x + pad_bottom_right.x; }
    f32 TotalHeightPad() { return pad_top_left.y + pad_bottom_right.y; }
    f32x2 TotalPadSize() { return pad_top_left + pad_bottom_right; }

    WindowFlags flags;
    f32x2 pad_top_left;
    f32x2 pad_bottom_right;
    f32 scrollbar_padding;
    f32 scrollbar_padding_top;
    f32 scrollbar_width;
    DrawWindowScrollbar* draw_routine_scrollbar;
    TrivialFixedSizeFunction<48, void(IMGUI_DRAW_WINDOW_BG_ARGS)> draw_routine_window_background;
    TrivialFixedSizeFunction<48, void(IMGUI_DRAW_WINDOW_BG_ARGS)> draw_routine_popup_background;
};

struct ButtonSettings {
    ButtonFlags flags;
    TrivialFixedSizeFunction<24, void(IMGUI_DRAW_BUTTON_ARGS)> draw;
    WindowSettings window;
};

struct TextSettings {
    DrawText* draw;
    u32 col;
};

struct SliderSettings {
    SliderFlags flags;
    // Pixels for a value change of 1.0. Lots of sliders use the range 0-1 anyways to this represents the
    // pixels for a full turn.
    f32 sensitivity;
    TrivialFixedSizeFunction<24, void(IMGUI_DRAW_SLIDER_ARGS)> draw;
};

struct TextInputSettings {
    ButtonFlags button_flags;
    TextInputFlags text_flags;
    bool select_all_on_first_open;
    TrivialFixedSizeFunction<24, void(IMGUI_DRAW_TEXT_INPUT_ARGS)> draw;
};

struct TextInputDraggerSettings {
    TextInputSettings text_input_settings;
    SliderSettings slider_settings;
    String format;
};

//
//
//

struct Window {
    DynamicArrayBounded<char, 128> name;
    bool is_open = false;
    bool skip_drawing_this_frame = false;

    graphics::DrawList local_graphics = {};
    graphics::DrawList* graphics = nullptr;

    bool has_been_sorted = false; // internal

    Window* root_window = nullptr;
    Window* parent_window = nullptr;
    DynamicArray<Window*> children {Malloc::Instance()};

    Window* parent_popup = nullptr;
    Id creator_of_this_popup = 0;

    int nested_level = 0;
    int child_nesting_counter = 0;

    WindowFlags flags = 0;
    u32 user_flags = 0; // optional user storage

    WindowSettings style = {};

    // all bounds are in absolute coordinates - never relative to parent windows
    Rect bounds = {}; // the windows region minus padding, this is probably the one you want to use for
                      // positioning/sizing your gui
    Rect unpadded_bounds = {}; // the whole window
    Rect visible_bounds = {}; // the region of the window that is visible on the screen IMPROVE: fix bug when
                              // child window is scroll down - the top of the visible bounds in not clipped
    Rect clipping_rect = {}; // the area that can be drawn to when in a begin/end block

    Id id = 0;

    bool x_contents_was_auto = false; // internal
    bool y_contents_was_auto = false; // internal

    f32x2 prev_content_size = {}; // the size of the stuff put into the window from last frame, this is
                                  // updated when a component calls Register
    f32x2 prevprev_content_size = {}; // the size of the stuff put into the window from last frame, this
                                      // is updated when a component calls Register

    f32x2 scroll_offset = {}; // the pixel offset from scrollbars
    f32x2 scroll_max = {};
    bool has_yscrollbar = false;
    bool has_xscrollbar = false;
};

struct ActiveItem {
    Id id = 0;
    // ActiveItemType type = ActiveItem_None;
    bool closes_popups = true;
    bool just_activated = false;
    Window* window = nullptr;

    bool check_for_release = false;
    ButtonFlags button_flags {};
};

struct TextInputResult {
    bool HasSelection() const { return selection_start != selection_end; }

    Rect GetCursorRect() const {
        ASSERT(show_cursor);
        return cursor_rect;
    }

    f32x2 GetTextPos() const { return text_pos; }

    struct SelectionIterator {
        graphics::DrawContext& draw_ctx;
        char const* pos;
        u32 remaining_chars;
        u32 line_index;
        bool reached_end;
    };
    Optional<Rect> NextSelectionRect(SelectionIterator& it) const;

    bool enter_pressed {};
    bool buffer_changed {};

    f32x2 text_pos = {};
    Rect cursor_rect = {};
    // temporary, changes when TextInput is called again, copy this into your own buffer if
    // buffer_changed is true
    String text {};

    int cursor = 0;
    int selection_start = 0;
    int selection_end = 0;
    bool show_cursor = false;
};

extern LiveEditGui g_live_edit_values;

struct Context {
    Context(GuiFrameInput& frame_input, GuiFrameResult& frame_output);
    ~Context();

    void Begin(WindowSettings settings); // Call at the start of the frame
    void End(ArenaAllocator& scratch_arena); // Call at the end of the frame

    //
    // > Widget Behaviours
    //

    //
    // Sliders
    //
    // Returns true when the slider value changes
    bool SliderBehavior(Rect r, Id id, f32& percent, SliderFlags flags);
    bool SliderBehavior(Rect r, Id id, f32& percent, f32 default_percent, SliderFlags flags);
    bool SliderBehavior(Rect r, Id id, f32& percent, f32 default_percent, f32 sensitivity, SliderFlags flags);

    bool SliderRangeBehavior(Rect r, Id id, f32 min, f32 max, f32& value, SliderFlags flags);
    bool
    SliderRangeBehavior(Rect r, Id id, f32 min, f32 max, f32& value, f32 default_value, SliderFlags flags);
    bool SliderRangeBehavior(Rect r,
                             Id id,
                             f32 min,
                             f32 max,
                             f32& value,
                             f32 default_value,
                             f32 sensitivity,
                             SliderFlags flags);

    bool SliderRangeBehavior(Rect r,
                             Id id,
                             int min,
                             int max,
                             int& value,
                             int default_value,
                             f32 sensitivity,
                             SliderFlags flags);

    bool
    SliderUnboundedBehavior(Rect r, Id id, f32& val, f32 default_val, f32 sensitivity, SliderFlags flags);

    //
    // Buttons
    //
    // Returns true when clicked, the conditions that determine 'clicked' are set in the flags
    bool ButtonBehavior(Rect r, Id id, ButtonFlags flags);

    // Opens a popup which appears in an appropriate place relative to the rect passed here
    // You must call BeginWindowPopup on the popup_id after calling this
    bool PopupButtonBehavior(Rect r, Id button_id, Id popup_id, ButtonFlags flags);

    //
    // Text Input
    //
    TextInputResult
    TextInput(Rect r, Id id, String text, TextInputFlags flags, ButtonFlags button_flags, bool select_all);

    Id GetTextInput() const { return active_text_input; }
    bool TextInputHasFocus(Id id) const;
    bool TextInputJustFocused(Id id) const;
    bool TextInputJustUnfocused(Id id) const;
    void SetImguiTextEditState(String new_text, bool multiline);
    void SetTextInputFocus(Id id, String new_text, bool multiline); // pass 0 to unfocus
    void TextInputSelectAll();
    void ResetTextInputCursorAnim();

    //
    // > Windows
    //

    Window* CurrentWindow() const { return window_stack.size ? Last(window_stack) : nullptr; }
    Window* HoveredWindow() const { return hovered_window; }
    f32 X() const { return curr_window->bounds.x; }
    f32 Y() const { return curr_window->bounds.y; }
    f32 Width() const { return curr_window->bounds.w; }
    f32 Height() const { return curr_window->bounds.h; }
    Rect Bounds() const { return curr_window->bounds; }
    f32x2 Size() const { return curr_window->bounds.size; }
    f32x2 Min() const { return curr_window->bounds.pos; }
    f32x2 Max() const { return curr_window->bounds.Max(); }

    void BeginWindow(WindowSettings settings, Rect r, String str);
    void BeginWindow(WindowSettings settings, Id id, Rect r);
    void BeginWindow(WindowSettings settings, Id id, Rect r, String str);
    void BeginWindow(WindowSettings settings, Window* window, Rect r);
    void BeginWindow(WindowSettings settings, Window* window, Rect r, String str);

    void EndWindow();

    Window* FindWindow(Id id);

    void SetYScroll(Window* window, f32 val) {
        window->scroll_offset.y = val;
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }
    void SetXScroll(Window* window, f32 val) {
        window->scroll_offset.x = val;
        frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
    }
    bool ScrollWindowToShowRectangle(Rect r);

    bool WasWindowJustCreated(Window* window);
    bool WasWindowJustCreated(Id id);

    bool WasWindowJustHovered(Window* window);
    bool WasWindowJustHovered(Id id);
    bool WasWindowJustUnhovered(Window* window);
    bool WasWindowJustUnhovered(Id id);
    bool IsWindowHovered(Window* window);
    bool IsWindowHovered(Id id);

    //
    // > Popups (a type of window)
    //

    // Opens a popup ready to be 'Begin'ed into
    Window* OpenPopup(Id id, Id creator_of_this_popup = 0); // returns the window for convenience
    // Begins a popup window - EndWindow() must be called if this returns true.
    bool BeginWindowPopup(WindowSettings settings, Id id, Rect r);
    bool BeginWindowPopup(WindowSettings settings, Id id, Rect r, String str);

    void SetMinimumPopupSize(f32 width, f32 height); // use this in a BeginWindowPopup block
    bool IsPopupOpen(Id id);

    void ClosePopupToLevel(int remaining);
    void CloseCurrentPopup(); // closes the whole popup stack
    void CloseTopPopupOnly(); // just closes the top popup
    bool DidPopupMenuJustOpen(Id id);
    Window* GetPopupFromID(Id id);

    f32 LargestStringWidth(f32 pad, void* items, int num, String (*GetStr)(void* items, int index));
    f32 LargestStringWidth(f32 pad, Span<String const> strs);

    //
    // > IDs
    //

    // Hashes an input and returns a ID. This id affected by the whole id stack,
    // so the hash will be different depending on what is pushed onto the id stack
    // before with PushID()/PopID()
    Id GetID(String str);
    Id GetID(uintptr num);

    void PushID(String str);
    void PushID(uintptr num);
    void PopID();

    bool IsActive(Id id) const;
    bool WasJustActivated(Id id);
    bool WasJustDeactivated(Id id);
    bool AnItemIsActive();
    Id GetActive() { return active_item.id; }

    bool IsHot(Id id) const;
    bool WasJustMadeHot(Id id);
    bool WasJustMadeUnhot(Id id);
    bool AnItemIsHot();
    Id GetHot() { return hot_item; }
    f64 SecondsSpentHot() {
        return time_when_turned_hot ? frame_input.current_time - time_when_turned_hot : 0;
    }

    bool IsHotOrActive(Id id) const { return IsHot(id) || IsActive(id); }

    // 'Is the cursor over the given ID' is often the same as IsHot(). But unlike the hot item,
    // there can be both an active item and a hovered item at the same time, you most likely
    // want to check IsHot for when you are drawing.
    bool IsHovered(Id id);
    bool WasJustHovered(Id id);
    bool WasJustUnhovered(Id id);
    Id GetHovered() { return hovered_item; }

    //
    // > Rects
    //

    // is the rectangle visible at all on the GUI - useful for efficiency purposes
    bool IsRectVisible(Rect r);
    Rect GetCurrentClipRect() { return current_scissor_rect; }

    void PushRectToCurrentScissorStack(Rect const& r);
    void PopRectFromCurrentScissorStack();
    void PushScissorStack();
    void PopScissorStack();
    void DisableScissor();
    void EnableScissor();

    //
    // > Misc
    //

    // Similar to CSS vw units, we have a concept of 'viewport width' relative units. They allow us to
    // make our GUI scale with the window size.
    f32 VwToPixels(f32 vw) const {
        ASSERT_HOT(vw >= 0);
        return vw * pixels_per_vw;
    }
    f32x2 VwToPixels(f32x2 vw) const {
        ASSERT_HOT(All(vw >= 0.0f));
        return vw * pixels_per_vw;
    }
    f32 PixelsToVw(f32 pixels) const {
        ASSERT_HOT(pixels_per_vw >= 0);
        return pixels / pixels_per_vw;
    }
    f32x2 PixelsToVw(f32x2 pixels) const {
        ASSERT_HOT(All(pixels >= 0.0f));
        return pixels / pixels_per_vw;
    }
    void SetPixelsPerVw(f32 v) {
        ASSERT(v > 0);
        pixels_per_vw = v;
    }

    // FIXME: 'Screen' coordinates is not the right term. It's actually coordinates relative to the top-left
    // of the GUI window, as opposed to 'Window' coordinates which are relative to the top-left of the current
    // imgui::Context window.
    f32x2 WindowPosToScreenPos(f32x2 rel_pos);
    f32x2 ScreenPosToWindowPos(f32x2 screen_pos);
    Rect WindowRectToScreenRect(Rect rel_rect) {
        return {.pos = WindowPosToScreenPos(rel_rect.pos), .size = rel_rect.size};
    }
    Rect ScreenRectToWindowRect(Rect screen_rect) {
        return {.pos = ScreenPosToWindowPos(screen_rect.pos), .size = screen_rect.size};
    }

    void RegisterToWindow(Rect window_bounds);

    // Every widget should use these before doing *Behaviour calls or drawing. It does 2 things:
    // - Converts coordinates.
    // - Registers the rectangle with the current window (so the window knows scrollbars, clipping, etc).
    Rect GetRegisteredAndConvertedRect(Rect r);
    void RegisterAndConvertRect(Rect* r);

    void AddTimedWakeup(TimePoint time_ms, char const* timer_name);
    bool WakeupAtTimedInterval(TimePoint& counter, f64 interval_seconds); // Returns true when it ticks

    //
    // > High level funcs
    //

    bool Button(ButtonSettings settings, Rect r, Id id, String str);
    bool Button(ButtonSettings settings, Rect r, String str);

    bool ToggleButton(ButtonSettings settings, Rect r, Id id, bool& state, String str);

    bool Slider(SliderSettings settings, Rect r, Id id, f32& percent, f32 def);
    bool SliderRange(SliderSettings settings, Rect r, Id id, f32 min, f32 max, f32& val, f32 def);

    bool PopupButton(ButtonFlags flags, WindowSettings window_settings, Rect r, Id button_id, Id popup_id);
    bool PopupButton(ButtonSettings settings, Rect r, Id popup_id, String str);
    bool PopupButton(ButtonSettings settings, Rect r, Id button_id, Id popup_id, String str);

    TextInputResult TextInput(TextInputSettings settings, Rect r, Id id, String text);

    bool TextInputDraggerInt(TextInputDraggerSettings settings,
                             Rect r,
                             Id id,
                             int min,
                             int max,
                             int& value,
                             int default_value = 0);
    bool TextInputDraggerFloat(TextInputDraggerSettings settings,
                               Rect r,
                               Id id,
                               f32 min,
                               f32 max,
                               f32& value,
                               f32 default_value = 0);

    struct DraggerResult {
        bool value_changed {};
        Optional<String> new_string_value {};
    };
    [[nodiscard]] DraggerResult TextInputDraggerCustom(TextInputDraggerSettings settings,
                                                       Rect r,
                                                       Id id,
                                                       String display_string,
                                                       f32 min,
                                                       f32 max,
                                                       f32& value,
                                                       f32 default_value);

    void Text(TextSettings settings, Rect r, String str);
    void Textf(TextSettings settings, Rect r, char const* text, ...);
    void Textf(TextSettings settings, Rect r, char const* text, va_list args);

    //

    f32 debug_y_pos = 0;
    bool debug_ids = true;
    bool debug_perf = true;
    bool debug_windows = true;
    bool debug_general = true;
    bool debug_popup = true;
    bool debug_show_register_widget_overlay = false;

    void DebugTextItem(char const* label, char const* text, ...);
    bool DebugTextHeading(bool& state, char const* text);

    bool DebugButton(char const* text);
    void DebugWindow(Rect r);

    //

    bool
    RegisterRegionForMouseTracking(Rect r,
                                   bool check_intersection = true); // returns true if the widget is visible

    // is_not_window_content == something that is not inside a window, but part of it e.g. scrollbar
    bool SetHot(Rect r, Id id, bool32 is_not_window_content = false);
    void SetActiveID(Id id, bool closes_popups, ButtonFlags button_flags, bool check_for_release);
    void SetActiveIDZero();

    // adds a window to the window array, returns the new (or found) window
    Window* AddWindowIfNotAlreadyThere(Id id);
    void SetHotRaw(Id id);

    struct ScrollbarResult {
        f32 new_scroll_value;
        f32 new_scroll_max;
    };
    [[nodiscard]] ScrollbarResult Scrollbar(Window* window,
                                            bool is_vertical,
                                            f32 window_y,
                                            f32 window_h,
                                            f32 window_right,
                                            f32 content_size_y,
                                            f32 y_scroll_value,
                                            f32 y_scroll_max,
                                            f32 cursor_y);
    void HandleHoverPopupOpeningAndClosing(Id id);
    void OnScissorChanged() const;

    //
    //
    //
    static constexpr f64 k_text_cursor_blink_rate {0.5};
    f64 hover_popup_delay {0.10}; // delay before popups close
    f64 button_repeat_rate {0.1}; // rate at which button hold to repeat triggers
    WindowSettings default_window_style = {};
    f32 pixels_per_vw = 1.0f;

    graphics::DrawList* graphics = nullptr; // Shortcut to the current windows graphics
    GuiFrameInput& frame_input;
    GuiFrameResult& frame_output;

    DynamicArray<MouseTrackedRect> mouse_tracked_rects {Malloc::Instance()}; // internal
    DynamicArray<char> clipboard_for_os {Malloc::Instance()}; // internal
    DynamicArray<TimePoint> timed_wakeups {Malloc::Instance()}; // internal

    u32 frame_counter = 0;

    graphics::DrawList overlay_graphics = {};

    // we need a way for drawing functions that only have access to imgui::Context to get images from
    // your external object
    graphics::ImageID (*get_image_callback)(void*, int) = nullptr;
    void* user_callback_data = nullptr;

    char temp_buffer[1024];
    Char32 w_temp_buffer[1024];

    f32x2 cached_pos = {}; // misc cached variable

    Window* debug_window_to_inspect = nullptr;

    graphics::DrawData draw_data = {}; // output draw data

    // input text

    stb::STB_TexteditState stb_state = {};
    Id active_text_input = 0;
    Id prev_active_text_input = 0;
    bool text_cursor_is_shown = true;
    bool tab_to_focus_next_input = false;
    bool tab_just_used_to_focus = false;
    DynamicArray<Char32> textedit_text {Malloc::Instance()};
    DynamicArray<char> textedit_text_utf8 {Malloc::Instance()};
    int textedit_len = 0;
    bool active_text_input_shown = false; // Unfocus active input if it's not shown in the frame

    // window
    DynamicArray<Window*> sorted_windows {Malloc::Instance()}; // internal
    DynamicArray<Window*> active_windows {Malloc::Instance()}; // internal
    DynamicArray<graphics::DrawList*> output_draw_lists {Malloc::Instance()}; // internal

    // storage of windows, grows whenever a different BeginWindow is called for the first time
    // this array actually owns each window pointer, and will delete them when finished
    DynamicArray<Window*> windows {Malloc::Instance()};

    DynamicArray<Window*> window_stack {Malloc::Instance()}; // grows to show the layering of the windows,
                                                             // should always start and end frames empty
    Window* curr_window = nullptr; // pushed/popped to represent what window is currently active
    Window* hovered_window = nullptr; // at the beginning of the frame find which layer the mouse is over
                                      // using the rects from last frame
    Window* hovered_window_last_frame = nullptr;
    Window* hovered_window_content =
        nullptr; // used to differentiate between when the mouse is over the padding of the window
    Window* focused_popup_window = nullptr;
    Window* window_just_created = nullptr;

    // next window
    WindowSettings next_window_style =
        {}; // will be set back to default_window_style every BeginWindow() call
    u32 next_window_user_flags = 0; // SetNextWindowUserFlags()
    f32x2 next_window_contents_size = {}; // SetNextWindowContentSize()

    // popups
    // If there are multiple popups open they are in a single stack, with each having at most one child and
    // one parent.
    DynamicArray<Window*> persistent_popup_stack {Malloc::Instance()};
    DynamicArray<Window*> current_popup_stack {Malloc::Instance()};
    Id popup_menu_just_created =
        0; // IMPROVE: could there be multiple popups created? in which case we might need to store
           // a stack of ids in order to ensure DidPopupMenuJustOpen performs correctly
    Id prev_popup_menu_just_created = 0;
    Id prevprev_popup_menu_just_created = 0;
    s64 popup_hover_counter = 0;
    // ID next_idopens_apopup = 0; // push and pop this value so that any ids behave correctly with popup
    // hover to open int64_t popup_opened_timer = 0; Window *new_popup_window = nullptr; ID
    // popup_opened_timer_id = 0; ID id_that_just_opened_apopup = 0; bool inside_popup_triangle = false;

    // scissor and layers
    DynamicArray<DynamicArray<Rect>> scissor_stacks {Malloc::Instance()};
    Rect current_scissor_rect = {};
    bool scissor_rect_is_active = false;

    TimePoint button_repeat_counter = {};
    TimePoint cursor_blink_counter {};

    // ids
    //
    // we use temp variables to add a frame of lag so that you can layer widgets
    // on top of each other and the behaviour is as expected. if we don't do this,
    // if you put a button on top of another button, they will both highlight on
    // on hovering.
    Id active_item_last_frame = 0;
    Id hot_item_last_frame = 0;
    Id hovered_item_last_frame = 0;

    Id hot_item = 0;
    Id temp_hot_item = 0;
    TimePoint time_when_turned_hot = {};

    Id hovered_item = 0;
    Id temp_hovered_item = 0;

    ActiveItem active_item = {};
    ActiveItem temp_active_item = {};

    Id dragged_id = 0;
    int dragged_value = 0;
    int dragged_identifier = 0;
    int dragged_mouse = -1;

    DynamicArray<Id> id_stack {Malloc::Instance()};
};

f32x2 BestPopupPos(Rect base_r, Rect avoid_r, f32x2 window_size, bool find_left_or_right);

PUBLIC IMGUI_DRAW_WINDOW_BG(DefaultDrawWindowBackground) {
    auto r = window->unpadded_bounds;
    imgui.graphics->AddRectFilled(r.Min(), r.Max(), 0xff202020);
    imgui.graphics->AddRect(r.Min(), r.Max(), 0xffffffff);
}

PUBLIC IMGUI_DRAW_WINDOW_BG(DefaultDrawPopupBackground) {
    auto r = window->unpadded_bounds;
    imgui.graphics->AddRectFilled(r.Min(), r.Max(), 0xff202020);
    imgui.graphics->AddRect(r.Min(), r.Max(), 0xffffffff);
}

PUBLIC IMGUI_DRAW_WINDOW_SCROLLBAR(DefaultDrawWindowScrollbar) {
    imgui.graphics->AddRectFilled(bounds.Min(), bounds.Max(), 0xff404040);
    u32 col = 0xffe5e5e5;
    if (imgui.IsHot(id))
        col = 0xffffffff;
    else if (imgui.IsActive(id))
        col = 0xffb5b5b5;
    imgui.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), col);
}

PUBLIC IMGUI_DRAW_BUTTON(DefaultDrawButton) {
    u32 col = 0xffd5d5d5;
    if (imgui.IsHot(id)) col = 0xfff0f0f0;
    if (imgui.IsActive(id)) col = 0xff808080;
    if (state) col = 0xff808080;
    imgui.graphics->AddRectFilled(r.Min(), r.Max(), col);

    auto font_size = imgui.graphics->context->CurrentFontSize();
    auto pad = (f32)imgui.frame_input.window_size.width / 200.0f;
    imgui.graphics->AddText(f32x2 {r.x + pad, r.y + (r.h / 2 - font_size / 2)}, 0xff000000, str);
}

PUBLIC IMGUI_DRAW_BUTTON(DefaultDrawPopupButton) {
    (void)state;
    u32 col = 0xffd5d5d5;
    if (imgui.IsHot(id)) col = 0xfff0f0f0;
    if (imgui.IsActive(id)) col = 0xff808080;
    imgui.graphics->AddRectFilled(r.Min(), r.Max(), col);

    auto font_size = imgui.graphics->context->CurrentFontSize();
    imgui.graphics->AddText(f32x2 {r.x + 4, r.y + ((r.h - font_size) / 2)}, 0xff000000, str);

    imgui.graphics->AddTriangleFilled({r.Right() - 14, r.y + 4},
                                      {r.Right() - 4, r.y + (r.h / 2)},
                                      {r.Right() - 14, r.y + r.h - 4},
                                      0xff000000);
}

PUBLIC void DefaultDrawSlider(Context const& s, Rect r, Id, f32 percent, SliderSettings const*) {
    s.graphics->AddRectFilled(r.Min(), r.Max(), 0xffffffff);

    s.graphics->AddRectFilled(f32x2 {r.x, (r.y + r.h) - (percent * r.h)}, r.Max(), 0xff3f3f3f);
}

PUBLIC void DefaultDrawTextInput(Context const& s, Rect r, Id id, String text, TextInputResult* result) {
    u32 col = 0xffffffff;
    if (s.IsHot(id) && !s.TextInputHasFocus(id)) col = 0xffe5e5e5;
    s.graphics->AddRectFilled(r.Min(), r.Max(), col);

    if (result->HasSelection()) {
        TextInputResult::SelectionIterator it {*s.graphics->context};
        while (auto const sel_r = result->NextSelectionRect(it))
            s.graphics->AddRectFilled(*sel_r, 0xff0000ff);
    }

    if (result->show_cursor) {
        auto cursor_r = result->GetCursorRect();
        s.graphics->AddRectFilled(cursor_r.Min(), cursor_r.Max(), 0xff000000);
    }

    s.graphics->AddText(result->GetTextPos(), 0xff000000, text);
}

// These are functions instead of just constants so we can declare initial values with names
// in c++ {} syntax you have can only initialise in order

PUBLIC WindowSettings DefMainWindow() {
    WindowSettings s = {};
    s.flags = WindowFlags_NoPadding | WindowFlags_NoScrollbarX | WindowFlags_NoScrollbarY;
    s.pad_top_left = {4, 4};
    s.pad_bottom_right = {4, 4};
    s.draw_routine_window_background = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), 0xff151515);
    };
    return s;
}

PUBLIC WindowSettings DefWindow() {
    WindowSettings s = {};
    s.flags = WindowFlags_NoScrollbarX | WindowFlags_NoScrollbarY;
    s.pad_top_left = {4, 4};
    s.pad_bottom_right = {4, 4};
    s.scrollbar_padding = 4;
    s.scrollbar_width = 8;
    s.draw_routine_scrollbar = DefaultDrawWindowScrollbar;
    s.draw_routine_window_background = DefaultDrawWindowBackground;
    s.draw_routine_popup_background = DefaultDrawPopupBackground;
    return s;
}

PUBLIC WindowSettings DefPopup() {
    WindowSettings s = {};
    s.flags = WindowFlags_AutoWidth | WindowFlags_AutoHeight | WindowFlags_AutoPosition;
    s.pad_top_left = {4, 4};
    s.pad_bottom_right = {4, 4};
    s.scrollbar_padding = 4;
    s.scrollbar_padding_top = 0;
    s.scrollbar_width = 8;
    s.draw_routine_scrollbar = DefaultDrawWindowScrollbar;
    s.draw_routine_window_background = DefaultDrawWindowBackground;
    s.draw_routine_popup_background = DefaultDrawPopupBackground;
    return s;
}

PUBLIC ButtonSettings DefButton() {
    ButtonSettings s = {};
    s.flags = {.left_mouse = true, .triggers_on_mouse_up = true};
    s.draw = DefaultDrawButton;
    return s;
}

PUBLIC ButtonSettings DefToggleButton() {
    ButtonSettings s = {};
    s.flags = {.left_mouse = true, .triggers_on_mouse_up = true};
    s.draw = DefaultDrawButton;
    return s;
}

PUBLIC ButtonSettings DefButtonPopup() {
    ButtonSettings s = {};
    s.flags = {.left_mouse = true, .triggers_on_mouse_up = true};
    s.draw = DefaultDrawPopupButton;
    s.window = DefPopup();
    return s;
}

PUBLIC SliderSettings DefSlider() {
    SliderSettings s = {};
    s.flags = {.slower_with_shift = true, .default_on_modifer = true};
    s.sensitivity = 256;
    s.draw = DefaultDrawSlider;
    return s;
}

PUBLIC TextInputSettings DefTextInput() {
    TextInputSettings s;
    s.button_flags = {.left_mouse = true, .triggers_on_mouse_down = true};
    s.text_flags = {.tab_focuses_next_input = true};
    s.draw = DefaultDrawTextInput;
    s.select_all_on_first_open = true;
    return s;
}

PUBLIC TextInputDraggerSettings DefTextInputDraggerInt() {
    TextInputDraggerSettings s;
    s.slider_settings.flags = {.slower_with_shift = true, .default_on_modifer = true};
    s.slider_settings.draw = [](IMGUI_DRAW_SLIDER_ARGS) {};
    s.slider_settings.sensitivity = 256;
    s.text_input_settings.button_flags = {.left_mouse = true,
                                          .double_click = true,
                                          .triggers_on_mouse_down = true};
    s.text_input_settings.text_flags = {.chars_decimal = true, .tab_focuses_next_input = true};
    s.text_input_settings.draw = DefaultDrawTextInput;
    s.text_input_settings.select_all_on_first_open = true;
    s.format = "{}";
    return s;
}

PUBLIC TextInputDraggerSettings DefTextInputDraggerFloat() {
    TextInputDraggerSettings s = DefTextInputDraggerInt();
    s.format = "{.1}";
    return s;
}

PUBLIC TextSettings DefText() {
    TextSettings s;
    s.col = 0xffffffff;
    s.draw = [](IMGUI_DRAW_TEXT_ARGS) {
        auto font_size = imgui.graphics->context->CurrentFontSize();
        f32x2 pos;
        pos.x = (f32)(int)r.x;
        pos.y = r.y + ((r.h / 2) - (font_size / 2));
        pos.y = (f32)(int)pos.y;
        imgui.graphics->AddText(imgui.graphics->context->CurrentFont(), font_size, pos, col, str, 0);
    };
    return s;
}

} // namespace imgui

namespace live_edit {

extern bool g_high_contrast_gui; // IMPROVE: this is hacky

} // namespace live_edit

// IMPROVE: separate out possibly constexpr lookup from runtime calculation for better performance
inline u32 LiveCol(imgui::Context const&, UiColMap type) {
    auto const map_index = ToInt(type);

    String const col_string = (live_edit::g_high_contrast_gui &&
                               imgui::g_live_edit_values.ui_col_map[map_index].high_contrast_colour.size)
                                  ? imgui::g_live_edit_values.ui_col_map[map_index].high_contrast_colour
                                  : imgui::g_live_edit_values.ui_col_map[map_index].colour;

    // NOTE: linear search but probably ok
    for (auto const i : Range(k_max_num_colours))
        if (String(imgui::g_live_edit_values.ui_cols[i].name) == col_string)
            return imgui::g_live_edit_values.ui_cols[i].col;

    return {};
}

inline f32 LiveSize(imgui::Context const& imgui, UiSizeId size_id) {
    f32 res = 1;
    switch (imgui::g_live_edit_values.ui_sizes_units[ToInt(size_id)]) {
        case UiSizeUnit::Vw:
            res = imgui.VwToPixels(imgui::g_live_edit_values.ui_sizes[ToInt(size_id)]);
            break;
        case UiSizeUnit::None: res = imgui::g_live_edit_values.ui_sizes[ToInt(size_id)]; break;
        case UiSizeUnit::Count: PanicIfReached();
    }
    return res;
}
