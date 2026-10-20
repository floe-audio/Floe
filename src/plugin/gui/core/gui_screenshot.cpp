// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_screenshot.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/json/json_writer.hpp"

#include "common_infrastructure/sample_library/server/sample_library_server.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/core/gui_waveform_images.hpp"
#include "gui/panels/gui_effects.hpp"
#include "gui/panels/gui_perform.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"

bool IsScreenshotRequest(String id_name) {
    return IsEqualToCaseInsensitiveAscii(GuiIo().in.requested_screenshot_id_name, id_name);
}

bool IsAnyScreenshotInProgress() {
    return GuiIo().in.requested_screenshot_id_name.size != 0 || GuiIo().out.request_screenshot.HasValue();
}

static bool AsyncLoadsInProgress(GuiState& g) {
    if (sample_lib_server::AreLibrariesScanning(g.shared_engine_systems.sample_library_server)) return true;
    auto& preset_server = g.shared_engine_systems.preset_server;
    if (preset_server.enable_scanning.Load(LoadMemoryOrder::Acquire) && AreFoldersScanning(preset_server))
        return true;
    for (auto& pct : g.engine.sample_lib_server_async_channel.instrument_loading_percents)
        if (pct.Load(LoadMemoryOrder::Relaxed) != -1) return true;
    for (auto const& [_, imgs, _] : g.library_images.table)
        if (!IsSettled(imgs)) return true;
    for (auto const& [_, w, _] : g.waveform_images.table)
        if (w.loading_pixels && w.loading_pixels->IsInProgress()) return true;
    return false;
}

static bool ScreenshotPreconditionsMet(GuiState& g) {
    if (AsyncLoadsInProgress(g)) {
        g.screenshot_clear_since = k_nullopt;
        return false;
    }
    if (!g.screenshot_clear_since) g.screenshot_clear_since = TimePoint::Now();

    auto const notes_held =
        g.engine.processor.audio_processing_context.midi_note_state.NotesCurrentlyHeldAllChannels()
            .AnyValuesSet();
    auto const settle_seconds = notes_held ? 1.0 : 0.05;
    if ((TimePoint::Now() - *g.screenshot_clear_since) < settle_seconds) {
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
        return false;
    }
    return true;
}

struct CaptureSpec {
    Rect rect; // window-coord pixels
    DynamicArrayBounded<GuiFrameOutput::ScreenshotRequest::Overlay, 16> overlays;
    // Overlays that reach a layer page's side margins are pushed out into those margins so their outlines
    // don't crowd the content.
    Optional<Rect> layer_page_content;
};

static void AppendOverlay(CaptureSpec& spec, String name, Rect rect) {
    GuiFrameOutput::ScreenshotRequest::Overlay stored {.rect = rect};
    dyn::AssignFitInCapacity(stored.name, name);
    dyn::Append(spec.overlays, stored);
}

// Resolve the capture rect (and any overlay rects) for the currently requested ID name. Returns k_nullopt
// when the required named rects haven't been registered yet — caller will request an immediate update and
// retry on the next frame.
static Optional<CaptureSpec> ResolveCapture(GuiState& g) {
    auto& imgui = g.imgui;
    auto const named = [&](String n) { return imgui.NamedRect(n); };

    auto const simple = [&](String rect_name) -> Optional<CaptureSpec> {
        if (auto const r = named(rect_name)) return CaptureSpec {.rect = *r};
        return k_nullopt;
    };

    // --- Top panel, with the preset box outlined -------------------------------------------------------
    if (IsScreenshotRequest("top-panel"_s)) {
        auto spec = simple("top-panel"_s);
        auto const preset_box = named("top-panel.preset-box"_s);
        if (!(spec && preset_box)) return k_nullopt;
        AppendOverlay(*spec, "preset-box"_s, *preset_box);
        return spec;
    }

    if (IsScreenshotRequest("top-panel-overview"_s)) {
        auto spec = simple("top-panel"_s);
        auto const logo = named("top-panel.logo"_s);
        auto const version = named("top-panel.version"_s);
        auto const preset_box = named("top-panel.preset-box"_s);
        auto const buttons = named("top-panel.buttons"_s);
        auto const master_knobs = named("top-panel.master-knobs"_s);
        auto const meters = named("top-panel.meters"_s);
        if (!(spec && logo && version && preset_box && buttons && master_knobs && meters)) return k_nullopt;
        auto const pad = WwToPixels(3.0f);
        AppendOverlay(*spec,
                      "logo-and-version"_s,
                      Rect::FromMinMax(::Min(logo->Min(), version->Min()), ::Max(logo->Max(), version->Max()))
                          .Expanded(pad));
        AppendOverlay(*spec, "preset-box"_s, *preset_box);
        AppendOverlay(*spec, "buttons"_s, buttons->Expanded(pad));
        AppendOverlay(*spec, "master-knobs"_s, master_knobs->Expanded(pad));
        AppendOverlay(*spec, "meters"_s, meters->Expanded(pad));
        return spec;
    }

    if (IsScreenshotRequest("main-menu"_s)) {
        auto const menu = named("main-menu"_s);
        if (!menu) return k_nullopt;
        auto const pad = WwToPixels(10.0f);
        return CaptureSpec {
            .rect = {.xywh {menu->x - pad, menu->y - pad, menu->w + (pad * 2), menu->h + (pad * 2)}}};
    }

    // --- Save preset modal ----------------------------------------------------------------------------
    if (IsScreenshotRequest("save-preset"_s)) {
        auto const modal = named("save-preset-panel.modal"_s);
        auto const contents = named("save-preset.contents"_s);
        auto const tags = named("save-preset.tags"_s);
        if (!(modal && contents && tags)) return k_nullopt;
        auto visible_tags = *tags;
        if (!Rect::Intersection(visible_tags, *contents)) return k_nullopt;
        CaptureSpec spec {.rect = *modal};
        AppendOverlay(spec, "tags"_s, visible_tags);
        return spec;
    }

    // --- Layer screenshots (layer 0) -------------------------------------------------------------------
    if (IsScreenshotRequest("layer-top-controls"_s)) {
        auto const inst_selector = named("layer-top.inst-selector"_s);
        auto const inst_navigation = named("layer-top.inst-navigation"_s);
        auto const mute_solo = named("layer-top.mute-solo"_s);
        auto const volume = named("layer-top.volume"_s);
        auto const pan_stereo = named("layer-top.pan-stereo"_s);
        auto const tuning = named("layer-top.tuning"_s);
        auto const controls = named("layer-top.controls"_s);
        if (!(inst_selector && inst_navigation && mute_solo && volume && pan_stereo && tuning && controls))
            return k_nullopt;
        CaptureSpec spec {.rect = *controls};
        AppendOverlay(spec, "inst-selector"_s, *inst_selector);
        AppendOverlay(spec, "inst-navigation"_s, *inst_navigation);
        AppendOverlay(spec, "mute-solo"_s, *mute_solo);
        AppendOverlay(spec, "volume"_s, *volume);
        AppendOverlay(spec, "pan-stereo"_s, *pan_stereo);
        AppendOverlay(spec, "tuning"_s, *tuning);
        return spec;
    }
    // The page tabs and the open tab's page.
    auto const layer_tab = [&]() -> Optional<CaptureSpec> {
        auto const tabs = named("layer.page-tabs"_s);
        auto const page = named("layer.page-container"_s);
        if (!(tabs && page)) return k_nullopt;
        auto const margin = WwToPixels(15.0f);
        return CaptureSpec {
            .rect = Rect::FromMinMax(::Min(tabs->Min(), page->Min()), ::Max(tabs->Max(), page->Max())),
            .layer_page_content = Rect {.xywh {page->x + margin, page->y, page->w - (margin * 2), page->h}},
        };
    };
    if (IsScreenshotRequest("layer-config"_s)) {
        auto spec = layer_tab();
        auto const transpose = named("config.transpose"_s);
        auto const pitch_bend_range = named("config.pitch-bend-range"_s);
        auto const keytrack = named("config.keytrack"_s);
        auto const monophonic = named("config.monophonic"_s);
        auto const mpe = named("config.mpe"_s);
        auto const key_range = named("key-range-rows"_s);
        auto const velocity_curve = named("velocity-curve"_s);
        if (!(spec && transpose && pitch_bend_range && keytrack && monophonic && mpe && key_range &&
              velocity_curve))
            return k_nullopt;
        // The rows hug their contents, so they vary in width, and some labels overhang. Give every row's box
        // the same width.
        auto const rows_left = Min(Min(transpose->x, monophonic->x), key_range->x);
        auto const rows_right = Max(Max(transpose->Right(), monophonic->Right()), key_range->Right());
        auto const row_box = [&](Rect r) {
            return Rect {.xywh {rows_left, r.y, rows_right - rows_left, r.h}};
        };
        AppendOverlay(*spec, "transpose"_s, row_box(*transpose));
        AppendOverlay(*spec, "pitch-bend-range"_s, row_box(*pitch_bend_range));
        AppendOverlay(*spec, "keytrack"_s, row_box(*keytrack));
        AppendOverlay(*spec, "monophonic"_s, row_box(*monophonic));
        AppendOverlay(*spec, "mpe"_s, row_box(*mpe));
        AppendOverlay(*spec, "key-range"_s, row_box(*key_range));
        AppendOverlay(*spec, "velocity-curve"_s, *velocity_curve);
        return spec;
    }
    if (IsScreenshotRequest("layer-main"_s)) {
        auto spec = layer_tab();
        auto const volume_envelope = named("main.volume-envelope"_s);
        auto const filter_on = named("main.filter-on"_s);
        auto const filter_graph = named("main.filter-graph"_s);
        auto const filter_controls = named("main.filter-controls"_s);
        auto const filter_envelope = named("main.filter-envelope"_s);
        if (!(spec && volume_envelope && filter_on && filter_graph && filter_controls && filter_envelope))
            return k_nullopt;
        AppendOverlay(*spec, "volume-envelope"_s, *volume_envelope);
        AppendOverlay(*spec,
                      "filter-graph"_s,
                      Rect::FromMinMax(::Min(filter_on->Min(), filter_graph->Min()),
                                       ::Max(filter_on->Max(), filter_graph->Max())));
        AppendOverlay(*spec, "filter-controls"_s, *filter_controls);
        // The envelope's handles sit on the edges of its rect. Its right edge already lines up with the boxes
        // above.
        auto const envelope_pad = WwToPixels(6.0f);
        AppendOverlay(*spec,
                      "filter-envelope"_s,
                      Rect::FromMinMax(filter_envelope->Min() - envelope_pad,
                                       filter_envelope->Max() + f32x2 {0, envelope_pad}));
        return spec;
    }
    if (IsScreenshotRequest("layer-playback"_s)) {
        auto spec = layer_tab();
        auto const play_mode = named("playback.play-mode"_s);
        auto const waveform = named("playback.waveform"_s);
        auto const sample_info = named("playback.sample-info"_s);
        auto const reverse = named("playback.reverse"_s);
        auto const loop_selector = named("playback.loop-selector"_s);
        if (!(spec && play_mode && waveform && sample_info && reverse && loop_selector)) return k_nullopt;
        AppendOverlay(*spec, "play-mode"_s, *play_mode);
        AppendOverlay(*spec, "waveform"_s, *waveform);
        AppendOverlay(*spec, "sample-info"_s, *sample_info);
        AppendOverlay(*spec, "reverse"_s, *reverse);
        AppendOverlay(*spec, "loop-selector"_s, *loop_selector);
        return spec;
    }
    if (IsScreenshotRequest("layer-playback-granular-fixed"_s)) {
        auto spec = layer_tab();
        auto const share_grains = named("playback.share-grains"_s);
        auto const variation = named("playback.variation"_s);
        auto const waveform = named("playback.waveform"_s);
        auto const playhead = named("granular.playhead"_s);
        auto const grains = named("granular.grains"_s);
        auto const random = named("granular.random"_s);
        if (!(spec && share_grains && variation && waveform && playhead && grains && random))
            return k_nullopt;
        AppendOverlay(*spec, "share-grains"_s, *share_grains);
        AppendOverlay(*spec, "variation"_s, *variation);
        AppendOverlay(*spec, "waveform"_s, *waveform);
        AppendOverlay(*spec, "playhead"_s, *playhead);
        AppendOverlay(*spec, "grains"_s, *grains);
        AppendOverlay(*spec, "random"_s, *random);
        return spec;
    }
    if (IsScreenshotRequest("layer-lfo"_s)) {
        auto spec = layer_tab();
        auto const on = named("lfo.on"_s);
        auto const display = named("lfo.display"_s);
        auto const menus = named("lfo.menus"_s);
        auto const knobs = named("lfo.knobs"_s);
        if (!(spec && on && display && menus && knobs)) return k_nullopt;
        AppendOverlay(*spec, "on"_s, *on);
        AppendOverlay(*spec, "display"_s, *display);
        // Labels overhang their layout rects.
        AppendOverlay(*spec, "menus"_s, menus->Expanded(WwToPixels(6.0f)));
        AppendOverlay(*spec, "knobs"_s, knobs->Expanded(WwToPixels(6.0f)));
        return spec;
    }
    if (IsScreenshotRequest("layer-eq"_s)) {
        auto spec = layer_tab();
        auto const on = named("eq.on"_s);
        auto const graph = named("eq.graph"_s);
        auto const bands = named("eq.bands"_s);
        if (!(spec && on && graph && bands)) return k_nullopt;
        AppendOverlay(*spec, "on"_s, *on);
        AppendOverlay(*spec, "graph"_s, *graph);
        AppendOverlay(*spec, "bands"_s, *bands);
        return spec;
    }
    if (IsScreenshotRequest("layer-arp"_s)) {
        auto spec = layer_tab();
        auto const heading_row = named("arp.heading-row"_s);
        auto const velocity_bars = named("arp.velocity-bars"_s);
        auto const step_rows = named("arp.step-rows"_s);
        auto const sequence = named("arp.sequence-section"_s);
        auto const config = named("arp.config-section"_s);
        if (!(spec && heading_row && velocity_bars && step_rows && sequence && config)) return k_nullopt;
        AppendOverlay(*spec, "heading-row"_s, *heading_row);
        AppendOverlay(*spec, "velocity-bars"_s, *velocity_bars);
        AppendOverlay(*spec, "step-rows"_s, *step_rows);
        AppendOverlay(*spec, "sequence"_s, *sequence);
        AppendOverlay(*spec, "config"_s, *config);
        return spec;
    }
    if (IsScreenshotRequest("arp-fixed-notes"_s)) {
        auto spec = layer_tab();
        auto const heading_row = named("arp.heading-row"_s);
        auto const pitch_row = named("arp.pitch-row"_s);
        auto const record = named("arp.record-button"_s);
        if (!(spec && heading_row && pitch_row && record)) return k_nullopt;
        AppendOverlay(*spec, "heading-row"_s, *heading_row);
        AppendOverlay(*spec, "pitch-row"_s, *pitch_row);
        AppendOverlay(*spec, "record"_s, *record);
        return spec;
    }
    if (IsScreenshotRequest("arp-sliced"_s)) {
        auto spec = layer_tab();
        auto const sequence = named("arp.sequence-section"_s);
        auto const slicing = named("arp.slicing-section"_s);
        if (!(spec && sequence && slicing)) return k_nullopt;
        AppendOverlay(*spec, "sequence"_s, *sequence);
        AppendOverlay(*spec, "slicing"_s, *slicing);
        return spec;
    }
    auto const padded_union = [&](String a, String b, f32 pad_ww) -> Optional<CaptureSpec> {
        auto const ra = named(a);
        auto const rb = named(b);
        if (!(ra && rb)) return k_nullopt;
        return CaptureSpec {.rect = Rect::FromMinMax(::Min(ra->Min(), rb->Min()), ::Max(ra->Max(), rb->Max()))
                                        .Expanded(WwToPixels(pad_ww))};
    };
    auto const padded = [&](String rect_name, f32 pad_ww) -> Optional<CaptureSpec> {
        if (auto const r = named(rect_name)) return CaptureSpec {.rect = r->Expanded(WwToPixels(pad_ww))};
        return k_nullopt;
    };
    if (IsScreenshotRequest("layer-tab-menu"_s))
        return padded_union("layer.page-tabs"_s, "layer-tab-menu"_s, 10.0f);
    if (IsScreenshotRequest("granular-controls"_s))
        return padded_union("granular.playhead"_s, "granular.grains"_s, 16.0f);
    if (IsScreenshotRequest("granular-random"_s)) return padded("granular.random"_s, 16.0f);
    if (IsScreenshotRequest("granular-harmony"_s))
        return padded_union("granular.intervals-button"_s, "granular.harmony-menu"_s, 10.0f);
    if (IsScreenshotRequest("granular-variation"_s))
        return padded_union("playback.variation"_s, "granular.variation-menu"_s, 10.0f);
    if (IsScreenshotRequest("arp-sequence"_s)) return padded("arp.sequence-section"_s, 8.0f);
    if (IsScreenshotRequest("arp-config"_s)) return padded("arp.config-section"_s, 8.0f);
    if (IsScreenshotRequest("arp-step-settings"_s)) {
        // Wide side margins leave room for the numbers, which sit beside these thin boxes.
        auto const step_rows = named("arp.step-rows"_s);
        if (!step_rows) return k_nullopt;
        Optional<CaptureSpec> spec = CaptureSpec {.rect = {.xywh {step_rows->x - WwToPixels(24.0f),
                                                                  step_rows->y,
                                                                  step_rows->w + WwToPixels(48.0f),
                                                                  step_rows->h + WwToPixels(4.0f)}}};
        auto const step_number = named("arp.step-number-row"_s);
        auto const pitch = named("arp.pitch-row"_s);
        auto const tie = named("arp.tie-row"_s);
        auto const gate = named("arp.gate-row"_s);
        if (!(spec && step_number && pitch && tie && gate)) return k_nullopt;
        AppendOverlay(*spec, "step-number"_s, *step_number);
        AppendOverlay(*spec, "pitch"_s, *pitch);
        AppendOverlay(*spec, "tie"_s, *tie);
        AppendOverlay(*spec, "gate"_s, *gate);
        return spec;
    }
    if (IsScreenshotRequest("looping"_s)) {
        // From the top of the tab so the play mode menu is included.
        auto const page = named("layer.page-container"_s);
        auto const waveform = named("playback.waveform"_s);
        auto const loop_start = named("waveform.loop-start-handle"_s);
        auto const loop_end = named("waveform.loop-end-handle"_s);
        auto const crossfade = named("waveform.crossfade-handle"_s);
        auto const sample_start = named("waveform.sample-start-handle"_s);
        auto const reverse = named("playback.reverse"_s);
        auto const loop_selector = named("playback.loop-selector"_s);
        if (!(page && waveform && loop_start && loop_end && crossfade && sample_start && reverse &&
              loop_selector))
            return k_nullopt;
        auto const pad = WwToPixels(8.0f);
        f32x2 const tl {waveform->x - pad, page->y};
        auto const br = ::Max(waveform->Max(), loop_selector->Max()) + pad;
        CaptureSpec spec {.rect = {.pos = tl, .size = br - tl}};
        AppendOverlay(spec, "loop-start"_s, *loop_start);
        AppendOverlay(spec, "loop-end"_s, *loop_end);
        AppendOverlay(spec, "crossfade"_s, *crossfade);
        AppendOverlay(spec, "sample-start"_s, *sample_start);
        AppendOverlay(spec, "reverse"_s, *reverse);
        AppendOverlay(spec, "loop-selector"_s, *loop_selector);
        return spec;
    }
    if (IsScreenshotRequest("key-range-controls"_s)) return simple("key-range-rows"_s);
    if (IsScreenshotRequest("velocity-curve"_s)) {
        auto const r = named("velocity-curve"_s);
        if (!r) return k_nullopt;
        auto const h_pad = WwToPixels(20.0f);
        auto const v_pad = WwToPixels(12.0f);
        return CaptureSpec {
            .rect = {.xywh {r->x - h_pad, r->y - v_pad, r->w + (h_pad * 2), r->h + (v_pad * 2)}}};
    }
    if (IsScreenshotRequest("loop-mode-menu"_s)) {
        auto const r = named("loop-mode-menu"_s);
        if (!r) return k_nullopt;
        auto const pad = WwToPixels(10.0f);
        return CaptureSpec {.rect = {.xywh {r->x - pad, r->y - pad, r->w + (pad * 2), r->h + (pad * 2)}}};
    }

    // --- Mid-panel tabs --------------------------------------------------------------------------------
    // The whole window, with the open page's tab outlined.
    if (IsScreenshotRequest("page-perform"_s) || IsScreenshotRequest("page-layers"_s) ||
        IsScreenshotRequest("page-effects"_s)) {
        if (IsScreenshotRequest("page-perform"_s)) SetPerformPanelCollapseState(g, false);
        auto const tab = named("mid-panel.selected-tab"_s);
        if (!tab) return k_nullopt;
        auto const ws = GuiIo().in.window_size;
        CaptureSpec spec {.rect = {.xywh {0.0f, 0.0f, (f32)ws.width, (f32)ws.height}}};
        AppendOverlay(spec, "tab"_s, tab->Expanded(WwToPixels(3.0f)));
        return spec;
    }
    // The top of the window, down to the Perform page's preset arrows: the PERFORM tab and the arrows
    // outlined.
    if (IsScreenshotRequest("perform-tab-and-arrows"_s)) {
        auto const tab = named("mid-panel.selected-tab"_s);
        auto const prev_preset = named("perform.prev-preset"_s);
        auto const next_preset = named("perform.next-preset"_s);
        auto const preset_info = named("perform.preset-info"_s);
        if (!(tab && prev_preset && next_preset && preset_info)) return k_nullopt;
        auto const preset_nav_tl = ::Min(::Min(prev_preset->Min(), next_preset->Min()), preset_info->Min());
        auto const preset_nav_br = ::Max(::Max(prev_preset->Max(), next_preset->Max()), preset_info->Max());
        auto const bottom = ({
            auto const folder_badge = named("perform.folder-badge"_s);
            folder_badge ? ::Max(preset_nav_br.y, folder_badge->Bottom()) : preset_nav_br.y;
        });
        auto const ws = GuiIo().in.window_size;
        CaptureSpec spec {.rect = {.xywh {0.0f, 0.0f, (f32)ws.width, bottom + WwToPixels(8.0f)}}};
        AppendOverlay(spec, "tab"_s, tab->Expanded(WwToPixels(3.0f)));
        AppendOverlay(spec, "preset-nav"_s, {.pos = preset_nav_tl, .size = preset_nav_br - preset_nav_tl});
        return spec;
    }
    if (IsScreenshotRequest("layers"_s)) {
        auto spec = simple("mid-panel"_s);
        auto const controls = named("layer-top.controls"_s);
        auto const tabs = named("layer.page-tabs"_s);
        auto const page = named("layer.page-container"_s);
        auto const random_all = named("layers.random-all-button"_s);
        auto const unload_all = named("layers.unload-all-button"_s);
        if (!(spec && controls && tabs && page && random_all && unload_all)) return k_nullopt;
        AppendOverlay(*spec, "layer-top"_s, *controls);
        AppendOverlay(*spec, "page-tabs"_s, *tabs);
        AppendOverlay(*spec, "page"_s, *page);
        auto const all_layers_tl = ::Min(random_all->Min(), unload_all->Min());
        auto const all_layers_br = ::Max(random_all->Max(), unload_all->Max());
        AppendOverlay(*spec,
                      "all-layers-buttons"_s,
                      {.pos = all_layers_tl, .size = all_layers_br - all_layers_tl});
        return spec;
    }
    if (IsScreenshotRequest("knob"_s)) {
        // The Filter effect's controls: its Cutoff knob shows a macro moving it in the test preset.
        auto const graph = named("effects.filter-graph"_s);
        auto const resonance = named("effects.filter-resonance-knob"_s);
        if (!(graph && resonance)) return k_nullopt;
        auto const pad = WwToPixels(8.0f);
        auto const tl = ::Min(graph->Min(), resonance->Min()) - pad;
        auto const br = ::Max(graph->Max(), resonance->Max()) + pad;
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    }
    if (IsScreenshotRequest("knob-menu"_s)) {
        // The menu opens beneath the Cutoff knob, which sits beside Reso.
        auto const resonance = named("effects.filter-resonance-knob"_s);
        auto const menu = named("knob-menu"_s);
        if (!(resonance && menu)) return k_nullopt;
        // The knob's arc overhangs its rect, and the next effect's knobs sit just below the menu.
        auto const side_pad = WwToPixels(10.0f);
        auto const tl = ::Min(resonance->Min(), menu->Min()) - f32x2 {side_pad, WwToPixels(16.0f)};
        auto const br = ::Max(resonance->Max(), menu->Max()) + f32x2 {side_pad, WwToPixels(3.0f)};
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    }
    if (IsScreenshotRequest("effects"_s)) {
        auto spec = simple("mid-panel"_s);
        auto const switchboard = named("effects.switchboard"_s);
        auto const rack = named("effects.rack"_s);
        auto const heading = named("effects.second-heading"_s);
        auto const common_controls = named("effects.first-common-controls"_s);
        auto const randomise = named("effects.randomise-button"_s);
        auto const remove_all = named("effects.remove-all-button"_s);
        if (!(spec && switchboard && rack && heading && common_controls && randomise && remove_all))
            return k_nullopt;
        AppendOverlay(*spec, "switchboard"_s, *switchboard);
        AppendOverlay(*spec, "rack"_s, *rack);
        AppendOverlay(*spec, "heading"_s, *heading);
        AppendOverlay(*spec, "common-controls"_s, *common_controls);
        AppendOverlay(*spec,
                      "all-effects-buttons"_s,
                      {.pos = randomise->Min(), .size = remove_all->Max() - randomise->Min()});
        return spec;
    }
    if (IsEffectScreenshotRequest()) return simple("effects.screenshot-section"_s);
    if (IsScreenshotRequest("macros-play"_s)) return simple("bot-panel.play-macros"_s);
    if (IsScreenshotRequest("macros"_s)) {
        // The tabs and the first macro: the other macros are identical.
        auto const bot_panel = named("bot-panel"_s);
        auto const destinations = named("macros.first-destinations"_s);
        auto const add_button = named("macros.first-add-button"_s);
        auto const name = named("macros.first-name"_s);
        if (!(bot_panel && destinations && add_button && name)) return k_nullopt;
        auto const right = ::Max(add_button->Right(), name->Right()) + WwToPixels(16.0f);
        CaptureSpec spec {.rect = {.xywh {bot_panel->x, bot_panel->y, right - bot_panel->x, bot_panel->h}}};
        AppendOverlay(spec, "destinations"_s, *destinations);
        AppendOverlay(spec, "add-button"_s, *add_button);
        AppendOverlay(spec, "name"_s, *name);
        return spec;
    }
    if (IsScreenshotRequest("perform"_s)) {
        SetPerformPanelCollapseState(g, false);
        auto spec = simple("mid-panel"_s);
        auto const prev_preset = named("perform.prev-preset"_s);
        auto const next_preset = named("perform.next-preset"_s);
        auto const preset_info = named("perform.preset-info"_s);
        auto const folder_badge = named("perform.folder-badge"_s);
        auto const macros = named("perform.macros"_s);
        auto const layers = named("perform.layers"_s);
        auto const variation_strip = named("perform.variation-strip"_s);
        auto const compare = named("perform.compare"_s);
        auto const description = named("perform.description"_s);
        auto const collapse_icon = named("perform.collapse-icon"_s);
        auto const keyboard = named("bot-panel"_s);
        auto const top_panel = named("top-panel"_s);
        auto const preset_box = named("top-panel.preset-box"_s);
        if (!(spec && prev_preset && next_preset && preset_info && macros && layers && variation_strip &&
              folder_badge && compare && description && collapse_icon && keyboard && top_panel && preset_box))
            return k_nullopt;
        // The keyboard below is part of the Perform page too, and the top panel's preset box opens the preset
        // browser.
        spec->rect = Rect::FromMinMax(::Min(top_panel->Min(), ::Min(spec->rect.Min(), keyboard->Min())),
                                      ::Max(top_panel->Max(), ::Max(spec->rect.Max(), keyboard->Max())));
        auto const preset_nav_tl = ::Min(::Min(prev_preset->Min(), next_preset->Min()), preset_info->Min());
        auto const preset_nav_br = ::Max(::Max(prev_preset->Max(), next_preset->Max()), preset_info->Max());
        AppendOverlay(*spec, "preset-box"_s, *preset_box);
        AppendOverlay(*spec, "preset-nav"_s, {.pos = preset_nav_tl, .size = preset_nav_br - preset_nav_tl});
        AppendOverlay(*spec, "folder-badge"_s, *folder_badge);
        AppendOverlay(*spec, "macros"_s, *macros);
        AppendOverlay(*spec, "layers"_s, *layers);
        AppendOverlay(*spec, "variation-strip"_s, *variation_strip);
        AppendOverlay(*spec, "compare"_s, *compare);
        AppendOverlay(*spec, "description"_s, *description);
        AppendOverlay(*spec, "collapse-icon"_s, collapse_icon->Expanded(WwToPixels(4.0f)));
        AppendOverlay(*spec, "keyboard"_s, *keyboard);
        return spec;
    }

    // --- PERFORM variation strip -----------------------------------------------------------------------
    if (IsScreenshotRequest("perform-variation-strip"_s)) {
        auto const pill = named("perform.variation-strip"_s);
        if (!pill) return k_nullopt;
        auto const h_pad = WwToPixels(20.0f);
        auto const v_pad = WwToPixels(20.0f);
        return CaptureSpec {
            .rect = {.xywh {pill->x - h_pad, pill->y - v_pad, pill->w + (h_pad * 2), pill->h + (v_pad * 2)}}};
    }

    // --- Bottom-panel keyboard -------------------------------------------------------------------------
    if (IsScreenshotRequest("key-range-bars"_s) || IsScreenshotRequest("key-range-enlarged"_s)) {
        auto const bot_panel = named("bot-panel"_s);
        if (!bot_panel) return k_nullopt;
        if (IsScreenshotRequest("key-range-bars"_s)) return CaptureSpec {.rect = *bot_panel};

        auto const popup = named("key-range-enlarged-popup"_s);
        if (!popup) return k_nullopt;
        auto const margin = popup->Expanded(WwToPixels(20.0f));
        auto const tl = ::Min(bot_panel->Min(), margin.Min());
        auto const br = ::Max(bot_panel->Max(), margin.Max());
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    }

    if (IsScreenshotRequest("keyboard"_s)) {
        auto spec = simple("bot-panel"_s);
        auto const tabs = named("bot-panel.tabs"_s);
        auto const macros = named("bot-panel.play-macros"_s);
        auto const key_range_bars = named("keyboard.key-range-bars"_s);
        auto const keys = named("keyboard.keys"_s);
        auto const octave = named("bot-panel.octave"_s);
        if (!(spec && tabs && macros && key_range_bars && keys && octave)) return k_nullopt;
        AppendOverlay(*spec, "tabs"_s, *tabs);
        AppendOverlay(*spec, "macros"_s, *macros);
        AppendOverlay(*spec, "key-range-bars"_s, *key_range_bars);
        AppendOverlay(*spec, "keys"_s, *keys);
        AppendOverlay(*spec, "octave"_s, *octave);
        return spec;
    }

    // --- Info-panel modals -----------------------------------------------------------------------------
    if (IsScreenshotRequest("check-for-updates"_s)) return simple("info-panel.modal"_s);
    if (IsScreenshotRequest("uninstall-library"_s)) return simple("info-panel.first-library-card"_s);

    // --- Preset browser uninstall ----------------------------------------------------------------------
    if (IsScreenshotRequest("uninstall-preset-bank"_s)) {
        auto const bank = named("preset-browser.first-bank"_s);
        auto const menu = named("preset-browser.folder-menu"_s);
        if (!(bank && menu)) return k_nullopt;
        auto const tl = ::Min(bank->Min(), menu->Min());
        auto const br = ::Max(bank->Max(), menu->Max());
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    }

    // --- Performance configuration panel ----------------------------------------------------------------
    if (IsScreenshotRequest("performance-controls"_s)) {
        auto spec = simple("performance-controls-panel.modal"_s);
        auto const profiles = named("performance-controls.profiles"_s);
        auto const defaults_row = named("performance-controls.defaults-row"_s);
        auto const tabs = named("performance-controls.tabs"_s);
        if (!(spec && profiles && defaults_row && tabs)) return k_nullopt;
        AppendOverlay(*spec, "profiles"_s, *profiles);
        AppendOverlay(*spec, "defaults"_s, *defaults_row);
        AppendOverlay(*spec, "tabs"_s, *tabs);
        return spec;
    }
    if (IsScreenshotRequest("reproducibility"_s)) {
        auto const modal = named("performance-controls-panel.modal"_s);
        auto const controls = named("reproducibility.controls"_s);
        auto const reset_on_transport = named("reproducibility.reset-on-transport"_s);
        auto const reset_keyswitch = named("reproducibility.reset-keyswitch"_s);
        auto const seed = named("reproducibility.seed"_s);
        if (!(modal && controls && reset_on_transport && reset_keyswitch && seed)) return k_nullopt;
        // The rest of the modal is empty.
        auto const bottom_y = controls->Bottom() + WwToPixels(10.0f);
        CaptureSpec spec {.rect = {.xywh {modal->x, modal->y, modal->w, bottom_y - modal->y}}};
        auto const pad = WwToPixels(3.0f);
        AppendOverlay(spec, "reset-on-transport"_s, reset_on_transport->Expanded(pad));
        AppendOverlay(spec, "reset-keyswitch"_s, reset_keyswitch->Expanded(pad));
        AppendOverlay(spec, "seed"_s, seed->Expanded(pad));
        return spec;
    }
    if (IsScreenshotRequest("midi-cc"_s)) return simple("performance-controls-panel.modal"_s);
    if (IsScreenshotRequest("mpe"_s)) {
        auto const modal = named("performance-controls-panel.modal"_s);
        auto const controls = named("mpe.controls"_s);
        if (!(modal && controls)) return k_nullopt;
        // The rest of the modal is empty.
        auto const bottom_y = controls->Bottom() + WwToPixels(10.0f);
        return CaptureSpec {.rect = {.xywh {modal->x, modal->y, modal->w, bottom_y - modal->y}}};
    }

    // --- Preferences panel -----------------------------------------------------------------------------
    if (IsScreenshotRequest("folders"_s)) return simple("prefs-panel.modal"_s);
    if (IsScreenshotRequest("install-packages"_s)) {
        auto const modal = named("prefs-panel.modal"_s);
        auto const last_row = named("prefs-install.last-row"_s);
        if (!(modal && last_row)) return k_nullopt;
        auto const bottom_y = last_row->Bottom() + WwToPixels(16.0f);
        return CaptureSpec {.rect = {.xywh {modal->x, modal->y, modal->w, bottom_y - modal->y}}};
    }

    // --- Top-panel dots-menu update indicator ----------------------------------------------------------
    if (IsScreenshotRequest("update-indicator"_s)) {
        auto const dots = named("top-panel.dots-button"_s);
        if (!dots) return k_nullopt;
        auto const h_pad = WwToPixels(120.0f);
        auto const v_pad = WwToPixels(20.0f);
        return CaptureSpec {
            .rect = {.xywh {dots->x - h_pad, dots->y - v_pad, dots->w + (h_pad * 2), dots->h + (v_pad * 2)}}};
    }

    // --- Browsers --------------------------------------------------------------------------------------
    // A browser and the element that opened it are drawn as one shape, so they're captured together.
    auto const browser_with_opener = [&](Rect opener) -> Optional<CaptureSpec> {
        auto const modal = named("browser.modal"_s);
        if (!modal || All(opener.size == 0)) return k_nullopt;
        auto const tl = ::Min(opener.Min(), modal->Min());
        auto const br = ::Max(opener.Max(), modal->Max());
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    };
    auto const inst_opener = g.inst_browser_state[0].common_state.absolute_button_rect;

    if (IsScreenshotRequest("browser-browse"_s)) {
        auto spec = browser_with_opener(inst_opener);
        auto const results = named("browser.results-panel"_s);
        auto const summary = named("browser.results-summary"_s);
        auto const filters = named("browser.filters-panel"_s);
        auto const breadcrumb = named("browser.breadcrumb-row"_s);
        auto const mode = named("browser.mode-toggle"_s);
        if (!(spec && results && summary && filters && breadcrumb && mode)) return k_nullopt;
        AppendOverlay(*spec,
                      "results-list"_s,
                      {.xywh {results->x, results->y, results->w, summary->y - results->y}});
        AppendOverlay(*spec,
                      "filters-page"_s,
                      {.xywh {filters->x, filters->y, filters->w, breadcrumb->y - filters->y}});
        AppendOverlay(*spec, "opener"_s, inst_opener);
        AppendOverlay(*spec, "breadcrumb"_s, *breadcrumb);
        AppendOverlay(*spec, "mode-toggle"_s, *mode);
        return spec;
    }
    if (IsScreenshotRequest("browser-full"_s)) {
        auto spec = browser_with_opener(inst_opener);
        auto const filters = named("browser.filters-panel"_s);
        auto const filters_toolbar = named("browser.filters-toolbar"_s);
        auto const summary = named("browser.results-summary"_s);
        auto const mode = named("browser.mode-toggle"_s);
        auto const match = named("browser.match-button"_s);
        if (!(spec && filters && filters_toolbar && summary && mode && match)) return k_nullopt;
        AppendOverlay(*spec,
                      "filters-tree"_s,
                      {.xywh {filters->x, filters->y, filters->w, filters_toolbar->y - filters->y}});
        AppendOverlay(*spec, "results-summary"_s, *summary);
        AppendOverlay(*spec, "mode-toggle"_s, *mode);
        AppendOverlay(*spec, "match-button"_s, *match);
        return spec;
    }
    if (IsScreenshotRequest("browser-menu"_s)) return simple("browser.match-menu"_s);
    if (IsScreenshotRequest("preset-browser"_s)) {
        // Cut off a little below the loaded preset's highlighted row.
        auto const opener = g.preset_browser_state.common_state.absolute_button_rect;
        auto spec = browser_with_opener(opener);
        auto const current_item = named("preset-browser.current-item"_s);
        if (!(spec && current_item)) return k_nullopt;
        spec->rect.h = current_item->Bottom() + WwToPixels(40.0f) - spec->rect.y;
        AppendOverlay(*spec, "preset-box"_s, opener);
        return spec;
    }

    return k_nullopt;
}

void MaybeFireScreenshot(GuiState& g) {
    if (IsAnyScreenshotInProgress())
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);

    if (GuiIo().in.requested_screenshot_id_name.size == 0) return;

    auto const spec = ResolveCapture(g);
    if (!spec || !ScreenshotPreconditionsMet(g)) {
        // Required named rects not registered yet, or async loads still in flight: retry next frame.
        GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);
        return;
    }

    auto rect = spec->rect;
    if (GuiIo().in.requested_screenshot_full_window) {
        auto const ws = GuiIo().in.window_size;
        rect = {.xywh {0.0f, 0.0f, (f32)ws.width, (f32)ws.height}};
    }
    GuiFrameOutput::ScreenshotRequest req {
        .rect = rect,
        .output_path = GuiIo().in.requested_screenshot_output_path,
    };
    for (auto o : spec->overlays) {
        if (auto const content = spec->layer_page_content) {
            auto const tolerance = 1.0f;
            auto const outset = WwToPixels(6.0f);
            auto const min_x =
                o.rect.x <= content->x + tolerance ? Min(o.rect.x, content->x - outset) : o.rect.x;
            auto const max_x = o.rect.Right() >= content->Right() - tolerance
                                   ? Max(o.rect.Right(), content->Right() + outset)
                                   : o.rect.Right();
            o.rect.x = min_x;
            o.rect.w = max_x - min_x;
        }
        dyn::Append(req.overlays, o);
    }
    GuiIo().out.request_screenshot = req;
}

void MaybeWriteUiDump(GuiState& g) {
    auto const out_path = String {GuiIo().in.ui_dump_output_path};
    if (!out_path.size) return;

    auto const loading = g.engine.pending_state_change.HasValue() || AsyncLoadsInProgress(g);
    // Keep updating while loading so that the dump reports when loading finishes.
    if (loading) GuiIo().out.IncreaseUpdateInterval(GuiFrameOutput::UpdateInterval::Animate);

    auto& arena = g.scratch_arena;
    DynamicArray<char> json_buf {arena};
    auto writer = dyn::WriterFor(json_buf);
    json::WriteContext ctx {.out = writer, .add_whitespace = true};
    auto const write_json = [&]() -> ErrorCodeOr<void> {
        TRY(json::WriteObjectBegin(ctx));
        TRY(json::WriteKeyValue(ctx, "window_width", GuiIo().in.window_size.width));
        TRY(json::WriteKeyValue(ctx, "window_height", GuiIo().in.window_size.height));
        TRY(json::WriteKeyValue(ctx, "loading", loading));
        TRY(json::WriteKeyArrayBegin(ctx, "elements"));
        for (auto const& element : g.imgui.interactive_elements) {
            TRY(json::WriteObjectBegin(ctx));
            TRY(json::WriteKeyValue(ctx, "x", (s32)Round(element.rect.x)));
            TRY(json::WriteKeyValue(ctx, "y", (s32)Round(element.rect.y)));
            TRY(json::WriteKeyValue(ctx, "w", (s32)Round(element.rect.w)));
            TRY(json::WriteKeyValue(ctx, "h", (s32)Round(element.rect.h)));
            if (element.name.size) TRY(json::WriteKeyValue(ctx, "name", element.name));
            if (element.text.size) TRY(json::WriteKeyValue(ctx, "text", element.text));
            if (element.value.size) TRY(json::WriteKeyValue(ctx, "value", element.value));
            if (element.tooltip.size) TRY(json::WriteKeyValue(ctx, "tooltip", element.tooltip));
            TRY(json::WriteObjectEnd(ctx));
        }
        TRY(json::WriteArrayEnd(ctx));
        TRY(json::WriteObjectEnd(ctx));
        return k_success;
    };
    if (auto const r = write_json(); r.HasError()) {
        LogError(ModuleName::Gui, "Failed to build UI dump JSON: {}", r.Error());
        return;
    }

    if (String {json_buf} == String {g.last_ui_dump}) return;
    dyn::Assign(g.last_ui_dump, json_buf);

    // Write then rename so that readers never see a partially written file.
    DynamicArray<char> temp_path {arena};
    dyn::AppendSpan(temp_path, out_path);
    dyn::AppendSpan(temp_path, ".tmp"_s);
    if (auto const w = WriteFile(temp_path, json_buf); w.HasError())
        LogError(ModuleName::Gui, "Failed to write UI dump: {}", w.Error());
    else if (auto const rn = Rename(temp_path, out_path); rn.HasError())
        LogError(ModuleName::Gui, "Failed to rename UI dump: {}", rn.Error());
}
