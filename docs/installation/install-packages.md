# Install Packages

> How to install Floe packages containing sample libraries and presets

Sample libraries and presets come to you in _packages_. Download your package file, then present it to Floe via its _Install package_ button.

1.  **Download your package(s)** — files ending `.floe-pkg` or `.floe-pkg-enc`.
2.  **Open Floe** in your DAW. If you haven't installed it yet, see [Install Floe](/docs/installation/download-and-install-floe).
3.  **Open Preferences** from the top panel, and choose the _Packages_ tab.
4.  **Click _Install package_** and select your package file. You can select several at once.

![The Packages tab of Floe's preferences](https://assets.floe.audio/website/screenshots/install-packages.732780b46497022b.png)

## Entering your license key

Some packages, often paid ones, are locked and need a license key. They install in the same way, but Floe first asks for the key to unlock them. These packages end in `.floe-pkg-enc` and need Floe version 2 or later. Everything happens offline, without an account or an internet connection.

There are two ways to give Floe your key:

-   **Load a license file** — if your purchase came with a `.floe-license` file, choose _Load license file_ and select it. One file can hold keys for several packages, so it unlocks everything you've selected in one go.
-   **Enter a key manually** — paste your key into the box, using _Paste_, then click _Activate_.

A license key is personal to you and contains your details. The summary at the end of the installation shows who the package is licensed to.

## When the installation finishes

The new libraries and presets are ready to use straight away, without restarting Floe or your DAW.

Floe shows a summary of what it installed, with a couple of optional extras:

-   **Send to Trash** — delete the package file. Floe doesn't need it once it's installed, though you might like to keep a copy as a backup.
-   **Load Preset** — load the preset bank's default preset. If Floe was blank when you installed, this preset is loaded for you instead.

Click _Done_ to close the summary.

## What is a package?

A package is simply how content is delivered to Floe. It usually holds one sample library plus a bank of factory presets for it, but it can hold any mix: just presets, several libraries, or both.

Floe doesn't keep track of packages; they're just the means of getting sample libraries and presets installed into your [folders](/docs/installation/folders). To remove things, see [Uninstalling](/docs/installation/uninstalling).

## Installing to a different drive

Floe installs into its default folders on your main drive unless you tell it otherwise. To install somewhere else, such as an external drive with more space:

1.  On the _Packages_ tab, open the _Sample library install folder_ menu.
2.  Choose _Add folder_ and pick or create the folder you want, such as "Floe Libraries" on your external drive. Floe adds it to the folders it scans and selects it for installing.
3.  Do the same with the _Preset install folder_ menu, if you want your presets somewhere else too.

Future packages install into the folders you've chosen. Libraries and presets you've already installed stay where they are, but Floe updates them in place when you install a newer version. To move them, see [Folders](/docs/installation/folders).

## Updates and conflicts

Whenever a new version comes out, install it in the same way. Floe handles upgrades for you:

-   If you have an older version, Floe updates it where it is.
-   If you already have the same or a newer version, Floe skips it.
-   If you've changed the installed files yourself, Floe asks before touching them. You can skip that part to keep your changes, or overwrite it. For presets, you can also keep both.

Whenever Floe replaces something, the old version goes to your Trash or Recycle Bin, with a name ending in something like `(old-3KJ9)`. If you change your mind, restore it from there and remove that ending from its name.

## ZIP packages

Older packages (from Floe v1) are ZIP files (`.zip`). Floe still installs them. Don't extract the ZIP first: select the ZIP file itself with _Install package_.

On macOS, Safari extracts ZIP files after downloading them. To stop it, untick _Open "safe" files after downloading_ in the _General_ tab of Safari's settings.

## Installing by hand

You don't usually need to, but you can install a package without the _Install package_ button. This works for `.floe-pkg` and `.zip` packages, not ones that need a license key.

A `.floe-pkg` file is really a ZIP file with a different ending. Rename it to end in `.zip` instead (on macOS: right-click › _Get Info_ › _Name & Extension_; on Windows, first turn on _File name extensions_ in File Explorer's _View_ menu, then rename it), and extract it with your usual tools.

Then copy the contents of its `Libraries` folder into one of Floe's sample library folders, and the contents of its `Presets` folder into one of its preset folders. You'll find them on the _Folders_ tab of Preferences, and the button opens them. Floe picks up the new files straight away.

## Mirage libraries

If a package contains a library made for [Mirage](/docs/about-the-project/mirage), Floe's predecessor, you may see an error saying _Mirage Compatibility - FrozenPlain_ isn't installed. To fix it, download the [Mirage Compatibility package](https://github.com/FrozenPlain/floe-mirage-compatibility/releases/download/v1.2/FrozenPlain.-.Mirage.Compatibility.Package.floe-pkg) and install it like any other package.
