<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Library Lua Scripts

The `floe.lua` file is the most important part of a Floe sample library. This page serves as documentation for all of the functions that you can use in your script to create and configure the library and its instruments.

Floe runs your script using Lua v==lua-version==, providing you with access to these [standard libraries](https://www.lua.org/manual/5.4/manual.html#6): `math`, `string`, `table` and `utf8`. The other standard libraries are not available - including the `require` function. This is to minimise security risks.

If there are any errors in your script, Floe will show them on the GUI along with a line number and a description of the problem.

## Multiple files
`dofile()` is available, so you can split your script into multiple files if you want to. Floe's `dofile` implementation is the same as the standard Lua version except that you can only specify files relative to the library folder — that is, the folder that contains the `floe.lua` file.

For example, you could have a folder next to `floe.lua` called `Lua` and put a file in there called `data.lua`. You could then load that file with `dofile("Lua/data.lua")`.

To pass data between files, you would typically use the ["module" pattern](http://lua-users.org/wiki/ModuleDefinition) (except using dofile instead of require).

## Library Functions
Use these functions to create your sample library. Take note of the `[required]` annotations - omitting fields marked with these will cause an error. 


### `floe.new_library`
Creates a new library. It takes one parameter: a table of configuration. It returns a new library object. You should only create one library in your script. Return the library at the end of your script.

The library is the top-level object. It contains all the instruments, regions, and impulse responses.

```lua
==sample-library-example-lua:new_library==
```


### `floe.new_instrument`
Creates a new instrument on the library. It takes 2 parameters: the library object and a table of configuration. It returns a new instrument object. You can call this function multiple times to create multiple instruments.

An instrument is like a musical instrument. It is a sound-producing entity that consists of one or more samples (samples are specified in regions). Each library can have multiple instruments.

You can use Floe's [Tag Builder GUI](./tags-and-folders.md#instrument-tags) to generate tag tables.

```lua
==sample-library-example-lua:new_instrument==
```



### `floe.add_region`
Adds a region to an instrument. It takes 2 parameters: the instrument object and a table of configuration. Doesn't return anything. You can call this function multiple times to create multiple regions. 

A region is a part of an instrument. It defines an audio file and the conditions under which it will be played. For example, you might have a region that plays the audio file `Piano_C3.flac` when the note C3 is played. Each instrument must have one or more regions.
```lua
==sample-library-example-lua:add_region==
```


### `floe.add_ir`
Adds an reverb impulse response to the library. It takes 2 parameters: the library object and a table of configuration. Doesn't return anything. You can call this function multiple times to create multiple impulse responses. 
```lua
==sample-library-example-lua:add_ir==
```

### `floe.set_attribution_requirement`
Sets the attribution information for a particular audio file or folder. It takes 2 parameters: a path to the file or folder whose license information you want to set, and a table of configuration. If the path is a folder, the attribution requirement will be applied to all audio files in that folder and its subfolders.
```lua
==sample-library-example-lua:set_attribution_requirement==
```

### `floe.set_required_floe_version`
Sets the minimum required version of Floe for this library. It takes one parameter: a string representing the version number (a [semantic version](https://semver.org/)).

It's best to set this at the top of your `floe.lua` file so that Floe can check the version before running the script.

Calling this function is recommended so that older versions of Floe behave predictably when trying to load an unsupported library.
```lua
==sample-library-example-lua:set_required_floe_version==
```

## Support Function
Floe provides some additional functions to make developing libraries easier.


### `floe.extend_table`
Extends a table with another table, including all sub-tables. It takes 2 parameters: the base table and the table to extend it with. The base table is not modified. The extension table is modified and returned. It has all the keys of the base table plus all the keys of the extended table. If a key exists in both tables, the value from the extension table is used.

Floe doesn't have the concept of 'groups' like other formats like SFZ or Kontakt have. Instead, this function offers a way to apply a similar configuration to multiple regions. Alternatively, you can use functions and loops in Lua to add regions in a more dynamic way.

```lua
==sample-library-example-lua:extend_table==
```

## Lua Language Server
If you are using the [Lua Language Server](https://luals.github.io/), you can get autocompletion and diagnostics for Floe's Lua API by using the following configuration. 

We plan to make this configuration more user-friendly, but for now you can use the following steps to set it up.

#### Step 1
Create a file called `floe-api.lua` in the same folder as your `floe.lua` file. Paste the following code into it:
```lua
==floe-lua-lsp-defs==
```

#### Step 2
Create a `.luarc.json` file next to your `floe.lua` file. Paste the following code into it:
```json
{
    "workspace": {
        "library": [
            "floe-api.lua"
        ]
    },
    "diagnostics": {
        "globals": [
            "floe"
        ]
    }
}
```
