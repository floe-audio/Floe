<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Download & Install Floe

There's two ways to install Floe: use the installer, or manually move files. 

Either way, Floe is backwards-compatible[^pre-releases]. This means that you can replace an old version of Floe with a new version and everything will work.

Please check the [requirements](requirements.md) before downloading. 

Additional information:
- Floe is totally free, there's no signup or account needed; just download and install.
- Sample libraries and presets are installed separately via [packages](../packages/install-packages.md).
- The latest released version of Floe is v==latest-release-version==.

[^pre-releases]: Main releases are backwards compatible, but alpha and beta versions do not have this guarantee.

To [update Floe](./updating.md), just download and run the latest installer again.

## Installer (recommended)

<img src="../images/installer-macos-gui.png" width="49%" style="display: inline;">
<img src="../images/installer-windows-gui.png" width="49%" style="display: inline;">


> **<i class="fa fa-windows"></i> Floe Installer Windows**:<br>==Floe-Installer-Windows-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Installer macOS Apple Silicon[^mac-arm]**:<br>==Floe-Installer-macOS-Apple-Silicon-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Installer macOS Intel[^mac-intel]**:<br>==Floe-Installer-macOS-Intel-markdown-link==

Download, unzip, and run the installer program. The installer will guide you through the installation process, including choosing the plugin formats you want to install. 

Once the installation is complete you might need to restart your DAW in order for it to find the Floe plugins.


## Manually Install (advanced)

> **<i class="fa fa-windows"></i> Floe Manual Install Windows**:<br>==Floe-Manual-Install-Windows-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Manual Install macOS Apple Silicon[^mac-arm]**:<br>==Floe-Manual-Install-macOS-Apple-Silicon-markdown-link==
> 
> **<i class="fa fa-apple"></i> Floe Manual Install macOS Intel[^mac-intel]**:<br>==Floe-Manual-Install-macOS-Intel-markdown-link==


Normally you'll want to use the installer, but there could be some cases where you'd prefer to install Floe manually. To allow for this, we provide a zip file that contains Floe's plugin files. Extract it and move the files to your plugin folders.

##### Windows:
- CLAP: Move `Floe.clap` into `C:\Program Files\Common Files\CLAP`
- VST3: Move `Floe.vst3` into `C:\Program Files\Common Files\VST3`

##### macOS:
- CLAP: Move `Floe.clap` into `/Library/Audio/Plug-Ins/CLAP`
- VST3: Move `Floe.vst3` into `/Library/Audio/Plug-Ins/VST3`
- AU: Move `Floe.component` into `/Library/Audio/Plug-Ins/Components`

## 

---

Download links can also be found on the [Github releases page](https://github.com/floe-audio/Floe/releases/latest).

[^mac-arm]: For computers with [Apple Silicon](https://support.apple.com/en-us/116943) chips; Apple's line of M processors (M1, M2, etc.); ARM64 architecture.
[^mac-intel]: For computers with Intel chips; x86_64 architecture.
