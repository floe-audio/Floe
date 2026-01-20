// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/misc.hpp"

#include "graphics.hpp"

static constexpr u8 k_gui_refresh_rate_hz = 60;

enum class KeyCode : u32 {
    Tab,
    LeftArrow,
    RightArrow,
    UpArrow,
    DownArrow,
    PageUp,
    PageDown,
    Home,
    End,
    Delete,
    Backspace,
    Enter,
    Escape,
    A,
    C,
    F,
    V,
    X,
    Y,
    Z,
    F1,
    F2,
    F3,
    ShiftL,
    ShiftR,
    Count,
};

constexpr auto k_navigation_keys = Array {
    KeyCode::Tab,
    KeyCode::LeftArrow,
    KeyCode::RightArrow,
    KeyCode::UpArrow,
    KeyCode::DownArrow,
    KeyCode::PageUp,
    KeyCode::PageDown,
    KeyCode::Home,
    KeyCode::End,
    KeyCode::Enter,
};

enum class ModifierKey : u32 {
    Shift,
    Ctrl,
    Alt, // 'Option' on macOS
    Super, // 'Cmd' on macOS, else Super/Windows-key
    Count,

    // alias
    Modifier = IS_MACOS ? Super : Ctrl,
};

#if IS_MACOS
#define MODIFIER_KEY_NAME "Cmd"
#else
#define MODIFIER_KEY_NAME "Ctrl"
#endif

struct ModifierFlags {
    bool operator==(ModifierFlags const& other) const = default;
    bool Get(ModifierKey k) const { return flags & (1 << ToInt(k)); }
    void Set(ModifierKey k) { flags |= (1 << ToInt(k)); }
    bool IsOnly(ModifierKey k) const { return flags == (1 << ToInt(k)); }
    bool IsNone() const { return flags == 0; }
    u8 flags {};
};

enum class MouseButton : u32 { Left, Right, Middle, Count };

// The framework gives the application this struct every frame.
struct GuiFrameInput {
    struct MouseButtonState {
        struct Event {
            f32x2 point {};
            TimePoint time {};
            ModifierFlags modifiers {};

            // For press, true if this is a double-click event.
            // For release, true if the corresponding press was a double-click.
            bool is_double_click {};
        };

        ArenaStack<Event> presses {}; // mouse-down events since last frame, cleared every frame
        ArenaStack<Event> releases {}; // mouse-up events since last frame, cleared every frame
        Event last_press {};
        Optional<Event> is_down {}; // current state
        bool is_dragging {};
        bool dragging_started {}; // cleared every frame
        bool dragging_ended {}; // cleared every frame
    };

    struct KeyState {
        struct Event {
            ModifierFlags modifiers;
        };

        bool is_down;
        ArenaStack<Event> presses_or_repeats; // key-down or repeats since last frame, cleared every frame
        ArenaStack<Event> presses; // key-down events since last frame, cleared every frame
        ArenaStack<Event> releases; // key-up events since last frame, cleared every frame
    };

    auto const& Mouse(MouseButton n) const { return mouse_buttons[ToInt(n)]; }
    auto const& Key(KeyCode n) const { return keys[ToInt(n)]; }

    void Reset() {
        cursor_pos = {};
        cursor_pos_prev = {};
        cursor_delta = {};
        mouse_scroll_delta_in_lines = {};
        mouse_buttons = {};
        modifiers = {};
        keys = {};
        dyn::Clear(clipboard_text);
        dyn::Clear(input_utf32_chars);
    }

    graphics::Renderer* renderer {};

    f32x2 cursor_pos {};
    f32x2 cursor_pos_prev {};
    f32x2 cursor_delta {};
    f32 mouse_scroll_delta_in_lines {};
    Array<MouseButtonState, ToInt(MouseButton::Count)> mouse_buttons {};
    Array<KeyState, ToInt(KeyCode::Count)> keys {};
    ModifierFlags modifiers {};
    // may contain text from the OS clipboard if you requested it
    DynamicArray<char> clipboard_text {PageAllocator::Instance()};
    DynamicArrayBounded<u32, 16> input_utf32_chars {};

    // A list of filepaths that the user selected in the (now closed) file picker dialog. Cleared every frame.
    // If needed, you will need to have stored what these relate to - what GuiFrameResult::file_picker_dialog
    // was set to.
    ArenaStack<String> file_picker_results {};

    TimePoint current_time {};
    TimePoint time_prev {};
    f32 delta_time {};
    u64 update_count {};
    UiSize window_size {};
    void* native_window {}; // HWND, NSView*, etc.
    void* pugl_view {}; // PuglView* for the current frame

    // internal
    ArenaAllocator event_arena {Malloc::Instance(), 256};
};

struct MouseTrackedRect {
    Rect rect;
    bool mouse_over;
};

enum class CursorType : u8 {
    Default,
    Hand,
    IBeam,
    AllArrows,
    HorizontalArrows,
    VerticalArrows,
    UpLeftDownRight,
    Count,
};

struct FilePickerDialogOptions {
    enum class Type { SaveFile, OpenFile, SelectFolder };

    struct FileFilter {
        FileFilter Clone(Allocator& a, CloneType t = CloneType::Deep) const {
            return {
                .description = description.Clone(a, t),
                .wildcard_filter = wildcard_filter.Clone(a, t),
            };
        }
        String description;
        String wildcard_filter;
    };

    FilePickerDialogOptions Clone(Allocator& a, CloneType t = CloneType::Deep) const {
        return {
            .type = type,
            .title = title.Clone(a, t),
            .default_folder = default_folder.Clone(a, t),
            .default_filename = default_filename.Clone(a, t),
            .filters = a.Clone(filters, t),
            .allow_multiple_selection = allow_multiple_selection,
        };
    }

    Type type {Type::OpenFile};
    String title {"Select File"};
    Optional<String> default_folder {};
    Optional<String> default_filename {};
    Span<FileFilter const> filters {};
    bool allow_multiple_selection {};
};

// Fill this struct every frame to instruct the framework about the application's needs.
struct GuiFrameOutput {
    enum class UpdateInterval {
        // 1. GUI will sleep until there's user interaction, a timed wakeup fired or the global 'request
        // update' bool is set.
        Sleep,

        // 2. GUI will update at the timer (normally 60Hz).
        Animate,

        // 3. re-update the GUI instantly - as soon as the frame is done - use this sparingly for necessary
        // layout changes.
        ImmediatelyUpdate,
    };

    // Only elevates, never decreases importance.
    void IncreaseUpdateInterval(UpdateInterval r) {
        if (ToInt(r) > ToInt(wants.update_interval)) wants.update_interval = r;
    }

    void AddTimedWakeup(TimePoint time, char const* timer_name) {
        (void)timer_name;
        dyn::AppendIfNotAlreadyThere(timed_wakeups, time);
    }

    // Set this if you want to be woken up at certain times in the future. Out-of-date wakeups will be removed
    // for you.
    DynamicArray<TimePoint> timed_wakeups {Malloc::Instance()};

    // Rectangles that will wake up the GUI when the mouse enters/leaves it.
    DynamicArray<MouseTrackedRect> mouse_tracked_rects {Malloc::Instance()};

    // Set this to the text that you want put into the OS clipboard.
    DynamicArray<char> set_clipboard_text {Malloc::Instance()};

    // Set this to request a file picker dialog be opened. It's rejected if a dialog is already open. You will
    // receive the results in GuiFrameInput::file_picker_results - check that variable every frame. Allocate
    // strings using the arena or Clone method.
    Optional<FilePickerDialogOptions> file_picker_dialog {};
    ArenaAllocator file_picker_options_arena {Malloc::Instance()};

    // Add draw lists to render.
    DynamicArray<graphics::DrawList*> draw_lists {Malloc::Instance()};
    graphics::DrawListAllocator draw_list_allocator {};

    // Simple impermanent state.
    struct Wants {
        UpdateInterval update_interval {UpdateInterval::Sleep};

        bool text_input = false;
        Bitset<ToInt(KeyCode::Count)> keyboard_keys {};
        bool mouse_capture = false;
        bool mouse_scroll = false;
        bool all_left_clicks = false;
        bool all_right_clicks = false;
        bool all_middle_clicks = false;

        // Set this to the cursor that you want
        CursorType cursor_type = CursorType::Default;

        // Set this if you want text from the OS clipboard, it will be given to you in an upcoming frame
        bool clipboard_text_paste = false;
    } wants {};
};

struct GuiFrameIo {
    // Returns true when it ticks
    bool WakeupAtTimedInterval(TimePoint& counter, f64 interval_seconds) {
        bool triggered = false;
        if (in.current_time >= counter) {
            counter = in.current_time + interval_seconds;
            triggered = true;
        }
        out.AddTimedWakeup(counter, __FUNCTION__);
        return triggered;
    }

    GuiFrameInput const& in;
    GuiFrameOutput& out;
};

// Global data for exclusive use within a window's 'GUI update' (main thread) function call. Outside of this
// it is invalid to use this data. A large percentage of GUI code needs access to the frame input and output.
// Rather than pass it around everywhere which will be incredibly noisy, we use this global.
GuiFrameIo GuiIo();

// Set this at any time from any thread to request a GUI update at some point in the future.
extern Atomic<bool> g_request_gui_update;

// Internal.
void SetGuiIo(GuiFrameInput* in, GuiFrameOutput* out);
