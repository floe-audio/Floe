# MIDI

> Information about MIDI control in Floe

Floe is controllable via MIDI in the typical ways you'd expect from a software instrument.

## MIDI CC

All automatable parameters can be assigned to a MIDI CC, so you can control Floe from a MIDI controller.

### Creating a mapping

Right-click a parameter and select **MIDI CC Learn**, then move the control on your MIDI controller you want to assign — this creates the mapping. To remove a mapping, right-click the parameter and select **Remove MIDI CC**.

You can also add and remove mappings from the [Performance Controls](/docs/beta/usage/performance-controls) panel, which lists every mapping on the current instance in one place.

### Default mappings

Unless you have changed you default Performance Controls, these mappings will be assigned:

-   **CC 1 (Modwheel)** — Macro 1
-   **CC 7** — Master Volume
-   **CC 11** — Master Timbre

### How mappings are saved

MIDI CC mappings are part of the [Performance Controls](/docs/beta/usage/performance-controls) system. They apply only the current instance of Floe. However, you can easily set up a 'default' configuration that all new instances of Floe will start with. As with all performance controls, preset files never touch them so that you can browse presets without your settings changing each time.

## Sustain Pedal

Floe supports sustain pedal control via MIDI CC-64. When the sustain pedal is pressed, currently held notes will continue playing even after key release, until the pedal is released. This mimics standard piano sustain pedal behaviour.

## Pitch Wheel

Floe supports the MIDI pitch wheel. The extent of the pitch bend can be configured individually for each layer by using the pitch bend range parameter on the [Config tab of each layer](/docs/beta/usage/layers#config-tab). This per-layer configuration allows for interesting sound design possibilities. A pitch bend range of 0 will disable pitch bend for that layer.

## MPE

Floe supports MIDI Polyphonic Expression (MPE), letting compatible controllers (such as Seaboard, LinnStrument, Osmose, and others) control each note independently.

Enable MPE from the [Performance Controls](/docs/beta/usage/performance-controls) panel.

When enabled, Floe responds to the standard MPE per-note messages:

-   **Glide**: pitch bend on a note's channel bends just that note, following the MPE ±48-semitone convention (or whatever range your controller negotiates). The master channel's pitch wheel still bends everything, using each layer's pitch bend range parameter.
-   **Press**: each note's channel pressure (aftertouch).
-   **Slide**: each note's CC74.

Choose what press and slide control per layer on the [Config tab](/docs/beta/usage/layers#config-tab). You can set a target (volume, filter cutoff, or timbre) and an amount (which can be negative to invert the response, and even have values beyond 100% to reach the full effect with a smaller gesture).

For the volume target, press restores volume up to the layer's Volume slider, and slide acts as a per-note fader — the top of its travel is the layer's Volume level. For the filter cutoff and timbre targets, press adds an offset and slide is bipolar with its centre position neutral. The timbre target offsets the master timbre control per note.

The smoothing setting in the [Performance Controls](/docs/beta/usage/performance-controls) panel sets how gradually press and slide respond: lower is more responsive, higher is smoother.

While notes are sounding, each active voice gets a red line on whichever control press or slide is routed to — the filter cutoff knob, or the master Timbre knob in the top panel — so you can see the expression from each voice.

Floe also understands the MPE Configuration Message for zone setup, and a sustain pedal on a zone's master channel sustains the whole zone.

CLAP note expressions (per-note pitch, pressure, and brightness) work the same way in supporting hosts regardless of the MPE setting.

## Velocity

[Each layer](/docs/beta/usage/layers#config-tab) has a customisable velocity-to-volume curve that shapes how hard you play (MIDI velocity) into the volume of each note.

![Velocity to volume curve editor](/images/screenshots/velocity-curve.png)

### How the curve works

The horizontal axis is velocity — far left is the softest possible velocity (MIDI value 1), far right is the hardest (MIDI value 127), and higher-resolution velocity from DAWs that support it works too. The vertical axis is the resulting note volume. When you play a note, a red indicator appears at its velocity position on the curve so you can see where you're playing.

While notes are sounding, each active voice also gets a red line on the layer's volume slider, showing the level that voice is currently playing at after the curve (and, in MPE mode, any per-note volume expression) has been applied.

A straight line from bottom-left to top-right gives a 1:1 response — the harder you play, the louder the note, in equal proportion. By moving, adding, or removing control points (and reshaping the curve between them), you can change this relationship: make soft playing louder than it would be naturally, cap the volume of hard hits, or create a non-linear response that emphasises a particular dynamic range. Setting different curves across the 3 layers also opens up creative sound design — for example, fading one layer in as you play harder while another fades out.

By default the curve doesn't start at zero, so even a zero-velocity note still produces a quiet sound. You can drag the left-most point down to silence if you want zero-velocity notes to be inaudible.

### Velocity and multisampled instruments

It's worth noting that velocity does more than just control volume. For multisampled instruments that contain separate velocity layers, a note's velocity also selects _which_ sample is played — not just how loud it is. A low-velocity note on a piano, for instance, plays the soft-velocity samples (with their distinct tone and character) rather than a quieter version of the hard-hit samples. The velocity curve only reshapes the volume response; it doesn't change which velocity layer gets triggered.

### Editing velocity curves

Find the velocity curve editor on the **Config tab** for each layer.

-   **Double-click** anywhere on the curve to add a control point (up to 6 per layer)
-   **Drag control points** to move them
-   **Drag between points** to adjust curve shape
-   **Double-click a point** to remove it
-   **Right-click** for menu options
