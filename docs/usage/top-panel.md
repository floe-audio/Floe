# Top Panel and Main Menu

> The top panel's preset box, buttons, master controls and meters, and everything in the main menu

The top panel runs along the top of Floe's window and stays there on every page. It holds the things that apply to the whole sound rather than to one page: the current preset, undo and redo, the master controls and the output meters, along with the buttons that open Floe's other windows.

![Floe's top panel with its parts numbered](https://assets.floe.audio/website/screenshots/top-panel-overview.6c17c53c76feceb5.png)12345

1.  **Logo and version** — the version of Floe you're running. If you turn on _Show instance name_ on the _General_ tab of Preferences, this instance's [name](/docs/support/autosave#instance-names) appears after it.
2.  **Preset box** — the current preset's name and description, with buttons to step through presets, save, and load a preset from a file. Click the name to open the preset browser. See [The preset box](/docs/presets/loading-presets#the-preset-box).
3.  **Buttons** — Preferences, Performance Controls, undo and redo, and the main menu.
4.  **Timbre and Volume** — the master controls, which act on everything you hear.
5.  **Meters** — the level of the final output.

## Buttons

-   **Preferences** — settings for Floe itself rather than for your sound, such as how the interface looks and where your sounds and presets are installed. They're saved on your computer and apply to every instance of Floe.
-   **Performance Controls** — how you play Floe: mostly MIDI settings, plus options for making performances exactly reproducible. They're saved with this instance in your DAW project, and loading a preset never changes them. See [Performance Controls](/docs/usage/performance-controls).
-   **Undo** — step back through your changes to the sound, including tweaks to controls and loading Instruments or effects, so you can experiment freely. Hover over the button to see which change it will undo.
-   **Redo** — bring back a change you've just undone. If you make a new change instead, there's nothing left to redo.

-   **Attribution** — appears in red when a sound you've loaded comes from a library that asks to be credited. Click it for the exact text to use. See [Attribution](/docs/reference/attribution).

-   **Main menu** — more options, and Floe's other windows. A red dot on the button means a new version of Floe is available: see [Updating](/docs/installation/download-and-install-floe#updating).

## Master controls

-   **Volume** — the final stage of the sound. It comes after the [effects rack](/docs/usage/effects), so it sets the overall level of everything just before the audio leaves Floe.

-   **Timbre** — some Instruments are built with crossfade layers: variations of the same sound, such as soft to hard or dark to bright. _Timbre_ sweeps between them, so you can shape the tone. While you drag it, the Instruments that respond are highlighted. When none of the loaded Instruments have crossfade layers, the knob is greyed out. An MPE controller can move it for each note separately: see [MIDI](/docs/usage/midi#mpe).

## Meters

The peak meter (5) shows the level of the final output, measured after the master _Volume_. It flashes red if the signal clips above 0 dB.

There's also an optional loudness (LUFS) meter, mostly useful if you're making presets and want them to sit at a consistent level. Turn on _Show Loudness (LUFS) meter_ on the _General_ tab of Preferences, and hover over it for details.

## The main menu

Click , the last of the buttons (3), to open the main menu.

![Floe's main menu, open beneath its button in the top panel](https://assets.floe.audio/website/screenshots/main-menu.f549d251ee1a692d.png)

-   **Load Blank Preset** — return every setting to its default and clear all the Instruments and IRs, ready to build a sound from scratch. Remember, if you need it, _Undo_ brings back what you had.
-   **Panic (Stop All Sound)** — stop all audio and clear every playing note. Handy if a note gets stuck.
-   **Rescan Libraries & Presets** — Floe notices new and changed files in its folders by itself, but if something you've added doesn't show up, this makes it look through every folder again.
-   **Preferences** and **Performance Controls** — the same as the buttons in the top panel.
-   **Info** — the _Libraries_ tab shows what's installed and is where you can [uninstall](/docs/installation/uninstalling) it. The other tabs show your version of Floe and whether an update is available, the legal notices, and some metrics, such as how many voices are playing and how much memory the samples use.
-   **Share Feedback** — send a message to the developers, such as a bug report or an idea. See [Share feedback](/docs/support/error-reporting#share-feedback).
-   **Legacy Parameters** — controls kept from older versions of Floe so that projects made with them still sound the same. You only need this window if a control shows a yellow badge. See [Legacy Parameters](/docs/reference/legacy-parameters).
-   **Library Developer Tools** — tools for people making their own sample libraries for Floe. See [Library Development](/docs/develop/develop-libraries).

-   **Add Mirage Folders** — add the library and preset folders of Mirage, Floe's predecessor, if you had it installed. Not shown on Linux.

-   **Help** — open Floe's documentation website.
