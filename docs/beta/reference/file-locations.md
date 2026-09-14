# File Locations

> Quick reference guide for where Floe stores its files on Windows, macOS, and Linux

This page provides a quick reference for where Floe creates and stores its files on your system.

Windows

macOS

Linux

## Public files

-   Default sample libraries folder: `C:\Users\Public\Floe\Libraries\`
-   Default presets folder: `C:\Users\Public\Floe\Presets\`
-   Preferences: `C:\Users\Public\Floe\Preferences\floe.ini`
-   Autosaves: `C:\Users\Public\Floe\Autosaves\`

## Plugins

-   CLAP plugin: `C:\Program Files\Common Files\CLAP\Floe.clap`
-   VST3 plugin: `C:\Program Files\Common Files\VST3\Floe.vst3`

## Auxiliary files

-   Persistent store: `%APPDATA%\Floe\persistent_store`
-   Device ID: `%APPDATA%\Floe\device_id`
-   Logs: `%LOCALAPPDATA%\Floe\Logs\`

## Mirage files

These are only relevant if you used to have [Mirage](/docs/beta/about-the-project/mirage) installed.

-   Preferences: `C:\ProgramData\Mirage\Settings\mirage.json`
-   Preferences (alternate): `C:\Users\<your-name>\AppData\Local\FrozenPlain\Mirage\mirage.json`
-   VST2: `C:\Program Files\VSTPlugins\mirage64.dll`
-   VST2 (alternate): `C:\Program Files\Steinberg\VSTPlugins\mirage64.dll`
-   VST2 (alternate): `C:\Program Files\Common Files\VST2\mirage64.dll`
-   VST2 (alternate): `C:\Program Files\Common Files\Steinberg\VST2\mirage64.dll`

## Notes

Auxiliary files can be deleted at any time (although you may have to re-set some preferences) - they will be recreated by Floe the next time it starts up.

`floe.ini` is user-editable. We currently do not have have documentation for its contents. But the mechanisms are in place: changes you make to this file are instantly reflected in Floe. Floe's UI preferences panel also edits this file.

`persistent_store` is a small binary file for storing things that are preferable to persist between sessions.

`device_id` is a randomly generated ID that we use when error-reporting is switched on so that we can identify 'devices' and get data on the error/crash rate of our releases.

Logs are small diagnostic text files that can be useful when troubleshooting issues.
