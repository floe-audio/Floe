# Effects

> Information about the audio effects available in Floe

Floe has a selection of 12 reorderable effects that are applied to the mix of the [layers](/docs/usage/layers). Audio flows through the rack from top to bottom.

![Effects panel](/images/screenshots/effects.png)

## Reordering

You can change the ordering of the effects by dragging and dropping the effect names — either the name in the rack or the name next to the on/off switch.

## Enabling and bypassing

Each effect has two controls for its active state:

-   **Switchboard toggle** (left sidebar): Toggles the effect on or off. When toggled off, the effect is both deactivated and hidden from the rack.
-   **Bypass button** (power icon in the effect heading): Deactivates the effect's audio processing but keeps it visible in the rack. This is useful for quickly A/B comparing with and without an effect, or for keeping your effect chain layout while temporarily disabling an effect.

A bypassed effect appears greyed out in the rack. Click the bypass button again to reactivate it. The close button (✕) on an effect removes it from the rack entirely, same as toggling it off in the switchboard.

## Panel buttons

The buttons in the panel heading act on all effects at once:

-   **Randomise** (shuffle icon): Randomly turns effects on or off and shuffles their order.
-   **Bypass all** (power icon): Bypasses every active effect, or reactivates them all if any are already bypassed.
-   **Remove all** (unload icon): Removes every effect from the rack.

Right-clicking an effect — either its heading or its switchboard entry — opens a menu for copying and pasting effects, and for resetting that effect's parameters to their defaults. You can copy a single effect and paste it onto the same effect elsewhere, or copy the entire FX rack — every effect, its order and its settings — and paste it as a whole. This works within a preset or between presets: copy from one, load another, and paste.

Right-clicking the empty space of the switchboard or the rack opens the same copy and paste options for the whole rack, plus an option to paste a single copied effect.

## Mix

Every effect has a _Mix_ knob in its heading area. Mix blends the original incoming audio (the _dry_ signal) with the effect's output (the _wet_ signal):

-   At 0%, you hear only the dry signal — the effect is effectively inaudible.
-   At 100%, you hear only the wet signal — the original is fully replaced by the effect's output.
-   In between, the two are crossfaded.

This lets you dial in the strength of an effect without changing any of its other settings — useful for subtle reverb tails, parallel compression, gentle distortion, and so on.

## Convolution Reverb

The convolution unit adds a reverb-like effect to the sound by utilising an impulse response (IR) selected using a dedicated [browser](/docs/usage/browsers). Floe offers some realistic reverb room IRs, but also IRs that produce unusual sound effects.

The set of available IRs depend on what sample libraries you have installed and their features. Floe always has a built-in set of impulse responses available. But additionally, some sample libraries expand this collection, such as the community [Antique IRs](/packages/antique-irs) library.

## All available effects

| Name | Description |
| --- | --- |
| Distortion | This effect pushes the signal through a shaping curve, for anything from gentle tape warmth to outright destruction. Oversampled and anti-aliased for a clean sound. |
| Bit Crush | This is a lo-fi effect that degrades the signal in two ways: dropping the sample rate for ringing, metallic aliasing, and reducing the bit depth for gritty quantisation noise. Both controls start at full quality, so lower them to hear the effect. |
| Compressor | This effect can be used to shape the dynamics: controlling dynamics and making quiet sections louder. |
| Filter | This effect filters the signal, either cutting away a region of the frequency range or boosting/dipping it. |
| Stereo Widen | This effect allows for both narrowing the signal towards mono, or spreading it out wider. There's also a Bass Mono mode that holds the low end in the centre while everything above it widens. |
| Chorus | This effect thickens the sound by layering it with delayed copies that drift in pitch. Gentle settings add a subtle shimmer and movement, while deeper settings give an obvious, tape-like wobble. |
| Reverb | This effect algorithmically simulates the reflections and reverberations of a real space, from a small, tight room to a vast hall that takes many seconds to fade. Features modulation options for creating shimmering tails. |
| Delay | A fully-featured stereo echo, with separate left and right times, free or tempo-synced, a choice of ping-pong modes, and a filter that thins the repeats as they fade. |
| Convol Reverb | This effect's character comes entirely from an impulse response (IR): a sample of how a space or object responds to sound. Most of the IRs on offer are strange and characterful, making this as much a sound-design tool as a reverb. |
| Phaser | This effect can sweep a series of peaks and notches through the sound, giving it the classic swooshing, jet-like motion. Gentle settings add a subtle sense of movement to sustained sounds, while faster or more resonant ones become an unmistakable whoosh. |
| EQ | This is a three-band equaliser for lifting or taming particular parts of the frequency range, from broad tonal shaping to surgical cuts. |
| Limiter | This effect can hold the signal below a set ceiling, either to catch stray peaks or to push the overall level up without clipping. It's a true-peak brickwall limiter with a very short lookahead, and usually belongs at the end of the effects chain. |
