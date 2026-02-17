// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_viewport_utils.hpp"

#include "gui_framework/gui_live_edit.hpp"

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
                    else if (imgui.IsActive(b->id, {}))
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
