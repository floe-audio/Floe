# Main Tab

> Shape each note's volume and tone over time with a layer's volume envelope and filter

The _MAIN_ tab shapes how each note's volume and tone change over time, using the volume envelope and the filter.

![A layer's MAIN tab with its parts numbered](https://assets.floe.audio/website/screenshots/layer-main.86776d3fc54ab4a0.png)1234

1.  **Volume envelope** — its switch and graph.
2.  **Filter** — its switch and graph.
3.  **Filter controls** — _Type_, _Cut_, _Res_ and _Env_.
4.  **Filter envelope** — shapes how the cutoff moves over time.

## Volume envelope

-   **On/off** — the volume envelope is typically best left on. If you turn it off every note plays its sample straight through to the end, however briefly you press the key: handy for one-shot sounds that should always be heard in full. Looping is disabled while the envelope is off. And since notes can't be cut short, voices can pile up and use a lot of CPU if you play quickly.
-   **Envelope graph** — drag its three points to shape each note's volume over time: the first sets the _Attack_, the second the _Decay_ and _Sustain_ together, and the third the _Release_.

## Filter

Each note gets its own filter, so the filter envelope and the LFO can sweep every note independently. It's a clean, digital-sounding state-variable filter (12 dB per octave). For broader tone shaping, see the [EQ tab](/docs/beta/usage/eq-tab).

-   **On/off** — switch the filter on or off.
-   **Filter graph** — shows the filter's response. Drag the point left and right to set the cutoff, and up and down for the resonance.
-   **Type** — choose the kind of filter, such as low-pass, high-pass or band-pass. Hover over the options in the menu for a description of each.
-   **Cut** — the cutoff frequency, where the filter takes effect. The filter envelope and LFO move the cutoff around this setting. If MPE press or slide is routed to the filter (see the [CONFIG tab](/docs/beta/usage/config-tab)), red marks show each voice's current cutoff.
-   **Res** — resonance, which emphasises the frequencies around the cutoff. It never self-oscillates. For the band-pass, notch and band-shelving types it also narrows the band, and for the peak type it sets the size of the boost.
-   **Env** — how strongly the filter envelope moves the cutoff on each note. At the centre it does nothing. Turn it up and the cutoff rises and falls with the envelope; turn it down and the cutoff moves the opposite way. With a low-pass filter, for example, a long attack opens the sound up gradually as each note begins.
-   **Filter envelope** — works like the volume envelope, shaping how the cutoff moves over time, scaled by _Env_.
