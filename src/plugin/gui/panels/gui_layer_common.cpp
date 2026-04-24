// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer_common.hpp"

#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_builder.hpp"

void DoInstSelectorRightClickMenu(GuiState& g, Box selector_button, u8 layer_index) {
    auto const& layer_obj = g.engine.Layer(layer_index);
    auto const right_click_id = SourceLocationHash() + layer_index;

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
            DoBoxViewport(
                g.builder,
                {
                    .run =
                        [&](GuiBuilder&) {
                            auto const root = DoBox(g.builder,
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
                                             .text = "Unload instrument"_s,
                                             .mode = layer_obj.instrument_id.tag == InstrumentType::None
                                                         ? MenuItemOptions::Mode::Disabled
                                                         : MenuItemOptions::Mode::Active,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                LoadInstrument(g.engine, layer_index, InstrumentType::None);
                            }

                            DoModalDivider(g.builder, root, {.horizontal = true});

                            StateSnapshotSelector const inst_target {InstrumentSelector {layer_index}};

                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Copy Instrument"_s,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                g.snapshot_clipboard = GuiState::CopiedSection {
                                    .snapshot = CurrentStateSnapshot(g.engine),
                                    .selector = inst_target,
                                };
                            }

                            auto const can_paste_inst =
                                g.snapshot_clipboard.HasValue() &&
                                g.snapshot_clipboard->selector.tag == SelectorKind::Instrument;

                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Paste Instrument"_s,
                                             .mode = can_paste_inst ? MenuItemOptions::Mode::Active
                                                                    : MenuItemOptions::Mode::Disabled,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired &&
                                can_paste_inst) {
                                ApplySection(g.engine,
                                             g.snapshot_clipboard->snapshot,
                                             g.snapshot_clipboard->selector,
                                             inst_target);
                            }

                            DoModalDivider(g.builder, root, {.horizontal = true});

                            StateSnapshotSelector const layer_target {
                                ParamModules {LayerModuleFromIndex(layer_index)}};

                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Copy Layer"_s,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                g.snapshot_clipboard = GuiState::CopiedSection {
                                    .snapshot = CurrentStateSnapshot(g.engine),
                                    .selector = layer_target,
                                };
                            }

                            auto const can_paste_layer = ({
                                bool ok = false;
                                if (g.snapshot_clipboard.HasValue() &&
                                    g.snapshot_clipboard->selector.tag == SelectorKind::Modules) {
                                    auto const& mods = g.snapshot_clipboard->selector.Get<ParamModules>();
                                    ok = LayerIndexFromModule(mods[0]).HasValue() &&
                                         mods[1] == ParameterModule::None;
                                }
                                ok;
                            });

                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Paste Layer"_s,
                                             .mode = can_paste_layer ? MenuItemOptions::Mode::Active
                                                                     : MenuItemOptions::Mode::Disabled,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired &&
                                can_paste_layer) {
                                ApplySection(g.engine,
                                             g.snapshot_clipboard->snapshot,
                                             g.snapshot_clipboard->selector,
                                             layer_target);
                            }

                            if (MenuItem(g.builder,
                                         root,
                                         {
                                             .text = "Reset Layer"_s,
                                             .no_icon_gap = true,
                                         })
                                    .button_fired) {
                                ApplySection(g.engine, DefaultStateSnapshot(), layer_target, layer_target);
                            }
                        },
                    .bounds = window_r,
                    .imgui_id = right_click_id,
                    .viewport_config = k_default_popup_menu_viewport,
                });
    }
}
