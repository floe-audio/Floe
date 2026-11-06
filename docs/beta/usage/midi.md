# MIDI

> Information about MIDI control in Floe

Floe responds to MIDI the way you'd expect from a software instrument: notes and velocity, the sustain pedal and pitch wheel, CC controllers mapped to its parameters, and MPE for expressive controllers.

-   **Velocity** — how hard you play sets each note's volume, shaped by each layer's [velocity to volume curve](/docs/beta/usage/config-tab#velocity-to-volume-curve) on its _CONFIG_ tab. On multi-sampled Instruments with velocity layers, it also picks which samples play.
-   **Sustain pedal** — CC-64 holds notes after you release the keys, until you lift the pedal, just like a piano's sustain pedal.
-   **Pitch wheel** — how far it bends is set per layer with _Pitch Bend Range_ on the layer's [_CONFIG_ tab](/docs/beta/usage/config-tab), so one layer can bend a whole tone while another underneath it stays put. A range of 0 disables pitch bend for that layer.
-   **All Notes Off and All Sound Off** — the channel messages that DAWs and controllers send to stop stuck notes. All Notes Off (CC-123) releases every note on that channel, including sustained ones, so they fade out naturally; All Sound Off (CC-120) silences them immediately.

## MIDI CC

All automatable parameters can be assigned to a MIDI CC, so you can move them with the knobs and faders on your MIDI controller.

### Creating a mapping

Right-click a parameter (a knob or slider anywhere on the UI) and select _MIDI CC Learn_, then move the dial on your MIDI controller that you want to assign. To remove a mapping, right-click the parameter and select _Remove MIDI CC_ (followed by the CC number). A mapped CC works on any MIDI channel.

You can also add and remove mappings from the [Performance Controls](/docs/beta/usage/performance-controls) panel, which lists every mapping on the current instance in one place.

![MIDI CC tab of the Performance Controls panel](https://assets.floe.audio/website/screenshots/midi-cc.2063ddce6cec1071.png)

### Default mappings

Unless you have changed your default Performance Controls, these mappings will be assigned:

-   **CC 1 (Modwheel)** — Macro 1
-   **CC 7** — Master Volume
-   **CC 11** — Master Timbre

### How mappings are saved

MIDI CC mappings are part of the [Performance Controls](/docs/beta/usage/performance-controls) system. They apply only to the current instance of Floe, but you can set up a default configuration that all new instances start with. Presets never touch them, so you can browse presets without your settings changing each time.

## MPE

MIDI Polyphonic Expression (MPE) lets compatible controllers, such as the Seaboard, LinnStrument, Osmose and Haken Continuum, shape each note independently.

To switch it on, open the [Performance Controls](/docs/beta/usage/performance-controls) panel with in the top panel, click the _MPE_ tab, and tick _MPE (MIDI Polyphonic Expression)_.

![The MPE tab of the Performance Controls panel](https://assets.floe.audio/website/screenshots/mpe.2d6bff66cf1a9aa7.png)

Once it's on, each note responds to the standard MPE per-note messages:

-   **Glide**: pitch bend on a note's channel bends just that note, following the MPE ±48-semitone convention (or whatever range your controller negotiates). The master channel's pitch wheel still bends everything, using each layer's pitch bend range parameter.
-   **Press**: each note's channel pressure (aftertouch).
-   **Slide**: each note's CC74.

Choose what press and slide control for each layer with the _MPE_ button on its [_CONFIG_ tab](/docs/beta/usage/config-tab). Each has a target (_Volume_, _Filter_, _Timbre_ or _LFO Amount_) and an amount. The amount can be negative to invert the response, or beyond 100% to reach the full effect with a smaller gesture.

With the _Volume_ target, press brings each note up to the level of the layer's _Volume_ slider, and slide works like a fader for each note, topping out at the slider's level. So until you apply expression, notes play quieter than the slider. The _Filter_, _Timbre_ and _LFO Amount_ targets nudge a setting for each note individually: _Filter_ moves the layer's filter cutoff, _Timbre_ the master _Timbre_ control, and _LFO Amount_ the layer's LFO _Amount_. Press adds to the setting, while slide can move it either way, with its centre position leaving it unchanged.

The _MPE smoothing_ setting in the [Performance Controls](/docs/beta/usage/performance-controls) panel sets how gradually press and slide respond: lower is more responsive, higher is smoother.

While notes are sounding, each active voice gets a red line on whichever control press or slide is routed to — the filter cutoff knob, the LFO Amount knob, or the master Timbre knob in the top panel — so you can see the expression from each voice.

The MPE Configuration Message for zone setup is supported too, and a sustain pedal on a zone's master channel sustains the whole zone.

CLAP note expressions (per-note pitch, pressure, and brightness) work the same way in supporting hosts regardless of the MPE setting.
