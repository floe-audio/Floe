<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

<a href="https://floe.audio">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/floe-audio/Floe-Logos/HEAD/horizontal_transparent.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/floe-audio/Floe-Logos/HEAD/horizontal_transparent_dark.svg">
    <img alt="Floe" src="https://raw.githubusercontent.com/floe-audio/Floe-Logos/HEAD/horizontal_background.svg" width="250" height="auto" style="max-width: 100%;">
  </picture>
</a>

---

### Streamlined sample-based instrument platform
This is the source code for Floe, a CLAP, VST3 and AU plugin for Windows, macOS, and Linux. It loads and plays sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project.

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
### Quick start
- Download and install [Zig 0.14.0](https://ziglang.org/download/)
- Clone this repository `git clone https://github.com/floe-audio/Floe.git`
- Run `zig build install` in the root of the project
- Binaries are created in the `zig-out` directory

### Build options
Run `zig build --help` for every step and option. The main ones:
- **Release build:** `-Dbuild-mode=production` (default is `development`; there's also `performance_profiling`).
- **Cross-compile:** `-Dtargets=windows,mac_arm` (comma-separated; also `linux`, `mac_x86`). Linux binaries can only be built on Linux.
- **Install to plugin folders:** `--prefix <dir>` puts plugins in the standard subfolders. Use `~` on Linux, `~/Library/Audio/Plug-Ins` on macOS, `%LOCALAPPDATA%\Programs\Common` on Windows. Or copy them from `zig-out` yourself.

### Linux dependencies
- **Build:** curl, X11, OpenGL, GLX, Vulkan (usually preinstalled). ALSA too, but only for the dev-only standalone.
- **Runtime:** `xdg-open`, `zenity`.

### Nix
Optional, but it's what we use day to day. It pins Zig and all dependencies.
- **Dev shell:** `nix develop` (flakes need to be enabled).
- **Package (Linux):** `nix build` gives a production build; `.#floe-profiling` and `.#floe-development` give the other modes. NixOS needs [nix-ld](https://github.com/nix-community/nix-ld).
- **Profile install:** `nix profile add .#floe` installs binaries to `bin`, plugins to `lib/clap` and `lib/vst3` (picked up by setups like [musnix](https://github.com/musnix/musnix)). Only one variant fits at a time; swap with `nix profile remove --regex '.*floe.*' ; nix profile add .#floe-development`.

### Testing and scripts
- **Unit tests:** `zig build test`. Test runner options go after `--`.
- **Other checks:** `test:*` and `check:*` steps run pluginval, clap-validator, vst3-validator, valgrind, clang-tidy and more. Add `-Dsanitize-thread` for thread sanitizer.
- **Dev scripts:** `script:*` steps, written in Zig rather than bash.

## About this codebase
Floe is written in C++ and we use Zig for the build system. Eventually, we're considering using Zig for the entire codebase.

We strive for handcrafted, detail-focused, performant code. The goal is to sustainably maintain a long-term codebase that is a joy to work on and results in a reliable, fast, professional product. With that in mind, we chose not to use any framework; just a handful of third-party libraries for specific tasks. We also don't use the C++ standard library, and only sparingly use the C standard library. Instead, we take a hands-on approach to as much as possible: memory management, data structure design and error handling. We have full control to tailor the code to our detail-focused design.

Good developer tools is a priority. Our build system supports clangd by emitting a compile_commands.json file. Additionally, we have good support for stacktraces, valgrind, clang-tidy, clang-format, etc. And because we use only Zig for the build system, we can cross-compile to other platforms and we can wholeheartedly use Clang-specific language extensions and features from the latest C++ language standards.

Thorough continuous testing and deployment is also a priority. We want to provide frequent backwards-compatible updates, and so need a way to ensure we don't break anything.

Floe's website is in the `website` folder and it's built with Docusaurus. We have 2 release channels: stable and beta. We use Docusaurus' versioning feature to maintain separate documentation for each channel. `website/docs` contains the beta documentation, and `website/versioned_docs/version-stable` contains the stable documentation. We use the command `zig build script:website-promote-beta-to-stable` to promote the beta website to stable.

## Discussion
Feel free to use the discussions on GitHub for questions, feedback, and ideas. Report bugs to the Github issue tracker. Also, FrozenPlain has a Floe section on their [forum](https://forum.frozenplain.com).

FrozenPlain, Sam's sample library company, is currently Floe's main user and funder, so its needs necessarily set much of the direction. Sam does make time for community requests and passion projects, but if you'd like to see other priorities, consider [sponsoring](https://floe.audio/docs/about-the-project/sponsorship) Floe's development. Thank you to our existing supporters for their contributions.

### CI
[Bencher](https://bencher.dev/perf/floe-benchmarks)
![CI](https://github.com/floe-audio/Floe/actions/workflows/tests.yml/badge.svg)
[![codecov](https://codecov.io/github/floe-audio/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/floe-audio/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-audio/floe/badge/main)](https://www.codefactor.io/repository/github/floe-audio/floe/overview/main)
