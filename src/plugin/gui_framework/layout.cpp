// Copyright 2024 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT

#include "layout.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

namespace layout {

constexpr usize k_total_item_size = sizeof(Item) + sizeof(f32x4);

static Span<u8> Allocation(Context& ctx) { return {(u8*)ctx.items, ctx.capacity * k_total_item_size}; }

void ReserveItemsCapacity(Context& ctx, Allocator& a, u32 new_capacity) {
    if (new_capacity <= ctx.capacity) return;

    if (auto existing_allocation = Allocation(ctx); !existing_allocation.size) {
        ctx.items = (Item*)(void*)a
                        .Allocate({
                            .size = new_capacity * k_total_item_size,
                            .alignment = k_max_alignment,
                            .allow_oversized_result = false,
                        })
                        .data;
    } else {
        ctx.items = (Item*)(void*)a
                        .Resize({
                            .allocation = existing_allocation,
                            .new_size = new_capacity * k_total_item_size,
                            .allow_oversize_result = false,
                            .move_memory_handler =
                                {
                                    .function =
                                        [](ResizeCommand::MoveMemoryHandler::Args args) {
                                            CopyMemory(args.destination, args.source, args.num_bytes);
                                        },
                                },
                        })
                        .data;
    }

    ctx.capacity = new_capacity;

    static_assert(alignof(f32x4) % alignof(Item) == 0);
    auto const* past_last = ctx.items + ctx.capacity;
    ctx.rects = (f32x4*)past_last;
}

void DestroyContext(Context& ctx, Allocator& a) {
    if (ctx.capacity) {
        a.Free(Allocation(ctx));
        ctx.items = nullptr;
        ctx.rects = nullptr;
        ctx.capacity = 0;
    }
}

void ResetContext(Context& ctx) { ctx.num_items = 0; }

static void CalcSize(Context& ctx, Id item, u32 dim);
static void Arrange(Context& ctx, Id item, u32 dim);

void RunItem(Context& ctx, Id id) {
    CalcSize(ctx, id, 0);
    Arrange(ctx, id, 0);

    for (u32 i = 0; i < ctx.num_items; ++i)
        if (ctx.items[i].flags & flags::SetItemHeightAfterWidth) {
            ctx.items[i].size[1] = ctx.item_height_from_width_calculation(Id {i}, ctx.rects[i][2]);
            ctx.items[i].flags |= flags::VerticalSizeFixed;
        }

    CalcSize(ctx, id, 1);
    Arrange(ctx, id, 1);
}

void RunContext(Context& ctx) {
    if (ctx.num_items > 0) RunItem(ctx, Id {0});
}

void ClearItemBreak(Context& ctx, Id item) {
    Item* pitem = GetItem(ctx, item);
    pitem->flags = pitem->flags & ~flags::LineBreak;
}

u32 ItemsCount(Context& ctx) { return ctx.num_items; }

u32 ItemsCapacity(Context& ctx) { return ctx.capacity; }

Id CreateItem(Context& ctx, Allocator& a) {
    Id idx {ctx.num_items++};

    if (ToInt(idx) >= ctx.capacity) ReserveItemsCapacity(ctx, a, ctx.capacity < 1 ? 32 : (ctx.capacity * 4));

    auto* item = GetItem(ctx, idx);
    // We can either do this here, or when creating/resetting buffer
    *item = {};
    item->first_child = k_invalid_id;
    item->next_sibling = k_invalid_id;
    // hmm
    ctx.rects[ToInt(idx)] = {};
    return idx;
}

static ALWAYS_INLINE void AppendByPtr(Item* __restrict earlier, Id later_id, Item* __restrict later) {
    later->next_sibling = earlier->next_sibling;
    later->flags |= flags::ItemInserted;
    earlier->next_sibling = later_id;
}

Id LastChild(Context const& ctx, Id parent_id) {
    auto parent = GetItem(ctx, parent_id);
    auto child_id = parent->first_child;
    if (child_id == k_invalid_id) return k_invalid_id;
    auto child = GetItem(ctx, child_id);
    auto result = child_id;
    for (;;) {
        auto next = child->next_sibling;
        if (next == k_invalid_id) break;
        result = next;
        child = GetItem(ctx, next);
    }
    return result;
}

void Append(Context& ctx, Id earlier_id, Id later_id) {
    ASSERT(later_id != Id {0}); // Must not be root item
    ASSERT(earlier_id != later_id); // Must not be same item id
    Item* __restrict earlier = GetItem(ctx, earlier_id);
    Item* __restrict later = GetItem(ctx, later_id);
    AppendByPtr(earlier, later_id, later);
}

void Insert(Context& ctx, Id parent_id, Id child_id) {
    ASSERT(child_id != Id {0}); // Must not be root item
    ASSERT(parent_id != child_id); // Must not be same item id
    Item* __restrict parent = GetItem(ctx, parent_id);
    Item* __restrict child = GetItem(ctx, child_id);
    ASSERT(!(child->flags & flags::ItemInserted));
    if (parent->first_child == k_invalid_id) {
        // Parent has no existing children, make inserted item the first child.
        parent->first_child = child_id;
        child->flags |= flags::ItemInserted;
    } else {
        // Parent has existing items, iterate to find the last child and append the inserted item after it.
        auto next_id = parent->first_child;
        Item* __restrict next = GetItem(ctx, next_id);
        for (;;) {
            next_id = next->next_sibling;
            if (next_id == k_invalid_id) break;
            next = GetItem(ctx, next_id);
        }
        AppendByPtr(next, child_id, child);
    }
}

void Push(Context& ctx, Id parent_id, Id new_child_id) {
    ASSERT(new_child_id != Id {0}); // Must not be root item
    ASSERT(parent_id != new_child_id); // Must not be same item id
    Item* __restrict parent = GetItem(ctx, parent_id);
    auto old_child = parent->first_child;
    Item* __restrict child = GetItem(ctx, new_child_id);
    ASSERT(!(child->flags & flags::ItemInserted));
    parent->first_child = new_child_id;
    child->flags |= flags::ItemInserted;
    child->next_sibling = old_child;
}

static ALWAYS_INLINE f32 MaxChildSize(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        // rect[dim] will contain the start margin already
        auto const rect = ctx.rects[ToInt(child_id)];
        auto const child_size = rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        max_child_size = Max(max_child_size, child_size);
        child_id = child->next_sibling;
    }
    return max_child_size;
}

static ALWAYS_INLINE f32 TotalChildSize(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto need_size = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[ToInt(child_id)];
        // rect[dim] will contain the start margin already
        need_size += rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        child_id = child->next_sibling;
    }
    return need_size;
}

static ALWAYS_INLINE f32 MaxChildSizeWrapped(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto max_child_size2 = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[ToInt(child_id)];
        if (child->flags & flags::LineBreak) {
            max_child_size2 += max_child_size;
            max_child_size = 0;
        }
        auto const child_size = rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        max_child_size = Max(max_child_size, child_size);
        child_id = child->next_sibling;
    }
    return max_child_size2 + max_child_size;
}

static ALWAYS_INLINE f32 TotalChildSizeWrapped(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto max_child_size2 = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[ToInt(child_id)];
        if (child->flags & flags::LineBreak) {
            max_child_size2 = Max(max_child_size2, max_child_size);
            max_child_size = 0;
        }
        max_child_size += rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        child_id = child->next_sibling;
    }
    return Max(max_child_size2, max_child_size);
}

static void CalcSize(Context& ctx, Id const id, u32 const dim) {
    auto const size_dim = dim + 2;
    auto item = GetItem(ctx, id);

    auto const item_layout_dim = item->flags & 1;

    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto child = GetItem(ctx, child_id);

        // To support item gaps, we increase the inner margins between items
        if (child->next_sibling != k_invalid_id && dim == item_layout_dim)
            child->margins_ltrb[size_dim] += item->contents_gap[dim];

        // To support container padding, we increase the margins of the children
        f32x4 increase {};
        if (dim == item_layout_dim) {
            // Along the layout direction we don't increase margins between items, only the first and last
            increase[dim] = child_id == item->first_child ? item->container_padding_ltrb[dim] : 0;
            increase[size_dim] =
                child->next_sibling == k_invalid_id ? item->container_padding_ltrb[size_dim] : 0;
        } else {
            increase[dim] = item->container_padding_ltrb[dim];
            increase[size_dim] = item->container_padding_ltrb[size_dim];
        }
        child->margins_ltrb += increase;

        // NOTE: this is recursive and will run out of stack space if items are nested too deeply.
        CalcSize(ctx, child_id, dim);
        child_id = child->next_sibling;
    }

    // Set the mutable rect output data to the starting input data
    ctx.rects[ToInt(id)][dim] = item->margins_ltrb[dim];

    // If we have an explicit input size, ust set our output size (which other calc_size and arrange
    // procedures will use) to it.
    if (item->size[dim] != 0) {
        ctx.rects[ToInt(id)][size_dim] = item->size[dim];
        return;
    }

    // Calculate our size based on children items. Note that we've already called CalcSize on our children at
    // this point.
    f32 cal_size;
    switch (item->flags & flags::LayoutModeMask) {
        case flags::Column | flags::Wrap:
            if (dim) // direction
                cal_size = TotalChildSize(ctx, id, 1);
            else
                cal_size = MaxChildSize(ctx, id, 0);
            break;
        case flags::Row | flags::Wrap:
            if (!dim) // direction
                cal_size = TotalChildSizeWrapped(ctx, id, 0);
            else
                cal_size = MaxChildSizeWrapped(ctx, id, 1);
            break;
        case flags::Column:
        case flags::Row:
            if (item_layout_dim == dim) // direction
                cal_size = TotalChildSize(ctx, id, dim);
            else
                cal_size = MaxChildSize(ctx, id, dim);
            break;
        default:
            // NoLayout
            cal_size = MaxChildSize(ctx, id, dim);
            break;
    }

    // Set our output data size. Will be used by parent calc_size procedures., and by arrange procedures.
    ctx.rects[ToInt(id)][size_dim] = cal_size;
}

static ALWAYS_INLINE void ArrangeStacked(Context& ctx, Id id, u32 const dim, bool const wrap) {
    auto const size_dim = dim + 2;
    auto item = GetItem(ctx, id);

    auto const item_flags = item->flags;
    auto rect = ctx.rects[ToInt(id)];
    auto space = rect[size_dim];

    auto max_x2 = rect[dim] + space;

    auto start_child_id = item->first_child;
    while (start_child_id != k_invalid_id) {
        f32 used = 0;
        u32 count = 0; // count of fillers
        u32 total = 0;
        bool hardbreak = false;
        // first pass: count items that need to be expanded, and the space that is used
        auto child_id = start_child_id;
        auto end_child_id = k_invalid_id;
        while (child_id != k_invalid_id) {
            auto* child = GetItem(ctx, child_id);
            auto const child_flags = child->flags;
            auto const behaviour_flags = (child_flags & flags::ChildBehaviourMask) >> dim;
            auto const child_margins = child->margins_ltrb;
            auto child_rect = ctx.rects[ToInt(child_id)];
            auto extend = used;
            if ((behaviour_flags & flags::AnchorLeftAndRight) == flags::AnchorLeftAndRight) {
                ++count;
                extend += child_rect[dim] + child_margins[size_dim];
            } else {
                extend += child_rect[dim] + child_rect[size_dim] + child_margins[size_dim];
            }
            // wrap on end of line or manual flag
            if (wrap && (total && ((extend > space) || (child_flags & flags::LineBreak)))) {
                end_child_id = child_id;
                hardbreak = (child_flags & flags::LineBreak) == flags::LineBreak;
                // add marker for subsequent queries
                child->flags = child_flags | flags::LineBreak;
                break;
            } else {
                used = extend;
                child_id = child->next_sibling;
            }
            ++total;
        }

        auto extra_space = space - used;
        auto filler = 0.0f;
        auto spacer = 0.0f;
        auto extra_margin = 0.0f;

        if (extra_space > 0) {
            if (count > 0)
                filler = extra_space / (f32)count;
            else if (total > 0) {
                switch (item_flags & flags::Justify) {
                    case flags::Justify:
                        // ustify when not wrapping or not in last line, or not manually breaking
                        if (!wrap || ((end_child_id != k_invalid_id) && !hardbreak))
                            spacer = extra_space / (f32)(total - 1);
                        break;
                    case flags::Start: break;
                    case flags::End: extra_margin = extra_space; break;
                    default: extra_margin = extra_space / 2.0f; break;
                }
            }
        }

        // distribute width among items
        auto x = rect[dim];
        f32 x1;
        // second pass: distribute and rescale
        child_id = start_child_id;
        while (child_id != end_child_id) {
            f32 ix0;
            f32 ix1;
            auto* child = GetItem(ctx, child_id);
            auto const child_flags = child->flags;
            auto const behaviour_flags = (child_flags & flags::ChildBehaviourMask) >> dim;
            auto const fixed_size_flags = (child_flags & flags::FixedSizeMask) >> dim;
            auto const child_margins = child->margins_ltrb;
            auto child_rect = ctx.rects[ToInt(child_id)];

            x += child_rect[dim] + extra_margin;
            if ((behaviour_flags & flags::AnchorLeftAndRight) == flags::AnchorLeftAndRight) // grow
                x1 = x + filler;
            else if ((fixed_size_flags & flags::HorizontalSizeFixed) == flags::HorizontalSizeFixed)
                x1 = x + child_rect[size_dim];
            else // squeeze
                // NOTE(Sam): I have removed the eater addition in the squeeze calculations, so that when
                // passing 0 as a width or height, the compenent will fit to the size of it's children, even
                // if it overruns the parent size. This is not fully tested if it does what I expect all the
                // time
                x1 = x + Max(0.0f, child_rect[size_dim] /* + eater */);

            ix0 = x;
            if (wrap)
                ix1 = Min(max_x2 - child_margins[size_dim], x1);
            else
                ix1 = x1;
            child_rect[dim] = ix0; // pos
            child_rect[size_dim] = ix1 - ix0; // size
            ctx.rects[ToInt(child_id)] = child_rect;
            x = x1 + child_margins[size_dim];
            child_id = child->next_sibling;
            extra_margin = spacer;
        }

        start_child_id = end_child_id;
    }
}

static ALWAYS_INLINE void ArrangeOverlay(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    auto* item = GetItem(ctx, id);
    auto const rect = ctx.rects[ToInt(id)];
    auto const offset = rect[dim];
    auto const space = rect[size_dim];

    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto* child = GetItem(ctx, child_id);
        auto const behaviour_flags = (child->flags & flags::ChildBehaviourMask) >> dim;
        auto const child_margins = child->margins_ltrb;
        auto child_rect = ctx.rects[ToInt(child_id)];

        switch (behaviour_flags & flags::AnchorLeftAndRight) {
            case flags::CentreHorizontal:
                child_rect[dim] += (space - child_rect[size_dim]) / 2 - child_margins[size_dim];
                break;
            case flags::AnchorRight:
                child_rect[dim] +=
                    space - child_rect[size_dim] - child_margins[dim] - child_margins[size_dim];
                break;
            case flags::AnchorLeftAndRight:
                child_rect[size_dim] = Max(0.0f, space - child_rect[dim] - child_margins[size_dim]);
                break;
            default: break;
        }

        child_rect[dim] += offset;
        ctx.rects[ToInt(child_id)] = child_rect;
        child_id = child->next_sibling;
    }
}

static ALWAYS_INLINE void ArrangeOverlaySqueezedRange(Context& ctx,
                                                      u32 const dim,
                                                      Id start_item_id,
                                                      Id end_item_id,
                                                      f32 offset,
                                                      f32 space) {
    auto const size_dim = dim + 2;
    auto item_id = start_item_id;
    while (item_id != end_item_id) {
        auto* item = GetItem(ctx, item_id);
        // IMPORTANT: we bitwise shift by the dimension so that we can use the left/right flags regardless of
        // what dimension we are in
        auto const behaviour_flags = (item->flags & flags::ChildBehaviourMask) >> dim;
        auto const margins = item->margins_ltrb;
        auto rect = ctx.rects[ToInt(item_id)];
        auto min_size = Max(0.0f, space - rect[dim] - margins[size_dim]);
        switch (behaviour_flags & flags::AnchorLeftAndRight) {
            case flags::CentreHorizontal:
                rect[size_dim] = Min(rect[size_dim], min_size);
                rect[dim] += (space - rect[size_dim]) / 2 - margins[size_dim];
                break;
            case flags::AnchorRight:
                rect[size_dim] = Min(rect[size_dim], min_size);
                rect[dim] = space - rect[size_dim] - margins[size_dim];
                break;
            case flags::AnchorLeftAndRight: {
                rect[size_dim] = min_size;
                break;
            }
            default: {
                rect[size_dim] = Min(rect[size_dim], min_size);
                break;
            }
        }
        rect[dim] += offset;
        ctx.rects[ToInt(item_id)] = rect;
        item_id = item->next_sibling;
    }
}

static ALWAYS_INLINE f32 ArrangeWrappedOverlaySqueezed(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    auto* item = GetItem(ctx, id);
    auto offset = ctx.rects[ToInt(id)][dim];
    auto need_size = 0.0f;
    auto child_id = item->first_child;
    auto start_child_id = child_id;
    while (child_id != k_invalid_id) {
        Item* child = GetItem(ctx, child_id);
        if (child->flags & flags::LineBreak) {
            ArrangeOverlaySqueezedRange(ctx, dim, start_child_id, child_id, offset, need_size);
            offset += need_size;
            start_child_id = child_id;
            need_size = 0;
        }
        f32x4 const rect = ctx.rects[ToInt(child_id)];
        f32 child_size = rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        need_size = Max(need_size, child_size);
        child_id = child->next_sibling;
    }
    ArrangeOverlaySqueezedRange(ctx, dim, start_child_id, k_invalid_id, offset, need_size);
    offset += need_size;
    return offset;
}

static void Arrange(Context& ctx, Id id, u32 dim) {
    auto* item = GetItem(ctx, id);

    auto const flags = item->flags;
    switch (flags & flags::LayoutModeMask) {
        case flags::Column | flags::Wrap:
            if (dim != 0) {
                ArrangeStacked(ctx, id, 1, true);
                f32 offset = ArrangeWrappedOverlaySqueezed(ctx, id, 0);
                ctx.rects[ToInt(id)][2 + 0] = offset - ctx.rects[ToInt(id)][0];
            }
            break;
        case flags::Row | flags::Wrap:
            if (dim == 0)
                ArrangeStacked(ctx, id, 0, true);
            else
                // discard return value
                ArrangeWrappedOverlaySqueezed(ctx, id, 1);
            break;
        case flags::Column:
        case flags::Row:
            if ((flags & 1) == dim) {
                ArrangeStacked(ctx, id, dim, false);
            } else {
                f32x4 const rect = ctx.rects[ToInt(id)];
                ArrangeOverlaySqueezedRange(ctx,
                                            dim,
                                            item->first_child,
                                            k_invalid_id,
                                            rect[dim],
                                            rect[dim + 2]);
            }
            break;
        default: ArrangeOverlay(ctx, id, dim); break;
    }
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        // NOTE: this is recursive and will run out of stack space if items are nested too deeply.
        Arrange(ctx, child_id, dim);
        auto* child = GetItem(ctx, child_id);
        child_id = child->next_sibling;
    }
}

} // namespace layout

enum Colours : u32 {
    Rosewater = 0xdc8a78,
    Flamingo = 0xdd7878,
    Pink = 0xea76cb,
    Mauve = 0x8839ef,
    Red = 0xd20f39,
    Maroon = 0xe64553,
    Peach = 0xfe640b,
    Yellow = 0xdf8e1d,
    Green = 0x40a02b,
    Teal = 0x179299,
    Sky = 0x04a5e5,
    Sapphire = 0x209fb5,
    Blue = 0x1e66f5,
    Lavender = 0x7287fd,
    Text = 0x4c4f69,
    Subtext1 = 0x5c5f77,
    Subtext0 = 0x6c6f85,
    Overlay2 = 0x7c7f93,
    Overlay1 = 0x8c8fa1,
    Overlay0 = 0x9ca0b0,
    Surface2 = 0xacb0be,
    Surface1 = 0xbcc0cc,
    Surface0 = 0xccd0da,
    Base = 0xeff1f5,
    Mantle = 0xe6e9ef,
    Crust = 0xdce0e8,
};

struct LayoutImageArgs {
    layout::ItemOptions root_options;
    Array<layout::ItemOptions, 3> child_options;
};

static ErrorCodeOr<String> GenerateSvgContainerHugChildFill(ArenaAllocator& arena) {
    layout::Context ctx;
    DEFER { layout::DestroyContext(ctx, arena); };

    auto const root = layout::CreateItem(ctx,
                                         arena,
                                         {
                                             .size = layout::k_hug_contents,
                                             .contents_direction = layout::Direction::Column,
                                         });

    auto const child1_wrapper =
        layout::CreateItem(ctx,
                           arena,
                           {
                               .parent = root,
                               .size = {layout::k_hug_contents, layout::k_hug_contents},
                           });

    auto const child1_inner = layout::CreateItem(ctx,
                                                 arena,
                                                 {
                                                     .parent = child1_wrapper,
                                                     .size = {60, 20},
                                                 });

    auto const child2_wrapper =
        layout::CreateItem(ctx,
                           arena,
                           {
                               .parent = root,
                               .size = {layout::k_fill_parent, layout::k_hug_contents},
                           });

    auto const child2_inner = layout::CreateItem(ctx,
                                                 arena,
                                                 {
                                                     .parent = child2_wrapper,
                                                     .size = {100, 20},
                                                 });

    layout::RunContext(ctx);

    auto const root_rect = layout::GetRect(ctx, root);

    DynamicArray<char> svg {arena};
    fmt::Append(svg,
                "<svg width=\"{}\" height=\"{}\" xmlns=\"http://www.w3.org/2000/svg\">\n",
                root_rect.w,
                root_rect.h);

    auto draw_rect = [&](Rect rect, u32 colour) {
        fmt::Append(svg,
                    "<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"#{06x}\" />\n",
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    colour);
    };

    draw_rect({.pos = 0, .size = root_rect.size}, Base);

    draw_rect(layout::GetRect(ctx, child1_wrapper), Yellow);
    draw_rect(layout::GetRect(ctx, child1_inner), Red);

    draw_rect(layout::GetRect(ctx, child2_wrapper), Yellow);
    draw_rect(layout::GetRect(ctx, child2_inner), Green);

    fmt::Append(svg, "</svg>\n");

    dyn::AppendSpan(svg, "<p>");
    auto print_rect_desc = [&](String name, Rect rect) {
        fmt::Append(svg, "{}: {.0}, {.0}, {.0}, {.0}<br>\n", name, rect.x, rect.y, rect.w, rect.h);
    };

    print_rect_desc("root", layout::GetRect(ctx, root));
    print_rect_desc("child1_wrapper", layout::GetRect(ctx, child1_wrapper));
    print_rect_desc("child1_inner", layout::GetRect(ctx, child1_inner));
    print_rect_desc("child2_wrapper", layout::GetRect(ctx, child2_wrapper));
    print_rect_desc("child2_inner", layout::GetRect(ctx, child2_inner));
    dyn::AppendSpan(svg, "</p><hr>\n");

    return svg.ToOwnedSpan();
}

static ErrorCodeOr<String> GenerateLayoutSvg3ChildElements(ArenaAllocator& arena, LayoutImageArgs args) {
    layout::Context ctx;
    DEFER { layout::DestroyContext(ctx, arena); };

    auto root = layout::CreateItem(ctx, arena, args.root_options);
    for (auto& child_options : args.child_options)
        child_options.parent = root;

    Array<layout::Id, args.child_options.size> children;
    for (auto const i : Range(children.size))
        children[i] = layout::CreateItem(ctx, arena, args.child_options[i]);

    layout::RunContext(ctx);

    DynamicArray<char> svg {arena};
    fmt::Append(svg,
                "<svg width=\"{}\" height=\"{}\" xmlns=\"http://www.w3.org/2000/svg\">\n",
                args.root_options.size.x,
                args.root_options.size.y);

    auto print_rect = [&](Rect rect, u32 colour) {
        fmt::Append(svg,
                    "<rect x=\"{}\" y=\"{}\" width=\"{}\" height=\"{}\" fill=\"#{06x}\" />\n",
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    colour);
    };

    print_rect({.pos = 0, .size = args.root_options.size}, Base);

    auto const colours = Array {Red, Green, Blue, Yellow, Peach, Pink, Mauve, Flamingo, Rosewater};

    for (auto const i : Range(children.size)) {
        auto const rect = layout::GetRect(ctx, children[i]);
        print_rect(rect, colours[i]);
    }
    fmt::Append(svg, "</svg>\n");
    for (auto const i : Range(children.size)) {
        auto& item = *layout::GetItem(ctx, children[i]);
        auto const rect = layout::GetRect(ctx, children[i]);
        fmt::Append(svg,
                    "<p>child {}: {.0}, {.0}, {.0}, {.0}, margins ltrb: {.0}, {.0}, {.0}, {.0}</p>\n",
                    i,
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    item.margins_ltrb[0],
                    item.margins_ltrb[1],
                    item.margins_ltrb[2],
                    item.margins_ltrb[3]);
    }
    fmt::Append(svg, "<hr>\n");
    return svg.ToOwnedSpan();
}

static String DirectionName(layout::Direction direction) {
    switch (direction) {
        case layout::Direction::Row: return "row";
        case layout::Direction::Column: return "column";
    }
    PanicIfReached();
    return {};
}

static String JustifyContentName(layout::Alignment j) {
    switch (j) {
        case layout::Alignment::Start: return "start";
        case layout::Alignment::Middle: return "middle";
        case layout::Alignment::End: return "end";
        case layout::Alignment::Justify: return "justify";
    }
    PanicIfReached();
    return {};
}

static String AnchorName(layout::Anchor a) {
    switch (ToInt(a)) {
        case ToInt(layout::Anchor::None): return "none";
        case ToInt(layout::Anchor::Left): return "left";
        case ToInt(layout::Anchor::Right): return "right";
        case ToInt(layout::Anchor::Top): return "top";
        case ToInt(layout::Anchor::Bottom): return "bottom";
        case ToInt(layout::Anchor::Left | layout::Anchor::Right): return "fill-x";
        case ToInt(layout::Anchor::Top | layout::Anchor::Bottom): return "fill-y";
    }
    PanicIfReached();
    return {};
}

TEST_CASE(TestLayout) {
    using namespace layout;

    auto const output_dir = tests::HumanCheckableOutputFilesFolder(tester);
    DynamicArray<char> html {tester.arena};
    fmt::Append(html, "<!DOCTYPE html><html>\n<head>\n<title>Layout Tests</title>\n</head>\n<body>\n");

    auto const basic_child = layout::ItemOptions {
        .size = 20,
    };

    for (auto const padding : Array {0, 8}) {
        for (auto const gap : Array {0.0f, 8.0f}) {
            for (auto const contents_direction : Array {Direction::Row, Direction::Column}) {
                for (auto const contents_align :
                     Array {Alignment::Start, Alignment::Middle, Alignment::End, Alignment::Justify}) {
                    for (auto const middle_item_anchor :
                         Array {Anchor::None,
                                Anchor::Left,
                                Anchor::Right,
                                Anchor::Top,
                                Anchor::Bottom,
                                contents_direction == Direction::Row ? Anchor::Top | Anchor::Bottom
                                                                     : Anchor::Left | Anchor::Right}) {
                        auto const filename =
                            fmt::Format(tester.arena,
                                        "{}, {}, middle-anchor: {}, gap: {.0}, padding: {.0}",
                                        DirectionName(contents_direction),
                                        JustifyContentName(contents_align),
                                        AnchorName(middle_item_anchor),
                                        gap,
                                        padding);
                        LayoutImageArgs args {
                            .root_options =
                                {
                                    .size = 128,
                                    .contents_padding = {.lrtb = padding},
                                    .contents_gap = gap,
                                    .contents_direction = contents_direction,
                                    .contents_align = contents_align,
                                },
                            .child_options = {basic_child, basic_child, basic_child},
                        };
                        args.child_options[1].anchor = middle_item_anchor;
                        auto const svg = TRY(GenerateLayoutSvg3ChildElements(tester.arena, args));

                        fmt::Append(html, "<p>{}</p>\n{}", filename, svg);
                    }
                }
            }
        }
    }

    dyn::AppendSpan(html, TRY(GenerateSvgContainerHugChildFill(tester.arena)));

    fmt::Append(html, "</body>\n</html>\n");
    TRY(WriteFile(path::Join(tester.arena, Array {output_dir, "layout-tests.html"}), html));

    return k_success;
}

TEST_REGISTRATION(RegisterLayoutTests) { REGISTER_TEST(TestLayout); }
