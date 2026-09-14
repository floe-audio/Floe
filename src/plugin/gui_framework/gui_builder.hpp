// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "font_type.hpp"
#include "gui_framework/colours.hpp"
#include "gui_imgui.hpp"
#include "layout.hpp"

// GUI Builder
//
// This is an API for conveniently building whole GUI panels. It's built on top of our IMGUI system, which
// provides immediate-mode GUI functionality (gui_imgui.hpp), and our flexbox-like layout system (layout.hpp).
// Understanding the other 2 APIs will be necessary to properly use this API.
//
// This higher-level API makes the most common parts of building a GUI quick and easy:
// - Laying out boxes (containers, alignment, nesting, etc.). This is outsourced to our layout.hpp module, but
//   this builder makes it easier to use in an immediate-mode way by seamlessly performing 2 passes: firstly
//   building the layout, then handling user input and rendering.
// - Making an element clickable (button behaviour).
// - Drawing text (colour, alignment, font choice, etc.).
// - Drawing rectangles (backgrounds, borders, rounding. etc.).
// - Adding a tooltip to an element.
// All other uses are deliberately not covered by this API, such as sliders, text input, or custom,
// complicated UI elements like an ADSR envelope UI. To implement such things you can just create an box and
// then get a rectangle using BoxRect - and then use the IMGUI API inside the rectangle to build more advanced
// behaviour.
//
// Terminology
// ===========================================================================================================
// - Builder: the name of this API - the bookkeeping involved with running box-viewports.
// - Box-viewport: very similar to imgui::Viewport, except box-viewports have a layout, and are managed by the
//   builder system. Box-viewports run your callback to build the viewport and the boxes inside it.
//   Box-viewports work for any imgui::ViewportMode.
// - Box: a rectangle inside a box-viewport. It might have drawing involved, or button interactivity, or just
//   a be a layout container. Boxes are configured with a big struct; C++ designated initialiser syntax is
//   great and this config leans into it.
//
// 2-pass approach
// ===========================================================================================================
// In order to have a single codepath for both layout and input-handling/rendering, we use a 2-pass approach.
// This means that your box-viewport functions are actually called twice per frame - this is almost
// entirely a technical detail, though occasionally you need to be aware of it if you want to add some custom
// rendering or input-handling (which must be done in the second pass). This is also why we have callbacks as
// part of the API - we need to call the function twice at different stages.
// 1. The first pass is the layout pass where we build a tree of boxes and perform layout on them. No input or
//    rendering should be done here.
// 2. The second pass is the input-handling and rendering pass. At this point we have the layout data.
//
// Unique IDs
// ===========================================================================================================
// Every box MUST have a unique ID otherwise you will hit a Panic. Rather than having to laboriously set an
// ID for every box, there are a few shortcuts that should be used, made up of 4 parts. Provide a mix of
// any of the 4 parts to ensure the system gets a unique ID. In the DoBox function, these 4 parts are hashed
// together.
//
// Firstly, DoBox has a default function argument accepting a source location hash - if DoBox is called from a
// singular, unique location in the codebase, it's already unique. Next, the BoxConfig struct takes an
// id_extra option - provide any value here to build uniqueness (perhaps an index if DoBox is called in a
// loop). Next, the box's ID is seeded on the parent Box's id. And finally, DoBox uses the IMGUI's ID stack
// system (it calls MakeId) which is effected by PushId and PopId - push/pop unique values to ensure
// uniqueness in a scope.
//
// When building custom GUI elements (such as MyTextButton() function), you should probably use the same
// techniques that are used in this system: add a default SourceLocationHash() argument and/or add an id_extra
// option in an arguments struct. Alternatively, you could hash the button text, or any other kind of unique,
// consistent attribute of the element. The most appropriate method will vary.

struct GuiBuilder;

struct Box {
    layout::Id layout_id;
    imgui::Id imgui_id;
    bool32 is_hot : 1 = false;
    bool32 is_active : 1 = false;
    bool32 button_fired : 1 = false;
};

struct BoxViewportConfig {
    using RunFunction = TrivialFunctionRef<void(GuiBuilder&)>;

    enum class BoundsType : u8 { Box, Rect };

    using Bounds =
        TaggedUnion<BoundsType, TypeAndTag<Box, BoundsType::Box>, TypeAndTag<Rect, BoundsType::Rect>>;

    // The run function is where you do all the work which presumably includes lots of calls to DoBox. The
    // function object is cloned if the system needs to store it, so it's safe to use a lambda here (so long
    // as you understand the lambda references lifetime comment of DoBoxViewport).
    RunFunction run;

    // The rectangle for this viewport. Just like IMGUI BeginViewport, the interpretation of this value is
    // dependent on the config's positioning field. For WindowCentred, only size matters (pos is ignored).
    // You can provide either a Box if you are already in a box-viewport, or a Rect in *pixels*.
    Bounds bounds;

    // Box-viewports must be provided with a unique ID.
    imgui::Id imgui_id;

    // IMPORTANT: all sizes in this config should be WW units, not pixels. The builder converts all contents
    // to pixels.
    imgui::ViewportConfig viewport_config;

    String debug_name {}; // Recommended but optional.
};

enum class GuiBuilderPass : u8 {
    LayoutBoxes,
    HandleInputAndRender,
};

// Sides are tried in the order named. The first side is strongly preferred: text is wrapped narrower to fit
// there, and only when even the minimum width doesn't fit is the next side tried. The remaining sides are
// tried after the named ones.
enum class TooltipPlacement : u8 {
    BelowThenAbove,
    AboveThenBelow,
    RightThenLeft,
    LeftThenRight,
    RightThenBelow,
};

// Two boxes: the value popup (regular font, nearest the element) and the tooltip (italic, stacked beside
// the value popup). Either is skipped when its opacity is 0.
struct DrawTooltipArgs {
    Rect r; // The rect that opened the tooltip.
    Rect avoid_r; // The rect to avoid when placing the tooltip;
    TooltipPlacement placement;
    String value_popup {};
    f32 value_popup_opacity = 0;
    // WW units. See TooltipArgs::value_popup_fixed_width.
    Optional<f32> value_popup_fixed_width {};
    String tooltip {};
    String tooltip_footer {}; // Drawn dimmer beneath the tooltip text, inside the same box.
    f32 tooltip_opacity = 0;
};
using DrawOverlayTooltipForRectFunc = void(imgui::Context const& imgui,
                                           Fonts& fonts,
                                           DrawTooltipArgs const& args);
using DrawDropShadowFunc = void(imgui::Context const& imgui, Rect r, Optional<f32> rounding, f32 opacity);

struct GuiBuilder {
    struct WordWrappedText {
        layout::Id id;
        String text;
        Font* font;
        f32 font_size;
    };

    // Ephemeral
    struct CurrentViewportState {
        BoxViewportConfig cfg;
        Optional<Rect> rect {};
        layout::Context layout {
            .snap_to_integers = true,
        };
        GuiBuilderPass pass {GuiBuilderPass::LayoutBoxes};
        HashTable<u64, Box, NoHash> boxes {};
        HashTable<layout::Id, WordWrappedText> word_wrapped_texts {};
        bool mouse_down_on_modal_background = false;
        CurrentViewportState* next {};
        CurrentViewportState* first_child {};

        // Cached values to speed up the hot path in DoBox.
        struct ViewportCache {
            bool is_auto_sized;
            DrawList* draw_list;
            f32 pixels_per_ww;

            f32 WwToPixels(f32 ww) const { return ww * pixels_per_ww; }
            f32x4 WwToPixels(f32x4 ww) const { return ww * pixels_per_ww; }
        };
        ViewportCache viewport_cache {};
    };

    struct Config {
        bool show_tooltips;
        bool instant_value_popups;
        DrawOverlayTooltipForRectFunc* draw_tooltip;
        DrawDropShadowFunc* draw_drop_shadow;
    };

    bool IsLayoutPass() const { return state->pass == GuiBuilderPass::LayoutBoxes; }
    bool IsInputAndRenderPass() const { return state->pass == GuiBuilderPass::HandleInputAndRender; }

    ArenaAllocator& arena; // Scratch arena, cleared each frame.
    imgui::Context& imgui;
    Fonts& fonts;
    Config config;

    CurrentViewportState* state; // Ephemeral

    // Persistent: previous frame's box count per viewport, used to pre-reserve hash tables.
    DynamicHashTable<imgui::Id, u32> prev_box_counts {Malloc::Instance()};
};

void BeginFrame(GuiBuilder& builder, GuiBuilder::Config const& config);

// Begin a viewport, or if we're already in a box-viewport function, queue it to run after the current has
// completed. This very directly maps to a call to IMGUI BeginViewport (although you don't need EndViewport).
// IMPORTANT: if the cfg.bounds is a Box and you are already inside a DoBoxViewport function, the run function
// of this viewport will be run AFTER the current run function completes. As such, you must not capture by
// reference variables that will not be alive at that point. If cfg.bounds is a rect it's run immediately.
void DoBoxViewport(GuiBuilder& builder, BoxViewportConfig const& cfg);

constexpr f32 k_no_wrap = 0;
constexpr f32 k_wrap_to_parent = -1; // You should additionally set size_from_text = true.
constexpr f32 k_default_font_size = 0;

enum class BackgroundShape : u8 { Rectangle, Circle, Count };

enum class TooltipStringType : u8 { None, Function, String };

using TooltipString = TaggedUnion<TooltipStringType,
                                  TypeAndTag<NulloptType, TooltipStringType::None>,
                                  TypeAndTag<FunctionRef<String()>, TooltipStringType::Function>,
                                  TypeAndTag<String, TooltipStringType::String>>;

struct ColSet {
    Col base {};
    Col hot {};
    Col active {};
};

struct Colours {
    constexpr Colours(Col c) { s.base = s.hot = s.active = c; }
    constexpr Colours(ColSet set) { s = set; }
    ColSet s;
};

enum class BackgroundTexFillMode : u8 {
    Stretch, // Stretch the image to fill the entire box (default behavior)
    Cover, // Maintain aspect ratio, crop image to fill box completely
    Count,
};

struct BoxConfig {
    // Specifies the parent box for this box. This is used for layout. Use this instead of layout.parent.
    Optional<Box> parent {};
    u64 id_extra = 0;

    // Draws this text in the box. Also uses it for size if size_from_text is true.
    String text {};
    f32 wrap_width = k_no_wrap; // See k_no_wrap and k_wrap_to_parent.
    bool size_from_text = false; // Sets layout.size for you.
    bool size_from_text_preserve_height = false; // When size_from_text is true, only the width of the text is
                                                 // used, the height is retained from layout.size.

    FontType font {FontType::Body};
    f32 font_size = k_default_font_size;
    Colours text_colours = Col {.c = Col::Text};
    TextJustification text_justification = TextJustification::TopLeft;
    MultilineTextAlignment multiline_alignment = MultilineTextAlignment::Left;
    TextOverflowType text_overflow = TextOverflowType::AllowOverflow;
    bool capitalize_text = false;

    Colours background_fill_colours = Col {.c = Col::None};
    BackgroundShape background_shape = BackgroundShape::Rectangle;
    // Instead of using the hot/active variants from background_fill_colours, always use the base
    // colour and blend a semi-transparent highlight overlay on top for hot (hovered) and active
    // (pressed) states.
    bool background_fill_auto_hot_active_overlay = false;
    bool drop_shadow = false;
    ImageID const* background_tex {};
    u8 background_tex_alpha = 255;
    BackgroundTexFillMode background_tex_fill_mode = BackgroundTexFillMode::Stretch;

    Colours border_colours = Col {.c = Col::None};
    f32 border_width_pixels = 1.0f; // Pixels is more useful than WW here.
    bool border_auto_hot_active_overlay = false;

    bool parent_dictates_hot_and_active = false;

    // Draw in the hot style regardless of the cursor, e.g. a menu item whose submenu is open.
    bool show_as_hot = false;

    // Corners and rounding effect both fill and border.
    Corners round_background_corners = 0b0000;
    f32 corner_rounding = 3.0f;

    // For drawing borders, which sides to draw. See Edges type in draw_list.hpp.
    Edges border_edges = 0b1111;

    layout::ItemOptions layout {}; // Don't set parent here, use BoxConfig::parent instead.

    // Shown immediately while hovered or dragged, regardless of preferences. For information the UI doesn't
    // otherwise show: a knob's value, a meter's level, a preset's description.
    TooltipString value_popup = k_nullopt;
    // Help text. Shown in italics after the mouse rests on the element, only if the show-tooltips
    // preference is on. Placed beside the value popup when both are present.
    TooltipString tooltip = k_nullopt;
    // Interaction hints (e.g. "double-click to type"). Shown dimmer beneath the tooltip text.
    String tooltip_footer {};
    // Placement options below apply to both boxes.
    imgui::Id tooltip_avoid_viewport_id = 0; // 0 = avoid nothing.
    Box const* tooltip_avoid_box = nullptr; // Tooltip is placed outside the visible part of this box.
    TooltipPlacement tooltip_placement = TooltipPlacement::BelowThenAbove;

    Optional<imgui::ButtonConfig> button_behaviour = k_nullopt;
    u8 extra_margin_for_mouse_events = 0;

    String name {};
};

NO_UBSAN Box DoBox(GuiBuilder& builder, BoxConfig const& config, u64 loc_hash = SourceLocationHash());

// Returns k_nullopt if we're in the layout pass. Otherwise, returns a viewport-relative rectangle that you
// can use to add IMGUI behaviours, or do custom drawing in. Use this to add additional functionality that
// isn't provided by BoxConfig, such as: drag behaviour, text input, adding multiple click-modes (right click,
// middle-click).
Optional<Rect> BoxRect(GuiBuilder& builder, Box const& box);

// For custom IMGUI elements that aren't boxes. Same semantics as BoxConfig::value_popup and
// BoxConfig::tooltip. Returns true if anything was drawn.
struct TooltipArgs {
    TooltipString value_popup = k_nullopt;
    // WW units. When set, the value popup box always uses this width instead of sizing to fit the text.
    // Use this when the text length varies between frames (e.g. a cutoff frequency shown as a note name,
    // where sharps change the string length) so the popup doesn't resize/jump as the value changes.
    Optional<f32> value_popup_fixed_width {};
    TooltipString tooltip = k_nullopt;
    String tooltip_footer {};
    Optional<Rect> avoid_r {}; // Window coords. If nullopt, uses the element's rect.
    TooltipPlacement placement = TooltipPlacement::BelowThenAbove;
};
bool Tooltip(GuiBuilder& builder, imgui::Id id, Rect rect_in_window_coords, TooltipArgs const& args);
