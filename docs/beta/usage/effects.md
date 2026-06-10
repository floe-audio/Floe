# Effects

> Information about the audio effects available in Floe

Floe has a selection of 11 reorderable effects that are applied to the mix of the [layers](/docs/beta/usage/layers). Audio flows through the rack from top to bottom.

![Effects panel](/images/screenshots/effects.png)

## Reordering

You can change the ordering of the effects by dragging and dropping the effect names — either the name in the rack or the name next to the on/off switch.

## Enabling and bypassing

Each effect has two controls for its active state:

-   **Switchboard toggle** (left sidebar): Toggles the effect on or off. When toggled off, the effect is both deactivated and hidden from the rack.
-   **Bypass button** (power icon in the effect heading): Deactivates the effect's audio processing but keeps it visible in the rack. This is useful for quickly A/B comparing with and without an effect, or for keeping your effect chain layout while temporarily disabling an effect.

A bypassed effect appears greyed out in the rack. Click the bypass button again to reactivate it. The close button (✕) on an effect removes it from the rack entirely, same as toggling it off in the switchboard.

## Mix

Every effect has a _Mix_ knob in its heading area. Mix blends the original incoming audio (the _dry_ signal) with the effect's output (the _wet_ signal):

-   At 0%, you hear only the dry signal — the effect is effectively inaudible.
-   At 100%, you hear only the wet signal — the original is fully replaced by the effect's output.
-   In between, the two are crossfaded.

This lets you dial in the strength of an effect without changing any of its other settings — useful for subtle reverb tails, parallel compression, gentle distortion, and so on.

## Convolution Reverb

The convolution unit adds a reverb-like effect to the sound by utilising an impulse response (IR) selected using a dedicated [browser](/docs/beta/usage/browsers). Floe offers some realistic reverb room IRs, but also IRs that produce unusual sound effects.

The set of available IRs depend on what sample libraries you have installed and their features. Floe always has a built-in set of impulse responses available. But additionally, some sample libraries expand this collection, such as the community [Antique IRs](/packages/antique-irs) library.

## All available effects

| Name | Description |
| --- | --- |
| Distortion | Distort the audio using various algorithms. |
| Bit Crush | Apply a lo-fi effect to the signal by either reducing the sample rate or by reducing the sample resolution. Doing either distorts the signal. |
| Compressor | Compress the signal to make the quiet sections louder. |
| Filter | Adjust the volume frequency bands in the signal, or cut out frequency bands altogether. The filter type can be selected with the menu. |
| Stereo Widen | Increase or decrease the stereo width of the signal. |
| Chorus | An effect that changes the character of the signal by adding a modulated and pitch-varying duplicate signal. |
| Reverb | Algorithmically simulate the reflections and reverberations of a real room. |
| Delay | Simulate an echo effect, as if the sound is reflecting off of a distant surface. |
| Convol Reverb | The Convolution reverb effect applies a reverb to the signal. The characteristic of the reverb is determined by the impulse response (IR). The IR can be selected from the menu. |
| Phaser | Modulate the sound using a series of moving filters |
| EQ | Three-band parametric equaliser. Each band can be configured as a peak, shelf, notch, low-pass or high-pass filter. |
