// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_builder.hpp"

#include "utils/debug/tracy_wrapped.hpp"

#include "gui/gui_drawing_helpers.hpp"
#include "gui_framework/fonts.hpp"
#include "image.hpp"

constexpr f64 k_tooltip_open_delay = 0.5;
constexpr f32 k_tooltip_max_width = 200;
constexpr f32 k_tooltip_pad_x = 5;
constexpr f32 k_tooltip_pad_y = 2;
constexpr f32 k_tooltip_rounding = k_button_rounding;

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
    c.padding = {.lrtb = GuiIo().WwToPixels(c.padding.lrtb)};
    c.scrollbar_width = GuiIo().WwToPixels(c.scrollbar_width);
    c.scrollbar_padding = Max(2.0f, GuiIo().WwToPixels(c.scrollbar_padding));
    c.scroll_line_size = GuiIo().WwToPixels(c.scroll_line_size);
    return c;
}

static void Run(GuiBuilder& builder, GuiBuilder::BoxViewport* panel) {
    ZoneScoped;
    if (!panel) return;

    // If the panel is the first panel of this current builder, we can just use the
    // given rectangle if there is one.
    auto const rect = panel->rect.OrElse([&]() { return panel->cfg.bounds.Get<Rect>(); });

    builder.imgui.BeginViewport(ConvertViewportConfigWwToPixels(panel->cfg.viewport_config),
                                panel->cfg.imgui_id,
                                rect,
                                panel->cfg.debug_name);

    builder.imgui.SetViewportMinimumAutoSize(rect.size);

    {
        GuiBuilder::CurrentViewportState state {
            .current_viewport = panel,
        };
        builder.state = &state;
        DEFER { builder.state = nullptr; };

        {
            layout::ReserveItemsCapacity(builder.layout, builder.arena, 2048);
            ZoneNamedN(prof1, "Builder: create layout", true);
            panel->cfg.run(builder);
        }

        builder.layout.item_height_from_width_calculation = [&builder](layout::Id id, f32 width) {
            return HeightOfWrappedText(builder, id, width);
        };

        {
            ZoneNamedN(prof2, "Builder: calculate layout", true);
            layout::RunContext(builder.layout);
        }

        {
            ZoneNamedN(prof3, "Builder: handle input and render", true);
            state.pass = GuiBuilderPass::HandleInputAndRender;
            panel->cfg.run(builder);
        }
    }

    // Fill in the rect of new panels so we can reuse the layout system.
    // New panels can be identified because they have no rect.
    for (auto p = panel->first_child; p != nullptr; p = p->next) {
        if (p->rect) continue;

        p->rect = ({
            Rect r {};
            switch (p->cfg.bounds.tag) {
                case BoxViewportConfig::BoundsType::Box: {
                    r = layout::GetRect(builder.layout, p->cfg.bounds.Get<Box>().layout_id);
                    switch (p->cfg.viewport_config.positioning) {
                        case imgui::ViewportPositioning::ParentRelative: break;
                        case imgui::ViewportPositioning::WindowAbsolute:
                        case imgui::ViewportPositioning::AutoPosition:
                            r.pos = builder.imgui.ViewportPosToWindowPos(r.pos);
                            break;
                    }
                    break;
                }
                case BoxViewportConfig::BoundsType::Rect: r = p->cfg.bounds.Get<Rect>(); break;
            }
            r;
        });

        ASSERT(All(p->rect->size > 0));
    }

    layout::ResetContext(builder.layout);

    for (auto p = panel->first_child; p != nullptr; p = p->next)
        Run(builder, p);

    builder.imgui.EndViewport();
}

void BeginFrame(GuiBuilder& builder, bool show_tooltips) {
    // The layout uses the scratch arena, so we need to make sure we're not using any memory from the previous
    // frame.
    builder.layout = {};
    builder.show_tooltips = show_tooltips;
}

void DoBoxViewport(GuiBuilder& builder, BoxViewportConfig const& config) {
    if (builder.state) {
        if (builder.IsInputAndRenderPass()) {
            auto p = builder.arena.New<GuiBuilder::BoxViewport>(GuiBuilder::BoxViewport {.cfg = config});
            if (builder.state->current_viewport->first_child) {
                for (auto q = builder.state->current_viewport->first_child; q; q = q->next)
                    if (!q->next) {
                        q->next = p;
                        break;
                    }
            } else
                builder.state->current_viewport->first_child = p;
        }
    } else {
        Run(builder, builder.arena.New<GuiBuilder::BoxViewport>(GuiBuilder::BoxViewport {.cfg = config}));
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
                    bool show_left_or_right) {
    ZoneScoped;
    if (!builder.show_tooltips) return false;
    if (tooltip_str.tag == TooltipStringType::None) return false;

    auto& imgui = builder.imgui;
    if (imgui.WasJustMadeHot(id))
        GuiIo().out.AddTimedWakeup(GuiIo().in.current_time + k_tooltip_open_delay, "Tooltip");

    auto hot_seconds = imgui.SecondsSpentHot();
    if (imgui.IsHot(id) && hot_seconds >= k_tooltip_open_delay) {
        builder.fonts.Push(ToInt(FontType::Body));
        DEFER { builder.fonts.Pop(); };

        auto const pad_x = GuiIo().WwToPixels(k_tooltip_pad_x);
        auto const pad_y = GuiIo().WwToPixels(k_tooltip_pad_y);

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

        auto text_size =
            builder.fonts.CalcTextSize(str, {.wrap_width = GuiIo().WwToPixels(k_tooltip_max_width)});

        Rect popup_r;
        if (!show_left_or_right) {
            popup_r.x = r.x;
            popup_r.y = r.y + r.h;
        } else {
            popup_r.pos = r.pos;
        }
        popup_r.w = text_size.x + pad_x * 2;
        popup_r.h = text_size.y + pad_y * 2;

        auto const cursor_pos = GuiIo().in.cursor_pos;

        // Shift the x so that it's centred on the cursor.
        popup_r.x = cursor_pos.x - popup_r.w / 2;

        auto avoid_r = r;
        if (additional_avoid_r) avoid_r = Rect::MakeRectThatEnclosesRects(avoid_r, *additional_avoid_r);

        popup_r.pos =
            imgui::BestPopupPos(popup_r, avoid_r, GuiIo().in.window_size.ToFloat2(), show_left_or_right);

        f32x2 text_start;
        text_start.x = popup_r.x + pad_x;
        text_start.y = popup_r.y + pad_y;

        DrawDropShadow(imgui, popup_r);
        imgui.overlay_draw_list->AddRectFilled(popup_r,
                                               ToU32({.c = Col::Background0}),
                                               GuiIo().WwToPixels(k_tooltip_rounding));
        imgui.overlay_draw_list->AddText(text_start,
                                         ToU32({.c = Col::Text}),
                                         str,
                                         {.wrap_width = text_size.x + 1});
        return true;
    }
    return false;
}

Optional<Rect> BoxRect(GuiBuilder& builder, Box const& box) {
    if (!builder.IsInputAndRenderPass()) return {};
    if (box.layout_id == layout::k_invalid_id) return {};
    return layout::GetRect(builder.layout, box.layout_id);
}

Box DoBox(GuiBuilder& builder, BoxConfig const& config, u64 loc_hash) {
    ZoneScoped;
    auto const id =
        builder.imgui.MakeId(loc_hash ^ config.id_extra ^ (config.parent ? config.parent->imgui_id : 0));
    auto const font = builder.fonts.atlas[ToInt(config.font)];
    auto const font_size = config.font_size != 0 ? GuiIo().WwToPixels(config.font_size) : font->font_size;
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
                    layout::CreateItem(builder.layout, builder.arena, ({
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
                                           if (builder.layout.num_items == 0) {
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
                                                       layout.size.y = config.layout.size.y;
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
                builder.imgui.RegisterAndConvertRect(layout::GetRect(builder.layout, box.layout_id));

            // We want to let our IMGUI system know our margins when it's doing an auto-size otherwise the
            // bottom or rightmost elements might not have the requested spacing around it.
            if (Any(builder.imgui.curr_viewport->cfg.auto_size)) {
                auto const margins = layout::GetMargins(builder.layout, box.layout_id);
                auto bb = layout::GetRect(builder.layout, box.layout_id);
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
                box.is_active = builder.imgui.IsActive(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
            }

            if (config.tooltip.tag != TooltipStringType::None && !config.button_behaviour) {
                builder.imgui.SetHot(rect, box.imgui_id);
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

                auto const rounding = Min(GuiIo().WwToPixels(config.corner_rounding), Min(r.w, r.h) / 2);

                u32 col_u32 = ToU32(background_fill);
                if (config.background_fill_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_hot_white_overlay)
                                          : k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? BlendColours(col_u32, k_auto_active_white_overlay)
                                          : k_auto_active_white_overlay;
                }

                if (config.drop_shadow) DrawDropShadow(builder.imgui, r, rounding);

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
                    auto const rounding =
                        Min(GuiIo().WwToPixels(config.corner_rounding), Min(rect.w, rect.h) / 2);
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
                        config.round_background_corners ? GuiIo().WwToPixels(config.corner_rounding) : 0;
                    builder.imgui.draw_list->AddRect(r,
                                                     col_u32,
                                                     rounding,
                                                     config.round_background_corners,
                                                     config.border_width_pixels);
                } else {
                    if (config.border_edges & 0b1000) {
                        // Left edge
                        builder.imgui.draw_list->AddLine(r.Min(),
                                                         {r.x, r.y + r.h},
                                                         col_u32,
                                                         config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0100) {
                        // Top edge
                        builder.imgui.draw_list->AddLine(r.Min(),
                                                         {r.x + r.w, r.y},
                                                         col_u32,
                                                         config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0010) {
                        // Right edge
                        builder.imgui.draw_list->AddLine({r.x + r.w, r.y},
                                                         {r.x + r.w, r.y + r.h},
                                                         col_u32,
                                                         config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0001) {
                        // Bottom edge
                        builder.imgui.draw_list->AddLine({r.x, r.y + r.h},
                                                         {r.x + r.w, r.y + r.h},
                                                         col_u32,
                                                         config.border_width_pixels);
                    }
                }
            }

            if (config.text.size) {
                auto text_pos = rect.pos;
                Optional<f32x2> text_size;
                if (config.text_justification != TextJustification::TopLeft) {
                    text_size = builder.fonts.CalcTextSize(config.text, {.font_size = font_size});
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
                        .wrap_width = wrap_width == k_wrap_to_parent ? rect.w : wrap_width,
                        .font_size = font_size,
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
                        config.tooltip_show_left_or_right);
            }

            return box;
        }
    }

    return {};
}
