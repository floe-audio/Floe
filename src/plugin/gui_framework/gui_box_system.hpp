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
    DynamicArray<WordWrappedText> word_wrapped_texts;
    bool mouse_down_on_modal_background = false;
    imgui::TextInputResult last_text_input_result {};

    // TODO: this is a hack. The issue is this: in our 2-pass system, if we change state partway through the
    // second pass that causes a different GUI to be rendered, it crashses because it will be using
    // layout/box data from the first pass, but the GUI is different from the first pass. This is a hack to
    // prevent that. We should fix this by perhaps turning the boxes field into a hashmap and requiring each
    // box to have a unique ID. This way, we lookup the box by ID and can know when something is missing and
    // skip it.
    DynamicArray<TrivialFixedSizeFunction<40, void()>> deferred_actions;
};

struct GuiBoxSystem {
    ArenaAllocator& arena;
    imgui::Context& imgui;
    Fonts& fonts;
    layout::Context layout;
    bool show_tooltips;

    BoxSystemCurrentPanelState* state; // Ephemeral
};

PUBLIC f32 HeightOfWrappedText(GuiBoxSystem& box_system, layout::Id id, f32 width) {
    for (auto& t : box_system.state->word_wrapped_texts)
        if (id == t.id) return t.font->CalcTextSizeA(t.font_size, FLT_MAX, width, t.text)[1];
    return 0;
}

PUBLIC void AddPanel(GuiBoxSystem& box_system, Panel panel) {
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

PUBLIC void Run(GuiBoxSystem& builder, Panel* panel) {
    ZoneScoped;
    if (!panel) return;

    f32 const scrollbar_width = builder.imgui.VwToPixels(8);
    f32 const scrollbar_padding = builder.imgui.VwToPixels(style::k_scrollbar_rhs_space);
    imgui::DrawWindowScrollbar* const draw_scrollbar = [](IMGUI_DRAW_WINDOW_SCROLLBAR_ARGS) {
        u32 handle_col = style::Col(style::Colour::Surface1);
        if (imgui.IsHotOrActive(id)) handle_col = style::Col(style::Colour::Surface2);
        imgui.graphics->AddRectFilled(handle_rect.Min(), handle_rect.Max(), handle_col, imgui.VwToPixels(4));
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
            auto const size = panel->rect->size;
            ASSERT(All(size > 0));
            regular_window_settings.flags = subpanel.flags;
            builder.imgui.BeginWindow(regular_window_settings, subpanel.imgui_id, *panel->rect);
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
                        if (builder.imgui.frame_input.Mouse(MouseButton::Left).presses.size) {
                            [[maybe_unused]] int b = 0;
                            modal.on_close();
                        }
                    }
                }
            }

            auto settings = modal_window_settings;
            if (modal.auto_height) settings.flags |= imgui::WindowFlags_AutoHeight;
            if (modal.auto_width) settings.flags |= imgui::WindowFlags_AutoWidth;
            if (modal.auto_position) settings.flags |= imgui::WindowFlags_AutoPosition;
            if (modal.transparent_panel) settings.draw_routine_window_background = {};

            builder.imgui.BeginWindow(settings, modal.imgui_id, modal.r);
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
            .word_wrapped_texts = {builder.arena},
            .deferred_actions = {builder.arena},
        };
        builder.state = &state;
        DEFER { builder.state = nullptr; };

        {
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
                auto const subpanel_rect = layout::GetRect(builder.layout, data.id);
                ASSERT(All(subpanel_rect.size > 0));
                p->rect = subpanel_rect;
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

PUBLIC void RunPanel(GuiBoxSystem& builder, Panel initial_panel) {
    auto panel = builder.arena.New<Panel>(initial_panel);
    Run(builder, panel);
}

enum class ActivationClickEvent : u32 { None, Down, Up, Count };

enum class TextAlignX : u32 { Left, Centre, Right, Count };
enum class TextAlignY : u32 { Top, Centre, Bottom, Count };

PUBLIC f32x2 AlignWithin(Rect container, f32x2 size, TextAlignX align_x, TextAlignY align_y) {
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

constexpr f32 k_no_wrap = 0;
constexpr f32 k_wrap_to_parent = -1; // set size_from_text = true
constexpr f32 k_default_font_size = 0;

enum class TextInputBox : u32 { None, SingleLine, MultiLine, Count };

enum class BackgroundShape : u32 { Rectangle, Circle, Count };

enum class TooltipStringType { None, Function, String };
using TooltipString = TaggedUnion<TooltipStringType,
                                  TypeAndTag<NulloptType, TooltipStringType::None>,
                                  TypeAndTag<FunctionRef<String()>, TooltipStringType::Function>,
                                  TypeAndTag<String, TooltipStringType::String>>;

struct BoxConfig {
    Optional<Box> parent {};

    String text {};
    f32 font_size = k_default_font_size; // see k_default_font_size
    f32 wrap_width = k_no_wrap; // see k_no_wrap and k_wrap_to_parent
    FontType font : NumBitsNeededToStore(ToInt(FontType::Count)) {FontType::Body};
    style::Colour text_fill : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_hot : style::k_colour_bits = style::Colour::Text;
    style::Colour text_fill_active : style::k_colour_bits = style::Colour::Text;
    bool32 size_from_text : 1 = false; // sets layout.size for you
    TextAlignX text_align_x : NumBitsNeededToStore(ToInt(TextAlignX::Count)) = TextAlignX::Left;
    TextAlignY text_align_y : NumBitsNeededToStore(ToInt(TextAlignY::Count)) = TextAlignY::Top;
    TextOverflowType text_overflow
        : NumBitsNeededToStore(ToInt(TextOverflowType::Count)) = TextOverflowType::AllowOverflow;
    bool32 capitalize_text : 1 = false;

    BackgroundShape background_shape
        : NumBitsNeededToStore(ToInt(BackgroundShape::Count)) = BackgroundShape::Rectangle;
    style::Colour background_fill : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_hot : style::k_colour_bits = style::Colour::None;
    style::Colour background_fill_active : style::k_colour_bits = style::Colour::None;
    bool32 background_fill_auto_hot_active_overlay : 1 = false;
    bool32 drop_shadow : 1 = false;
    Optional<graphics::TextureHandle> background_tex {};

    style::Colour border : style::k_colour_bits = style::Colour::None;
    style::Colour border_hot : style::k_colour_bits = style::Colour::None;
    style::Colour border_active : style::k_colour_bits = style::Colour::None;
    bool32 border_auto_hot_active_overlay : 1 = false;

    // 4 bits, clockwise from top-left: top-left, top-right, bottom-right, bottom-left, set using 0b0001 etc.
    u32 round_background_corners : 4 = 0;

    TextInputBox text_input_box : NumBitsNeededToStore(ToInt(TextInputBox::Count)) = TextInputBox::None;
    style::Colour text_input_cursor : style::k_colour_bits = style::Colour::Text;
    style::Colour text_input_selection : style::k_colour_bits = style::Colour::Highlight;

    MouseButton activate_on_click_button
        : NumBitsNeededToStore(ToInt(MouseButton::Count)) = MouseButton::Left;
    bool32 activate_on_click_use_double_click : 1 = false;
    ActivationClickEvent activation_click_event
        : NumBitsNeededToStore(ToInt(ActivationClickEvent::Count)) = ActivationClickEvent::None;
    bool32 ignore_double_click : 1 = false;
    bool32 parent_dictates_hot_and_active : 1 = false;
    u8 extra_margin_for_mouse_events = 0;

    layout::ItemOptions layout {};

    TooltipString tooltip = k_nullopt;
};

static bool Tooltip(GuiBoxSystem& builder, imgui::Id id, Rect r, TooltipString tooltip_str) {
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
        popup_r.x = r.x;
        popup_r.y = r.y + r.h;
        popup_r.w = text_size.x + pad_x * 2;
        popup_r.h = text_size.y + pad_y * 2;

        auto const cursor_pos = imgui.frame_input.cursor_pos;

        // Shift the x so that it's centred on the cursor.
        popup_r.x = cursor_pos.x - popup_r.w / 2;

        popup_r.pos = imgui::BestPopupPos(popup_r, r, imgui.frame_input.window_size.ToFloat2(), false);

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

PUBLIC Box DoBox(GuiBoxSystem& builder,
                 BoxConfig const& config,
                 SourceLocation source_location = SourceLocation::Current()) {
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
            ZoneScopedN("Box system: layout boxes");
            auto const box = Box {
                .layout_id =
                    layout::CreateItem(builder.layout, ({
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

                                           if (config.size_from_text) {
                                               if (wrap_width != k_wrap_to_parent) {
                                                   layout.size = font->CalcTextSizeA(font_size,
                                                                                     FLT_MAX,
                                                                                     wrap_width,
                                                                                     config.text);
                                                   ASSERT(layout.size[1] > 0);
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
                dyn::Append(builder.state->word_wrapped_texts,
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
            ZoneScopedN("Box system: handle input and render");
            auto& box = builder.state->boxes[box_index];
            ASSERT(box.source_location == source_location,
                   "GUI has changed between layout and render, see deffered_actions");
            auto const rect =
                builder.imgui.GetRegisteredAndConvertedRect(layout::GetRect(builder.layout, box.layout_id));

            if (!builder.imgui.IsRectVisible(rect)) return box;

            auto const mouse_rect =
                rect.Expanded(builder.imgui.VwToPixels(config.extra_margin_for_mouse_events));

            if (config.activation_click_event != ActivationClickEvent::None ||
                (config.tooltip.tag != TooltipStringType::None && !config.parent_dictates_hot_and_active)) {
                imgui::ButtonFlags button_flags {
                    .left_mouse = !config.activate_on_click_use_double_click &&
                                  config.activate_on_click_button == MouseButton::Left,
                    .right_mouse = config.activate_on_click_button == MouseButton::Right,
                    .middle_mouse = config.activate_on_click_button == MouseButton::Middle,
                    .double_click = config.activate_on_click_use_double_click,
                    .ignore_double_click = config.ignore_double_click,
                    .triggers_on_mouse_down = config.activation_click_event == ActivationClickEvent::Down,
                    .triggers_on_mouse_up = config.activation_click_event == ActivationClickEvent::Up,
                };
                box.imgui_id = builder.imgui.GetID((usize)box_index);
                box.button_fired = builder.imgui.ButtonBehavior(mouse_rect, box.imgui_id, button_flags);
                box.is_active = builder.imgui.IsActive(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
            }
            if (config.text_input_box != TextInputBox::None) {
                box.imgui_id = builder.imgui.GetID((usize)box_index);
                builder.state->last_text_input_result = builder.imgui.TextInput(
                    mouse_rect,
                    box.imgui_id,
                    config.text,
                    config.text_input_box == TextInputBox::MultiLine
                        ? imgui::TextInputFlags {.multiline = true, .multiline_wordwrap_hack = true}
                        : imgui::TextInputFlags {},
                    {.left_mouse = true, .triggers_on_mouse_down = true},
                    false);
                box.is_active = builder.imgui.TextInputHasFocus(box.imgui_id);
                box.is_hot = builder.imgui.IsHot(box.imgui_id);
                box.text_input_result = &builder.state->last_text_input_result;
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
                        c = config.background_fill;
                    else if (is_active)
                        c = config.background_fill_active;
                    else if (is_hot)
                        c = config.background_fill_hot;
                    else
                        c = config.background_fill;
                    c;
                });
                background_fill != style::Colour::None || config.background_fill_auto_hot_active_overlay) {

                auto r = rect;
                // If we normally don't show a background, then we can assume that hot/active colours are
                // exclusively for the mouse so we should use the mouse rectangle.
                if (config.background_fill == style::Colour::None) r = mouse_rect;

                auto const rounding =
                    config.round_background_corners ? builder.imgui.VwToPixels(style::k_button_rounding) : 0;

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

            if (config.background_tex)
                builder.imgui.graphics->AddImage(*config.background_tex, rect.Min(), rect.Max());

            if (auto const border = ({
                    style::Colour c {};
                    if (config.border_auto_hot_active_overlay)
                        c = config.border;
                    else if (is_active)
                        c = config.border_active;
                    else if (is_hot)
                        c = config.border_hot;
                    else
                        c = config.border;
                    c;
                });
                border != style::Colour::None || config.border_auto_hot_active_overlay) {

                auto r = rect;
                if (config.border == style::Colour::None) r = mouse_rect;

                auto const rounding =
                    config.round_background_corners ? builder.imgui.VwToPixels(style::k_button_rounding) : 0;

                u32 col_u32 = style::Col(border);
                if (config.border_auto_hot_active_overlay) {
                    if (is_hot)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_hot_white_overlay)
                                          : style::k_auto_hot_white_overlay;
                    else if (is_active)
                        col_u32 = col_u32 ? style::BlendColours(col_u32, style::k_auto_active_white_overlay)
                                          : style::k_auto_active_white_overlay;
                }

                builder.imgui.graphics->AddRect(r, col_u32, rounding, config.round_background_corners);
            }

            if (config.text.size && config.text_input_box == TextInputBox::None) {
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
                                                style::Col(is_hot      ? config.text_fill_hot
                                                           : is_active ? config.text_fill_active
                                                                       : config.text_fill),
                                                text,
                                                wrap_width == k_wrap_to_parent ? rect.w : wrap_width);
            }

            if (config.text_input_box != TextInputBox::None) {
                auto input_result = box.text_input_result;
                ASSERT(input_result);

                if (input_result->HasSelection()) {
                    imgui::TextInputResult::SelectionIterator it {*builder.imgui.graphics->context};
                    while (auto const r = input_result->NextSelectionRect(it))
                        builder.imgui.graphics->AddRectFilled(*r, style::Col(config.text_input_selection));
                }

                if (input_result->show_cursor) {
                    auto cursor_r = input_result->GetCursorRect();
                    builder.imgui.graphics->AddRectFilled(cursor_r.Min(),
                                                          cursor_r.Max(),
                                                          style::Col(config.text_input_cursor));
                }

                builder.imgui.graphics->AddText(input_result->GetTextPos(),
                                                style::Col(config.text_fill),
                                                input_result->text);
            }

            if (config.tooltip.tag != TooltipStringType::None)
                Tooltip(builder,
                        config.parent_dictates_hot_and_active ? config.parent->imgui_id : box.imgui_id,
                        rect,
                        config.tooltip);

            return box;
        }
    }

    return {};
}

PUBLIC bool AdditionalClickBehaviour(GuiBoxSystem& box_system,
                                     Box const& box,
                                     imgui::ButtonFlags const& config,
                                     Rect* out_item_rect = nullptr) {
    if (box_system.state->pass == BoxSystemCurrentPanelState::Pass::LayoutBoxes) return false;

    auto const item_r =
        box_system.imgui.WindowRectToScreenRect(layout::GetRect(box_system.layout, box.layout_id));

    auto const result = imgui::ClickCheck(config, box_system.imgui.frame_input, &item_r);
    if (result && out_item_rect) *out_item_rect = item_r;
    return result;
}

// =================================================================================================================
// Helpers
PUBLIC Rect CentredRect(Rect container, f32x2 size) {
    return {
        .pos = container.pos + ((container.size - size) / 2),
        .size = size,
    };
}
