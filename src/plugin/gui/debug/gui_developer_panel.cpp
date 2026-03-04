// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/debug/gui_developer_panel.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

#include "engine/engine.hpp"
#include "gui/debug/gui_developer_panel.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"

// This code works, but should not be the model for new code. It has some messy design:
// - Inflexible incrementing y-position layout rather than something more comprehensive such as the
//   GuiBuilder.
// - Inconsistent hard-coded style.
// - Very little use of scalable window-width sizes, instead using pixels.

bool DoBasicTextButton(imgui::Context& imgui, imgui::ButtonConfig cfg, Rect r, imgui::Id id, String str) {
    r = imgui.RegisterAndConvertRect(r);
    bool const clicked = imgui.ButtonBehaviour(r, id, cfg);

    u32 col = 0xffd5d5d5;
    if (imgui.IsHot(id)) col = 0xfff0f0f0;
    if (imgui.IsActive(id, cfg.mouse_button)) col = 0xff808080;
    imgui.draw_list->AddRectFilled(r.Min(), r.Max(), col);

    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    auto const pad = (f32)GuiIo().in.window_size.width / 200.0f;
    imgui.draw_list->AddText(f32x2 {r.x + pad, r.y + (r.h / 2 - font_size / 2)}, 0xff000000, str);

    return clicked;
}

void DoBasicWhiteText(imgui::Context& imgui, Rect r, String str) {
    r = imgui.RegisterAndConvertRect(r);
    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    f32x2 pos;
    pos.x = (f32)(int)r.x;
    pos.y = r.y + ((r.h / 2) - (font_size / 2));
    pos.y = (f32)(int)pos.y;
    imgui.draw_list->AddText(pos, 0xffffffff, str);
}

using DevGuiTextInputBuffer = DynamicArrayBounded<char, 128>;

f32 const k_item_h = 19;

static void DrawDevGuiTextButton(imgui::Context const& imgui, Rect r, imgui::Id id, String str, bool state) {
    imgui.draw_list->AddRectFilled(r, ({
                                       u32 c = 0xffd5d5d5;
                                       if (imgui.IsHot(id)) c = 0xfff0f0f0;
                                       if (imgui.IsActive(id, MouseButton::Left)) c = 0xff808080;
                                       if (state) c = 0xff808080;
                                       c;
                                   }));

    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    imgui.draw_list->AddText(f32x2 {r.x + WwToPixels(4.0f), r.y + (r.h / 2 - font_size / 2)},
                             0xff000000,
                             str);
}

static void
DrawDevGuiPopupTextButton(imgui::Context const& imgui, Rect r, imgui::Id id, String str, bool popup_open) {
    imgui.draw_list->AddRectFilled(r, ({
                                       u32 c = 0xffd5d5d5;
                                       if (imgui.IsHot(id)) c = 0xfff0f0f0;
                                       if (imgui.IsActive(id, MouseButton::Left) || popup_open)
                                           c = 0xff808080;
                                       c;
                                   }));

    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    imgui.draw_list->AddText(f32x2 {r.x + 4, r.y + ((r.h - font_size) / 2)}, 0xff000000, str);

    imgui.draw_list->AddTriangleFilled({r.Right() - 14, r.y + 4},
                                       {r.Right() - 4, r.y + (r.h / 2)},
                                       {r.Right() - 14, r.y + r.h - 4},
                                       0xff000000);
}

static void DevGuiReset(DeveloperPanel& g) { g.y_pos = 0; }

static Rect DevGuiGetFullR(DeveloperPanel& g) {
    return Rect {.x = 0, .y = g.y_pos, .w = g.imgui.CurrentVpWidth(), .h = k_item_h - 1};
}

static Rect DevGuiGetLeftR(DeveloperPanel& g) {
    return Rect {.x = 0, .y = g.y_pos, .w = g.imgui.CurrentVpWidth() / 2, .h = k_item_h - 1};
}

static Rect DevGuiGetRightR(DeveloperPanel& g) {
    auto w = g.imgui.CurrentVpWidth() / 2;
    return Rect {.x = w, .y = g.y_pos, .w = w, .h = k_item_h - 1};
}

static void DevGuiIncrementPos(DeveloperPanel& g, f32 size = 0) { g.y_pos += (size != 0) ? size : k_item_h; }

template <typename... Args>
static void DevGuiText(DeveloperPanel& g, String format, Args const&... args) {
    DoBasicWhiteText(g.imgui, DevGuiGetFullR(g), fmt::Format(g.imgui.scratch_arena, format, args...));
    DevGuiIncrementPos(g);
}

static void DevGuiHeading(DeveloperPanel& g, String text) {
    if (g.y_pos != 0) // don't do this for the first item
        g.y_pos += k_item_h;

    auto r = DevGuiGetFullR(g);
    DoBasicWhiteText(g.imgui, r, text);

    g.y_pos += k_item_h;
    auto const line_y = g.imgui.RegisterAndConvertRect({.xywh {0, g.y_pos, 0, 0}}).y;
    auto const line_x1 = g.imgui.RegisterAndConvertRect({.xywh {0, 0, 0, 0}}).x;
    auto const line_x2 = g.imgui.RegisterAndConvertRect({.xywh {g.imgui.CurrentVpWidth(), 0, 0, 0}}).x;
    g.imgui.draw_list->AddLine({line_x1, line_y}, {line_x2, line_y}, 0x60ffffff);

    g.y_pos += 2;
}

static void DevGuiLabel(DeveloperPanel& g, Rect r, String text, TextJustification just) {
    g.imgui.draw_list->AddTextInRect(
        g.imgui.RegisterAndConvertRect(r.CutRight(4)),
        0xffffffff,
        text,
        {.justification = just, .overflow_type = TextOverflowType::ShowDotsOnRight});
}

static void DevGuiLabel(DeveloperPanel& g, String text) {
    DevGuiLabel(g, DevGuiGetLeftR(g), text, TextJustification::CentredRight);
}

static bool DevGuiButton(DeveloperPanel& g, String button_text, String label) {
    DoBasicWhiteText(g.imgui, DevGuiGetLeftR(g), label);

    auto const r = g.imgui.RegisterAndConvertRect(DevGuiGetRightR(g));
    auto const id = g.imgui.MakeId(button_text);
    auto const clicked = g.imgui.ButtonBehaviour(r, id, imgui::ButtonConfig {});
    DrawDevGuiTextButton(g.imgui, r, id, button_text, false);

    DevGuiIncrementPos(g);

    return clicked;
}

static void
DrawDevGuiTextInput(imgui::Context const& imgui, imgui::TextInputResult const& result, imgui::Id id, Rect r) {
    imgui.draw_list->AddRectFilled(r, ({
                                       u32 c = 0xffffffff;
                                       if (imgui.IsHot(id) && !imgui.TextInputHasFocus(id)) c = 0xffe5e5e5;
                                       c;
                                   }));

    if (result.HasSelection()) {
        imgui::TextInputResult::SelectionIterator it {imgui};
        while (auto const sel_r = result.NextSelectionRect(it))
            imgui.draw_list->AddRectFilled(*sel_r, 0xffff0000);
    }

    if (result.cursor_rect) imgui.draw_list->AddRectFilled(*result.cursor_rect, 0xff000000);

    imgui.draw_list->AddText(result.text_pos, 0xff000000, result.text);
}

static void DevGuiTextInput(DeveloperPanel& g, String label, DevGuiTextInputBuffer& buf) {
    DevGuiLabel(g, label);

    {
        auto const r = g.imgui.RegisterAndConvertRect(DevGuiGetRightR(g));
        auto const id = g.imgui.MakeId(label);
        auto const result = g.imgui.TextInputBehaviour({
            .rect_in_window_coords = r,
            .id = id,
            .text = buf,
        });
        if (result.buffer_changed) dyn::Assign(buf, result.text);
        DrawDevGuiTextInput(g.imgui, result, id, r);
    }

    DevGuiIncrementPos(g);
}

static void DrawDevGuiViewportBackground(imgui::Context const& imgui) {
    auto const r = imgui.curr_viewport->unpadded_bounds;
    imgui.draw_list->AddRectFilled(r, 0xff202020);
}

static void DrawDevGuiScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    for (auto const b : bars) {
        if (!b) continue;
        imgui.draw_list->AddRectFilled(b->strip, 0xff404040);
        u32 col = 0xffe5e5e5;
        if (imgui.IsHot(b->id))
            col = 0xffffffff;
        else if (imgui.IsActive(b->id, MouseButton::Left))
            col = 0xffb5b5b5;
        imgui.draw_list->AddRectFilled(b->handle, col);
    }
}

struct FloatDraggerArgs {
    Rect viewport_r;
    imgui::Id id;
    String format_string;
    f32 min {};
    f32 max {};
    f32& value;
    f32 default_value {};
    imgui::ButtonConfig text_input_button_cfg = {
        .mouse_button = MouseButton::Left,
        .event = MouseButtonEvent::DoubleClick,
    };
    imgui::TextInputConfig text_input_cfg = {
        .chars_decimal = true,
        .tab_focuses_next_input = true,
        .escape_unfocuses = true,
        .select_all_when_opening = true,
    };
    imgui::SliderConfig slider_cfg = imgui::SliderConfig {};
};

static bool DoDevGuiFloatDragger(DeveloperPanel& g, FloatDraggerArgs const& args) {
    auto const window_r = g.imgui.RegisterAndConvertRect(args.viewport_r);

    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};

    auto const original_text = fmt::Format(allocator, args.format_string, args.value);

    auto result = g.imgui.DraggerBehaviour({
        .rect_in_window_coords = window_r,
        .id = args.id,
        .text = original_text,
        .min = args.min,
        .max = args.max,
        .value = args.value,
        .default_value = args.default_value,
        .text_input_button_cfg = args.text_input_button_cfg,
        .text_input_cfg = args.text_input_cfg,
        .slider_cfg = args.slider_cfg,
    });

    bool changed = false;

    if (result.new_string_value) {
        if (auto const v = ParseFloat(*result.new_string_value, nullptr)) {
            args.value = Clamp((f32)*v, args.min, args.max);
            changed = true;
        }
    }
    if (result.value_changed) changed = true;

    if (result.text_input_result) {
        DrawDevGuiTextInput(g.imgui, *result.text_input_result, args.id, window_r);
    } else {
        auto const text = changed ? fmt::Format(allocator, args.format_string, args.value) : original_text;
        g.imgui.draw_list->AddText(
            imgui::TextInputTextPos(text, window_r, args.text_input_cfg, g.imgui.draw_list->fonts),
            0xffffffff,
            text);
    }

    return changed;
}

static bool DevGuiMenuItems(DeveloperPanel& g, Span<String const> items, int& current) {
    auto const w = g.imgui.draw_list->fonts.Current()->LargestStringWidth(4, items);

    int clicked = -1;
    for (auto const i : Range(items.size)) {
        bool selected = (int)i == current;
        if (({
                auto const r = g.imgui.RegisterAndConvertRect({.xywh = {0, k_item_h * (f32)i, w, k_item_h}});
                auto const id = g.imgui.MakeId(items[i]);
                auto const changed = g.imgui.ToggleButtonBehaviour(r,
                                                                   id,
                                                                   {
                                                                       .mouse_button = MouseButton::Left,
                                                                       .event = MouseButtonEvent::Up,
                                                                       .closes_popup_or_modal = true,
                                                                   },
                                                                   selected);
                DrawDevGuiTextButton(g.imgui, r, id, items[i], selected);
                changed;
            }))
            clicked = (int)i;
    }
    if (clicked != -1 && current != clicked) {
        current = clicked;
        return true;
    }
    return false;
}

static imgui::ViewportConfig DevGuiPopupConfig() {
    return {
        .mode = imgui::ViewportMode::PopupMenu,
        .positioning = imgui::ViewportPositioning::AutoPosition,
        .draw_background = DrawDevGuiViewportBackground,
        .draw_scrollbars = DrawDevGuiScrollbars,
        .padding = {.lrtb = 4},
        .scrollbar_padding = 4,
        .scrollbar_width = 8,
        .auto_size = true,
    };
}

static bool DevGuiMenu(DeveloperPanel& g, Rect r, Span<String const> items, int& current) {
    auto curr_text = items[(usize)current];
    bool result = false;

    auto const btn_r = g.imgui.RegisterAndConvertRect(r);
    auto const btn_id = g.imgui.MakeId((uintptr)items.data);
    auto const pop_id = btn_id + 1;
    auto const o = g.imgui.PopupMenuButtonBehaviour(btn_r, btn_id, pop_id, imgui::ButtonConfig {});
    DrawDevGuiPopupTextButton(g.imgui, btn_r, btn_id, curr_text, o.show_as_active);

    if (g.imgui.IsPopupMenuOpen(pop_id)) {
        g.imgui.BeginViewport(DevGuiPopupConfig(), pop_id, btn_r);
        DEFER { g.imgui.EndViewport(); };

        result = DevGuiMenuItems(g, items, current) || result;
    }
    return result;
}

constexpr String const k_ui_col_map_names[ToInt(UiColMap::Count)] = {
#define X(cat, n, col_id, alpha, dark_mode) #n,
#include COLOUR_MAP_DEF_FILENAME
#undef X
};

constexpr String k_ui_col_map_categories[ToInt(UiColMap::Count)] = {
#define X(cat, n, col_id, alpha, dark_mode) cat,
#include COLOUR_MAP_DEF_FILENAME
#undef X
};

static String UiStyleFilepath(Allocator& a, String filename) {
    return path::Join(a, Array {*path::Directory(*path::Directory(__FILE__)), "live_edit_defs", filename});
}

static void WriteHeader(Writer writer) {
    // REUSE-IgnoreStart
    auto _ = fmt::FormatToWriter(
        writer,
        "// Copyright 2018-2026 Sam Windell\n// SPDX-License-Identifier: GPL-3.0-or-later\n\n");
    // REUSE-IgnoreEnd
}

static void WriteColourMapFile(LiveEditGui const& gui) {
    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, COLOUR_MAP_DEF_FILENAME), FileMode::Write());
    if (outcome.HasError()) {
        LogError(ModuleName::Gui, "{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const i : Range(ToInt(UiColMap::Count))) {
        auto const& v = gui.ui_col_map[i];
        auto name = k_ui_col_map_names[i];
        auto cat = k_ui_col_map_categories[i];
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "X(\"{}\", {}, {}, {}, {})\n",
                                     cat,
                                     name,
                                     EnumToString(v.col.c),
                                     v.col.alpha,
                                     v.col.dark_mode ? "true" : "false");
        if (o.HasError())
            LogError(ModuleName::Gui,
                     "could not write to file {} for reason {}",
                     COLOUR_MAP_DEF_FILENAME,
                     o.Error());
    }
}

static void LiveEditColourMapMenus(DeveloperPanel& g, String search) {
    auto& live_gui = g_live_edit_values;
    DevGuiHeading(g, "Colour Mapping");

    static DynamicArrayBounded<String, ToInt(UiColMap::Count)> categories {};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiColMap::Count)))
            dyn::AppendIfNotAlreadyThere(categories, k_ui_col_map_categories[i]);

    for (auto const cat : categories) {
        g.imgui.PushId(cat);
        DEFER { g.imgui.PopId(); };

        bool contains_values = search.size && ContainsCaseInsensitiveAscii(cat, search);
        if (!contains_values) {
            for (auto const i : Range(ToInt(UiColMap::Count))) {
                if (k_ui_col_map_categories[i] != cat) continue;
                if (!ContainsCaseInsensitiveAscii(k_ui_col_map_names[i], search)) continue;
                contains_values = true;
                break;
            }
        }

        if (!contains_values) continue;

        DevGuiHeading(g, cat);

        for (auto const i : Range(ToInt(UiColMap::Count))) {
            if (k_ui_col_map_categories[i] != cat) continue;

            auto const name = k_ui_col_map_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;

            g.imgui.PushId((u64)i);
            DEFER { g.imgui.PopId(); };

            auto& col_map = live_gui.ui_col_map[i];

            f32 const pad = 1.0f;
            f32 const label_w = g.imgui.CurrentVpWidth() * 0.35f;
            f32 const col_preview_size = k_item_h - pad;
            f32 const alpha_w = g.imgui.CurrentVpWidth() * 0.1f;
            f32 const dark_w = k_item_h * 3;
            f32 const menu_w =
                g.imgui.CurrentVpWidth() - label_w - col_preview_size - alpha_w - dark_w - (pad * 4);

            Rect const label_r = {.xywh {0, g.y_pos, label_w, k_item_h - 1}};
            f32 x = label_w;
            Rect col_preview_r = {.xywh {x, g.y_pos, col_preview_size, k_item_h}};
            x += col_preview_size + pad;
            Rect const menu_r = {.xywh {x, g.y_pos, menu_w, k_item_h - 1}};
            x += menu_w + pad;
            Rect const alpha_r = {.xywh {x, g.y_pos, alpha_w, k_item_h - 1}};
            x += alpha_w + pad;
            Rect const dark_r = {.xywh {x, g.y_pos, dark_w, k_item_h - 1}};

            // Colour preview
            {
                col_preview_r = g.imgui.RegisterAndConvertRect(col_preview_r);
                g.imgui.draw_list->AddRectFilled(col_preview_r, ToU32(col_map.col));
            }

            // Col::Id dropdown
            int col_id_index = ToInt(col_map.col.c);
            constexpr auto k_col_id_names = []() {
                Array<String, ToInt(Col::Id::Count)> names {};
                for (auto e : EnumIterator<Col::Id>())
                    names[ToInt(e)] = EnumToString(e);
                return names;
            }();
            bool changed = DevGuiMenu(g, menu_r, k_col_id_names, col_id_index);
            if (changed) col_map.col.c = (Col::Id)col_id_index;

            // Alpha dragger
            {
                auto alpha_f = (f32)col_map.col.alpha;
                changed |= DoDevGuiFloatDragger(g,
                                                {
                                                    .viewport_r = alpha_r,
                                                    .id = g.imgui.MakeId("alpha"),
                                                    .format_string = "{.0}",
                                                    .min = 0,
                                                    .max = 255,
                                                    .value = alpha_f,
                                                    .default_value = 255,
                                                    .slider_cfg = ({
                                                        auto f = imgui::SliderConfig {};
                                                        f.sensitivity = 4;
                                                        f;
                                                    }),
                                                });
                if (changed) col_map.col.alpha = (u8)alpha_f;
            }

            // Dark mode toggle
            {
                auto const r = g.imgui.RegisterAndConvertRect(dark_r);
                auto const id = g.imgui.MakeId("dark");
                bool dark = col_map.col.dark_mode;
                if (g.imgui.ToggleButtonBehaviour(r, id, imgui::ButtonConfig {}, dark)) {
                    col_map.col.dark_mode = !col_map.col.dark_mode;
                    changed = true;
                }
                DrawDevGuiTextButton(g.imgui, r, id, "DarkMode", col_map.col.dark_mode);
            }

            DevGuiLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) WriteColourMapFile(live_gui);

            DevGuiIncrementPos(g);
        }
        DevGuiIncrementPos(g);
    }
}

static imgui::ViewportConfig DevGuiViewport() {
    return {
        .draw_background = DrawDevGuiViewportBackground,
        .draw_scrollbars = DrawDevGuiScrollbars,
        .padding = {.lrtb = 4},
        .scrollbar_padding = 4,
        .scrollbar_width = 8,
    };
}

static void DoImGuiInspector(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "text-viewport");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    static bool debug_ids = true;
    static bool debug_popup = true;
    static bool debug_viewports = true;

    if (DevGuiButton(g, "Toggle Registered Widget Overlay", "Toggle Overlay"))
        g.imgui.debug_show_register_widget_overlay = !g.imgui.debug_show_register_widget_overlay;

    static bool debug_general = true;

    if (DevGuiButton(g, debug_general ? "Hide General" : "Show General", "General"))
        debug_general = !debug_general;

    if (debug_general) {
        GuiIo().out.wants.text_input = true;

        auto const& in = GuiIo().in;
        DevGuiText(g, "Update: {}", in.update_count);
        DevGuiText(g, "Key shift: {}", in.modifiers.Get(ModifierKey::Shift));
        DevGuiText(g, "Key ctrl: {}", in.modifiers.Get(ModifierKey::Ctrl));
        DevGuiText(g, "Key modifier: {}", in.modifiers.Get(ModifierKey::Modifier));
        DevGuiText(g, "Key alt: {}", in.modifiers.Get(ModifierKey::Alt));
        DevGuiText(g, "Time: {}", in.current_time.Raw());
        DevGuiText(g, "Window size: {}, {}", in.window_size.width, in.window_size.height);
        DevGuiText(g, "Widgets: {}", GuiIo().out.mouse_tracked_rects.size);

        DevGuiIncrementPos(g, k_item_h);

        DevGuiText(g, "Timers:");
        for (auto [id, time, hash] : GuiIo().out.timed_wakeups)
            DevGuiText(g, "ID: {x}, Time: {}", id, time.Raw());
    }

    if (DevGuiButton(g, debug_ids ? "Hide IDs" : "Show IDs", "IDs")) debug_ids = !debug_ids;

    if (debug_ids) {
        DevGuiText(g, "Active ID: {}", g.imgui.GetActive());
        DevGuiText(g, "Hot ID: {}", g.imgui.GetHot());
        DevGuiText(g, "Hovered ID: {}", g.imgui.GetHovered());
        DevGuiText(g, "TextInput ID: {}", g.imgui.GetTextInput());
    }

    if (DevGuiButton(g, debug_popup ? "Hide Popups" : "Show Popups", "Popups")) debug_popup = !debug_popup;

    if (debug_popup) DevGuiText(g, "Persistent popups: {}", g.imgui.open_popups.size);

    if (DevGuiButton(g, debug_viewports ? "Hide Viewports" : "Show Viewports", "Viewports"))
        debug_viewports = !debug_viewports;

    auto const hov = g.imgui.hovered_viewport;
    if (debug_viewports && hov) {
        DevGuiText(g, "Hovered ID: {}", hov->id);
        DevGuiText(g, "Hovered name: {}", (String)hov->debug_name);
        DevGuiText(g, "Hovered root: {}", hov->root_viewport->id);

        DevGuiText(g, "Hovered padding lr: {}, tb: {}", hov->cfg.padding.lr, hov->cfg.padding.tb);

        DevGuiText(g, "Hovered creator ID: {}", hov->creator_of_this_popup_menu);

        DevGuiText(g,
                   "Hovered size: {.1} {.1} {.1} {.1}",
                   hov->unpadded_bounds.x,
                   hov->unpadded_bounds.y,
                   hov->unpadded_bounds.w,
                   hov->unpadded_bounds.h);
        DevGuiText(g,
                   "Hovered scrollbar padding: {}, width: {}",
                   hov->cfg.scrollbar_padding,
                   hov->cfg.scrollbar_width);

        {
            DynamicArray<char> buf {g.imgui.scratch_arena};
            auto const& f = hov->cfg;

            switch (f.mode) {
                case imgui::ViewportMode::Contained: break;
                case imgui::ViewportMode::Floating: dyn::AppendSpan(buf, "floating "); break;
                case imgui::ViewportMode::Modal: dyn::AppendSpan(buf, "modal "); break;
                case imgui::ViewportMode::PopupMenu: dyn::AppendSpan(buf, "popup "); break;
            }
            switch (f.positioning) {
                case imgui::ViewportPositioning::ParentRelative: break;
                case imgui::ViewportPositioning::WindowAbsolute:
                    dyn::AppendSpan(buf, "window_absolute ");
                    break;
                case imgui::ViewportPositioning::AutoPosition: dyn::AppendSpan(buf, "auto_position "); break;
            }
            if (f.auto_size[0]) dyn::AppendSpan(buf, "auto_width ");
            if (f.auto_size[1]) dyn::AppendSpan(buf, "auto_height ");
            for (auto const i : Range(2uz)) {
                switch (f.scrollbar_visibility[i]) {
                    case imgui::ViewportScrollbarVisibility::Auto:
                        fmt::Append(buf, "scrollbar[{}]=auto", i);
                        break;
                    case imgui::ViewportScrollbarVisibility::Never:
                        fmt::Append(buf, "scrollbar[{}]=never", i);
                        break;
                    case imgui::ViewportScrollbarVisibility::Always:
                        fmt::Append(buf, "scrollbar[{}]=always", i);
                        break;
                }
            }

            if (f.scrollbar_inside_padding) dyn::AppendSpan(buf, "scrollbar_inside_padding ");

            DevGuiText(g, "Hovered flags: {}", (String)buf);
        }
    }
}

static void DoAudioDebugPanel(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "inspector-audio");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    DevGuiText(g,
               "Voices: {}",
               g.engine.processor.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed));
    DevGuiText(g, "Master Audio Processing: {}", g.engine.processor.fx_need_another_frame_of_processing);

    DevGuiText(g, "State diff: {}", g.engine.state_change_description);

    // NOTE: not really thread-safe to access engine.processor but it's fine for this debug UI.
    auto const max_ms = (f32)g.engine.processor.audio_processing_context.process_block_size_max /
                        g.engine.processor.audio_processing_context.sample_rate * 1000.0f;
    DevGuiText(g,
               "FS: {} Block: {} Max MS Allowed: {.3}",
               g.engine.processor.audio_processing_context.sample_rate,
               g.engine.processor.audio_processing_context.process_block_size_max,
               max_ms);
}

static void DoLiveEditColourMapEditor(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "colour-map-edit");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    static DevGuiTextInputBuffer search;
    DevGuiTextInput(g, "Search:", search);

    LiveEditColourMapMenus(g, search);
}

static bool g_show_dev_gui = false;
static bool g_show_dev_gui_on_left = true;

static void DoCommandPanel(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "commands");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    if (DevGuiButton(g, "Show Dev GUI", "Show : F1")) {
        g_show_dev_gui = !g_show_dev_gui;
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
    if (DevGuiButton(g, "Dev GUI Left", "Dev GUI position left: F2")) {
        g_show_dev_gui_on_left = !g_show_dev_gui_on_left;
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }
}

void DoDeveloperPanel(DeveloperPanel& g) {
    if constexpr (PRODUCTION_BUILD) return;

    GuiIo().out.wants.keyboard_keys.Set(ToInt(KeyCode::F1));
    GuiIo().out.wants.keyboard_keys.Set(ToInt(KeyCode::F2));

    if (GuiIo().in.Key(KeyCode::F1).presses.size) {
        g_show_dev_gui = !g_show_dev_gui;
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
    }

    if (g_show_dev_gui) {
        if (GuiIo().in.Key(KeyCode::F2).presses.size) {
            g_show_dev_gui_on_left = !g_show_dev_gui_on_left;
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }

        auto const half_w = (f32)(int)(g.imgui.CurrentVpWidth() / 2);
        g.imgui.BeginViewport(
            {
                .mode = imgui::ViewportMode::Floating,
                .positioning = imgui::ViewportPositioning::WindowAbsolute,
                .scrollbar_visibility = imgui::ViewportScrollbarVisibility::Never,
                .z_order = 200,
            },
            (g_show_dev_gui_on_left) ? Rect {.xywh {half_w + 1, 0, half_w - 1, g.imgui.CurrentVpHeight()}}
                                     : Rect {.xywh {0, 0, half_w - 1, g.imgui.CurrentVpHeight()}},
            "whole-dev-gui");
        DEFER { g.imgui.EndViewport(); };

        static String const tab_text[] = {
            "Commands",
            "Audio",
            "ColMap",
            "UI Inspect",
        };
        static auto const num_tabs = ArraySize(tab_text);
        static usize selected_tab = 0;
        auto const tab_h = g.imgui.draw_list->fonts.Current()->font_size * 2;
        for (auto const i : Range(num_tabs)) {
            auto const third = g.imgui.CurrentVpWidth() / (f32)num_tabs;
            bool v = i == selected_tab;
            auto const id = g.imgui.MakeId(tab_text[i]);
            if (({
                    auto const r = g.imgui.RegisterAndConvertRect({.xywh {(f32)i * third, 0, third, tab_h}});
                    auto const changed = g.imgui.ToggleButtonBehaviour(r, id, imgui::ButtonConfig {}, v);
                    DrawDevGuiTextButton(g.imgui, r, id, tab_text[i], v);
                    changed;
                })) {
                selected_tab = i;
            }
        }
        Rect const selected_r = {
            .xywh {0, tab_h, g.imgui.CurrentVpWidth(), g.imgui.CurrentVpHeight() - tab_h}};
        switch (selected_tab) {
            case 0: DoCommandPanel(g, selected_r); break;
            case 1: DoAudioDebugPanel(g, selected_r); break;
            case 2: DoLiveEditColourMapEditor(g, selected_r); break;
            case 3: DoImGuiInspector(g, selected_r); break;
            default: PanicIfReached();
        }
    }
}
