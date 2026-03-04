<!--
SPDX-FileCopyrightText: 2026 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

# Plan: Replace Granular Knobs with Horizontal Sliders

## Problem
The granular controls in the Engine page (`DoEnginePage` in `gui_layer_subtabbed.cpp:1070-1103`) use 6 tiny knobs (`width = 20`) in a multiline wrapping row. They're too cluttered and leave no room for additional granular parameters.

## Solution
Replace the granular knobs with a column of horizontal sliders. Each slider has its label on the left and the slider track on the right. The slider supports a dual-value display (analogous to the knob's "outer arc") so it can show both the direct value and the macro-adjusted value simultaneously.

## Design

### Visual design of a single horizontal slider
```
  Label   [====|===========----------]
           ^    ^           ^
           |    |           |
           |    |           Empty track
           |    Outer fill (macro-adjusted value, dimmer)
           Inner fill (direct value, bright highlight colour)
```

- **Track**: full-width rounded rectangle background
- **Inner fill**: from left edge (or centre if bidirectional) to the parameter value, using the highlight colour
- **Outer fill**: extends from the inner fill to the macro-adjusted value position, using a dimmer colour - this mirrors the knob's outer arc concept
- **Handle cursor**: a small vertical line at the current value position

### Layout in the Engine page
```
┌──────────────────────────────┐
│ Speed     [========---------]│
│ Spread    [===--------------]│
│ Grains    [==========-------]│
│ Length    [============-----]│
│ Smooth    [==========-------]│
│ Rnd Pan   [====-------------]│
└──────────────────────────────┘
```

Single column of rows, each row containing a right-aligned label and a horizontal slider.

## Implementation Steps

### Step 1: Add slider colour entries to `gui_colour_map.def`

Add after the `KnobTextInputBorder` line (~line 19):

```cpp
X("Controls", SliderMidTrack, White, 30, true)
X("Controls", SliderMidTrackHover, White, 45, true)
X("Controls", SliderMidFilled, Highlight200, 255, true)
X("Controls", SliderMidFilledGreyedOut, Highlight200, 105, true)
X("Controls", SliderMidOuterFilled, White, 80, true)
X("Controls", SliderMidHandle, White, 200, true)
X("Controls", SliderMidHandleHover, White, 255, true)
X("Controls", SliderMidHandleGreyedOut, White, 80, true)
```

These follow the same naming pattern as `KnobMid*` colours. All are mid-panel style (white-based, semi-transparent, dark_mode=true) since the granular controls live in the mid panel.

### Step 2: Add `DrawHorizontalSlider` to `gui_element_drawing.hpp/.cpp`

**Header** (`gui_element_drawing.hpp`): Add after `DrawKnob` declaration:

```cpp
struct DrawHorizontalSliderOptions {
    u32 highlight_col;
    Optional<f32> outer_percent; // Secondary value (e.g. macro-adjusted), displayed as a dimmer fill.
    bool greyed_out;
    bool is_fake;
    bool bidirectional;
};

void DrawHorizontalSlider(imgui::Context& imgui,
                          imgui::Id id,
                          Rect r,
                          f32 percent,
                          DrawHorizontalSliderOptions const& options);
```

**Implementation** (`gui_element_drawing.cpp`): The drawing logic:

1. **Track background**: `AddRectFilled` the full rect with `SliderMidTrack` (or `SliderMidTrackHover` when hot/active)
2. **Outer fill** (if `outer_percent` is set and differs from `percent`):
   - If not bidirectional: fill from `x` to `x + outer_percent * w` with `SliderMidOuterFilled`
   - If bidirectional: fill from centre to outer_percent position with `SliderMidOuterFilled`
3. **Inner fill**:
   - If not bidirectional: fill from `x` to `x + percent * w` with `SliderMidFilled` (or `SliderMidFilledGreyedOut`)
   - If bidirectional: fill from centre to percent position with `SliderMidFilled`
4. **Handle cursor**: a vertical line at the percent position using `SliderMidHandle`/`SliderMidHandleHover`/`SliderMidHandleGreyedOut`

All rects should use `k_corner_rounding` for rounded corners. The handle should be a thin vertical line (~2px wide) spanning the track height.

### Step 3: Add `use_horizontal_slider` option to `ParameterComponentOptions`

In `gui_param_elements.hpp`, add to `ParameterComponentOptions`:

```cpp
bool use_horizontal_slider = false;
```

### Step 4: Modify `DoKnobParameter` to support horizontal slider mode

In `gui_param_elements.cpp`, the `DoKnobParameter` function (~line 348-527) needs a branch. When `options.use_horizontal_slider` is true:

- The container layout changes from a vertical column (knob above, label below) to a horizontal row (label left, slider right)
- Instead of creating a knob-sized box and calling `DrawKnob`, create a slider-sized box and call `DrawHorizontalSlider`
- The dragger behaviour remains the same (it already works with any rect)
- The text input overlay, tooltip, context menu, and macro overlay all work the same
- The label is placed to the left instead of below, using `TextJustification::CentredRight` and a fixed width

Key changes within `DoKnobParameter` when `use_horizontal_slider` is true:

1. **Container**: change from column to row direction, set `contents_cross_axis_align = layout::CrossAxisAlign::Middle`
2. **Label**: render *before* the slider (left side), with right-justified text and a fixed width (~45-50 ww units)
3. **Slider box**: use `{layout::k_fill_parent, slider_height}` where `slider_height` is ~8 ww units
4. **Drawing**: call `DrawHorizontalSlider` instead of `DrawKnob`, passing the same percent and outer_arc_percent values computed via `AdjustedLinearValue()`
5. **Skip** the peak meter code path (not applicable to sliders)
6. **Skip** the separate label at the bottom (already rendered on the left)

The dragger/slider behaviour from imgui (`DraggerBehaviour`) already works with any rectangle shape, so the interaction code doesn't need changes.

### Step 5: Update granular controls in `DoEnginePage`

In `gui_layer_subtabbed.cpp`, replace the granular controls section (~lines 1070-1103):

**Before:**
```cpp
if (IsGranular(play_mode)) {
    auto const knobs_row = DoBox(g.builder, { ... multiline row ... });

    auto const do_knob = [&](LayerParamIndex param) {
        DoKnobParameter(g, knobs_row, params.DescribedValue(layer_index, param),
                        { .width = 20, .style_system = GuiStyleSystem::MidPanel });
    };

    do_knob(speed_or_position);
    do_knob(GranularSpread);
    do_knob(GranularGrains);
    do_knob(GranularLength);
    do_knob(GranularSmoothing);
    do_knob(GranularRandomPan);
}
```

**After:**
```cpp
if (IsGranular(play_mode)) {
    auto const sliders_col = DoBox(g.builder,
                                   {
                                       .parent = page,
                                       .layout {
                                           .size = {layout::k_fill_parent, layout::k_hug_contents},
                                           .contents_gap = 4,
                                           .contents_direction = layout::Direction::Column,
                                           .contents_align = layout::Alignment::Start,
                                       },
                                   });

    auto const do_slider = [&](LayerParamIndex param) {
        DoKnobParameter(g,
                        sliders_col,
                        params.DescribedValue(layer_index, param),
                        {
                            .width = layout::k_fill_parent,
                            .style_system = GuiStyleSystem::MidPanel,
                            .use_horizontal_slider = true,
                        });
    };

    do_slider(speed_or_position);
    do_slider(GranularSpread);
    do_slider(GranularGrains);
    do_slider(GranularLength);
    do_slider(GranularSmoothing);
    do_slider(GranularRandomPan);
}
```

## Files Modified

| File | Change |
|------|--------|
| `src/plugin/gui/live_edit_defs/gui_colour_map.def` | Add 8 slider colour entries |
| `src/plugin/gui/elements/gui_element_drawing.hpp` | Add `DrawHorizontalSliderOptions` struct + `DrawHorizontalSlider` declaration |
| `src/plugin/gui/elements/gui_element_drawing.cpp` | Implement `DrawHorizontalSlider` (~40-60 lines) |
| `src/plugin/gui/elements/gui_param_elements.hpp` | Add `use_horizontal_slider` bool to `ParameterComponentOptions` |
| `src/plugin/gui/elements/gui_param_elements.cpp` | Add horizontal slider branch in `DoKnobParameter` |
| `src/plugin/gui/panels/gui_layer_subtabbed.cpp` | Replace granular knobs section with slider layout |

## Notes

- `DrawHorizontalSlider` only needs MidPanel colours for now since granular controls are only in the mid panel. If we need TopBottomPanels or Overlay slider styles later, we can add more colour entries then.
- The horizontal slider reuses the existing `DraggerBehaviour` for mouse interaction - no new imgui behaviour code is needed.
- The `outer_percent` / outer fill concept directly mirrors the knob's `outer_arc_percent` which shows the macro-adjusted value. This is computed identically via `AdjustedLinearValue()`.
- The `width` field of `ParameterComponentOptions` takes on a different meaning in slider mode: it's the width of the entire row (label + slider), not the knob diameter. Setting it to `layout::k_fill_parent` is the expected usage.
- The `knob_height_fraction` field is ignored in slider mode.
- Bidirectional mode (fill from centre) is not needed for any current granular param but is supported for future use.
