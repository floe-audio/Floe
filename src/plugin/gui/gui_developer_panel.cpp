// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_developer_panel.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

#include "engine/engine.hpp"
#include "gui_developer_panel.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "old/gui_widget_helpers.hpp"

// This code works, but should not be the model for new code. It has some messy design:
// - Inflexible incrementing y-position layout rather than something more comprehensive such as the
//   GuiBuilder.
// - Inconsistent hard-coded style.
// - Very little use of scalable window-width sizes, instead using pixels.

using DevGuiTextInputBuffer = DynamicArrayBounded<char, 128>;

f32 const k_item_h = 19;

static void DrawDevGuiTextButton(imgui::Context const& imgui, Rect r, imgui::Id id, String str, bool state) {
    imgui.draw_list->AddRectFilled(r, ({
                                       u32 c = 0xffd5d5d5;
                                       if (imgui.IsHot(id)) c = 0xfff0f0f0;
                                       if (imgui.IsActive(id)) c = 0xff808080;
                                       if (state) c = 0xff808080;
                                       c;
                                   }));

    auto const font_size = imgui.draw_list->fonts.Current()->font_size;
    imgui.draw_list->AddText(f32x2 {r.x + GuiIo().WwToPixels(4.0f), r.y + (r.h / 2 - font_size / 2)},
                             0xff000000,
                             str);
}

static void
DrawDevGuiPopupTextButton(imgui::Context const& imgui, Rect r, imgui::Id id, String str, bool popup_open) {
    imgui.draw_list->AddRectFilled(r, ({
                                       u32 c = 0xffd5d5d5;
                                       if (imgui.IsHot(id)) c = 0xfff0f0f0;
                                       if (imgui.IsActive(id) || popup_open) c = 0xff808080;
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
    auto back_r = r;
    back_r = g.imgui.RegisterAndConvertRect(back_r);
    g.imgui.draw_list->AddRectFilled(back_r, 0x50ffffff);
    DoBasicWhiteText(g.imgui, r, text);

    g.y_pos += k_item_h * 1.1f;
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
        else if (imgui.IsActive(b->id))
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

constexpr String k_ui_sizes_categories[ToInt(UiSizeId::Count)] = {
#define GUI_SIZE(cat, n, v) #cat,
#include SIZES_DEF_FILENAME
#undef GUI_SIZE
};

constexpr String const k_ui_col_map_names[ToInt(UiColMap::Count)] = {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) #n,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
};

constexpr String k_ui_col_map_categories[ToInt(UiColMap::Count)] = {
#define GUI_COL_MAP(cat, n, v, high_contrast_col) #cat,
#include COLOUR_MAP_DEF_FILENAME
#undef GUI_COL_MAP
};

static String UiStyleFilepath(Allocator& a, String filename) {
    return path::Join(a, Array {path::Directory(__FILE__).Value(), "live_edit_defs", filename});
}

static void WriteHeader(Writer writer) {
    // REUSE-IgnoreStart
    auto _ = fmt::FormatToWriter(
        writer,
        "// Copyright 2018-2026 Sam Windell\n// SPDX-License-Identifier: GPL-3.0-or-later\n\n");
    // REUSE-IgnoreEnd
}

static void WriteColoursFile(LiveEditGui const& gui) {
    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, COLOURS_DEF_FILENAME), FileMode::Write());
    if (outcome.HasError()) {
        LogError(ModuleName::Gui, "{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const& c : gui.ui_cols) {
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "GUI_COL(\"{}\", 0x{08x}, \"{}\", {.2}f, {.2}f)\n",
                                     String(c.name),
                                     c.col,
                                     String(c.based_on),
                                     c.with_brightness,
                                     c.with_alpha);
        if (o.HasError())
            LogError(ModuleName::Gui,
                     "could not write to file {} for reasion {}",
                     COLOURS_DEF_FILENAME,
                     o.Error());
    }
}

static void WriteSizesFile(LiveEditGui const& gui) {
    ArenaAllocator scratch_arena {PageAllocator::Instance()};

    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, SIZES_DEF_FILENAME), FileMode::Write());
    if (outcome.HasError()) {
        LogError(ModuleName::Gui, "{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const i : Range(ToInt(UiSizeId::Count))) {
        auto const sz = gui.ui_sizes[i];
        String const name = gui.ui_sizes_names[i];
        auto cat = k_ui_sizes_categories[i];
        auto o = fmt::FormatToWriter(outcome.Value().Writer(), "GUI_SIZE({}, {}, {.6}f)\n", cat, name, sz);
        if (o.HasError())
            LogError(ModuleName::Gui,
                     "could not write to file {} for reason {}",
                     SIZES_DEF_FILENAME,
                     o.Error());
    }
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
                                     "GUI_COL_MAP({}, {}, \"{}\", \"{}\")\n",
                                     cat,
                                     name,
                                     String(v.colour),
                                     String(v.high_contrast_colour));
        if (o.HasError())
            LogError(ModuleName::Gui,
                     "could not write to file {} for reason {}",
                     COLOUR_MAP_DEF_FILENAME,
                     o.Error());
    }
}

static void LiveEditSliders(DeveloperPanel& g, String search) {
    auto& live_gui = g_live_edit_values;
    DevGuiHeading(g, "Sizes");

    static DynamicArrayBounded<String, ToInt(UiSizeId::Count)> categories {};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiSizeId::Count)))
            dyn::AppendIfNotAlreadyThere(categories, k_ui_sizes_categories[i]);

    for (auto cat : categories) {
        g.imgui.PushId(cat);
        DEFER { g.imgui.PopId(); };

        bool contains_values = search.size && ContainsCaseInsensitiveAscii(cat, search);
        if (!contains_values) {
            for (auto const i : Range(ToInt(UiSizeId::Count))) {
                if (k_ui_sizes_categories[i] != cat) continue;
                if (!ContainsCaseInsensitiveAscii(live_gui.ui_sizes_names[i], search)) continue;
                contains_values = true;
                break;
            }
        }

        if (!contains_values) continue;

        DevGuiHeading(g, cat);

        for (auto const i : Range(ToInt(UiSizeId::Count))) {
            if (k_ui_sizes_categories[i] != cat) continue;
            auto name = live_gui.ui_sizes_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;

            if (DoDevGuiFloatDragger(g,
                                     {
                                         .viewport_r = DevGuiGetRightR(g),
                                         .id = g.imgui.MakeId(name),
                                         .format_string = "{.1}",
                                         .min = 0,
                                         .max = 1500,
                                         .value = live_gui.ui_sizes[i],
                                         .default_value = 0,
                                         .slider_cfg = ({
                                             auto f = imgui::SliderConfig {};
                                             f.sensitivity = 2;
                                             f;
                                         }),
                                     }))
                WriteSizesFile(live_gui);

            DevGuiLabel(g, DevGuiGetLeftR(g), name, TextJustification::CentredRight);

            DevGuiIncrementPos(g);
        }
    }
}

static auto GetColourNames(LiveEditGui const& gui, bool include_none) {
    DynamicArrayBounded<String, k_max_num_colours + 1> colour_names;
    if (include_none) dyn::Append(colour_names, "---");
    for (auto const i : Range(k_max_num_colours))
        dyn::Append(colour_names, gui.ui_cols[i].name);
    return colour_names;
}

static int FindColourIndex(LiveEditGui const& gui, String col_string) {
    if (col_string.size == 0) return -1;
    for (auto const i : Range(k_max_num_colours))
        if (String(gui.ui_cols[i].name) == col_string) return i;
    return -1;
}

static void
LiveEditColourMapMenus(DeveloperPanel& g, String search, String colour_search, bool high_contrast) {
    auto& live_gui = g_live_edit_values;
    DevGuiHeading(g, "Colour Mapping");

    static DynamicArrayBounded<String, ToInt(UiColMap::Count)> categories {};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiColMap::Count)))
            dyn::AppendIfNotAlreadyThere(categories, k_ui_col_map_categories[i]);
    auto col_names = GetColourNames(live_gui, high_contrast);

    for (auto const cat : categories) {
        g.imgui.PushId(cat);
        DEFER { g.imgui.PopId(); };

        bool contains_values = search.size && ContainsCaseInsensitiveAscii(cat, search);
        if (!contains_values) {
            for (auto const i : Range(ToInt(UiColMap::Count))) {
                auto& col_map = high_contrast ? live_gui.ui_col_map[i].high_contrast_colour
                                              : live_gui.ui_col_map[i].colour;

                if (k_ui_col_map_categories[i] != cat) continue;
                if (!ContainsCaseInsensitiveAscii(k_ui_col_map_names[i], search)) continue;
                if (col_map.size && !ContainsCaseInsensitiveAscii(col_map, colour_search)) continue;
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

            auto& col_map =
                high_contrast ? live_gui.ui_col_map[i].high_contrast_colour : live_gui.ui_col_map[i].colour;
            if (col_map.size && !ContainsCaseInsensitiveAscii(col_map, colour_search)) continue;

            g.imgui.PushId((u64)i);
            DEFER { g.imgui.PopId(); };

            Rect const label_r = DevGuiGetLeftR(g);
            Rect const sr = DevGuiGetRightR(g);

            int index = FindColourIndex(live_gui, col_map);
            if (index == -1 && high_contrast) index = 0;
            bool const changed = DevGuiMenu(g, sr, col_names.Items(), index);
            DevGuiLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) {
                if (high_contrast && index == 0)
                    col_map.size = 0;
                else
                    col_map = String(live_gui.ui_cols[index].name);
                WriteColourMapFile(live_gui);
            }

            DevGuiIncrementPos(g);
        }
        DevGuiIncrementPos(g);
    }
}

static void RecalculateBasedOnCol(LiveEditColour& c, LiveEditColour const& other_c) {
    c.col = other_c.col;
    c.col = colour::ChangeBrightness(c.col, Pow(2.0f, c.with_brightness));
    c.col = colour::ChangeAlpha(c.col, Pow(2.0f, c.with_alpha));
}

static void LiveEditColourSliders(DeveloperPanel& gui, String search) {
    auto& live_gui = g_live_edit_values;
    auto& imgui = gui.imgui;
    auto pad = 1.0f;

    DevGuiHeading(gui, "Colours");

    for (auto const index : Range<uintptr>(k_max_num_colours)) {
        auto& c = live_gui.ui_cols[index];
        if (c.name.size && !ContainsCaseInsensitiveAscii(c.name, search)) continue;

        colour::Channels const col = colour::FromU32(c.col);
        auto hex_rgb = u32(col.r << 16 | col.g << 8 | col.b);
        auto const a = col.a / 255.0f;
        auto const b = col.b / 255.0f;
        auto const g = col.g / 255.0f;
        auto const r = col.r / 255.0f;

        auto hsv = colour::ConvertRgbtoHsv({.r = r, .g = g, .b = b});
        f32 alpha = a;

        imgui.PushId(index);
        DEFER { imgui.PopId(); };

        auto id = imgui.MakeId(index);

        f32 x_pos = 0;
        Rect const label_r = {.xywh {x_pos, gui.y_pos, imgui.CurrentVpWidth() / 3.5f, k_item_h}};
        x_pos += label_r.w;
        Rect const hex_col_r = {.xywh {x_pos, gui.y_pos, imgui.CurrentVpWidth() / 8, k_item_h}};
        x_pos += hex_col_r.w + pad;
        Rect col_preview_r = {.xywh {x_pos, gui.y_pos, k_item_h - pad, k_item_h}};
        x_pos += col_preview_r.w + pad;
        auto remaining_w = imgui.CurrentVpWidth() - x_pos;
        Rect const edit_button_r = {.xywh {x_pos, gui.y_pos, ((remaining_w / 12) * 2) - pad, k_item_h}};
        x_pos += edit_button_r.w + pad;
        Rect const based_on_r = {.xywh {x_pos, gui.y_pos, ((remaining_w / 12) * 6) - pad, k_item_h}};
        x_pos += based_on_r.w + pad;
        Rect const bright_r = {.xywh {x_pos, gui.y_pos, ((remaining_w / 12) * 2) - pad, k_item_h}};
        x_pos += bright_r.w + pad;
        Rect const alpha_r = {.xywh {x_pos, gui.y_pos, ((remaining_w / 12) * 2) - pad, k_item_h}};

        bool hex_code_changed = false;
        bool hsv_changed = false;

        {
            auto const display = fmt::FormatInline<16>("{06x}", hex_rgb);
            if (c.based_on.size == 0) {
                auto const res = ({
                    auto const input_r = gui.imgui.RegisterAndConvertRect(hex_col_r);
                    auto const o = gui.imgui.TextInputBehaviour({
                        .rect_in_window_coords = input_r,
                        .id = id,
                        .text = display,
                        .input_cfg {
                            .chars_hexadecimal = true,
                        },
                    });
                    DrawDevGuiTextInput(gui.imgui, o, id, input_r);
                    o;
                });
                if (res.buffer_changed) {
                    hex_code_changed = true;
                    auto const rgb = ParseInt(({
                                                  auto hex_str = res.text;
                                                  if (hex_str[0] == '#') hex_str.RemovePrefix(1);
                                                  hex_str;
                                              }),
                                              ParseIntBase::Hexadecimal)
                                         .ValueOr(0);
                    c.col = colour::ToU32({
                        .a = col.a,
                        .b = (u8)(rgb & 0xff),
                        .g = (u8)((rgb & 0xff00) >> 8),
                        .r = (u8)((rgb & 0xff0000) >> 16),
                    });
                }
            } else {
                DevGuiLabel(gui, hex_col_r, display, TextJustification::CentredLeft);
            }
        }

        if (c.based_on.size == 0) {
            auto const pop_id = gui.imgui.MakeId("Pop");
            auto const btn_r = imgui.RegisterAndConvertRect(edit_button_r);
            auto const btn_id = imgui.MakeId("edit");
            auto const o = imgui.PopupMenuButtonBehaviour(btn_r, btn_id, pop_id, imgui::ButtonConfig {});
            DrawDevGuiPopupTextButton(imgui, btn_r, btn_id, "Edit", o.show_as_active);

            if (gui.imgui.IsPopupMenuOpen(pop_id)) {
                gui.imgui.BeginViewport(DevGuiPopupConfig(), pop_id, btn_r);
                DEFER { imgui.EndViewport(); };

                static f32 static_hue;
                static f32 static_sat;
                static f32 static_val;
                static f32 static_alpha;

                if (imgui.DidPopupMenuJustOpen(pop_id)) {
                    static_hue = hsv.h;
                    static_sat = hsv.s;
                    static_val = hsv.v;
                    static_alpha = alpha;
                }

                f32 const pop_w = (f32)GuiIo().in.window_size.width / 3.5f;
                f32 const text_size = pop_w / 4;
                f32 const itm_w = (pop_w - text_size) / 3;
                f32 pop_pos = 0;

                constexpr String k_format_string = "{.4}";
                constexpr f32 k_slider_sensitivity = 1000;

                DoBasicWhiteText(gui.imgui, {.xywh {0, pop_pos, text_size, k_item_h}}, "Alpha");
                hsv_changed |= DoDevGuiFloatDragger(
                    gui,
                    {
                        .viewport_r = {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, k_item_h}},
                        .id = imgui.MakeId((uintptr)&alpha),
                        .format_string = k_format_string,
                        .min = 0,
                        .max = 1,
                        .value = static_alpha,
                        .slider_cfg = ({
                            auto f = imgui::SliderConfig {};
                            f.sensitivity = k_slider_sensitivity;
                            f;
                        }),
                    });
                pop_pos += k_item_h + pad;

                DoBasicWhiteText(gui.imgui, {.xywh {0, pop_pos, text_size, k_item_h}}, "Hue");
                hsv_changed |= DoDevGuiFloatDragger(
                    gui,
                    {
                        .viewport_r = {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, k_item_h}},
                        .id = imgui.MakeId("hue"),
                        .format_string = k_format_string,
                        .min = 0,
                        .max = 1,
                        .value = static_hue,
                        .slider_cfg = ({
                            auto f = imgui::SliderConfig {};
                            f.sensitivity = k_slider_sensitivity;
                            f;
                        }),
                    });
                pop_pos += k_item_h + pad;

                DoBasicWhiteText(gui.imgui, {.xywh {0, pop_pos, text_size, k_item_h}}, "Sat");
                hsv_changed |= DoDevGuiFloatDragger(
                    gui,
                    {
                        .viewport_r = {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, k_item_h}},
                        .id = imgui.MakeId("sat"),
                        .format_string = k_format_string,
                        .min = 0,
                        .max = 1,
                        .value = static_sat,
                        .slider_cfg = ({
                            auto f = imgui::SliderConfig {};
                            f.sensitivity = k_slider_sensitivity;
                            f;
                        }),
                    });
                pop_pos += k_item_h + pad;

                DoBasicWhiteText(gui.imgui, {.xywh {0, pop_pos, text_size, k_item_h}}, "Val");
                hsv_changed |= DoDevGuiFloatDragger(
                    gui,
                    {
                        .viewport_r = {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, k_item_h}},
                        .id = imgui.MakeId("val"),
                        .format_string = k_format_string,
                        .min = 0,
                        .max = 1,
                        .value = static_val,
                        .slider_cfg = ({
                            auto f = imgui::SliderConfig {};
                            f.sensitivity = k_slider_sensitivity;
                            f;
                        }),
                    });

                if (hsv_changed) {
                    c.col = colour::ToU32(colour::FromFloatRgb(
                        colour::ConvertHsvtoRgb({.h = static_hue, .s = static_sat, .v = static_val}),
                        (u8)(static_alpha * 255.0f)));
                }
            }
        }

        {
            col_preview_r = imgui.RegisterAndConvertRect(col_preview_r);
            imgui.draw_list->AddRectFilled(col_preview_r, c.col);
        }

        auto float_dragger = [&](Rect slider_r, imgui::Id id, f32 min, f32 max, f32& value) {
            return DoDevGuiFloatDragger(gui,
                                        {
                                            .viewport_r = slider_r,
                                            .id = id,
                                            .format_string = "{.3}",
                                            .min = min,
                                            .max = max,
                                            .value = value,
                                            .slider_cfg = ({
                                                auto f = imgui::SliderConfig {};
                                                f.sensitivity = 1000;
                                                f;
                                            }),
                                        });
        };

        auto text_editor = [&](Rect edit_r, imgui::Id id, ColourString& str) {
            str.NullTerminate();
            auto const res = ({
                auto const input_r = gui.imgui.RegisterAndConvertRect(edit_r);
                auto const o = gui.imgui.TextInputBehaviour({
                    .rect_in_window_coords = input_r,
                    .id = id,
                    .text = str,
                });
                DrawDevGuiTextInput(gui.imgui, o, id, input_r);
                o;
            });
            if (res.enter_pressed) {
                str = res.text;
                return true;
            }
            return false;
        };

        ColourString const starting_name = c.name;
        if (text_editor(label_r, imgui.MakeId("name"), c.name)) {
            hex_code_changed = true;
            for (auto& m : live_gui.ui_col_map) {
                if (String(m.colour) == String(starting_name)) m.colour = String(c.name);
                if (String(m.high_contrast_colour) == String(starting_name))
                    m.high_contrast_colour = String(c.name);
            }
            for (auto& other_c : live_gui.ui_cols)
                if (other_c.based_on.size && String(other_c.based_on) == String(starting_name))
                    other_c.based_on = String(c.name);

            WriteColourMapFile(live_gui);
        }

        bool recalculate_val = false;
        if (c.based_on.size) {
            recalculate_val |= float_dragger(bright_r, imgui.MakeId("Light Scale"), -8, 8, c.with_brightness);
            recalculate_val |= float_dragger(alpha_r, imgui.MakeId("Alpha"), -8, 8, c.with_alpha);
        }
        if (text_editor(based_on_r, imgui.MakeId("based"), c.based_on)) {
            bool valid = false;
            for (auto const& other_c : live_gui.ui_cols)
                if (other_c.name.size && String(other_c.name) == String(c.based_on)) valid = true;

            if (!valid) c.based_on.size = 0;

            recalculate_val = true;
        }

        if (recalculate_val) {
            hex_code_changed = true;
            for (auto const& other_c : live_gui.ui_cols) {
                if (other_c.name.size && String(other_c.name) == String(c.based_on)) {
                    RecalculateBasedOnCol(c, other_c);
                    break;
                }
            }
        }

        if (hex_code_changed || hsv_changed) {
            for (auto& other_c : live_gui.ui_cols)
                if (other_c.based_on.size && String(other_c.based_on) == String(c.name))
                    RecalculateBasedOnCol(other_c, c);

            WriteColoursFile(live_gui);
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        }

        DevGuiIncrementPos(gui);
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
        for (auto& t : GuiIo().out.timed_wakeups)
            DevGuiText(g, "Time: {}", t.Raw());
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

static void DoLiveEditColourEditor(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "colour-edit");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    static DevGuiTextInputBuffer search;
    DevGuiTextInput(g, "Search:", search);
    LiveEditColourSliders(g, search);
}

static void DoLiveEditColourMapEditor(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "colour-map-edit");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    static DevGuiTextInputBuffer search;
    DevGuiTextInput(g, "Search:", search);
    static DevGuiTextInputBuffer colour_search;
    DevGuiTextInput(g, "Colour Search:", colour_search);

    static bool show_high_contrast = false;
    if (DevGuiButton(g,
                     show_high_contrast ? "Current: On"_s : "Current: Off",
                     "Show colours for high contrast mode"))
        show_high_contrast = !show_high_contrast;

    LiveEditColourMapMenus(g, search, colour_search, show_high_contrast);
}

static void DoLiveEditSizeEditor(DeveloperPanel& g, Rect r) {
    g.imgui.BeginViewport(DevGuiViewport(), r, "size-edit");
    DEFER { g.imgui.EndViewport(); };

    DevGuiReset(g);

    static DevGuiTextInputBuffer search;
    DevGuiTextInput(g, "Search:", search);
    LiveEditSliders(g, search);
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
        g.imgui.BeginViewport(DevGuiViewport(),
                              (g_show_dev_gui_on_left)
                                  ? Rect {.xywh {half_w + 1, 0, half_w - 1, g.imgui.CurrentVpHeight()}}
                                  : Rect {.xywh {0, 0, half_w - 1, g.imgui.CurrentVpHeight()}},
                              "whole-dev-gui");
        DEFER { g.imgui.EndViewport(); };

        static String const tab_text[] = {
            "Commands",
            "Audio",
            "Colours",
            "ColMap",
            "Sizes",
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
            case 2: DoLiveEditColourEditor(g, selected_r); break;
            case 3: DoLiveEditColourMapEditor(g, selected_r); break;
            case 4: DoLiveEditSizeEditor(g, selected_r); break;
            case 5: DoImGuiInspector(g, selected_r); break;
            default: PanicIfReached();
        }
    }
}
