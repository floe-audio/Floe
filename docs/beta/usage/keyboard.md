# Keyboard

> The on-screen keyboard in Floe's bottom panel, and what it shows about the notes and voices playing

The bottom panel of Floe's window is home to the on-screen keyboard. It's there on every page, and it's a quick way to see which notes are coming in, how many voices each one is playing, and which keys play which layer. You can click it to play notes too.

![The bottom panel on the Layers page, with a chord held on the keyboard](https://assets.floe.audio/website/screenshots/keyboard.65449d5b30f4e02c.png)12345

1.  **Tabs** — _PLAY_ shows the keyboard and macros. _MACROS_ shows the panel where you [set up the macros](/docs/beta/usage/macros#making-your-own-macros).
2.  **Macros** — the four macro knobs, the same ones as on the _Perform_ page. A greyed-out macro has nothing linked to it. See [Macros](/docs/beta/usage/macros).
3.  **Key range bars** — which keys play each layer, shown when any layer is limited to part of the keyboard.
4.  **Keys** — held notes light up, and red marks along the top show the voices sounding on each note.
5.  **Octave** — scroll the keyboard up or down to see a different part of the note range.

On the _Perform_ page, the bottom panel is simpler: there are no tabs, macros or octave controls, and the keyboard stretches across the whole panel to show nine octaves at once.

## Playing notes

Click a key to play it. Where you click sets the velocity: near the top of the key for the softest note, lower down for harder. A clicked note plays just like a note from your MIDI keyboard, so the layers' key ranges, the arpeggiator and everything else apply as usual.

Throughout the UI we label middle C (MIDI note 60) as C3. But some DAWs call that note C4.

## What the keyboard shows

The keys light up when you play, whether that's from your MIDI keyboard, your DAW, or a click, on any MIDI channel.

The red marks along the top of a key show the voices currently sounding on that note. A note can have more than one voice, such as one for each layer, and the mark gets stronger the more voices there are. Marks often linger after you let go, because each voice plays on through the release of its [volume envelope](/docs/beta/usage/main-tab#volume-envelope).

If you've set a _Reset keyswitch_ in [Performance Controls](/docs/beta/usage/performance-controls), that key has a blue marker along its bottom edge. See [Reproducibility](/docs/beta/usage/reproducibility).

## Key range bars

When a layer is limited to part of the keyboard, the bars above the keys (3) show which keys trigger each layer, with a separate colour for each layer. Hover over them for an enlarged view that names each layer's Instrument. See [Key range display](/docs/beta/usage/config-tab#key-range-display) for everything the bars show, and [Key ranges](/docs/beta/usage/config-tab#key-ranges) for how to set ranges up.
