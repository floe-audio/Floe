# Randomisation

> Use an element of chance to find sounds you might not have found yourself

Floe has a few features that bring in an element of chance. It's a way to find spontaneous inspiration, and sounds you might not have reached for yourself. They're also good fun.

As with anything left to chance, sometimes the results are great and sometimes they're a bit too wacky. The _Undo_ button in the top panel is always available.

All of these randomisation buttons use the same shuffle icon, so you can identify them at a glance.

## Random variation

The main random feature is the _Random Variation_ strip on the [_Perform_](/docs/usage/perform) page. It takes the preset you've loaded and changes some of its properties, giving you a fresh take on the sound.

![The Random Variation strip on the Perform page](https://assets.floe.audio/website/screenshots/perform-variation-strip.68e36d0f47412a76.png)

Click anywhere on the strip. Where you click sets how far the variation strays from the preset. Towards the left, the changes are mostly subtle and the sound keeps its character. Move towards the right to venture into wilder changes. As you hover, the strip shows the amount and a word for it, from _small variation_ to _extreme variation_.

The button at the left of the strip rolls another variation at the same amount as last time, so you can try several takes at an intensity you like.

Every variation starts from the preset as you loaded it, not from the previous variation, so you can keep clicking without drifting ever further away. When you find one you like, save it as a preset. That keeps it, and makes it the new starting point for further variations.

If nothing is loaded, the strip builds a random sound instead, around an Instrument picked at random.

### What a variation changes

A variation reworks the preset rather than starting from scratch, so it usually keeps something of the original's feel. Depending on how far right you click, it may:

-   **Swap Instruments** — each layer can get a different Instrument. Further left, the new one is usually a close relative, from the same library or folder or with similar tags. Further right, it can come from anywhere. With more than one layer in use, one is sometimes left as it is. Layers using a built-in waveform are never changed.
-   **Fill or empty layers** — an empty layer may get an Instrument, and occasionally a layer is cleared. Filling is much more likely than emptying, so wilder variations tend to add to the sound rather than thin it out. At least one layer always keeps an Instrument.
-   **Move the macros** — some of the preset's [macros](/docs/usage/macros) are turned by a noticeable amount, taking everything they control with them.
-   **Change the effects** — some effects' mix levels move, and a few effects are switched on or off. An effect that's switched on gets fresh settings, kept within sensible limits so it won't suddenly jump in level.
-   **Shift octaves** — now and then, a layer's _Transpose_ moves up or down an octave or two.

## Jumping to a random item in a browser

Wherever you can choose a preset, an Instrument or an impulse response, there's a button that jumps to a random one:

-   **Presets** — in the top panel, next to the preset name.
-   **Instruments** — in each layer's header on the [_Layers_](/docs/usage/layers#layer-header) page. It loads a random Instrument into that layer only.
-   **Impulse responses** — on the [Convolution Reverb](/docs/usage/effects#convolution-reverb) effect.

Each button is linked to its [browser](/docs/usage/browsers): it only picks from what that browser's filters allow, so you can set the bounds of what "random" means. Filter the preset browser to one library and a tag such as _ambient_, say, then keep clicking to jump around within that selection. The same goes for the browser's own button while it's open.

## Layers and effects

The button at the top right of the [_Layers_](/docs/usage/layers) page loads a random Instrument into all three layers at once, as if you'd clicked each layer's button in turn. Each layer has its own Instrument browser filters, so narrow the first layer to bass sounds and the second to textures, for example, then click it to roll through combinations that stay within those bounds.

The button at the top right of the [_Effects_](/docs/usage/effects) page randomly puts each effect in or takes it out of the rack, and shuffles their order. It leaves each effect's settings alone, so anything you've dialled in is still there when the effect comes back.

## Randomness in the sound itself

Parts of the audio engine, such as the [granular engine](/docs/usage/playback-tab#granular-synthesis), also use randomness while you play, to create varied, natural-sounding performances. See [Reproducibility](/docs/usage/reproducibility).
