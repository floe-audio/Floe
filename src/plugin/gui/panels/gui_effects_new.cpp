// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <IconsFontAwesome6.h>
#include <float.h>

#include "os/threading.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/effect.hpp"

constexpr auto k_reverb_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Reverb},
    .skip = ParamIndex::ReverbOn,
}>();

constexpr auto k_phaser_params = ComptimeParamSearch<ComptimeParamSearchOptions {
    .modules = {ParameterModule::Effect, ParameterModule::Phaser},
    .skip = ParamIndex::PhaserOn,
}>();

struct FXColours {
    UiColMap back;
    UiColMap highlight;
    UiColMap button;
};

static FXColours GetFxColMap(EffectType type) {
    using enum UiColMap;
    switch (type) {
        case EffectType::Distortion: return {DistortionBack, DistortionHighlight, DistortionButton};
        case EffectType::BitCrush: return {BitCrushBack, BitCrushHighlight, BitCrushButton};
        case EffectType::Compressor: return {CompressorBack, CompressorHighlight, CompressorButton};
        case EffectType::FilterEffect: return {FilterBack, FilterHighlight, FilterButton};
        case EffectType::StereoWiden: return {StereoBack, StereoHighlight, StereoButton};
        case EffectType::Chorus: return {ChorusBack, ChorusHighlight, ChorusButton};
        case EffectType::Reverb: return {ReverbBack, ReverbHighlight, ReverbButton};
        case EffectType::Delay: return {DelayBack, DelayHighlight, DelayButton};
        case EffectType::ConvolutionReverb: return {ConvolutionBack, ConvolutionHighlight, ConvolutionButton};
        case EffectType::Phaser: return {PhaserBack, PhaserHighlight, PhaserButton};
        case EffectType::Count: PanicIfReached();
    }
    return {};
}

static void DoWhitespace(GuiBuilder& builder, Box parent, f32 height, u64 loc_hash = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .layout {.size = {1, height}},
          },
          loc_hash);
}

static Box DoDivider(GuiState& g, Box parent, u64 loc_hash = SourceLocationHash()) {
    auto const divider = DoBox(g.builder,
                               {
                                   .parent = parent,
                                   .layout {
                                       .size = {layout::k_fill_parent, 1},
                                   },
                               },
                               loc_hash);
    if (auto const r = BoxRect(g.builder, divider)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddLine({window_r.x, window_r.Bottom()},
                                   {window_r.Right(), window_r.Bottom()},
                                   LiveCol(UiColMap::MidViewportDivider));
    }
    return divider;
}

static void DoDividerColoured(GuiState& g, Box divider, u32 col) {
    if (auto const r = BoxRect(g.builder, divider)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddLine({window_r.x, window_r.Bottom()},
                                   {window_r.Right(), window_r.Bottom()},
                                   col);
    }
}

static void DoKnobJoiningLine(GuiState& g, Box knob1, Box knob2) {
    if (auto const r1 = BoxRect(g.builder, knob1)) {
        if (auto const r2 = BoxRect(g.builder, knob2)) {
            auto const wr1 = g.imgui.ViewportRectToWindowRect(*r1);
            auto const wr2 = g.imgui.ViewportRectToWindowRect(*r2);
            auto const thickness = LivePx(UiSizeId::FXKnobJoiningLineThickness);
            auto const pad = LivePx(UiSizeId::FXKnobJoiningLinePadLR);
            auto const y = wr1.CentreY() - (thickness / 2);
            g.imgui.draw_list->AddLine({wr1.Right() + pad, y},
                                       {wr2.x - pad, y},
                                       LiveCol(UiColMap::FXKnobJoiningLine),
                                       thickness);
        }
    }
}

struct EffectHeadingResult {
    Box heading_btn;
    Box extras_container;
    bool close_fired;
};

static EffectHeadingResult DoEffectHeading(GuiState& g, Effect& fx, Box parent) {
    auto const cols = GetFxColMap(fx.type);
    auto const name = k_effect_info[ToInt(fx.type)].name;

    // Master heading row
    auto const master_row = DoBox(g.builder,
                                  {
                                      .parent = parent,
                                      .id_extra = ToInt(fx.type),
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                  });

    // Heading button
    auto const heading_btn =
        DoBox(g.builder,
              {
                  .parent = master_row,
                  .text = name,
                  .size_from_text = true,
                  .size_from_text_preserve_height = true,
                  .text_colours =
                      ColSet {
                          .base = LiveColStruct(UiColMap::MidText),
                          .hot = LiveColStruct(UiColMap::MidText),
                          .active = LiveColStruct(UiColMap::MidText),
                      },
                  .text_justification = TextJustification::CentredLeft,
                  .background_fill_colours = LiveColStruct(cols.back),
                  .round_background_corners = 0b0010,
                  .layout {
                      .size = {1, LiveWw(UiSizeId::FXHeadingH)},
                      .margins {.l = LiveWw(UiSizeId::FXHeadingL), .r = LiveWw(UiSizeId::FXHeadingR)},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      return fmt::Format(g.scratch_arena, "{}", k_effect_info[ToInt(fx.type)].description);
                  }},
                  .button_behaviour = imgui::ButtonConfig {},
              });

    // Extras container (for compressor auto-gain, delay sync, etc.)
    auto const extras = DoBox(g.builder,
                              {
                                  .parent = master_row,
                                  .layout {
                                      .size = {layout::k_fill_parent, layout::k_hug_contents},
                                      .contents_direction = layout::Direction::Row,
                                      .contents_align = layout::Alignment::End,
                                  },
                              });

    // Close button
    auto const close_btn =
        DoBox(g.builder,
              {
                  .parent = master_row,
                  .text = ICON_FA_XMARK ""_s,
                  .font = FontType::Icons,
                  .text_colours =
                      ColSet {
                          .base = LiveColStruct(UiColMap::MidText),
                          .hot = LiveColStruct(UiColMap::MidTextHot),
                          .active = LiveColStruct(UiColMap::MidTextHot),
                      },
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {LiveWw(UiSizeId::FXCloseButtonWidth), LiveWw(UiSizeId::FXCloseButtonHeight)},
                  },
                  .tooltip = FunctionRef<String()> {[&]() -> String {
                      return fmt::Format(g.scratch_arena, "Remove {}", k_effect_info[ToInt(fx.type)].name);
                  }},
                  .button_behaviour = imgui::ButtonConfig {},
              });

    bool close_fired = false;
    if (close_btn.button_fired) {
        SetParameterValue(g.engine.processor, k_effect_info[ToInt(fx.type)].on_param_index, 0, {});
        close_fired = true;
    }

    return {heading_btn, extras, close_fired};
}

static Box DoEffectParamContainer(GuiBuilder& builder, Box parent, u64 loc_hash = SourceLocationHash()) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_direction = layout::Direction::Row,
                         .contents_multiline = true,
                         .contents_align = layout::Alignment::Middle,
                     },
                 },
                 loc_hash);
}

static void DoIrSelectorRightClickMenu(GuiState& g, Box selector_button) {
    auto const right_click_id = g.imgui.MakeId("ir-selector-popup");

    if (auto const r = BoxRect(g.builder, selector_button)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        if (g.imgui.ButtonBehaviour(window_r,
                                    selector_button.imgui_id,
                                    {
                                        .mouse_button = MouseButton::Right,
                                        .event = MouseButtonEvent::Up,
                                    })) {
            g.imgui.OpenPopupMenu(right_click_id, selector_button.imgui_id);
        }

        if (g.imgui.IsPopupMenuOpen(right_click_id))
            DoBoxViewport(g.builder,
                          {
                              .run =
                                  [&](GuiBuilder&) {
                                      auto const root =
                                          DoBox(g.builder,
                                                {
                                                    .layout {
                                                        .size = layout::k_hug_contents,
                                                        .contents_direction = layout::Direction::Column,
                                                        .contents_align = layout::Alignment::Start,
                                                    },
                                                });
                                      if (MenuItem(g.builder,
                                                   root,
                                                   {
                                                       .text = "Unload IR"_s,
                                                       .mode = !g.engine.processor.convo.ir_id
                                                                   ? MenuItemOptions::Mode::Disabled
                                                                   : MenuItemOptions::Mode::Active,
                                                       .no_icon_gap = true,
                                                   })
                                              .button_fired) {
                                          LoadConvolutionIr(g.engine, k_nullopt);
                                      }
                                  },
                              .bounds = window_r,
                              .imgui_id = right_click_id,
                              .viewport_config = k_default_popup_menu_viewport,
                          });
    }
}

static void
DoImpulseResponseSelector(GuiState& g, GuiFrameContext const& frame_context, Box param_container) {
    auto const ir_name = IrName(g.engine);

    // Selector row
    auto const selector_row =
        DoBox(g.builder,
              {
                  .parent = param_container,
                  .layout {
                      .size = {LiveWw(UiSizeId::FXConvoIRWidth), layout::k_hug_contents},
                      .contents_padding {.r = LiveWw(UiSizeId::ParamIntButtonMarginR)},
                      .contents_direction = layout::Direction::Column,
                  },
              });

    // Row for button + arrows + shuffle
    auto const btn_row = DoMidPanelPrevNextRow(g.builder, selector_row, layout::k_fill_parent);

    // Background fill for the whole row
    if (auto const r = BoxRect(g.builder, btn_row)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddRectFilled(window_r,
                                         LiveCol(UiColMap::MidDarkSurface),
                                         LivePx(UiSizeId::CornerRounding));
    }

    // IR name button
    auto const ir_btn = DoBox(g.builder,
                              {
                                  .parent = btn_row,
                                  .text = ir_name,
                                  .text_colours =
                                      ColSet {
                                          .base = LiveColStruct(UiColMap::MidText),
                                          .hot = LiveColStruct(UiColMap::MidTextHot),
                                          .active = LiveColStruct(UiColMap::MidTextOn),
                                      },
                                  .text_justification = TextJustification::CentredLeft,
                                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                                  .layout {
                                      .size = {layout::k_fill_parent, TextButtonHeight()},
                                  },
                                  .tooltip = FunctionRef<String()> {[&]() -> String {
                                      return fmt::Format(g.scratch_arena,
                                                         "Impulse: {}\n{}",
                                                         ir_name,
                                                         "The impulse response to use");
                                  }},
                                  .button_behaviour = imgui::ButtonConfig {},
                              });

    if (ir_btn.button_fired) {
        g.imgui.OpenModalViewport(g.ir_browser_state.k_panel_id);
        if (auto const r = BoxRect(g.builder, ir_btn))
            g.ir_browser_state.common_state.absolute_button_rect = g.imgui.ViewportRectToWindowRect(*r);
    }

    DoIrSelectorRightClickMenu(g, ir_btn);

    IrBrowserContext context {
        .sample_library_server = g.shared_engine_systems.sample_library_server,
        .library_images = g.library_images,
        .engine = g.engine,
        .prefs = g.prefs,
        .notifications = g.notifications,
        .persistent_store = g.shared_engine_systems.persistent_store,
        .confirmation_dialog_state = g.confirmation_dialog_state,
        .frame_context = frame_context,
    };

    // Prev/next arrows
    auto const prev_next = DoMidPanelPrevNextButtons(
        g.builder,
        btn_row,
        {
            .prev_tooltip = "Previous IR.\n\nThis is based on the currently selected filters."_s,
            .next_tooltip = "Next IR.\n\nThis is based on the currently selected filters."_s,
        });
    if (prev_next.prev_fired) LoadAdjacentIr(context, g.ir_browser_state, SearchDirection::Backward);
    if (prev_next.next_fired) LoadAdjacentIr(context, g.ir_browser_state, SearchDirection::Forward);

    // Shuffle button
    auto const shuffle_btn = DoMidPanelShuffleButton(
        g.builder,
        btn_row,
        {.tooltip = "Load a random IR.\n\nThis is based on the currently selected filters."_s});
    if (shuffle_btn.button_fired) LoadRandomIr(context, g.ir_browser_state);

    // Label below
    DoBox(g.builder,
          {
              .parent = selector_row,
              .text = "Impulse"_s,
              .text_colours = LiveColStruct(UiColMap::MidText),
              .text_justification = TextJustification::Centred,
              .layout {
                  .size = {layout::k_fill_parent, k_font_body_size},
              },
          });
}

struct EffectSectionInfo {
    Effect* fx;
    Box heading_btn;
    Box divider;
};

// Switchboard: effect toggle buttons arranged in two columns.
// Layout phase: creates a pure structural grid of boxes.
// Input+render phase: draws toggle buttons, labels, drop zones, and handles interaction.
static Box DoSwitchboard(GuiState& g, Box root, EffectsArray& ordered_effects) {
    using enum UiSizeId;
    auto& imgui = g.imgui;
    auto& params = g.engine.processor.main_params;

    int const switches_left_col_size = (k_num_effect_types / 2) + (k_num_effect_types % 2);

    // === Layout: pure structural grid ===

    auto const switches_container = DoBox(g.builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_padding = {.l = LiveWw(FXSwitchBoardMarginL),
                                                                       .r = LiveWw(FXSwitchBoardMarginR),
                                                                       .t = LiveWw(FXSwitchBoardMarginT),
                                                                       .b = LiveWw(FXSwitchBoardMarginB)},
                                                  .contents_direction = layout::Direction::Row,
                                              },
                                          });

    auto const left_col = DoBox(g.builder,
                                {
                                    .parent = switches_container,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_direction = layout::Direction::Column,
                                    },
                                });
    auto const right_col = DoBox(g.builder,
                                 {
                                     .parent = switches_container,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_direction = layout::Direction::Column,
                                     },
                                 });

    struct SlotBoxes {
        Box switch_row;
        Box number_box;
        Box slot_box;
        Box grab_box;
    };
    Array<SlotBoxes, k_num_effect_types> slots {};

    for (auto const slot : Range(k_num_effect_types)) {
        auto const col_parent = ((int)slot < switches_left_col_size) ? left_col : right_col;

        auto const switch_row =
            DoBox(g.builder,
                  {
                      .parent = col_parent,
                      .id_extra = (u64)slot,
                      .layout {
                          .size = {layout::k_fill_parent, LiveWw(FXSwitchBoardItemHeight)},
                          .contents_direction = layout::Direction::Row,
                      },
                  });

        auto const number_box =
            DoBox(g.builder,
                  {
                      .parent = switch_row,
                      .layout {
                          .size = {LiveWw(FXSwitchBoardNumberWidth), layout::k_fill_parent},
                      },
                  });

        auto const slot_box = DoBox(g.builder,
                                    {
                                        .parent = switch_row,
                                        .layout {
                                            .size = layout::k_fill_parent,
                                        },
                                    });

        auto const grab_box =
            DoBox(g.builder,
                  {
                      .parent = switch_row,
                      .layout {
                          .size = {LiveWw(FXSwitchBoardGrabRegionWidth), layout::k_fill_parent},
                      },
                  });

        slots[slot] = {switch_row, number_box, slot_box, grab_box};
    }

    // === Input + render: drawing and interaction ===

    if (g.builder.IsInputAndRenderPass()) {
        auto& draw_list = *imgui.draw_list;
        auto const corner_rounding = LivePx(UiSizeId::CornerRounding);
        auto& dragging_fx = g.dragging_fx_switch;

        usize fx_index = 0;
        for (auto const slot : Range(k_num_effect_types)) {
            auto const window_number_r =
                imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[slot].number_box).Value());
            auto const window_slot_r =
                imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[slot].slot_box).Value());
            auto const window_grab_r =
                imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[slot].grab_box).Value());

            // Number label
            draw_list.AddTextInRect(window_number_r,
                                    LiveCol(UiColMap::MidText),
                                    fmt::Format(g.scratch_arena, "{}", slot + 1),
                                    {.justification = TextJustification::CentredLeft});

            // Drop zone: if dragging and cursor is over this slot (or it's the current drop slot)
            if (dragging_fx &&
                (window_slot_r.Contains(GuiIo().in.cursor_pos) || dragging_fx->drop_slot == slot)) {
                if (dragging_fx->drop_slot != slot)
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                dragging_fx->drop_slot = slot;
                draw_list.AddRectFilled(window_slot_r, LiveCol(UiColMap::FXButtonDropZone), corner_rounding);
            } else {
                // Normal slot: show toggle button for the effect assigned to this slot
                auto fx = ordered_effects[fx_index++];
                if (dragging_fx && fx == dragging_fx->fx) fx = ordered_effects[fx_index++];

                auto const fx_cols = GetFxColMap(fx->type);
                bool const is_on = EffectIsOn(params, fx);

                // Toggle button: register behaviour, draw background, draw text
                auto const btn_id = imgui.MakeId(slots[slot].slot_box.imgui_id);
                bool const fired = imgui.ButtonBehaviour(window_slot_r, btn_id, {});
                bool const is_hot = imgui.IsHot(btn_id);
                bool const is_active = imgui.IsActive(btn_id, MouseButton::Left);

                auto bg_col = is_on ? LiveCol(fx_cols.button) : LiveCol(UiColMap::MidDarkSurface);
                if (is_active)
                    bg_col = ChangeBrightness(bg_col, 0.8f);
                else if (is_hot)
                    bg_col = ChangeBrightness(bg_col, 1.2f);
                draw_list.AddRectFilled(window_slot_r, bg_col, corner_rounding);

                auto const text_col = (is_hot || is_active) ? LiveCol(UiColMap::MidTextHot)
                                      : is_on               ? LiveCol(UiColMap::MidTextOn)
                                                            : LiveCol(UiColMap::MidText);
                draw_list.AddTextInRect(window_slot_r,
                                        text_col,
                                        k_effect_info[ToInt(fx->type)].name,
                                        {.justification = TextJustification::Centred});

                if (fired) {
                    SetParameterValue(g.engine.processor,
                                      k_effect_info[ToInt(fx->type)].on_param_index,
                                      is_on ? 0.0f : 1.0f,
                                      {});
                }

                // Grab handle icon (show only on hover)
                imgui.RegisterRectForMouseTracking(window_grab_r);
                if (is_hot || window_grab_r.Contains(GuiIo().in.cursor_pos)) {
                    draw_list.AddTextInRect(window_grab_r,
                                            LiveCol(UiColMap::FXButtonGripIcon),
                                            ICON_FA_ARROWS_UP_DOWN ""_s,
                                            {.justification = TextJustification::CentredRight});
                }
                if (window_grab_r.Contains(GuiIo().in.cursor_pos))
                    GuiIo().out.wants.cursor_type = CursorType::AllArrows;

                // Drag start detection
                if (is_active && !dragging_fx) {
                    auto const click_pos = GuiIo().in.mouse_buttons[0].last_press.point;
                    auto const current_pos = GuiIo().in.cursor_pos;
                    auto const delta = current_pos - click_pos;

                    constexpr f32 k_wiggle_room = 3;
                    if (Sqrt((delta.x * delta.x) + (delta.y * delta.y)) > k_wiggle_room)
                        dragging_fx =
                            DraggingFX {btn_id, fx, slot, GuiIo().in.cursor_pos - window_slot_r.pos};
                }
            }
        }

        // Floating switch during drag
        if (dragging_fx) {
            auto const fx_cols = GetFxColMap(dragging_fx->fx->type);
            bool const is_on_drag = EffectIsOn(params, dragging_fx->fx);

            Rect btn_r = imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[0].slot_box).Value());
            btn_r.pos = GuiIo().in.cursor_pos - dragging_fx->relative_grab_point;

            auto const bg = is_on_drag ? LiveCol(fx_cols.button) : LiveCol(UiColMap::MidDarkSurface);
            draw_list.AddRectFilled(btn_r, bg, corner_rounding);

            auto const text_col = is_on_drag ? LiveCol(UiColMap::MidTextOn) : LiveCol(UiColMap::MidText);
            draw_list.AddTextInRect(btn_r,
                                    text_col,
                                    k_effect_info[ToInt(dragging_fx->fx->type)].name,
                                    {.justification = TextJustification::Centred});

            GuiIo().out.wants.cursor_type = CursorType::AllArrows;
        }

        // Handle release
        if (dragging_fx && imgui.WasJustDeactivated(dragging_fx->id, MouseButton::Left)) {
            MoveEffectToNewSlot(ordered_effects, dragging_fx->fx, dragging_fx->drop_slot);
            g.engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                           StoreMemoryOrder::Release);
            g.engine.processor.inbox_flags.FetchOr(audio_thread_inbox::FxOrderChanged,
                                                   RmwMemoryOrder::Release);
            g.engine.processor.host.request_process(&g.engine.processor.host);
            dragging_fx.Clear();
        }
    }

    // Switchboard bottom divider
    DoWhitespace(g.builder, root, LiveWw(FXDividerMarginT));
    auto const switches_bottom_divider = DoDivider(g, root);
    DoWhitespace(g.builder, root, LiveWw(FXDividerMarginB));

    return switches_bottom_divider;
}

// Per-effect-type parameter controls.
static void DoEffectParams(GuiState& g,
                           GuiFrameContext const& frame_context,
                           Effect& fx,
                           Box param_container,
                           Box extras_container,
                           Col highlight_col,
                           Box root) {
    using enum UiSizeId;
    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    auto const knob_w = LiveWw(ParamComponentSmallWidth);

    switch (fx.type) {
        case EffectType::StereoWiden: {
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::StereoWidenWidth),
                            {
                                .width = knob_w,
                                .knob_highlight_col = highlight_col,
                                .bidirectional = true,
                            });
            break;
        }

        case EffectType::Distortion: {
            DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DistortionType), {});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::DistortionDrive),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            break;
        }

        case EffectType::BitCrush: {
            DoIntParameter(g,
                           param_container,
                           params.DescribedValue(ParamIndex::BitCrushBits),
                           {.width = LiveWw(FXDraggerWidth)});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::BitCrushBitRate),
                            {.width = knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::BitCrushWet),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::BitCrushDry),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, wet_box, dry_box);
            break;
        }

        case EffectType::Compressor: {
            DoButtonParameter(g,
                              extras_container,
                              params.DescribedValue(ParamIndex::CompressorAutoGain),
                              {.width = LiveWw(FXCompressorAutoGainWidth)});

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorThreshold),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorRatio),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorGain),
                            {
                                .width = knob_w,
                                .knob_highlight_col = highlight_col,
                                .bidirectional = true,
                            });
            break;
        }

        case EffectType::FilterEffect: {
            bool const using_gain = engine.processor.filter_effect.IsUsingGainParam(params);
            DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::FilterType), {});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::FilterCutoff),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::FilterResonance),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::FilterGain),
                            {
                                .width = knob_w,
                                .knob_highlight_col = highlight_col,
                                .greyed_out = !using_gain,
                            });
            break;
        }

        case EffectType::Chorus: {
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusRate),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusDepth),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusHighpass),
                            {.width = knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ChorusWet),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ChorusDry),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, wet_box, dry_box);
            break;
        }

        case EffectType::Reverb: {
            Optional<Box> group_container {};
            u8 previous_group = 0;
            Box prev_knob {};
            for (auto const i : Range(k_reverb_params.size)) {
                auto const& info = k_param_descriptors[ToInt(k_reverb_params[i])];
                auto knob_parent = param_container;
                if (info.grouping_within_module != 0) {
                    if (!group_container || info.grouping_within_module != previous_group) {
                        group_container = DoBox(g.builder,
                                                {
                                                    .parent = param_container,
                                                    .id_extra = (u64)i,
                                                    .layout {
                                                        .size = layout::k_hug_contents,
                                                        .contents_direction = layout::Direction::Row,
                                                    },
                                                });
                    }
                    knob_parent = *group_container;
                    if (info.grouping_within_module == previous_group) {
                        auto const knob = DoKnobParameter(g,
                                                          knob_parent,
                                                          params.DescribedValue(k_reverb_params[i]),
                                                          {.width = knob_w, .knob_highlight_col = highlight_col});
                        DoKnobJoiningLine(g, prev_knob, knob);
                        prev_knob = knob;
                        previous_group = info.grouping_within_module;
                        continue;
                    }
                    previous_group = info.grouping_within_module;
                } else {
                    group_container = {};
                    previous_group = 0;
                }
                prev_knob = DoKnobParameter(g,
                                            knob_parent,
                                            params.DescribedValue(k_reverb_params[i]),
                                            {.width = knob_w, .knob_highlight_col = highlight_col});
            }
            break;
        }

        case EffectType::Phaser: {
            Optional<Box> group_container {};
            u8 previous_group = 0;
            Box prev_knob {};
            for (auto const i : Range(k_phaser_params.size)) {
                auto const& info = k_param_descriptors[ToInt(k_phaser_params[i])];
                auto knob_parent = param_container;
                if (info.grouping_within_module != 0) {
                    if (!group_container || info.grouping_within_module != previous_group) {
                        group_container = DoBox(g.builder,
                                                {
                                                    .parent = param_container,
                                                    .id_extra = (u64)i,
                                                    .layout {
                                                        .size = layout::k_hug_contents,
                                                        .contents_direction = layout::Direction::Row,
                                                    },
                                                });
                    }
                    knob_parent = *group_container;
                    if (info.grouping_within_module == previous_group) {
                        auto const knob = DoKnobParameter(g,
                                                          knob_parent,
                                                          params.DescribedValue(k_phaser_params[i]),
                                                          {.width = knob_w, .knob_highlight_col = highlight_col});
                        DoKnobJoiningLine(g, prev_knob, knob);
                        prev_knob = knob;
                        previous_group = info.grouping_within_module;
                        continue;
                    }
                    previous_group = info.grouping_within_module;
                } else {
                    group_container = {};
                    previous_group = 0;
                }
                prev_knob = DoKnobParameter(g,
                                            knob_parent,
                                            params.DescribedValue(k_phaser_params[i]),
                                            {.width = knob_w, .knob_highlight_col = highlight_col});
            }
            break;
        }

        case EffectType::Delay: {
            bool const synced = params.BoolValue(ParamIndex::DelayTimeSyncSwitch);

            // Sync toggle in heading extras
            DoButtonParameter(g,
                              extras_container,
                              params.DescribedValue(ParamIndex::DelayTimeSyncSwitch),
                              {.width = LiveWw(FXDelaySyncBtnWidth)});

            // Time params (conditional)
            if (synced) {
                DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayTimeSyncedL), {});
                DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayTimeSyncedR), {});
            } else {
                auto const left_knob = DoKnobParameter(g,
                                                       param_container,
                                                       params.DescribedValue(ParamIndex::DelayTimeLMs),
                                                       {.width = knob_w, .knob_highlight_col = highlight_col});
                auto const right_knob = DoKnobParameter(g,
                                                        param_container,
                                                        params.DescribedValue(ParamIndex::DelayTimeRMs),
                                                        {.width = knob_w, .knob_highlight_col = highlight_col});
                DoKnobJoiningLine(g, left_knob, right_knob);
            }

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::DelayFeedback),
                            {.width = knob_w, .knob_highlight_col = highlight_col});

            DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayMode), {});

            // Second row for filter + mix
            auto const param_container2 = DoEffectParamContainer(g.builder, root);

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container2,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const cutoff_knob =
                DoKnobParameter(g,
                                sub,
                                params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones),
                                {.width = knob_w, .knob_highlight_col = highlight_col});
            auto const spread_knob = DoKnobParameter(g,
                                                     sub,
                                                     params.DescribedValue(ParamIndex::DelayFilterSpread),
                                                     {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, cutoff_knob, spread_knob);

            DoKnobParameter(g,
                            param_container2,
                            params.DescribedValue(ParamIndex::DelayMix),
                            {.width = knob_w, .knob_highlight_col = highlight_col});
            break;
        }

        case EffectType::ConvolutionReverb: {
            DoImpulseResponseSelector(g, frame_context, param_container);

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ConvolutionReverbHighpass),
                            {.width = knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ConvolutionReverbWet),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ConvolutionReverbDry),
                                                 {.width = knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, wet_box, dry_box);
            break;
        }

        case EffectType::Count: PanicIfReached(); break;
    }
}

// Effect sections: heading + params + divider for each active effect.
[[maybe_unused]] static DynamicArrayBounded<EffectSectionInfo, k_num_effect_types>
DoEffectSections(GuiState& g, GuiFrameContext const& frame_context, Box root, EffectsArray& ordered_effects) {
    using enum UiSizeId;
    auto& params = g.engine.processor.main_params;
    auto& dragging_fx_unit = g.dragging_fx_unit;
    DynamicArrayBounded<EffectSectionInfo, k_num_effect_types> effect_sections;

    for (auto fx : ordered_effects) {
        if (!EffectIsOn(params, fx)) continue;

        g.imgui.PushId((u64)fx->type);
        DEFER { g.imgui.PopId(); };

        bool const is_being_dragged = dragging_fx_unit && dragging_fx_unit->fx == fx;

        auto const cols = GetFxColMap(fx->type);
        auto const highlight_col = LiveColStruct(cols.highlight);

        auto const [heading_btn, extras_container, close_fired] = DoEffectHeading(g, *fx, root);

        if (!is_being_dragged) {
            // Drag start on heading
            if (g.imgui.WasJustActivated(heading_btn.imgui_id, MouseButton::Left)) {
                dragging_fx_unit =
                    DraggingFX {heading_btn.imgui_id, fx, FindSlotInEffects(ordered_effects, fx), {}};
                GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
            }
            if (g.imgui.IsHotOrActive(heading_btn.imgui_id, MouseButton::Left))
                GuiIo().out.wants.cursor_type = CursorType::AllArrows;
        }

        auto const param_container = DoEffectParamContainer(g.builder, root);

        DoEffectParams(g, frame_context, *fx, param_container, extras_container, highlight_col, root);

        // Divider after effect section
        DoWhitespace(g.builder, root, LiveWw(FXDividerMarginT));
        auto const div = DoDivider(g, root);
        DoWhitespace(g.builder, root, LiveWw(FXDividerMarginB));

        dyn::Append(effect_sections, EffectSectionInfo {fx, heading_btn, div});
    }

    return effect_sections;
}

// Drag-and-drop for both effect unit reordering and switchboard reordering.
[[maybe_unused]] static void DoEffectDragAndDrop(GuiState& g,
                                                 Box root,
                                                 Box switches_bottom_divider,
                                                 Span<EffectSectionInfo const> effect_sections,
                                                 EffectsArray& ordered_effects) {
    auto& engine = g.engine;
    auto& dragging_fx_unit = g.dragging_fx_unit;
    auto& dragging_fx_switch = g.dragging_fx_switch;

    // Effect unit drag (heading drag to reorder sections)
    if (dragging_fx_unit && g.imgui.IsViewportHovered(g.imgui.curr_viewport)) {
        // Find closest divider
        f32 const rel_y_pos = g.imgui.WindowPosToViewportPos(GuiIo().in.cursor_pos).y;
        Box closest_div = switches_bottom_divider;
        usize closest_slot = 0;
        auto const original_slot = FindSlotInEffects(ordered_effects, dragging_fx_unit->fx);

        f32 distance = FLT_MAX;
        if (auto const r = BoxRect(g.builder, switches_bottom_divider)) distance = Abs(r->y - rel_y_pos);

        for (auto const& section : effect_sections) {
            if (auto const r = BoxRect(g.builder, section.divider)) {
                if (f32 const d = Abs(r->y - rel_y_pos); d < distance) {
                    distance = d;
                    closest_div = section.divider;
                    closest_slot = FindSlotInEffects(ordered_effects, section.fx) + 1;
                    if (closest_slot > original_slot) --closest_slot;
                }
            }
        }

        // Highlight closest divider
        DoDividerColoured(g, closest_div, LiveCol(UiColMap::FXDividerLineDropZone));

        if (dragging_fx_unit->drop_slot != closest_slot)
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        dragging_fx_unit->drop_slot = closest_slot;
    }

    // Floating heading during drag
    if (dragging_fx_unit) {
        GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        auto const drag_cols = GetFxColMap(dragging_fx_unit->fx->type);
        auto const text = k_effect_info[ToInt(dragging_fx_unit->fx->type)].name;

        auto const cursor_vp = g.imgui.WindowPosToViewportPos(GuiIo().in.cursor_pos);

        auto const floating_heading =
            DoBox(g.builder,
                  {
                      .parent = root,
                      .id_extra = 998,
                      .text = text,
                      .text_colours = LiveColStruct(UiColMap::MidText),
                      .text_justification = TextJustification::Centred,
                      .round_background_corners = 0b0010,
                      .layout {
                          .size = {layout::k_hug_contents, LiveWw(UiSizeId::FXHeadingH)},
                          .margins {
                              .l = Max(0.0f, cursor_vp.x + LiveWw(UiSizeId::FXHeadingH)),
                              .t = Max(0.0f, cursor_vp.y),
                          },
                          .anchor = layout::Anchor::Left | layout::Anchor::Top,
                      },
                  });

        if (auto const fhr = BoxRect(g.builder, floating_heading)) {
            auto const window_fhr = g.imgui.ViewportRectToWindowRect(*fhr);
            g.imgui.draw_list->AddRectFilled(window_fhr,
                                             ChangeBrightness(LiveCol(drag_cols.back) | 0xff000000, 0.7f),
                                             LivePx(UiSizeId::CornerRounding));
        }

        // Auto-scroll
        {
            auto const space_around_cursor = 100.0f;
            Rect spacer_r;
            spacer_r.pos = GuiIo().in.cursor_pos;
            spacer_r.y -= space_around_cursor / 2;
            spacer_r.w = 1;
            spacer_r.h = space_around_cursor;

            auto wnd = g.imgui.curr_viewport;
            if (!Rect::DoRectsIntersect(spacer_r, wnd->clipping_rect.ReducedVertically(spacer_r.h))) {
                bool const going_up = GuiIo().in.cursor_pos.y < wnd->clipping_rect.CentreY();
                auto const d = 100.0f * GuiIo().in.delta_time;
                GuiIo().WakeupAtTimedInterval(g.redraw_counter, 0.016);
                g.imgui.SetYScroll(
                    wnd,
                    Clamp(wnd->scroll_offset.y + (going_up ? -d : d), 0.0f, wnd->scroll_max.y));
            }
        }
    }

    // Handle release for both drag types
    bool effects_order_changed = false;

    if (dragging_fx_unit && g.imgui.WasJustDeactivated(dragging_fx_unit->id, MouseButton::Left)) {
        MoveEffectToNewSlot(ordered_effects, dragging_fx_unit->fx, dragging_fx_unit->drop_slot);
        effects_order_changed = true;
        dragging_fx_unit.Clear();
    }

    if (dragging_fx_switch && g.imgui.WasJustDeactivated(dragging_fx_switch->id, MouseButton::Left)) {
        MoveEffectToNewSlot(ordered_effects, dragging_fx_switch->fx, dragging_fx_switch->drop_slot);
        effects_order_changed = true;
        dragging_fx_switch.Clear();
    }

    if (effects_order_changed) {
        engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                     StoreMemoryOrder::Release);
        engine.processor.inbox_flags.FetchOr(audio_thread_inbox::FxOrderChanged, RmwMemoryOrder::Release);
        engine.processor.host.request_process(&engine.processor.host);
    }
}

void DoEffectsPanel(GuiState& g, GuiFrameContext const& frame_context, Box parent) {
    DoBoxViewport(
        g.builder,
        {
            .run =
                [&](GuiBuilder&) {
                    auto ordered_effects = DecodeEffectsArray(
                        g.engine.processor.desired_effects_order.Load(LoadMemoryOrder::Relaxed),
                        g.engine.processor.effects_ordered_by_type);

                    auto const root = DoBox(g.builder,
                                            {
                                                .layout {
                                                    .size = layout::k_fill_parent,
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

                    auto const switches_bottom_divider = DoSwitchboard(g, root, ordered_effects);
                    auto const effect_sections = DoEffectSections(g, frame_context, root, ordered_effects);
                    DoEffectDragAndDrop(g, root, switches_bottom_divider, effect_sections, ordered_effects);
                },
            .bounds = parent,
            .imgui_id = g.imgui.MakeId("Effects"),
            .viewport_config =
                {
                    .draw_scrollbars = DrawMidPanelScrollbars,
                    .padding =
                        {
                            .l = LiveWw(UiSizeId::FXViewportPadL),
                            .t = LiveWw(UiSizeId::FXViewportPadT),
                            .r = LiveWw(UiSizeId::FXViewportPadR),
                            .b = LiveWw(UiSizeId::FXViewportPadB),
                        },
                    .scrollbar_padding = 4,
                    .scrollbar_width = LiveWw(UiSizeId::ScrollbarWidth),
                    .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                             imgui::ViewportScrollbarVisibility::Always},
                },
            .debug_name = "Effects",
        });
}
