<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installing Packages

Floe can install sample libraries and presets from ZIP files called _Floe packages_.

## 'Install package' button

![Floe's GUI](../images/install-package-gui.png)

If you don't already have Floe, [install](../installation/download-and-install-floe.md) it first, and open it in your DAW.

Next, download your chosen package(s). Remember, [packages](./about-packages.html) are ZIP files that contain sample libraries and presets.

__Don't extract it though__. Instead, use the 'Install package' button in the preferences panel of Floe and direct it to the ZIP file. It will extract the package into the correct folders. The library/presets will be available immediately, no need to restart Floe.

### Here's the full steps:

1. Open Floe.
1. Open the Preferences panel using the <i class="fa fa-cog"></i> cog icon at the top.
1. Open the Packages tab.
1. Click the 'Install package' button. A file browser will open. Use it to select the [package(s)](../packages/about-packages.html) you want to install. Packages are ZIP files that contain sample libraries and presets. They normally follow this naming scheme `Developer Name - Library Name Package.zip`.
1. Done. After installation, you may delete the ZIP file. No need to restart Floe.

### Mirage Compatibility
If the package is a Mirage library you will also need to install the Mirage Compatibility package. Download and install it the same way as the main package. [Download Mirage Compatibility Package.zip](https://github.com/FrozenPlain/floe-mirage-compatibility/releases/download/v1.1/FrozenPlain.-.Mirage.Compatibility.Package.zip).


### Floe handles the details

Floe handles the installation process intelligently. It will check existing installations across all known folders, check for conflicts and handle upgrades. It will even detect if you've modified a library and give you the option to keep your modified version. It will ask you about skipping or overwriting if it needs too. It will never duplicate libraries unnecessarily.

## Manually installing

Alternatively, you can manually install libraries and presets by extracting the ZIP file into the correct folders.

1. Extract the package ZIP file.
1. Open Floe.
1. Open the Preferences panel using the gear icon at the top.
1. Open the Folders tab.
1. Here you can control which folders Floe looks for libraries and presets in. Copy/move the contents of this package's Libraries into one of Floe's library folders, and the same for Presets into one Floe's presets folder.
1. Done. No need to restart Floe.
