<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

# GUI Layer Refactor Guide

Context, best practices, and important files for incrementally converting the legacy `gui_layer.cpp` to the modern GuiBuilder system.

## Goal

Replace the legacy two-pass approach in `gui_layer.cpp` (manual `layout::CreateItem` in `Layout()` + separate `Draw()` pass using `gui/old/*` APIs) with the modern `GuiBuilder` approach where layout, interaction, and rendering happen together via `DoBox`. The conversion is done incrementally, section by section, in the new file `gui_layer_new.cpp`.

## Architecture

### Current state

- **`gui_layer.cpp`** (legacy): `Layout()` builds a tree of `layout::Id` nodes with `layout::CreateItem`, then `Draw()` manually renders each element using old APIs (`buttons::Button`, `labels::Label` from `gui/old/*`). It creates an `imgui::BeginViewport`/`EndViewport` pair for each layer.
- **`gui_layer_new.cpp`** (new, in progress): Uses `GuiBuilder` with `DoBox` calls. Called from `gui_mid_panel.cpp` for layer 0, while layers 1 and 2 still use the legacy path.
- **`gui_mid_panel.cpp`**: Creates a `Box` for each layer and dispatches to either `layer_gui_new::DoLayerPanel` or the legacy `layer_gui::Layout`+`Draw`.

### Entry point (gui_mid_panel.cpp)

```cpp
for (auto const i : Range(k_num_layers)) {
    auto const layer_box = DoBox(builder, { .parent = layers_row, .id_extra = (u64)i, ... });

    if (i == 0) {
        layer_gui_new::DoLayerPanel(g, frame_context, i, layer_box);
    } else if (auto const r = BoxRect(builder, layer_box)) {
        // legacy path: Layout + RunContext + Draw + ResetContext
    }
}
```

### New file structure (gui_layer_new.cpp)

`DoLayerPanel` receives a `Box parent` from the mid panel and creates a root column inside it. Each GUI section is a separate static function called from `DoLayerPanel`. The function signature is:

```cpp
void DoLayerPanel(GuiState& g, GuiFrameContext const& frame_context, u32 layer_index, Box parent);
```

Note: `GuiBuilder` is accessed via `g.builder` rather than passed as a separate argument.

### What's converted so far

- **Instrument selector** (`DoInstSelector`): The row with instrument name button (opens browser modal), library icon, prev/next arrows, shuffle button, right-click "Unload instrument" menu, timbre-highlight background, and loading progress bar.

## Key files

| File | Purpose |
|------|---------|
| `src/plugin/gui/panels/gui_layer_new.hpp` | New layer GUI header |
| `src/plugin/gui/panels/gui_layer_new.cpp` | New layer GUI implementation (work in progress) |
| `src/plugin/gui/panels/gui_layer.hpp` | Legacy layer GUI header - has `LayerLayoutTempIDs` showing all layout elements |
| `src/plugin/gui/panels/gui_layer.cpp` | Legacy layer GUI - reference for what to convert |
| `src/plugin/gui/panels/gui_mid_panel.cpp` | Parent panel dispatching to new/legacy layer GUI |
| `src/plugin/gui/elements/gui_common_elements.hpp/.cpp` | Reusable elements: `DoMidPanelPrevNextButtons`, `DoMidPanelShuffleButton`, `Tooltip` |
| `src/plugin/gui/elements/gui_param_elements.hpp/.cpp` | Parameter controls (knobs, popup buttons) using GuiBuilder |
| `src/plugin/gui_framework/gui_builder.hpp` | GuiBuilder API reference |
| `src/plugin/gui_framework/layout.hpp` | Layout system reference |
| `src/plugin/gui/panels/gui_inst_browser.hpp` | `InstBrowserContext`, `LoadAdjacentInstrument`, `LoadRandomInstrument` |
| `src/plugin/gui/live_edit_defs/gui_sizes.def` | Size constants (LivePx/LiveWw values) |
| `src/plugin/gui/live_edit_defs/gui_colour_map.def` | Colour constants |

## Legacy section map

These are the sections in `gui_layer.cpp`'s `Layout()` (line ~343) and `Draw()` (line ~1019), listed top-to-bottom. The `LayerLayoutTempIDs` struct in `gui_layer.hpp` shows every layout ID.

| Section | Layout lines | Draw lines | Status |
|---------|-------------|------------|--------|
| Instrument selector | 363-411 | 1048-1156 | DONE |
| Mixer container 1 (volume knob, mute/solo, level meter) | 416-453 | 1160-1223 | DONE |
| Mixer container 2 (3 context-dependent knobs) | 455-500 | 1224-1248 | DONE |
| Divider | 502-510 | 1160-1161 | DONE |
| Page tabs (Main/Filter/LFO/EQ/Play) | 512-540 | 1249+ (tab switching logic) | DONE |
| Divider 2 | 534-540 | same area | DONE |
| Page content: Main page | 568-655 | 1251-1291 | DONE |
| Page content: Filter page | 657-724 | 1293-1332 | TODO |
| Page content: EQ page | 726-820 | 1334-1383 | TODO |
| Page content: Play page | 822-940 | 1385-1484 | TODO |
| Page content: LFO page | 942-1008 | 1486-1560 | TODO |

Note: `Layout()` returns early at line 413 if no instrument is loaded (`instrument.tag == InstrumentType::None`), so everything below the selector only appears when an instrument is loaded.

## Best practices

Established during the mid panel conversion (commits `87604076..07727c49`):

### Layout
- **`contents_padding` on containers** instead of `margins` on children. Note: `contents_padding` only works on boxes that have children — it has no effect on leaf boxes (e.g. `size_from_text` text boxes).
- **`contents_gap`** for spacing between siblings rather than individual margins.
- **`layout::k_fill_parent`** instead of manually calculating sizes.
- **`layout::k_hug_contents`** for containers that should size to their content.
- **`DoWhitespace(builder, parent, height)`** for explicit vertical spacing between sections — prefer this over margins on dividers or other structural elements. Keeps spacing visible and adjustable via `UiSizeId`s.
- **2-box hierarchy for clickable text buttons**: outer box is `k_hug_contents` with `button_behaviour`; inner box has `size_from_text = true`, `parent_dictates_hot_and_active = true`, and margins for padding. This gives a larger clickable area driven by inner text size. See `DoMidIconButton` in `gui_common_elements.cpp` for the pattern.
- **Use new `UiSizeId`s** rather than raw values or repurposing legacy IDs. Give them clear, descriptive names (e.g. `LayerDividerGapAbove` not `LayerMixerDividerVertMargins`). Add them to `gui_sizes.def`.

### Units
- **`LiveWw()` for all sizes in `DoBox`** config (GuiBuilder layout works in window-width-relative units).
- **`LivePx()` only in render-pass draw calls** (draw_list operations work in pixels).
- **`LiveCol()` for `u32` colours** (draw_list), **`LiveColStruct()` for `ColourTint`/`ColSet`** (DoBox config).

### Style
- **Avoid aliases** - use `g.builder`, `g.imgui`, `g.engine` directly.
- **Use `LiveWw`/`LivePx`/`LiveCol` inline** rather than storing in locals.
- **Reduce variable scope** as much as possible.
- **Lean into GuiBuilder** - use `DoBox` tooltips, `button_behaviour`, text rendering rather than manual imgui calls.
- **`size_from_text = true`** for text-only boxes.
- **`parent_dictates_hot_and_active = true`** on children that share hover/active state with a parent button.

### IDs
- **Duplicate ID panics**: `DoBox` generates unique IDs by hashing: source location (via default `loc_hash` arg), `id_extra`, parent ID, and the IMGUI ID stack. If a helper function containing a single `DoBox` is called multiple times with the same parent, the IDs will collide. Fix this by adding a `u64 loc_hash = SourceLocationHash()` default parameter to the helper — each call site then automatically gets a unique hash. See the comment at the top of `gui_builder.hpp` for full details. This is rare; most functions contain multiple `DoBox` calls or are only called once per parent.

### Architecture
- **No includes of `gui/old/*`** in new code.
- **Avoid unnecessary `BoxViewport`s** - only for scrollable regions or popups.
- **Don't pass args that can be fetched from `GuiState`** inside the function.
- **Extract reusable elements** to `gui_common_elements.hpp/.cpp`.
- **Custom background drawing**: `BoxRect(g.builder, box)` to get rect, then `g.imgui.draw_list->AddRectFilled(...)`. Convert to window coords with `g.imgui.ViewportRectToWindowRect()`.

## How to add the next section

1. Read the legacy `Layout()` section to understand sizes, margins, and direction.
2. Read the legacy `Draw()` section for rendering and interaction logic.
3. Convert margins to `contents_padding` on parent containers.
4. Convert `LivePx` size values to `LiveWw` for DoBox configs.
5. Replace `buttons::Button(...)` with `DoBox(builder, { .button_behaviour = ... })`.
6. Replace `labels::Label(...)` with `DoBox(builder, { .text = ..., .size_from_text = true })`.
7. Replace manual draw calls with `BoxRect` + draw_list for custom rendering.
8. Add the new section as a child of the appropriate container in `DoLayerPanel`.
9. Run `nix develop --command zb` to verify compilation.
10. Visually verify layer 0 matches layers 1 and 2.

## Expanding to all layers

Once feature-complete and visually matching:
1. Change `if (i == 0)` to apply to all layers in `gui_mid_panel.cpp`.
2. Remove `gui_layer.cpp`/`.hpp` and associated types.
3. Remove `gui_layer.cpp` from `build.zig`.
