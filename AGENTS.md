<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: CC0-1.0
-->

This is the Floe repository, an audio plugin written in C++ and built using the Zig build system. Floe is a sample library platform for performing, finding and transforming sounds from sample libraries - more like a ROMpler or sample-based synth than a traditional sampler. It's available on Linux, Windows, and macOS.

Floe, at it's core, is a CLAP plugin, a modern alternative to APIs such as VST3. CLAP is documented in the C header files that make up its interface. See the dependencies section below for where to find the CLAP source code.

# Commands
Building is done inside a Nix flake shell. You can use `nix develop` to enter the shell. Or to run a command inside a shell (normally recommended), use `nix develop --command <command>`. All these commands should be prefixed with `nix develop --command` if you're not already in the shell:
- Compile the project: `zig build -Dtargets=native -Dbuild-mode=development`. Cross-compiling is supported. Alternatives options instead of `native` are: `linux`, `windows`, `mac_arm`, `mac_x86`. You can add `-Dsanitize-thread` to enable Clang's thread sanitizer.
- Run unit tests: `tests --filter=*`. Run `tests --help` for more options.
- Format all code using clang-tidy: `just format`
- Check spelling : `just check-spelling`
  - Be prepared to add exceptions to docs/ignored-spellings.dic since our spell-check is not smart and will often think non-words are words. We use British English.
- Check license comment headers: `just check-reuse`

# Source code overview
Here are some notable subdirectories, though there are plenty more.
- `src/foundation/`: our 'standard library' with data structures and core utilities. All code depends on this.
- `src/os/`: OS abstraction layer.
- `src/utils/`: more specific utilities that are not necessary Floe-specific, building on OS and foundation.
- `src/common_infrastructure/`: Floe-specific code that's used by the plugin and also installers and other tools.
- `src/plugin/`: the actual plugin code, including audio processing and GUI.
- `docs/`: markdown documentation built into a website using mdbook.
- `src/tests/`: unit test framework and test for foundation, OS, and utils modules.

# Dependencies
Floe uses a few third-party libraries. These are typically managed by the Zig package manager. See `build.zig.zon` for the full list. Once the project has been built once, you can get the source code for these libraries by searching the `.zig-cache-global/` directory using `fd` or `find`. This is the directory we've configured Zig to put downloaded packages.

# Workflow
- Run the compile command. Don't try filtering it's output with grep. Just read all of it. It will contain errors with file names and line numbers - understand the error and fix it if necessary.
- Where possible, add tests. We have our own test framework that is similar to Catch2: src/tests/framework.hpp. We tend to put tests in the same cpp file as the implementation. Write a test case with TEST_CASE(TestName). Then use TEST_REGISTRATION(RegisterMyTests) to create a registration function, inside that use REGISTER_TEST(TestName) to register the test, finally add this registration function to src/tests/tests_main.cpp. If you want an example of a test, look at src/common_infrastructure/autosave.cpp. The API for the test framework is in src/tests/framework.hpp.

# Style
- No C++ STL/standard library.
- We write in a Zig-like style: closer to modern C than C++.
- See `.clang-tidy`'s readability-identifier-naming section for naming conventions.

# Github Issues
We extensively use Github issues to track work. Use `gh` to query and manage issues. Our issues often include lots of details and design.

- **Priority labels**: `priority/urgent` (blocking users or critical bug - ship ASAP), `priority/high` (significant impact - next release), `priority/medium` (valuable improvement - plan soon), `priority/low` (nice-to-have - when capacity allows)
- **Effort labels**: `effort/small` (~1-4 hours), `effort/medium` (~0.5-1 day), `effort/large` (~2-3 days), `effort/extra-large` (~1 week or needs breakdown)
- `awaiting-release` - for issues fixed but not yet released. Issues should be closed and tagged with this label.
- `needs-repro` - needs investigation work to reproduce the issue
- `needs-design` - needs thinking and design before beginning writing code

**Never** try to write code for a `needs-design` issue. First, discuss, and design the solution - working out a full plan. Update the issue description with the design and remove the label when ready.

# Github Project
Additionally, we use an organisation-level kanban board GitHub Project (floe-audio/1) with a "Status" field to track issue progress (Up Next, In Progress, Done). All issues are automatically added to the board.

### Commands
```bash
just project-items-json                                # Get raw JSON of all project items
just project-item-id <issue_number>                    # Get project item ID
just project-status <issue_number>                     # Get current status
just project-set-status <issue_number> "<status>"      # Set status (Up Next, In Progress, Done)
just project-issues-by-status "<status>"               # Get issues by status (number + title)

```
