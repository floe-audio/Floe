# System Requirements

> The operating systems, processors and plugin hosts Floe runs on

Floe is a plugin, so it runs inside your DAW or another plugin host. Here's what it needs:

-   **macOS:** macOS 11 (Big Sur) or later, on an Apple Silicon or Intel Mac.
-   **Windows:** Windows 10 or later, on a processor with SSE2 support (almost every PC since about 2006).
-   **Linux:** an x86 distribution with glibc 2.31 or later. Floe also needs the libcurl, OpenGL, GLX and X11 libraries (including Xcursor and Xext), plus `zenity` and `xdg-open`, which most desktop distributions already have.

Floe is 64-bit only, so your DAW needs to be 64-bit too.

## Plugin formats

Floe comes as a CLAP and a VST3 plugin, plus an AU (v2) plugin on macOS. If your DAW supports CLAP, we recommend using that version. There's no standalone application and no AAX (Pro Tools) support at the moment.

-   **CLAP** hosts include Reaper, Bitwig, FL Studio (2024 or newer) and Studio One Pro (v7 or newer).
-   **VST3** hosts include Cubase, Studio One, Ableton Live, Reason and Ardour.
-   **AU** hosts include Logic Pro and GarageBand.
