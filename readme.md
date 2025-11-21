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
Floe is a CLAP, VST3 and AU plugin for Windows, macOS, and Linux. It loads and plays sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project. 

See also our [roadmap](https://floe.audio/about-the-project/roadmap.html) section on our website, and our [FAQ](./faq.md) document.

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
### Quick start
- Download [Zig 0.14.0](https://ziglang.org/download/)
- Clone this repository
- Run `zig build install` in the root of the project
- Binaries are created in the `zig-out` directory

### More info
We typically use our Nix development environment on Linux or macOS to build Floe, and cross-compile to Windows. Enter this environment by installing Nix, enabling Nix flakes, and running `nix develop` in the root of the project. This ensures the correct Zig version and all dependencies are available. However, the Nix environment is not often necessary.

Run `zig build --help` to see more options.

To build an optimised release binaries, do something like `zig build install -Dbuild-mode=production`. You can also use Zig's standard `--prefix` option to specify an installation directory. We have configured this so that it will install audio plugins to the standard subfolders on each platform. On Linux, set the prefix to your home directory, on macOS set it to `~/Library/Audio/Plug-Ins`, and on Windows set it to `%LOCALAPPDATA%\Programs\Common`. Alternatively, omit the `--prefix` option and copy the binaries from the `zig-out` directory manually to wherever you need them.

We support cross-compiling using the `-Dtargets=` option. So for example you can compile the Windows and macOS plugins on a Linux machine. Valid targets are `x86_64-windows`, `x86_64-linux`, `x86_64-macos` and `aarch64-macos`. You can specify multiple targets separated by commas. Note that we don't support cross-compiling to Linux from any OS; you need to be on Linux to build Linux binaries at the moment.

Building on Linux, you will need libraries for curl, x11, OpenGL and GLX (handled automatically in the Nix environment); these are also normally installed by default on your distro. Runtime dependencies are `xdg-open` and `zenity`.

### Testing
Run `zig build test` to run the unit tests. Add arguments after `--` to configure our test runner's command-line options. We also have easy to use testing using pluginval, vst3-validator, clap-validator, valgrind, thread sanitizer, clang-tidy; see `zig build --help`.

### Scripts
Rather than use bash for miscellaneous development scripts, we use Zig. The scripts.zig file is automatically compiles and then is run via the CLI. See `zig build --help` for a list of available scripts.

## About this codebase
Floe is written in C++ and we use Zig for the build system. Eventually, we're considering using Zig for the entire codebase.

We strive for handcrafted, detail-focused, performant code. The goal is to sustainably maintain a long-term codebase that is a joy to work on and results in a reliable, fast, professional product. With that in mind, we chose not to use any framework; just a handful of third-party libraries for specific tasks. We also don't use the C++ standard library, and only sparingly use the C standard library. Instead, we take a hands-on approach to as much as possible: memory management, data structure design and error handling. We have full control to tailor the code to our detail-focused design.

Good developer tools is a priority. Our build system supports clangd by emitting a compile_commands.json file. Additionally, we have good support for stacktraces, valgrind, clang-tidy, clang-format, cppcheck, etc. And because we use only Zig for the build system, we can cross-compile to other platforms and we can wholeheartedly use Clang-specific language extensions and features from the latest C++ language standards.

Thorough continuous testing and deployment is also a priority. We want to provide frequent backwards-compatible updates, and so need a way to ensure we don't break anything.

Some parts of the codebase need some love (I'm looking at you GUI and audio processing). We're working on it.

Floe's website is in the `website` folder and it's built with Docusaurus. We have 2 release channels: **stable** and **beta**. We use Docusaurus' versioning feature to maintain separate documentation for each channel. `website/docs` contains the beta documentation, and `website/versioned_docs/version-stable` contains the stable documentation. We use the command `zig build script:website-promote-beta-to-stable` to promote the beta website to stable.

## Discussion
Feel free to use the discussions on GitHub for questions, feedback, and ideas. Report bugs to the Github issue tracker. Also, FrozenPlain has a Floe section on their [forum](https://forum.frozenplain.com).

### CI
![CI](https://github.com/floe-audio/Floe/actions/workflows/tests.yml/badge.svg)
[![codecov](https://codecov.io/github/floe-audio/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/floe-audio/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-audio/floe/badge/main)](https://www.codefactor.io/repository/github/floe-audio/floe/overview/main)
