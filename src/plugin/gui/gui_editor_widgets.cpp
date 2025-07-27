// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_editor_widgets.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/constants.hpp"

#include "gui_editors.hpp"
#include "gui_framework/colours.hpp"
#include "gui_window.hpp"

void EditorReset(EditorGUI* g) {
    g->y_pos = 0;
    g->alternating_back = false;
}

Rect EditorGetFullR(EditorGUI* g) {
    return Rect {.x = 0, .y = g->y_pos, .w = g->imgui->Width(), .h = g->item_h - 1};
}

Rect EditorGetLeftR(EditorGUI* g) {
    return Rect {.x = 0, .y = g->y_pos, .w = g->imgui->Width() / 2, .h = g->item_h - 1};
}

Rect EditorGetRightR(EditorGUI* g) {
    auto w = g->imgui->Width() / 2;
    return Rect {.x = w, .y = g->y_pos, .w = w, .h = g->item_h - 1};
}

void EditorIncrementPos(EditorGUI* g, f32 size) { g->y_pos += (size != 0) ? size : g->item_h; }

void EditorText(EditorGUI* g, String text) {
    g->imgui->Text(imgui::DefText(), EditorGetFullR(g), text);
    EditorIncrementPos(g);
}

void EditorHeading(EditorGUI* g, String text) {
    if (g->y_pos != 0) // don't do this for the first item
        g->y_pos += g->item_h;

    auto r = EditorGetFullR(g);
    auto back_r = r;
    g->imgui->RegisterAndConvertRect(&back_r);
    g->imgui->graphics->AddRectFilled(back_r.Min(), back_r.Max(), 0x50ffffff);
    g->imgui->Text(imgui::DefText(), r, text);
    // g->imgui->graphics->AddLine(r.BottomLeft(), r.BottomRight(), 0xffffffff);

    g->y_pos += g->item_h * 1.1f;
}

void EditorLabel(EditorGUI* g, Rect r, String text, TextJustification just) {
    g->imgui->graphics->AddTextJustified(g->imgui->GetRegisteredAndConvertedRect(r.CutRight(4)),
                                         text,
                                         imgui::DefText().col,
                                         just,
                                         TextOverflowType::ShowDotsOnRight);
}

void EditorLabel(EditorGUI* g, String text) {
    EditorLabel(g, EditorGetLeftR(g), text, TextJustification::CentredRight);
}

bool EditorButton(EditorGUI* g, String button, String label) {
    g->imgui->Text(imgui::DefText(), EditorGetLeftR(g), label);

    bool const result = g->imgui->Button(imgui::DefButton(), EditorGetRightR(g), button);
    EditorIncrementPos(g);
    return result;
}

bool EditorBeginMenu(EditorGUI* g, String label, String text, imgui::Id pop_id) {
    EditorLabel(g, label);
    auto btn_id = g->imgui->GetID(label);
    auto sets = imgui::DefButton();
    sets.window = PopupWindowSettings(*g->imgui);
    bool const res = g->imgui->PopupButton(sets, EditorGetRightR(g), btn_id, pop_id, text);
    EditorIncrementPos(g);
    return res;
}

void EditorEndMenu(EditorGUI* g) { g->imgui->EndWindow(); }

bool EditorSlider(EditorGUI* g, Rect r, imgui::Id id, f32 min, f32 max, f32& val) {
    return g->imgui->SliderRange(imgui::DefSlider(), r, id, min, max, val, min);
}

bool EditorSlider(EditorGUI* g, String label, f32 min, f32 max, f32& val) {
    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};
    EditorLabel(g, fmt::Format(allocator, "{} ({.2})", label, val));
    auto res = EditorSlider(g, EditorGetRightR(g), g->imgui->GetID(label), min, max, val);
    EditorIncrementPos(g);
    return res;
}

bool EditorSlider(EditorGUI* g, String label, int min, int max, int& val) {
    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};
    EditorLabel(g, fmt::Format(allocator, "{} ({})", label, val));
    auto id = g->imgui->GetID(label);
    auto fval = (f32)val;
    auto res =
        g->imgui->SliderRange(imgui::DefSlider(), EditorGetRightR(g), id, (f32)min, (f32)max, fval, (f32)min);
    if (res) val = (int)fval;
    EditorIncrementPos(g);
    return res;
}

bool EditorDragger(EditorGUI* g, String label, int min, int max, int& val) {
    ArenaAllocatorWithInlineStorage<100> allocator {Malloc::Instance()};
    EditorLabel(g, fmt::Format(allocator, "{} ({})", label, val));
    auto id = g->imgui->GetID(label);
    auto sets = imgui::DefTextInputDraggerInt();
    sets.slider_settings.sensitivity *= 3;
    bool const res = g->imgui->TextInputDraggerInt(sets, EditorGetRightR(g), id, min, max, val);
    EditorIncrementPos(g);
    return res;
}

void EditorTextInput(EditorGUI* g, String label, EditorTextInputBuffer& buf) {
    EditorLabel(g, label);
    auto const r = EditorGetRightR(g);
    auto const id = g->imgui->GetID(label);
    auto sets = imgui::DefTextInput();
    auto const result = g->imgui->TextInput(sets, r, id, buf);
    if (result.buffer_changed) dyn::Assign(buf, result.text);

    EditorIncrementPos(g);
}

bool EditorMenuItems(EditorGUI* g, Span<String const> items, int& current) {
    auto w = g->imgui->LargestStringWidth(4, items);
    auto h = g->item_h;

    auto item_set = imgui::DefButton();
    item_set.flags.closes_popups = true;

    int clicked = -1;
    for (auto const i : Range(items.size)) {
        bool selected = (int)i == current;
        if (g->imgui->ToggleButton(item_set,
                                   {.xywh = {0, h * (f32)i, w, h}},
                                   g->imgui->GetID(items[i]),
                                   selected,
                                   items[i]))
            clicked = (int)i;
    }
    if (clicked != -1 && current != clicked) {
        current = clicked;
        return true;
    }
    return false;
}

bool EditorMenu(EditorGUI* g, Rect r, Span<String const> items, int& current) {
    auto sets = imgui::DefButtonPopup();
    auto id = g->imgui->GetID((uintptr)items.data);
    auto curr_text = items[(usize)current];
    bool result = false;
    if (g->imgui->PopupButton(sets, r, id, id + 1, curr_text)) {
        result = EditorMenuItems(g, items, current) || result;
        g->imgui->EndWindow();
    }
    return result;
}

void EditorLabelAlternatingBack(EditorGUI* g, Rect r, String text, bool extra_highlight) {
    if (g->alternating_back || extra_highlight) {
        auto reg = r;
        g->imgui->RegisterAndConvertRect(&reg);
        auto col = 0x15ffffffu;
        if (extra_highlight) col = 0x35ffbfbfu;
        g->imgui->graphics->AddRectFilled(reg.Min(), reg.Max(), col);
    }
    g->alternating_back = !g->alternating_back;
    EditorLabel(g, r, text, TextJustification::CentredLeft);
}

#if FLOE_EDITOR_ENABLED

constexpr String k_ui_sizes_categories[ToInt(UiSizeId::Count)] = {
#define GUI_SIZE(cat, n, v, unit) #cat,
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
        "// Copyright 2018-2024 Sam Windell\n// SPDX-License-Identifier: GPL-3.0-or-later\n\n");
    // REUSE-IgnoreEnd
}

static void WriteColoursFile(LiveEditGui const& gui) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
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
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    auto outcome = OpenFile(UiStyleFilepath(scratch_arena, SIZES_DEF_FILENAME), FileMode::Write());
    if (outcome.HasError()) {
        LogError(ModuleName::Gui, "{} failed: {}", __FUNCTION__, outcome.Error());
        return;
    }

    WriteHeader(outcome.Value().Writer());

    for (auto const i : Range(ToInt(UiSizeId::Count))) {
        auto const sz = gui.ui_sizes[i];
        String const name = gui.ui_sizes_names[i];
        auto unit_name = k_ui_size_units_text[ToInt(gui.ui_sizes_units[i])];
        auto cat = k_ui_sizes_categories[i];
        auto o = fmt::FormatToWriter(outcome.Value().Writer(),
                                     "GUI_SIZE({}, {}, {.6}f, {})\n",
                                     cat,
                                     name,
                                     sz,
                                     unit_name);
        if (o.HasError())
            LogError(ModuleName::Gui,
                     "could not write to file {} for reason {}",
                     SIZES_DEF_FILENAME,
                     o.Error());
    }
}

static void WriteColourMapFile(LiveEditGui const& gui) {
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
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

void SizesGUISliders(EditorGUI* g, String search) {
    auto& live_gui = imgui::g_live_edit_values;
    EditorHeading(g, "Sizes");

    static DynamicArrayBounded<String, ToInt(UiSizeId::Count)> categories {};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiSizeId::Count)))
            dyn::AppendIfNotAlreadyThere(categories, k_ui_sizes_categories[i]);

    for (auto cat : categories) {
        g->imgui->PushID(cat);
        DEFER { g->imgui->PopID(); };

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

        EditorHeading(g, cat);

        for (auto const i : Range(ToInt(UiSizeId::Count))) {
            if (k_ui_sizes_categories[i] != cat) continue;
            auto name = live_gui.ui_sizes_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;

            f32 sz = live_gui.ui_sizes[i];

            Rect const label_r = EditorGetLeftR(g);
            Rect const sr = EditorGetRightR(g);

            auto settings = imgui::DefTextInputDraggerFloat();
            settings.slider_settings.sensitivity = 2;
            bool const changed =
                g->imgui->TextInputDraggerFloat(settings, sr, g->imgui->GetID(name), 0, 1500, sz);
            EditorLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) {
                live_gui.ui_sizes[i] = sz;
                WriteSizesFile(live_gui);
            }

            EditorIncrementPos(g);
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

void ColourMapGUIMenus(EditorGUI* g, String search, String colour_search, bool high_contrast) {
    auto& live_gui = imgui::g_live_edit_values;
    EditorHeading(g, "Colour Mapping");

    static DynamicArrayBounded<String, ToInt(UiColMap::Count)> categories {};
    if (categories.size == 0)
        for (auto const i : Range(ToInt(UiColMap::Count)))
            dyn::AppendIfNotAlreadyThere(categories, k_ui_col_map_categories[i]);
    auto col_names = GetColourNames(live_gui, high_contrast);

    for (auto cat : categories) {
        g->imgui->PushID(cat);
        DEFER { g->imgui->PopID(); };

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

        EditorHeading(g, cat);

        for (auto const i : Range(ToInt(UiColMap::Count))) {
            if (k_ui_col_map_categories[i] != cat) continue;

            auto const name = k_ui_col_map_names[i];
            if (!ContainsCaseInsensitiveAscii(name, search) && !ContainsCaseInsensitiveAscii(cat, search))
                continue;

            auto& col_map =
                high_contrast ? live_gui.ui_col_map[i].high_contrast_colour : live_gui.ui_col_map[i].colour;
            if (col_map.size && !ContainsCaseInsensitiveAscii(col_map, colour_search)) continue;

            g->imgui->PushID((u64)i);
            DEFER { g->imgui->PopID(); };
            Rect const label_r = EditorGetLeftR(g);
            Rect const sr = EditorGetRightR(g);

            int index = FindColourIndex(live_gui, col_map);
            if (index == -1 && high_contrast) index = 0;
            bool const changed = EditorMenu(g, sr, col_names.Items(), index);
            EditorLabel(g, label_r, name, TextJustification::CentredRight);

            if (changed) {
                if (high_contrast && index == 0)
                    col_map.size = 0;
                else
                    col_map = String(live_gui.ui_cols[index].name);
                WriteColourMapFile(live_gui);
            }

            EditorIncrementPos(g);
        }
        EditorIncrementPos(g);
    }
}

static void RecalculateBasedOnCol(EditorCol& c, EditorCol const& other_c) {
    c.col = other_c.col;
    c.col = colours::ChangeBrightness(c.col, Pow(2.0f, c.with_brightness));
    c.col = colours::ChangeAlpha(c.col, Pow(2.0f, c.with_alpha));
}

void ColoursGUISliders(EditorGUI* gui, String search) {
    auto& live_gui = imgui::g_live_edit_values;
    auto imgui = gui->imgui;
    auto pad = 1.0f;
    auto h = gui->item_h;

    EditorHeading(gui, "Colours");

    for (auto const index : Range<uintptr>(k_max_num_colours)) {
        auto& c = live_gui.ui_cols[index];
        if (c.name.size && !ContainsCaseInsensitiveAscii(c.name, search)) continue;

        colours::Col const col = colours::FromU32(c.col);
        auto hex_rgb = u32(col.r << 16 | col.g << 8 | col.b);
        f32 a;
        f32 b;
        f32 g;
        f32 r;
        a = col.a / 255.0f;
        b = col.b / 255.0f;
        g = col.g / 255.0f;
        r = col.r / 255.0f;
        f32 hue;
        f32 sat;
        f32 val;
        f32 alpha;
        colours::ConvertRGBtoHSV(r, g, b, hue, sat, val);
        alpha = a;

        imgui->PushID(index);
        auto id = imgui->GetID(index);

        f32 x_pos = 0;
        Rect const label_r = {.xywh {x_pos, gui->y_pos, imgui->Width() / 3.5f, h}};
        x_pos += label_r.w;
        Rect const hex_col_r = {.xywh {x_pos, gui->y_pos, imgui->Width() / 8, h}};
        x_pos += hex_col_r.w + pad;
        Rect col_preview_r = {.xywh {x_pos, gui->y_pos, h - pad, h}};
        x_pos += col_preview_r.w + pad;
        auto remaining_w = imgui->Width() - x_pos;
        Rect const edit_button_r = {.xywh {x_pos, gui->y_pos, ((remaining_w / 12) * 2) - pad, h}};
        x_pos += edit_button_r.w + pad;
        Rect const based_on_r = {.xywh {x_pos, gui->y_pos, ((remaining_w / 12) * 6) - pad, h}};
        x_pos += based_on_r.w + pad;
        Rect const bright_r = {.xywh {x_pos, gui->y_pos, ((remaining_w / 12) * 2) - pad, h}};
        x_pos += bright_r.w + pad;
        Rect const alpha_r = {.xywh {x_pos, gui->y_pos, ((remaining_w / 12) * 2) - pad, h}};

        bool hex_code_changed = false;
        bool hsv_changed = false;

        {
            char display[32];
            stbsp_sprintf(display, "%06x", hex_rgb);
            if (c.based_on.size == 0) {
                auto settings = imgui::DefTextInput();
                settings.text_flags.chars_hexadecimal = true;
                auto const res = gui->imgui->TextInput(settings, hex_col_r, id, FromNullTerminated(display));
                hex_code_changed = res.buffer_changed;
                if (hex_code_changed) {
                    char const* start = res.text.data;
                    if (start[0] == '#') start++;
                    auto rgb = ParseInt(FromNullTerminated(start), ParseIntBase::Hexadecimal).ValueOr(0);
                    colours::Col cs = {};
                    cs.a = col.a;
                    cs.r = (u8)((rgb & 0xff0000) >> 16);
                    cs.g = (u8)((rgb & 0xff00) >> 8);
                    cs.b = (u8)(rgb & 0xff);
                    c.col = colours::ToU32(cs);
                }
            } else {
                EditorLabel(gui, hex_col_r, FromNullTerminated(display), TextJustification::CentredLeft);
            }
        }

        if (c.based_on.size == 0) {
            auto pop_id = gui->imgui->GetID("Pop");
            if (imgui->PopupButton(imgui::DefButtonPopup(),
                                   edit_button_r,
                                   imgui->GetID("Edit"),
                                   pop_id,
                                   "Edit")) {
                static f32 static_hue;
                static f32 static_val;
                static f32 static_sat;
                static f32 static_alpha;
                if (imgui->DidPopupMenuJustOpen(pop_id)) {
                    static_hue = hue;
                    static_val = val;
                    static_sat = sat;
                    static_alpha = alpha;
                }

                f32 const pop_w = (f32)imgui->frame_input.window_size.width / 3.5f;
                f32 const text_size = pop_w / 4;
                f32 const itm_w = (pop_w - text_size) / 3;
                f32 pop_pos = 0;

                auto dragger_set = imgui::DefTextInputDraggerFloat();
                dragger_set.format = "{.4}";
                dragger_set.slider_settings.sensitivity = 1000;

                imgui->Text(imgui::DefText(), {.xywh {0, pop_pos, text_size, h}}, "Alpha");
                hsv_changed |=
                    imgui->TextInputDraggerFloat(dragger_set,
                                                 {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, h}},
                                                 imgui->GetID((uintptr)&alpha),
                                                 0,
                                                 1,
                                                 static_alpha);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {.xywh {0, pop_pos, text_size, h}}, "Hue");
                hsv_changed |=
                    imgui->TextInputDraggerFloat(dragger_set,
                                                 {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, h}},
                                                 imgui->GetID((uintptr)&hue),
                                                 0,
                                                 1,
                                                 static_hue);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {.xywh {0, pop_pos, text_size, h}}, "Sat");
                hsv_changed |=
                    imgui->TextInputDraggerFloat(dragger_set,
                                                 {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, h}},
                                                 imgui->GetID((uintptr)&sat),
                                                 0,
                                                 1,
                                                 static_sat);
                pop_pos += h + pad;

                imgui->Text(imgui::DefText(), {.xywh {0, pop_pos, text_size, h}}, "Val");
                hsv_changed |=
                    imgui->TextInputDraggerFloat(dragger_set,
                                                 {.xywh {text_size + (0 * itm_w), pop_pos, itm_w - pad, h}},
                                                 imgui->GetID((uintptr)&val),
                                                 0,
                                                 1,
                                                 static_val);

                if (hsv_changed) {
                    f32 r1;
                    f32 g1;
                    f32 b1;
                    colours::ConvertHSVtoRGB(static_hue, static_sat, static_val, r1, g1, b1);

                    colours::Col new_col;
                    new_col.a = u8(static_alpha * 255.0f);
                    new_col.r = u8(r1 * 255.0f);
                    new_col.g = u8(g1 * 255.0f);
                    new_col.b = u8(b1 * 255.0f);

                    u32 const col_from_hsv = colours::ToU32(new_col);
                    c.col = col_from_hsv;
                }
                imgui->EndWindow();
            }
        }

        {
            imgui->RegisterAndConvertRect(&col_preview_r);
            imgui->graphics->AddRectFilled(col_preview_r.Min(), col_preview_r.Max(), c.col);
        }

        auto float_dragger = [&](Rect slider_r, imgui::Id id, f32 min, f32 max, f32& value) {
            auto settings = imgui::DefTextInputDraggerFloat();
            settings.format = "{.3}";
            settings.slider_settings.sensitivity = 1000;
            return gui->imgui->TextInputDraggerFloat(settings, slider_r, id, min, max, value);
        };

        auto text_editor = [&](Rect edit_r, imgui::Id id, ColourString& str) {
            str.NullTerminate();
            auto settings = imgui::DefTextInput();
            auto const res = gui->imgui->TextInput(settings, edit_r, id, str);
            if (res.enter_pressed) {
                str = res.text;
                return true;
            }
            return false;
        };

        ColourString const starting_name = c.name;
        if (text_editor(label_r, imgui->GetID("name"), c.name)) {
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
            recalculate_val |= float_dragger(bright_r, imgui->GetID("Light Scale"), -8, 8, c.with_brightness);
            recalculate_val |= float_dragger(alpha_r, imgui->GetID("Alpha"), -8, 8, c.with_alpha);
        }
        if (text_editor(based_on_r, imgui->GetID("based"), c.based_on)) {
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
                    hex_code_changed = true;
                    break;
                }
            }
        }

        if (hex_code_changed || hsv_changed) {
            for (auto& other_c : live_gui.ui_cols)
                if (other_c.based_on.size && String(other_c.based_on) == String(c.name))
                    RecalculateBasedOnCol(other_c, c);

            WriteColoursFile(live_gui);
            imgui->frame_output.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::ImmediatelyUpdate);
        }

        EditorIncrementPos(gui);
        imgui->PopID();
    }
}

#else

void SizesGUISliders(EditorGUI*, String) {}
void ColourMapGUIMenus(EditorGUI*, String, String, bool) {}
void ColoursGUISliders(EditorGUI*, String) {}

#endif
