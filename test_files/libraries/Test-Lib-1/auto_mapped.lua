local M = {}

function M.add_instrument(library)
    print("Adding Auto Mapped Samples Instrument")
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
end

return M
