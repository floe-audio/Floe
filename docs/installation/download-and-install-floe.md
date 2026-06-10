# Installing Floe

> Instructions for downloading and installing Floe on Windows, macOS, and Linux

Simply [download](/download) and run the installer for your operating system.

For thoroughness, this page provides detailed installation instructions for each platform.

Windows

macOS

Linux

## Requirements

On Windows, Floe is available in the CLAP and VST3 formats. We recommend using the CLAP version where possible. Requirements:

-   Windows 10 or later
-   64-bit computer
-   x86-64 processor with SSE2 support (almost all processors in a Windows PC since ~2006 have this)
-   64-bit CLAP or VST3 host. CLAP hosts include Reaper, Bitwig, FL Studio (2024 or newer) and Studio One Pro (v7 or newer). VST3 hosts include Cubase, Studio One, Ableton Live, Reason, and more.

Just to be extra clear: there's no 'standalone' application and no AAX (Pro Tools) support. We hope to expand Floe's compatibility in the future.

## Overview

To install Floe, download the installer from the [downloads](/download) page and extract it (normally double-click). Double click on the installer EXE file and follow the instructions.

### Alternative: Manual Installation (Advanced)

Normally you'll want to use the installer, but there could be some cases where you'd prefer to install Floe manually. To allow for this, we provide a zip file that contains Floe's plugin files; these can also be found on the [downloads](/download) page. Extract it and move the files to your plugin folders.

-   **CLAP:** Move `Floe.clap` into `C:\Program Files\Common Files\CLAP`
-   **VST3:** Move `Floe.vst3` into `C:\Program Files\Common Files\VST3`

* * *

### How to update Floe

To update Floe, simply download and run the latest installer again. It will replace the old version in a backwards-compatible way — your DAW projects won't break.

Alternatively, if you manually installed Floe, install the latest files - replacing the old ones.

### Checking for updates

Floe can automatically check for available updates. It does this in a simple, non-intrusive way.

When a newer version is available, a red dot will appear on the info button in Floe's window.

![Floe red dot update indicator](/assets/images/floe-update-red-dot-490955c5b5f388e3b898da502b17a527.png)

Clicking this will open the info panel and show you the options: ignore the update, or visit the download or changelog pages. Ignoring the update means the red dot will disappear until a newer version is available.

![Floe info panel update info](/assets/images/floe-update-info-panel-323de58d9b7d96ffd36afc17793fa19d.png)

### Disabling automatic update checks

You can disable automatic update checks by unchecking the "Check for updates" option in Floe's preferences panel.

### Beta updates

There is also a checkbox on the preferences panel for receiving update notifications for the latest release, including if there's a newer beta version available. This is only recommended in certain situations. See the [Beta Testing](/docs/about-the-project/beta-testing) page for more information.
