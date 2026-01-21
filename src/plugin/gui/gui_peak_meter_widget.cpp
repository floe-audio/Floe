// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_peak_meter_widget.hpp"

#include "foundation/foundation.hpp"

#include "gui.hpp"
#include "gui_framework/gui_live_edit.hpp"

namespace peak_meters {

static void DrawPeakMeters(imgui::Context const& imgui, Rect r, f32x2 v, bool did_clip) {
    auto const gap = LiveSize(imgui, UiSizeId::PeakMeterGap);
    auto const marker_w = LiveSize(imgui, UiSizeId::PeakMeterMarkerWidth);
    auto const marker_pad = LiveSize(imgui, UiSizeId::PeakMeterMarkerPad);
    auto padded_r = Rect {.x = r.x + marker_w, .y = r.y, .w = r.w - (marker_w * 2), .h = r.h};
    auto w = (padded_r.w / 2) - (gap / 2);

    constexpr f32 k_max_db = 10;
    constexpr f32 k_min_db = -60;
    constexpr f32 k_min_amp = constexpr_math::Powf(10, k_min_db / 20);

    auto const rounding = LiveSize(imgui, UiSizeId::CornerRounding);

    {
        {
            auto l_channel = padded_r;
            l_channel.w = w;
            imgui.draw_list->AddRectFilled(l_channel, LiveCol(imgui, UiColMap::PeakMeterBack), rounding);
        }
        {
            auto r_channel = padded_r;
            r_channel.x += w + gap;
            r_channel.w = w;
            imgui.draw_list->AddRectFilled(r_channel, LiveCol(imgui, UiColMap::PeakMeterBack), rounding);
        }

        auto draw_marker = [&](f32 db, bool bold) {
            f32 const pos = MapTo01(db, k_min_db, k_max_db);
            auto const line_y = padded_r.y + ((1 - pos) * padded_r.h);
            imgui.draw_list->AddLine({r.x, line_y},
                                     {r.x + (marker_w - marker_pad), line_y},
                                     bold ? LiveCol(imgui, UiColMap::PeakMeterMarkersBold)
                                          : LiveCol(imgui, UiColMap::PeakMeterMarkers));
            imgui.draw_list->AddLine({r.Right() - (marker_w - marker_pad), line_y},
                                     {r.Right(), line_y},
                                     bold ? LiveCol(imgui, UiColMap::PeakMeterMarkersBold)
                                          : LiveCol(imgui, UiColMap::PeakMeterMarkers));
        };

        draw_marker(0, true);
        draw_marker(-12, false);
        draw_marker(-24, false);
        draw_marker(-36, false);
        draw_marker(-48, false);
    }

    auto const clamped_v = Max(v, f32x2(k_min_amp)); // Ensure we don't Log10 zero.
    auto const v_db = 20 * Log10(clamped_v);
    auto const v_percieved = Clamp<f32x2>(MapTo01Unchecked<f32x2>(v_db, k_min_db, k_max_db), 0, 1);
    auto const pixels = v_percieved * padded_r.h;
    auto const level_y_pos = padded_r.y + (padded_r.h - pixels);

    auto l_r = padded_r;
    l_r.y = level_y_pos[0];
    l_r.w = w;
    l_r.SetBottomByResizing(padded_r.Bottom());

    auto r_r = padded_r;
    r_r.x += w + gap;
    r_r.y = level_y_pos[1];
    r_r.w = w;
    r_r.SetBottomByResizing(padded_r.Bottom());

    Array<Rect, 2> const channel_rs = {l_r, r_r};

    auto const top_segment_line = padded_r.y + ((1 - MapTo01(0.0f, k_min_db, k_max_db)) * padded_r.h);
    auto const mid_segment_line = padded_r.y + ((1 - MapTo01(-12.0f, k_min_db, k_max_db)) * padded_r.h);
    for (auto& chan_r : channel_rs) {
        if (chan_r.h < 1) continue;

        if (chan_r.y < top_segment_line) {
            auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightTop);
            if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
            imgui.draw_list->AddRectFilled(chan_r.Min(), chan_r.Max(), col);
        }

        if (chan_r.y < mid_segment_line) {
            auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightMiddle);
            if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
            auto const top = Max(chan_r.y, top_segment_line);
            imgui.draw_list->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col);
        }

        auto col = LiveCol(imgui, UiColMap::PeakMeterHighlightBottom);
        if (did_clip) col = LiveCol(imgui, UiColMap::PeakMeterClipping);
        auto const top = Max(chan_r.y, mid_segment_line);
        imgui.draw_list->AddRectFilled(f32x2 {chan_r.x, top}, chan_r.Max(), col, rounding, 4 | 8);
    }
}

void PeakMeter(Gui* g, Rect r, StereoPeakMeter const& level, bool flash_when_clipping) {
    auto const snapshot = level.GetSnapshot();
    DrawPeakMeters(g->imgui,
                   g->imgui.GetRegisteredAndConvertedRect(r),
                   snapshot.levels,
                   flash_when_clipping && level.DidClipRecently());
}
void PeakMeter(Gui* g, layout::Id lay_id, StereoPeakMeter const& level, bool flash_when_clipping) {
    PeakMeter(g, layout::GetRect(g->layout, lay_id), level, flash_when_clipping);
}

} // namespace peak_meters
