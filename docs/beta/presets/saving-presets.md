# Saving Presets

> How to save your own Floe presets with a description and tags

You can save your own Floe presets ready to load into another project or another instance of Floe. It's worth doing whenever you've found something you'd hate to lose.

Click the save button in the [preset box](/docs/beta/presets/loading-presets#the-preset-box) to open the Save Preset panel. All of its fields are optional: you can click _Save_ straight away and just choose a name. Filling them in helps you find the sound again later, especially once you've saved lots of presets.

![Floe's Save Preset panel with the tags outlined](https://assets.floe.audio/website/screenshots/save-preset.feb3ad1cf1db22ab.png)

-   **Author** — who made the preset. The buttons beside it store your name, so you don't have to type it out every time: stores the current name, and fills in the stored name.
-   **Description** — a few words about the sound.
-   **Tags** — the outlined section, describing the sound so you can find it again with the preset browser's filters.

## Naming and saving

You name a preset by naming its file. When you click _Save_ or _Save As New_, your system's file dialog opens, and the file name you choose there becomes the preset's name. To rename a preset later, rename its file in your file manager (Finder, File Explorer, etc.). The files are portable. Copy/move them as you like.

The buttons at the bottom of the panel depend on where the current sound came from. _Cancel_ closes the panel without saving, and the others are:

-   **Save** — choose a file name and folder for a new preset. This is the only option after _Load Blank Preset_.
-   **Overwrite** — save over the preset file you loaded, keeping its name. It doesn't ask for confirmation, and _Undo_ won't bring back the old file, so only use it when you're sure.
-   **Save As New** — save a new preset, leaving the one you loaded untouched.

The first time you save, the file dialog opens in a `User` folder inside Floe's preset folder; after that it remembers where you last saved. Save inside one of Floe's [preset folders](/docs/beta/installation/folders) and the preset appears in the preset browser straight away.

The button at the top of the panel keeps it open, so you can step through presets and edit each one's author, description and tags as you go.

## Writing a description

When you load a preset, its description appears in two places on Floe's UI. As much as fits goes beneath the preset name in the top panel, and the rest continues on the _Perform_ page. To choose where it splits add a newline when you write the description: everything before the first line break goes in the top panel. If that line is too long to fit, Floe splits it where it runs out of room instead.

You can leave the description blank. When Floe loads a preset with no description, it generates one on the fly from what's in the preset and displays that instead, so even quick saves get something helpful.

## Choosing tags

Tags are grouped into categories such as sound source, sound type, mood and timbre. Choose the ones that describe the sound: they're what the preset browser's tag filters use.

Some tags turn grey once you've chosen a related one. That's a hint that the combination is unusual, such as a real instrument category on a synthesized sound, but you can still click them if they fit.
