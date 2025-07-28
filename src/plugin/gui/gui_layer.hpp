// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "engine/engine.hpp"
#include "gui_framework/layout.hpp"
#include "gui_widget_compounds.hpp"

struct Gui;
struct Engine;

namespace layer_gui {

static constexpr Array<String, 6> k_velo_btn_tooltips = {
    "No special volume-velocity relationship",
    "Loudest at high velocity, quietest at low velocity",
    "Loudest at low velocity, quietest at high velocity",
    "Loudest at high velocity, quietest at middle velocity and below",
    "Loudest at middle velocity, quietest at both high and low velocities",
    "Loudest at bottom velocity, quietest at middle velocity and above",
};

enum class PageType {
    Main,
    Filter,
    Lfo,
    Eq,
    Play,
    Count,
};

constexpr usize k_num_pages = ToInt(PageType::Count);

struct LayerLayoutTempIDs {
    layout::Id selector_box;
    layout::Id selector_menu;
    layout::Id selector_l, selector_r;
    layout::Id selector_randomise;

    layout::Id volume;
    layout::Id mute_solo;

    LayIDPair knob1, knob2, knob3;

    layout::Id divider;
    layout::Id tabs[k_num_pages];
    layout::Id divider2;

    union {
        struct {
            layout::Id waveform_label;
            layout::Id waveform;
            layout::Id loop_mode;
            layout::Id reverse;

            layout::Id divider;

            layout::Id env_on;
            layout::Id envelope;
        } main;

        struct {
            layout::Id filter_on;
            layout::Id filter_type;
            LayIDPair cutoff;
            LayIDPair reso;
            LayIDPair env_amount;
            layout::Id envelope;
        } filter;

        struct {
            layout::Id on;
            layout::Id type[k_num_layer_eq_bands];
            LayIDPair freq[k_num_layer_eq_bands];
            LayIDPair reso[k_num_layer_eq_bands];
            LayIDPair gain[k_num_layer_eq_bands];
        } eq;

        struct {
            layout::Id transpose;
            layout::Id transpose_name;
            layout::Id keytrack;
            layout::Id mono;
            layout::Id retrig;
            layout::Id velo_name;
            layout::Id velo_graph;
        } play;

        struct {
            layout::Id on;
            LayIDPair amount;
            LayIDPair rate;
            layout::Id target;
            layout::Id target_name;
            layout::Id shape;
            layout::Id shape_name;
            layout::Id mode;
            layout::Id mode_name;
        } lfo;
    };
};

struct LayerLayout {
    PageType selected_page;
};

void Layout(Gui* g,
            LayerProcessor* layer,
            LayerLayoutTempIDs& ids,
            LayerLayout* layer_gui,
            f32 width,
            f32 height);

void Draw(Gui* g, Engine* a, Rect r, LayerProcessor* layer, LayerLayoutTempIDs& ids, LayerLayout* layer_gui);

} // namespace layer_gui
