// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "gui_frame.hpp"
#include "renderer.hpp"

// An immediate-mode GUI system.
//
// The entire GUI is rebuilt every frame. There are no persistent widget objects. When you call a
// behaviour function (e.g. ButtonBehaviour), you're not creating or updating a widget - you're declaring that
// a rectangle with a given ID should have button behaviour *for this frame*. Next frame, if you don't call
// ButtonBehaviour for that ID, the button ceases to exist. The only persistent state is: which ID is hot,
// which is active, which has text input focus, and viewport data (which is mostly reconfigured each frame).
//
// Element Z-ORDER: The order you register elements within a viewport matters. Elements registered later in
// the frame logically sit "on top" of earlier elements. For example, if two overlapping rectangles both have
// button behaviour, the one registered later will receive mouse events and become hot/active, while the
// earlier one will be ignored. Internally, this is achieved by adding a frame of delay to these interactions
// (although our system actually immediately requests a re-run of the GUI in these cases).
//
// Viewport Z-ORDER: In general, viewports have the z-order rules as elements, however, we additionally offer
// some order-independent features for viewports. By setting the mode of a viewport config to Floating, Modal
// or PopupMenu, you break outside of the ordering requirement. Instead, you can use the z_order field (popup
// menus automatically handle this).
//
// IDs: Every element and viewport must have a constant, unique ID. This is how the system tracks the minimal
// persistent state (hot, active, text input focus). Use MakeId() with a string or number, or PushId()/PopId()
// to scope IDs hierarchically. Alternatively, hash anything that uniquely identifies the element: a string,
// pointer, SourceLocationHash(), or existing hash.
//
// It provides the building blocks for creating your own 'elements' (not widgets - they don't persist). This
// is a reasonably low-level API for handling UI interaction; it does not do any layouts, or any drawing.
// There should only be one Context per GUI window.
//
// The system is built around 'viewports' - containers that introduce their own coordinate system where the
// top left is the origin. Viewports are started with a 'begin' call and ended with an 'end' call. Inside
// these 2 calls, you build the UI elements by registering rectangles into the current viewport, calling
// 'behaviour' functions, and then drawing using the current draw-list.
//
// Behaviours can often be combined. For example, internally, 'draggers' just use both slider and text input
// behaviour on the same ID and rectangle.
//
// Viewports can be nested. Additionally, viewports can be configured to automatically handle scrollbars, and
// can configured as popup-menus or modals that sit on top of other viewports. While viewports have some
// persistent data (like scroll offset), they are mostly reconfigured each frame, and the viewport hierarchy
// is rebuilt every frame through Begin/EndViewport calls.
//
// There's 2 coordinate systems here: viewport-related (relative to the top left of the current viewport), and
// window-relative (relative to the top left of the whole GUI). It's mostly convenient to work using
// viewport-relative positions, and only convert them to window-relative for the final Behaviour* call and
// drawing. The internals of this system typically work with window-relative, but users of this API typically
// work with viewport-relative. IMPORTANT: any drawing is assumed to be window-relative.
//
// All sizes/positions in this system are pixels.
//
// There's still some technical debt here. This system has been ad-hoc built bit-by-bit over 10+ years.

namespace imgui {

struct Context;
struct Viewport;
using Char32 = u32;

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

using Id = u64;

constexpr Id k_null_id = 0;

struct ButtonConfig {
    bool operator==(ButtonConfig const&) const = default;

    // The mouse button that will cause the button to fire.
    MouseButton mouse_button = MouseButton::Left;

    // What type of event for this mouse button should trigger the element to fire. Typically mouse-up is used
    // for buttons - in which case the system internally checks that both the mouse down and up occurred in
    // the button rectangle - giving the user the opportunity to change their mind and release elsewhere.
    MouseButtonEvent event = MouseButtonEvent::Up;

    // Modifiers required to fire the button. Currently not supported for MouseButtonEvent::Up events.
    ModifierFlags required_modifiers {};

    // Cursor to show while hot.
    CursorType cursor_type = CursorType::Hand;

    // For a single-click event, don't fire another single-click if the click is detected to be a double-click
    // (that is, a click that happens in quick succession to the first). The default is that rapid clicks all
    // fire single-click events even if the second click might typically be considered a double-click. Turning
    // this on allows you to handle double-clicks separately.
    bool32 dont_fire_on_double_click : 1 = false;

    // If this button is inside a popup menu or modal, close it when the button fires.
    bool32 closes_popup_or_modal : 1 = false;

    // Fire the button at a regular interval if held down.
    bool32 hold_to_repeat : 1 = false;

    // Internal. Specify that this element does not live inside the typical content of a viewport, instead
    // it's inside the padding or scrollbar.
    bool32 is_non_viewport_content : 1 = false;
};

struct SliderConfig {
    static constexpr ButtonConfig const k_activation_cfg = {.mouse_button = MouseButton::Left,
                                                            .event = MouseButtonEvent::Down};

    // Number of pixels for a value change of a full turn (from min to max).
    f32 sensitivity = 256;

    // Increase the sensitivity while the shift key is held.
    bool32 slower_with_shift : 1 = true;

    // Set the slider's value to its default when its clicked while holding the modifier key.
    bool32 default_on_modifer : 1 = true;
};

struct TextInputConfig {
    f32 x_padding = 4;

    bool32 chars_decimal : 1 = false; // Allow 0123456789.+-*/
    bool32 chars_hexadecimal : 1 = false; // Allow 0123456789ABCDEFabcdef
    bool32 chars_uppercase : 1 = false; // Turn a..z into A..Z
    bool32 chars_no_blank : 1 = false; // Filter out spaces, tabs
    bool32 chars_note_names : 1 = false; // Allow 0123456789+-#abcdefgABCDEFG
                                         //
    bool32 tab_focuses_next_input : 1 = true;
    bool32 centre_align : 1 = false;
    bool32 escape_unfocuses : 1 = true;
    bool32 select_all_when_opening : 1 = true;

    // Our multi-line text input leaves a lot to be desired... but it sort of works for now.
    bool32 multiline : 1 = false;
    bool32 multiline_wordwrap_hack : 1 = false;
};

// Draw the background of the imgui.curr_viewport. Typically using the viewport's unpadded bounded.
using DrawViewportBackgroundFunction = TrivialFunctionRef<void(Context const& imgui)>;

struct ViewportScrollbar {
    Rect strip; // Long strip that the handle sits in.
    Rect handle; // The bit that you can grab.
    imgui::Id id; // ID for the handle - use with IsHot(), etc.
};

using ViewportScrollbars = Array<Optional<ViewportScrollbar>, 2>; // x, y

// Draw the scrollbars if they are given.
using DrawScrollbarsFunction =
    TrivialFunctionRef<void(Context const& imgui, ViewportScrollbars const& scrollbars)>;

enum class ViewportMode : u8 {
    // Contained viewports live inside their parent. They clip inside the drawable region of the parent.
    // (default).
    Contained,
    // Floating viewports escape their parent - and sit on top of any contained viewports on the UI. There's a
    // few additional flags for configuring a Floating behaviour in the config, including the z-order which
    // effects the ordering of Floating and Modal viewports.
    Floating,
    // Modal viewports are the same as Floating, except they have their lifecycle managed by this IMGUI
    // system. All the same config applies.
    Modal,
    // Popup menu's are similar to modals. They float, their lifecycle is managed, but additionally, they have
    // behaviours appropriate for a popup menu: sub-menus stack and have auto-open/close hover behaviour.
    // They're typically designed for context menus. Z-ordering for popup menus is handled automatically -
    // they sit on top of all other viewport types.
    PopupMenu,
};

enum class ViewportPositioning : u8 {
    ParentRelative, // rect is viewport-relative (default)
    WindowAbsolute, // rect is already in window coordinates
    AutoPosition, // rect is avoid-rect in window coords; actual position is calculated
    WindowCentred, // centre in window; rect pos is ignored; size is clamped to window; works with auto_size
};

enum class ViewportScrollbarVisibility : u8 {
    // Scrollbars are shown when the viewport's contents is larger than its bounds.
    Auto,
    // Always position and draw a scrollbar, even if the viewport doesn't currently need any scrolling.
    Always,
    // Never show scrollbars.
    Never,
};

struct ViewportConfig {
    ViewportConfig Clone(ArenaAllocator& arena) {
        auto result = *this;
        result.draw_background = result.draw_background.CloneObject(arena);
        result.draw_scrollbars = result.draw_scrollbars.CloneObject(arena);
        return result;
    }

    // Custom little wrapper to make ViewportScrollbarVisibility behave like a clang vector extension (which
    // can't be used for enum class types).
    struct ViewportScrollbarVisibilityx2 {
        // Set both to the same.
        constexpr ViewportScrollbarVisibilityx2(ViewportScrollbarVisibility v) { x = y = v; }
        // Set individually.
        constexpr ViewportScrollbarVisibilityx2(ViewportScrollbarVisibility x, ViewportScrollbarVisibility y)
            : x(x)
            , y(y) {}
        constexpr ViewportScrollbarVisibility operator[](usize i) const { return i == 0 ? x : y; }
        ViewportScrollbarVisibility x;
        ViewportScrollbarVisibility y;
    };

    f32 TotalWidthPad() { return padding.l + padding.r; }
    f32 TotalHeightPad() { return padding.t + padding.b; }
    f32x2 TotalPadSize() { return padding.lrtb.xw + padding.lrtb.yz; }

    ViewportMode mode = ViewportMode::Contained;
    ViewportPositioning positioning = ViewportPositioning::ParentRelative;

    DrawViewportBackgroundFunction draw_background {}; // Optional.
    DrawScrollbarsFunction draw_scrollbars {}; // Required unless set to use no automatic scrollbars.

    // Gap inside the viewport area for left, top, right, and bottom. This is like CSS padding. The space
    // inside the viewport becomes smaller. Set using designated initializer syntax.
    Margins padding {};

    // Gap between the viewport content, and the scrollbar. Like padding, it reduces the size of internal
    // usable space in the axis that the scrollbar would appear.
    f32 scrollbar_padding {};
    f32 scrollbar_width {4}; // Ignored if scrollbar_inside_padding.
    f32 scroll_line_size {}; // Mouse scroll step amount. 0 means use default.

    // Automatically set the size of the viewport based on what rectangles are registered into it.
    b8x2 auto_size = false;

    ViewportScrollbarVisibilityx2 scrollbar_visibility = ViewportScrollbarVisibility::Auto;

    // Rather than use the scrollbar_width field, the scrollbars size is set to the padding of the
    // viewport: right for y scrollbars, and bottom for x scrollbars. This is typically a good idea
    // because it reduces gaps. IMPROVE: make this the default mode?
    bool32 scrollbar_inside_padding : 1 = false;

    u8 z_order {}; // [Floating or Modal]. Viewports with larger values sit on top of lower values.
    bool32 exclusive_focus : 1 = false; // [Floating or Modal]. Make all other viewports uninteractable.
    bool32 ignore_exclusive_focus : 1 = false; // [Floating]. Allow interaction even when another viewport
                                               // has exclusive focus. Clicks on this viewport won't
                                               // trigger close_on_click_outside for modals.
    bool32 close_on_click_outside : 1 = false; // [Modal]. Close with clicks outside viewport.
    bool32 close_on_escape : 1 = false; // [Modal]. Close when escape key pressed.
};

// Viewport internals are not frequently used by the user of this API.
struct Viewport {
    // The active draw list for this Viewport, it might be the same as owned_draw_list or it might be another
    // Viewport's draw list in the case that it's more efficient to share a draw list. Shouldn't be null. Use
    // this to do all your drawing for this viewport.
    DrawList* draw_list = nullptr;

    // The draw list that is actually allocated and owned by this viewport - might be null.
    DrawList* owned_draw_list = nullptr;

    // The viewport's rectangle minus padding, this is probably the one you want to use for positioning/sizing
    // elements inside the viewport. Window-coordinates.
    Rect bounds = {};

    // The whole viewport excluding the padding, probably use this for drawing the viewport background.
    // Window-coordinates.
    Rect unpadded_bounds = {};

    // The region of the viewport that is visible on the screen. Window-coordinates.
    Rect visible_bounds = {};

    // The area that can be drawn in. Window-coordinates.
    Rect clipping_rect = {};

    Id id = k_null_id;

    // The root of this viewport tree - never null. It will point to itself if it's the root.
    Viewport* root_viewport = nullptr;

    // The viewport that this viewport lives inside.
    Viewport* parent_viewport = nullptr;

    Id creator_of_this_popup_menu = k_null_id;

    u16 nested_level = k_null_id;
    u16 child_nesting_counter = k_null_id;

    ViewportConfig cfg = {};

    DynamicArrayBounded<char, 128> debug_name;
    bool active = false;

    b8x2 contents_was_auto = false;

    // The size of the stuff put into the viewport from last frame.
    f32x2 prev_content_size = {};
    f32x2 prevprev_content_size = {};

    f32x2 scroll_offset = {}; // The pixel offset from scrollbars.
    f32x2 scroll_max = {};
    b8x2 has_scrollbar = false;
};

// Data about interactions with a text input, and data required to draw a text input.
// Example processor to draw a text input:
// - Draw a background using the rectangle you passed into the text input behaviour function.
// - Check if HasSelection, if so, create an iterator and loop NextSelectionRect - drawing a blue highlight
//   box for each.
// - Check cursor_rect - fill black if present.
// - Draw the text at position text_pos.
struct TextInputResult {
    bool HasSelection() const { return selection_start != selection_end; }

    // Create an iterator, filling in the required reference fields. And then repeatadly call
    // NextSelectionRect to get all parts of the selection - this can happen when it's a multi-line text
    // input.
    struct SelectionIterator {
        Context const& imgui;
        char const* pos {};
        u32 remaining_chars {};
        u32 line_index {};
        bool reached_end {};
    };
    Optional<Rect> NextSelectionRect(SelectionIterator& it) const;

    // The current text in the text input. Temporary memory. Changes when another text input is run. Copy this
    // into your own buffer if buffer_changed is true (or other criteria that you want).
    String text {};

    // Information about if the text has changed.
    bool enter_pressed {};
    bool buffer_changed {};

    // The position to draw_list->AddText().
    f32x2 text_pos = {};

    // The rectangle for the cursor if it should currently be shown.
    Optional<Rect> cursor_rect = {};

    // Internals.
    int cursor = 0;
    int selection_start = 0;
    int selection_end = 0;
    bool is_placeholder = false;
};

// Returns the position to draw_list->AddText() at. This function is also used internally by the text input
// and returned automatically in TextInputResult::text_pos. Use this if you want to render text in the same
// position as the text input.
f32x2 TextInputTextPos(String text, Rect r, TextInputConfig cfg, Fonts const& fonts);

// Tries to find a appropriate position for a popup, given the constraints.
enum class PopupJustification { AboveOrBelow, LeftOrRight };
f32x2 BestPopupPos(Rect base_r, Rect avoid_r, f32x2 viewport_size, PopupJustification justification);

struct Context {
    //
    // Public fields
    //

    // Shortcut to the current viewport.
    Viewport* curr_viewport = nullptr;

    // Shortcut to the current viewport's draw list.
    DrawList* draw_list = nullptr;

    // A draw list that is layered on top of all viewports. Use this for occasion sit-on-top graphics.
    DrawList* overlay_draw_list = {};

    //
    // Viewport coordinates
    //

    // Every element should use this before doing *Behaviour calls or drawing. It does 2 things:
    // - Converts coordinates from viewport-relative to window-relative.
    // - Registers the rectangle with the current viewport (so the viewport knows scrollbars, clipping, etc).
    [[nodiscard]] Rect RegisterAndConvertRect(Rect r);

    // Convert to/from window (OS level) to viewport (IMGUI container) coordinates.
    f32x2 ViewportPosToWindowPos(f32x2 viewport_pos) const;
    f32x2 WindowPosToViewportPos(f32x2 window_pos) const;
    Rect ViewportRectToWindowRect(Rect viewport) const {
        return {.pos = ViewportPosToWindowPos(viewport.pos), .size = viewport.size};
    }
    Rect WindowRectToViewportRect(Rect window) const {
        return {.pos = WindowPosToViewportPos(window.pos), .size = window.size};
    }

    //
    // IDs
    //

    // Hashes an input and returns an ID. This ID is seeded on the current ID stack, so the hash will be
    // different depending on what is pushed before with PushId()/PopId()
    Id MakeId(String str) const;
    Id MakeId(uintptr num) const;

    void PushId(String str);
    void PushId(uintptr num);
    void PopId();

    //
    // Interaction checks
    //

    // Once an element has been given a behaviour (button, slider, etc.), you can check its user-interaction
    // state. Text inputs have an additional concept of 'focus', outlined further down.

    // 'active' means the user holding the element with a mouse-click.
    bool IsActive(Id id, Optional<MouseButton> via_mouse_button = {}) const;
    bool WasJustActivated(Id id, Optional<MouseButton> via_mouse_button = {}) const;
    bool WasJustDeactivated(Id id, Optional<MouseButton> via_mouse_button = {}) const;
    bool AnItemIsActive() const;
    Id GetActive() const { return active_item.id; }

    // 'hot' means that the user is hovering over an element and it would become active if they clicked.
    bool IsHot(Id id) const;
    bool WasJustMadeHot(Id id) const;
    bool WasJustMadeUnhot(Id id) const;
    bool AnItemIsHot() const;
    Id GetHot() const { return hot_item; }
    f64 SecondsSpentHot() const {
        return time_when_turned_hot ? GuiIo().in.current_time - time_when_turned_hot : 0;
    }

    bool IsHotOrActive(Id id, Optional<MouseButton> via_mouse_button = {}) const {
        return IsHot(id) || IsActive(id, via_mouse_button);
    }

    // Having keyboard focus means that this element is allowed to consume text events. This is different to
    // text input focus. Returns true if the ID is the keyboard focus item.
    bool RequestKeyboardFocus(Id id);
    bool IsKeyboardFocus(Id id) const { return keyboard_focus_item == id; }
    Id KeyboardFocus() const { return keyboard_focus_item; }

    // 'Is the cursor over the given ID' is often the same as IsHot(). But unlike the hot item,
    // there can be both an active item and a hovered item at the same time, you most likely
    // want to check IsHot for when you are drawing.
    bool IsHovered(Id id) const;
    bool WasJustHovered(Id id) const;
    bool WasJustUnhovered(Id id) const;
    Id GetHovered() const { return hovered_item; }

    //
    // Button behaviour
    //

    // Returns true when clicked, the conditions that determine 'clicked' are set in the config. The given
    // rectangle is window-relative, not viewport-relative. You typically will need to register-and-convert
    // the rectangle before calling this. You can call this multiple times for the same ID with different
    // configurations to specify different interactions. You can call this on the same ID as previously used
    // for another type of behaviour (such as slider behaviour).
    bool ButtonBehaviour(Rect rect_in_window_coords, Id id, ButtonConfig cfg);

    // Same as ButtonBehaviour but also swaps a boolean for you.
    bool ToggleButtonBehaviour(Rect rect_in_window_coords, Id id, ButtonConfig cfg, bool& state) {
        auto const clicked = ButtonBehaviour(rect_in_window_coords, id, cfg);
        if (clicked) state = !state;
        return clicked;
    }

    struct PopupMenuButtonBehaviourResult {
        bool clicked;
        // True if the button's corresponding popup is open, and the cursor is not on this viewport - in this
        // situation it makes sense to display this button in an 'active' colour showing to the user the
        // association between what button resulted in the popup opening.
        bool show_as_active;
    };
    // If clicked, opens a popup menu which appears in an appropriate place relative to the rectangle passed
    // here. Remember, opening a popup does not mean the popup is actually run: you must check IsPopupOpen and
    // call BeginViewport on the popup_id after calling this.
    PopupMenuButtonBehaviourResult
    PopupMenuButtonBehaviour(Rect rect_in_window_coords, Id button_id, Id popup_id, ButtonConfig cfg);

    //
    // Slider/knob behaviour
    //

    // Adds DAW-like drag behaviour to the rectangle, modifying the given value based on vertical/horizontal
    // drags. This variant works with fractions (values from 0 to 1). Returns true when the slider value
    // changes. Uses the left-mouse button.
    struct SliderBehaviourFractionArgs {
        Rect rect_in_window_coords {};
        Id id {};
        f32& fraction;
        f32 default_fraction {};
        SliderConfig cfg = {};
    };
    bool SliderBehaviourFraction(SliderBehaviourFractionArgs const& args);

    // Same as the fraction variant, but can work on any given range.
    struct SliderBehaviourRangeArgs {
        Rect rect_in_window_coords {};
        Id id {};
        f32 min {};
        f32 max {};
        f32& value;
        f32 default_value {};
        SliderConfig cfg = {};
    };
    bool SliderBehaviourRange(SliderBehaviourRangeArgs const& args);

    //
    // Text input behaviour
    //

    // A text input is an element that you can type text into. Only one text input can be focused at a time.
    // You receive a result struct with lots of data about the text, selection, interactions. Multi-line text
    // input is sort of supported but it's incredibly hacky - it needs to be redone.
    struct TextInputBehaviourArgs {
        Rect rect_in_window_coords {};
        Id id {};

        // Optional. The text that will be displayed when it's unfocused, and the will become the default text
        // when it first gains focus. It doesn't have to be static memory. When the input is focused, the data
        // is copied into the IMGUI system's internal string buffer.
        String text {};

        // Optional. The text that will be displayed when it's unfocused. It is not in the actual buffer.
        String placeholder_text {};

        TextInputConfig input_cfg = {};

        // The 'click' criteria for focusing the text input. Typically a single or double left-click.
        ButtonConfig button_cfg = {};
    };
    TextInputResult TextInputBehaviour(TextInputBehaviourArgs const& args);

    bool TextInputHasFocus(Id id) const;
    bool TextInputJustFocused(Id id) const;
    bool TextInputJustUnfocused(Id id) const;
    Id GetTextInput() const { return active_text_input; }
    void SetImguiTextEditState(String new_text, bool multiline);
    void SetTextInputFocus(Id id, String new_text, bool multiline); // Pass 0 to unfocus.
    void TextInputSelectAll();
    void ResetTextInputCursorAnim();

    //
    // Dragger behaviour
    //

    // A dragger is simply a combination of a text input and a slider. When the text input is not focused, it
    // behaves like a slider. Typically double-clicks are used to focus the text input. Read more about
    // SliderBehaviour and TextInputBehaviour.
    struct DraggerBehaviourArgs {
        Rect rect_in_window_coords {};
        Id id {};
        String text {};
        f32 min {};
        f32 max {};
        f32& value;
        f32 default_value {};

        // The 'click' requirements to focus the text input.
        ButtonConfig text_input_button_cfg = {
            .mouse_button = MouseButton::Left,
            .event = MouseButtonEvent::DoubleClick,
        };
        TextInputConfig text_input_cfg = {
            .chars_decimal = true,
            .tab_focuses_next_input = true,
            .escape_unfocuses = true,
            .select_all_when_opening = true,
        };
        SliderConfig slider_cfg = {};
    };
    struct DraggerResult {
        bool value_changed {}; // Valued dragged to new value.
        Optional<String> new_string_value {}; // New text confirmed. Parse this and set your own value.

        // When the text input is active for this element, use this to draw it. Otherwise, if you want to
        // render text that is in the same place as what the text input would, format your value to a string
        // (after consuming new values if needed), and then use TextInputTextPos to know where to place it.
        Optional<TextInputResult> text_input_result {};
    };
    DraggerResult DraggerBehaviour(DraggerBehaviourArgs const& args);

    //
    // Tooltip behaviour
    //

    // Returns true if you should draw a tooltip for the given ID. Probably use overlay draw-list for drawing
    // tooltips.
    bool TooltipBehaviour(Rect rect_in_window_coords, imgui::Id id);

    //
    // Viewports
    //

    // Any Begin* call must be paired with an EndViewport. Use DEFER { imgui.EndViewport(); };. There's 2 core
    // overloads: either pass a unique name that will be converted to an ID, or make an ID first.
    //
    // IMPORTANT: what the rectangle argument means depends on the config's positioning. See
    // ViewportPositioning. For auto_width/auto_height viewports, the corresponding size dimension is ignored.
    // For WindowCentred, the position is ignored (it's computed automatically).
    void BeginViewport(ViewportConfig const& config, Rect r, String unique_name);
    void BeginViewport(ViewportConfig const& config, Id id, Rect r, String debug_name = {});

    void EndViewport();

    // Mostly internal overload.
    void BeginViewport(ViewportConfig const& config,
                       Viewport* viewport,
                       Rect r_in_viewport_coords,
                       String debug_name = {});

    bool WasViewportJustCreated(Id id) const;
    bool WasViewportJustHovered(Id id) const;
    bool WasViewportJustUnhovered(Id id) const;
    bool IsViewportHovered(Id id) const;

    bool IsViewportHovered(Viewport* viewport) const;

    Viewport* FindViewport(Id id) const;

    static void SetYScroll(Viewport* viewport, f32 val) {
        viewport->scroll_offset.y = Round(val);
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
    static void SetXScroll(Viewport* viewport, f32 val) {
        viewport->scroll_offset.x = Round(val);
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
    bool ScrollViewportToShowRectangle(Rect r);

    // Handy shortcuts.
    f32 CurrentVpWidth() const { return curr_viewport->bounds.w; }
    f32 CurrentVpHeight() const { return curr_viewport->bounds.h; }
    f32x2 CurrentVpSize() const { return curr_viewport->bounds.size; }
    f32 CurrentVpWidthWw() const { return PixelsToWw(curr_viewport->bounds.w); }
    f32 CurrentVpHeightWw() const { return PixelsToWw(curr_viewport->bounds.h); }
    f32x2 CurrentVpSizeWw() const { return PixelsToWw(curr_viewport->bounds.size); }

    //
    // Popup menus (a special type of viewport)
    //

    // Opens a popup ready to be 'Begin'ed into.
    void OpenPopupMenu(Id id, Id creator_of_this_popup = k_null_id);

    bool IsPopupMenuOpen(Id id);
    bool IsAnyPopupMenuOpen();

    void ClosePopupToLevel(usize level);
    void CloseAllPopups();
    void CloseTopPopupOnly();
    bool DidPopupMenuJustOpen(Id id);

    //
    // Modal viewports
    //
    // Modals are floating viewports that be opened and closed, but unlike popups they have no
    // child-menu stacking, auto-hover sub-menu behaviour, or creator tracking. Multiple modals
    // can be open simultaneously (e.g. an "are you sure?" dialog on top of another modal).

    void OpenModalViewport(Id id);
    bool IsModalOpen(Id id);
    bool IsAnyModalOpen();
    void CloseModal(Id id);
    void CloseTopModal();
    void CloseAllModals();

    // Inside a viewport begin/end that has auto_width and/or auto_height, use this to configure a minimum
    // size. Alternatively, call RegisterAndConvertRect.
    void SetViewportMinimumAutoSize(f32x2 size);

    //
    // Rects
    //

    // Is the rectangle visible at all on the GUI - useful for efficiency purposes.
    bool IsRectVisible(Rect r) const;
    Rect GetCurrentClipRect() const { return current_scissor_rect; }

    void PushRectToCurrentScissorStack(Rect const& r);
    void PopRectFromCurrentScissorStack();

    void PushScissorStack();
    void PopScissorStack();

    //
    //
    //

    Context(ArenaAllocator& scratch_arena);
    void BeginFrame(ViewportConfig config, Fonts& fonts); // Call at the start of the frame
    void EndFrame(); // Call at the end of the frame

    //
    // Misc
    //

    // Registering a rectangle means the GUI updates when the cursor enters/leaves it. Returns true if the
    // widget is visible.
    bool RegisterRectForMouseTracking(Rect r_in_window_coords, bool check_intersection = true);

    // Set a rectangle to the 'hot' state if possible.
    // is_not_viewport_content means something that is not inside a viewport, but part of it e.g. scrollbar.
    void SetHot(Rect r_in_window_coords, Id id, bool32 is_not_viewport_content = false);

    // Set an ID as the active. Most likely the element would have previously been made hot.
    void SetActive(Id id, MouseButton mouse_button);

    // Clear the current active ID.
    void ClearActive();

    //
    //
    //

    // Internal.
    void UpdateExclusiveFocusViewport();
    Viewport* FindOrCreateViewport(Id id);
    void OnScissorChanged() const;

    struct ActiveItem {
        Id id = k_null_id;
        bool just_activated = false;
        Viewport* viewport = nullptr;
        MouseButton mouse_button {};
    };

    bool debug_show_register_widget_overlay = false;

    ArenaAllocator& scratch_arena;

    stb::STB_TexteditState stb_state = {};
    Id active_text_input = k_null_id;
    Id prev_active_text_input = k_null_id;
    bool text_cursor_is_shown = true;
    bool tab_to_focus_next_input = false;
    bool tab_just_used_to_focus = false;
    DynamicArray<Char32> textedit_text {Malloc::Instance()};
    DynamicArray<char> textedit_text_utf8 {Malloc::Instance()};
    int textedit_len = 0;
    bool active_text_input_shown = false; // Unfocus active input if it's not shown in the frame

    DynamicArray<Viewport*> sorted_viewports {Malloc::Instance()};

    ArenaAllocator viewport_arena {Malloc::Instance(), 0, sizeof(Viewport) * 12};

    // Storage of viewports, grows whenever a different BeginViewport is called for the first time
    // this array actually owns each viewport pointer.
    DynamicArray<Viewport*> viewports {Malloc::Instance()};

    // Grows to show the layering of the viewports. Should always start and end frames empty if you are using
    // correct matching begin/end calls.
    DynamicArray<Viewport*> viewport_stack {Malloc::Instance()};

    // At the beginning of the frame, find which layer the mouse is over using the rects from last frame.
    Viewport* hovered_viewport = nullptr;
    Viewport* hovered_viewport_last_frame = nullptr;
    // Used to differentiate between when the mouse is over the padding of the viewport.
    Viewport* hovered_viewport_content = nullptr;
    Viewport* exclusive_focus_viewport = nullptr;
    Viewport* viewport_just_created = nullptr;
    Viewport* floating_exclusive_viewport = nullptr;
    Viewport* floating_exclusive_viewport_last_frame = nullptr;

    DynamicArray<Viewport*> open_popups {Malloc::Instance()};
    DynamicArray<Viewport*> current_popup_stack {Malloc::Instance()};
    Id popup_menu_just_opened = k_null_id;

    DynamicArray<Viewport*> open_modals {Malloc::Instance()};
    Id modal_just_opened = k_null_id;

    DynamicArray<DynamicArray<Rect>> scissor_stacks {Malloc::Instance()};
    Rect current_scissor_rect = {};
    bool scissor_rect_is_active = false;

    TimePoint button_repeat_counter = {};
    TimePoint cursor_blink_counter {};

    // IDs
    //
    // We use temp variables to add a frame of lag so that you can layer widgets on top of each other and the
    // behaviour is as expected. If we don't do this, if you put a button on top of another button, they will
    // both highlight on hovering.
    ActiveItem active_item_last_frame = {};
    Id hot_item_last_frame = k_null_id;
    Id hovered_item_last_frame = k_null_id;

    Id hot_item = k_null_id;
    Id temp_hot_item = k_null_id;
    CursorType temp_hot_item_cursor {};
    TimePoint time_when_turned_hot = {};

    Id hovered_item = k_null_id;
    Id temp_hovered_item = k_null_id;

    ActiveItem active_item = {};
    ActiveItem temp_active_item = {};

    Id keyboard_focus_item = k_null_id;
    Id temp_keyboard_focus_item = {};
    bool temp_keyboard_focus_item_is_popup = false;

    DynamicArray<Id> id_stack {Malloc::Instance()};
};

} // namespace imgui
