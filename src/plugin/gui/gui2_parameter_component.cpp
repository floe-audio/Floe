// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_parameter_component.hpp"

#include "gui/gui_draw_knob.hpp"
#include "processor/param.hpp"
#include "processor/processor.hpp"

enum class LayoutType { Generic, Layer, Effect };

f32 MaxStringLength(imgui::Context& imgui, void* items, int num, String (*GetStr)(void* items, int index)) {
    return imgui.LargestStringWidth(0, items, num, GetStr);
}

f32 MaxStringLength(imgui::Context& imgui, Span<String const> strs) {
    return imgui.LargestStringWidth(0, strs);
}

// IMPROVE: this is WAY too complicated and messy. It's code pasted from the old GUI system.

Box DoParameterComponent(GuiBoxSystem& builder,
                         Box parent,
                         Parameter const& param,
                         AudioProcessor& processor,
                         ParameterComponentOptions const& options) {
    auto& imgui = builder.imgui;

    auto live_size = [&](UiSizeId id) { return builder.imgui.PixelsToVw(LiveSize(builder.imgui, id)); };

    auto const type = param.info.IsLayerParam()
                          ? LayoutType::Layer
                          : (param.info.IsEffectParam() ? LayoutType::Effect : LayoutType::Generic);

    auto width = type == LayoutType::Layer
                     ? live_size(UiSizeId::ParamComponentLargeWidth)
                     : (type == LayoutType::Effect ? live_size(UiSizeId::ParamComponentSmallWidth)
                                                   : live_size(UiSizeId::ParamComponentExtraSmallWidth));
    auto const starting_width = width;
    auto height = width - live_size(UiSizeId::ParamComponentHeightOffset);
    auto const starting_height = height;
    auto gap_x = options.size_index_for_gapx ? live_size(*options.size_index_for_gapx)
                                             : live_size(UiSizeId::ParamComponentMarginLR);
    ASSERT(gap_x >= 0.0f);
    auto gap_bottom = live_size(UiSizeId::ParamComponentMarginB);
    auto gap_top = live_size(UiSizeId::ParamComponentMarginT);

    auto const index_for_menu_items =
        param.info.value_type == ParamValueType::Menu ? Optional<ParamIndex> {param.info.index} : k_nullopt;

    auto const param_popup_button_height = live_size(UiSizeId::ParamPopupButtonHeight);

    if (index_for_menu_items) {
        auto const menu_items = ParameterMenuItems(*index_for_menu_items);
        auto strings_width =
            MaxStringLength(imgui, menu_items) + (live_size(UiSizeId::MenuButtonTextMarginL) * 2);
        auto const btn_w = live_size(UiSizeId::NextPrevButtonSize);
        auto const margin_r = live_size(UiSizeId::ParamIntButtonMarginR);
        strings_width += btn_w * 2 + margin_r;
        width = strings_width;
        height = param_popup_button_height;
    }

    if (options.set_gapx_independent_of_size && width != starting_width)
        gap_x = Max(0.0f, gap_x - ((width - starting_width) / 2));

    if (options.set_bottom_gap_independent_of_size && height != starting_height) {
        auto const delta = Max(0.0f, starting_height - height);
        gap_bottom += delta / 2;
        gap_top += delta / 2;
    }

    layout::Margins margins {.b = live_size(UiSizeId::ParamComponentLabelGapY)};

    if (param.info.value_type == ParamValueType::Int) {
        width = live_size(UiSizeId::FXDraggerWidth);
        height = live_size(UiSizeId::FXDraggerHeight);
        margins.t += live_size(UiSizeId::FXDraggerMarginT);
        margins.b += live_size(UiSizeId::FXDraggerMarginB);
    }

    Optional<f32> new_val {};
    auto val = param.NormalisedLinearValue();

    auto const container =
        DoBox(builder,
              {
                  .parent = parent,
                  .layout {
                      .size = layout::k_hug_contents,
                      .margins {.lr = gap_x, .t = gap_top, .b = gap_bottom},
                      .contents_direction = layout::Direction::Column,
                      .contents_align = layout::Alignment::Start,
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      if (options.override_tooltip.size) return options.override_tooltip;
                      auto const str = param.info.LinearValueToString(param.LinearValue());
                      ASSERT(str);

                      DynamicArray<char> buf {builder.arena};
                      fmt::Append(buf, "{}: {}\n{}", param.info.name, str.Value(), param.info.tooltip);
                      if (param.info.value_type == ParamValueType::Int)
                          fmt::Append(buf, ". Drag to edit or double-click to type a value");

                      return buf.ToOwnedSpan();
                  }},
                  .knob_behaviour = !options.is_fake,
                  .knob_percent = val,
              });

    if (!__builtin_isnan(container.knob_percent)) {
        val = container.knob_percent;
        new_val = MapFrom01(val, param.info.linear_range.min, param.info.linear_range.max);
    }

    if (!(param.info.flags.not_automatable)) {
        if (AdditionalClickBehaviour(builder,
                                     container,
                                     {.right_mouse = true, .triggers_on_mouse_up = true})) {
            // TODO: MIDI learn menu
        }
    }

    if (builder.imgui.WasJustActivated(container.imgui_id))
        ParameterJustStartedMoving(processor, param.info.index);
    if (new_val) SetParameterValue(processor, param.info.index, *new_val, {});
    if (builder.imgui.WasJustDeactivated(container.imgui_id))
        ParameterJustStoppedMoving(processor, param.info.index);

    if (auto const r = BoxRect(builder, container)) {
        if (param.info.value_type == ParamValueType::Float && container.is_active) {
            auto const str = param.info.LinearValueToString(param.LinearValue());

            auto const abs_pos = imgui.WindowPosToScreenPos(r->pos);

            auto const font = imgui.graphics->context->CurrentFont();

            auto const size = draw::GetTextSize(font, *str);
            auto const pad_x = LiveSize(imgui, UiSizeId::TooltipPadX);
            auto const pad_y = LiveSize(imgui, UiSizeId::TooltipPadY);

            Rect popup_r;
            popup_r.x = abs_pos.x + (r->w / 2) - (size.x / 2 + pad_x);
            popup_r.y = abs_pos.y + r->h;
            popup_r.w = size.x + pad_x * 2;
            popup_r.h = size.y + pad_y * 2;

            popup_r.pos = imgui::BestPopupPos(popup_r,
                                              {.pos = abs_pos, .size = r->size},
                                              imgui.frame_input.window_size.ToFloat2(),
                                              false);

            f32x2 text_start;
            text_start.x = popup_r.x + pad_x;
            text_start.y = popup_r.y + pad_y;

            draw::DropShadow(imgui, popup_r);
            imgui.overlay_graphics.AddRectFilled(popup_r.Min(),
                                                 popup_r.Max(),
                                                 LiveCol(imgui, UiColMap::TooltipBack),
                                                 LiveSize(imgui, UiSizeId::CornerRounding));
            imgui.overlay_graphics.AddText(text_start, LiveCol(imgui, UiColMap::TooltipText), *str);
        }
    }

    // TODO: behaviour for 'set value' via a menu rather than just double-click
    // auto const display_string = param.info.LinearValueToString(val).ReleaseValueOr({});
    //
    // if (g->param_text_editor_to_open && *g->param_text_editor_to_open == param.info.index) {
    //     g->param_text_editor_to_open.Clear();
    //     g->imgui.SetTextInputFocus(id, display_string, false);
    // }

    auto const control = DoBox(builder,
                               {
                                   .parent = container,
                                   .layout {
                                       .size = {width, height},
                                       .margins = margins,
                                   },
                               });

    if (auto const r = BoxRect(builder, control)) {
        DrawKnob(builder.imgui,
                 container.imgui_id,
                 *r,
                 val,
                 {
                     .highlight_col = style::Col(options.knob_highlight_col),
                     .line_col = style::Col(options.knob_line_col),
                     .overload_position = param.info.display_format == ParamDisplayFormat::VolumeAmp
                                              ? param.info.LineariseValue(1, true)
                                              : k_nullopt,
                     .greyed_out = options.greyed_out,
                     .is_fake = options.is_fake,
                 });
    }

    DoBox(builder,
          {
              .parent = container,
              .text = param.info.gui_label,
              .text_colours = {options.greyed_out ? style::Colour::DarkModeSubtext0
                                                  : style::Colour::DarkModeText},
              .text_align_x = TextAlignX::Centre,
              .text_align_y = TextAlignY::Centre,
              .layout {
                  .size = {width, style::k_font_body_size},
              },
          });

    return control;
}
