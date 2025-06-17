local library = floe.new_library({
    name = "Test Lua",
    tagline = "Tagline",
    author = "Tester",
    background_image_path = "images/background.jpg",
    icon_image_path = "images/icon.png",
    minor_version = 1,
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
dofile("auto_mapped.lua").add_instrument(library)

-- test that scripts within subfolders are also loaded
assert(dofile("Scripts/script_in_folder.lua"))

return library
