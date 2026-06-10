# Randomisation

> How to use Floe's randomisation features to explore new sounds

## Random Variation (PERFORM page)

![The Random Variation strip on the PERFORM page](/images/screenshots/perform-variation-strip.png)

The PERFORM page hosts the **Random Variation** strip. Click anywhere on the strip to generate a new variation of the current sound. Where you click matters:

-   **Further left** stays close to the current preset — small, related tweaks.
-   **Further right** strays much further — wilder instrument swaps, more effects toggled, bigger parameter moves.

Hovering the strip shows a percentage and a short description: _small_, _medium_, _large_, _huge_, or _extreme variation_. The shuffle icon at the left of the strip repeats the most recent amount, so you can keep rolling fresh variations at the same intensity without having to re-click the strip.

A Random Variation is built from the last loaded presets — its instruments, parameter values, effects, and macros — rather than from scratch. Concretely, it may:

-   **Swap instruments**: each layer's instrument is replaced with another one. Closer matches (same library or folder, overlapping tags) are favoured; the strip position controls how far the picker is allowed to roam.
-   **Add or remove layers**: blank layers may be filled in; populated layers may occasionally be cleared. Filling in is much more likely than emptying out, so wilder variations tend to add detail rather than thin the sound.
-   **Nudge macros**: a subset of your active [macros](/docs/beta/usage/macros) is shifted by a moderate-to-large amount, picking up the rest of the patch with them.
-   **Toggle effects**: a small number of effects are flipped on or off — newly enabled effects get a fresh set of randomised parameters (within safe ranges, so you won't get a sudden +30 dB gain blast).
-   **Shift octaves**: occasionally a layer is transposed up or down an octave or two for flavour.

Every variation creates a single undo step, so you can roll variations rapidly and step back to the previous state with the undo button in the top bar.

## All-layers instrument shuffle (LAYERS page)

The LAYERS page has a **shuffle button** in the top-right that loads a random instrument into every layer in one go. It's equivalent to clicking each layer's individual shuffle button (see below) in turn. Each layer's selection honours that layer's own browser filters, so if one layer is filtered to a specific library and another to a specific tag, the shuffle respects both at once.

## Per-layer instrument shuffle (LAYERS page)

Each layer has its own shuffle button in the layer top controls. It loads a random instrument into that layer only, based on the currently selected filters in that layer's [instrument browser](/docs/beta/usage/browsers). For example, filter a layer to the _Music Box Suite_ library and the layer shuffle will only pick instruments from there. The left/right arrows next to the shuffle button step through the same filtered list one item at a time.

## Effects shuffle (EFFECTS page)

The EFFECTS page has a shuffle button in the top-right of the tab bar. It **randomly turns each effect on or off** and **shuffles their order in the rack**. Parameter values inside each effect are left alone, so anything you've dialled in is preserved when an effect is re-enabled later.

If you do want more dramatic effect changes — including new internal parameter values — use the [Random Variation](#random-variation-perform-page) strip on the PERFORM page; it'll touch effects as part of the broader patch variation.

## Impulse response shuffle

The Convolution Reverb effect's impulse response selector has a shuffle button alongside its menu. Click it to load a random impulse response, honouring the currently selected filters in the [IR browser](/docs/beta/usage/browsers).

## Preset browser shuffle

The Presets browser, like every browser, has its own shuffle button to load a random preset from the currently filtered results. The same controls are duplicated above the browser on the main GUI so you can shuffle without opening the browser. Use the filters in the browser to narrow down what _random_ means — for example, only ambient presets from a chosen library.

## Tips

-   Randomisers _add_ to your workflow; they don't replace careful editing. Roll a variation, pick something you like, then dial in the details by hand.
-   Use browser filters to keep the browser-based shuffles within a chosen library, folder, or tag.
-   Use the undo button in the top bar to step back through variations — every roll is a single undoable change.
