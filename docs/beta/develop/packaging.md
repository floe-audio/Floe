# Package libraries & presets for distribution

> How to create Floe Packages to distribute your sample libraries and presets

The easiest and most reliable way to distribute your Floe content is with [Floe Packages](/docs/beta/installation/install-packages#what-is-a-package).

Floe Packages are `.floe-pkg` files (ZIP files internally) containing **sample libraries** (which provide instruments and IRs) and/or **presets** (ready-to-use professional sounds). Packages are what users download and install into Floe. Floe also accepts `.zip` file extension.

Floe offers an easy-to-use GUI for [installing packages](/docs/beta/installation/install-packages). This installation process carefully considers existing content, versions, installation preferences, and whether installed libraries have been modified. The result should 'just work' or provide clear instructions on what to do next.

As with Floe's sample library format, openness is key. That's why Floe Packages are just ZIP files with a specific structure and a `.floe-pkg` extension. Anyone can create them and anyone can open them (rename to `.zip` to extract). Additionally, it gives the user the option to extract them manually rather than use Floe's GUI if they wish.

Create Floe Packages using our command-line tool or any ZIP program. We often recommend not leaving packages with the `.zip` extension though because the common thing to do with ZIPs is extract them, which is not what Floe's 'Install Package' button wants, leading to confusion. Instead, always change the file extension to `.floe-pkg`.

## Encrypted packages

Floe also supports encrypted packages. Whilst not open, these are implemented in a spirit of Floe's hassle-free philosophy. A license key is needed to unlock the package. No internet connection is required.

## Packager command-line tool

If you're comfortable with such things, we have a command-line tool to create Floe Packages. It ensures everything is set up correctly and adds a couple of nice-to-have features, particularly for users who want to install the package manually rather than with Floe's GUI.

However, for regular packages, you can use any ZIP program to create them. Just make sure they follow the structure described in the next section, and rename the resulting `.zip` to `.floe-pkg`. Creating encrypted packages requires `floe-packager` and is not yet documented.

#### Download

-   **Floe Packager Windows**: [Floe-Packager-v2.0.2-Windows.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.2/Floe-Packager-v2.0.2-Windows.zip) (13 MB)
-   **Floe Packager macOS Apple Silicon**: [Floe-Packager-v2.0.2-macOS-Apple-Silicon.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.2/Floe-Packager-v2.0.2-macOS-Apple-Silicon.zip) (1 MB)
-   **Floe Packager macOS Intel**: [Floe-Packager-v2.0.2-macOS-Intel.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.2/Floe-Packager-v2.0.2-macOS-Intel.zip) (1 MB)
-   **Floe Packager Linux**: [Floe-Packager-v2.0.2-Linux.tar.gz](https://github.com/floe-audio/Floe/releases/download/v2.0.2/Floe-Packager-v2.0.2-Linux.tar.gz) (14 MB)

Download the program, extract it, and run it from the command line.

#### Usage

Here's the output of `floe-packager --help`:

```
Packages libraries and presets into a Floe package file (.floe-pkg or .floe-pkg-enc).Existing packages can be merged into the output. Multiple libraries and preset foldersare supported. Additionally:- Validates any Lua files.- Ensures libraries have a License file.- Adds an 'About' document for each library.- Adds an 'Installation' document for the package.- Embeds a checksum file into the package for better change detection if the package  is installed manually.Encrypted workflow: run gen-key once per product line and store the key securely (orgenerate the key by some other means). Pass it to pack via --package-key for every versionso that a single license key unlocks all of them.Usage: floe-packager <command> [OPTIONS]Commands:  pack     Build a Floe package (.floe-pkg) from libraries and presets. Existing packages can be merged into the output.  info     Write a JSON manifest describing the package's instruments, presets, tags, etc., without producing a package file.  gen-key  Generate a random package key and print it to stdout.Run 'floe-packager <command> --help' for more information on a command.Build a Floe package (.floe-pkg) from libraries and presets. Existing packages can be merged into the output.Usage: floe-packager pack <output-dir> [OPTIONS]Positional arguments:  <output-dir>  Directory to write the package into. Created if missing. The package filename is auto-generated.Optional arguments:  -l, --library-folder <path>...  Library folder to include (repeatable)  -p, --preset-folder <path>...   Preset folder to include (repeatable)  -i, --input-package <path>...   Existing package file to merge into the output. Files from --library-folder and --preset-folder take precedence on conflict. (repeatable)      --prune                     Silently drop files that aren't used: for libraries, files not referenced from Lua (samples, images, IRs) or the .lua/license files; for preset folders, files that aren't presets or preset-bank info files. Without this, such files are warned about but still included.  -n, --package-name <name>       Override the auto-generated package name.      --package-key <hex>         64-character hex key for encrypting the package (gen-key can create one). When present, produces an encrypted .floe-pkg-enc instead of a plain .floe-pkg. Reuse the same key across versions so existing license keys stay valid.Write a JSON manifest describing the package's instruments, presets, tags, etc., without producing a package file.Usage: floe-packager info <output-json> [OPTIONS]Positional arguments:  <output-json>  JSON file path to write the manifest to. Use '-' to write to stdout.Optional arguments:  -l, --library-folder <path>...  Library folder to include (repeatable)  -p, --preset-folder <path>...   Preset folder to include (repeatable)  -i, --input-package <path>...   Existing package file to merge into the output. Files from --library-folder and --preset-folder take precedence on conflict. (repeatable)      --prune                     Silently drop files that aren't used: for libraries, files not referenced from Lua (samples, images, IRs) or the .lua/license files; for preset folders, files that aren't presets or preset-bank info files. Without this, such files are warned about but still included.  -n, --package-name <name>       Override the auto-generated package name.Generate a random package key and print it to stdout.Usage: floe-packager gen-key [OPTIONS]
```

#### Examples

These examples use bash syntax.

```
# Creates a .floe-pkg from the Slow library and the Slow Factory Presets.# Slow and "Slow Factory Presets" are folders in the current directory../floe-packager --library-folder "Slow" \                --preset-folder "Slow Factory Presets" \                --output-dir .# Creates a .floe-pkg containing multiple libraries and no presets./floe-packager --library-folder "C:/Users/Sam/Floe-Dev/Strings" \                                 "C:/Users/Sam/Floe-Dev/Common-IRs" \                --output-dir "C:/Users/Sam/Floe-Dev/Releases" \                --package-name "FrozenPlain - Strings"
```

## Package structure

If you're not using the packager tool, you need to know the structure of the Floe Package. It's very simple.

Requirements of a floe package:

-   The package must contain a folder called `Libraries` and/or a folder called `Presets`. If present, these folders must contain the libraries and presets respectively.

Be careful that your ZIP program is not adding an extra folder when you create the ZIP file. There should not be a top-level folder in the ZIP file, just the `Libraries` and/or `Presets` folders.

#### Example: single library & factory presets

```
📦FrozenPlain - Arctic Strings Package.floe-pkg/├── 📁Libraries│   └── 📁Arctic Strings│       ├── 📄arctic-strings.floe.lua│       ├── 📁Samples│       │   ├── 📄strings_c4.flac│       │   └── 📄strings_d4.flac│       └── 📁Images│           ├── 📄background.png│           └── 📄icon.png└── 📁Presets    └── 📁Arctic Strings Factory        ├── 📁Realistic        │   ├── 📄Octaved.floe-preset        │   └── 📄Soft.floe-preset        └── 📁Synthetic            ├── 📄Bright.floe-preset            └── 📄Warm.floe-preset
```

#### Example: multiple libraries

```
📦Audioata - Synthwave Bundle Package.floe-pkg/├── 📁Libraries│   ├── 📁Synthwave Bass│   │   ├── 📄synthwave-bass.floe.lua│   │   └── 📁Samples│   │       ├── 📄bass_c1.flac│   │       └── 📄bass_d1.flac│   ├── 📁Synthwave Drums│   │   ├── 📄synthwave-drums.floe.lua│   │   └── 📁Samples│   │       ├── 📄kick.flac│   │       └── 📄snare.flac│   └── 📁Synthwave Synths│       ├── 📄synthwave-synths.floe.lua│       └── 📁Samples/│           ├── 📄synth_c4.flac│           └── 📄synth_d4.flac└── 📁Presets    └── 📁Synthwave Factory        ├── 📄Clean Pad.floe-preset        ├── 📄Dirty Lead.floe-preset        ├── 📄Nebula Drone.floe-preset        ├── 📄Punchy Kickdrum.floe-preset        ├── 📄Backing FX.floe-preset        └── 📄Full Effect.floe-preset
```
