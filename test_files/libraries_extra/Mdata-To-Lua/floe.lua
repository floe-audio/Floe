-- A newer version of shared_files_test_lib.mdata but in the Floe format for testing
-- updating a Mirage library to a compatible Floe library.

local library = floe.new_library({
    name = "Foo",
    tagline = "Tagline",
    author = "Tester",
    id = "SharedFilesMdata - FrozenPlain - OG", -- shared_files_test_lib.mdata ID
    minor_version = 2,
})

return library
