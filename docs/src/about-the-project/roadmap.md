<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Roadmap
Floe is an ongoing project that will have lots of backwards-compatible updates. We want to be able to release new features and improvements as soon as they're ready.

- [x] Finish VST3 and AU support
- [x] Complete the GUI for adding multiple libraries to a single instance
- [x] Recreate the presets infrastructure and GUI
- [x] Comprehensive testing of all plugin formats in all DAWs and operating systems
- [x] Prepare a couple of Floe sample libraries ready for release
- [x] Ability to split layers to different ranges on the keyboard
- [x] Implement pitchbend
- [x] Macro parameters

Later down the line, we're planning to:
- [ ] Refresh the GUI: make it more consistent and performant, and refactor the code for easier expansion
- [ ] Overhaul the audio & MIDI processing engine for easier expansion, such as: MIDI MPE, MIDI2, polyphonic modulation, sample-accurate automation, MIDI learn.
- [ ] Add more features to the sample library format to allow for more complex instruments: legato, key-switches, drums, etc.

Even further down the line, we're looking into:
- Allow for creating custom GUIs 'views' using the library Lua API
- Adding wavetable oscillators/granular synthesis as a option to layer with sampled instruments
- Adding a more comprehensive modulation system

Give us feedback on these using [Github's discussions](https://github.com/floe-audio/Floe/discussions) section. FrozenPlain also have a Floe section on their [forum](https://forum.frozenplain.com/t/floe).
