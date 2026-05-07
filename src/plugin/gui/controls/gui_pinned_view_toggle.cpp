// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/controls/gui_pinned_view_toggle.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_constants.hpp"

void DoPinnedViewToggle(GuiState& g, Box parent) {
    auto const viewing_pinned = ViewingPinnedSnapshot(g.engine);
    auto const modified = StateModifiedFromPinned(g.engine);
    auto const has_comparison = viewing_pinned || modified;
    auto const original_selected = viewing_pinned || !modified;

    auto const left_label = "Original"_s;
    auto const right_label = "Modified"_s;
    auto const segment_width = 70.0f;
    auto const height = k_mid_button_height;
    auto const font_size_scale = 1.0f;

    constexpr f32 k_track_padding = 2;
    auto const scale = [&](u32 a) { return (u8)((a * (has_comparison ? 255u : 95u)) / 255u); };

    auto const track =
        DoBox(g.builder,
              {
                  .parent = parent,
                  .background_fill_colours = has_comparison
                                                 ? Colours {ColSet {
                                                       .base = Col {.c = Col::White, .alpha = scale(18)},
                                                       .hot = Col {.c = Col::White, .alpha = scale(26)},
                                                       .active = Col {.c = Col::White, .alpha = scale(34)},
                                                   }}
                                                 : Colours {Col {.c = Col::White, .alpha = scale(18)}},
                  .round_background_corners = 0b1111,
                  .corner_rounding = k_corner_rounding,
                  .layout {
                      .size = {segment_width * 2 + k_track_padding * 2, height},
                      .contents_padding = {.lr = k_track_padding, .tb = k_track_padding},
                      .contents_direction = layout::Direction::Row,
                  },
                  .tooltip = has_comparison
                                 ? TooltipString {"Switch between the original state and your modifications. "
                                                  "Editing while viewing the original discards your "
                                                  "modifications."_s}
                                 : TooltipString {k_nullopt},
                  .button_behaviour =
                      has_comparison ? Optional<imgui::ButtonConfig> {imgui::ButtonConfig {}} : k_nullopt,
              });

    auto const do_segment = [&](String label, bool selected, u64 segment_id) {
        auto const cell = DoBox(
            g.builder,
            {
                .parent = track,
                .id_extra = segment_id,
                .background_fill_colours = selected ? Colours {Col {.c = Col::White, .alpha = scale(40)}}
                                                    : Colours {Col {.c = Col::None}},
                .parent_dictates_hot_and_active = true,
                .round_background_corners = 0b1111,
                .corner_rounding = k_corner_rounding * 0.85f,
                .layout {
                    .size = {segment_width, layout::k_fill_parent},
                    .contents_align = layout::Alignment::Middle,
                    .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                },
            });
        DoBox(g.builder,
              {
                  .parent = cell,
                  .text = label,
                  .size_from_text = true,
                  .font = FontType::Body,
                  .font_size = font_size_scale != 1.0f ? k_font_body_size * font_size_scale : 0,
                  .text_colours = Col {.c = Col::White, .alpha = scale(selected ? 240 : 130)},
                  .parent_dictates_hot_and_active = true,
              });
    };

    do_segment(left_label, original_selected, 0);
    do_segment(right_label, !original_selected, 1);

    if (track.button_fired) TogglePinnedView(g.engine);
}
