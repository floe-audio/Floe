<!--
SPDX-FileCopyrightText: 2025 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Installing Packages

Floe can install sample libraries and presets from ZIP files called _Floe packages_.

## 'Install package' button

![Floe's GUI](../images/install-package-gui.png)

### At a glance

Download your package ZIP file(s). Don't extract them. Instead, click the 'Install package' button in Floe's Preferences panel. Direct it to the ZIP file.

### Instructions

First, [install Floe](../installation/download-and-install-floe.md) and open it in your DAW.

Download your chosen [package(s)](./about-packages.md) (ZIP files containing sample libraries and presets). **Don't extract them.**

To install:
1. Open Floe's Preferences panel (<i class="fa fa-cog"></i> cog icon)
2. Go to the Packages tab
3. Click 'Install package' and select your ZIP file(s)
4. Done - libraries/presets are immediately available

The package ZIP files can be deleted after installation. No restart required.

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
