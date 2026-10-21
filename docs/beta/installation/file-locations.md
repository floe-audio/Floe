# File Locations

> Quick reference guide for where Floe stores its files on Windows, macOS, and Linux

You rarely need to know where Floe keeps its files, but it's handy when you're backing up, moving to a new computer or troubleshooting. This page lists them for each operating system. To change where your sample libraries and presets live, see [Folders](/docs/beta/installation/folders) instead.

## Your files

Windows

macOS

Linux

-   Default sample library folder: `C:\Users\Public\Floe\Libraries\`
-   Default preset folder: `C:\Users\Public\Floe\Presets\`
-   Preferences: `C:\Users\Public\Floe\Preferences\floe.ini`
-   Autosaves: `C:\Users\Public\Floe\Autosaves\`

-   **Default folders** — where packages install sample libraries and presets unless you've chosen other folders. See [Folders](/docs/beta/installation/folders).
-   **Preferences** — `floe.ini` holds everything you set in Floe's Preferences panel. It's a text file you can edit by hand, and Floe picks up your changes straight away, though its contents aren't documented yet.
-   **Autosaves** — recent snapshots of each Floe instance's state, for recovering a lost sound. See [Autosave](/docs/beta/support/autosave).

## Plugins

Windows

macOS

Linux

-   CLAP: `C:\Program Files\Common Files\CLAP\Floe.clap`
-   VST3: `C:\Program Files\Common Files\VST3\Floe.vst3`

## Small background files

Windows

macOS

Linux

-   Persistent store: `%APPDATA%\Floe\persistent_store`
-   Device ID: `%APPDATA%\Floe\device_id`
-   Logs: `%LOCALAPPDATA%\Floe\Logs\`

-   **Persistent store** — a small file for things Floe remembers between sessions.
-   **Device ID** — a random ID used only when error reporting is on, so that we can measure how often each release crashes. See [Error Reporting](/docs/beta/support/error-reporting).
-   **Logs** — small text files describing what Floe has been doing. They can help when troubleshooting a problem.

These are safe to delete at any time. Floe recreates them the next time it starts, though you may need to set a few things again.

## Mirage files

These only matter if you used to have [Mirage](/docs/beta/about-the-project/mirage), Floe's predecessor, installed.

Windows

macOS

Linux

-   Preferences: `C:\ProgramData\FrozenPlain\Mirage\Settings\mirage.json`
-   Preferences (alternative): `%APPDATA%\FrozenPlain\Mirage\mirage.json`
-   VST2: `C:\Program Files\VSTPlugins\mirage64.dll`
-   VST2 (alternative): `C:\Program Files\Steinberg\VSTPlugins\mirage64.dll`
-   VST2 (alternative): `C:\Program Files\Common Files\VST2\mirage64.dll`
-   VST2 (alternative): `C:\Program Files\Common Files\Steinberg\VST2\mirage64.dll`
