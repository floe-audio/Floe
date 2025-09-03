<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Package libraries & presets for distribution
The easiest and most reliable way to distribute your Floe content is with [Floe Packages](../packages/about-packages.md).

Floe Packages are ZIP files containing **sample libraries** (which provide instruments and IRs) and/or **presets** (ready-to-use professional sounds). Packages are what users download and install into Floe.

Floe offers an easy-to-use GUI for [installing packages](../packages/install-packages.md). This installation process carefully considers existing content, versions, installation preferences, and whether installed libraries have been modified. The result should 'just work' or provide clear instructions on what to do next.

As with Floe's sample library format, openness is key. That's why Floe Packages are just normal ZIP files with a specific structure. Anyone can create them and anyone can open them. Additionally, it gives the user the option to extract them manually rather than use Floe's GUI if they wish.

Create Floe Packages using our command-line tool or any ZIP program.

## Packager command-line tool
If you're comfortable with such things, we have a command-line tool to create Floe Packages. It ensures everything is set up correctly and adds a couple of nice-to-have features, particularly for users who want to install the package manually rather than with Floe's GUI.

However, you can use any ZIP program to create Floe Packages. Just make sure they follow the structure described in the next section.

#### Download

> **<i class="fa fa-windows"></i> Floe Packager Windows**:<br>==Floe-Packager-Windows-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Packager macOS Apple Silicon**:<br>==Floe-Packager-macOS-Apple-Silicon-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Packager macOS Intel**:<br>==Floe-Packager-macOS-Intel-markdown-link==

Download the program, extract it, and run it from the command line. 

#### Usage
Here's the output of `floe-packager --help`:
```
==packager-help==
```

#### Examples
These examples use bash syntax.
```bash
# Creates a Floe Package from the Slow library and the Slow Factory Presets.
# Slow and "Slow Factory Presets" are folders in the current directory.
./floe-packager --library-folders "Slow" \
                --preset-folders "Slow Factory Presets" \
                --output-folder .

# Creates a Floe Package containing multiple libraries and no presets
./floe-packager --library-folders "C:/Users/Sam/Floe-Dev/Strings" \
                                  "C:/Users/Sam/Floe-Dev/Common-IRs" \
                --output-folder "C:/Users/Sam/Floe-Dev/Releases" \
                --package-name "FrozenPlain - Strings"
```


## Package structure

If you're not using the packager tool, you need to know the structure of the Floe Package. It's very simple.

Requirements of a floe package:
- The ZIP must contain a folder called `Libraries` and/or a folder called `Presets`. If present, these folders must contain the libraries and presets respectively.

Be careful that your ZIP program is not adding an extra folder when you create the ZIP file. There should not be a top-level folder in the ZIP file, just the `Libraries` and/or `Presets` folders.

#### Example: single library & factory presets
```
📦FrozenPlain - Arctic Strings Package.zip/
├── 📁Libraries
│   └── 📁Arctic Strings
│       ├── 📄arctic-strings.floe.lua
│       ├── 📁Samples
│       │   ├── 📄strings_c4.flac
│       │   └── 📄strings_d4.flac
│       └── 📁Images
│           ├── 📄background.png
│           └── 📄icon.png
└── 📁Presets
    └── 📁Arctic Strings Factory
        ├── 📁Realistic
        │   ├── 📄Octaved.floe-preset
        │   └── 📄Soft.floe-preset
        └── 📁Synthetic
            ├── 📄Bright.floe-preset
            └── 📄Warm.floe-preset
```

#### Example: multiple libraries
```
📦Audioata - Synthwave Bundle Package.zip/
├── 📁Libraries
│   ├── 📁Synthwave Bass
│   │   ├── 📄synthwave-bass.floe.lua
│   │   └── 📁Samples
│   │       ├── 📄bass_c1.flac
│   │       └── 📄bass_d1.flac
│   ├── 📁Synthwave Drums
│   │   ├── 📄synthwave-drums.floe.lua
│   │   └── 📁Samples
│   │       ├── 📄kick.flac
│   │       └── 📄snare.flac
│   └── 📁Synthwave Synths
│       ├── 📄synthwave-synths.floe.lua
│       └── 📁Samples/
│           ├── 📄synth_c4.flac
│           └── 📄synth_d4.flac
└── 📁Presets
    └── 📁Synthwave Factory
        ├── 📄Clean Pad.floe-preset
        ├── 📄Dirty Lead.floe-preset
        ├── 📄Nebula Drone.floe-preset
        ├── 📄Punchy Kickdrum.floe-preset
        ├── 📄Backing FX.floe-preset
        └── 📄Full Effect.floe-preset
```
