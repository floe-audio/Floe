# Reproducibility

> How to make Floe's playback exactly reproducible in a DAW

## At a glance

1.  Reset on transport is on by default, so every DAW render of your track already comes out the same.
2.  For finer-grained control within a render, set a Reset keyswitch — a MIDI note that restarts the variation wherever you send it (for example, before each MIDI pattern).
3.  Pick a Seed to choose which variation plays back; the same seed always produces the same performance.

## Background

Floe has several elements that vary between performances, including:

-   Round robin sample selection
-   The granular engine's randomised grain properties
-   The LFO's random waveforms (Random Steps and Random Glide)
-   Other smaller sources of variation throughout the engine

Left alone, these would drift between plays. The Instance Config panel is where you control them — open it from the top-panel menu (the three-dots button).

![Instance Config Panel GUI](/images/screenshots/instance-config.png)

## How it works

Although the variation feels random as you play, it actually follows a fixed sequence behind the scenes — round robins step through their samples in a set order, and the randomised elements are drawn from a fixed sequence too. Given the same notes and settings, Floe will always produce the same result, as long as it knows where to begin.

By "the same notes" we mean every detail of how they're played: order, timing, length, and velocity all count. Any change between one play and the next sends the variation down a different path from that moment on.

So to get a repeatable performance, you put a marker in the sand: a moment that tells Floe "start every variation sequence from here." Every time you hit that marker, Floe rewinds to the same starting point, and the performance that follows will be identical.

## Placing the marker

Floe gives you two ways to set the marker — use either, or both at once. The difference is how fine-grained the control is.

-   Reset on transport — the marker is placed each time the DAW transport starts playing. Enabled by default. There's only one moment of control (the start of playback), but it's enough to make every render of your track come out the same.
-   Reset keyswitch — a MIDI note that places the marker whenever you send it. Drop a keyswitch before each MIDI pattern and the variation restarts at every one of them, giving you fine-grained control over where reproducibility kicks in. The note itself is consumed and does not trigger any sound. Shown as a blue marker on the on-screen keyboard.

## Seed

The seed is a number that picks the starting point for Floe's variation. Each seed gives a different round robin starting position and a different sequence of randomised choices — and the same seed always reproduces the same performance. To explore alternatives, change the seed and replay the same notes; you'll hear a different result that is itself fully reproducible.

## Tips

-   Reproducibility runs forwards, not backwards. If you're improvising and a particular take sounds great, there's no way to recapture the variation you just heard — Floe doesn't record the path it took. Decide on the part first, then audition seeds against it until you find one that fits.
-   These settings belong to this specific instance of Floe rather than being a global preference, and they are saved with your DAW session. Loading a preset leaves them untouched, so you can audition presets without losing your reproducibility setup. Different instances in the same project can use different settings.
-   A single instance only has one seed, so every marker hit within that instance uses the same one. If a seed sounds great under one section of your track but wrong under another, you can either hunt for a seed that works across all your reset points, or load a second instance of Floe for the section that needs its own variation.
