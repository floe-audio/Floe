// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_drawing_helpers.hpp"

#include <float.h>

#include "foundation/foundation.hpp"

#include "gui_framework/colours.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"

namespace draw {

void DropShadow(imgui::Context const& imgui, Rect r, Optional<f32> rounding_opt) {
    auto const rounding = rounding_opt ? *rounding_opt : LiveSize(imgui, UiSizeId::CornerRounding);
    auto const blur = LiveSize(imgui, UiSizeId::WindowDropShadowBlur);
    imgui.draw_list->AddDropShadow(r.Min(),
                                   r.Max(),
                                   LiveCol(imgui, UiColMap::WindowDropShadow),
                                   blur,
                                   rounding);
}

f32x2 GetTextSize(graphics::Font* font, String str, Optional<f32> wrap_width) {
    auto const result = font->CalcTextSizeA(font->font_size, FLT_MAX, wrap_width.ValueOr(0), str);
    return {result.x, result.y};
}

f32 GetTextWidth(graphics::Font* font, String str, Optional<f32> wrap_width) {
    return GetTextSize(font, str, wrap_width).x;
}

void VoiceMarkerLine(imgui::Context const& imgui,
                     f32x2 pos,
                     f32 height,
                     f32 left_min,
                     Optional<Line> upper_line_opt,
                     f32 opacity) {
    {

        constexpr f32 k_tail_size_max = 10;
        f32 const tail_size = Min(pos.x - left_min, k_tail_size_max);

        if (tail_size > 1) {
            auto const aa = imgui.draw_list->renderer->fill_anti_alias;
            imgui.draw_list->renderer->fill_anti_alias = false;
            auto const darkened_col =
                colours::ChangeBrightness(LiveCol(imgui, UiColMap::Waveform_LoopVoiceMarkers), 0.7f);
            auto const col = colours::WithAlpha(darkened_col, (u8)MapFrom01(opacity, 10, 40));
            auto const transparent_col = colours::WithAlpha(darkened_col, 0);

            if (upper_line_opt) {
                auto& upper_line = *upper_line_opt;
                auto p0 = upper_line.IntersectionWithVerticalLine(pos.x - tail_size).ValueOr(upper_line.a);
                auto p1 = pos;
                auto p2 = pos + f32x2 {0, height};
                auto p3 = pos + f32x2 {p0.x - pos.x, height};

                imgui.draw_list
                    ->AddQuadFilledMultiColor(p0, p1, p2, p3, transparent_col, col, col, transparent_col);
            } else {
                auto left = Max(left_min, pos.x - tail_size);

                imgui.draw_list->AddRectFilledMultiColor({left, pos.y},
                                                         pos + f32x2 {0, height},
                                                         transparent_col,
                                                         col,
                                                         col,
                                                         transparent_col);
            }

            imgui.draw_list->renderer->fill_anti_alias = aa;
        }
    }

    {
        auto const aa = imgui.draw_list->renderer->anti_aliased_lines;
        imgui.draw_list->renderer->anti_aliased_lines = false;
        auto const col =
            colours::WithAlpha(LiveCol(imgui, UiColMap::Waveform_LoopVoiceMarkers), (u8)(opacity * 255.0f));

        imgui.draw_list->AddLine(pos, pos + f32x2 {0, height}, col);
        imgui.draw_list->renderer->anti_aliased_lines = aa;
    }
}

} // namespace draw
