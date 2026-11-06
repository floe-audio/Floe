# Performance Controls

> The Performance Controls panel — MIDI CC mappings, MPE, Reproducibility, and Profiles

The _Performance Controls_ panel is where you set up how Floe responds to your playing and your controllers. Open it with in the top panel, or from the main menu.

![The Performance Controls panel with its parts numbered](https://assets.floe.audio/website/screenshots/performance-controls.2063ddce6cec1071.png)123

1.  **Profiles menu** — load and save profiles. It reads _Profiles_ until you load one, and then shows that profile's name.
2.  **Settings for new instances** — make this panel's settings the ones new instances start with.
3.  **Tabs**:
    -   **MIDI CC** — every [MIDI CC mapping](/docs/beta/usage/midi#midi-cc) on this instance. Remove one with its button, or click _Add_ to create one by choosing a CC number and a parameter, as an alternative to right-clicking a knob and using _MIDI CC Learn_.
    -   **Reproducibility** — the _Seed_ and reset settings that make Floe's random elements play back the same every time. See [Reproducibility](/docs/beta/usage/reproducibility).
    -   **MPE** — switch on [MPE](/docs/beta/usage/midi#mpe) for expressive controllers, and set how smoothly per-note expression responds.

## Scope

Everything in this panel belongs to the current instance rather than being a global preference. When you save your DAW project, these settings are saved with it, so the next time you open the project you can perform it the same way. Different instances in the same project can each have their own settings. However, loading a _preset_ leaves it untouched, meaning you can audition presets without disturbing your performance setup.

## Settings for new instances

Each Floe instance has its own performance controls, but you can choose what new instances start with: their MIDI CC mappings, MPE, and Reproducibility settings. Set the panel up the way you like, then click _Make default_ (2), just below the panel's title. The reset button beside it does the reverse, returning this instance to your defaults. Both only appear when the current settings differ from your defaults; otherwise a _Default for new instances_ label shows in their place.

For example, say your keyboard has a fader that you always want on the second macro. Assign it once with _MIDI CC Learn_, click _Make default_, and every new instance you add from then on starts with that fader ready to go. Instances already in your projects keep their own settings.

Floe's own defaults include some common MIDI CC mappings, such as the mod wheel (CC1) to the first macro. See [MIDI](/docs/beta/usage/midi#default-mappings) for the full list.

## Profiles

A profile is a named, saved bundle of the panel's settings, like a save file for this Performance Controls. Profiles are useful when you switch between MIDI controllers regularly: save one profile per controller, then switch between them as you swap hardware.

Open the _Profiles_ menu (1) at the top-right of the panel to:

-   Load a profile, applying it to the current instance immediately. This is a one-shot copy, not a persistent link: further changes to the instance don't affect the saved profile unless you save over it.
-   Save the current settings as a new profile.
-   Overwrite, rename, or delete an existing profile.

Once a profile is loaded, the menu shows its name, with an asterisk when you've changed the settings since.
