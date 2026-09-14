We use a custom IMGUI system. There's 2 levels to it. The higher-level API is normally used. If you need more details, the .hpp files often contains lots more information in comments.

## The GUI system

Low-level:
- The IMGUI machinery is gui_imgui.hpp/cpp: button behaviour, 'hot' and 'active' tracking, viewport handling, scrollbars, provides a draw_list pointers, etc..
- The rectangle layout 'flexbox style' engine is layout.hpp/cpp: alignment, rows, columns, padding, etc..
- Drawing primitives: draw_list.hpp/cpp, fonts.hpp/cpp

Higher-level:
- The higher-level API is gui_builder.hpp/cpp. It builds on top of gui_imgui and layout to create 'boxes'. You create boxes using large config structs (great with C++ designated initializer syntax), and the box system handles the rest - running the boxes through 2 passes: once to build the layout, and a second time to handle user-interaction and rendering.

Unique IDs:
The every box needs a unique ID so that it can be tracked through its 2-pass system. Often the unique ID is deduced automatically, but you must always consider if it can't. If the ID isn't unique the will eventually program hit an assertion failure at runtime. The final unique ID that a box gets is hashed from a few different contexts:
- The loc_hash argument to DoBox (default set to SourceLocationHash())
- The id_extra field of BoxConfig
- The current ID pushed via imgui.PushId()/PopId()
- The ID of the box's .parent box

k_hug_contents and k_fill_parent:
Our layout system supports these handle constants to help more easily build UIs. Important though: when using the BoxConfig - k_hug_contents does NOT mean the .text field is measured and hugged, you must instead use .size_from_text field.

## Floe's GUI implementation

gui_state.hpp/cpp is the core structure of our GUI - its 'update' callback get triggered whenever there's user input, or whenever update-request have been given.

When developing, we typically run the standalone version of Floe - which loads the Floe CLAP plugin dynamically, and watches the plugin for changes. When zig build install:clap is run, the CLAP changes, and the standalone hot-reloads the module - allowing for a quick iteration of visual changes.

Floe's GUI has different base styles for different parts of the GUI. For modals and popups we use a light-mode style. For mid-panel, we use a dark-mode but translucent elements (because the mid-panel has a blurred background image that the GUI sits on). For the top and bottom panels, its dark-mode but without translucency. These are defined in gui_constants.

Floe's GUI can be scaled to any size (maintaining aspect ratio). As such, we use 'window width', WW, units sometimes. Within standard GUI code, we can use free functions WwToPixels and PixelsToWw.

Style guidelines:
- Use colours from the defined set: colours.hpp (ToU32)
- Consider beautiful, minimal, UX-friendly design
- Use the existing font sizes - we rare can use smaller fonts because the become illegible
- For sizes, 2 options: use a standard size from gui_constants.hpp; or if we need something custom, just use a inline number literal. ONLY refactor it out to a constexpr variable if the number literal is used in multiple places.
