# Legacy Parameters

> How Floe ensures backwards compatibility

Floe strives for perfect backwards compatibility: a preset or DAW project saved with any version of Floe will load up sounding exactly the same in any later version. To make this possible without dragging an ever-growing pile of outdated UI into the main interface, Floe **never deletes a parameter**. When a parameter needs to change, we add a new parameter to the main UI and move the old one out of sight as a "legacy" parameter that quietly keeps doing its old job.

## When does a parameter become legacy?

A parameter becomes legacy when we change it in a way that would otherwise alter how an existing project sounds. Common reasons:

-   **Adding new options to a menu** — for example, new LFO shapes, new filter types, or new modes for monophonic voice behaviour. Automation written against the old menu would land on different values in a wider menu, so we add a new menu parameter and keep the old one for compatibility.
-   **Improving a parameter's feel** — adjusting how the knob's position maps to the audible value so the response is better spread across the range, or has a more intuitive projection at low or high values.
-   **Combining or simplifying** several controls into one, where the new control isn't a 1-to-1 substitute for any single old one.

In every case the old parameter stays alive behind the scenes so your projects sound the same, and a new modern parameter appears on the main UI.

## Presets vs. DAW projects

When you load a **preset**, Floe automatically modernises any legacy values it finds: the audibly-equivalent value is copied onto the modern parameter and the legacy slot is cleared. You'll see no legacy parameters and the modern UI controls work as expected.

When you load a **DAW project**, Floe can't modernise automatically: it has no way of knowing whether your DAW is automating a legacy parameter, and silently changing the target would break your automation. Instead, Floe keeps the legacy value intact, lets the legacy parameter override its modern equivalent, and shows a small yellow warning badge on the affected control. The Legacy Parameters panel (accessible from the 3-dots menu) lists every legacy parameter that is currently overriding a modern control.

## What the modern control looks like while overridden

While a legacy override is in effect, the modern control on the main UI is **greyed out and not interactive** — there's no point dragging it because the legacy value is what reaches the audio engine. The yellow warning badge in its corner is clickable; it opens the Legacy Parameters panel directly.

The control's knob position and any associated visualisers (filter response curves, EQ displays, envelope shapes, etc.) reflect the _modern_ parameter's underlying value, not the legacy value driving the audio. **The sound you hear is correct** — it's the legacy value being applied — but the on-screen position and graphics show the modern parameter underneath. Once the override is cleared, the display, interactivity and audio all match up again.

## Modernising a legacy parameter

"Modernising" hands control over from the legacy parameter to its modern replacement, copying across an audibly-equivalent value so the sound doesn't change. The Legacy Parameters panel offers two ways to do this:

-   **Modernise all** — modernises every overriding parameter at once.
-   **Modernise (per row)** — modernises a single parameter.

Your project will sound exactly as it did before, and from then on the modern control on the main UI is live again.

If you have any DAW automation targeting one of these parameters, **don't modernise it without first removing or re-creating that automation**: Floe decides whether a legacy parameter is "overriding" purely by checking whether its value is at the default, so live automation will re-engage the override the moment it writes a non-default value, and the modern control will go unresponsive again.

The recommended order:

1.  **Check your DAW** for any automation lanes targeting the legacy parameter.
2.  If there is automation, **delete it in your DAW** (and, if you still want that automation, recreate it on the modern parameter).
3.  Open the Legacy Parameters panel and click **Modernise** (per row) or **Modernise all**.

If you have no DAW automation on a legacy parameter, you can skip step 2 — modernising is then completely safe.
