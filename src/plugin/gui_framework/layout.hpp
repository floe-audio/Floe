// Copyright 2024-2026 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT
// Based on "layout" micro library.

// A simple CSS flexbox-like layout. Items can contain sub-items, laid out in a given direction, along with
// alignment, margins, paddings, gaps, etc.
//
// It's designed to be convenient when used with C++ designated initializer syntax, and clang vector
// extensions (setting multiple dimensions with a single value, etc.).
//
//
// Example:
//
// layout::Context ctx;
// DEFER { layout::DestroyContext(ctx, arena); };
//
// auto const root = layout::CreateItem(ctx,
//                                      arena,
//                                      {
//                                          .size = {100, layout::k_hug_contents},
//                                          .contents_gap = 3.0f,
//                                          .contents_direction = layout::Direction::Column,
//                                          .contents_align = layout::Alignment::Start,
//                                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
//                                      });
//
// auto const child1 = layout::CreateItem(ctx, arena, { .size = {60, 20} });
// auto const child2 = layout::CreateItem(ctx, arena, { .size = {60, 20} });
//
// layout::RunContext(ctx);
//
// auto const root_rect = layout::GetRect(ctx, root);
// auto const c1_rect = layout::GetRect(ctx, child1);
// auto const c2_rect = layout::GetRect(ctx, child2);

#pragma once
#include "foundation/foundation.hpp"

namespace layout {

enum class Id : u32 {};

struct Item;

struct Context {
    Item* items {};
    f32x4* rects {}; // xywh
    u32 capacity {};
    u32 num_items {};
    TrivialFixedSizeFunction<8, f32(Id id, f32 width)> item_height_from_width_calculation {};
    bool snap_to_integers {};
};

// =========================================================================================================
// Flags: internal data
// =========================================================================================================

namespace flags {

constexpr u32 BitRange(u32 from, u32 to) { return (u32)((1ull << (to + 1ull)) - (1ull << from)); }

enum : u32 {
    AutoLayout = 1 << 1, // don't use this directly, use Row or Column

    // Container flags
    // ======================================================================================================

    // No auto-layout, children will all be positioned at the same position (as per the
    // justify-content flags) unless they set their own anchors.
    NoLayout = 0,
    // left to right, AKA horizontal, CSS flex-direction: row
    Row = AutoLayout | 0,
    // top to bottom, AKA vertical, CSS flex-direction: column
    Column = AutoLayout | 1,

    // Bit 3 = wrap
    NoWrap = 0, // single-line, does nothing if NoLayout
    Wrap = 1 << 2, // items will be wrapped to a new line if needed, does nothing if NoLayout

    Start = 1 << 3, // items at start of row/column, CSS justify-content: start
    Middle = 0, // item at middle of row/column, CSS justify-content: center
    End = 1 << 4, // item at end of row/column, CSS justify-content: end
    // insert spacing to stretch across whole row/column, CSS justify-content: space-between
    Justify = Start | End,

    // Cross-axis alignment, CSS align-items
    CrossAxisStart = 1 << 5,
    CrossAxisMiddle = 0,
    CrossAxisEnd = 1 << 7,

    // Child behaviour flags (anchors, and line-break)
    // ======================================================================================================

    // Anchors cause an item to be positioned at the edge of its parent. All anchors are valid when the parent
    // is NoLayout. When the parent is Row or Column, then only anchors that are in the cross-axis are valid.
    // For example, if you have a row, then you can only use Top, Bottom. If you have a column, then you can
    // only use Left, Right. You can simulate CSS align-content by setting all children to the required
    // anchor.

    CentreHorizontal = 0,
    CentreVertical = 0,
    Centre = 0,

    AnchorLeft = 1 << 8,
    AnchorTop = 1 << 9,
    AnchorRight = 1 << 10,
    AnchorBottom = 1 << 11,

    AnchorLeftAndRight = AnchorLeft | AnchorRight, // causes the item to stretch
    AnchorTopAndBottom = AnchorTop | AnchorBottom, // causes the item to stretch
    AnchorAll = AnchorLeft | AnchorTop | AnchorRight | AnchorBottom,

    // When in a wrapping container, put this element on a new line. Wrapping layout code auto-inserts
    // LineBreak flags as needed. Drawing routines can read this via item pointers as needed after performing
    // layout calculations.
    LineBreak = 1 << 12,

    SetItemHeightAfterWidth = 1 << 13,

    // Internal flags
    // ======================================================================================================

    // IMPORTANT: the direction of an autolayout is determined by the first bit. So flags & 1 will give you
    // the dimension: row or column.
    LayoutModeMask = BitRange(0, 2),
    ContainerMask = BitRange(0, 7),
    ChildBehaviourMask = BitRange(8, 13),

    ItemInserted = 1 << 14,
    HorizontalSizeFixed = 1 << 15,
    VerticalSizeFixed = 1 << 16,

    FixedSizeMask = HorizontalSizeFixed | VerticalSizeFixed,

    // These bits can be used by the user.
    UserMask = BitRange(17, 31),
};

} // namespace flags

// =========================================================================================================
// Main API: all-in-one declarative item creation
// =========================================================================================================

// Rarely used for this API since the other enums provide the functionality you need.
// It's not recommended to combine Left+Right or Top+Bottom, instead, set the size to k_fill_parent.
enum class Anchor : u16 {
    None = 0, // no anchor, item will be in the centre
    Left = flags::AnchorLeft,
    Top = flags::AnchorTop,
    Right = flags::AnchorRight,
    Bottom = flags::AnchorBottom,
};

BITWISE_OPERATORS(Anchor)

enum class Direction : u8 {
    Row = flags::Row,
    Column = flags::Column,
};

enum class Alignment : u8 {
    Start = flags::Start,
    Middle = flags::Middle,
    End = flags::End,
    Justify = flags::Justify,
};

enum class CrossAxisAlign : u8 {
    Start = flags::CrossAxisStart,
    Middle = flags::CrossAxisMiddle,
    End = flags::CrossAxisEnd,
};

// Hug contents: a special container 'size' value. Makes the item precisely as large as the laid out items
// that it contains. Can be set in either dimension.
constexpr f32 k_hug_contents = 0.0f;

// Fill parent: a special 'size' value for any item. Scales the size up to fill the parent's available space.
constexpr f32 k_fill_parent = -1.0f;

struct ItemOptions {
    // Item that this element lives inside.
    Optional<Id> parent {};

    // Size in pixels, or a special value: k_hug_contents or k_fill_parent. This is a clang vector extension
    // type. Set both dimensions to one value with a single assignment, or use brace initialisation.
    f32x2 size {};

    // Space around this element. See Margins type (a union with lots of handy ways to declare values).
    // CSS equivalent: margin.
    Margins margins {};

    // Rarely used in this context. Force the item to an edge of the parent.
    Anchor anchor {Anchor::None};

    // When in a wrapping container, put this element on a newline.
    bool line_break {false};

    // Space inside the elements content.
    // CSS equivalent: padding.
    Margins contents_padding {};

    // Container property: gap between elements. Set for either or both dimensions.
    // CSS equivalent: gap, row-gap, column-gap.
    f32x2 contents_gap {};

    // Container property: the direction that items should be placed inside this item. Defines the 'main
    // axis'.
    // CSS equivalent: flex-direction.
    Direction contents_direction {Direction::Row};

    // Container property: wrap items to a new line if they do not fit in the main axis.
    bool contents_multiline {false};

    // Internal.
    bool set_item_height_after_width_calculated {false};

    // Container property: the alignment along the main axis.
    // CSS equivalent: justify-content.
    Alignment contents_align {Alignment::Middle};

    // Container property: how items are laid out in the 'cross axis' (perpendicular to the 'main axis').
    // CSS equivalent: align-items.
    CrossAxisAlign contents_cross_axis_align {CrossAxisAlign::Middle};
};

// The most commonly used function, designed for usage with C++ designated initializer syntax: fill in the
// options struct, omitting fields you want to leave with default values.
Id CreateItem(Context& ctx, Allocator& a, ItemOptions const& options);

// =========================================================================================================
// Context management
// =========================================================================================================

void ReserveItemsCapacity(Context& ctx, Allocator& a, u32 count);

void DestroyContext(Context& ctx, Allocator& a);

// Resets but doesn't free memory.
void ResetContext(Context& ctx);

// Performs the layout calculations, starting at the root item (id 0). After calling this, you can use
// GetRect() to query for an item's calculated rectangle. If you use procedures such as Append() or Insert()
// after calling this, your calculated data may become invalid if a reallocation occurs.
//
// You should prefer to recreate your items starting from the root instead of doing fine-grained updates to
// the existing context.
//
// However, it's safe to use SetSize on an item, and then re-run RunContext. This might be useful if you are
// doing a resizing animation on items in a layout without any contents changing.
void RunContext(Context& ctx);

// Performing a layout on items where wrapping is enabled in the parent container can cause flags to be
// modified during the calculations. If you plan to call RunContext or RunItem multiple times without calling
// Reset, and if you have a container that uses wrapping, and if the width or height of the container may have
// changed, you should call ClearItemBreak on all the children of a container before calling RunContext or
// RunItem again. If you don't, the layout calculations may perform unnecessary wrapping.
//
// This requirement may be changed in the future.
//
//
// Calling this will also reset any manually-specified breaking. You will need to set the manual breaking
// again, or simply not call this on any items that you know you wanted to break manually.
//
// If you clear your context every time you calculate your layout, or if you don't use wrapping, you don't
// need to call this.
void ClearItemBreak(Context& ctx, Id item);

u32 ItemsCount(Context& ctx);
u32 ItemsCapacity(Context& ctx);

// =========================================================================================================
// Lower-level API: create or modify items, or build them from scratch.
// =========================================================================================================

constexpr Id k_invalid_id {~(UnderlyingType<Id>)0};

// Create a new item, which can just be thought of as a rectangle. Returns the id (handle) used to identify
// the item.
Id CreateItem(Context& ctx, Allocator& a);

// Inserts an item into another item, forming a parent - child relationship. An item can contain any number of
// child items. Items inserted into a parent are put at the end of the ordering, after any existing siblings.
void Insert(Context& ctx, Id parent, Id child);

// Append inserts an item as a sibling after another item. This allows inserting an item into the middle of an
// existing list of items within a parent. It's also more efficient than repeatedly using Insert(ctx, parent,
// new_child) in a loop to create a list of items in a parent, because it does not need to traverse the
// parent's children each time. So if you're creating a long list of children inside of a parent, you might
// prefer to use this after using Insert to insert the first child.
void Append(Context& ctx, Id earlier, Id later);

// Like Insert, but puts the new item as the first child in a parent instead of as the last.
void Push(Context& ctx, Id parent, Id child);

//  Don't keep this around -- it will become invalid as soon as any reallocation occurs.
Item* GetItem(Context const& ctx, Id id);

// Returns k_invalid_id if there is no child.
Id FirstChild(Context const& ctx, Id id);

// Returns k_invalid_id if there is no next sibling.
Id NextSibling(Context const& ctx, Id id);

// Returns the calculated rectangle of an item. This is only valid after calling RunContext and before any
// other reallocation occurs. Otherwise, the result will be undefined.
Rect GetRect(Context const& ctx, Id id);

f32x2 GetSize(Context& ctx, Id item);

// 0 means hug contents, but use the constant k_hug_contents rather than 0.
// You can also use k_fill_parent in either dimension.
void SetSize(Context& ctx, Id id, f32x2 size);

// Flags for how the item behaves inside a parent item.
void SetBehave(Context& ctx, Id id, u32 flags);

// Flags for how the item arranges its children.
void SetContain(Context& ctx, Id id, u32 flags);

// Set the margins on an item. The components of the vector are:
// 0: left, 1: top, 2: right, 3: bottom.
void SetMargins(Context& ctx, Id id, Margins m);

// Get the margins that were set by SetMargins. Caveat: after the layout has run, the margins might be larger
// than you set because internally the system handles contents_padding by expanding the margins of the child
// elements.
Margins GetMargins(Context& ctx, Id id);

} // namespace layout
