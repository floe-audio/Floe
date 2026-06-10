# Package libraries & presets for distribution

> How to create Floe Packages to distribute your sample libraries and presets

The easiest and most reliable way to distribute your Floe content is with [Floe Packages](/docs/beta/installation/install-packages#what-is-a-package).

Floe Packages are `.floe-pkg` files (ZIP files internally) containing **sample libraries** (which provide instruments and IRs) and/or **presets** (ready-to-use professional sounds). Packages are what users download and install into Floe. Floe also accepts `.zip` packages for backwards compatibility. Packages can also be encrypted (`.floe-pkg-enc`) to require a license key for installation.

Floe offers an easy-to-use GUI for [installing packages](/docs/beta/installation/install-packages). This installation process carefully considers existing content, versions, installation preferences, and whether installed libraries have been modified. The result should 'just work' or provide clear instructions on what to do next.

As with Floe's sample library format, openness is key. That's why Floe Packages are just ZIP files with a specific structure and a `.floe-pkg` extension. Anyone can create them and anyone can open them (rename to `.zip` to extract). Additionally, it gives the user the option to extract them manually rather than use Floe's GUI if they wish. If you need to protect your content, you can create encrypted packages instead.

Create Floe Packages using our command-line tool or any ZIP program.

## Packager command-line tool

If you're comfortable with such things, we have a command-line tool to create Floe Packages. It ensures everything is set up correctly and adds a couple of nice-to-have features, particularly for users who want to install the package manually rather than with Floe's GUI.

However, you can use any ZIP program to create Floe Packages. Just make sure they follow the structure described in the next section, and rename the resulting `.zip` to `.floe-pkg`.

#### Download

-   **Floe Packager Windows**: [Floe-Packager-v1.1.2-Windows.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-Windows.zip) (12 MB)
-   **Floe Packager macOS Apple Silicon**: [Floe-Packager-v1.1.2-macOS-Apple-Silicon.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-macOS-Apple-Silicon.zip) (1 MB)
-   **Floe Packager macOS Intel**: [Floe-Packager-v1.1.2-macOS-Intel.zip](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-macOS-Intel.zip) (1 MB)
-   **Floe Packager Linux**: [Floe-Packager-v1.1.2-Linux.tar.gz](https://github.com/floe-audio/Floe/releases/download/v1.1.2/Floe-Packager-v1.1.2-Linux.tar.gz) (12 MB)

Download the program, extract it, and run it from the command line.

#### Usage

Here's the output of `floe-packager --help`:

```
Takes libraries and presets and turns them into a Floe package file (.floe-pkg).Also accepts existing packages to merge into the output package.You can specify multiple libraries and preset-folders. Additionally:- Validates any Lua files.- Ensures libraries have a License file.- Adds an 'About' document for each library.- Adds a 'Installation' document for the package.- Embeds a checksum file into the package for better change detection if the package  is installed manually.Usage: floe-packager [OPTIONS]Optional arguments:  --library-folders <path>...  One or more library folders  --presets-folders <path>...  One or more presets folders  --input-packages <path>...   One or more input package files to include in the output package  --output-folder <path>       Folder to write the created package to  --package-name <name>        Package name - inferred from library name if not provided  --output-info-json <path>    If set, writes a JSON file with comprehensive package information: instruments, presets, tags, etc.  --encrypt                    Encrypt the output package. A random content key is generated and printed to stdout. Output will be .floe-pkg-enc instead of .floe-pkg.
```

#### Examples

These examples use bash syntax.

```
# Creates a .floe-pkg from the Slow library and the Slow Factory Presets.# Slow and "Slow Factory Presets" are folders in the current directory../floe-packager --library-folders "Slow" \                --preset-folders "Slow Factory Presets" \                --output-folder .# Creates a .floe-pkg containing multiple libraries and no presets./floe-packager --library-folders "C:/Users/Sam/Floe-Dev/Strings" \                                  "C:/Users/Sam/Floe-Dev/Common-IRs" \                --output-folder "C:/Users/Sam/Floe-Dev/Releases" \                --package-name "FrozenPlain - Strings"
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

## Encrypted packages

The packager tool also supports creating encrypted packages (`.floe-pkg-enc`) that require a license key to install. This is aimed at commercial developers. Documentation for this feature is not yet available.
