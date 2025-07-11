<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

<div class="warning">
The tags and folder features of Floe are under development. Some of the features on this page aren't implemented yet.
</div>

# Tags and folders

Floe has 2 complimentary features for browsing, searching and filtering: tags and folders. Presets, instruments, and impulse responses all use tags and folders.

For developers, there are best practises for how to use tags and folders to offer the best experience for the user.

On this page, we use the term 'item' to mean either preset, instrument, or impulse response: these all use tags and folders in a similar way.

## Folders
Folders are used to group items under a common heading.

Folders should be categories that relate to the theme or objective of the library. For example, for Lost Reveries, we wanted to explore the technique of using three complimentary timbres to layer into a full-spectrum sound, so we choose folders: "Low", "Mid" and "Air". Another example might be a library of field recordings, folders might be "Urban/London", "Urban/York" or "Nature/New Forest".

Folders may contain slashes to represent a hierarchy, just like a filepath. For example: "Piano/1978 Steinway". Use this to specify broad categories that narrow down to specific categories. It's not usually best to have more than 2 levels of hierarchy.

It's best to have 5 to 30 items per folder. Folders with only a couple of items clutter the GUI and don't offer much value. Folders with too many items loose their focus.

Folders might overlap with tags but, in general, they are more abstract and can use phrases that only make sense in the context of the library as a whole. However, if it makes sense to group items under a common heading such as 'Percussion' (a common tag), then that's fine too — but you should also add this as a tag.  

## Tags
Tags are the properties of an item. They are used for filtering and searching items across all libraries. Therefore, tags should normally come from the standard pool of tags so that there is a common language that enables standard usage across all libraries. Custom tags are allowed, but they should only be used for a good reason.

Add as many relevant tags as possible.

Tags are case-insensitive.

## Instrument tags
Instrument tags are set in the sample library's Lua file when calling `floe.new_instrument`. However, writing tags manually is quite laborious.

Floe has a utility that allows you to use the GUI to select tags. It will write these tags as Lua code, ready to be included in your `floe.lua` file.

`Lua/instrument_tags.lua`:
```lua
-- This file is generated by Floe's tag builder.
return {
  ["Air - Restless Canopy"] = { "ambient", "bittersweet", "breathy", "dreamy", "ethereal", "nostalgic", "resonant", "smooth", "synthesized", "texture", },
  -- ...
}
```

`floe.lua`:
```lua
local instrument_tags = dofile("Lua/instrument_tags.lua")

-- ...

local instrument_name = "Air - Restless Canopy"
local instrument = floe.new_instrument(library, {
    name = instrument_name,
    tags = instrument_tags[instrument_name],
    -- ...
})

```

This Tag Builder is found by clicking on the 3-dots menu at the top of Floe, and then selecting "Library Developer Panel".

The tag builder works for whatever instrument is loaded in the first layer of Floe. As you click on tags, the generated Lua code will be placed in the library's folder in a subfolder called `Lua`.

## Standard tags
We take a pragmatic approach to defining this set of standard tags. Rather than try to be completely comprehensive and technically accurate, we instead aim to strike a balance between correctness and common usage within the music production space. The goal of tags is to help users find the item they need. You can suggest edits to this list via Github or other means.

For best results setting tags, go over each of the following questions and add all tags that apply to the item.

==tags-listing==
