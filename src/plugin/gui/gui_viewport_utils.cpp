// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_viewport_utils.hpp"

#include "gui_drawing_helpers.hpp"
#include "gui_framework/gui_live_edit.hpp"

static void DrawPopupBackground(imgui::Context const& imgui) {
    auto const rounding = LiveSize(UiSizeId::PopupViewportRounding);
    auto const window_r = imgui.curr_viewport->unpadded_bounds;
    DrawDropShadow(imgui, window_r, rounding);
    imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::PopupViewportBack), rounding);
    imgui.draw_list->AddRect(window_r, LiveCol(UiColMap::PopupViewportOutline), rounding);
}

static void DrawPopupScrollbars(imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
    for (auto const b : bars) {
        if (!b) continue;
        imgui.draw_list->AddRectFilled(b->strip, LiveCol(UiColMap::PopupScrollbarBack));
        u32 handle_col = LiveCol(UiColMap::PopupScrollbarHandle);
        if (imgui.IsHotOrActive(b->id)) handle_col = LiveCol(UiColMap::PopupScrollbarHandleHover);
        imgui.draw_list->AddRectFilled(b->handle, handle_col, LiveSize(UiSizeId::CornerRounding));
    }
}

imgui::ViewportConfig FloeMenuConfig(imgui::Context const&) {
    return {
        .mode = imgui::ViewportMode::PopupMenu,
        .positioning = imgui::ViewportPositioning::AutoPosition,
        .draw_background = DrawPopupBackground,
        .draw_scrollbars = DrawPopupScrollbars,
        .padding = {.lr = 1, .tb = LiveSize(UiSizeId::PopupViewportRounding)},
        .scrollbar_padding = 4,
        .scrollbar_width = LiveSize(UiSizeId::ScrollbarWidth),
        .auto_size = true,
    };
}

imgui::ViewportConfig FloeStandardConfig(imgui::Context const& imgui,
                                         imgui::DrawViewportBackgroundFunction draw_background) {
    return {
        .draw_background = draw_background.CloneObject(imgui.scratch_arena),
        .draw_scrollbars =
            [](imgui::Context const& imgui, imgui::ViewportScrollbars const& bars) {
                for (auto const b : bars) {
                    if (!b) continue;
                    auto const rounding = LiveSize(UiSizeId::CornerRounding);
                    imgui.draw_list->AddRectFilled(b->strip, LiveCol(UiColMap::ScrollbarBack), rounding);
                    u32 handle_col = LiveCol(UiColMap::ScrollbarHandle);
                    if (imgui.IsHot(b->id))
                        handle_col = LiveCol(UiColMap::ScrollbarHandleHover);
                    else if (imgui.IsActive(b->id))
                        handle_col = LiveCol(UiColMap::ScrollbarHandleActive);
                    imgui.draw_list->AddRectFilled(b->handle, handle_col, rounding);
                }
            },
        .padding = {},
        .scrollbar_padding = 4,
        .scrollbar_width = LiveSize(UiSizeId::ScrollbarWidth),
        .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                 imgui::ViewportScrollbarVisibility::Auto},
    };
}
