// Copyright 2024 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT

// Layout - Simple 2D stacking boxes calculations
// Customised for Floe:
// - Standardised style
// - C++
// - Remove config options and just use what we need
// - Add a higher-level API for creating items
// - Give things better names

#pragma once
#include "foundation/foundation.hpp"

namespace layout {

// stronly-typed id
enum class Id : u32 {};

constexpr Id k_invalid_id {~(UnderlyingType<Id>)0};
constexpr f32 k_hug_contents = 0.0f;
constexpr f32 k_fill_parent = -1.0f;

struct Item {
    u32 flags;
    Id first_child;
    Id next_sibling;
    f32x4 margins_ltrb;
    f32x2 size;
    f32x2 contents_gap; // gap between children
    f32x4 container_padding_ltrb; // padding around all children
};

struct Context {
    Item* items {};
    f32x4* rects {};
    u32 capacity {};
    u32 num_items {};
    TrivialFixedSizeFunction<8, f32(Id id, f32 width)> item_height_from_width_calculation {};
};

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
// changed, you should call ClearItemBreak on all of the children of a container before calling RunContext or
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
ALWAYS_INLINE inline Item* GetItem(Context const& ctx, Id id) {
    ASSERT(id != k_invalid_id && ToInt(id) < ctx.num_items);
    return ctx.items + ToInt(id);
}

// Returns k_invalid_id if there is no child.
ALWAYS_INLINE inline Id FirstChild(Context const& ctx, Id id) {
    Item const* item = GetItem(ctx, id);
    return item->first_child;
}

// Returns k_invalid_id if there is no next sibling.
ALWAYS_INLINE inline Id NextSibling(Context const& ctx, Id id) {
    Item const* item = GetItem(ctx, id);
    return item->next_sibling;
}

// Returns the calculated rectangle of an item. This is only valid after calling RunContext and before any
// other reallocation occurs. Otherwise, the result will be undefined. The vector components are: 0: x
// starting position, 1: y starting position 2: width, 3: height
ALWAYS_INLINE inline f32x4 GetRectXywh(Context const& ctx, Id id) {
    ASSERT(id != k_invalid_id && ToInt(id) < ctx.num_items);
    return ctx.rects[ToInt(id)];
}
ALWAYS_INLINE inline Rect GetRect(Context const& ctx, Id id) {
    auto const xywh = GetRectXywh(ctx, id);
    return {.xywh = xywh};
}

ALWAYS_INLINE inline f32x2 GetSize(Context& ctx, Id item) { return GetItem(ctx, item)->size; }

// 0 means hug contents, but use the constant k_hug_contents rather than 0.
// You can also use k_fill_parent in either dimension.
ALWAYS_INLINE inline void SetItemSize(Item& item, f32x2 size) {
    item.size = size;

    auto const w = size[0];
    if (w == k_hug_contents)
        item.flags &= ~flags::HorizontalSizeFixed;
    else if (w == k_fill_parent) {
        item.size[0] = 0;
        item.flags &= ~flags::HorizontalSizeFixed;
        item.flags |= flags::AnchorLeftAndRight;
    } else {
        ASSERT(w > 0);
        item.flags |= flags::HorizontalSizeFixed;
    }

    auto const h = size[1];
    if (h == k_hug_contents)
        item.flags &= ~flags::VerticalSizeFixed;
    else if (h == k_fill_parent) {
        item.size[1] = 0;
        item.flags &= ~flags::VerticalSizeFixed;
        item.flags |= flags::AnchorTopAndBottom;
    } else {
        ASSERT(h > 0);
        item.flags |= flags::VerticalSizeFixed;
    }
}
ALWAYS_INLINE inline void SetSize(Context& ctx, Id id, f32x2 size) { SetItemSize(*GetItem(ctx, id), size); }

// Flags for how the item behaves inside a parent item.
ALWAYS_INLINE inline void SetBehave(Item& item, u32 flags) {
    ASSERT_EQ(flags & flags::ChildBehaviourMask, flags);
    item.flags = (item.flags & ~flags::ChildBehaviourMask) | flags;
}
ALWAYS_INLINE inline void SetBehave(Context& ctx, Id id, u32 flags) { SetBehave(*GetItem(ctx, id), flags); }

// Flags for how the item arranges its children.
ALWAYS_INLINE inline void SetContain(Item& item, u32 flags) {
    ASSERT_EQ(flags & flags::ContainerMask, flags);
    item.flags = (item.flags & ~flags::ContainerMask) | flags;
}
ALWAYS_INLINE inline void SetContain(Context& ctx, Id id, u32 flags) { SetContain(*GetItem(ctx, id), flags); }

// Set the margins on an item. The components of the vector are:
// 0: left, 1: top, 2: right, 3: bottom.
ALWAYS_INLINE inline void SetMargins(Item& item, f32x4 ltrb) { item.margins_ltrb = ltrb; }
ALWAYS_INLINE inline void SetMargins(Context& ctx, Id item, f32x4 ltrb) {
    SetMargins(*GetItem(ctx, item), ltrb);
}

// Get the margins that were set by SetMargins.
// 0: left, 1: top, 2: right, 3: bottom.
ALWAYS_INLINE inline f32x4& GetMarginsLtrb(Context& ctx, Id item) { return GetItem(ctx, item)->margins_ltrb; }

// =========================================================================================================
// Higher-level API focused on creating items using designated initialisers
// =========================================================================================================

// Lots of unions/structs here so that designated initialisers can be used really effectively to just set the
// value you want, everything else will be zeroed out.
struct Margins {
    union {
        struct {
            union {
                struct {
                    f32 l;
                    f32 r;
                };
                f32x2 lr;
            };
            union {
                struct {
                    f32 t;
                    f32 b;
                };
                f32x2 tb;
            };
        };
        f32x4 lrtb {};
    };
};
static_assert(sizeof(float[4]) == sizeof(f32x4));

// It's not recommended to combine Left+Right or Top+Bottom, instead, set the size to k_fill_parent.
enum class Anchor : u16 {
    None = 0, // no anchor, item will be in the centre
    Left = flags::AnchorLeft,
    Top = flags::AnchorTop,
    Right = flags::AnchorRight,
    Bottom = flags::AnchorBottom,
};

constexpr Anchor operator|(Anchor a, Anchor b) { return (Anchor)(ToInt(a) | ToInt(b)); }
constexpr Anchor operator&(Anchor a, Anchor b) { return (Anchor)(ToInt(a) & ToInt(b)); }

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

struct ItemOptions {
    Optional<Id> parent {};
    f32x2 size {}; // remember k_hug_contents and k_fill_parent
    Margins margins {};
    Anchor anchor {Anchor::None};
    bool line_break {false};
    Margins contents_padding {};
    f32x2 contents_gap {};
    Direction contents_direction {Direction::Row};
    bool contents_multiline {false};
    bool set_item_height_after_width_calculated {false};
    Alignment contents_align {Alignment::Middle};
    CrossAxisAlign contents_cross_axis_align {CrossAxisAlign::Middle};
};

PUBLIC void SetMargins(Item& item, Margins m) {
    ASSERT(All(m.lrtb >= 0));
    ASSERT(All(m.lrtb < 65536)); // just some large value for sanity
    auto const ltrb = __builtin_shufflevector(m.lrtb, m.lrtb, 0, 2, 1, 3);
    SetMargins(item, ltrb);
}
PUBLIC void SetMargins(Context& ctx, Id id, Margins m) { SetMargins(*GetItem(ctx, id), m); }

PUBLIC Margins GetMargins(Context& ctx, Id id) {
    auto const ltrb = GetMarginsLtrb(ctx, id);
    return {.lrtb = __builtin_shufflevector(ltrb, ltrb, 0, 2, 1, 3)};
}

PUBLIC Id CreateItem(Context& ctx, Allocator& a, ItemOptions options) {
    auto const id = CreateItem(ctx, a);

    if (ToInt(id) == 0) {
        ASSERT(All(options.size != k_fill_parent));
        ASSERT(All(options.size >= 0));
        ASSERT(!options.parent);
        ASSERT(options.anchor == Anchor::None);
        ASSERT(!options.set_item_height_after_width_calculated);
    }

    if (auto const width = options.size.x; width > 0)
        ASSERT(options.contents_padding.l + options.contents_padding.r < width);
    if (auto const height = options.size.y; height > 0)
        ASSERT(options.contents_padding.t + options.contents_padding.b < height);

    auto& item = *GetItem(ctx, id);
    item.container_padding_ltrb =
        __builtin_shufflevector(options.contents_padding.lrtb, options.contents_padding.lrtb, 0, 2, 1, 3);
    SetItemSize(item, options.size);
    SetMargins(item, options.margins);
    item.contents_gap = options.contents_gap;
    item.flags |= ToInt(options.anchor) | (options.line_break ? flags::LineBreak : 0) |
                  ToInt(options.contents_direction) | ToInt(options.contents_cross_axis_align) |
                  ToInt(options.contents_align) | (options.contents_multiline ? flags::Wrap : flags::NoWrap) |
                  (options.set_item_height_after_width_calculated ? flags::SetItemHeightAfterWidth : 0);
    if (options.parent) {
        Insert(ctx, *options.parent, id);
        auto& parent = *GetItem(ctx, *options.parent);
        // there's no harm in setting both Top | Left, even though only one will be valid depending on if the
        // parent is a row or column.
        if (parent.flags & flags::CrossAxisStart)
            item.flags |= flags::AnchorTop | flags::AnchorLeft;
        else if (parent.flags & flags::CrossAxisEnd)
            item.flags |= flags::AnchorBottom | flags::AnchorRight;
    }
    return id;
}

} // namespace layout
