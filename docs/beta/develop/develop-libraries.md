# Developing Libraries for Floe

> Guide on how to create sample libraries for Floe using Lua scripting

This page explains how to develop sample libraries for Floe.

Creating sample libraries for Floe **requires programming knowledge**. While Floe itself is designed for all musicians, developing libraries involves writing Lua scripts to configure and map audio files. This programming-first approach enables automation, version control integration, AI-assisted workflows, bulk operations — scaling all the way to very complex libraries. It also allows creating custom functions for sample-mapping, normalisation, pitch-correction, and building instrument variations - a level of flexibility that cannot be achieved with UI-based sample-mapping tools.

In the future, we're looking at tools that convert/generate Floe Lua scripts to allow non-programmers to create libraries.

note

We made these tools for our own use and are sharing them with the community. Consider contributing or [sponsorship](/docs/beta/about-the-project/sponsorship) if you'd like to see further investment in this area.

### What you'll need

-   Tools and experience writing and editing code
-   Understanding of sample mapping concepts (key ranges, velocity layers, etc.)
-   It's recommended to have experience programming other samplers like Kontakt, SFZ, Decent Sampler, etc.

## Floe's sample library format

Floe's format is designed for configuring audio files into playable instruments. It does not add any audio-manipulation or custom GUIs. Floe has a ready-to-use GUI and lots of audio parameters. In the future we might add more advanced scripting/GUI creation features to the format. The format is designed to be extended whilst retaining backwards compatibility.

Floe's sample library format currently supports the following features:

-   Velocity layers
-   Round-robin
-   Crossfade layers
-   Loop points with crossfade
-   Convolution reverb IRs
-   Velocity layer feathering
-   Trigger samples on note-off

As someone in the community put it, our format is essentially _'[SFZ](https://sfzformat.com/) in Lua so you don't lose your mind'_ - which captures the gist pretty well.

_The basics are covered, but there are plenty of features that other samplers might have that Floe doesn't. We expand our format to meet the needs of our products rather than add add things that we don't currently need._

### Why a new format?

No existing format met our requirements for Floe, which are:

-   Libraries should be plain folders of audio files.
-   Libraries should be portable across filesystems & operating systems.
-   Libraries should be configured using a proper programming language to enable creating complex libraries in a maintainable way.
-   The format should be extensible - allowing us to innovate in the field of sampling.
-   The solution should easily integrate into Floe's codebase.

We're not trying to replace existing formats or create a new standard. Instead, our custom format is specifically designed to align with Floe's architecture and philosophy, enabling us to rapidly develop and deliver the high-quality sample libraries our users deserve.

### Developer friendly

Floe's format is designed to be developer-friendly. It plays well into the tooling and experience of people who are used to dealing with code:

-   Works with version control: libraries are just folders of files, they're portable across filesystems and operating systems.
-   Hot reloading: Floe automatically reloads the library whenever you change a file: Lua, audio, or images. It creates a very fast feedback loop. If a change isn't picked up, use the top panel's 3-dots menu → Rescan Libraries & Presets to force a rescan.
-   Uses a full programming language: Lua is simple, powerful, and widely used. You can use variables, functions, loops, and conditionals to configure your library.
-   Floe's Lua API is simple and concise.
-   [`floe-library-inspector`](#library-inspector) CLI tool dumps the parsed library as JSON or Lua so you can verify exactly what Floe loaded — useful for debugging without launching the plugin.

## The structure

Let's look at the structure of a Floe sample library:

```
📂FrozenPlain - Slow/├── 📄slow.floe.lua├── 📄Licence.pdf├── 📄About Slow.html├── 📁Samples/│   ├── 📄synth_sustain_c4.flac│   └── 📄synth_sustain_d4.flac└── 📁Images/    ├── 📄background.png    └── 📄icon.png
```

There's only one essential part of a Floe sample library: the `floe.lua` file. This file can have a custom name so long as it ends with `.floe.lua` - for example, `woodwind-textures.floe.lua`.

The rest of the structure are conventions that are recommended but not required:

-   **Licence**: Sample libraries are recommended to have a file called Licence that describes the terms-of-use of the library. It can be any format: PDF, TXT, etc.
-   **About**: A file that describes the library. Any file format. Information about a library is useful for when someone might not have Floe's GUI available. Use this document to explain what the library contains, website links and any other information that might be useful. Floe's [packager tool](/docs/beta/develop/packaging#packager-command-line-tool) can automatically generate this file.
-   **Library folder name**: The folder containing the `floe.lua` file should be named: "Developer - Library Name".
-   **Subfolders**: Subfolders are up to the developer. We recommend 'Samples' for audio files and 'Images' for images. These can have any number of subfolders. Your `floe.lua` file can reference any file in these subfolders.

## The `floe.lua` file

The `floe.lua` file is the most important part of a library. It's a script that maps and configures the audio files into playable instruments, written in the Lua 5.4 programming language.

This file uses [Floe's Lua functions](/docs/beta/develop/library-lua-scripts) to create the library, create instruments, and add regions and impulse responses. It can reference any file in the library using relative paths.

Here's a made-up example of a `floe.lua` file:

```
local library = floe.new_library({    name = "Iron Vibrations",    tagline = "Organic sounds from resonating metal objects",    library_url = "https://example.com/iron-vibrations",    description = "A collection of resonating metal objects sampled using a handheld stereo recorder.",    author = "Found-sound Labs",    id = "Iron Vibrations - Found-sound Labs",    author_url = "https://example.com",    revision = 1,    background_image_path = "Images/background.jpg",    icon_image_path = "Images/icon.png",    background_overlay = {        vignette = {            colour = 0x00000066,            inner_radius = 0.2,        },        panel_tint = {            colour = 0x0000000D,        },    }})local instrument = floe.new_instrument(library, {    name = "Metal Fence Strike",    id = "Metal Fence Strike",    folder = "Fences/Steel",    description = "Tonal pluck metallic pluck made from striking a steel fence.",    tags = { "found sounds", "tonal percussion", "metal", "keys", "cold", "ambient", "IDM", "cinematic" },    waveform_audio_path = "Samples/file1.flac",})floe.add_region(instrument, {    path = "Samples/One-shots/Resonating String.flac",    root_key = 60,    trigger_criteria = {        trigger_event = "note-on",        key_range = { 60, 64 },        velocity_range = { 0, 100 },        velocity_range_high_resolution = { 0, 1000 },        round_robin_index = 0,        round_robin_sequencing_group = "group1",        feather_overlapping_velocity_layers = false,        auto_map_key_range_group = "group1",    },    loop = {        builtin_loop = {            start_frame = 24,            end_frame = 6600,            crossfade = 100,            mode = "standard",            lock_loop_points = false,            lock_mode = false,        },        loop_requirement = "always-loop",    },    timbre_layering = {        layer_range = { 0, 50 },    },    audio_properties = {        gain_db = -3,        start_offset_frames = 0,        tune_cents = 0,        fade_in_frames = 0,        fade_out_frames = 0,    },    playback = {        keytrack_requirement = "default",    },    slices = {},    loop_beats = 4,    native_bpm = 120,})floe.add_ir(library, {    name = "Cathedral",    id = "Cathedral",    path = "irs/cathedral.flac",    folder = "Cathedrals",    tags = { "acoustic", "cathedral" },    description = "Sine sweep in St. Paul's Cathedral.",    audio_properties = {        gain_db = -3,    },})floe.set_attribution_requirement("Samples/bell.flac", {    title = "Bell Strike",    license_name = "CC-BY-4.0",    license_url = "https://creativecommons.org/licenses/by/4.0/",    attributed_to = "John Doe",    attribution_url = "https://example.com",})floe.add_named_key_range(instrument, {    name = "Extended Notes",    key_range = { 80, 128 },})floe.set_required_floe_version("2.0.0-beta.6+8eb988e5")local extended_table = floe.extend_table({ foo = "" }, {})local hundred_range = floe.midi_range_to_hundred_range({ 0, 127 })local thousand_range = floe.midi_range_to_thousand_range({ 0, 127 })return library
```

`floe.new_library()` must be called and returned it at the end of the script. All other features are optional. When Floe runs your Lua file, it will show you any errors that occur along with a line number and a description.

## How to get started

1.  Create a new folder in one of Floe's [sample library folders](/docs/beta/usage/folders). We recommend naming it 'Author Name - Library Name'.
2.  Create a file in that folder called `my-library.floe.lua`.
3.  Create a subfolder called `Samples` and put your audio files in there.
4.  Open the Lua file in your text editor. If you're not already familiar with an editor, then Sublime Text or Visual Studio Code are reasonable choices.
5.  Use the `floe.new_library()` function to create your library, filling in all the fields marked `[required]` in the [Floe's Lua reference](/docs/beta/develop/library-lua-scripts).
6.  Use `floe.new_instrument()` to create an instrument, and then add regions to it using `floe.add_region()`, again, filling in the fields that are documented.
7.  At the end of the file, return the library object you just created: `return library`.
8.  Floe automatically detects whenever any library file changes and will tell you if there's any errors. If a library is correctly configured, it will instantly appear in Floe. For a fuller view of what was parsed — regions, loop points, missing samples — run [`floe-library-inspector`](#library-inspector).

### Approach to sample mapping

You can start by manually writing individual `add_region` calls for each sample, filling out the required information as you go. When things start getting repetitive or you need to apply consistent logic across many samples, you'll want to leverage Lua's programming features:

-   **[Signet](https://github.com/SamWindell/Signet)** to analyse your samples and generate metadata files
-   **Variables and tables** to store information about your samples
-   **Loops** to process multiple samples with the same logic
-   **Functions** to calculate parameters like tuning, velocity ranges, and key ranges
-   **Pattern matching** to extract information from filenames

This programming approach can make it much more practical to work with larger sample sets - you write the mapping logic once and let the code apply it to all your samples.

## Using Signet

The command line tool [Signet](https://github.com/SamWindell/Signet) in an excellent aid for developing Floe samples libraries. Signet can export data about your samples in Lua format which you can then use in your `floe.lua` file using `dofile`.

For example, this file is generated by Signet:

```
-- signet . print-info --format lua --path-as-key -- NOTE: Signet can also detect the pitch of your samples when -- you use the `--detect-pitch` flag with print-info.return {  ["my-sample-a.flac"] = {    bit_depth = 16,    channels = 2,    crest_factor = 16.5734,    crest_factor_db = 24.3883,    frames = 208341,    length_seconds = 4.34044,    metadata = nil,    peak_db = -3.00036,    peak_frame = 5927,    rms_db = -27.3886,    sample_rate = 48000  },  ["my-sample-b.flac"] = {    bit_depth = 24,    channels = 2,    crest_factor = 8.90268,    crest_factor_db = 18.9904,    frames = 2299122,    length_seconds = 47.8984,    metadata = nil,    peak_db = -3,    peak_frame = 877676,    rms_db = -21.9904,    sample_rate = 48000  },}
```

Then in your Floe Lua file you have access to all the information about your samples allowing you to easily create instruments and regions.

With the features of a full programming language and Floe's hot-reloading of scripts, it's easy to create complex configurations and bulk-tweak tuning, normalisation, loop points, and other parameters of an instrument.

For example, normalising all samples to a target level can be done using a simple calculation based on the sample's `peak_db` value. We often find it's useful to partially move levels towards a target level rather than fully normalising them in order to retain some of the character of the instrument. This can be computed in the Lua and tweaked as needed, with the results being immediately audible in Floe.

Floe doesn't generate this kind of audio data for you because it can be slow to scan large folders of samples. It's better for the sample library developer to generate it once with Signet and bake it into a Lua file, instead of the sample library users having to wait every time they load the library.

## Library inspector

`floe-library-inspector` is a small CLI tool for debugging libraries. It parses your library and dumps everything Floe loaded as JSON (or Lua with `--format=lua`) — surfacing parse errors, captured `print()` output, and any orphan or missing audio files.

-   [Floe-Library-Inspector-v2.0.0-beta.6-Windows.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.0-beta.6/Floe-Library-Inspector-v2.0.0-beta.6-Windows.zip) (5 MB)
-   [Floe-Library-Inspector-v2.0.0-beta.6-macOS-Apple-Silicon.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.0-beta.6/Floe-Library-Inspector-v2.0.0-beta.6-macOS-Apple-Silicon.zip) (1 MB)
-   [Floe-Library-Inspector-v2.0.0-beta.6-macOS-Intel.zip](https://github.com/floe-audio/Floe/releases/download/v2.0.0-beta.6/Floe-Library-Inspector-v2.0.0-beta.6-macOS-Intel.zip) (1 MB)
-   [Floe-Library-Inspector-v2.0.0-beta.6-Linux.tar.gz](https://github.com/floe-audio/Floe/releases/download/v2.0.0-beta.6/Floe-Library-Inspector-v2.0.0-beta.6-Linux.tar.gz) (6 MB)

```
Inspect a Floe sample library and dump structured information to stdout.Output includes library metadata, captured Lua print() output, parse errors,instruments, regions, IRs, and orphan/missing sample files. Defaults to JSON;use --format=lua for a loadable Lua chunk.Useful when developing or debugging sample libraries: it surfaces parse errorsalongside everything Floe actually loaded, so you can sanity-check the resultof your Lua script. The JSON output is also well-suited as input to AI agents.Examples (piping JSON output into jq):  # Check whether the library parsed and show any error:  floe-library-inspector ./my-lib | jq '.parse'  # Top-level library metadata (name, author, version, counts):  floe-library-inspector ./my-lib | jq '.library'  # List orphan and missing sample files:  floe-library-inspector ./my-lib | jq '.samples'  # List every instrument id and how many regions it has:  floe-library-inspector ./my-lib \    | jq '.instruments[] | {id, num_regions}'  # List every sample path referenced by an instrument:  floe-library-inspector ./my-lib \    | jq -r '.instruments[].regions[].sample_path'  # Show the key/velocity range of every region of an instrument:  floe-library-inspector ./my-lib \    | jq '.instruments[] | select(.id=="My Inst") | .regions[].trigger'  # List IR ids and their file paths:  floe-library-inspector ./my-lib | jq '.irs[] | {id, path}'Usage: floe-library-inspector [OPTIONS]Optional arguments:      --format <json|lua>         Output format: 'json' (default) or 'lua'. Lua output is a single `return { ... }`chunk loadable with Lua's load()/dofile().
```

## Creating high-quality samples

### Levels

It's important to ensure your audio samples have the the right levels. This makes browsing and switching samples in Floe a consistent, nice experience. Additionally, Floe offers the ability to layer sounds together; this process is more ergonomic when instruments have similar volume levels.

[Signet](https://github.com/SamWindell/Signet) can be a useful tool for changing the levels of your samples.

When changing the volume levels of a realistic multi-sampled instrument, you probably don't want to normalise each sample individually because part of the character of the instrument is its volume variation. Instead, you should change the gain of the instrument _as a whole_. Signet has features for this. It also has features for proportionally moving levels towards a target level. This allows you to keep some of the character of an instrument while nudging it towards a more consistent level.

Here are some guidelines for levels:

-   Each sample's peak level should be less than -3 dB.
-   Playing the instrument should barely cause Floe's peak meter to reach its yellow region. Another way to levels could be this: RMS levels for an instrument _as a whole_ should be around -18 dB. Play the instrument polyphonically and watch the RMS level. If the instrument is designed to be monophonic, then adjust for that.
-   The noise floor should be as low as possible: -60 dB is a good target. Use high-quality noise reduction tools to remove noise from your samples if you need to. Noise levels can quickly stack up with a multi-sampled instrument played polyphonically. Being too aggressive with algorithmic noise reduction can make your samples sound unnatural - so it's a balance.
-   Impulse responses should be normalised by their energy (power) levels: `signet my-impulses norm -100 --mode energy --independently && signet my-impulses norm 0`. Or if not using Signet, then adjust their levels so that they feel similar to the volume levels of Floe's built-in IRs.

### Sample rate, bit depth, and file format

Floe only supports FLAC and WAV files. We recommend using FLAC for your samples. It's lossless and can reduce the file size by 50% to 70% compared to WAV. Floe loads FLAC files very quickly.

We find 44.1 kHz and 16-bit is often a perfectly reasonable choice. 48 kHz and 24-bit might also be appropriate in certain cases. FLAC also supports bit-depths between 16 and 24 bits (such as 20-bit). Floe supports these too.

### Volume envelopes of samples

Floe blurs the line between a sampler and a sample-based synthesizer. It has lots of parameters for manipulating the sound in real-time.

If your sample is a single continuous sound, then don't include a fade-in or fade-out in the sample. Floe has a GUI for volume envelopes that offer more control: they can be adjusted to any value, automated by the DAW, and they are independent of the playback speed of the sample. If you have a sample that is stretched across a keyboard range, it will be sped-up or slowed-down in order to be the correct pitch. If there's a volume fade, then the speed of the fade will change depending on the pitch of the voice. This is not normally a desirable effect.

If your sound has important timbral variation over time, then don't cut that away. Only if the sound is a constant tone should you remove the fade in/out.

## Background images

Floe allows libraries to have a custom background image that appear behind the interface when the library is loaded. These images are configured in your [Lua script](/docs/beta/develop/library-lua-scripts) through Floe's `new_library` function.

Floe accepts any image. It uses "cover" scaling with top-left positioning — in other words, it scales up the image (maintaining its aspect ratio) until it covers the entire background of Floe. However, for optimal results consider these characteristics:

-   Roughly 16:9 aspect ratio to match the plugin GUI proportions
-   Approximately 1500 pixels wide as a starting point (consider high-DPI displays)
-   Consistent colour palettes and brightness levels throughout the image
-   Darker or mid-tone images rather than bright ones for better text readability
-   Avoid dramatic contrasts between very dark and very bright regions

The most critical consideration is interface readability. Since Floe's knobs, buttons, and text render on top of your background image, the image needs consistent tones to ensure controls remain clearly visible. While some variation is acceptable, remember that interface legibility is paramount.

Additionally, Floe has options to add a coloured vignette on the background image and an overlay tint to its blurred panels via options in the Lua code. Experiment with these options, starting with very transparent colours.
