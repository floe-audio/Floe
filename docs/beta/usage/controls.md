# Controls

> How to use Floe's knobs, sliders, menus and graphs

Almost everything you shape is done with knobs and sliders, and they all work the same way.

## Knobs and sliders

-   **Drag** — click and drag up or right to increase the value, down or left to decrease it. Use whichever direction feels natural.
-   **Fine adjustment** — hold `shift` while dragging for four times finer control. You can press or release `shift` mid-drag.
-   **Reset** — `ctrl`\-click (`cmd`\-click on macOS) to return the control to its default value.
-   **Type a value** — double-click to type an exact number. You can typically use abbreviations like `2k` instead of 2000 Hz.
-   **More options** — right-click for a menu of extra actions (see below).

When you hover over a control it lights up, and its current value appears below it. Pause on it for a moment and a short description appears too, with a reminder of these shortcuts underneath. Both are adjustable in the preferences: _Show tooltips_ turns the descriptions off, and turning off _Instant value readouts_ makes the value appear only after a moment or while you're dragging.

Sometimes controls are greyed out — showing it currently has no effect; such as a filter knob while the filter is switched off.

### Reading a knob

A knob shows its value in two places: the line on the dial, and the coloured arc around the edge.

These 2 aspects can differ when a [macro](/docs/beta/usage/macros) is controlling the knob. The dial shows the knob's own setting, the one you drag. The coloured arc shows the value you actually hear once the macros have been applied. Hover over the knob to see both readouts.

![The Filter effect's controls, with a macro moving its Cutoff knob](https://assets.floe.audio/website/screenshots/knob.ed9ee6bd141faee8.png)

Here, a macro is raising the Filter's _Cutoff_. The dial points to where the knob is set, while the arc reaches further round, to the cutoff you hear.

## The right-click menu

![The right-click menu of the Filter effect's Cutoff knob](https://assets.floe.audio/website/screenshots/knob-menu.2b32dcce31cfb5f2.png)

Right-click any knob or slider for extra actions, including:

-   **Copy Value** and **Paste Value** — carry a setting from one control to another.
-   **Enter Value** — type an exact value, the same as double-clicking.
-   **Reset Value to Default** — the same as `ctrl`/`cmd`\-click.
-   **Reset Value to "…" state** — appears when you've loaded a preset, and names it. It returns just this control to how the preset had it.
-   **MIDI CC Learn** — link the control to a knob or fader on your MIDI controller. See [MIDI](/docs/beta/usage/midi#midi-cc).

Some controls add their own shortcuts. A layer's _Pitch_, for example, has _+1 Octave_ and _\-1 Octave_, and _Detune_ can fold whole semitones into _Pitch_.

Many sections have a right-click menu of their own too. Right-click a layer's tab, an envelope or an effect to copy, paste or reset the whole section at once: a quick way to give two layers the same filter settings, for example.

## Menus, buttons and number fields

-   **Menus** — click to open the list of options. The arrows either side, and , step to the previous or next option, which is a quick way to audition them. Hover over an option in the open menu to see what it does.
-   **Buttons** — click to switch on or off. Most can be right-clicked for more options.
-   **Number fields**, such as a layer's _Pitch_ — drag them like a knob, double-click to type, or use the arrows beside them ( ).

Menus reset to their default with `ctrl`/`cmd`\-click, just like knobs.

## Graphs

Envelopes, filters and EQs have graphs with points you can drag directly. Dragging a point is simply another way of moving the knobs beneath it. Everything above still applies: `shift` for fine adjustment, `ctrl`/`cmd`\-click to reset, double-click to type.

Hover over a point to see exactly what it controls. On a filter point, for example, moving left and right changes the cutoff, and moving up and down changes the resonance. On some points, such as the EQ bands, the mouse wheel adjusts the resonance.

## Undo

Every change you make can be undone. Use the _Undo_ and _Redo_ buttons in the [top panel](/docs/beta/usage/top-panel#buttons) to step back and forth through your edits.
