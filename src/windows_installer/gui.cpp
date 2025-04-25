// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <minwindef.h>
#include <windef.h>
#include <windows.h>

//
#include "os/undef_windows_macros.h"
//

#include <commctrl.h>
#include <errhandlingapi.h>
#include <richedit.h>
#include <shellapi.h>
#include <stb_image.h>
#include <wingdi.h>

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
#include "os/misc_windows.hpp"
#include "os/threading.hpp"
#include "utils/cli_arg_parse.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "common_infrastructure/error_reporting.hpp"
#include "common_infrastructure/global.hpp"

#include "gui.hpp"

constexpr auto k_page_class_name = L"floe-page";
constexpr auto k_divider_class_name = L"floe-divider";

enum class ProgressBarMode { None, Marquee, Normal };

struct Widget {
    static constexpr usize k_max_children = 10;

    HWND window {};
    GuiFramework* framework {};
    u32 id {};

    DynamicArrayBounded<Widget*, k_max_children> children {};

    WidgetOptions options {};

    ProgressBarMode progress_bar_mode {};
    LabelStyle label_style {};
    ButtonStyle button_style {};
    bool button_state {};
    int scroll_y {};
    bool scroll_y_visible {};
};

struct GuiFramework {
    struct AllocedWidget {
        Widget& widget;
        u32 id;
    };

    AllocedWidget AllocWidget() {
        auto const id = (u32)widgets.size;
        ASSERT(id != widgets.Capacity());
        dyn::Emplace(widgets, Widget {.framework = this, .id = id});
        return {Last(widgets), id};
    }

    HWND root {};
    Widget* root_layout {};
    HFONT regular_font {};
    HFONT bold_font {};
    HFONT heading_font {};
    HBRUSH static_background_brush {};
    Application* app {};
    DynamicArrayBounded<Widget, k_max_widgets> widgets {};
    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};
    ArenaAllocator arena {PageAllocator::Instance()};
    DynamicArray<char> buffer {arena};
    bool in_timer = false;
    bool autorun_mode = false;
};

struct WindowRect {
    int x, y;
    UiSize size;
};

#undef CreateWindow

static void CheckLastError(String message, SourceLocation loc = SourceLocation::Current()) {
    auto const error = GetLastError();
    if (error) PanicF(loc, "{}, {}", message, Win32ErrorCode(error));
}

static void Require(bool condition, String message, SourceLocation loc = SourceLocation::Current()) {
    if (!condition) CheckLastError(message, loc);
}

// for 'static' controls, the notifications regarding interaction are passed to the _parent_ window, and
// therefore we need to use the static_id field to identify the child item that was interacted with.
static HWND CreateWindow(Widget& widget,
                         const WCHAR* class_name,
                         const WCHAR* window_name,
                         WindowRect r,
                         DWORD style,
                         DWORD ex_style,
                         HWND parent,
                         u32 button_id = 0) {
    auto window = CreateWindowExW(ex_style,
                                  class_name,
                                  window_name,
                                  style,
                                  r.x,
                                  r.y,
                                  r.size.width,
                                  r.size.height,
                                  parent,
                                  (HMENU)(uintptr_t)button_id,
                                  GetModuleHandleW(nullptr),
                                  nullptr);
    ASSERT(window, "failed to create window");

    SetLastError(0);
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&widget);
    CheckLastError("SetWindowLongPtrW");

    return window;
}

static UiSize WindowContentsSize(HWND window) {
    RECT r;
    Require(GetClientRect(window, &r), "GetClientRect");
    return {CheckedCast<u16>(r.right), CheckedCast<u16>(r.bottom)};
}

static WCHAR* NullTermWideString(GuiFramework& framework, String str) {
    ASSERT(IsValidUtf8(str));
    auto result = WidenAllocNullTerm(framework.scratch_arena, str);
    if (!result.HasValue()) Panic("Failed to widen string");
    return result.Value().data;
}

static DynamicArray<wchar_t> WindowText(Allocator& a, HWND window) {
    auto len = GetWindowTextLengthW(window);
    DynamicArray<wchar_t> buffer {a};
    dyn::Resize(buffer, (usize)len);
    int num_copied = GetWindowTextW(window, buffer.data, len + 1);
    dyn::Resize(buffer, (usize)num_copied);
    return buffer;
}

static constexpr UINT k_label_draw_text_flags = DT_LEFT | DT_TOP | DT_WORDBREAK;

static UiSize LabelSize(HWND label, Optional<UiSize> container) {
    auto dc = GetDC(label);
    DEFER { ReleaseDC(label, dc); };

    SelectObject(dc, (HGDIOBJ)SendMessageW(label, WM_GETFONT, 0, 0));

    RECT r {};
    r.right = container ? container->width : LargestRepresentableValue<decltype(r.right)>();
    ArenaAllocatorWithInlineStorage<4096> allocator {Malloc::Instance()};
    auto const text = WindowText(allocator, label);
    DrawTextW(dc, text.data, (int)text.size, &r, DT_CALCRECT | k_label_draw_text_flags);

    return {CheckedCast<u16>(r.right), CheckedCast<u16>(r.bottom)};
}

static UiSize ButtonSize(HWND button) { return ExpandChecked(LabelSize(button, k_nullopt), {20, 10}); }

inline bool ExpandsInDimension(WidgetOptions const& options, int dim) {
    bool const expand[2] = {options.expand_x, options.expand_y};
    return expand[dim];
}

// This function returns the minimum acceptable size of the widget. The caller may decide to use a larger size
// than this.
static UiSize GetSizeAndLayoutChildren(Widget& widget, UiSize max_size_allowed) {
    if (max_size_allowed.height == 0 || max_size_allowed.width == 0) return {};

    auto const total_margins = UiSize {CheckedCast<u16>(widget.options.margins.l + widget.options.margins.r),
                                       CheckedCast<u16>(widget.options.margins.t + widget.options.margins.b)};
    if (widget.children.size) {
        do {
            auto const& container_options = widget.options.type.Get<WidgetOptions::Container>();

            int start_pos[2] = {0, 0};
            auto bounding_box = ReduceClampedToZero(max_size_allowed, total_margins);

            if (container_options.type == WidgetOptions::Container::Type::Tabs) {
                // TabCtrl adds a heading area, so we need to reduce our bounding box so that we don't draw
                // over the heading.
                RECT r {
                    .left = start_pos[0],
                    .top = start_pos[1],
                    .right = bounding_box.width,
                    .bottom = bounding_box.height,
                };
                TabCtrl_AdjustRect(widget.window, FALSE, &r);
                start_pos[0] = r.left;
                start_pos[1] = r.top;
                bounding_box.width = CheckedCast<u16>(r.right - r.left);
                bounding_box.height = CheckedCast<u16>(r.bottom - r.top);
            }

            u16 scrollbar_width = 0;
            if (container_options.has_vertical_scrollbar && widget.scroll_y_visible) {
                SCROLLBARINFO info {.cbSize = sizeof(SCROLLBARINFO)};
                if (GetScrollBarInfo(widget.window, OBJID_VSCROLL, &info))
                    scrollbar_width = CheckedCast<u16>(info.rcScrollBar.right - info.rcScrollBar.left);
                bounding_box.width =
                    (bounding_box.width > scrollbar_width) ? bounding_box.width - scrollbar_width : 0;
            }

            int const dim = (container_options.orientation == Orientation::Horizontal) ? 0 : 1;
            int const other_dim = (container_options.orientation == Orientation::Horizontal) ? 1 : 0;

            struct Child {
                Widget* w {};
                UiSize size {};
            };

            int num_of_expand = 0;
            int used = 0;
            DynamicArrayBounded<Child, Widget::k_max_children> visible_children;
            for (auto [i, c] : Enumerate(widget.children)) {
                if (!IsWindowVisible(c->window)) continue;

                Child child {c};

                if (!ExpandsInDimension(c->options, dim)) {
                    child.size = GetSizeAndLayoutChildren(*c, bounding_box);
                    used += child.size.e[dim];
                } else {
                    ++num_of_expand;
                }

                dyn::Append(visible_children, child);
            }

            UiSize max_contents {};

            {
                auto const size_for_each_expand = CheckedCast<u16>(
                    Max(0,
                        num_of_expand
                            ? ((bounding_box.e[dim] -
                                (container_options.spacing * Max(0, (int)visible_children.size - 1)) - used) /
                               num_of_expand)
                            : 0));

                UiSize size_for_expand_children = bounding_box;
                size_for_expand_children.e[dim] = size_for_each_expand;
                for (auto& c : visible_children) {
                    if (ExpandsInDimension(c.w->options, dim)) {
                        c.size = GetSizeAndLayoutChildren(*c.w, size_for_expand_children);
                        c.size.e[dim] = Max(size_for_each_expand, c.size.e[dim]);
                    }

                    max_contents.e[dim] += c.size.e[dim];
                    max_contents.e[other_dim] = Max(max_contents.e[other_dim], c.size.e[other_dim]);
                }

                max_contents.e[dim] += container_options.spacing * Max(0, (int)visible_children.size - 1);
            }

            UiSize final_contents_size = max_contents;
            if (widget.options.fixed_size)
                final_contents_size = *widget.options.fixed_size;
            else {
                for (auto const i : Range(2))
                    if (ExpandsInDimension(widget.options, i))
                        final_contents_size.e[i] = Max(final_contents_size.e[i], bounding_box.e[i]);
                if (container_options.has_vertical_scrollbar)
                    final_contents_size.height = Min(final_contents_size.height, bounding_box.height);
            }

            for (auto& c : visible_children)
                if (ExpandsInDimension(c.w->options, other_dim))
                    c.size.e[other_dim] = Max(final_contents_size.e[other_dim], c.size.e[other_dim]);

            int pos = start_pos[dim];
            if (container_options.alignment == Alignment::End) {
                if (max_contents.e[dim] > bounding_box.e[dim])
                    pos = start_pos[dim];
                else
                    pos = start_pos[dim] + (bounding_box.e[dim] - max_contents.e[dim]);
            }

            if (container_options.has_vertical_scrollbar) {
                SCROLLINFO info {
                    .cbSize = sizeof(SCROLLINFO),
                    .fMask = SIF_PAGE | SIF_RANGE |
                             ((max_contents.height <= final_contents_size.height) ? SIF_POS : 0u),
                    .nMin = 0,
                    .nMax = max_contents.height,
                    .nPage = (UINT)final_contents_size.height,
                    .nPos = 0,
                };
                auto const current_pos = SetScrollInfo(widget.window, SB_VERT, &info, false);
                (void)current_pos;

                bool showing_scroll = false;
                if (max_contents.height <= final_contents_size.height) {
                    widget.scroll_y = 0;
                } else {
                    showing_scroll = true;
                    start_pos[1] -= widget.scroll_y;
                    if (dim == 1) pos -= widget.scroll_y;
                }

                if (showing_scroll != widget.scroll_y_visible) {
                    // The scrollbar has just appeared/disappear, we need to recalculate the children for this
                    // widget because the scrollbar has changed the area that children must appear.
                    widget.scroll_y_visible = showing_scroll;
                    continue;
                }
            }

            for (auto& c : visible_children) {
                int coord[2] = {start_pos[0], start_pos[1]};
                coord[dim] = pos;
                auto const total_margin_x = c.w->options.margins.l + c.w->options.margins.r;
                auto const total_margin_y = c.w->options.margins.t + c.w->options.margins.b;
                Require(SetWindowPos(c.w->window,
                                     nullptr,
                                     coord[0] + c.w->options.margins.l,
                                     coord[1] + c.w->options.margins.t,
                                     c.size.width - total_margin_x,
                                     c.size.height - total_margin_y,
                                     SWP_NOZORDER | SWP_NOCOPYBITS),
                        "SetWindowPos");
                pos += c.size.e[dim] + container_options.spacing;
            }

            auto result = final_contents_size;
            result = ExpandChecked(result, total_margins);
            result = ExpandChecked(result, {scrollbar_width, 0});
            return result;
        } while (true);
    } else {
        UiSize result {};
        if (widget.options.fixed_size) {
            result = *widget.options.fixed_size;
        } else {
            switch (widget.options.type.tag) {
                case WidgetType::None: break;

                case WidgetType::Hyperlink:
                case WidgetType::Label: {
                    result = LabelSize(widget.window, ReduceClampedToZero(max_size_allowed, total_margins));
                    break;
                }
                case WidgetType::Container: {
                    result = {10, 10};
                    break;
                }
                case WidgetType::RadioButtons:
                case WidgetType::Button: {
                    result = ButtonSize(widget.window);
                    break;
                }
                case WidgetType::Divider: {
                    result = {1, 1};
                    break;
                }
                case WidgetType::ReadOnlyTextbox: {
                    result = {100, 100};
                    break;
                }
                case WidgetType::ProgressBar: {
                    result = {100, 20};
                    break;
                }
                case WidgetType::TextInput: {
                    result = {100, 20};
                    break;
                }
                case WidgetType::CheckboxTable: {
                    result = {100, 100};
                    break;
                }
                case WidgetType::Image: {
                    result = {100, 100};
                    break;
                }
            }
        }
        return ExpandChecked(result, total_margins);
    }
    PanicIfReached();
    return {};
}

static HWND CreatePageWindow(Widget& widget, HWND parent) {
    return CreateWindow(widget,
                        k_page_class_name,
                        nullptr,
                        {0, 0, {100, 100}},
                        WS_CHILD | WS_VISIBLE,
                        0,
                        parent);
}

void RecalculateLayout(GuiFramework& framework) {
    auto const size = WindowContentsSize(framework.root_layout->window);
    GetSizeAndLayoutChildren(*framework.root_layout, size);
}

String GetText(GuiFramework& framework, u32 id) {
    auto wide_text = WindowText(framework.scratch_arena, framework.widgets[id].window);
    framework.buffer.Reserve(512);
    dyn::Assign(framework.buffer, Narrow(framework.scratch_arena, wide_text).ValueOr({}));
    return framework.buffer.Items();
}

constexpr UINT k_timer_id = 1;

void ExitProgram(GuiFramework& framework) {
    KillTimer(framework.root, k_timer_id);
    PostQuitMessage(0);
}

bool AutorunMode(GuiFramework& framework) { return framework.autorun_mode; }

void EditWidget(GuiFramework& framework, u32 id, EditWidgetOptions const& options) {
    framework.scratch_arena.ResetCursorAndConsolidateRegions();
    auto& widget = framework.widgets[id];

    if (options.visible) {
        ShowWindow(widget.window, options.visible.Value() ? SW_SHOW : SW_HIDE);
        GetSizeAndLayoutChildren(*framework.root_layout, WindowContentsSize(framework.root_layout->window));
    }
    if (options.enabled) EnableWindow(widget.window, options.enabled.Value());
    if (options.text) {
        SetWindowTextW(widget.window, NullTermWideString(framework, options.text.Value()));
        if (widget.options.type.tag == WidgetType::Label) InvalidateRect(widget.window, nullptr, true);
    }
    if (options.progress_bar_pulse) {
        if (widget.progress_bar_mode != ProgressBarMode::Marquee) {
            SetWindowLongPtrW(widget.window,
                              GWL_STYLE,
                              GetWindowLongPtrW(widget.window, GWL_STYLE) | PBS_MARQUEE);
            SendMessageW(widget.window, PBM_SETMARQUEE, true, 0);
            widget.progress_bar_mode = ProgressBarMode::Marquee;
        }
    }
    if (options.progress_bar_position) {
        static constexpr int k_progress_bar_max = 100;
        if (widget.progress_bar_mode != ProgressBarMode::Normal) {
            SetWindowLongPtrW(widget.window,
                              GWL_STYLE,
                              GetWindowLongPtrW(widget.window, GWL_STYLE) & ~PBS_MARQUEE);
            SendMessageW(widget.window, PBM_SETMARQUEE, false, 0);
            SendMessageW(widget.window, PBM_SETRANGE, 0, MAKELPARAM(0, k_progress_bar_max));
            SendMessageW(widget.window, PBM_SETSTEP, (WPARAM)1, 0);
            widget.progress_bar_mode = ProgressBarMode::Normal;
        }
        SendMessageW(widget.window,
                     PBM_SETPOS,
                     (WPARAM)(options.progress_bar_position.Value() * k_progress_bar_max),
                     0);
    }
    if (options.clear_checkbox_table) ListView_DeleteAllItems(widget.window);
    if (options.add_checkbox_table_item) {
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = INT_MAX;
        item.pszText = (WCHAR*)L"";
        auto const item_id = ListView_InsertItem(widget.window, &item);
        for (auto [i, col] : Enumerate<int>(options.add_checkbox_table_item.Value().items))
            ListView_SetItemText(widget.window, item_id, i, NullTermWideString(framework, col));
        ListView_SetCheckState(widget.window, item_id, options.add_checkbox_table_item.Value().state);
    }
    if (options.label_style && widget.options.type.tag == WidgetType::Label &&
        widget.label_style != *options.label_style) {
        widget.label_style = *options.label_style;
        switch (widget.label_style) {
            case LabelStyle::Regular:
            case LabelStyle::DullColour:
                SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
                break;
            case LabelStyle::Bold:
                SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.bold_font, TRUE);
                break;
            case LabelStyle::Heading:
                SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.heading_font, TRUE);
                break;
        }
    }
    if (options.simulate_button_press) {
        HWND parent_window = GetParent(widget.window);
        ASSERT(parent_window);
        PostMessageW(parent_window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)widget.window);
    }
}

u32 CreateStackLayoutWidget(GuiFramework& framework, Optional<u32> parent_id, WidgetOptions options) {
    ASSERT_EQ(options.type.tag, WidgetType::Container);
    auto parent = parent_id ? &framework.widgets[*parent_id] : nullptr;
    auto [widget, id] = framework.AllocWidget();
    widget.window = CreatePageWindow(widget, parent ? parent->window : nullptr);
    widget.options = options;
    if (parent) {
        dyn::Append(parent->children, &widget);

        if (auto parent_options = parent->options.type.TryGet<WidgetOptions::Container>()) {
            if (parent_options->type == WidgetOptions::Container::Type::Tabs) {
                auto const& container = options.type.Get<WidgetOptions::Container>();
                ASSERT(container.tab_label.size);
                TCITEMW item {
                    .mask = TCIF_TEXT,
                    .dwState = 0,
                    .dwStateMask = 0,
                    .pszText = NullTermWideString(framework, container.tab_label),
                    .cchTextMax = 0,
                    .iImage = -1,
                    .lParam = 0,
                };
                SendMessageW(parent->window,
                             TCM_INSERTITEM,
                             (WPARAM)parent->children.size,
                             (LPARAM)(uintptr_t)&item);
                ShowWindow(widget.window, parent->children.size == 1 ? SW_SHOW : SW_HIDE);
            }
        }
    }

    return id;
}

u32 CreateWidget(GuiFramework& framework, u32 page, WidgetOptions options) {
    framework.scratch_arena.ResetCursorAndConsolidateRegions();
    // IMPROVE: review usage of WS_TABSTOP
    // https://devblogs.microsoft.com/oldnewthing/20031021-00/?p=42083

    auto& parent = framework.widgets[page];
    ASSERT_EQ(parent.options.type.tag, WidgetType::Container);

    auto [widget, id] = framework.AllocWidget();
    widget.options = options;
    switch (options.type.tag) {
        case WidgetType::None: {
            PanicIfReached();
            break;
        }
        case WidgetType::Hyperlink: {
            auto const& hyperlink = options.type.Get<WidgetOptions::Hyperlink>();
            widget.window = CreateWindow(widget,
                                         WC_LINK,
                                         NullTermWideString(framework,
                                                            fmt::Format(framework.scratch_arena,
                                                                        "<A HREF=\"{}\">{}</A>",
                                                                        hyperlink.url,
                                                                        options.text)),
                                         {.x = 0, .y = 0, .size = {100, 20}},
                                         WS_TABSTOP | WS_CHILD | WS_VISIBLE | SS_LEFT,
                                         0,
                                         parent.window);
            SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
            break;
        }
        case WidgetType::ProgressBar: {
            widget.window = CreateWindow(widget,
                                         PROGRESS_CLASSW,
                                         nullptr,
                                         {.x = 0, .y = 0, .size = {100, 20}},
                                         WS_TABSTOP | WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                         0,
                                         parent.window);
            widget.progress_bar_mode = ProgressBarMode::None;
            break;
        }
        case WidgetType::ReadOnlyTextbox: {
            auto library = LoadLibraryW(L"Msftedit.dll");
            Require(library, "LoadLibrary");

            widget.window =
                CreateWindow(widget,
                             MSFTEDIT_CLASS,
                             NullTermWideString(framework, options.text),
                             {.x = 0, .y = 0, .size = {100, 20}},
                             WS_VSCROLL | ES_READONLY | ES_MULTILINE | WS_VISIBLE | WS_CHILD | WS_TABSTOP,
                             0,
                             parent.window);
            break;
        }
        case WidgetType::TextInput: {
            auto const& text_input = options.type.Get<WidgetOptions::TextInput>();
            widget.window =
                CreateWindow(widget,
                             L"EDIT",
                             nullptr,
                             {.x = 0, .y = 0, .size = {100, 20}},
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (text_input.password ? ES_PASSWORD : 0),
                             WS_EX_CLIENTEDGE,
                             parent.window,
                             id);
            SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
            break;
        }
        case WidgetType::RadioButtons: {
            auto const& button_opts = options.type.Get<WidgetOptions::RadioButtons>();
            widget.window = CreatePageWindow(widget, parent.window);
            widget.options.type = WidgetOptions::Container {};

            for (auto [i, b] : Enumerate<u32>(button_opts.labels)) {
                // IMPROVE: the docs say that the autoradiobutton will group _all_ radio buttons together, so
                // if I have multiple radiobutton groups then there might be strange behaviour.
                // I think I need to create the parent window with WS_GROUP
                // https://learn.microsoft.com/en-us/windows/win32/controls/button-types-and-styles#radio-buttons
                auto [button_widget, button_id] = framework.AllocWidget();
                button_widget.window = CreateWindow(button_widget,
                                                    L"BUTTON",
                                                    NullTermWideString(framework, b),
                                                    {.x = 0, .y = 0, .size = {100, 20}},
                                                    WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON |
                                                        (i == 0 ? WS_GROUP | WS_TABSTOP : 0),
                                                    0,
                                                    widget.window,
                                                    i);
                SendMessageW(button_widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
                if (i == 0) SendMessageW(button_widget.window, BM_SETCHECK, BST_CHECKED, 0);

                button_widget.options.expand_x = true;
                button_widget.options.type = WidgetType::RadioButtons;
                dyn::Append(widget.children, &button_widget);
            }

            break;
        }
        case WidgetType::Button: {
            auto const& button_opts = options.type.Get<WidgetOptions::Button>();
            auto text = options.text;
            if (button_opts.style == ButtonStyle::ExpandCollapseToggle)
                text = fmt::Format(framework.scratch_arena, "{} >>", text);
            widget.window = CreateWindow(widget,
                                         L"BUTTON",
                                         NullTermWideString(framework, text),
                                         {.x = 0, .y = 0, .size = {100, 100}},
                                         WS_TABSTOP | WS_CHILD | WS_VISIBLE |
                                             (button_opts.is_default ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                         0,
                                         parent.window,
                                         id);
            widget.button_style = button_opts.style;
            SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
            break;
        }
        case WidgetType::Label: {
            auto const& label_opts = options.type.Get<WidgetOptions::Label>();
            auto const align_flags = ({
                DWORD r {};
                switch (label_opts.text_alignment) {
                    case TextAlignment::Left: r = SS_LEFT; break;
                    case TextAlignment::Right: r = SS_RIGHT; break;
                    case TextAlignment::Centre: r = SS_CENTER; break;
                }
                r;
            });
            widget.window = CreateWindow(widget,
                                         L"STATIC",
                                         NullTermWideString(framework, options.text),
                                         {.x = 0, .y = 0, .size = {100, 100}},
                                         WS_CHILD | WS_VISIBLE | align_flags,
                                         0,
                                         parent.window);
            widget.label_style = label_opts.style;

            auto font = ({
                HFONT f {};
                switch (label_opts.style) {
                    case LabelStyle::DullColour:
                    case LabelStyle::Regular: f = framework.regular_font; break;
                    case LabelStyle::Bold: f = framework.bold_font; break;
                    case LabelStyle::Heading: f = framework.heading_font; break;
                }
                f;
            });
            SendMessageW(widget.window, WM_SETFONT, (WPARAM)font, TRUE);
            break;
        }
        case WidgetType::Image: {
            auto const& image_opts = options.type.Get<WidgetOptions::Image>();
            widget.window = CreateWindow(widget,
                                         L"STATIC",
                                         nullptr,
                                         {.x = 0, .y = 0, .size = image_opts.size},
                                         WS_CHILD | WS_VISIBLE | SS_BITMAP,
                                         0,
                                         parent.window);
            widget.options.fixed_size = image_opts.size;

            {
                auto dc = GetDC(widget.window);
                auto bitmap = CreateCompatibleBitmap(dc, image_opts.size.width, image_opts.size.height);

                BITMAPINFO bitmap_info {};
                bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmap_info.bmiHeader.biBitCount = 32;
                bitmap_info.bmiHeader.biHeight = -(int)image_opts.size.height;
                bitmap_info.bmiHeader.biWidth = image_opts.size.width;
                bitmap_info.bmiHeader.biPlanes = 1;
                bitmap_info.bmiHeader.biCompression = BI_RGB;

                auto const bgra_data = PageAllocator::Instance().AllocateExactSizeUninitialised<u8>(
                    image_opts.size.width * image_opts.size.height * 4);
                for (auto const i : Range<u32>(image_opts.size.width * image_opts.size.height)) {
                    bgra_data[(i * 4u) + 0] = image_opts.rgba_data[(i * 4u) + 2];
                    bgra_data[(i * 4u) + 1] = image_opts.rgba_data[(i * 4u) + 1];
                    bgra_data[(i * 4u) + 2] = image_opts.rgba_data[(i * 4u) + 0];
                    bgra_data[(i * 4u) + 3] = image_opts.rgba_data[(i * 4u) + 3];
                }
                DEFER { PageAllocator::Instance().Free(bgra_data); };

                SetDIBits(dc,
                          bitmap,
                          0,
                          image_opts.size.height,
                          bgra_data.data,
                          &bitmap_info,
                          DIB_RGB_COLORS);

                SendMessageW(widget.window, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bitmap);

                ReleaseDC(widget.window, dc);
            }
            break;
        }
        case WidgetType::Divider: {
            auto const& divider = options.type.Get<WidgetOptions::Divider>();
            widget.window = CreateWindow(widget,
                                         k_divider_class_name,
                                         nullptr,
                                         {0, 0, {1, 1}},
                                         WS_CHILD | WS_VISIBLE,
                                         0,
                                         parent.window);
            widget.options.expand_x = divider.orientation == Orientation::Horizontal;
            widget.options.expand_y = divider.orientation == Orientation::Vertical;
            break;
        }
        case WidgetType::CheckboxTable: {
            auto const& table_opts = options.type.Get<WidgetOptions::CheckboxTable>();
            widget.window = CreateWindow(widget,
                                         WC_LISTVIEW,
                                         nullptr,
                                         {0, 0, {300, 300}},
                                         WS_VISIBLE | WS_CHILD | LVS_REPORT,
                                         0,
                                         parent.window);
            ListView_SetExtendedListViewStyle(widget.window,
                                              LVS_EX_CHECKBOXES | LVS_EX_GRIDLINES | LVS_EX_INFOTIP |
                                                  LVS_EX_DOUBLEBUFFER);

            for (auto [i, column] : Enumerate(table_opts.columns)) {
                LVCOLUMN col = {};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = NullTermWideString(framework, column.label);
                col.cx = 160;
                ListView_InsertColumn(widget.window, i, &col);
                ListView_SetColumnWidth(widget.window, i, column.default_width);
            }
            SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.regular_font, TRUE);
            break;
        }
        case WidgetType::Container: {
            auto const& cont = options.type.Get<WidgetOptions::Container>();
            switch (cont.type) {
                case WidgetOptions::Container::Type::Tabs: {
                    widget.window = CreateWindow(widget,
                                                 WC_TABCONTROLW,
                                                 nullptr,
                                                 {0, 0, {300, 300}},
                                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                                 0,
                                                 parent.window);
                    SendMessageW(widget.window, WM_SETFONT, (WPARAM)framework.bold_font, TRUE);
                    break;
                }
                case WidgetOptions::Container::Type::Frame: {
                    widget.window = CreatePageWindow(widget, parent.window);
                    break;
                }
                case WidgetOptions::Container::Type::StackLayout: {
                    PanicIfReached();
                    break;
                }
            }
            break;
        }
    }

    dyn::Append(parent.children, &widget);
    return id;
}

static LRESULT CALLBACK RootWindowProc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_SIZE: {
            auto& framework = *(GuiFramework*)GetWindowLongPtrW(window, GWLP_USERDATA);
            auto const new_size = WindowContentsSize(window);
            SetWindowPos(framework.root_layout->window,
                         nullptr,
                         0,
                         0,
                         new_size.width,
                         new_size.height,
                         SWP_NOMOVE | SWP_NOZORDER);
            GetSizeAndLayoutChildren(*framework.root_layout,
                                     WindowContentsSize(framework.root_layout->window));
            return 0;
        }
        case WM_TIMER: {
            if (w_param == k_timer_id) {
                auto& framework = *(GuiFramework*)GetWindowLongPtrW(window, GWLP_USERDATA);
                if (framework.in_timer) break;
                framework.in_timer = true;
                OnTimer(*framework.app, framework);
                framework.in_timer = false;
                return 0;
            }
            break;
        }
    }
    return DefWindowProc(window, msg, w_param, l_param);
}

static LRESULT CALLBACK DividerWindowProc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT paint {};
            auto dc = BeginPaint(window, &paint);
            DEFER { EndPaint(window, &paint); };
            RECT rect {};
            GetClientRect(window, &rect);
            DrawEdge(dc, &rect, EDGE_RAISED, BF_FLAT | BF_TOP | BF_RIGHT);
            return 0;
            break;
        }
    }
    return DefWindowProcW(window, msg, w_param, l_param);
}

static Widget* FindWidget(GuiFramework& framework, HWND window) {
    for (auto [i, w] : Enumerate(framework.widgets))
        if (w.window == window) return &w;
    return nullptr;
}

static LRESULT CALLBACK PageWindowProc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
        case WM_PAINT: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;

            auto const& container = widget.options.type.Get<WidgetOptions::Container>();
            if (container.type == WidgetOptions::Container::Type::Frame) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(window, &ps);
                RECT r;
                GetClientRect(window, &r);
                FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
                EndPaint(window, &ps);
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;

            auto const& container = widget.options.type.Get<WidgetOptions::Container>();
            if (container.has_vertical_scrollbar && widget.scroll_y_visible) {
                auto const delta = GET_WHEEL_DELTA_WPARAM(w_param) / WHEEL_DELTA;
                for (auto d = delta; d != 0; d += ((delta < 0) ? 1 : -1)) {
                    SendMessageW(window, WM_VSCROLL, MAKEWPARAM((delta < 0) ? SB_LINEDOWN : SB_LINEUP, 0), 0);

                    // Redraw scrollbar. 'Paint' messages are always handled after all other messages (see
                    // WM_PAINT docs). We've just added a message above so we know this redraw will happen
                    // after the scroll position has been updated.
                    SCROLLINFO info {.cbSize = sizeof(SCROLLINFO)};
                    SetScrollInfo(widget.window, SB_VERT, &info, true);
                }
                return 0;
            }

            break;
        }
        case WM_VSCROLL: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;

            auto const lo = LOWORD(w_param);
            Optional<int> pos = {};
            if (lo == SB_THUMBPOSITION || lo == SB_THUMBTRACK)
                pos = HIWORD(w_param);
            else if (lo == SB_LINEDOWN)
                pos = widget.scroll_y + 10;
            else if (lo == SB_LINEUP)
                pos = widget.scroll_y - 10;

            if (pos) {
                {
                    SCROLLINFO info {.cbSize = sizeof(SCROLLINFO), .fMask = SIF_RANGE | SIF_PAGE};
                    if (GetScrollInfo(widget.window, SB_VERT, &info)) {
                        if (*pos < info.nMin) *pos = info.nMin;
                        auto const max = info.nMax - ((int)info.nPage - 1);
                        if (*pos > max) *pos = max;
                    }
                }

                widget.scroll_y = *pos;
                SetScrollPos(widget.window, SB_VERT, *pos, false);

                GetSizeAndLayoutChildren(*widget.framework->root_layout,
                                         WindowContentsSize(widget.framework->root_layout->window));
                return 0;
            }

            break;
        }
        case WM_COMMAND: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;
            auto const event = HIWORD(w_param);
            if (event == BN_CLICKED) {
                // l_param is a HWND to the button, but for some reason getting the window style using
                // GetWindowLongPtrW(button, GLW_STYLE) does not return a value that contains the button
                // styles BS_PUSHBUTTON, etc. So instead we're just doing a search through the list of
                // widgets
                Widget* button_widget = FindWidget(*widget.framework, (HWND)l_param);
                if (button_widget) {
                    if (button_widget->options.type.Is<WidgetOptions::RadioButtons>()) {
                        HandleUserInteraction(*widget.framework->app,
                                              *widget.framework,
                                              {
                                                  .type = UserInteraction::Type::RadioButtonSelected,
                                                  .widget_id = widget.id,
                                                  .button_state = true,
                                                  .button_index = LOWORD(w_param),
                                              });
                    } else if (button_widget->options.type.tag == WidgetType::Button) {
                        if (button_widget->button_style == ButtonStyle::ExpandCollapseToggle) {
                            button_widget->button_state = !button_widget->button_state;
                        } else {
                            button_widget->button_state =
                                SendMessageW(button_widget->window, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        }
                        HandleUserInteraction(*widget.framework->app,
                                              *widget.framework,
                                              {
                                                  .type = UserInteraction::Type::ButtonPressed,
                                                  .widget_id = LOWORD(w_param),
                                                  .button_state = button_widget->button_state,
                                              });
                    }
                    return 0;
                }
            }
            if (event == EN_CHANGE) {
                // IMPROVE: handle enter being pressed by subclassing the EDIT control using
                // SetWindowSubclass
                // https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass
                HandleUserInteraction(*widget.framework->app,
                                      *widget.framework,
                                      {
                                          .type = UserInteraction::Type::TextInputChanged,
                                          .widget_id = LOWORD(w_param),
                                          .text = GetText(*widget.framework, LOWORD(w_param)),
                                      });
                return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;

            auto const info = (NMHDR*)l_param;
            switch (info->code) {
                case LVN_ITEMCHANGED: {
                    Widget* checkbox_table = FindWidget(*widget.framework, info->hwndFrom);
                    if (!checkbox_table) break;

                    auto listview = (NMLISTVIEW*)l_param;
                    bool new_state = false;
                    bool changed = false;
                    if (listview->uNewState == 8192) {
                        new_state = true;
                        changed = true;
                    } else if (listview->uNewState == 4096) {
                        new_state = false;
                        changed = true;
                    }
                    if (changed) {
                        ASSERT(listview->iItem >= 0);
                        HandleUserInteraction(*widget.framework->app,
                                              *widget.framework,
                                              {
                                                  .type = UserInteraction::Type::CheckboxTableItemToggled,
                                                  .widget_id = checkbox_table->id,
                                                  .button_state = new_state,
                                                  .button_index = (u32)listview->iItem,
                                              });
                    }
                    break;
                }

                case TCN_SELCHANGE: {
                    Widget* tab_widget = FindWidget(*widget.framework, info->hwndFrom);
                    if (!tab_widget) break;

                    auto const tab_index = TabCtrl_GetCurSel(tab_widget->window);
                    for (auto [i, c] : Enumerate<int>(tab_widget->children))
                        ShowWindow(c->window, i == tab_index ? SW_SHOW : SW_HIDE);
                    GetSizeAndLayoutChildren(*tab_widget, WindowContentsSize(tab_widget->window));
                    break;
                }

                case NM_CLICK:
                case NM_RETURN: {
                    auto hwnd_from = ((LPNMHDR)l_param)->hwndFrom;
                    auto widget_from = FindWidget(*widget.framework, hwnd_from);
                    if (widget_from && widget_from->options.type.tag == WidgetType::Hyperlink) {
                        auto nm_link = (PNMLINK)l_param;
                        LITEM item = nm_link->item;
                        if (item.iLink == 0)
                            ShellExecuteW(nullptr, L"open", item.szUrl, nullptr, nullptr, SW_SHOW);
                    }
                    break;
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            auto& widget = *(Widget*)GetWindowLongPtrW(window, GWLP_USERDATA);
            if (!widget.framework->app) break;

            auto static_widget = FindWidget(*widget.framework, (HWND)l_param);
            if (!static_widget) break;

            auto hdc_static = (HDC)w_param;

            if (static_widget->options.type.tag == WidgetType::Label) {
                switch (static_widget->label_style) {
                    case LabelStyle::DullColour: SetTextColor(hdc_static, RGB(140, 140, 140)); break;
                    case LabelStyle::Regular:
                    case LabelStyle::Heading:
                    case LabelStyle::Bold: SetTextColor(hdc_static, RGB(0, 0, 0)); break;
                }
            }
            auto const bk_colour = RGB(255, 255, 255);
            SetBkColor(hdc_static, bk_colour);

            if (widget.framework->static_background_brush == nullptr)
                widget.framework->static_background_brush = CreateSolidBrush(bk_colour);

            return (INT_PTR)widget.framework->static_background_brush;
        }
    }
    return DefWindowProcW(window, msg, w_param, l_param);
}

enum class InstallerCliArgs { Autorun, Count };

auto constexpr k_cli_arg_defs = MakeCommandLineArgDefs<InstallerCliArgs>({
    {
        .id = (u32)InstallerCliArgs::Autorun,
        .key = "autorun",
        .description = "Automatically run with default options",
        .value_type = "flag",
        .required = false,
        .num_values = 0,
    },
});

static ErrorCodeOr<int> Main(HINSTANCE h_instance, int cmd_show, ArgsCstr args) {
    GlobalInit({.init_error_reporting = true, .set_main_thread = true});
    DEFER { GlobalDeinit({.shutdown_error_reporting = true}); };

    auto const config = GetAppConfig();

    ArenaAllocatorWithInlineStorage<1024> arena {PageAllocator::Instance()};

    auto const cli_args = TRY(ParseCommandLineArgsStandard(arena,
                                                           args,
                                                           k_cli_arg_defs,
                                                           {
                                                               .handle_help_option = true,
                                                               .print_usage_on_error = true,
                                                               .description = config.description,
                                                               .version = FLOE_VERSION_STRING,
                                                           }));

    constexpr INITCOMMONCONTROLSEX k_init_cc {.dwSize = sizeof(INITCOMMONCONTROLSEX),
                                              .dwICC = ICC_LINK_CLASS};
    InitCommonControlsEx(&k_init_cc);

    GuiFramework framework {};
    framework.autorun_mode = cli_args[(u32)InstallerCliArgs::Autorun].was_provided;

    if (auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE); hr != S_OK)
        return HresultErrorCode(hr, "CoInitialiseEx");

    constexpr auto k_root_window_class_name = L"floe-root";
    {
        const WNDCLASSEXW wc {
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = RootWindowProc,
            .hInstance = h_instance,
            // We use rcedit.exe to embed the icon resource; it uses 0 for this.
            .hIcon = LoadIconW(h_instance, MAKEINTRESOURCE(0)),
            .hCursor = LoadCursor(nullptr, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
            .lpszClassName = k_root_window_class_name,
        };
        if (RegisterClassExW(&wc) == 0) return Win32ErrorCode(GetLastError(), "RegisterClassExW");
    }
    {
        const WNDCLASSEXW wc = {
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = PageWindowProc,
            .hInstance = h_instance,
            .hCursor = LoadCursor(nullptr, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
            .lpszClassName = k_page_class_name,
        };
        if (RegisterClassExW(&wc) == 0) return Win32ErrorCode(GetLastError(), "RegisterClassExW");
    }
    {
        const WNDCLASSEXW wc = {
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = DividerWindowProc,
            .hInstance = h_instance,
            .hCursor = LoadCursor(nullptr, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
            .lpszClassName = k_divider_class_name,
        };
        if (RegisterClassExW(&wc) == 0) return Win32ErrorCode(GetLastError(), "RegisterClassExW");
    }

    auto win32_create_font = [](int height, bool bold, wchar_t const* name) {
        auto result = CreateFontW(height,
                                  0,
                                  0,
                                  0,
                                  bold ? FW_BOLD : FW_REGULAR,
                                  FALSE,
                                  FALSE,
                                  FALSE,
                                  ANSI_CHARSET,
                                  OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH,
                                  name);
        if (result == nullptr) {
            // try to get a default font (don't specify the font name)
            result = CreateFontW(height,
                                 0,
                                 0,
                                 0,
                                 bold ? FW_BOLD : FW_REGULAR,
                                 FALSE,
                                 FALSE,
                                 FALSE,
                                 ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH,
                                 nullptr);
        }
        return result;
    };

    framework.regular_font = win32_create_font(16, false, L"Tahoma");
    framework.bold_font = win32_create_font(16, true, L"Tahoma");
    framework.heading_font = win32_create_font(24, false, L"Tahoma");

    if (framework.regular_font == nullptr || framework.heading_font == nullptr ||
        framework.bold_font == nullptr)
        CheckLastError("Failed to get font");

    framework.root = CreateWindowExW(WS_EX_CLIENTEDGE,
                                     k_root_window_class_name,
                                     config.window_title,
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                         WS_MAXIMIZEBOX | WS_SIZEBOX,
                                     CW_USEDEFAULT,
                                     CW_USEDEFAULT,
                                     config.window_width,
                                     config.window_height,
                                     nullptr,
                                     nullptr,
                                     h_instance,
                                     nullptr);
    if (!framework.root) return Win32ErrorCode(GetLastError(), "CreateWindowEx");
    {
        SetLastError(0);
        SetWindowLongPtrW(framework.root, GWLP_USERDATA, (LONG_PTR)&framework);
        CheckLastError("SetWindowLongPtrW");
    }

    auto [root_layout, root_layout_id] = framework.AllocWidget();
    framework.root_layout = &root_layout;
    root_layout.window = CreatePageWindow(root_layout, framework.root);
    root_layout.options.debug_name = "Root";
    root_layout.options.type = WidgetOptions::Container {
        .orientation = Orientation::Vertical,
        .alignment = Alignment::Start,
    };
    auto const root_size = WindowContentsSize(framework.root);
    SetWindowPos(root_layout.window,
                 nullptr,
                 0,
                 0,
                 root_size.width,
                 root_size.height,
                 SWP_NOMOVE | SWP_NOZORDER);

    ShowWindow(framework.root, cmd_show);
    UpdateWindow(framework.root);

    framework.app = CreateApplication(framework, root_layout_id);

    SetTimer(framework.root, k_timer_id, k_timer_ms, nullptr);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return DestroyApplication(*framework.app, framework);
}

int WINAPI WinMain(HINSTANCE h_instance, HINSTANCE, LPSTR, int cmd_show) {
    bool attached = false;
    struct ConsoleMode {
        DWORD output_handle;
        Optional<DWORD> original_mode;
    };
    auto console_modes = ArrayT<ConsoleMode>({
        {.output_handle = STD_OUTPUT_HANDLE},
        {.output_handle = STD_ERROR_HANDLE},
    });
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        attached = true;
        for (auto& cm : console_modes) {
            DWORD starting_mode {};
            auto handle = GetStdHandle(cm.output_handle);
            if (GetConsoleMode(handle, &starting_mode)) {
                // enable converting \n to \r\n
                auto new_mode = starting_mode;
                new_mode &= ~(DWORD)DISABLE_NEWLINE_AUTO_RETURN;
                if (SetConsoleMode(handle, new_mode)) cm.original_mode = starting_mode;
            }
        }
    }
    DEFER {
        for (auto& cm : console_modes)
            if (cm.original_mode.HasValue()) {
                auto handle = GetStdHandle(cm.output_handle);
                SetConsoleMode(handle, cm.original_mode.Value());
            }
        if (attached) FreeConsole();
    };

    if (auto const o = Main(h_instance, cmd_show, ArgsCstr {__argc, __argv}); o.HasValue()) {
        return o.Value();
    } else {
        PanicF(SourceLocation::Current(), "{}", o.Error());
        return 1;
    }
}
