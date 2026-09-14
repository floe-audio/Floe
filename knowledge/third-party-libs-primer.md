Third party libs are typically dependencies pulled in by build.zig.zon. ZIG_GLOBAL_CACHE_DIR is set to <this_repo>/.zig-cache-global. Search within this cache (fd or find) to find the source files.

We have a couple of third party libs also in third_party_libs subfolder. These we have chosen to include as part of the source for full control.

We display license info on the GUI via k_third_party_licence_texts; this will need updating when deps change.
