// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_box_system.hpp"

#include "gui/gui_drawing_helpers.hpp"
#include "image.hpp"

static f32 HeightOfWrappedText(GuiBoxSystem& box_system, layout::Id id, f32 width) {
    if (auto const t_ptr = box_system.state->word_wrapped_texts.Find(id)) {
        auto const& t = *t_ptr;
        return t.font->CalcTextSizeA(t.font_size, FLT_MAX, width, t.text)[1];
    }
    return 0;
}

void AddPanel(GuiBoxSystem& box_system, Panel panel) {
    if (box_system.state->pass == BoxSystemCurrentPanelState::Pass::HandleInputAndRender) {
        auto p = box_system.arena.New<Panel>(panel);
        if (box_system.state->current_panel->first_child) {
            for (auto q = box_system.state->current_panel->first_child; q; q = q->next)
                if (!q->next) {
                    q->next = p;
                    break;
                }
        } else
            box_system.state->current_panel->first_child = p;
    }
}

static void Run(GuiBoxSystem& builder, Panel* panel) {
    ZoneScoped;
    if (!panel) return;

    f32 const scrollbar_width = builder.imgui.VwToPixels(6);
    f32 const scrollbar_padding = Max(2.0f, builder.imgui.VwToPixels(style::k_scrollbar_rhs_space));
    imgui::DrawWindowScrollbar* const draw_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        if (imgui.IsWindowHovered(imgui.CurrentWindow()) || imgui.IsActive(id)) {
            auto const hot_or_active = imgui.IsHotOrActive(id);
            auto const rounding = imgui.VwToPixels(4);

            // Channel.
            if (hot_or_active) {
                u32 col = style::Col(style::Colour::Background2);
                imgui.graphics->AddRectFilled(bounds.Min(), bounds.Max(), col, rounding);
            }

            // Handle.
            {
                u32 handle_col = style::Col(style::Colour::Surface1);
                if (hot_or_active) handle_col = style::Col(style::Colour::Overlay0);
                if (imgui.CurrentWindow()->style.flags & imgui::WindowFlags_ScrollbarInsidePadding) {
                    auto const pad_l = imgui.VwToPixels(hot_or_active ? 1 : 3.0f);
                    auto const pad_r = 0;
                    auto const total_pad = pad_l + pad_r;
                    if (handle_rect.w > total_pad) {
                        handle_rect.x += pad_l;
                        handle_rect.w -= total_pad;
                    }
                }
                imgui.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), handle_col, rounding);
            }
        }
    };

    imgui::DrawWindowBackground* const draw_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto const rounding = imgui.VwToPixels(style::k_panel_rounding);
        auto r = window->unpadded_bounds;
        draw::DropShadow(imgui, r, rounding);
        imgui.graphics->AddRectFilled(r, style::Col(style::Colour::Background0), rounding);
    };

    imgui::WindowSettings regular_window_settings {
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
    };

    imgui::WindowSettings const popup_settings {
        .flags = imgui::WindowFlags_AutoWidth | imgui::WindowFlags_AutoHeight |
                 imgui::WindowFlags_AutoPosition | ({
                     u32 additional_flags = 0;
                     if (auto const popup_data = panel->data.TryGet<PopupPanel>())
                         additional_flags |= popup_data->additional_imgui_window_flags;
                     additional_flags;
                 }),
        .pad_top_left = {1, builder.imgui.VwToPixels(style::k_panel_rounding)},
        .pad_bottom_right = {1, builder.imgui.VwToPixels(style::k_panel_rounding)},
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_padding_top = 0,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
        .draw_routine_popup_background = draw_window,
    };

    imgui::WindowSettings const modal_window_settings {
        .flags = imgui::WindowFlags_NoScrollbarX,
        .scrollbar_padding = scrollbar_padding,
        .scrollbar_width = scrollbar_width,
        .draw_routine_scrollbar = draw_scrollbar,
        .draw_routine_window_background = draw_window,
    };

    switch (panel->data.tag) {
        case PanelType::Subpanel: {
            auto const& subpanel = panel->data.Get<Subpanel>();
            // If the Subpanel is the first panel of this current box system, we can just use the
            // given rect if there is one.
            auto const rect = panel->rect.OrElse([&]() { return subpanel.rect.Value(); });
            auto const size = rect.size;
            ASSERT(All(size > 0));
            regular_window_settings.flags |= subpanel.flags;
            regular_window_settings.pad_top_left =
                builder.imgui.VwToPixels(f32x2 {subpanel.padding.l, subpanel.padding.t});
            regular_window_settings.pad_bottom_right =
                builder.imgui.VwToPixels(f32x2 {subpanel.padding.r, subpanel.padding.b});
            regular_window_settings.pixels_per_line =
                builder.imgui.VwToPixels(subpanel.line_height_for_scroll_wheel);
            builder.imgui.BeginWindow(regular_window_settings, subpanel.imgui_id, rect);
            break;
        }
        case PanelType::Modal: {
            auto const& modal = panel->data.Get<ModalPanel>();

            if (modal.disable_other_interaction) {
                imgui::WindowSettings const invis_sets {
                    .draw_routine_window_background =
                        [darken = modal.darken_background](IMGUI_DRAW_WINDOW_BG_ARGS) {
                            if (!darken) return;
                            auto r = window->unpadded_bounds;
                            imgui.graphics->AddRectFilled(r.Min(), r.Max(), 0x6c0f0d0d);
                        },
                };
                builder.imgui.BeginWindow(invis_sets, {.pos = 0, .size = builder.imgui.Size()}, "invisible");
                DEFER { builder.imgui.EndWindow(); };
                auto invis_window = builder.imgui.CurrentWindow();

                if (modal.close_on_click_outside) {
                    if (builder.imgui.IsWindowHovered(invis_window)) {
                        builder.imgui.frame_output.cursor_type = CursorType::Hand;
                        if (builder.imgui.frame_input.Mouse(MouseButton::Left).presses.size) modal.on_close();
                    }
                }
            }

            auto settings = modal_window_settings;
            if (modal.auto_height) settings.flags |= imgui::WindowFlags_AutoHeight;
            if (modal.auto_width) settings.flags |= imgui::WindowFlags_AutoWidth;
            if (modal.auto_position) settings.flags |= imgui::WindowFlags_AutoPosition;
            if (modal.transparent_panel) settings.draw_routine_window_background = {};

            builder.imgui.BeginWindow(settings, modal.imgui_id, modal.r);

            if (modal.close_on_esc) {
                builder.imgui.frame_output.wants_keyboard_keys.Set(ToInt(KeyCode::Escape));
                if (!builder.imgui.active_text_input && builder.imgui.RequestKeyboardFocus(modal.imgui_id))
                    if (builder.imgui.frame_input.Key(KeyCode::Escape).presses.size) modal.on_close();
            }

            break;
        }
        case PanelType::Popup: {
            auto const popup_data = panel->data.Get<PopupPanel>();
            if (!builder.imgui.BeginWindowPopup(
                    popup_settings,
                    popup_data.popup_imgui_id,
                    panel->rect ? *panel->rect : *popup_data.creator_absolute_rect,
                    popup_data.debug_name.size ? popup_data.debug_name : "popup"_s)) {
                return;
            }
            break;
        }
    }

    {
        BoxSystemCurrentPanelState state {
            .current_panel = panel,
            .boxes = {builder.arena},
            .deferred_actions = {builder.arena},
        };
        builder.state = &state;
        DEFER { builder.state = nullptr; };

        {
            layout::ReserveItemsCapacity(builder.layout, builder.arena, 2048);
            ZoneNamedN(prof1, "Box system: create layout", true);
            panel->run(builder);
        }

        builder.layout.item_height_from_width_calculation = [&builder](layout::Id id, f32 width) {
            return HeightOfWrappedText(builder, id, width);
        };

        {
            ZoneNamedN(prof2, "Box system: calculate layout", true);
            layout::RunContext(builder.layout);
        }

        {
            ZoneNamedN(prof3, "Box system: handle input and render", true);
            state.box_counter = 0;
            state.pass = BoxSystemCurrentPanelState::Pass::HandleInputAndRender;
            panel->run(builder);
        }

        for (auto& action : state.deferred_actions)
            action();
    }

    // Fill in the rect of new panels so we can reuse the layout system.
    // New panels can be identified because they have no rect.
    for (auto p = panel->first_child; p != nullptr; p = p->next) {
        if (p->rect) continue;
        switch (p->data.tag) {
            case PanelType::Subpanel: {
                auto const data = p->data.Get<Subpanel>();
                if (data.rect) {
                    p->rect = *data.rect;
                } else {
                    auto const subpanel_rect = layout::GetRect(builder.layout, data.id);
                    p->rect = subpanel_rect;
                }
                ASSERT(All(p->rect->size > 0));
                break;
            }
            case PanelType::Modal: {
                break;
            }
            case PanelType::Popup: {
                auto const data = p->data.Get<PopupPanel>();
                if (data.creator_absolute_rect) {
                    p->rect = *data.creator_absolute_rect;
                } else {
                    p->rect = layout::GetRect(builder.layout, data.creator_layout_id);
                    // We now have a relative position of the creator of the popup (usually a button). We
                    // need to convert it to screen space. When we run the panel, the imgui system will
                    // take this button rectangle and find a place for the popup below/right of it.
                    p->rect->pos = builder.imgui.WindowPosToScreenPos(p->rect->pos);
                }
                break;
            }
        }
    }

    layout::ResetContext(builder.layout);

    for (auto p = panel->first_child; p != nullptr; p = p->next)
        Run(builder, p);

    builder.imgui.EndWindow();
}

void BeginFrame(GuiBoxSystem& builder, bool show_tooltips) {
    // The layout uses the scratch arena, so we need to make sure we're not using any memory from the previous
    // frame.
    builder.layout = {};
    builder.show_tooltips = show_tooltips;
}

void RunPanel(GuiBoxSystem& builder, Panel initial_panel) {
    auto panel = builder.arena.New<Panel>(initial_panel);
    Run(builder, panel);
}

static f32x2 AlignWithin(Rect container, f32x2 size, TextAlignX align_x, TextAlignY align_y) {
    f32x2 result = container.Min();
    if (align_x == TextAlignX::Centre)
        result.x += (container.w - size.x) / 2;
    else if (align_x == TextAlignX::Right)
        result.x += container.w - size.x;

    if (align_y == TextAlignY::Centre)
        result.y += (container.h - size.y) / 2;
    else if (align_y == TextAlignY::Bottom)
        result.y += container.h - size.y;

    return result;
}

static bool Tooltip(GuiBoxSystem& builder,
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
        imgui.AddTimedWakeup(imgui.frame_input.current_time + style::k_tooltip_open_delay, "Tooltip");

    auto hot_seconds = imgui.SecondsSpentHot();
    if (imgui.IsHot(id) && hot_seconds >= style::k_tooltip_open_delay) {
        builder.imgui.graphics->context->PushFont(builder.fonts[ToInt(FontType::Body)]);
        DEFER { builder.imgui.graphics->context->PopFont(); };

        auto const font = imgui.overlay_graphics.context->CurrentFont();
        auto const pad_x = imgui.VwToPixels(style::k_tooltip_pad_x);
        auto const pad_y = imgui.VwToPixels(style::k_tooltip_pad_y);

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

        auto text_size = draw::GetTextSize(font, str, imgui.VwToPixels(style::k_tooltip_max_width));

        Rect popup_r;
        if (!show_left_or_right) {
            popup_r.x = r.x;
            popup_r.y = r.y + r.h;
        } else {
            popup_r.pos = r.pos;
        }
        popup_r.w = text_size.x + pad_x * 2;
        popup_r.h = text_size.y + pad_y * 2;

        auto const cursor_pos = imgui.frame_input.cursor_pos;

        // Shift the x so that it's centred on the cursor.
        popup_r.x = cursor_pos.x - popup_r.w / 2;

        auto avoid_r = r;
        if (additional_avoid_r) avoid_r = Rect::MakeRectThatEnclosesRects(avoid_r, *additional_avoid_r);

        popup_r.pos = imgui::BestPopupPos(popup_r,
                                          avoid_r,
                                          imgui.frame_input.window_size.ToFloat2(),
                                          show_left_or_right);

        f32x2 text_start;
        text_start.x = popup_r.x + pad_x;
        text_start.y = popup_r.y + pad_y;

        draw::DropShadow(imgui, popup_r);
        imgui.overlay_graphics.AddRectFilled(popup_r.Min(),
                                             popup_r.Max(),
                                             style::Col(style::Colour::Background0),
                                             style::k_tooltip_rounding);
        imgui.overlay_graphics.AddText(font,
                                       font->font_size,
                                       text_start,
                                       style::Col(style::Colour::Text),
                                       str,
                                       text_size.x + 1);
        return true;
    }
    return false;
}

Optional<Rect> BoxRect(GuiBoxSystem& box_system, Box const& box) {
    if (box_system.state->pass != BoxSystemCurrentPanelState::Pass::HandleInputAndRender) return {};
    return layout::GetRect(box_system.layout, box.layout_id);
}

Box DoBox(GuiBoxSystem& builder, BoxConfig const& config, SourceLocation source_location) {
    ZoneScoped;
    auto const box_index = builder.state->box_counter++;
    auto const font = builder.fonts[ToInt(config.font)];
    auto const font_size =
        config.font_size != 0 ? builder.imgui.VwToPixels(config.font_size) : font->font_size;
    ASSERT(font_size > 0);
    ASSERT(font_size < 10000);

    // IMPORTANT: if the string is very long, it needs to be word-wrapped manually by including newlines in
    // the text. This is necessary because our text rendering system is bad at doing huge amounts of
    // word-wrapping. It still renders text that isn't visible unless there's no word-wrapping, in which case
    // it's does skip rendering off-screen text.
    f32 const wrap_width = config.text.size < 10000 ? config.wrap_width : k_no_wrap;

    switch (builder.state->pass) {
        case BoxSystemCurrentPanelState::Pass::LayoutBoxes: {
            ZoneNamedN(tracy_layout, "Box system: layout boxes", true);
            auto const box = Box {
                .layout_id =
                    layout::CreateItem(builder.layout, builder.arena, ({
                                           layout::ItemOptions layout = config.layout;

                                           if (config.parent) [[likely]]
                                               layout.parent = config.parent->layout_id;

                                           // If the size is a pixel size (not one of the special values),
                                           // convert it to pixels.
                                           if (layout.size[0] > 0)
                                               layout.size[0] *= builder.imgui.pixels_per_vw;
                                           if (layout.size[1] > 0)
                                               layout.size[1] *= builder.imgui.pixels_per_vw;

                                           layout.margins.lrtb *= builder.imgui.pixels_per_vw;
                                           layout.contents_gap *= builder.imgui.pixels_per_vw;
                                           layout.contents_padding.lrtb *= builder.imgui.pixels_per_vw;

                                           // Root items need a real size.
                                           if (builder.layout.num_items == 0) {
                                               if (layout.size.x == layout::k_fill_parent)
                                                   layout.size.x = builder.imgui.Width();
                                               if (layout.size.y == layout::k_fill_parent)
                                                   layout.size.y = builder.imgui.Height();
                                           }

                                           if (config.size_from_text) {
                                               if (wrap_width != k_wrap_to_parent) {
                                                   layout.size = font->CalcTextSizeA(font_size,
                                                                                     FLT_MAX,
                                                                                     wrap_width,
                                                                                     config.text);
                                                   ASSERT(layout.size[1] > 0);
                                                   if (config.size_from_text_preserve_height)
                                                       layout.size.y = config.layout.size.y;
                                               } else {
                                                   // We can't know the text size until we know the parent
                                                   // width.
                                                   layout.size = {layout::k_fill_parent, 1};
                                                   layout.set_item_height_after_width_calculated = true;
                                               }
                                           }

                                           layout;
                                       })),
                .imgui_id = {},
                .source_location = source_location,
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

            dyn::Append(builder.state->boxes, box);

            return box;
        }
        case BoxSystemCurrentPanelState::Pass::HandleInputAndRender: {
            ZoneNamedN(tracy_input, "Box system: handle input and render", true);
            auto& box = builder.state->boxes[box_index];
            ASSERT(box.source_location == source_location,
                   "GUI has changed between layout and render, see deffered_actions");

            auto const rect =
                builder.imgui.GetRegisteredAndConvertedRect(layout::GetRect(builder.layout, box.layout_id));

            if (!builder.imgui.IsRectVisible(rect)) return box;

            auto const mouse_rect = rect.Expanded(config.extra_margin_for_mouse_events);

            imgui::ButtonFlags const button_flags {
                .left_mouse = config.activate_on_click_button == MouseButton::Left,
                .right_mouse = config.activate_on_click_button == MouseButton::Right,
                .middle_mouse = config.activate_on_click_button == MouseButton::Middle,
                .double_click = config.activate_on_double_click,
                .ignore_double_click = config.ignore_double_click,
                .triggers_on_mouse_down = config.activation_click_event == ActivationClickEvent::Down,
                .triggers_on_mouse_up = config.activation_click_event == ActivationClickEvent::Up,
            };

            if (ToInt(config.behaviour) || config.tooltip.tag != TooltipStringType::None)
                box.imgui_id = builder.imgui.GetID((usize)box_index);

            if (ToInt(config.behaviour & Behaviour::TextInput)) {
                builder.state->last_text_input_result = builder.imgui.TextInput(
                    rect,
                    box.imgui_id,
                    config.text,
                    config.text_input_placeholder_text,
                    ({
                        imgui::TextInputFlags f {
                            .x_padding = builder.imgui.VwToPixels(config.text_input_x_padding),
                            .centre_align = (config.text_align_x == TextAlignX::Centre),
                            .escape_unfocuses = true,
                        };
                        if (config.multiline_text_input) {
                            f.multiline = true;
                            f.multiline_wordwrap_hack = true;
                        }
                        f;
                    }),
                    button_flags,
                    config.text_input_select_all_on_focus);
                box.is_active = builder.imgui.TextInputHasFocus(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
                box.text_input_result = &builder.state->last_text_input_result;
            }

            if (ToInt(config.behaviour & Behaviour::Knob) &&
                (!ToInt(config.behaviour & Behaviour::TextInput) ||
                 (ToInt(config.behaviour & Behaviour::TextInput) && !box.is_active))) {
                box.knob_percent = config.knob_percent;
                if (!builder.imgui.SliderBehavior(
                        rect,
                        box.imgui_id,
                        box.knob_percent,
                        config.knob_default_percent,
                        config.knob_sensitivity,
                        imgui::SliderFlags {.slower_with_shift = config.slower_with_shift,
                                            .default_on_modifer = config.default_on_modifer})) {
                    box.knob_percent = k_nan<f32>;
                }
                box.is_active = builder.imgui.IsActive(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
                if (box.is_hot) {
                    int b = 0;
                    (void)b;
                }
            }

            if ((config.behaviour & Behaviour::Button) == Behaviour::Button) {
                box.button_fired = builder.imgui.ButtonBehavior(mouse_rect, box.imgui_id, button_flags);
                box.is_active = builder.imgui.IsActive(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
            }

            if (config.tooltip.tag != TooltipStringType::None && !ToInt(config.behaviour)) {
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
                    style::Colour c {};
                    if (config.background_fill_auto_hot_active_overlay)
                        c = config.background_fill_colours.base;
                    else if (is_active)
                        c = config.background_fill_colours.active;
                    else if (is_hot)
                        c = config.background_fill_colours.hot;
                    else
                        c = config.background_fill_colours.base;
                    c;
                });
                background_fill != style::Colour::None || config.background_fill_auto_hot_active_overlay) {

                auto r = rect;
                // If we normally don't show a background, then we can assume that hot/active colours are
                // exclusively for the mouse so we should use the mouse rectangle.
                if (config.background_fill_colours.base == style::Colour::None) r = mouse_rect;

                auto const rounding =
                    config.round_background_corners
                        ? (config.round_background_fully ? Min(r.w, r.h) / 2
                                                         : builder.imgui.VwToPixels(style::k_button_rounding))
                        : 0;

                u32 col_u32 = style::Col(background_fill);
                if (config.background_fill_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                if (config.drop_shadow) draw::DropShadow(builder.imgui, r, rounding);

                // IMPROVE: we shouldn't need to convert this - we should just use the same format throughout
                // the system. The issue is that the drawing code works differently to this system.
                auto const corner_flags = __builtin_bitreverse32(config.round_background_corners) >> 28;

                switch (config.background_shape) {
                    case BackgroundShape::Rectangle:
                        builder.imgui.graphics->AddRectFilled(r, col_u32, rounding, (int)corner_flags);
                        break;
                    case BackgroundShape::Circle: {
                        auto const centre = r.Centre();
                        auto const radius = Min(r.w, r.h) / 2;
                        builder.imgui.graphics->AddCircleFilled(centre, radius, col_u32);
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
                        // Default behavior - stretch image to fill entire box
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
                if (auto const texture =
                        builder.imgui.frame_input.graphics_ctx->GetTextureFromImage(*config.background_tex)) {
                    auto const corner_flags = __builtin_bitreverse32(config.round_background_corners) >> 28;
                    auto const rounding = config.round_background_corners
                                              ? (config.round_background_fully
                                                     ? Min(rect.w, rect.h) / 2
                                                     : builder.imgui.VwToPixels(style::k_button_rounding))
                                              : 0;
                    builder.imgui.graphics->AddImageRounded(*texture,
                                                            rect.Min(),
                                                            rect.Max(),
                                                            uv0,
                                                            uv1,
                                                            col,
                                                            rounding,
                                                            (int)corner_flags);
                }
            }

            if (auto const border = ({
                    style::Colour c {};
                    if (config.border_auto_hot_active_overlay)
                        c = config.border_colours.base;
                    else if (is_active)
                        c = config.border_colours.active;
                    else if (is_hot)
                        c = config.border_colours.hot;
                    else
                        c = config.border_colours.base;
                    c;
                });
                border != style::Colour::None || config.border_auto_hot_active_overlay) {

                auto r = rect;
                if (config.border_colours.base == style::Colour::None) r = mouse_rect;

                u32 col_u32 = style::Col(border);
                if (config.border_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                if (config.border_edges == 0b1111) {
                    // IMPROVE: we shouldn't need to convert this - we should just use the same format
                    // throughout the system. The issue is that the drawing code works differently to this
                    // system.
                    auto const corner_flags = __builtin_bitreverse32(config.round_background_corners) >> 28;

                    auto const rounding = config.round_background_corners
                                              ? builder.imgui.VwToPixels(style::k_button_rounding)
                                              : 0;
                    builder.imgui.graphics->AddRect(r,
                                                    col_u32,
                                                    rounding,
                                                    (int)corner_flags,
                                                    config.border_width_pixels);
                } else {
                    if (config.border_edges & 0b1000) {
                        // Left edge
                        builder.imgui.graphics->AddLine(r.Min(),
                                                        {r.x, r.y + r.h},
                                                        col_u32,
                                                        config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0100) {
                        // Top edge
                        builder.imgui.graphics->AddLine(r.Min(),
                                                        {r.x + r.w, r.y},
                                                        col_u32,
                                                        config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0010) {
                        // Right edge
                        builder.imgui.graphics->AddLine({r.x + r.w, r.y},
                                                        {r.x + r.w, r.y + r.h},
                                                        col_u32,
                                                        config.border_width_pixels);
                    }
                    if (config.border_edges & 0b0001) {
                        // Bottom edge
                        builder.imgui.graphics->AddLine({r.x, r.y + r.h},
                                                        {r.x + r.w, r.y + r.h},
                                                        col_u32,
                                                        config.border_width_pixels);
                    }
                }
            }

            if (config.text.size && !ToInt(config.behaviour & Behaviour::TextInput)) {
                auto text_pos = rect.pos;
                Optional<f32x2> text_size;
                if (config.text_align_x != TextAlignX::Left || config.text_align_y != TextAlignY::Top) {
                    text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0, config.text);
                    text_pos = AlignWithin(rect, *text_size, config.text_align_x, config.text_align_y);
                }

                String text = config.text;
                if (config.text_overflow != TextOverflowType::AllowOverflow) {
                    text = graphics::OverflowText({
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

                builder.imgui.graphics->AddText(font,
                                                font_size,
                                                text_pos,
                                                style::Col(is_hot      ? config.text_colours.hot
                                                           : is_active ? config.text_colours.active
                                                                       : config.text_colours.base),
                                                text,
                                                wrap_width == k_wrap_to_parent ? rect.w : wrap_width);
            }

            if (config.tooltip.tag != TooltipStringType::None) {
                Optional<Rect> additional_avoid_r = {};
                if (config.tooltip_avoid_window_id != 0) {
                    if (auto w = builder.imgui.FindWindow(config.tooltip_avoid_window_id))
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

void DrawTextInput(GuiBoxSystem& builder, Box const& box, DrawTextInputConfig const& config) {
    if (builder.state->pass != BoxSystemCurrentPanelState::Pass::HandleInputAndRender) return;

    auto input_result = box.text_input_result;

    // Not normally null, but it can happen due to DoBox's early return when the box is not visible.
    if (!input_result) return;

    if (input_result->HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {*builder.imgui.graphics->context};
        auto const selection_col = style::Col(config.selection_col);
        while (auto const r = input_result->NextSelectionRect(it))
            builder.imgui.graphics->AddRectFilled(*r, selection_col);
    }

    if (input_result->show_cursor) {
        auto cursor_r = input_result->GetCursorRect();
        builder.imgui.graphics->AddRectFilled(cursor_r.Min(), cursor_r.Max(), style::Col(config.cursor_col));
    }

    builder.imgui.graphics->AddText(
        input_result->GetTextPos(),
        colours::WithAlpha(style::Col(config.text_col), input_result->is_placeholder ? 140 : 255),
        input_result->text);
}

bool AdditionalClickBehaviour(GuiBoxSystem& box_system,
                              Box const& box,
                              imgui::ButtonFlags const& config,
                              Rect* out_item_rect) {
    if (box_system.state->pass == BoxSystemCurrentPanelState::Pass::LayoutBoxes) return false;

    if (!box.is_hot) return false;

    auto const item_r =
        box_system.imgui.WindowRectToScreenRect(layout::GetRect(box_system.layout, box.layout_id));

    auto const result = imgui::ClickCheck(config, box_system.imgui.frame_input, &item_r);
    if (result && out_item_rect) *out_item_rect = item_r;
    return result;
}
