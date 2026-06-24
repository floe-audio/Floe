# Package libraries & presets for distribution

> How to create Floe Packages to distribute your sample libraries and presets

The easiest and most reliable way to distribute your Floe content is with [Floe Packages](/docs/beta/installation/install-packages#what-is-a-package).

Floe Packages are `.floe-pkg` files (ZIP files internally) containing **sample libraries** (which provide instruments and IRs) and/or **presets** (ready-to-use professional sounds). Packages are what users download and install into Floe. Floe also accepts `.zip` file extension.

Floe offers an easy-to-use GUI for [installing packages](/docs/beta/installation/install-packages). This installation process carefully considers existing content, versions, installation preferences, and whether installed libraries have been modified. The result should 'just work' or provide clear instructions on what to do next.

As with Floe's sample library format, openness is key. That's why Floe Packages are just ZIP files with a specific structure and a `.floe-pkg` extension. Anyone can create them and anyone can open them (rename to `.zip` to extract). Additionally, it gives the user the option to extract them manually rather than use Floe's GUI if they wish.

Create Floe Packages using our command-line tool or any ZIP program.

## Encrypted packages

Floe also supports encrypted packages. Whilst not open, these are implemented in a spirit of Floe's hassle-free philosophy. A license key is needed to unlock the package. No internet connection is required.

## Packager command-line tool

If you're comfortable with such things, we have a command-line tool to create Floe Packages. It ensures everything is set up correctly and adds a couple of nice-to-have features, particularly for users who want to install the package manually rather than with Floe's GUI.

However, for regular packages, you can use any ZIP program to create them. Just make sure they follow the structure described in the next section, and rename the resulting `.zip` to `.floe-pkg`. Creating encrypted packages requires `floe-packager`.

#### Download

-   **Floe Packager Windows**: [Floe-Packager-v1.1.2-Windows.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-Windows.zip) (12 MB)
-   **Floe Packager macOS Apple Silicon**: [Floe-Packager-v1.1.2-macOS-Apple-Silicon.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-macOS-Apple-Silicon.zip) (1 MB)
-   **Floe Packager macOS Intel**: [Floe-Packager-v1.1.2-macOS-Intel.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-macOS-Intel.zip) (1 MB)
-   **Floe Packager Linux**: [Floe-Packager-v1.1.2-Linux.tar.gz](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-Linux.tar.gz) (12 MB)

Download the program, extract it, and run it from the command line.

#### Usage

Here's the output of `floe-packager --help`:

```
Packages libraries and presets into a Floe package file (.floe-pkg).Existing packages can be merged into the output. Multiple libraries and preset foldersare supported. Additionally:- Validates any Lua files.- Ensures libraries have a License file.- Adds an 'About' document for each library.- Adds an 'Installation' document for the package.- Embeds a checksum file into the package for better change detection if the package  is installed manually.Usage: floe-packager [OPTIONS]Optional arguments:  -l, --library-folder <path>...  Library folder to include (repeatable)  -p, --preset-folder <path>...   Preset folder to include (repeatable)  -i, --input-package <path>...   Existing package file to merge into the output. Files from --library-folder and --preset-folder take precedence on conflict. (repeatable)  -o, --output-dir <path>         Directory to write the package into. The filename is auto-generated.  -n, --package-name <name>       Override the auto-generated package filename. Any file extension is stripped.  -j, --info-json <path>          Write a JSON file describing the package's instruments, presets, tags, etc. Use '-' to write to stdout.  -e, --encrypt                   Encrypt the output package with a random content key. The key is printed to stdout; the output filename uses the .floe-pkg-enc extension.      --prune                     Silently drop files that aren't used: for libraries, files not referenced from Lua (samples, images, IRs) or the .lua/license files; for preset folders, files that aren't presets or preset-bank info files. Without this, such files are warned about but still included.
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
