// Copyright 2018-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_effects_strip.hpp"

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
#include "gui/panels/gui_ir_browser.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/colours.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "processor/effect.hpp"

constexpr f32 k_fx_controls_gap_x = 26;
constexpr f32 k_fx_controls_gap_y = 8;
constexpr f32 k_fx_heading_h = 18;
constexpr f32 k_fx_heading_text_pad_lr = 8;
constexpr f32 k_fx_icon_width = 20.5f;

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
            auto const thickness = WwToPixels(3.0f);
            auto const pad = WwToPixels(6.0f);
            auto const y = wr1.CentreY() - (thickness / 2) - WwToPixels(7.4f);
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
    auto const heading_btn = DoBox(g.builder,
                                   {
                                       .parent = master_row,
                                       .background_fill_colours = LiveColStruct(cols.back),
                                       .round_background_corners = 0b0010,
                                       .layout {
                                           .size = layout::k_hug_contents,
                                           .contents_padding {.lr = k_fx_heading_text_pad_lr},
                                       },
                                       .button_behaviour = imgui::ButtonConfig {},
                                   });

    // Heading button inner (text)
    DoBox(g.builder,
          {
              .parent = heading_btn,
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
              .parent_dictates_hot_and_active = true,
              .layout {
                  .size = {1, k_fx_heading_h},
              },
              .tooltip = FunctionRef<String()> {[&]() -> String {
                  return fmt::Format(g.scratch_arena, "{}", k_effect_info[ToInt(fx.type)].description);
              }},
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
                  .text = String {ICON_FA_XMARK},
                  .font = FontType::Icons,
                  .text_colours =
                      ColSet {
                          .base = LiveColStruct(UiColMap::MidIcon),
                          .hot = LiveColStruct(UiColMap::MidTextHot),
                          .active = LiveColStruct(UiColMap::MidTextHot),
                      },
                  .text_justification = TextJustification::Centred,
                  .layout {
                      .size = {19, 17},
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
                         .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
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
    auto const selector_row = DoBox(g.builder,
                                    {
                                        .parent = param_container,
                                        .layout {
                                            .size = {183, layout::k_hug_contents},
                                            .contents_padding {.r = 3},
                                            .contents_direction = layout::Direction::Column,
                                        },
                                    });

    // Row for button + arrows + shuffle
    auto const btn_row = DoMidPanelPrevNextRow(g.builder, selector_row, layout::k_fill_parent);

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
                                      .size = {layout::k_fill_parent, k_mid_button_height},
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
    Box divider;
};

// Switchboard: effect toggle buttons arranged in two columns.
static void DoSwitchboard(GuiState& g, Box root, EffectsArray& ordered_effects) {

    auto& params = g.engine.processor.main_params;

    int const switches_left_col_size = (k_num_effect_types / 2) + (k_num_effect_types % 2);

    // === Layout: pure structural grid ===

    auto const switches_container = DoBox(g.builder,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_padding = {.l = 8, .t = 4, .r = 2, .b = 4},
                                                  .contents_gap = 2,
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
    };
    Array<SlotBoxes, k_num_effect_types> slots {};

    for (auto const slot : Range(k_num_effect_types)) {
        auto const col_parent = ((int)slot < switches_left_col_size) ? left_col : right_col;

        auto const switch_row = DoBox(g.builder,
                                      {
                                          .parent = col_parent,
                                          .id_extra = (u64)slot,
                                          .layout {
                                              .size = {layout::k_fill_parent, 18.0f},
                                              .contents_direction = layout::Direction::Row,
                                          },
                                      });

        auto const number_box = DoBox(g.builder,
                                      {
                                          .parent = switch_row,
                                          .layout {
                                              .size = {18.0f, layout::k_fill_parent},
                                          },
                                      });

        auto const slot_box = DoBox(g.builder,
                                    {
                                        .parent = switch_row,
                                        .layout {
                                            .size = layout::k_fill_parent,
                                        },
                                    });

        slots[slot] = {switch_row, number_box, slot_box};
    }

    // === Input + render: drawing and interaction ===

    if (g.builder.IsInputAndRenderPass()) {
        auto& draw_list = *g.imgui.draw_list;
        auto const corner_rounding = WwToPixels(k_corner_rounding);

        usize fx_index = 0;
        for (auto const slot : Range(k_num_effect_types)) {
            auto const window_number_r =
                g.imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[slot].number_box).Value());
            auto const window_slot_r =
                g.imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[slot].slot_box).Value());

            // Number label
            draw_list.AddTextInRect(window_number_r,
                                    LiveCol(UiColMap::MidText),
                                    fmt::Format(g.scratch_arena, "{}", slot + 1),
                                    {.justification = TextJustification::CentredLeft});

            // Drop zone: if dragging and cursor is over this slot (or it's the current drop slot)
            if (g.dragging_fx_switch &&
                (window_slot_r.Contains(GuiIo().in.cursor_pos) || g.dragging_fx_switch->drop_slot == slot)) {
                if (g.dragging_fx_switch->drop_slot != slot)
                    GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
                g.dragging_fx_switch->drop_slot = slot;
                draw_list.AddRectFilled(window_slot_r, LiveCol(UiColMap::FXButtonDropZone), corner_rounding);
            } else {
                // Normal slot
                auto fx = ordered_effects[fx_index++];
                if (g.dragging_fx_switch && fx == g.dragging_fx_switch->fx) fx = ordered_effects[fx_index++];

                auto const fx_cols = GetFxColMap(fx->type);
                bool const is_on = EffectIsOn(params, fx);

                // Toggle button: register behaviour
                auto const btn_id = g.imgui.MakeId(SourceLocationHash() + ToInt(fx->type));
                bool const fired = g.imgui.ButtonBehaviour(window_slot_r, btn_id, {});
                bool const is_hot = g.imgui.IsHot(btn_id);
                bool const is_active = g.imgui.IsActive(btn_id, MouseButton::Left);

                // Toggle icon (coloured when on, grey when off)
                auto const icon_text = is_on ? ICON_FA_TOGGLE_ON ""_s : ICON_FA_TOGGLE_OFF ""_s;
                auto const icon_col =
                    is_on ? LiveCol(fx_cols.button)
                          : ((is_hot || is_active) ? LiveCol(UiColMap::MidIcon) : LiveCol(UiColMap::MidIcon));
                auto const icon_width = WwToPixels(k_fx_icon_width);
                auto const icon_r = window_slot_r.WithW(icon_width);

                {
                    g.fonts.Push(ToInt(FontType::Icons));
                    DEFER { g.fonts.Pop(); };
                    draw_list.AddTextInRect(
                        icon_r,
                        icon_col,
                        icon_text,
                        {.justification = TextJustification::CentredLeft, .font_scaling = 0.75f});
                }

                // Text label
                auto const text_r = window_slot_r.CutLeft(icon_width);
                auto const text_col = LiveCol(UiColMap::MidText);
                draw_list.AddTextInRect(text_r,
                                        text_col,
                                        k_effect_info[ToInt(fx->type)].name,
                                        {.justification = TextJustification::CentredLeft});

                if (fired) {
                    SetParameterValue(g.engine.processor,
                                      k_effect_info[ToInt(fx->type)].on_param_index,
                                      is_on ? 0.0f : 1.0f,
                                      {});
                }

                auto const window_grab_r = ({
                    auto w = 18.0f;
                    auto r = window_slot_r;
                    r.x += r.w - w;
                    r.w = w;
                    r;
                });

                // Grab handle icon (show only on hover)
                g.imgui.RegisterRectForMouseTracking(window_grab_r);
                if (is_hot || window_grab_r.Contains(GuiIo().in.cursor_pos)) {
                    g.fonts.Push(ToInt(FontType::Icons));
                    DEFER { g.fonts.Pop(); };
                    draw_list.AddTextInRect(
                        window_grab_r,
                        LiveCol(UiColMap::FXButtonGripIcon),
                        ICON_FA_ARROWS_UP_DOWN ""_s,
                        {.justification = TextJustification::CentredRight, .font_scaling = 0.7f});
                }
                if (window_grab_r.Contains(GuiIo().in.cursor_pos))
                    GuiIo().out.wants.cursor_type = CursorType::AllArrows;

                // Drag start detection
                if (is_active && !g.dragging_fx_switch) {
                    auto const click_pos = GuiIo().in.mouse_buttons[0].last_press.point;
                    auto const current_pos = GuiIo().in.cursor_pos;
                    auto const delta = current_pos - click_pos;

                    constexpr f32 k_wiggle_room = 3;
                    if (Sqrt((delta.x * delta.x) + (delta.y * delta.y)) > k_wiggle_room)
                        g.dragging_fx_switch =
                            DraggingFX {btn_id, fx, slot, GuiIo().in.cursor_pos - window_slot_r.pos};
                }
            }
        }

        // Floating switch during drag
        if (g.dragging_fx_switch) {
            auto const fx_cols = GetFxColMap(g.dragging_fx_switch->fx->type);
            bool const is_on_drag = EffectIsOn(params, g.dragging_fx_switch->fx);

            Rect btn_r = g.imgui.ViewportRectToWindowRect(BoxRect(g.builder, slots[0].slot_box).Value());
            btn_r.pos = GuiIo().in.cursor_pos - g.dragging_fx_switch->relative_grab_point;

            // Toggle icon
            auto const icon_text = is_on_drag ? String {ICON_FA_TOGGLE_ON} : String {ICON_FA_TOGGLE_OFF};
            auto const icon_col = is_on_drag ? LiveCol(fx_cols.button) : LiveCol(UiColMap::MidIcon);
            auto const icon_width = WwToPixels(k_fx_icon_width);
            auto const icon_r = btn_r.WithW(icon_width);

            {
                g.fonts.Push(ToInt(FontType::Icons));
                DEFER { g.fonts.Pop(); };
                draw_list.AddTextInRect(
                    icon_r,
                    icon_col,
                    icon_text,
                    {.justification = TextJustification::CentredLeft, .font_scaling = 0.75f});
            }

            // Text label
            auto const text_r = btn_r.CutLeft(icon_width);
            auto const text_col = LiveCol(UiColMap::MidText);
            draw_list.AddTextInRect(text_r,
                                    text_col,
                                    k_effect_info[ToInt(g.dragging_fx_switch->fx->type)].name,
                                    {.justification = TextJustification::CentredLeft});

            GuiIo().out.wants.cursor_type = CursorType::AllArrows;
        }

        // Handle release
        if (g.dragging_fx_switch && g.imgui.WasJustDeactivated(g.dragging_fx_switch->id, MouseButton::Left)) {
            MoveEffectToNewSlot(ordered_effects, g.dragging_fx_switch->fx, g.dragging_fx_switch->drop_slot);
            g.engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                           StoreMemoryOrder::Release);
            g.engine.processor.inbox_flags.FetchOr(audio_thread_inbox::FxOrderChanged,
                                                   RmwMemoryOrder::Release);
            g.engine.processor.host.request_process(&g.engine.processor.host);
            g.dragging_fx_switch.Clear();
        }
    }
}

// Per-effect-type parameter controls.
static void DoEffectParams(GuiState& g,
                           GuiFrameContext const& frame_context,
                           Effect& fx,
                           Box param_container,
                           Box extras_container,
                           Col highlight_col,
                           Box root) {

    auto& engine = g.engine;
    auto& params = engine.processor.main_params;
    constexpr f32 k_knob_w = 30.0f;

    switch (fx.type) {
        case EffectType::StereoWiden: {
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::StereoWidenWidth),
                            {
                                .width = k_knob_w,
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
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            break;
        }

        case EffectType::BitCrush: {
            DoIntParameter(g,
                           param_container,
                           params.DescribedValue(ParamIndex::BitCrushBits),
                           {.width = 52.0f});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::BitCrushBitRate),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::BitCrushWet),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::BitCrushDry),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, wet_box, dry_box);
            break;
        }

        case EffectType::Compressor: {
            DoButtonParameter(g,
                              extras_container,
                              params.DescribedValue(ParamIndex::CompressorAutoGain),
                              {.width = 80.0f, .on_colour = highlight_col});

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorThreshold),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorRatio),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::CompressorGain),
                            {
                                .width = k_knob_w,
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
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::FilterResonance),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            if (using_gain)
                DoKnobParameter(g,
                                param_container,
                                params.DescribedValue(ParamIndex::FilterGain),
                                {
                                    .width = k_knob_w,
                                    .knob_highlight_col = highlight_col,
                                    .greyed_out = !using_gain,
                                    .bidirectional = true,
                                });
            else
                // We leave space for the gain knob even when it's not active so that the layout doesn't jump
                // around while the user is interacting with it.
                DoBox(g.builder, {.parent = param_container, .layout {.size = {k_knob_w, 1}}});

            break;
        }

        case EffectType::Chorus: {
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusRate),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusDepth),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ChorusHighpass),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ChorusWet),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ChorusDry),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
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
                        group_container =
                            DoBox(g.builder,
                                  {
                                      .parent = param_container,
                                      .id_extra = (u64)i,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                          .contents_direction = layout::Direction::Row,
                                      },
                                  });
                    }
                    knob_parent = *group_container;
                    if (info.grouping_within_module == previous_group) {
                        auto const knob =
                            DoKnobParameter(g,
                                            knob_parent,
                                            params.DescribedValue(k_reverb_params[i]),
                                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
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
                                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
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
                        group_container =
                            DoBox(g.builder,
                                  {
                                      .parent = param_container,
                                      .id_extra = (u64)i,
                                      .layout {
                                          .size = layout::k_hug_contents,
                                          .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                          .contents_direction = layout::Direction::Row,
                                      },
                                  });
                    }
                    knob_parent = *group_container;
                    if (info.grouping_within_module == previous_group) {
                        auto const knob =
                            DoKnobParameter(g,
                                            knob_parent,
                                            params.DescribedValue(k_phaser_params[i]),
                                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
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
                                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            }
            break;
        }

        case EffectType::Delay: {
            bool const synced = params.BoolValue(ParamIndex::DelayTimeSyncSwitch);

            // Sync toggle in heading extras
            DoButtonParameter(g,
                              extras_container,
                              params.DescribedValue(ParamIndex::DelayTimeSyncSwitch),
                              {.width = 98.f, .on_colour = highlight_col});

            // Time params (conditional)
            if (synced) {
                DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayTimeSyncedL), {});
                DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayTimeSyncedR), {});
            } else {
                auto const left_knob =
                    DoKnobParameter(g,
                                    param_container,
                                    params.DescribedValue(ParamIndex::DelayTimeLMs),
                                    {.width = k_knob_w, .knob_highlight_col = highlight_col});
                auto const right_knob =
                    DoKnobParameter(g,
                                    param_container,
                                    params.DescribedValue(ParamIndex::DelayTimeRMs),
                                    {.width = k_knob_w, .knob_highlight_col = highlight_col});
                DoKnobJoiningLine(g, left_knob, right_knob);
            }

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::DelayFeedback),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});

            DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DelayMode), {});

            // Second row for filter + mix
            auto const param_container2 = DoEffectParamContainer(g.builder, root);

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container2,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const cutoff_knob =
                DoKnobParameter(g,
                                sub,
                                params.DescribedValue(ParamIndex::DelayFilterCutoffSemitones),
                                {.width = k_knob_w, .knob_highlight_col = highlight_col});
            auto const spread_knob =
                DoKnobParameter(g,
                                sub,
                                params.DescribedValue(ParamIndex::DelayFilterSpread),
                                {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, cutoff_knob, spread_knob);

            DoKnobParameter(g,
                            param_container2,
                            params.DescribedValue(ParamIndex::DelayMix),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});
            break;
        }

        case EffectType::ConvolutionReverb: {
            DoImpulseResponseSelector(g, frame_context, param_container);

            DoKnobParameter(g,
                            param_container,
                            params.DescribedValue(ParamIndex::ConvolutionReverbHighpass),
                            {.width = k_knob_w, .knob_highlight_col = highlight_col});

            auto sub = DoBox(g.builder,
                             {
                                 .parent = param_container,
                                 .layout {
                                     .size = layout::k_hug_contents,
                                     .contents_gap = {k_fx_controls_gap_x, k_fx_controls_gap_y},
                                     .contents_direction = layout::Direction::Row,
                                 },
                             });
            auto const wet_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ConvolutionReverbWet),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
            auto const dry_box = DoKnobParameter(g,
                                                 sub,
                                                 params.DescribedValue(ParamIndex::ConvolutionReverbDry),
                                                 {.width = k_knob_w, .knob_highlight_col = highlight_col});
            DoKnobJoiningLine(g, wet_box, dry_box);
            break;
        }

        case EffectType::Count: PanicIfReached(); break;
    }
}

// Effect sections: heading + params + divider for each active effect.
static DynamicArrayBounded<EffectSectionInfo, k_num_effect_types>
DoEffectSections(GuiState& g, GuiFrameContext const& frame_context, Box root, EffectsArray& ordered_effects) {
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

        auto const contents = DoBox(g.builder,
                                    {
                                        .parent = root,
                                        .layout {
                                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                                            .contents_padding =
                                                {
                                                    .lr = 12,
                                                    .tb = 8,
                                                },
                                        },
                                    });

        auto const param_container = DoEffectParamContainer(g.builder, contents);

        DoEffectParams(g, frame_context, *fx, param_container, extras_container, highlight_col, root);

        // Divider after effect section
        DoWhitespace(g.builder, root, 7.4f);
        auto const div = DoDivider(g, root);

        dyn::Append(effect_sections, EffectSectionInfo {fx, div});
    }

    return effect_sections;
}

// Drag-and-drop for both effect unit reordering and switchboard reordering.
static void DoEffectDragAndDrop(GuiState& g,
                                Box switches_bottom_divider,
                                Span<EffectSectionInfo const> effect_sections,
                                EffectsArray& ordered_effects) {
    if (!g.builder.IsInputAndRenderPass()) return;
    auto& engine = g.engine;

    // Effect unit drag (heading drag to reorder sections)
    if (g.dragging_fx_unit && g.imgui.IsViewportHovered(g.imgui.curr_viewport)) {
        // Find closest divider

        EffectSectionInfo zeroth_section {};

        EffectSectionInfo const* closest_section = nullptr;
        {
            f32 const rel_y_pos = g.imgui.WindowPosToViewportPos(GuiIo().in.cursor_pos).y;

            f32 distance = FLT_MAX;

            if (auto const r = BoxRect(g.builder, switches_bottom_divider)) {
                distance = Abs(r->y - rel_y_pos);
                zeroth_section = {
                    .fx = ({
                        Effect* first_fx = nullptr;
                        for (auto f : ordered_effects)
                            if (EffectIsOn(g.engine.processor.main_params, f)) {
                                first_fx = f;
                                break;
                            }
                        first_fx;
                    }),
                    .divider = switches_bottom_divider,
                };
                closest_section = &zeroth_section;
            }

            for (auto const& section : effect_sections) {
                if (auto const r = BoxRect(g.builder, section.divider)) {
                    if (f32 const d = Abs(r->y - rel_y_pos); d < distance) {
                        distance = d;
                        closest_section = &section;
                    }
                }
            }
        }

        ASSERT(closest_section);

        auto const closest_slot = ({
            usize dest = 0;
            auto const source = FindSlotInEffects(ordered_effects, g.dragging_fx_unit->fx);
            dest = FindSlotInEffects(ordered_effects, closest_section->fx);
            if (dest < source && closest_section != &zeroth_section) ++dest;
            dest;
        });

        // Highlight closest divider
        DoDividerColoured(g, closest_section->divider, LiveCol(UiColMap::FXDividerLineDropZone));

        if (g.dragging_fx_unit->drop_slot != closest_slot)
            GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::ImmediatelyUpdate);
        g.dragging_fx_unit->drop_slot = closest_slot;
    }

    // Floating heading during drag
    if (g.dragging_fx_unit) {
        GuiIo().out.wants.cursor_type = CursorType::AllArrows;

        auto const drag_cols = GetFxColMap(g.dragging_fx_unit->fx->type);
        auto const text = fmt::Format(g.scratch_arena,
                                      "{} dst: {} src: {}",
                                      k_effect_info[ToInt(g.dragging_fx_unit->fx->type)].name,
                                      g.dragging_fx_unit->drop_slot,
                                      FindSlotInEffects(ordered_effects, g.dragging_fx_unit->fx));

        auto const cursor_pos = GuiIo().in.cursor_pos;
        auto const heading_h = WwToPixels(k_fx_heading_h);
        auto const pad_lr = WwToPixels(k_fx_heading_text_pad_lr);
        auto const text_w = g.fonts.CalcTextSize(text, {}).x;
        auto const rounding = WwToPixels(k_corner_rounding);

        auto const floating_r = Rect {
            .x = cursor_pos.x + heading_h,
            .y = cursor_pos.y,
            .w = text_w + (pad_lr * 2),
            .h = heading_h,
        };

        g.imgui.draw_list->AddRectFilled(floating_r,
                                         ChangeBrightness(WithAlphaU8(LiveCol(drag_cols.back), 255), 0.6f),
                                         rounding);
        g.imgui.draw_list->AddTextInRect(floating_r,
                                         LiveCol(UiColMap::MidText),
                                         text,
                                         {.justification = TextJustification::Centred});

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

    if (g.dragging_fx_unit && g.imgui.WasJustDeactivated(g.dragging_fx_unit->id, MouseButton::Left)) {
        MoveEffectToNewSlot(ordered_effects, g.dragging_fx_unit->fx, g.dragging_fx_unit->drop_slot);
        effects_order_changed = true;
        g.dragging_fx_unit.Clear();
    }

    if (g.dragging_fx_switch && g.imgui.WasJustDeactivated(g.dragging_fx_switch->id, MouseButton::Left)) {
        MoveEffectToNewSlot(ordered_effects, g.dragging_fx_switch->fx, g.dragging_fx_switch->drop_slot);
        effects_order_changed = true;
        g.dragging_fx_switch.Clear();
    }

    if (effects_order_changed) {
        engine.processor.desired_effects_order.Store(EncodeEffectsArray(ordered_effects),
                                                     StoreMemoryOrder::Release);
        engine.processor.inbox_flags.FetchOr(audio_thread_inbox::FxOrderChanged, RmwMemoryOrder::Release);
        engine.processor.host.request_process(&engine.processor.host);
    }
}

void DoEffectsStripPanel(GuiState& g, GuiFrameContext const& frame_context, Box parent) {
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
                                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

                    DoSwitchboard(g, root, ordered_effects);
                    auto const switches_bottom_divider = DoDivider(g, root);
                    auto const effect_sections = DoEffectSections(g, frame_context, root, ordered_effects);
                    DoEffectDragAndDrop(g, switches_bottom_divider, effect_sections, ordered_effects);

                    // Add gap at bottom so you can see the last divider.
                    DoBox(g.builder, {.parent = root, .layout {.size = {1, 9}}});
                },
            .bounds = parent,
            .imgui_id = SourceLocationHash(),
            .viewport_config =
                {
                    .draw_scrollbars = DrawMidPanelScrollbars,
                    .padding = {.r = k_scrollbar_width},
                    .scrollbar_padding = k_scrollbar_rhs_space,
                    .scrollbar_visibility = {imgui::ViewportScrollbarVisibility::Never,
                                             imgui::ViewportScrollbarVisibility::Auto},
                    .scrollbar_inside_padding = true,
                },
            .debug_name = "EffectsStrip",
        });
}
