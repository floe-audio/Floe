# Folders

> Managing sample library and preset folders in Floe

Floe has a flexible folder system for managing your sample libraries and presets, allowing for custom organisation and easy use of external drives.

Floe works _with_ your natural usage of drives, files, and folders rather than enforcing rigid requirements.

There are two **separate** types of folders that Floe manages: **sample library folders** and **preset folders**.

You can see and manage folders in Floe's preferences panel — opened by clicking the cog icon at the top of Floe's window:

![Folder Preferences GUI](/images/screenshots/folders.png)

Floe automatically scans these folders (and their subfolders) for presets and libraries respectively. Anything added, removed or changed in these folders is reflected in Floe immediately. Floe doesn't have a separate 'memory' about presets and libraries — what exists in your folders is the only source of truth.

If you're not yet familiar with what a sample library or preset is, read more about these concepts on the [glossary](/docs/beta/getting-started/glossary) page.

### Adding folders

Add a folder to either sample libraries or presets by clicking the 'Add Folder' button. A file dialog will open - select your desired folder. Floe will immediately start scanning it.

Floe scans subfolders of the folder you pick too; so you typically want to select a top-level folder such as "Floe Libraries" or "Floe Presets" that contains all your libraries/presets inside it rather than adding each individual library/preset folder.

### Removing folders

Use the trash icon to remove a folder from either sample libraries or presets. This does not physically delete the folder, it just removes it from Floe's scanning list. You cannot remove Floe's default folders (the first on the lists).

### Open folder

Use the link icon to open the folder in your system's File Explorer/Finder.

### Moving libraries/presets to a new location

Floe does not have an automatic 'move' function, but you can easily move them using your system's File Explorer/Finder alongside Floe's folder management.

To move your existing libraries or presets to a new folder follow these steps:

1.  **Create the new folder in File Explorer/Finder**. For example, create a folder called "Floe Libraries" and "Floe Presets" on your external drive.
2.  **Open Floe's preferences** panel by clicking the . Open the _Folders_ tab.
3.  **Add the new folder** using the 'Add Folder' button. Ensure you use the right button for 2 separate types: libraries and presets.
4.  **Open your existing libraries/presets folders** using the link icon next to the folder listing.
5.  Use File Explorer/Finder to **move the libraries/presets** to your desired new location. This often involves selecting all items in the folder and cut-and-pasting them (or drag-and-drop) into the new folder.

Floe, in the background, should instantly detect the changes and update its library and preset listings accordingly. If not, try restarting Floe or your DAW.

### Set the default installation folder

If you've added extra folders, you can select these as the default location to install libraries and presets in the future. Use the dropdown on the _Packages_ tab of the preferences panel.

### Got mixed up?

If you loose track regarding what files are libraries vs. presets, here's some tips:

-   Sample libraries are folders typically with names like `Developer Name - Library Name`. They often contain a `Samples` subfolder and a file called `floe.lua`.
-   Preset banks are folders typically with names like `Library Name Factory Presets`. They often have various subfolders for different categories, each containing many tiny `.floe-preset` files.

We usually recommend not mixing libraries and presets in the same folder, however, Floe can handle this if needed — just set up both types of folders to point to the same location.

### Organising presets

### Why are libraries and presets separate?

The separation provides several benefits — even though the common case is that a sample library comes with an associated factory preset bank:

-   Sample libraries are developed using technical programming language tools - not typically edited by most users, whereas presets are often created and modified by users. Using different folders avoids confusion and frees up more fearless customisation for the 2 separate types of users (musicians, composers vs. sample library developers).
-   Preset banks can refer to multiple sample libraries rather than always relating to just one.
-   Samples libraries are often very large with hundreds of audio files and need to be stored on an external drive, whereas presets are tiny and can easily zipped and shared.
