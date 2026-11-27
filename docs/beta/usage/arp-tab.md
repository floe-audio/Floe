# Arp Tab

> Turn held notes into rhythmic patterns with each layer's own arpeggiator

The arpeggiator turns the notes you hold down into a tempo-synced rhythmic pattern. You choose how fast the steps go, the order the notes of your chord are played in, and the velocity, length, and pitch of each step.

Every [layer](/docs/beta/usage/layers) has its own arpeggiator, so you can build sounds with several rhythmic parts playing off each other: fast, high hits over slow, booming drums, say, or a melody over a pulsing bass. With a couple of arpeggiated layers, a single preset can drive a whole track.

Switch the arpeggiator on and hold a chord: you'll hear its notes play one after another in a rhythm. Each step triggers the layer's Instrument just as if you'd played the note yourself; each step runs through all the layer's usual settings, such as its play mode, volume envelope, filter and LFO.

The arpeggiator lives on each layer's _ARP_ tab. A dot on the tab shows that it's on.

![A layer's ARP tab with its parts numbered](https://assets.floe.audio/website/screenshots/layer-arp.3e2ced3e273a2a2e.png)12345

1.  **On/off and mode** — the _Arpeggiator_ switch, and a menu choosing where the steps get their notes from: _Played Notes_ or _Fixed Notes_.
2.  **Velocity bars** — one bar for each step, setting its velocity. Click and drag to change them, or drag sideways across the bars to draw a whole pattern in one go.
3.  **Step settings** — under each bar, from top to bottom, are the step number (click it to turn the step on or off), its pitch, a tie to the previous step, and its gate.
4.  **Sequence** — the step _Rate_, the pattern's _Length_, the note _Order_ and _Humanise_.
5.  **Config** — _Trigger_, _Polyrate_ and _One Shot_, plus the _Record_ button in _Fixed Notes_ mode.

While the arpeggiator is off, its controls are greyed out.

## The step sequencer

### Velocity bars

The height of each bar (2) is that step's velocity — typically, that's how loud the step is played. The taller the bar, the louder the step. Use this to accent every 4th step, for example.

`ctrl`/`cmd`\-click a bar to reset it, or `ctrl`/`cmd`\-drag across several to reset them all.

There are a few things that are good to know about velocities in Floe.

Velocity usually corresponds to volume, but how much depends on the layer's [velocity to volume curve](/docs/beta/usage/config-tab#velocity-to-volume-curve) on the _CONFIG_ tab. That curve sometimes doesn't start at zero, so even the lowest bar may still be heard quietly.

Velocity can also affect more than just volume. For multi-sampled Instruments, it may select different samples to play (for example, playing a softer dynamic hit).

In _Played Notes_ mode, whilst the step's velocity is a major factor, how hard you play the note that triggers the sequence also plays a part — it scales the step velocities down. In other words, when you play the triggering note as softly as possible (MIDI velocity 1), the step velocities will be 50% as intense. When you play as hard as possible (MIDI velocity 127), the step velocities reach their full intensity. This upholds the natural way you use velocity on a keyboard. Play at full velocity to make the step velocities the only thing that matters. In _Fixed Notes_ mode, how hard you play is ignored.

### Step settings

Under each velocity bar is a column of smaller controls (3):

![The rows of step settings under the velocity bars, numbered](https://assets.floe.audio/website/screenshots/arp-step-settings.03a9288cde0f19a0.png)1234

1.  **Step number** — click to skip the step, leaving a gap in the pattern. A skipped step keeps its other settings, so you can bring it back later. It still ends the note before it, just as an active step would.
2.  **Pitch** — drag up or down to nudge the step's note. In _Played Notes_ mode it's an offset in semitones from whichever held note the step plays. In _Fixed Notes_ mode it's the exact note the step plays. Double-click to type a value.
3.  **Tie** — the link icon joins the step to the previous one, so they play as one longer note. Use ties to place some long notes among the short ones.
4.  **Gate** — the small knob sets how long each note lasts, as a proportion of the step. Pull it right down for short, percussive notes; at 100% the notes run into each other for a more legato feel. Remember that the volume envelope's release on the layer's _MAIN_ tab may affect what you hear.

### More step options

Right-click a velocity bar for a menu of options for that step: reset it, apply its settings to all the steps, reset all the steps, or rotate the whole pattern forwards or backwards by one step. Right-click a pitch or gate control to type a value or reset it, or to apply it to every step or reset it on every step.

When the pattern has more steps than fit across the tab, scroll sideways to see the rest; while it plays, the view follows the current step. Alternatively, click _OVERVIEW_ in the top-right corner to squeeze every step on screen at once. You can still draw velocities there, but the other step settings are hidden until you switch back.

## Sequence controls

The _SEQUENCE_ section (4) sets the shape and feel of the whole pattern:

![The arpeggiator's SEQUENCE controls](https://assets.floe.audio/website/screenshots/arp-sequence.3fcc1295cec251c5.png)

-   **Rate** — how long each step lasts, as a note length at your DAW's tempo. The T and D suffixes are triplet and dotted divisions, useful for swung or lopsided patterns. Together with _Length_ it decides how long the pattern takes to come round: eight steps at 1/8 fills one bar of 4/4.
-   **Length** — how many steps the pattern runs through before it loops. Steps beyond the length are hidden but keep their settings, so you can shorten a pattern and lengthen it again without losing anything.
-   **Order** — how the notes you're holding are shared out across the steps. The options include going from lowest to highest or highest to lowest, bouncing between the two, playing at random, and some less common shapes. Hover over the options in the menu for a description of each. _Chord_ is the odd one out: it plays every held note together on each step, which is ideal for rhythmic stabs and percussion. Not available in _Fixed Notes_ mode.
-   **Humanise** — nudges each step's timing and velocity by a random amount, to counteract the machine-gun effect of perfectly identical steps. A few percent takes the mechanical edge off; higher settings give a much looser, hand-played feel. Steps are only ever pushed later, never earlier.

_Humanise_ works especially well with _Chord_ order: instead of every note of the chord starting at exactly the same instant, they spread out slightly, for a more natural, wider sound.

## Config controls

The _CONFIG_ section (5) sets how the pattern responds to your playing:

![The arpeggiator's CONFIG controls](https://assets.floe.audio/website/screenshots/arp-config.69dfd372a93a94bf.png)

-   **Trigger** — what a new note does to a pattern that's already running. _Free_ is usually the one you want: the pattern carries on, so you can change chord underneath it without breaking the groove. _Retrigger_ restarts the pattern from the first step every time you press a note, which keeps it locked to your playing. In both modes, letting go of every key (and the sustain pedal) resets the pattern, so the next chord starts from the first step.
-   **Polyrate** — gives each octave of the notes you're holding its own speed, so one chord can produce several interweaving rhythms. The octave starting at middle C (C3) plays at the _Rate_, and the octaves above and below scale from there. _Double at octaves_ is particularly good: each octave up plays twice as fast as the one below. _3:2 at octaves_ and _4:3 at octaves_ create polyrhythms, where the octaves drift apart and meet up again. Hold two notes an octave apart to hear it most clearly.
-   **One Shot** — the pattern plays through once and stops, rather than looping for as long as you hold the notes. It's handy for fills and one-off flourishes. To play it again, release the keys and press again, or use _Retrigger_.
-   **Record** — in _Fixed Notes_ mode only; see below.

## Played Notes and Fixed Notes

The menu next to the _Arpeggiator_ switch (1) chooses where the steps get their notes from.

_Played Notes_ arpeggiates whatever you're holding. Each step plays one of your held notes, chosen by _Order_, moved up or down by the step's pitch offset.

_Fixed Notes_ ignores which notes you play, and each step plays a specific note that you set. You still hold a key to make the pattern run, but which key doesn't matter. Every step starts on C3, and you can set each one to any note by dragging its pitch.

![A layer's ARP tab in Fixed Notes mode with its parts numbered](https://assets.floe.audio/website/screenshots/arp-fixed-notes.3501fc38f7f26fd5.png)123

1.  **Mode** — the menu beside the _Arpeggiator_ switch, set to _Fixed Notes_.
2.  **Step notes** — each step's note, shown by name. Drag one up or down to change it.
3.  **Record** — play the pattern in from your keyboard.

It's particularly useful with a drum kit Instrument, where each key is a different drum: rather than following your chord, the pattern always plays the drums you've picked.

You can also play the pattern in with the _Record_ button (3) in the _CONFIG_ section. Click it, then play notes on your keyboard in your own time: each note you press lands on the next step. Recording stops by itself once the last step is filled, or you can click again to stop early. Only the notes are recorded; each step keeps the velocity, gate and tie you've already set. While recording, your notes play normally rather than through the pattern.

If your kick drum is on C3 and your snare on D3, for example, play C3, D3, C3, D3 for an alternating pattern.

## Sliced loops

Some Instruments are sliced loops: rhythmic loops marked up with slice points. The arpeggiator is what powers them: each slice becomes a step, so the loop plays in time with your DAW at any tempo. Because each slice is played as it was recorded, rather than being time-stretched, the loop still sounds natural even when your tempo is a long way from the one it was recorded at.

With a sliced Instrument loaded, the arpeggiator is always on, and the _ARP_ tab changes a little:

![A layer's ARP tab with a sliced Instrument loaded, with its parts numbered](https://assets.floe.audio/website/screenshots/arp-sliced.080368edd742cf1b.png)12

-   The _Arpeggiator_ switch, mode menu and _Length_ are hidden.
-   Each step has its velocity bar, step number and gate, but no pitch or tie: ties are set for you, so slices longer than one step play in full.
-   **Auto Rate** appears in the _SEQUENCE_ section (1). It picks the step rate for you, so the loop plays close to the speed it was recorded at rather than leaving silent gaps between slices. It's most useful when making presets: the preset keeps the same feel whether it's loaded into a 60 BPM track or a 170 BPM one. While it's on, the chosen rate is shown in place of the _Rate_ menu. The multiplier shifts that choice: _2x_ for double speed, _0.5x_ for half, with dotted and triplet variants for a different feel. Set it to _Off_ to choose the rate yourself.
-   A _SLICING_ section (2) appears. **Offset** picks which slice the pattern starts from, so you can begin part-way into the loop: an easy way to get a fresh groove out of a familiar loop. **Length** sets how many slices play before the pattern loops back to the offset, wrapping round the end of the loop if it needs to. Set it to _All_ to play every slice. Looping a short run of slices turns a long loop into a tight, repeating figure.

### Auto Rate and tempo changes

You may notice that raising the tempo sometimes makes a loop using _Auto Rate_ play slower, and lowering it can make it play faster: for example, a loop can feel faster at 147 BPM than at 148 BPM. That's because _Auto Rate_ aims to play the loop at roughly the speed it was recorded at, where it sounds most natural, with the fewest silent gaps or overlaps between slices. It can only choose from the tempo-synced note lengths, so when the tempo moves far enough from the loop's own speed, it switches to whichever note length is now closer. Every layer using _Auto Rate_ switches at the same tempos, so they stay in the same rhythmic relationship.

## Good to know

-   The pattern starts the moment you press the first key, and runs whether or not your DAW is playing. It follows your DAW's tempo, but it isn't lined up to its bars and beats.
-   The sustain pedal counts as holding notes, so it keeps the pattern running.
-   The layer's [key range](/docs/beta/usage/config-tab#key-ranges) applies to the notes the arpeggiator plays, not the keys you hold. In _Played Notes_ mode a step whose note lands outside the range is silent.
-   The [_Monophonic_](/docs/beta/usage/config-tab) setting has no effect while the arpeggiator is running.
-   In _Free_ mode with _Chord_ order, press the notes of a chord at the same moment. If one arrives slightly late, the first step plays without it, and it joins on the next step. When playing from your DAW, quantising the recorded notes lines them up so every step plays the whole chord.
-   Right-click the _ARP_ tab to copy and paste the whole arpeggiator, steps included, to another layer.
