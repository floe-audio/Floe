# Performance Controls

> The Performance Controls panel — MIDI CC mappings, MPE, Reproducibility, and Profiles

The **Performance Controls** panel (sliders icon, top panel) is where you manage everything about how Floe responds to a live performance: [MIDI CC mappings](/docs/usage/midi#midi-cc), [MPE](/docs/usage/midi#mpe), and [Reproducibility](/docs/usage/reproducibility) settings.

![Performance Controls panel](/images/screenshots/performance-controls.png)

## Scope

Everything in this panel belongs to the current instance rather than being a global preference. It's saved with your DAW project. Loading a preset leaves it untouched, meaning you can audition presets without disturbing your performance setup. Different instances in the same project can each have their own settings.

## Settings for new instances

Each Floe instance has thier own performance controls, but you can configure what new instances of Floe start with - their MIDI CC mappings, MPE, and Reproducibility settings. Simply edit the current Performance Controls to your preference, then click **Make default**.

## Profiles

A profile is a named, saved bundle of the panel's settings — like a save file for this panel. Profiles are useful when you switch between MIDI controllers regularly: save one profile per controller, then switch between them as you swap hardware.

Open the **Profiles** menu at the top of the panel to:

-   Load a profile, applying it to the current instance immediately. This is a one-shot copy, not a persistent link — further changes to the instance don't affect the saved profile unless you save over it.
-   Save the current settings as a new profile.
-   Overwrite, rename, or delete an existing profile.

## Notes

-   Prior to v2.0.3, these options were spread across the Instance Config panel, and MIDI CC Assignments panel.
-   Floe defaults with some common MIDI CC assignments, such as the mod wheel (CC1) to the first macro — [more info](/docs/usage/midi).
