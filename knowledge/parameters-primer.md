Floe must keep backwards compatibility with all old parameters. Floe might be saved in DAW projections and any automatable parameter might have automations assigned to it. Some DAWs save this automation using the parameters true value, some DAWs save it using a normalised (0 to 1) representation of the parameter.

Therefore, parameters must NEVER:
- be removed
- have their min/max/projection change
- have new values added to a menu/enum - this changes the max

Instead, when we want to change a parameter, we must:
- Mark the existing parameter as legacy (enum, name, tooltip, flags) - param_descriptors.hpp
- Create a new parameter with our desired attributes
- Set its `added_in_generation` to one more than the highest shipped generation (needed to avoid breaking AUv2 automation)
- Consult what we need to change in legacy_param_logic.hpp/cpp file
- Add new version to StateVersion and handle the case to ensure old presets/DAW saves sound identical to before

The only occasion where we can modify the existing parameter (and reuse a StateVersion) is when the parameter has never been shipped (`gh release view --json name --jq .name`) and therefore no users will be effected.
