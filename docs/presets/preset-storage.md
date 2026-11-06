# How Presets Are Stored

> Preset files, preset folders and preset banks

Each preset is a single small file ending in `.floe-preset`, kept in a folder on your computer. Because they're ordinary files, you can organise them with your usual file manager (Finder, File Explorer, etc.): rename them, move them into folders, or delete them. Floe watches its [preset folders](/docs/installation/folders) and updates the preset browser as soon as anything changes.

The file name is the preset's name, so renaming the file renames the preset. The author, description and tags are stored inside the file: to change them, save over the preset in Floe. See [Saving Presets](/docs/presets/saving-presets).

Presets are tiny, typically just a few kilobytes, so even large collections take up very little space.

## Sharing presets

You can simply share `.floe-preset` files. As long as the recipient has the same libraries installed, it works just the same. They can put it in one of their [preset folders](/docs/installation/folders) so it appears in the preset browser, or do a one-off load with the load-from-file button.

## Preset banks

A preset bank is a folder of presets, including all its subfolders, that belongs together, such as a set of factory presets. Preset banks are the top level of organisation in the preset browser.

Presets in folders that aren't part of any bank might be grouped together as _Miscellaneous_.

Floe also recognises some older factory preset banks that don't have an info file.
