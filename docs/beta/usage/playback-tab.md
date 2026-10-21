# Playback Tab

> How a layer plays its samples, including granular synthesis and looping

The _PLAYBACK_ tab controls how the layer plays its Instrument's samples: straight through, rebuilt into textures by the [granular engine](#granular-synthesis), or [looping](#looping) for as long as you hold a note.

![A layer's PLAYBACK tab with its parts numbered](https://assets.floe.audio/website/screenshots/layer-playback.306deb78b66d7fc2.png)12345

1.  **Play mode** — there are three modes:
    -   _Standard Playback_ plays each sample straight through (or [loops](#looping) part of it).
    -   _Granular Playback_ uses [granular synthesis](#granular-synthesis), with the point the grains come from moving through the sample at a speed you choose (and looping, if a loop is set).
    -   _Granular Fixed_ also uses granular synthesis, with the point the grains come from set directly by a _Position_ control rather than moving on its own.
2.  **Waveform display** — for a single-sample Instrument, this is the sample itself. A multi-sampled Instrument contains many samples, so this shows the one from the most recent note (or a representative one until you play). If notes change too quickly to follow, it pauses on a recent sample, shown with a checkered overlay. When you play, red markers track each voice through the sample. The waveform often has [looping](#looping) controls as well.
    -   **Sample start** — the arrow on the bottom edge of the waveform sets where playback begins. Drag it to skip the beginning of the sound; the skipped part is shaded. If you move it into the audio, a few milliseconds of _Attack_ on the [volume envelope](/docs/beta/usage/main-tab#volume-envelope) avoids clicks. It isn't available for sliced Instruments.
3.  **Sample info** — shows whether the Instrument is a single sample or a multisample, with details such as the number of samples, or a single sample's length and root key.
4.  **Reverse** — play the samples backwards. It works in every play mode, with or without looping. The waveform flips to match, so playback always runs from left to right.
5.  **Loop mode** — choose how the sound loops, if at all. For example, you can use the Instrument's built-in loops, or set your own loop points on the waveform. Hover over the selector to see what the current setting does for this Instrument, and why some options may be unavailable.

Floe comes with a few simple built-in waveforms (oscillators). Many of the controls above don't apply to them, and so are hidden. Since they're algorithmic waveforms, the red voice markers are placed left to right by pitch instead of moving through a sample.

## Granular synthesis

As well as playing samples the usual way, from start to finish, each layer can switch to the granular engine. Granular synthesis plays lots of small snippets of the sample, called grains. Each grain can come from a different part of the sample, and play in a different direction, at a different pitch, or from a different place in the stereo field. It can completely transform a sample.

It's particularly good at smooth, lush textures. You can isolate a tiny fraction of a sample, such as one particular tonal moment, and hold it for as long as you like by running lots of grains over it. It's just as good at crackly, gritty textures, or at sparse ones where the grains are spread out so you can hear each one coming from a different point in the sample.

The granular engine works with any sampled Instrument. For each note, the grains come from the sample that note would normally play.

Granular synthesis uses randomness to keep the grains spontaneous and natural-sounding, rather than a mechanical, machine-gun-like repetition. Because of that, the sound can vary from note to note. When you want the same result every time, you have full control over that too: see [Reproducibility](/docs/beta/usage/reproducibility).

### The two granular modes

Choose a granular mode from the play mode menu at the top of the tab, where _Standard Playback_ is the usual way of playing samples.

-   **Granular Playback** — the point the grains come from travels through the sample at a steady speed, set by _Speed_. Use it to slow a sample down or speed it up without changing its pitch. It's not as precise as a dedicated time-stretching algorithm, but it gives a grainy or lush kind of time-stretch that's useful in its own right. Turn _Speed_ right down and the playhead barely moves, so a sample only a couple of seconds long can give you a whole minute of slowly changing texture. _Speed_ at 100%, the centre of the knob, is the sample's original speed, and the playhead still follows the sample start, looping and _Reverse_ settings.
-   **Granular Fixed** — instead of _Speed_ there's a _Position_ knob, which picks a precise point in the sample for all the grains to come from. _Spread_ sets how wide that region is.

Apart from _Speed_ and _Position_, the two modes have exactly the same controls.

### Getting started

Granular Fixed is perhaps the best place to start, because you can see and hear exactly what each control does.

![A layer's PLAYBACK tab in Granular Fixed mode with its parts numbered](https://assets.floe.audio/website/screenshots/layer-playback-granular-fixed.2140192424972f7d.png)123456

1.  **Share grains** — how grains build up when you play several notes at once.
2.  **Variation** — take the element of chance out of the grains.
3.  **Waveform display** — shows where the grains come from.
4.  **Playhead** — where in the sample the grains come from.
5.  **Grains** — the shape of each grain, and how much they overlap.
6.  **Random** — how much each grain differs from the next.

Choose _Granular Fixed_ from the play mode menu, then use _Position_ to pick the point in the sample you want to isolate. The highlighted region on the waveform shows where grains will come from. Play some keys: all the sound you hear is coming from that exact point, and lilac lines on the waveform show each grain as it plays.

From there, try these controls:

![The PLAYHEAD and GRAINS controls](https://assets.floe.audio/website/screenshots/granular-controls.9551a5fa66a30bfb.png)

-   **Density** — perhaps the next most useful control. It works a bit like an overlap percentage: at 0%, each grain starts as the previous one ends. Turn it up and new grains start sooner, so they overlap more and more, up to 100% where there may be hundreds of grains playing. Low values give an open texture where you can pick out individual grains; high values build a thick, continuous wash. High _Density_ uses more CPU.
-   **Length** — how long each grain lasts. Short grains chop the sound into a fine, buzzy texture; long grains keep much more of the sample's natural character. _Density_ and _Smooth_ are both measured relative to _Length_, so changing it changes how they feel too.
-   **Smooth** — how gently each grain fades in and out. High values give a softer texture, with neighbouring grains blending into each other. Low values make grains start and stop abruptly, for a grittier, glitchier texture.
-   **Spread** — widens the region that grains can come from, which you can see straight away on the waveform: each grain starts somewhere between _Position_ and a point this far past it. A narrow spread gives an isolated, pinpoint tone. Spread it out completely and grains come from all over the sample, for a texture that's much more varied, unexpected and full of different tones.

### Random

The controls under the _RANDOM_ heading (6) make the texture even more varied. Each grain picks its own settings at random as it starts, within the range you set. At 0% every grain is the same.

![The RANDOM controls and the Intervals menu](https://assets.floe.audio/website/screenshots/granular-random.2567c6ef69428431.png)

-   **Pan** — the higher it goes, the more likely grains are to be panned left and right. Especially with a high _Density_, it gives a rich stereo image, with grains spread right across it.
-   **Detune** — sets each grain's pitch at random, up to a semitone up or down. Low values give a chorus or unison-like thickness; high values sound obviously out of tune.
-   **Direction** — the chance of each grain playing in the opposite direction, backwards if the playhead is going forwards. At 100%, each grain is equally likely to play either way.
-   **Harmony** — the chance of a grain playing at a musical interval from the note you played. It works with the _Intervals_ menu beside it, and does nothing until you've chosen at least one interval there. For example, click _12_ in the menu's grid and some grains will play 12 semitones (an octave) above your note; turn up _Harmony_ and more of them will. Click a selected interval again to turn it off. You can pick lots of intervals at once, or choose a preset such as _Wide Octaves_, _Major Triad_ or _Pentatonic_. The note you played is always included. It's a great way to make lush, interesting tones.

![The open Intervals menu, with presets on the left and the grid of intervals on the right](https://assets.floe.audio/website/screenshots/granular-harmony.7692588f84b54478.png)

### Share grains

The button just to the right of the mode's name, showing a cloud of dots (1), chooses how grains build up when you play several notes at once. Hover over it to see which way it's set. It's there for both granular modes.

-   **Shared grains** — the number of grains stays the same however many notes you play. If you're after a sparse texture, it stays sparse even when you play a whole chord. Each grain plays from one of the held notes, so at a low _Density_ you can hear the grains hop between the notes of the chord. This is the default setting.
-   **Per-voice grains** — every note you play adds its own full set of grains. If your _Density_ setting gives 20 overlapping grains, a second note adds another 20, so the sound gets denser, and possibly louder, with each note.

### Variation

![The Variation and Seed settings, open beneath the Variation button](https://assets.floe.audio/website/screenshots/granular-variation.865ff2e8cbd88c95.png)

Each grain picks its starting point, pan, detune and so on at random, so every note you play scatters its grains differently. That keeps a granular sound alive, but it also means a pattern of grains you love might not come back. _Variation_ lets you make the grains the same every time instead.

Click the button (2), to the right of the share grains button, and choose one of these:

-   **Different on every note** (default) — every note scatters its grains differently. The best choice for most sounds, especially dense, textural ones. Even in this mode, you can get reproducible renders of your DAW track, see [Reproducibility](/docs/beta/usage/reproducibility).
-   **Identical on all notes** — every note plays exactly the same grains. Hold a chord and each note plays them at its own pitch. With shared grains, the notes take turns, so you hear the same grains come back at different pitches.
-   **Identical on each key** — each key gets its own pattern of grains. C3 always sounds the same, D3 always sounds the same (but is different to C3), etc..

With either _Identical_ option, the _Seed_ number picks which pattern repeats. Try a few numbers until you find one you like. It's saved with the preset, so the grains you chose are the grains everyone hears. The button lights up while one of the _Identical_ options is chosen.

See [Reproducibility](/docs/beta/usage/reproducibility) for more on seeds, and on making a whole performance come out the same every time in your DAW.

### Granular tips

-   In _Granular Fixed_ mode, the point the grains come from only moves when _Position_ does. To make it wander through the sample on its own, set the [LFO's](/docs/beta/usage/lfo-tab) target to _Grain Position_: the LFO sweeps _Position_ back and forth, so the texture keeps changing as you hold a note. Or link _Position_ to a [macro](/docs/beta/usage/macros) to move it by hand as you play.
-   The volume envelope, filter, EQ and effects all shape the granular sound just as they would in _Standard Playback_.
-   When designing a sound, try _Identical on all notes_ with a low _Density_ and step through a few _Seed_ values. Once you hear a grain scatter you like, save the preset and it's kept exactly.

## Looping

You can loop part of a sample for as long as you hold a note. Even a short sample can then sustain indefinitely, or you can pick out a section that has some movement in it, so the sound cycles through it over and over. You have full control over where the loop sits and how smoothly it joins up.

Looping works in _Standard Playback_ and in the _Granular Playback_ [granular mode](#granular-synthesis).

![A layer's waveform and loop controls with their parts numbered](https://assets.floe.audio/website/screenshots/looping.4f51878d98be7c61.png)123456

1.  **Loop start** — where the loop begins.
2.  **Loop end** — where the loop finishes, and playback goes back round.
3.  **Crossfade** — how smoothly the loop joins up.
4.  **Sample start** — where playback begins when you press a key. It isn't part of the loop, but it lives alongside it.
5.  **Reverse** — play the samples, and the loop, backwards. The loop end then appears on the left and the loop start on the right, though the loop points themselves don't change.
6.  **Loop mode** — how the sound loops, if at all.

### Loop modes

There are two modes of looping:

-   **Standard** — when playback reaches the loop end, it jumps back to the loop start, optionally smoothing the jump with a crossfade.
-   **Ping-pong** — uses the same start and end points, but at each end of the loop the playback direction reverses, so it goes back over what it just played, the other way.

Choose the loop mode from the selector (6). Which options are available depends on the Instrument. Some let you set your own loop points, some come with loops already built in, and some keep that control to themselves, such as sounds that are perfectly sustained or already loop.

![The loop mode menu](https://assets.floe.audio/website/screenshots/loop-mode-menu.96adc5ab47405221.png)

-   **Default** — let the Instrument decide: each sound loops, or doesn't, just as it was designed to. The menu shows what that means for the current Instrument.
-   **Built-in Loop - Standard** and **Built-in Loop - Ping-pong** — use the loop points that come built into the Instrument, looping in the direction you choose.
-   **No Loop** — each sound plays straight through.
-   **Custom Loop - Standard** and **Custom Loop - Ping-pong** — set your own loop points on the waveform.

Options that aren't available for the current Instrument are dimmed: hover over one to see why. Some Instruments have no built-in loops, for example, or don't allow custom loop points, or have sounds that must always loop. The selector itself shows what's actually happening, and hovering over it explains the current setting. The arrows beside it, and , step through only the options that are available.

### Setting your own loop

Choose one of the _Custom Loop_ modes, and the loop's handles appear on the waveform. Drag the loop start (1) and loop end (2) to set where the loop sits. The highlighted region between them is the part that loops. Drag inside the region to move the whole loop without changing its length.

The handles work like Floe's other [controls](/docs/beta/usage/controls): double-click one to type an exact value, `shift`\-drag for finer control, and `ctrl`/`cmd`\-click to reset it. Double-clicking the highlighted region types a value for the loop start.

For a multi-sampled Instrument, the loop points are positions through the sample, as a percentage of its length. Whichever sample a note triggers, it loops between those same percentages.

With a built-in loop on an Instrument that has just one sample, the handles show where the loop sits, but they're greyed out and can't be moved. To set your own, choose one of the _Custom Loop_ modes.

### Crossfade

A loop can click as playback jumps from one point in the sample to another. The crossfade (3) blends the audio across the join to smooth it out. A small crossfade usually helps, especially for sustained or tonal sounds. Drag its handle to change it.

-   In _Standard_ mode, the crossfade happens as playback nears the loop end, blending into the audio just before the loop start. Its handle sits inside the loop.
-   In _Ping-pong_ mode, the crossfade smooths each turnaround, blending in the audio just beyond the loop point. Its handle sits outside the loop.

The crossfade needs audio before the loop start to blend in from, so it can't be used while the loop starts at the very beginning of the sample: its handle is greyed out until you move the loop start later. The crossfade also has to fit within the loop and the sample, so it shrinks automatically if you make the loop too small for it. In _Ping-pong_ mode, it also needs room after the loop end.

### Looping tips

-   Looping needs the [volume envelope](/docs/beta/usage/main-tab#volume-envelope) to be on. With it off, nothing would ever bring a looping note to an end.
-   Pitch is changed by speeding up or slowing down playback. So the higher you play, the faster the loop cycles, and the lower you play, the slower. In _Granular Playback_ mode, the loop cycles at the rate set by _Speed_ instead.
-   In _Granular Fixed_ mode the grains always come from the _Position_ you choose, so there's no looping and the loop controls are hidden.
-   A loop can be moved by a [macro](/docs/beta/usage/macros). The highlighted region then shows where the loop actually is, while the handles stay at their own settings.

-   Release samples, which some Instruments play when you let go of a key, never loop.
