<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Presets

Floe can save its current state as a preset file, which can be loaded later. It's the same file type regardless of what sample libraries you're using (`.floe-preset` file). 

When you load a preset, Floe will check that you have the required libraries installed, and if not, it will show an error.

Presets are portable - you can copy them to other computers or operating systems. Using your file explorer (Finder, File Explorer, etc.), you can rearrange presets into folders and rename them as you like. Floe automatically detects any changes in your preset folders updates its listings accordingly.

## Preset browser

![Preset Browser GUI](../images/preset-browser.png)

Floe features a browser for conveniently navigating and loading presets from your preset folders. Filter presets using the left-hand panel, and select presets from the right-hand panel. You can also search for presets using the search bar at the top.

## Preset folders

![Folder Preferences GUI](../images/folder-preferences.png)

Floe automatically scans for presets in a set of folders - including subfolders. This works in the exact same way as your [library folders](./sample-libraries.md).

Presets are tiny files and so there's typically no need to move then to an external hard-drive.

