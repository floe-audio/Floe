# Reproducibility

> How to make Floe's playback exactly reproducible in a DAW

Some of Floe's features, most noticeably granular and the random LFO shapes, are designed around randomness to make the sound more natural and lively. Normally you don't need to worry about reproducibility: Floe's default setting, _Reset on transport_, covers most uses by making every render of your track in your DAW come out exactly the same. But sometimes those choices aren't quite what you want, such as grains landing in the wrong places or a random LFO moving the wrong way at a key moment. For that, Floe has seed settings that let you take more control.

To see what a seed does, it helps to know that Floe's randomness isn't truly random. It comes from a sequence of numbers that looks random, such as 57, 83, 12, 40, and so on. Floe works out each number from the one before, so the sequence never loops or repeats, but it's fixed: from the same starting point, it always unfolds the same way. The seed is that starting point. So you keep all the spontaneity of random choices, but the same seed always gives the same ones, as long as you play the same notes with the same timing and velocity.

You set the _Seed_ in the [Performance Controls](/docs/beta/usage/performance-controls) panel, but Floe only starts from it at certain moments. With _Reset on transport_, that's whenever you press play or render in your DAW. With _Reset keyswitch_, it's whenever you play a chosen MIDI note, so dropping it before each pattern makes each one play back identically. The keyswitch note itself is silent, and it's shown as a blue marker on the on-screen keyboard. Change the _Seed_ to hear a different take, just as lively as the last, and each one is fully repeatable.

The [granular engine](/docs/beta/usage/granular#variation) and the LFO's [random shapes](/docs/beta/usage/layers#lfo-tab) offer a different approach: a per-layer seed, saved with the preset. Rather than waiting for a reset, it applies every time a note starts. Each note gets its own sequence instead of sharing one with everything else, so nothing else you play changes which grains or LFO movement it gets. _Identical on all notes_ gives every note exactly the same pattern; hold a chord with a low _Density_ and each note plays the same grains at its own pitch. With [shared grains](/docs/beta/usage/granular#share-grains), the notes take turns playing those grains, and which note plays each one repeats too. _Identical on each key_ also mixes in the key number (C3 is 60, C#3 is 61, etc.), so each key has its own pattern. These _Identical_ modes ensure that it's exactly the same on any computer, in any instance of Floe.

## Other random elements

The seed also controls these smaller sources of variation:

-   The arpeggiator's _Humanise_, and its _Random_ and _Random No Repeat_ note orders
-   Which round robin sample plays first after a reset (after that, round robins step through in order)

## Tips

-   To find a variation you like, change the _Seed_ and press play again, repeating until one fits. If you'd rather loop a section, put the _Reset keyswitch_ at the start of the loop: a DAW loop jumping back to its start doesn't count as pressing play, so without the keyswitch each pass carries on from where the last one left off.
-   Put the _Reset keyswitch_ note at the very start of the MIDI clip itself, rather than on a separate track. Then the clip plays back identically wherever you copy or move it in your arrangement.
-   Floe can't recapture a variation after the fact. If an improvised take sounds great, the random choices behind it are gone. Record the MIDI first, then try seeds against it.
-   One instance has only one _Seed_. If a seed suits one section of your track but not another, load a second instance of Floe for that section and give it its own seed.
-   Performance Controls settings belong to the instance, and loading a preset leaves them untouched, so you can try out presets without losing your seed and reset setup.
-   When designing a granular sound, try _Identical on all notes_ with a low _Density_ and step through a few _Seed_ values. Once you hear a grain scatter you like, save the preset and it's kept exactly.
