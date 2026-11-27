# Config Tab

> How a layer responds to your playing, including key ranges, pitch bend, velocity and MPE

The _CONFIG_ tab sets how the layer responds to your playing: which keys it uses, and how it follows velocity, pitch bend and MPE controllers.

![A layer's CONFIG tab](https://assets.floe.audio/website/screenshots/layer-config.499578f75666d12d.png)1234567

1.  **Transpose** — shift the layer in semitones by changing which samples are triggered, rather than re-pitching the audio as _Pitch_ in the [layer header](/docs/beta/usage/layers#layer-header) does. On a multi-sampled Instrument this keeps the character of the sound intact, so it's the better choice for large shifts. You still play the same keys; they just reach for different samples.
2.  **Pitch Bend Range** — how far the pitch wheel bends this layer, in semitones. Each layer has its own, so one layer can bend a whole tone while another ignores the wheel entirely.
3.  **Keytrack** — with it on, higher keys play higher pitches. Switch it off and every key plays the sample at its recorded pitch, which suits drum hits, loops and sound effects. Some libraries mark particular samples as always or never keytracked.
4.  **Monophonic** — what happens when notes overlap. _Off_ is polyphonic: every note gets its own voice. _Retrigger_ plays one note at a time, with each new note releasing the previous one. _Latch_ holds on to the first note, ignoring any new ones, until you let go of every key: useful for drones and textures. Because it's per layer, one layer can play one note at a time while another stays polyphonic. While the layer's [arpeggiator](/docs/beta/usage/arp-tab) is on, this setting has no effect.
5.  **MPE** — choose what an MPE controller's press (channel pressure) and slide (CC74) control on this layer, such as volume, filter cutoff, timbre or LFO amount, and by how much. Negative amounts reverse the response. MPE must be switched on in the Performance Controls panel first. See [MIDI](/docs/beta/usage/midi#mpe) for details.
6.  **Key Range** and **Key Fade** — give the layer its own portion of the keyboard, with soft edges if you like.
7.  **Velocity to volume curve** — how hard you play turns into how loud each note sounds. Steepen the curve for a wider dynamic range, or flatten it to even out your playing. When you play, a red line shows where your note landed.

## Key ranges

Key ranges let you split the keyboard into zones, with each layer responding to specific keys. Use them to split the keyboard, stack zones on top of each other, or crossfade smoothly from one sound to another as you play up the keys.

### Setting up key ranges

_Key Range_ is the lowest and highest note the layer plays, and _Key Fade_ is how many semitones it takes to fade in at the bottom of the range, and to fade out at the top. Drag a value up or down, click its arrows ( ), or double-click to type it in (a note name, for the range).

![Key range controls](https://assets.floe.audio/website/screenshots/key-range-controls.8f042068dc69b686.png)

Fades are what make smooth transitions possible: overlap two layers' ranges by a few notes, fade the lower layer out and the upper layer in across the overlap, and one sound crossfades into the other as you play up the keyboard.

To play a different part of a multi-sampled Instrument's mapping, use _Transpose_ on the same tab. It shifts which samples are triggered, so higher or lower samples play, while the layer's _Pitch_ re-pitches the audio by speeding it up or slowing it down. _Transpose_ doesn't move the key range itself.

### Key range display

A bar above the keyboard at the bottom of the interface shows which keys trigger each layer, with colours corresponding to each layer. Only layers with an Instrument loaded appear. The bar is hidden when every layer uses the full keyboard range (C-2 to G8), unless an Instrument has named ranges (see below).

![Key range display above the keyboard](https://assets.floe.audio/website/screenshots/key-range-bars.bccf7ecc8a586a01.png)

On the _Layers_ and _Effects_ pages, the keyboard shows a section of the full 128 keys: use the [octave controls](/docs/beta/usage/keyboard) to scroll it, and the bar scrolls with it. The _Perform_ page shows a wider, fixed stretch of nine octaves. Vertical lines mark where each range starts and ends, and small chevrons show when a range carries on past the visible keys. Fades show as gradients on the bars: the more transparent, the quieter.

Hover over the display to see an enlarged view with more detail. An Instrument can also have named ranges, such as the name of the sound on each key, or each group of drums in a kit. They appear as thicker capsules, labelled in the enlarged view.

![Enlarged key range view with named ranges, fades, and off-screen chevrons](https://assets.floe.audio/website/screenshots/key-range-enlarged.631825e6f15b2f25.png)

## Velocity to volume curve

Each layer has its own _velocity to volume curve_, which shapes how hard you play (MIDI velocity) into the volume of each note.

![Velocity to volume curve editor](https://assets.floe.audio/website/screenshots/velocity-curve.7a4bb3dd5dc53a7e.png)

### How the curve works

The horizontal axis is velocity — far left is the softest possible velocity (MIDI value 1), far right is the hardest (MIDI value 127). The vertical axis is the resulting note volume. When you play a note, a red indicator appears at its velocity position on the curve so you can see where you're playing.

While notes are sounding, each active voice also gets a red line on the layer's volume slider, showing the level that voice is playing at, once the curve, any [key range](#key-ranges) fade and any MPE volume expression are taken into account.

A straight line from bottom-left to top-right gives a natural response: the harder you play, the louder the note.

Move, add or remove points, or reshape the curve between them, to change this relationship: make soft playing louder than it would be naturally, cap the volume of hard hits, or create a non-linear response that emphasises a particular dynamic range.

The curve pictured above is one example. The softest notes stay quiet, the volume climbs steeply as you play a little harder, then it levels off so the hardest hits can't get much louder.

Setting different curves across the three layers also opens up creative sound design — for example, fading one layer in as you play harder while another fades out.

By default the curve doesn't start at zero, so even the softest notes still make a quiet sound. Drag the left-most point down to the bottom if you want the softest notes to be silent.

### Velocity and multi-sampled Instruments

While not directly related to this curve, it's worth noting that velocity does more than just control volume. For multi-sampled Instruments that contain separate velocity layers, a note's velocity also selects _which_ sample is played — not just how loud it is. A low-velocity note on a piano, for instance, plays the soft-velocity samples (with their distinct tone and character) rather than a quieter version of the hard-hit samples.

### Editing the curve

-   **Double-click** anywhere on the curve to add a control point (up to 8 in total)
-   **Drag control points** to move them
-   **Drag between points** to adjust curve shape
-   **Double-click a point** to remove it
-   **Shift-drag** for finer control
-   **`ctrl`/`cmd`\-click** a point to reset its height, or between points to reset that stretch's shape
-   **Right-click** to add or remove points, copy and paste the curve between layers, or reset it
