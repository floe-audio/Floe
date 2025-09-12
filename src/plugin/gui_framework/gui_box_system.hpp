// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "fonts.hpp"
#include "gui/gui_drawing_helpers.hpp"
#include "gui_imgui.hpp"
#include "layout.hpp"
#include "style.hpp"

// GUI Box System (working prototype)
//
//
// This is a new GUI system that we intend to use universally. For now only a couple of parts use it.
//
// This API is a mostly a wrapper on top of the existing GUI systems. When we do the GUI overhaul the
// underlying systems will improve makes some aspects of this API better.
//
// It's an IMGUI system. No state is shared across frames, but within each frame we create a tree of boxes and
// perform flexbox-like layout on them. This 2-pass approach (1. layout, 2. handle input + render) is
// transparent to the user of this API. They just define layout, input-handling and rendering all in the same
// place.
//
// An overview of the system:
// - Panels correspond to the Windows in our current imgui system, accessing some functionality from them:
//   auto-sizing, 'popup' functionality and scrollbars. In the future we might not need panels to be separate
//   things but for now they are. They contain a set of boxes and optionally subpanels. Each panel has a
//   'panel function'. This is where everything happens. In a panel function you can add other panels - these
//   will be run after the current panel.
// - Boxes are the basic building block of the system. Boxes are configured using a bit BoxConfig struct.
//   Designated initialisers are great and this whole system relies on them.
//
// IMPORTANT: you must have the same boxes in the same order within every frame. For example if you are
// getting data from an external function that may produce different results based on when it's called, and
// building boxes based on it, cache the data and use that.
//
// The flexbox-like layout system is in layout.hpp.
//

struct GuiBoxSystem;

using PanelFunction = TrivialFixedSizeFunction<24, void(GuiBoxSystem&)>;

enum class PanelType {
    Subpanel,
    Modal,
    Popup,
};

struct Subpanel {
    layout::Id id;
    Optional<Rect> rect; // Instead of id. Relative to the parent panel.
    imgui::Id imgui_id;
    imgui::WindowFlags flags;
    String debug_name;
};

struct ModalPanel {
    Rect r;
    imgui::Id imgui_id;
    TrivialFixedSizeFunction<8, void()> on_close;
    bool close_on_click_outside;
    bool darken_background;
    bool disable_other_interaction;
    bool auto_width;
    bool auto_height;
    bool auto_position; // If true, r will be the rect to avoid.
    bool transparent_panel;
};

struct PopupPanel {
    String debug_name;
    layout::Id creator_layout_id;
    Optional<Rect> creator_absolute_rect; // instead of creator_layout_id
    imgui::Id popup_imgui_id;
    u32 additional_imgui_window_flags {};
};

using PanelUnion = TaggedUnion<PanelType,
                               TypeAndTag<Subpanel, PanelType::Subpanel>,
                               TypeAndTag<ModalPanel, PanelType::Modal>,
                               TypeAndTag<PopupPanel, PanelType::Popup>>;

struct Panel {
    PanelFunction run;
    PanelUnion data;

    // internal, filled by the layout system
    Optional<Rect> rect {};
    Panel* next {};
    Panel* first_child {};
};

struct Box {
    layout::Id layout_id;
    imgui::Id imgui_id;
    bool32 is_hot : 1 = false;
    bool32 is_active : 1 = false;
    bool32 button_fired : 1 = false;
    imgui::TextInputResult const* text_input_result {};
    SourceLocation source_location;
    f32 knob_percent = k_nan<f32>; // NaN if not used.
};

// Ephemeral
struct BoxSystemCurrentPanelState {
    enum class Pass {
        LayoutBoxes,
        HandleInputAndRender,
    };

    struct WordWrappedText {
        layout::Id id;
        String text;
        graphics::Font* font;
        f32 font_size;
    };

    Panel* current_panel {};
    u32 box_counter {};

    Pass pass {Pass::LayoutBoxes};
    DynamicArray<Box> boxes;
    HashTable<layout::Id, WordWrappedText> word_wrapped_texts;
    bool mouse_down_on_modal_background = false;
    imgui::TextInputResult last_text_input_result {};

    // TODO: this is a hack. The issue is this: in our 2-pass system, if we change state partway through the
    // second pass that causes a different GUI to be rendered, it crashes because it will be using
    // layout/box data from the first pass, but the GUI has changed. This is a hack to prevent that. We should
    // fix this by perhaps turning the boxes field into a hashmap and requiring each box to have a unique ID.
    // This way, we lookup the box by ID and can know when something is missing and skip it.
    DynamicArray<TrivialFixedSizeFunction<48, void()>> deferred_actions;
};

struct GuiBoxSystem {
    ArenaAllocator& arena;
    imgui::Context& imgui;
    Fonts& fonts;
    layout::Context layout;
    bool show_tooltips;

    BoxSystemCurrentPanelState* state; // Ephemeral
};

void AddPanel(GuiBoxSystem& box_system, Panel panel);

void BeginFrame(GuiBoxSystem& builder, bool show_tooltips);

void RunPanel(GuiBoxSystem& builder, Panel initial_panel);

enum class ActivationClickEvent : u32 { Up, Down, Count };

enum class TextAlignX : u32 { Left, Centre, Right, Count };
enum class TextAlignY : u32 { Top, Centre, Bottom, Count };

constexpr f32 k_no_wrap = 0;
constexpr f32 k_wrap_to_parent = -1; // set size_from_text = true
constexpr f32 k_default_font_size = 0;

enum class BackgroundShape : u32 { Rectangle, Circle, Count };

enum class TooltipStringType { None, Function, String };
using TooltipString = TaggedUnion<TooltipStringType,
                                  TypeAndTag<NulloptType, TooltipStringType::None>,
                                  TypeAndTag<FunctionRef<String()>, TooltipStringType::Function>,
                                  TypeAndTag<String, TooltipStringType::String>>;

struct Colours {
    style::Colour base = style::Colour::None;
    style::Colour hot = style::Colour::None;
    style::Colour active = style::Colour::None;
};

inline Colours Splat(style::Colour colour) {
    return Colours {
        .base = colour,
        .hot = colour,
        .active = colour,
    };
}

enum class Behaviour : u8 {
    None = 0,

    // Button behaviour. Handle Box::button_fired.
    // Buttons can be fully configured using Boxes; their whole style and behaviour. We don't offer this level
    // of control for other widgets.
    Button = 1 << 0,

    // Text input behaviour. You should supply BoxConfig::text, and handle Box::text_input_result. You can use
    // BoxConfig::activate_on_click_button and the others for configuring when the text input is activated.
    // IMPORTANT: while the background/border is drawn by this system, you must do the drawing of the text,
    // selection, and cursor yourself. There are helper functions for this.
    TextInput = 1 << 1,

    // Knob behaviour.
    // Knobs always trigger on left mouse down.
    // IMPORTANT: you must do the drawing of the knob yourself. There are helper functions for this. The
    // background, border, and text is drawn by this system but nothing else.
    Knob = 1 << 2,
};
ALWAYS_INLINE constexpr Behaviour operator|(Behaviour a, Behaviour b) {
    return (Behaviour)(ToInt(a) | ToInt(b));
}
ALWAYS_INLINE constexpr Behaviour operator&(Behaviour a, Behaviour b) {
    return (Behaviour)(ToInt(a) & ToInt(b));
}

enum class BackgroundTexFillMode : u8 {
    Stretch, // Stretch the image to fill the entire box (default behavior)
    Cover, // Maintain aspect ratio, crop image to fill box completely
    Count,
};

struct BoxConfig {
    // Specifies the parent box for this box. This is used for layout. Use this instead of layout.parent.
    Optional<Box> parent {};

    // Draws this text in the box. Also uses it for size if size_from_text is true.
    String text {};
    f32 wrap_width = k_no_wrap; // See k_no_wrap and k_wrap_to_parent.
    bool32 size_from_text : 1 = false; // Sets layout.size for you.
    bool32 size_from_text_preserve_height : 1 = false; // Only sets width when size_from_text is true.

    FontType font : NumBitsNeededToStore(ToInt(FontType::Count)) {FontType::Body};
    f32 font_size = k_default_font_size;
    Colours text_colours = Splat(style::Colour::Text);
    TextAlignX text_align_x : NumBitsNeededToStore(ToInt(TextAlignX::Count)) = TextAlignX::Left;
    TextAlignY text_align_y : NumBitsNeededToStore(ToInt(TextAlignY::Count)) = TextAlignY::Top;
    TextOverflowType text_overflow
        : NumBitsNeededToStore(ToInt(TextOverflowType::Count)) = TextOverflowType::AllowOverflow;
    bool32 capitalize_text : 1 = false;

    Colours background_fill_colours = Splat(style::Colour::None);
    BackgroundShape background_shape
        : NumBitsNeededToStore(ToInt(BackgroundShape::Count)) = BackgroundShape::Rectangle;
    u8 background_fill_alpha = 255;
    bool32 background_fill_auto_hot_active_overlay : 1 = false;
    bool32 drop_shadow : 1 = false;
    graphics::ImageID const* background_tex {};
    u8 background_tex_alpha = 255;
    BackgroundTexFillMode background_tex_fill_mode = BackgroundTexFillMode::Stretch;

    Colours border_colours = Splat(style::Colour::None);
    f32 border_width_pixels = 1.0f; // Pixels is more useful than vw here.
    bool32 border_auto_hot_active_overlay : 1 = false;

    bool32 parent_dictates_hot_and_active : 1 = false;

    // 4 bits, clockwise from top-left: top-left, top-right, bottom-right, bottom-left, set using 0b0001 etc.
    u32 round_background_corners : 4 = 0;
    bool32 round_background_fully : 1 = false;

    // 4 bits, clockwise from left: left, top, right, bottom, set using 0b0001 etc.
    u32 border_edges : 4 = 0b1111;

    layout::ItemOptions layout {}; // Don't set parent here, use BoxConfig::parent instead.

    TooltipString tooltip = k_nullopt;

    Behaviour behaviour = Behaviour::None;

    bool32 multiline_text_input : 1 = false;

    MouseButton activate_on_click_button
        : NumBitsNeededToStore(ToInt(MouseButton::Count)) = MouseButton::Left;
    bool32 activate_on_double_click : 1 = false;
    ActivationClickEvent activation_click_event
        : NumBitsNeededToStore(ToInt(ActivationClickEvent::Count)) = ActivationClickEvent::Up;
    bool32 ignore_double_click : 1 = false;
    u8 extra_margin_for_mouse_events = 0;

    f32 text_input_x_padding = 4; // Padding for text input, left and right.

    // Configuration for knob behaviour.
    f32 knob_percent {};
    f32 knob_default_percent {};
    f32 knob_sensitivity = 256; // Pixels for a value change of 1.0.
    bool32 slower_with_shift : 1;
    bool32 default_on_modifer : 1;
};

PUBLIC auto ScopedEnableTooltips(GuiBoxSystem& builder, bool enable) {
    struct ScopeGuard {
        GuiBoxSystem& builder;
        bool old_value;

        ScopeGuard(GuiBoxSystem& b, bool old) : builder(b), old_value(old) {}
        ~ScopeGuard() { builder.show_tooltips = old_value; }
    };
    auto old_value = builder.show_tooltips;
    builder.show_tooltips = enable;
    return ScopeGuard {builder, old_value};
}

Box DoBox(GuiBoxSystem& builder,
          BoxConfig const& config,
          SourceLocation source_location = SourceLocation::Current());

bool AdditionalClickBehaviour(GuiBoxSystem& box_system,
                              Box const& box,
                              imgui::ButtonFlags const& config,
                              Rect* out_item_rect = nullptr);

// Returns k_nullopt if we're in the layout pass.
Optional<Rect> BoxRect(GuiBoxSystem& box_system, Box const& box);

struct DrawTextInputConfig {
    style::Colour text_col = style::Colour::Text;
    style::Colour cursor_col = style::Colour::Text;
    style::Colour selection_col = style::Colour::Highlight;
    f32 selection_colour_alpha = 0.5f;
};

void DrawTextInput(GuiBoxSystem& box_system, Box const& box, DrawTextInputConfig const& config = {});

// =================================================================================================================
// Helpers
PUBLIC Rect CentredRect(Rect container, f32x2 size) {
    return {
        .pos = container.pos + ((container.size - size) / 2),
        .size = size,
    };
}
