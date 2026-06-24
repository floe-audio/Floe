# Overview

> A tour of Floe's window and how sound flows through it

## What is Floe?

Floe is an audio plugin for your digital audio workstation (DAW). It's a platform for performing, finding, and transforming sounds from sample libraries — closer to a sample-based synth or ROMpler than a traditional sampler. It hosts sample-based [_instruments_](/docs/beta/reference/instruments) (sampled pianos, synths, textures, and so on) and exposes an extensive set of parameters for venturing into sample-based synthesis. A comprehensive presets system with powerful browsers, macros, a random variation generator, and other features make it a formidable engine for sample libraries.

Floe is designed to be efficient with your CPU and can be added multiple times in a single project; multiple instances will share resources such as sample libraries to keep memory usage low.

Floe is not designed for ad-hoc sampling — you can't currently import your own samples.

### New in version 2

The recent version 2 update of Floe offers a substantial set of new features and improvements. Read about the update in the [blog post](/blog/floe-2-0-0-beta-2).

## The UI

![Floe's main window with its four regions highlighted](/images/screenshots/overview.png)

1

2

3

4

5

1.  **Top panel** — global controls: preset name and browser, save, undo/redo, the main menu, and master volume.
2.  **Main content** — one of 3 tabs. [_Perform_](/docs/beta/usage/perform) shows the distraction-free basic view, [_Layers_](/docs/beta/usage/layers) exposes the per-layer parameters for each of the three layers, and [_Effects_](/docs/beta/usage/effects) is the reorderable effects rack.
3.  **Bottom panel** — the keyboard, plus context-sensitive controls (key ranges, macros, etc.) depending on the active tab.
4.  **Main tabs** — switches the main content between _Perform_, _Layers_, and _Effects_.
5.  **Resize corner** — drag to scale the window (fixed aspect ratio). The _window size_ buttons in the preferences panel do the same. In some DAWs such as Logic Pro, you must grab this exact point of Floe, not the corner of DAW window wrapping it.

## Signal flow

Notes are sent in parallel to three identical layers — each hosting its own instrument with per-layer envelope, tuning, filter and LFO. The layers are combined and fed through a shared, reorderable effects rack, then a master volume.

## Tips

-   Knobs and sliders respond to click-and-drag (up/down or left/right). Hold `shift` for fine adjustments, `ctrl`/`cmd`+click to reset to default, double-click to type a value, and right-click for more options.
-   Hovering over most elements displays a tooltip with a brief description. Tooltips can be disabled in the preferences panel.
