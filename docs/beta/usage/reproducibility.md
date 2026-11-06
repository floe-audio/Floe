# Reproducibility

> How to make Floe's playback exactly reproducible in a DAW

Some features, most noticeably granular synthesis and the random LFO shapes, are designed around randomness to make the sound more natural and lively. Normally you don't need to worry about reproducibility: Floe's default setting, _Reset on transport_, covers most uses by making every render of your track in your DAW come out exactly the same. But sometimes those choices aren't quite what you want, such as grains landing in the wrong places or a random LFO moving the wrong way at a key moment. For that, Floe has _seed_ settings that let you take more control.

To understand what a seed does, it helps to know that Floe's randomness isn't truly random. It comes from a sequence of numbers that looks random. The sequence is so long that it never comes round again in practice, but it's fixed: from the same starting point, it always unfolds the same way. The seed is that starting point. So you keep all the spontaneity of random choices, but the same seed always gives the same choices.

Floe goes back to the start of the seed's sequence only at certain moments, called resets. You choose when they happen, and which seed to use, on the _Reproducibility_ tab of the [Performance Controls](/docs/beta/usage/performance-controls) panel:

![Reproducibility tab of the Performance Controls panel, with its settings numbered](https://assets.floe.audio/website/screenshots/reproducibility.acda98d8ce410fef.png)123

1.  **Reset on transport** — reset whenever you press play or start a render in your DAW. Play your track from the top twice and you hear exactly the same thing both times. It's on by default.
2.  **Reset keyswitch** — reset whenever Floe receives the MIDI note you choose here. Put that note in your MIDI just before a section, and that section plays back identically every time, wherever you start playback from. The keyswitch note makes no sound, and it's shown as a blue marker on the on-screen keyboard.
3.  **Seed** — which sequence Floe starts from. Change it to hear a different take, just as lively as the last and just as repeatable.

You can use either kind of reset, or both.

The _Seed_ also covers these smaller sources of variation:

-   The arpeggiator's _Humanise_ and random note orders

-   Which round robin sample plays first after a reset

-   The _White Noise_ Instruments

## Workflow

The _Seed_ settings are a finishing step. You choose a seed once your MIDI is recorded and you're getting ready to render, rather than while you're improvising.

There's one _Seed_ for the whole instance of Floe, and every random choice Floe makes moves its sequence along. So each choice affects all the choices that come after it. That's why it's important to ensure that you play the exact same notes with the exact same timing. A saved MIDI item in your DAW ensures this.

### With _Reset on transport_

This is the default. Floe starts from the _Seed_ whenever you press play or render, so everything from that point on comes out the same every time.

To judge a seed properly, render the whole track again, or play it from the very start. It won't sound the same if you start playback in the middle of a MIDI item, or between MIDI items played by the same instance of Floe, because Floe needs to make all the same choices from start to finish, not starting in the middle.

So the workflow is:

1.  Render your track, or play it from the very start.
2.  If something isn't quite right, change the _Seed_.
3.  Render again and listen, repeating until you have a take you like.

If Floe only plays in one part of your track, you don't need to go right back to the start. Anywhere before Floe's first note works just as well, since Floe hasn't made any choices before then.

### With _Reset keyswitch_

A _Reset keyswitch_ lets you lock in a performance with more granularity. Floe starts again from the _Seed_ every time the keyswitch note plays, so everything after it comes out the same no matter where you started playback or what Floe played before. You can add several of these lock-in points through your track, allowing you to audition the section after each one by starting playback just before its keyswitch.

This also suits looping a section: put the keyswitch at the start of the loop. A DAW loop jumping back to its start doesn't count as pressing play, so without the keyswitch each pass carries on from where the last one left off.

The catch is that every keyswitch uses the same _Seed_. If you change the seed to get a better take of one section, every other section that follows a keyswitch changes too. If a seed suits one section of your track but not another, load a second instance of Floe for that section and give it its own seed.

### Per-layer seeds instead

The [granular engine](/docs/beta/usage/playback-tab#variation) and the LFO's [random shapes](/docs/beta/usage/lfo-tab) offer an alternative to all of this: a per-layer seed , saved with the preset. Rather than waiting for a reset, it applies every time a note starts, so each note gets its own sequence. Choose one of the _Identical_ modes and try a few _Seed_ values until you like the result. It then sounds exactly the same on any computer, in any instance of Floe, without needing any of the settings above. Unlike the instance's _Seed_, it's a sound-design setting you can choose at any time.

## Tips

-   Floe can't recapture a variation after the fact. If an improvised take sounds great, the random choices behind it are gone. Record the MIDI first, then try seeds against it.
-   Performance Controls settings belong to the instance, so loading a preset leaves them untouched: you can try out presets without losing your seed and reset setup.
-   When designing a granular sound, try _Identical on all notes_ with a low _Density_ and step through a few _Seed_ values.
