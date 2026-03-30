// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_builder.hpp"

#include "utils/debug/tracy_wrapped.hpp"

#include "gui_framework/fonts.hpp"
#include "image.hpp"

constexpr u32 k_auto_hot_white_overlay = Hsla(k_highlight_hue, 35, 70, 20);
constexpr u32 k_auto_active_white_overlay = Hsla(k_highlight_hue, 35, 70, 38);

static f32 HeightOfWrappedText(GuiBuilder& builder, layout::Id id, f32 width) {
    if (auto const t_ptr = builder.state->word_wrapped_texts.Find(id)) {
        auto const& t = *t_ptr;
        return t.font->CalcTextSize(t.text,
                                    {
                                        .font_size = t.font_size,
                                        .max_width = FLT_MAX,
                                        .wrap_width = width,
                                    })[1];
    }
    return 0;
}

static imgui::ViewportConfig ConvertViewportConfigWwToPixels(imgui::ViewportConfig c) {
    c.padding = {.lrtb = WwToPixels(c.padding.lrtb)};
    c.scrollbar_width = WwToPixels(c.scrollbar_width);
    c.scrollbar_padding = Max(2.0f, WwToPixels(c.scrollbar_padding));
    c.scroll_line_size = WwToPixels(c.scroll_line_size);
    return c;
}

static void Run(GuiBuilder& builder, GuiBuilder::CurrentViewportState* state) {
    if (!state) return;

    ZoneScoped;
    // If the panel is the first panel of this current builder, we can just use the
    // given rectangle if there is one.
    auto const rect = state->rect.OrElse([&]() { return state->cfg.bounds.Get<Rect>(); });

    builder.imgui.BeginViewport(ConvertViewportConfigWwToPixels(state->cfg.viewport_config),
                                state->cfg.imgui_id,
                                rect,
                                state->cfg.debug_name);

    if (state->cfg.viewport_config.positioning != imgui::ViewportPositioning::AutoPosition)
        builder.imgui.SetViewportMinimumAutoSize(rect.size);

    {
        auto const initial_state = builder.state;
        builder.state = state;
        DEFER { builder.state = initial_state; };

        {
            layout::ReserveItemsCapacity(state->layout, builder.arena, 2048);
            ZoneNamedN(prof1, "Builder: create layout", true);
            state->cfg.run(builder);
        }

        state->layout.item_height_from_width_calculation = [&builder](layout::Id id, f32 width) {
            return HeightOfWrappedText(builder, id, width);
        };

        {
            ZoneNamedN(prof2, "Builder: calculate layout", true);
            layout::RunContext(state->layout);
        }

        {
            ZoneNamedN(prof3, "Builder: handle input and render", true);
            state->pass = GuiBuilderPass::HandleInputAndRender;
            state->cfg.run(builder);
        }
    }

    // Fill in the rect of new panels. New panels can be identified because they have no rect.
    for (auto p = state->first_child; p != nullptr; p = p->next) {
        if (p->rect) continue;

        p->rect = ({
            Rect r {};
            switch (p->cfg.bounds.tag) {
                case BoxViewportConfig::BoundsType::Box: {
                    r = layout::GetRect(state->layout, p->cfg.bounds.Get<Box>().layout_id);
                    switch (p->cfg.viewport_config.positioning) {
                        case imgui::ViewportPositioning::ParentRelative: break;
                        case imgui::ViewportPositioning::WindowAbsolute:
                        case imgui::ViewportPositioning::AutoPosition:
                        case imgui::ViewportPositioning::WindowCentred:
                            r.pos = builder.imgui.ViewportPosToWindowPos(r.pos);
                            break;
                    }
                    break;
                }
                case BoxViewportConfig::BoundsType::Rect: r = p->cfg.bounds.Get<Rect>(); break;
            }
            if (p->cfg.viewport_config.positioning != imgui::ViewportPositioning::WindowCentred)
                ASSERT(All(r.size > 0));
            r;
        });
    }

    for (auto p = state->first_child; p != nullptr; p = p->next)
        Run(builder, p);

    builder.imgui.EndViewport();
}

void BeginFrame(GuiBuilder& builder, GuiBuilder::Config const& config) { builder.config = config; }

void DoBoxViewport(GuiBuilder& builder, BoxViewportConfig const& config) {
    if (builder.state) {
        if (builder.IsInputAndRenderPass()) {
            auto s = builder.arena.New<GuiBuilder::CurrentViewportState>(
                GuiBuilder::CurrentViewportState {.cfg = config});
            if (config.bounds.tag == BoxViewportConfig::BoundsType::Box) {
                s->cfg.run = config.run.CloneObject(builder.arena);
                s->cfg.viewport_config = s->cfg.viewport_config.Clone(builder.arena);
                if (builder.state->first_child) {
                    for (auto q = builder.state->first_child; q; q = q->next)
                        if (!q->next) {
                            q->next = s;
                            break;
                        }
                } else
                    builder.state->first_child = s;
            } else {
                if (auto r = s->cfg.bounds.TryGetMut<Rect>()) r->Integerise();
                Run(builder, s);
            }
        }
    } else {
        auto s = builder.arena.New<GuiBuilder::CurrentViewportState>(
            GuiBuilder::CurrentViewportState {.cfg = config});
        if (auto r = s->cfg.bounds.TryGetMut<Rect>()) r->Integerise();
        Run(builder, s);
    }
}

static f32x2 AlignWithin(Rect container, f32x2 size, TextJustification justification) {
    f32x2 result = container.Min();
    if (justification & TextJustification::HorizontallyCentred)
        result.x += (container.w - size.x) / 2;
    else if (justification & TextJustification::Right)
        result.x += container.w - size.x;

    if (justification & TextJustification::VerticallyCentred)
        result.y += (container.h - size.y) / 2;
    else if (justification & TextJustification::Bottom)
        result.y += container.h - size.y;

    return result;
}

static bool Tooltip(GuiBuilder& builder,
                    imgui::Id id,
                    Rect r,
                    Optional<Rect> additional_avoid_r,
                    TooltipString tooltip_str,
                    TooltipJustification justification) {
    ZoneScoped;
    if (!builder.config.show_tooltips) return false;
    if (tooltip_str.tag == TooltipStringType::None) return false;

    if (builder.imgui.TooltipBehaviour(r, id)) {
        auto const str = ({
            String s;
            switch (tooltip_str.tag) {
                case TooltipStringType::None: PanicIfReached();
                case TooltipStringType::Function: {
                    s = tooltip_str.Get<FunctionRef<String()>>()();
                    break;
                }
                case TooltipStringType::String: {
                    s = tooltip_str.Get<String>();
                    break;
                }
            }
            s;
        });

        auto const avoid_r = ({
            auto a_r = r;
            if (additional_avoid_r) a_r = Rect::MakeRectThatEnclosesRects(a_r, *additional_avoid_r);
            a_r;
        });

        builder.config.draw_tooltip(builder.imgui,
                                    builder.fonts,
                                    str,
                                    {
                                        .r = r,
                                        .avoid_r = avoid_r,
                                        .justification = justification,
                                    });

        return true;
    }

    return false;
}

Optional<Rect> BoxRect(GuiBuilder& builder, Box const& box) {
    if (!builder.IsInputAndRenderPass()) return {};
    if (box.layout_id == layout::k_invalid_id) return {};
    return layout::GetRect(builder.state->layout, box.layout_id);
}

Box DoBox(GuiBuilder& builder, BoxConfig const& config, u64 loc_hash) {
    ZoneScoped;
    auto const id =
        builder.imgui.MakeId(loc_hash ^ config.id_extra ^ (config.parent ? config.parent->imgui_id : 0));
    ASSERT(config.parent || builder.state->pass != GuiBuilderPass::LayoutBoxes ||
               builder.state->layout.num_items == 0,
           "DoBox requires a parent unless it's the first item (root)");
    auto const font = builder.fonts.atlas[ToInt(config.font)];
    auto const font_size = config.font_size != 0 ? WwToPixels(config.font_size) : font->font_size;
    ASSERT(font_size > 0);
    ASSERT(font_size < 10000);

    // IMPORTANT: if the string is very long, it needs to be word-wrapped manually by including newlines in
    // the text. This is necessary because our text rendering system is bad at doing huge amounts of
    // word-wrapping. It still renders text that isn't visible unless there's no word-wrapping, in which case
    // it's does skip rendering off-screen text.
    f32 const wrap_width = config.text.size < 10000 ? config.wrap_width : k_no_wrap;

    switch (builder.state->pass) {
        case GuiBuilderPass::LayoutBoxes: {
            ZoneNamedN(tracy_layout, "Builder: layout boxes", true);
            auto const box = Box {
                .layout_id =
                    layout::CreateItem(builder.state->layout, builder.arena, ({
                                           layout::ItemOptions layout = config.layout;

                                           if (config.parent) [[likely]]
                                               layout.parent = config.parent->layout_id;

                                           // If the size is a pixel size (not one of the special
                                           // values), convert it to pixels.
                                           if (layout.size[0] > 0) layout.size[0] *= GuiIo().in.pixels_per_ww;
                                           if (layout.size[1] > 0) layout.size[1] *= GuiIo().in.pixels_per_ww;

                                           layout.margins.lrtb *= GuiIo().in.pixels_per_ww;
                                           layout.contents_gap *= GuiIo().in.pixels_per_ww;
                                           layout.contents_padding.lrtb *= GuiIo().in.pixels_per_ww;

                                           // Root items need a real size.
                                           if (builder.state->layout.num_items == 0) {
                                               if (layout.size.x == layout::k_fill_parent)
                                                   layout.size.x = builder.imgui.CurrentVpWidth();
                                               if (layout.size.y == layout::k_fill_parent)
                                                   layout.size.y = builder.imgui.CurrentVpHeight();
                                           }

                                           if (config.size_from_text) {
                                               if (wrap_width != k_wrap_to_parent) {
                                                   layout.size =
                                                       font->CalcTextSize(config.text,
                                                                          {
                                                                              .font_size = font_size,
                                                                              .max_width = FLT_MAX,
                                                                              .wrap_width = wrap_width,
                                                                          });
                                                   ASSERT(layout.size[1] > 0);
                                                   if (config.size_from_text_preserve_height)
                                                       layout.size.y = WwToPixels(config.layout.size.y);
                                               } else {
                                                   // We can't know the text size until we know the
                                                   // parent width.
                                                   layout.size = {layout::k_fill_parent, 1};
                                                   layout.set_item_height_after_width_calculated = true;
                                               }
                                           }

                                           layout;
                                       })),
                .imgui_id = id,
            };

            if (config.size_from_text && wrap_width == k_wrap_to_parent) {
                builder.state->word_wrapped_texts.InsertGrowIfNeeded(
                    builder.arena,
                    box.layout_id,
                    {
                        .id = box.layout_id,
                        .text = builder.arena.Clone(config.text),
                        .font = font,
                        .font_size = font_size,
                    });
            }

            auto const inserted = builder.state->boxes.InsertGrowIfNeeded(builder.arena, id, box);
            ASSERT(inserted, "duplicate ID detected, change id_extra");

            return box;
        }
        case GuiBuilderPass::HandleInputAndRender: {
            ZoneNamedN(tracy_input, "Builder: handle input and render", true);
            auto box_ptr = builder.state->boxes.Find(id, id);

            if (!box_ptr) {
                // A box was added in the input-and-render pass.
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                return {
                    .layout_id = layout::k_invalid_id,
                    .imgui_id = id,
                };
            }

            auto& box = *box_ptr;

            auto const rect =
                builder.imgui.RegisterAndConvertRect(layout::GetRect(builder.state->layout, box.layout_id));

            // We want to let our IMGUI system know our margins when it's doing an auto-size otherwise the
            // bottom or rightmost elements might not have the requested spacing around it.
            if (Any(builder.imgui.curr_viewport->cfg.auto_size)) {
                auto const margins = layout::GetMargins(builder.state->layout, box.layout_id);
                auto bb = layout::GetRect(builder.state->layout, box.layout_id);
                bb.size += margins.lrtb.yw;
                auto _ = builder.imgui.RegisterAndConvertRect(bb);
            }

            if (!builder.imgui.IsRectVisible(rect)) return box;

            builder.fonts.Push(font);
            DEFER { builder.fonts.Pop(); };

            auto const mouse_rect = rect.Expanded(config.extra_margin_for_mouse_events);

            if (config.button_behaviour) {
                box.button_fired =
                    builder.imgui.ButtonBehaviour(mouse_rect, box.imgui_id, *config.button_behaviour);
                box.is_active = builder.imgui.IsActive(box.imgui_id, config.button_behaviour->mouse_button);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
            }

            //
            // Drawing
            //

            bool32 const is_active =
                config.parent_dictates_hot_and_active ? config.parent->is_active : box.is_active;
            bool32 const is_hot = config.parent_dictates_hot_and_active ? config.parent->is_hot : box.is_hot;

            if (auto const background_fill = ({
                    Col c {};
                    if (config.background_fill_auto_hot_active_overlay)
                        c = config.background_fill_colours.s.base;
                    else if (is_active)
                        c = config.background_fill_colours.s.active;
                    else if (is_hot)
                        c = config.background_fill_colours.s.hot;
                    else
                        c = config.background_fill_colours.s.base;
                    c;
                });
                background_fill.c != Col::None || config.background_fill_auto_hot_active_overlay) {

                auto r = rect;
                // If we normally don't show a background, then we can assume that hot/active colours are
                // exclusively for the mouse so we should use the mouse rectangle.
                if (config.background_fill_colours.s.base.c == Col::None) r = mouse_rect;

                auto const rounding = Min(WwToPixels(config.corner_rounding), Min(r.w, r.h) / 2);

                u32 col_u32 = ToU32(background_fill);
                if (config.background_fill_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_hot_white_overlay)
                                          : k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_active_white_overlay)
                                          : k_auto_active_white_overlay;
                }

                if (config.drop_shadow) builder.config.draw_drop_shadow(builder.imgui, r, rounding);

                switch (config.background_shape) {
                    case BackgroundShape::Rectangle:
                        builder.imgui.draw_list->AddRectFilled(r,
                                                               col_u32,
                                                               rounding,
                                                               config.round_background_corners);
                        break;
                    case BackgroundShape::Circle: {
                        auto const centre = r.Centre();
                        auto const radius = Min(r.w, r.h) / 2;
                        builder.imgui.draw_list->AddCircleFilled(centre, radius, col_u32);
                        return box;
                    }
                    case BackgroundShape::Count: PanicIfReached();
                }
            }

            if (config.background_tex) {
                auto const col =
                    ((u32)config.background_tex_alpha << 24) | 0x00FFFFFF; // Alpha in high bits, RGB as white

                f32x2 uv0 {0, 0};
                f32x2 uv1 {1, 1};

                switch (config.background_tex_fill_mode) {
                    case BackgroundTexFillMode::Stretch: {
                        // Default behaviour - stretch image to fill entire box
                        uv0 = {0, 0};
                        uv1 = {1, 1};
                        break;
                    }
                    case BackgroundTexFillMode::Cover: {
                        // Use GetMaxUVToMaintainAspectRatio to crop the image while maintaining aspect ratio
                        auto const container_size = f32x2 {rect.w, rect.h};
                        auto const max_uv =
                            GetMaxUVToMaintainAspectRatio(*config.background_tex, container_size);

                        uv0 = {0, 0};
                        uv1 = max_uv;
                        break;
                    }
                    case BackgroundTexFillMode::Count: PanicIfReached();
                }

                // Convert ImageID to TextureHandle for rendering
                if (auto const texture = GuiIo().in.renderer->GetTextureFromImage(*config.background_tex)) {
                    auto const rounding = Min(WwToPixels(config.corner_rounding), Min(rect.w, rect.h) / 2);
                    builder.imgui.draw_list->AddImageRounded(*texture,
                                                             rect.Min(),
                                                             rect.Max(),
                                                             uv0,
                                                             uv1,
                                                             col,
                                                             rounding,
                                                             config.round_background_corners);
                }
            }

            if (auto const border = ({
                    Col c {};
                    if (config.border_auto_hot_active_overlay)
                        c = config.border_colours.s.base;
                    else if (is_active)
                        c = config.border_colours.s.active;
                    else if (is_hot)
                        c = config.border_colours.s.hot;
                    else
                        c = config.border_colours.s.base;
                    c;
                });
                border.c != Col::None || config.border_auto_hot_active_overlay) {

                auto r = rect;
                if (config.border_colours.s.base.c == Col::None) r = mouse_rect;

                u32 col_u32 = ToU32(border);
                if (config.border_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_hot_white_overlay)
                                          : k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_active_white_overlay)
                                          : k_auto_active_white_overlay;
                }

                if (config.border_edges == 0b1111) {
                    auto const rounding =
                        config.round_background_corners ? WwToPixels(config.corner_rounding) : 0;
                    builder.imgui.draw_list->AddRect(r,
                                                     col_u32,
                                                     rounding,
                                                     config.round_background_corners,
                                                     config.border_width_pixels);
                } else {
                    builder.imgui.draw_list->AddBorderEdges(r,
                                                            col_u32,
                                                            config.border_edges,
                                                            config.border_width_pixels);
                }
            }

            if (config.text.size) {
                auto const effective_wrap_width =
                    wrap_width == k_wrap_to_parent ? rect.w : wrap_width;
                auto text_pos = rect.pos;
                Optional<f32x2> text_size;
                if (config.text_justification != TextJustification::TopLeft) {
                    text_size = builder.fonts.CalcTextSize(
                        config.text,
                        {.font_size = font_size, .wrap_width = effective_wrap_width});
                    text_pos = AlignWithin(rect, *text_size, config.text_justification);
                }

                String text = config.text;
                if (config.text_overflow != TextOverflowType::AllowOverflow) {
                    text = OverflowText({
                        .font = font,
                        .font_size = font_size,
                        .r = rect,
                        .str = config.text,
                        .overflow_type = config.text_overflow,
                        .font_scaling = 1,
                        .text_size = text_size,
                        .allocator = builder.arena,
                        .text_pos = text_pos,
                    });
                }

                builder.imgui.draw_list->AddText(
                    text_pos,
                    ToU32(is_hot      ? config.text_colours.s.hot
                          : is_active ? config.text_colours.s.active
                                      : config.text_colours.s.base),
                    text,
                    {
                        .wrap_width = effective_wrap_width,
                        .font_size = font_size,
                        .multiline_alignment = config.multiline_alignment,
                        .multiline_alignment_width = rect.w,
                    });
            }

            if (config.tooltip.tag != TooltipStringType::None) {
                Optional<Rect> additional_avoid_r = {};
                if (config.tooltip_avoid_viewport_id != 0) {
                    if (auto w = builder.imgui.FindViewport(config.tooltip_avoid_viewport_id))
                        additional_avoid_r = w->visible_bounds;
                }
                Tooltip(builder,
                        config.parent_dictates_hot_and_active ? config.parent->imgui_id : box.imgui_id,
                        rect,
                        additional_avoid_r,
                        config.tooltip,
                        config.tooltip_justification);
            }

            return box;
        }
    }

    return {};
}
