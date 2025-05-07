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
Floe is a CLAP, VST3 and AU plugin for Windows, macOS, and (currently for development purposes only) Linux. It loads and plays sample libraries in the Floe format. Visit [floe.audio](https://floe.audio) for more information about the project. 

## Roadmap
See our [roadmap](https://floe.audio/about-the-project/roadmap.html) section our our website. We also sometimes use GitHub issues to track [milestones](https://github.com/floe-audio/Floe/milestones?direction=asc&sort=title&state=open) towards future releases. 

## License
This project is licensed under GPL version 3 or later. See the LICENCES folder for the full licence text. We follow the [REUSE](https://reuse.software/) recommendations for this repository.

## Building
Building is done on a Linux or macOS system. Cross-compilation is supported. Therefore from Linux/macOS you can build for Windows and other platforms. 

Linux is only used for development purposes at the moment, and you can't cross-compile to Linux from a non-Linux system.

- Install Nix and enable Flakes
- Run `nix develop` in the root of the project to enter a shell with all dependencies
- Run `just build native`. Alternative options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`
- Binaries are created in the zig-out directory

## About this codebase
Floe is written in C++ and we use Zig for the build system. Eventually, we're considering using Zig for the entire codebase.

We strive for handcrafted, detail-focused, performant code. The goal is to sustainably maintain a long-term codebase that is a joy to work on and results in a reliable, fast, professional product. With that in mind, we chose not to use any framework; just a handful of third-party libraries for specific tasks. We also don't use the C++ standard library, and only sparingly use the C standard library. Instead, we take a hands-on approach to as much as possible: memory management, data structure design and error handling. We have full control to tailor the code to our detail-focused design.

Good developer tools is a priority. Our build system supports clangd by emitting a compile_commands.json file. Additionally, we have good support for stacktraces, valgrind, clang-tidy, clang-format, cppcheck, etc. And because we use only Zig for the build system, we can cross-compile to other platforms and we can wholeheartedly use Clang-specific language extensions and features from the latest C++ language standards.

Thorough continuous testing and deployment is also a priority. We want to provide frequent backwards-compatible updates, and so need a way to ensure we don't break anything.

Some parts of the codebase need some love (I'm looking at you GUI and audio processing). We're working on it.

## Discussion
Feel free to use the discussions on GitHub for questions, feedback, and ideas. Report bugs to the Github issue tracker. Also, FrozenPlain has a Floe section on their [forum](https://forum.frozenplain.com).

### CI
![CI](https://github.com/floe-audio/Floe/actions/workflows/tests.yml/badge.svg)
[![codecov](https://codecov.io/github/floe-audio/Floe/graph/badge.svg?token=7HEJ7SF75K)](https://codecov.io/github/floe-audio/Floe)
[![CodeFactor](https://www.codefactor.io/repository/github/floe-audio/floe/badge/main)](https://www.codefactor.io/repository/github/floe-audio/floe/overview/main)
