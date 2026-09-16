// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_layer_common.hpp"

#include "common_infrastructure/state/state_snapshot.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_builder.hpp"

void DoInstSelectorRightClickMenu(GuiState& g, Box selector_button, u8 layer_index) {
    auto const& layer_obj = g.engine.Layer(layer_index);
    auto const right_click_id = SourceLocationHash() + layer_index;

    DoRightClickMenu(g, selector_button, right_click_id, [&](Box root) {
        if (MenuItem(g.builder,
                     root,
                     {
                         .text = "Unload Instrument"_s,
                         .mode = layer_obj.instrument_id.tag == InstrumentType::None
                                     ? MenuItemOptions::Mode::Disabled
                                     : MenuItemOptions::Mode::Active,
                         .no_icon_gap = true,
                     })
                .button_fired) {
            LoadInstrument(g.engine, layer_index, InstrumentType::None);
        }

        MenuDivider(g.builder, root);

        StateSnapshotSection const inst_target {InstrumentSection {layer_index}};

        if (MenuItem(g.builder,
                     root,
                     {
                         .text = "Copy Instrument"_s,
                         .no_icon_gap = true,
                     })
                .button_fired) {
            g.snapshot_clipboard = GuiState::CopiedSection {
                .snapshot = CurrentStateSnapshot(g.engine),
                .section = inst_target,
            };
        }

        auto const can_paste_inst = g.snapshot_clipboard.HasValue() &&
                                    g.snapshot_clipboard->section.tag == StateSnapshotSectionKind::Instrument;

        if (MenuItem(
                g.builder,
                root,
                {
                    .text = "Paste Instrument"_s,
                    .mode = can_paste_inst ? MenuItemOptions::Mode::Active : MenuItemOptions::Mode::Disabled,
                    .no_icon_gap = true,
                })
                .button_fired &&
            can_paste_inst) {
            ApplySectionOfState(g.engine,
                                g.snapshot_clipboard->snapshot,
                                g.snapshot_clipboard->section,
                                inst_target);
        }

        MenuDivider(g.builder, root);

        StateSnapshotSection const layer_target {LayerSection {layer_index}};

        if (MenuItem(g.builder,
                     root,
                     {
                         .text = "Copy Layer"_s,
                         .no_icon_gap = true,
                     })
                .button_fired) {
            g.snapshot_clipboard = GuiState::CopiedSection {
                .snapshot = CurrentStateSnapshot(g.engine),
                .section = layer_target,
            };
        }

        auto const can_paste_layer = g.snapshot_clipboard.HasValue() &&
                                     g.snapshot_clipboard->section.tag == StateSnapshotSectionKind::Layer;

        if (MenuItem(
                g.builder,
                root,
                {
                    .text = "Paste Layer"_s,
                    .mode = can_paste_layer ? MenuItemOptions::Mode::Active : MenuItemOptions::Mode::Disabled,
                    .no_icon_gap = true,
                })
                .button_fired &&
            can_paste_layer) {
            ApplySectionOfState(g.engine,
                                g.snapshot_clipboard->snapshot,
                                g.snapshot_clipboard->section,
                                layer_target);
        }

        MenuDivider(g.builder, root);

        auto const do_swap_menu_item = [&](String text, bool disabled, u8 other_layer_index) {
            if (MenuItem(
                    g.builder,
                    root,
                    {
                        .text = text,
                        .mode = disabled ? MenuItemOptions::Mode::Disabled : MenuItemOptions::Mode::Active,
                        .no_icon_gap = true,
                    },
                    SourceLocationHash() + other_layer_index)
                    .button_fired &&
                !disabled) {
                SwapLayers(g.engine, layer_index, other_layer_index);

                // Keep each panel's view with the layer it was showing.
                auto& this_panel = g.layer_panel_states[layer_index];
                auto& other_panel = g.layer_panel_states[other_layer_index];
                Swap(this_panel.selected_page, other_panel.selected_page);
                Swap(this_panel.arp_step_sequencer_show_all, other_panel.arp_step_sequencer_show_all);
            }
        };

        do_swap_menu_item("Swap with Left Layer"_s, layer_index == 0, (u8)(layer_index - 1));
        do_swap_menu_item("Swap with Right Layer"_s, layer_index + 1 == k_num_layers, (u8)(layer_index + 1));

        DoResetSectionMenuItems(g, root, layer_target, "Layer"_s);
    });
}
