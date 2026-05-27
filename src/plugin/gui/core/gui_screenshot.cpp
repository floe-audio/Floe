// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/core/gui_screenshot.hpp"

#include "foundation/foundation.hpp"

#include "common_infrastructure/sample_library/server/sample_library_server.hpp"

#include "engine/engine.hpp"
#include "gui/core/gui_library_images.hpp"
#include "gui/core/gui_state.hpp"
#include "gui/core/gui_waveform_images.hpp"
#include "gui/panels/gui_perform.hpp"
#include "gui_framework/gui_frame.hpp"
#include "gui_framework/gui_imgui.hpp"

bool IsScreenshotRequest(String id_name) {
    return IsEqualToCaseInsensitiveAscii(GuiIo().in.requested_screenshot_id_name, id_name);
}

bool IsAnyScreenshotInProgress() {
    return GuiIo().in.requested_screenshot_id_name.size != 0 || GuiIo().out.request_screenshot.HasValue();
}

static bool ScreenshotPreconditionsMet(GuiState& g) {
    auto const clear_this_frame = [&]() {
        if (sample_lib_server::AreLibrariesScanning(g.shared_engine_systems.sample_library_server))
            return false;
        for (auto& pct : g.engine.sample_lib_server_async_channel.instrument_loading_percents)
            if (pct.Load(LoadMemoryOrder::Relaxed) != -1) return false;
        for (auto const& [_, imgs, _] : g.library_images.table) {
            if (imgs.loading_icon && imgs.loading_icon->IsInProgress()) return false;
            if (imgs.loading_backgrounds && imgs.loading_backgrounds->IsInProgress()) return false;
        }
        for (auto const& [_, w, _] : g.waveform_images.table)
            if (w.loading_pixels && w.loading_pixels->IsInProgress()) return false;
        return true;
    }();

    // Bit hacky, but we wait a couple of frames to ensure images are actually rendered.
    if (!clear_this_frame) {
        g.screenshot_consecutive_clear_frames = 0;
        return false;
    }
    g.screenshot_consecutive_clear_frames++;
    return g.screenshot_consecutive_clear_frames >= 2;
}

struct CaptureSpec {
    Rect rect; // window-coord pixels
    DynamicArrayBounded<GuiFrameOutput::ScreenshotRequest::Overlay, 16> overlays;
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

    // --- Overview: whole window with labelled regions --------------------------------------------------
    if (IsScreenshotRequest("overview"_s)) {
        SetPerformPanelCollapseState(g, false);
        auto const top = named("top-panel"_s);
        auto const tab_row = named("mid-panel.tab-bar"_s);
        auto const content = named("mid-panel"_s);
        auto const bot = named("bot-panel"_s);
        auto const resize_corner = named("resize-corner"_s);
        if (!(top && tab_row && content && bot && resize_corner)) return k_nullopt;
        auto const tl = ::Min(::Min(top->Min(), tab_row->Min()), ::Min(content->Min(), bot->Min()));
        auto const br = ::Max(::Max(top->Max(), tab_row->Max()), ::Max(content->Max(), bot->Max()));
        CaptureSpec spec {.rect = {.pos = tl, .size = br - tl}};
        AppendOverlay(spec, "top-panel"_s, *top);
        AppendOverlay(spec, "main-content"_s, *content);
        AppendOverlay(spec, "bot-panel"_s, *bot);
        AppendOverlay(spec, "tab-row"_s, *tab_row);
        AppendOverlay(spec, "resize-corner"_s, *resize_corner);
        return spec;
    }

    // --- Top panel only -------------------------------------------------------------------------------
    if (IsScreenshotRequest("top-panel"_s)) return simple("top-panel"_s);

    // --- Save preset modal ----------------------------------------------------------------------------
    if (IsScreenshotRequest("save-preset"_s)) return simple("save-preset-panel.modal"_s);

    // --- Layer screenshots (layer 0) -------------------------------------------------------------------
    if (IsScreenshotRequest("layer-top-controls"_s)) {
        auto const inst_selector = named("layer-top.inst-selector"_s);
        auto const inst_navigation = named("layer-top.inst-navigation"_s);
        auto const mute_solo = named("layer-top.mute-solo"_s);
        auto const controls = named("layer-top.controls"_s);
        if (!(inst_selector && inst_navigation && mute_solo && controls)) return k_nullopt;
        CaptureSpec spec {.rect = *controls};
        AppendOverlay(spec, "inst-selector"_s, *inst_selector);
        AppendOverlay(spec, "inst-navigation"_s, *inst_navigation);
        AppendOverlay(spec, "mute-solo"_s, *mute_solo);
        return spec;
    }
    if (IsScreenshotRequest("layer-main"_s) || IsScreenshotRequest("layer-playback"_s) ||
        IsScreenshotRequest("layer-playback-granular-speed"_s) ||
        IsScreenshotRequest("layer-playback-granular-fixed"_s) || IsScreenshotRequest("layer-lfo"_s) ||
        IsScreenshotRequest("layer-eq"_s) || IsScreenshotRequest("layer-arp"_s) ||
        IsScreenshotRequest("layer-config"_s)) {
        auto const tabs = named("layer.page-tabs"_s);
        auto const page = named("layer.page-container"_s);
        if (!(tabs && page)) return k_nullopt;
        auto const tl = ::Min(tabs->Min(), page->Min());
        auto const br = ::Max(tabs->Max(), page->Max());
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
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
    if (IsScreenshotRequest("perform"_s) || IsScreenshotRequest("layers"_s) ||
        IsScreenshotRequest("effects"_s)) {
        return simple("mid-panel"_s);
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

    // --- Info-panel modals -----------------------------------------------------------------------------
    if (IsScreenshotRequest("check-for-updates"_s)) return simple("info-panel.modal"_s);
    if (IsScreenshotRequest("uninstall-library"_s)) return simple("info-panel.first-library-card"_s);

    // --- Preset browser uninstall ----------------------------------------------------------------------
    if (IsScreenshotRequest("uninstall-preset-bank"_s)) {
        auto const card = named("preset-browser.first-bank-card"_s);
        auto const menu = named("preset-browser.folder-menu"_s);
        if (!(card && menu)) return k_nullopt;
        auto const tl = ::Min(card->Min(), menu->Min());
        auto const br = ::Max(card->Max(), menu->Max());
        return CaptureSpec {.rect = {.pos = tl, .size = br - tl}};
    }

    // --- Instance config panel -------------------------------------------------------------------------
    if (IsScreenshotRequest("instance-config"_s)) return simple("instance-config-panel.modal"_s);

    // --- MIDI CC assignments panel ---------------------------------------------------------------------
    if (IsScreenshotRequest("midi-cc-assignments"_s)) return simple("midi-cc-panel.modal"_s);

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

    // --- Instrument browser ----------------------------------------------------------------------------
    if (IsScreenshotRequest("browser-full"_s)) {
        auto const modal = named("browser.modal"_s);
        auto const filters = named("browser.filters-panel"_s);
        auto const results = named("browser.results-panel"_s);
        auto const mode = named("browser.mode-selector"_s);
        if (!(modal && filters && results && mode)) return k_nullopt;
        CaptureSpec spec {.rect = *modal};
        AppendOverlay(spec, "filters-panel"_s, *filters);
        AppendOverlay(spec, "results-panel"_s, *results);
        AppendOverlay(spec, "mode-selector"_s, *mode);
        return spec;
    }
    if (IsScreenshotRequest("filter-card"_s)) {
        auto const outer = named("library-card.Dulcitone"_s);
        auto const header = named("library-card.Dulcitone.header"_s);
        auto const body = named("library-card.Dulcitone.body"_s);
        if (!(outer && header && body)) return k_nullopt;
        CaptureSpec spec {.rect = *outer};
        AppendOverlay(spec, "header"_s, *header);
        AppendOverlay(spec, "body"_s, *body);
        return spec;
    }
    if (IsScreenshotRequest("filter-card-all-selected"_s) ||
        IsScreenshotRequest("filter-card-body-item-selected"_s) ||
        IsScreenshotRequest("filter-card-body-tree"_s)) {
        return simple("library-card.Dulcitone"_s);
    }
    if (IsScreenshotRequest("filter-button"_s)) return simple("browser.favourites-button"_s);
    if (IsScreenshotRequest("browser-menu"_s)) return simple("browser.more-options-menu"_s);

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

    GuiFrameOutput::ScreenshotRequest req {
        .rect = spec->rect,
        .output_path = GuiIo().in.requested_screenshot_output_path,
    };
    for (auto const& o : spec->overlays)
        dyn::Append(req.overlays, o);
    GuiIo().out.request_screenshot = req;
}
