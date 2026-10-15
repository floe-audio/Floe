# Key Ranges

> How to use key ranges to split the keyboard into zones for different layers

Key ranges let you split the keyboard into zones, with each layer responding to specific keys. This enables keyboard splits, layered zones, and smooth transitions between different sounds across the keyboard.

## Setting up key ranges

Configure key ranges on each [layer's **Config tab**](/docs/beta/usage/layers#config-tab) using the low and high key controls. Click and drag these handles to set the range that layer will respond to.

![Key range controls](/images/screenshots/key-range-controls.png)

**Fade controls** let you create smooth transitions at the edges of key ranges. Set the number of notes that fade in at the start or fade out at the end of each range. This is used for creating seamless overlaps between layers.

For multisampled instruments in particular, consider using the **Transpose** parameter to change the pitch of the layer. This works by shifting the underlying mapping of samples so higher/lower samples are triggered as opposed to the standard pitch parameter which algorithmically re-pitches the sound.

## Key range display

A bar above the keyboard at the bottom of the interface shows which keys trigger each layer, with colours corresponding to each layer. Only layers with loaded instruments appear; the bar is hidden entirely when all layers use the full keyboard range (C-2 to G8).

![Key range display above the keyboard](/images/screenshots/key-range-bars.png)

Since 128 MIDI keys can't fit on screen, the keyboard shows a scrollable section — use the octave scroll buttons or drag the octave number to navigate. The display scrolls with the keyboard. Small chevrons indicate when a range extends beyond the visible keyboard area, and vertical lines mark the boundaries of each range. Gradients on the bars show fade-in and fade-out regions, with more transparency indicating lower volume.

Hover over the display to see an enlarged view with more detail. Sample library developers can define named ranges (like drum names or natural vs. extended ranges) which appear as thicker labelled capsules.

![Enlarged key range view with named ranges, fades, and off-screen chevrons](/images/screenshots/key-range-enlarged.png)
