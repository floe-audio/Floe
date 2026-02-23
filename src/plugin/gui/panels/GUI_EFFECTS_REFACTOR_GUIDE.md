<!--
SPDX-FileCopyrightText: 2026 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

# GUI Effects Refactor Guide

Context, transformation patterns, and reference material for converting the legacy `gui_effects.cpp` to the modern GuiBuilder system. Based on the patterns established during the `gui_layer.cpp` refactor (commit `03e85df6`).

## Goal

Replace the legacy two-pass approach in `gui_effects.cpp` (manual `layout::CreateItem` in a layout phase + separate draw/event phase using `gui/old/*` APIs) with the modern `GuiBuilder` approach where layout, interaction, and rendering happen together via `DoBox`. The effects panel is more complex than the layer panel because it has drag-and-drop reordering, a switchboard of toggle buttons, and a scrollable viewport.

## Current state

- **`gui_effects.cpp`** (legacy, 1304 lines): Single function `DoEffectsViewport` that creates its own `imgui::BeginViewport`/`EndViewport`, builds a layout tree, runs it with `layout::RunContext`, then draws/handles events. Uses `gui/old/*` includes.
- **`gui_mid_panel.cpp`**: Calls `DoEffectsViewport(g, frame_context, *r)` inside `DoEffectsContainer`, passing a raw `Rect`.

## Key files

| File | Purpose |
|------|---------|
| `gui/panels/gui_effects.hpp` | Current header - single `DoEffectsViewport` declaration |
| `gui/panels/gui_effects.cpp` | Legacy implementation to convert |
| `gui/panels/gui_mid_panel.cpp` | Parent panel that calls effects code (line 353) |
| `gui/panels/gui_layer.cpp` | **Reference**: fully converted layer panel showing all patterns |
| `gui/elements/gui_param_elements.hpp/.cpp` | Modern parameter controls: `DoKnobParameter`, `DoMenuParameter`, `DoButtonParameter`, `DoIntParameter` |
| `gui/elements/gui_common_elements.hpp/.cpp` | Reusable elements: `DoMidPanelPrevNextButtons`, `DoMidPanelShuffleButton`, `Tooltip`, `DoToggleIcon` |
| `gui/panels/gui_macros.hpp` | `OverlayMacroDestinationRegion` - already called inside modern param elements |
| `gui_framework/gui_builder.hpp` | GuiBuilder API reference |
| `gui/live_edit_defs/gui_sizes.def` | Size constants |
| `gui/live_edit_defs/gui_colour_map.def` | Colour constants |
| `gui/old/gui_widget_compounds.hpp` | Legacy `LayIDPair`, `LayoutParameterComponent`, `KnobAndLabel` |

## Transformation patterns

These patterns were established in the layer refactor and should be applied consistently.

### 1. Remove old includes, add new ones

**Old:**
```cpp
#include "gui/elements/gui_element_drawing.hpp"
#include "gui/old/gui_dragger_widgets.hpp"
#include "gui/old/gui_label_widgets.hpp"
#include "gui/old/gui_menu.hpp"
#include "gui/old/gui_widget_compounds.hpp"
#include "gui/old/gui_widget_helpers.hpp"
```

**New:**
```cpp
#include "gui/elements/gui_common_elements.hpp"
#include "gui/elements/gui_param_elements.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui/panels/gui_macros.hpp"
#include "gui_framework/gui_live_edit.hpp"
```

### 2. Eliminate the EffectIDs struct and two-pass layout

The legacy code builds an `EffectIDs` struct with `layout::Id` and `LayIDPair` members during a layout phase, then reads those IDs back during a draw phase. In the new system, layout, interaction, and rendering happen together in a single pass via `DoBox`.

**Old pattern:**
```cpp
// Layout phase
EffectIDs ids;
ids.heading = layout::CreateItem(lay, arena, { .parent = root, .size = {...} });
auto param_container = layout::CreateItem(lay, arena, param_container_options);
LayoutParameterComponent(g, param_container, ids.distortion.type, ...);

layout::RunContext(lay);

// Draw phase
buttons::Button(g, id, layout::GetRect(lay, ids.heading), text, style);
KnobAndLabel(g, param, ids.distortion.amount, knobs::DefaultKnob(imgui, cols.highlight));
```

**New pattern:**
```cpp
// Everything in one pass
auto const heading = DoBox(g.builder, { .parent = root, .text = name, ... });
DoMenuParameter(g, param_container, params.DescribedValue(ParamIndex::DistortionType), {...});
DoKnobParameter(g, param_container, params.DescribedValue(ParamIndex::DistortionDrive), {...});
```

### 3. Parameter controls: old widgets to new elements

Each legacy widget type maps to a modern `gui_param_elements` function:

| Old | New | Notes |
|-----|-----|-------|
| `KnobAndLabel(g, param, lay_id_pair, knobs::DefaultKnob(...))` | `DoKnobParameter(g, parent, param, {.width = LiveWw(...), ...})` | Width in Ww units, not Px |
| `KnobAndLabel(g, param, lay_id_pair, knobs::BidirectionalKnob(...))` | `DoKnobParameter(g, parent, param, {.width = ..., .bidirectional = true})` | |
| `buttons::PopupWithItems(g, param, lay_id.control, style)` + `labels::Label(g, param, lay_id.label, style)` | `DoMenuParameter(g, parent, param, {.width = LiveWw(...)})` | Combines popup + label into one call |
| `buttons::Toggle(g, param, lay_id, style)` | `DoButtonParameter(g, parent, param, {.width = LiveWw(...)})` | For on/off toggle parameters |
| `draggers::Dragger(g, param, lay_id, style)` | `DoIntParameter(g, parent, param, {.width = LiveWw(...)})` | For integer draggers |
| `labels::Label(g, rect, text, style)` | `DoBox(g.builder, {.parent = p, .text = text, .text_colours = LiveColStruct(...), ...})` | Plain text labels |

**Important**: The modern parameter elements (`DoKnobParameter`, `DoMenuParameter`, etc.) already handle:
- Tooltips
- Context menus (right-click)
- Macro destination regions (`OverlayMacroDestinationRegion`)
- Labels (unless `.label = false`)

So you do **not** need to manually call `Tooltip()`, `AddParamContextMenuBehaviour()`, or `OverlayMacroDestinationRegion()` for parameters.

### 4. Units: LivePx to LiveWw

All sizes in `DoBox` config use window-width-relative units (`LiveWw`). Only render-pass draw calls (draw_list operations) use pixel units (`LivePx`).

**Old:**
```cpp
layout::CreateItem(lay, arena, { .size = {LivePx(FXCloseButtonWidth), LivePx(FXCloseButtonHeight)} });
```

**New:**
```cpp
DoBox(g.builder, { .layout { .size = {LiveWw(UiSizeId::FXCloseButtonWidth), LiveWw(UiSizeId::FXCloseButtonHeight)} } });
```

Draw calls still use `LivePx`:
```cpp
g.imgui.draw_list->AddRectFilled(window_r, LiveCol(UiColMap::MidViewportDivider));
```

### 5. Margins to contents_padding and contents_gap

The legacy code uses `margins` on child items for spacing. The modern approach uses `contents_padding` on containers and `contents_gap` for spacing between siblings.

**Old:**
```cpp
auto switches_container = layout::CreateItem(lay, arena, {
    .parent = root,
    .size = {layout::k_fill_parent, layout::k_hug_contents},
    .margins = {.l = margin_l, .r = margin_r, .t = margin_t, .b = margin_b},
    .contents_direction = layout::Direction::Row,
});
```

**New:**
```cpp
auto const switches_container = DoBox(g.builder, {
    .parent = root,
    .layout {
        .size = {layout::k_fill_parent, layout::k_hug_contents},
        .contents_padding = {.l = LiveWw(...), .r = LiveWw(...), .t = LiveWw(...), .b = LiveWw(...)},
        .contents_direction = layout::Direction::Row,
    },
});
```

For vertical spacing between sections, use `DoWhitespace`:
```cpp
static void DoWhitespace(GuiBuilder& builder, Box parent, f32 height, u64 loc_hash = SourceLocationHash());
```

### 6. Dividers

**Old:**
```cpp
auto divider = layout::CreateItem(lay, arena, {
    .parent = root,
    .size = {layout::k_fill_parent, 1},
    .margins = {.t = LivePx(FXDividerMarginT), .b = fx_divider_margin_b},
});
// Later in draw phase:
imgui.draw_list->AddLine(line_r.TopLeft(), line_r.TopRight(), col);
```

**New (from layer refactor):**
```cpp
static void DoDivider(GuiState& g, Box parent, u64 loc_hash = SourceLocationHash()) {
    auto const divider = DoBox(g.builder, {
        .parent = parent,
        .layout { .size = {layout::k_fill_parent, 1} },
    }, loc_hash);
    if (auto const r = BoxRect(g.builder, divider)) {
        auto const window_r = g.imgui.ViewportRectToWindowRect(*r);
        g.imgui.draw_list->AddLine({window_r.x, window_r.Bottom()},
                                   {window_r.Right(), window_r.Bottom()},
                                   LiveCol(UiColMap::MidViewportDivider));
    }
}
```

Use `DoWhitespace` for the margins above/below dividers rather than margins on the divider itself. Create new `UiSizeId`s with clear names (e.g. `FXDividerGapAbove` rather than `FXDividerMarginT`).

### 7. Naming new UiSizeIds

Create new descriptive `UiSizeId`s rather than reusing legacy ones. Naming convention from the layer refactor:

| Legacy name | New name pattern |
|-------------|-----------------|
| `FXSwitchBoardMarginL` | `FXSwitchBoardPadL` (if it becomes contents_padding) |
| `FXDividerMarginT` | `FXDividerGapAbove` (if it becomes a whitespace gap) |
| `FXDividerMarginB` | `FXDividerGapBelow` |

General patterns:
- `*PadL`, `*PadR`, `*PadT`, `*PadB`, `*PadLR`, `*PadTB` for contents_padding
- `*Gap`, `*GapAbove`, `*GapBelow` for whitespace/contents_gap
- `*W`, `*H` for explicit widths/heights
- `*Pct` for percentage values

### 8. Colours: LiveCol vs LiveColStruct

- `LiveCol()` returns `u32` — for draw_list calls (`AddRectFilled`, `AddLine`, etc.)
- `LiveColStruct()` returns `ColourTint` — for `DoBox` config fields (`.text_colours`, `.background_fill_colours`)

### 9. ID management

**Old:** Manually created `imgui::Id` values with `imgui.MakeId("name")` and arithmetic offsets (`id - 4`, `id + 4`).

**New:** `DoBox` auto-generates unique IDs from source location hash + `id_extra` + parent ID. Use `id_extra` to disambiguate boxes in loops:
```cpp
for (auto const i : Range(k_num_effect_types)) {
    auto const switch_box = DoBox(g.builder, {
        .parent = switches_container,
        .id_extra = (u64)i,
        ...
    });
}
```

For helper functions called multiple times with the same parent, add a `u64 loc_hash = SourceLocationHash()` default parameter. See `gui_builder.hpp` header comment for details.

### 10. Button behaviour

**Old:** `buttons::Button(g, id, rect, text, style)` — separate function with manual rect.

**New:** `DoBox` with `.button_behaviour`:
```cpp
auto const btn = DoBox(g.builder, {
    .parent = parent,
    .text = text,
    .text_colours = ...,
    .layout { .size = ... },
    .tooltip = FunctionRef<String()> {[&]() -> String { return "..."_s; }},
    .button_behaviour = imgui::ButtonConfig {},
});
if (btn.button_fired) { /* handle click */ }
```

### 11. Tooltips

**Old:** Separate `Tooltip(g, id, rect, text, {})` call after the button.

**New:** Inline in `DoBox` config:
```cpp
.tooltip = FunctionRef<String()> {[&]() -> String { return "tooltip text"_s; }}
```

Or for formatted text:
```cpp
.tooltip = FunctionRef<String()> {[&]() -> String {
    return fmt::Format(g.scratch_arena, "Remove {}", name);
}}
```

### 12. Avoid aliases

**Old:**
```cpp
auto& imgui = g.imgui;
auto& lay = g.layout;
auto& engine = g.engine;
```

**New:** Use `g.imgui`, `g.builder`, `g.engine` directly. Reduces indirection and keeps the scope clear.

### 13. Viewport handling

The legacy code creates its own `imgui::BeginViewport`/`EndViewport`. The new code should use `DoBoxViewport` for scrollable regions:

```cpp
DoBoxViewport(g.builder, {
    .run = [&](GuiBuilder&) { /* build your boxes here */ },
    .bounds = ...,
    .imgui_id = ...,
    .viewport_config = { .draw_scrollbars = DrawMidPanelScrollbars, ... },
});
```

However, the effects panel is currently called via `DoEffectsViewport(g, frame_context, *r)` from `gui_mid_panel.cpp`. The entry point will need updating to pass a `Box` instead of a `Rect`, similar to how `DoLayerPanel` receives a `Box parent`.

## Effects-specific complexity

### Drag and drop

The effects panel has two drag-and-drop systems:
1. **Effect unit drag** (`dragging_fx_unit`): Dragging effect section headings to reorder active effects.
2. **Switchboard drag** (`dragging_fx_switch`): Dragging effect toggle buttons to reorder in the switchboard.

Both involve:
- Detecting drag start (click + movement past threshold)
- Finding the closest drop slot based on cursor position
- Drawing a floating "fake button" following the cursor
- Auto-scrolling when near viewport edges
- Applying the new order on mouse release

This logic will need to be preserved but adapted to work with `DoBox` and `BoxRect` instead of `layout::GetRect`. The `BoxRect` function returns `Optional<Rect>` (viewport-relative), so you will need `g.imgui.ViewportRectToWindowRect()` to convert for cursor comparisons and drawing.

### Switchboard

The switchboard is a two-column grid of effect enable/disable toggle buttons with:
- A number label (1-10) on the left
- The effect name as a toggle button
- A grab region on the right for drag handles
- Drop zone highlighting during drag

### Effect-specific parameter layouts

Each effect type has a different set of parameters. Some effects have special elements:
- **Compressor**: Auto-gain toggle button in the heading area
- **Delay**: Sync toggle button in the heading area; conditional display of synced vs. ms parameters
- **ConvolutionReverb**: IR selector with prev/next/shuffle buttons (already partially modernised as `DoImpulseResponseMenu`)
- **Reverb/Phaser**: Dynamic parameter sets via `ComptimeParamSearch`, with grouping support for joining lines

### Knob joining lines

Paired parameters (wet/dry, filter cutoff/spread, delay left/right) are visually connected with horizontal lines between knobs. This is custom rendering that uses `BoxRect` + draw_list after the knobs are created:

```cpp
// After creating both knobs, get their rects and draw a connecting line
if (auto const r1 = BoxRect(g.builder, knob1_box); auto const r2 = BoxRect(g.builder, knob2_box)) {
    auto const wr1 = g.imgui.ViewportRectToWindowRect(*r1);
    auto const wr2 = g.imgui.ViewportRectToWindowRect(*r2);
    auto const y = wr1.CentreY();
    g.imgui.draw_list->AddLine(
        {wr1.Right() + LivePx(UiSizeId::FXKnobJoiningLinePadLR), y},
        {wr2.x - LivePx(UiSizeId::FXKnobJoiningLinePadLR), y},
        LiveCol(UiColMap::FXKnobJoiningLine),
        LivePx(UiSizeId::FXKnobJoiningLineThickness));
}
```

### Effect colours

Each effect type has Back, Button, and Highlight colours defined in `gui_colour_map.def`. In the legacy code these are fetched via `GetFxCols()` and passed to widget styles. In the new code, the highlight colour can be passed to `DoKnobParameter` via `.knob_highlight_col`:

```cpp
DoKnobParameter(g, parent, param, {
    .width = LiveWw(...),
    .knob_highlight_col = LiveColStruct(UiColMap::DistortionHighlight),
});
```

## Legacy section map

| Section | Lines | Status |
|---------|-------|--------|
| `DoIrSelectorRightClickMenu` (already modernised) | 117-163 | Keep/adapt |
| `DoImpulseResponseMenu` (uses old buttons/layout API) | 165-241 | TODO |
| `GetFxCols` helper | 249-274 | Keep/adapt |
| `DoEffectsViewport` entry point | 276-1303 | TODO |
| — Switchboard layout | 307-374 | TODO |
| — Switchboard bottom divider | 376-382 | TODO |
| — Effect section layouts (per-type) | 383-781 | TODO |
| — `layout::RunContext` and draw phase begins | 786 | TODO |
| — Drag-and-drop closest divider | 788-809 | TODO |
| — Divider + joining line drawing | 811-833 | TODO |
| — Effect section drawing (headings + params) | 855-1165 | TODO |
| — Dragging effect unit (floating button) | 1167-1200 | TODO |
| — Switchboard slot management | 1210-1295 | TODO |
| — Effects order persistence | 1297-1302 | TODO |

## Conversion strategy

### Phase 1: New file and entry point

1. Create `gui_effects_new.cpp` with a function like:
   ```cpp
   void DoEffectsPanel(GuiState& g, GuiFrameContext const& frame_context, Box parent);
   ```
2. Update `gui_mid_panel.cpp` to call the new function instead of `DoEffectsViewport`. The effects content area already has a `Box` (`effects_box` at line 345 in `gui_mid_panel.cpp`).
3. The new function creates a `DoBoxViewport` for scrollability, then builds the switchboard and effect sections inside it.

### Phase 2: Switchboard

Convert the two-column grid of effect toggle buttons. Each button needs:
- Number label (`DoBox` with text)
- Toggle button (`DoButtonParameter` or custom `DoBox` with `.button_behaviour`)
- Grab region for drag handle
- Drop zone highlighting

### Phase 3: Effect sections

Convert each effect type one at a time. For each:
1. Create heading row with effect name button and close button
2. Create parameter container with the appropriate parameter elements
3. Add knob joining lines where needed
4. Add the divider after each section

### Phase 4: Drag and drop

Port the drag-and-drop logic for both:
- Effect unit reordering (dragging headings)
- Switchboard reordering (dragging toggle buttons)

### Phase 5: Cleanup

1. Remove `gui_effects.cpp` and its header
2. Remove old includes and `EffectIDs` struct
3. Remove from `build.zig` if needed
4. Verify visual match with the legacy code

## How to add each section

1. Read the legacy layout section to understand sizes, margins, and direction.
2. Read the legacy draw section for rendering and interaction logic.
3. Convert `margins` to `contents_padding` on parent containers.
4. Convert `LivePx` size values to `LiveWw` for `DoBox` configs.
5. Replace `buttons::Button(...)` with `DoBox(g.builder, { .button_behaviour = ... })`.
6. Replace `KnobAndLabel(...)` with `DoKnobParameter(g, parent, param, {...})`.
7. Replace `buttons::PopupWithItems(...)` + `labels::Label(...)` with `DoMenuParameter(g, parent, param, {...})`.
8. Replace `buttons::Toggle(...)` with `DoButtonParameter(g, parent, param, {...})`.
9. Replace `draggers::Dragger(...)` with `DoIntParameter(g, parent, param, {...})`.
10. Replace manual draw calls with `BoxRect` + draw_list for custom rendering.
11. Run `nix develop --command zb` to verify compilation.
12. Visually verify the effects panel matches the legacy version.
