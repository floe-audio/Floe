local library = floe.new_library({
    name = "Test Lua",
    tagline = "Tagline",
    author = "Tester",
    minor_version = 2,
})

-- ================================================================================
local single_sample = floe.new_instrument(library, {
    name = "Single Sample",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})

floe.add_region(single_sample, {
    root_key = 60,
    path = "Samples/a.flac",
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 0, 128 },
        velocity_range = { 0, 100 },
    },
})

-- ================================================================================
local same_sample_twice = floe.new_instrument(library, {
    name = "Same Sample Twice",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})

floe.add_region(same_sample_twice, {
    root_key = 30,
    path = "Samples/a.flac",
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 0, 60 },
        velocity_range = { 0, 100 },
    },
})
floe.add_region(same_sample_twice, {
    root_key = 60,
    path = "Samples/a.flac",
    trigger_criteria = {
        trigger_event = "note-on",
        key_range = { 60, 128 },
        velocity_range = { 0, 100 },
    },
})

-- ================================================================================
local auto_mapped_samples = floe.new_instrument(library, {
    name = "Auto Mapped Samples",
    folders = "Folder",
    description = "Description",
    tags = {},
    waveform_audio_path = "Samples/a.flac",
})
local auto_map_config = {
    {
        file = 'a',
        root = 20,
    },
    {
        file = 'b',
        root = 40,
    },
    {
        file = 'c',
        root = 60,
    },
    {
        file = 'd',
        root = 80,
    },
}
for _, config in pairs(auto_map_config) do
    floe.add_region(auto_mapped_samples, {
        root_key = config.root,
        path = "Samples/" .. config.file .. ".flac",
        trigger_criteria = {
            trigger_event = "note-on",
            velocity_range = { 0, 100 },
            auto_map_key_range_group = "group1",
        },
    })
end

-- ================================================================================
-- 2 seconds of 4/4 at 120 BPM, one slice per beat.
local drum_loop = floe.new_instrument(library, {
    name = "Drum Loop",
    folders = "Loops",
    description = "Description",
    tags = {},
})

floe.add_region(drum_loop, {
    root_key = 60,
    path = "Samples/drum-loop.flac",
    slices = {
        { start_frame = 0, length_proportion = 1 },
        { start_frame = 22050, length_proportion = 1 },
        { start_frame = 44100, length_proportion = 1 },
        { start_frame = 66150, length_proportion = 1 },
    },
    loop_beats = 4,
    native_bpm = 120,
    loop = {
        loop_requirement = "never-loop",
    },
})

return library
