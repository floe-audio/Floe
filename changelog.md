<!--
SPDX-FileCopyrightText: 2018-2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later

IMPORTANT: Our release process expects this file to contain heading that exactly match the released version numbers. 
For instance: 0.0.1. Don't change the headings.

-->

# Changelog
## 0.11.1-beta
- Add sitemap.xml for the website
- Improve the website homepage, installation, mirage, packages, roadmap pages.
- Packager tool:
  - Add ability to merge multiple packages into one
  - Add ability to output a JSON file with comprehensive information about the package
- Improve error reporting: show RAM, let Sentry decide on fingerprint, fix getting reports of crashes from outside of the plugin
- Fix a whole bunch of rare crashes
- Windows: fix uninstaller not uninstalling some small files

## 0.11.0-beta
- Add new velocity -> volume curve for each layer. This replaces the old mapping buttons and the master 'Velo' button. There's now much more control. The old parameters are available in a new 'legacy' parameters section but are no longer used unless loading a DAW preset that uses them.
- Add new algorithms to distortion: foldback, rectifier and ring-modulator
- Fix LFO and delay time not updating with the tempo
- Reduce pops and clicks with high-frequency LFOs, sharp changes in ADSR and some parameter changes
- GUI: improve picker panels behaviour:
    - Background darkens when panel is open
    - New close button on the top right
    - Remove unnecessary padding around the picker content
    - Single-click loads an item, double-click loads and closes the picker panel
    - Add right-click menu on libraries with option to open the library folder
    - Add right-click menu to presets and preset folders with option to open the folder, or delete the preset
- Improve menus:
    - Add left/right buttons to all menus on the GUI
    - Allow most menus to be dragged like a slider to change the value
    - Add randomise button to convolution IR menu as well as left/right buttons
    - Make all left/right buttons on the UI the same style
    - Move the left/right buttons to the right of the menu so it's consistent and easy to click forward and back without having to move the mouse long distances
    - Make the UI window size buttons consistent with the left/right buttons
- GUI: show loop points if an instrument has a single sample with built-in loop
- GUI: add resize corner to the bottom right of the GUI window
- Add missing tooltips for some preferences
- Work-around CLAP Studio One not using the correct threads
- GUI: right-click menu for the IR picker with 'unload IR' option
- GUI: don't show markers on ADSR when it's inactive
- GUI: Instrument pickers now have their own stat for filters (libraries, tags, etc.) allowing for more flexibility particularly with the randomise buttons
- GUI: remove option to show/hide the keyboard. It's not often used, and coming soon are new features that only make sense with the keyboard shown.
- Fix crash when changing tabs on the 'Legal' tab of the Info panel
- Fix presets picker not showing nesting of folder on Windows
- Fix rare crash when loading a library on Windows

## 0.10.3-beta
- Sample library creation: add 'fade_in_frames' to add_region
- GUI: remove the status panel from the picker panels, instead, items show a tooltip when hovered for a moment. This make the picker panels less cluttered and fixes the issue where the text would be too long to fit in the status panel.
- GUI: make sections in the picker panel collapsible
- GUI: improve the layout of the picker panel filters

## 0.10.2-beta
- Fix issue where layer volume would be automatically MIDI learned
- Fix some persistent MIDI CCs not being applied

## 0.10.1-beta
- Replace 'Reset all parameters' option with 'Reset State' which also clears things such as instruments.
- Fix crash with floe-package when using relative paths
- Fix possible error with installing/opening preset-picker
- Fix a set of possible rare errors

## 0.10.0-beta
- Picker panels:
    - Add filters for folders (tree view)
    - Put tags into categories and add icons
    - Add 2 filter modes: match-all and match-any, allowing for more powerful filtering. Every filter button has a label showing the number of matching items that dynamically updates as you change the filters.
    - Fix instrument picker not showing waveform instruments when no Mirage libraries installed
    - Show library icons for each library used by a preset
    - Show (?) icon for missing libraries
    - Add `<untagged>` filter for items that don't have any tags
- Add "Library Developer Panel" on the GUI with tag-builder tool for generating instrument tags in Lua. 
- Add right-click menu to the instrument picker button with 'unload instrument' option.
- Add lock/unlock mode for Save Preset panel for allowing changing presets with it still open
- Sample library creation: add support for multiple Lua files by using `dofile()`
- Sample library creation: add set_required_floe_version function
- Sample library creation: add generated Floe API definition file for Lua Language Server allowing for code completion and documentation as you type - available on the Library Developer Panel
- GUI: slightly improved icons
- GUI: use same toggle icon for all toggle buttons
- GUI: make mute/solo buttons more obvious when they're on
- Fix mute/solo buttons not greying out layers
- Fix cases where mute/solo button would get stuck on
- Fix preset selection buttons not working on the first click due to the scanning not having started. Now, scanning begins when the cursor is hovering over the button.

## 0.9.9-beta
- Sample library creation: add start_offset_frames to add_region
- Sample library creation: add tune_cents to add_region
- Normalise waveform GUI so that quiet samples are easier to see
- Fix rare crash related to sample library folder scanning

## 0.9.8-beta
- Fix rare error on Windows when closing the GUI when there's a file picker open
- Fix rare errors in some VST3 hosts
- Fix rare crash related to sample library folder scanning

## 0.9.7-beta
- Fix rare crash related to closing the GUI on Floe AU
- Fix rare crash related to library images
- Fix rare crash related to voice stealing
- Fix edge cases related to file paths on Windows

## 0.9.6-beta
- Check for updates on startup; a small indicator on the Info button will appear - leading to more information on the Info panel. Can be disabled in the preferences.
- Fix file picker on Windows not opened when there's missing folders
- Fix rare crash in Floe AU in Ableton Live

## 0.9.5-beta
- Fix potential crash in Floe AU format
- Fix errors related to filesystem watching on Windows
- Fix error related to sample library folders - could happen when installing a package

## 0.9.4-beta
- Offer separate downloads for Intel and Apple Silicon Macs rather than a universal binary, this reduces the size of the download to ~50 MB.
- Fix missing diagnostic information on macOS

## 0.9.3-beta
- Fix more errors with threads
- Fix memory leaks
- Fix some edge cases with certain hosts
- Improve automatic error reporting allowing us to fix bugs faster
- Protect against hosts that try to use multiple main threads

## 0.9.2-beta
- Slightly improve performance of Intel Mac builds
- Fix crash with Intel Mac builds
- Fix errors with threads

## 0.9.1-beta
- AU: fix crash when trying to open the file/folder picker
- Fix missing logo on macOS

## 0.9.0-beta
- Library creation: fix specifying loop end points relative to the end of the file.
- Library creation: replace always_loop, never_loop field with enum `loop_requirement`.
- Library creation: add round_robin_sequencing_group field to allow for different sets of variations.
- Library creation: separate note-off round-robin sequencing group from note-on round-robin sequencing group.
- Library creation: disable volume envelope for note-off regions
- Library creation: improve memory usage when reading Lua scripts
- Library creation: fix crash when an image file is empty
- VST3: fix not responding to note-on/note-off messages when they weren't on channel 0
- GUI: on the Save Preset dialog, add the ability to store/load a preset author
- GUI: make the Save Preset dialog larger and scroll to top when a new preset is loaded
- Fix potential crash when reporting an error
- Rename parameter right-click menu Set Value to Enter Value
- Support CLAP reset() function

## 0.8.3-beta
- Fix crash when error occurs in the Windows native file picker
- Fix crash when given invalid arguments to CLAP activate
- Fix crash when opening VST3 in FL Studio on Windows

## 0.8.2-beta
- Fix crash when trying to randomise instruments, presets or IRs when there's only one of them
- Fix presets folder still showing tags after a folder is removed. #120
- Fix crash when first opening the GUI

## 0.8.1-beta
See the [main release notes](https://github.com/floe-audio/Floe/releases/tag/v0.8.0-beta).

- Fix issue where Mirage and Floe libraries with the same name would conflict

## 0.8.0-beta
First beta release of Floe. This release is feature-complete. We are looking for feedback on the new features and any bugs you find.

- Add AU (Audio Unit v2) support
- Brand new instrument browser supporting tags, search, and filtering by library
- Brand new impulse response browser featuring all the options of the instrument browser
- Brand new preset browser with tags, search, and filtering by library. Preset metadata is tracked, library information is tracked, file changes are detected, duplicate presets are hidden.
- New built-in convolution reverb impulse library with 33 impulse responses
- New save preset dialog with author, description, and tags
- Fix instrument left/right and randomise buttons
- Fix instance ID not retaining
- Fix freeze when trying to resize GUI
- Fix GUI opening but only ever showing black
- Fix randomise parameters

Note: we have jumped from 0.0.7 to 0.8.0. This is to signify the change from alpha to beta but also because it wasn't really right in the first place only incrementing by the patch number. To make the jump obvious we start the minor version at 0.8.0 rather than 0.1.0.

## 0.0.7-alpha
The focus of this version has been bug fixes; in particular around loading Mirage libraries and presets.
- Max voice is increased from 32 to 256 allowing for more complex instruments
- Show the instrument type on the GUI: single sample, multisample or oscillator waveform
- Rename 'Dynamics' knob to 'Timbre' and fix its behaviour - for instruments such as Arctic Strings, it can be used to crossfade between different sets of samples.
- Fix missing code signing on Windows installer resulting in 'Unknown Publisher' warning
- Fix layer filter type menu being the incorrect width
- Fix crash when loop points were very close together
- Improve loop modes on GUI: it's obvious when a loop is built-in or custom, what modes are available for a given instrument, why loop modes are invalid.
- Add docs about looping
- Fix sustain pedal incorrectly ending notes

Mirage loading:
- Fix incorrect loading of Mirage on/off switches - resulting in parameters being on when they should be off
- Fix incorrect handling of Mirage's 'always loop' instruments
- Fix incorrect conversion from Mirage's effects to Floe's effects
- Improve sound matching when loading Mirage presets
- Fix failure loading some FrozenPlain Squeaky Gate instruments

Library creation:
- Re-organise the fields for add_region - grouping better into correct sections and allowing for easier expansion in the future.
- Sample library region loops now are custom tables with `start_frame`, `end_frame`, `crossfade` and `mode` fields instead of an array.
- Add `always_loop` and `never_loop` fields to sample library regions allowing for more control over custom loop usage on Floe's GUI.
- Show an error if there's more than 2 velocity layers that are using 'feathering' mode. We don't support this yet. Same for timbre layers.

## 0.0.6-alpha
- Add VST3 support
- Standardise how tags and folders will be used in instruments, presets and impulse responses: https://floe.audio/develop/tags-and-folders.html
- Fix sustain pedal
- Remove 'retrig CC 64' parameter from layer MIDI. This was mostly a legacy workaround from Mirage. Instead, we just use the typical behaviour that when you play the same note multiple times while holding the sustain pedal, the note plays again - stacking up.
- Fix package installation crash after removing folders
- Fix markers staying on ADSR envelope even when sound is silent
- Fix MIDI transpose causing notes to never stop
- Fix peak meters dropping to zero unexpectedly
- Fix not finding Mirage libraries/presets folders

## 0.0.5-alpha
- Fix text being pasted into text field when just pressing 'V' rather than 'Ctrl+V' 
- Windows: fix unable to use spacebar in text fields due to the host stealing the keypress
- Fix crash when trying to load or save a preset from file
- Improve the default background image

## 0.0.4-alpha
Fix crash when opening the preset browser.

## 0.0.3-alpha
Version 0.0.3-alpha is a big step towards a stable release. There's been 250 changed files with 17,898 code additions and 7,634 deletions since the last release. 

It's still alpha quality, and CLAP only. But if you're feeling adventurous, we'd love for you to try it out and give us feedback:
- [Download Floe](https://floe.audio/installation/download-and-install-floe.html)
- [Download some libraries](https://floe.audio/packages/available-packages.html)
- [Install libraries](https://floe.audio/packages/install-packages.html)

Error reporting has been a significant focus of this release. We want to be able to fix bugs quickly and make Floe as stable as possible. A part of this is a new a Share Feedback panel on the GUI - please use this!

Floe's website has been filled out a lot too.

New/edited documentation pages:
- [Floe support for using CC BY libraries](https://floe.audio/usage/attribution.html)
- [Autosave feature added](https://floe.audio/usage/autosave.html)
- [New error reports, crash protection, feedback form](https://floe.audio/usage/error-reporting.html)
- [New uninstaller for Windows](https://floe.audio/installation/uninstalling.html)

### Highlights
- Add new Info panel featuring info about installed libraries. 'About', 'Metrics' and 'Licenses' have been moved here too instead of being separate panels.
- Add new Share Feedback panel for submitting bug reports and feature requests
- Add attribution-required panel which appears when needed with generated copyable text for fulfilling attribution requirements. Synchronised between all instances of Floe. Makes complying with licenses like CC BY easy.
- Add new fields to the Lua API to support license info and attribution, such as CC BY
- Add lots of new content to floe.audio
- Add error reporting. We are now better able to fix bugs. When an error occurs, an anonymous report is sent to us. You can disable this in the preferences. This pairs with the new Share Feedback panel - that form can also be used to report bugs.
- Add autosave feature, which efficiently saves the current state of Floe at a configurable interval. This is useful for recovering from crashes. Configurable in the preferences.
- Add a Floe uninstaller for Windows, integrated into Windows' 'Add or Remove Programs' control panel
- Preferences system is more robust and flexible. Preferences are saved in a small file. It syncs between all instances of Floe - even if the instances are in different processes. Additionally, you can edit the preferences file directly if you want to; the results will be instantly reflected in Floe.
- Improve window resizing: fixed aspect ratio, correct remembering of previous size, correct keyboard show/hide, resizable to any size within a reasonable range.

### Other changes
- Add support for packaging and installing MDATA libraries (Mirage)
- Add tooltips to the preferences GUI
- Add ability to select multiple packages to install at once
- Rename 'settings' to 'preferences' everywhere
- Rename 'Appearance' preferences to 'General' since it's small and can be used for other preferences
- Make notifications dismiss themselves after a few seconds
- Fix externally deleted or moved-to-trash libraries not being removed from Floe
- Fix not installing to the chosen location
- Fix Windows installer creating nonsense CLAP folder
- Fix packager adding documents into the actual library rather than the package


## 0.0.2
- Fix Windows installer crash
- Don't show a console window with the Windows installer
- Better logo for Windows installer
- Remove unnecessary 'Floe Folders' component from macOS installer


## 0.0.1
This is the first release of Floe. It's 'alpha quality' at the moment - there will be bugs and there are a couple of missing features. This release is designed mostly to test our release process.

This release only contains the CLAP plugin. The VST3 and AU plugins will be released soon.


## Mirage
Floe used to be called [Mirage](https://floe.audio/about-the-project/mirage.html). Mirage contained many of the same features seen in Floe v0.0.1. But there are large structural changes, and some new features and improvements:

- Use multiple different libraries in the same instance.
- CLAP version added - VST3 and AU coming soon, VST2 support dropped.
- New installer: offline, no account/download-tickets needed. Libraries are installed separately.
- Ability for anyone to develop sample libraries using new Lua-based sample library format. The new format features a new system to tag instruments and libraries. Hot-reload library development: changes to a library are instantly applied to Floe.
- New [comprehensive documentation](https://floe.audio).
- Floe now can have multiple library and preset folders. It will scan all of them for libraries and presets. This is a much more robust and flexible way to manage assets rather than trying to track individual files.
- New, robust infrastructure:
    - Floe settings are saved in a more robust way avoiding issues with permissions/missing files.
    - Improved default locations for saving libraries and presets to avoid permissions issues.
    - New format for saving presets and DAW state - smaller and faster than before and allows for more expandability.
- New settings GUI.
- Floe packages: a robust way to install libraries and presets. Floe packages are zip files with a particular layout that contain libraries and presets. In the settings of Floe, you can install these packages. It handles all the details of installing/updating libraries and presets. It checks for existing installations and updates them if needed. It even checks if the files of any existing installation have been modified and will ask if you want to replace them. Everything just works as you'd expect.

Technical changes:
- Huge refactor of the codebase to be more maintainable and expandable. There's still more to be done yet though.
- New build system using Zig; cross-compilation, dependency management, etc.
- Comprehensive CI/CD pipeline for testing and creating release builds.
- New 'sample library server', our system for providing libraries and audio files to Floe in a fast, async way.

<!--
Notable changes from the last Mirage version to Floe v1.0.0:
- CLAP and VST3
- Load instruments from different libraries in same patch
- New format for creating sample libraries - open to everyone
- New settings GUI
- Sample libraries can add impulse responses
- New installer, offline and hassle-free
- Ability to install libraries and presets (packages) with a button - handles all the details automatically
- Voice count increased from 32 to 256
- New comprehensive documentation: https://floe.audio
- Add ability to have multiple library and preset folders
- Show the type of instrument below the waveform: multisample, single sample or waveform oscillator
- 'CC64 Retrigger' parameter removed - retrigger is the default now
- Change behaviour when a volume envelope if off - it now plays the sample all the way through, without any loops, playing the same note again will stack the sound
- Show the loop mode more clearly on the GUI - it's obvious when a loop is built-in or custom, what modes are available for a given instrument, why loop modes are invalid.
- New format for saving presets and DAW state - smaller and faster than before and allows for more expandability
- Improved default locations for saving libraries and presets to avoid permissions issues
- New GUI for picking instruments, presets and convolution reverb impulse responses: filter by library, tags; search.
- Floe is open source
- Floe's codebase is vastly more maintainable and better designed - huge work was put into creating a solid foundation to build on.
- Floe has systems in place for regularly releasing updates and new features
- Fixed issue with FrozenPlain - Arctic Strings crossfade layers not working
- Fixed issue where loop crossfade wouldn't be applied - resulting in pops
- Fixed issue where Mirage would spend forever trying to open
-->
