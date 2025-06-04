// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui2_library_dev_panel.hpp"

#include <lauxlib.h>
#include <lua.h>

#include "engine/engine.hpp"
#include "gui/gui2_save_preset_panel.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui_framework/gui_box_system.hpp"

#define GENERATED_TAGS_FILENAME "Lua/instrument_tags.lua"

using TagsArray = DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags>;
using TagsByInstrument = HashTable<String, Set<String>>;

static String LuaString(lua_State* lua, int stack_index) {
    usize size {};
    auto ptr = lua_tolstring(lua, stack_index, &size);
    return {ptr, size};
}

static void IterateTableAtTop(lua_State* lua, auto&& table_pair_callback) {
    if (lua_checkstack(lua, 3) == false) luaL_error(lua, "out of memory");

    auto const table_index = lua_gettop(lua);
    lua_pushnil(lua); // first key
    while (lua_next(lua, table_index) != 0) {
        DEFER {
            // removes 'value'; keeps 'key' for next iteration
            lua_pop(lua, 1);
        };

        // 'key' (at index -2) and 'value' (at index -1)
        table_pair_callback();
    }
}

static ErrorCodeOr<TagsByInstrument>
LoadExistingTagsFile(sample_lib::Instrument const& inst, ArenaAllocator& arena, String& error_message) {
    auto const path = path::Join(arena, Array {*path::Directory(inst.library.path), GENERATED_TAGS_FILENAME});

    auto const data = ({
        auto const o = ReadEntireFile(path, arena);
        if (o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist) return TagsByInstrument {};
            return o.Error();
        }
        o.Value();
    });

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    if (auto const r = luaL_loadbuffer(lua, data.data, data.size, "tags builder"); r != LUA_OK) {
        if (lua_isstring(lua, -1))
            error_message = fmt::Format(arena, "{}", LuaString(lua, -1));
        else
            error_message = "unknown error"_s;
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    if (auto const r = lua_pcall(lua, 0, LUA_MULTRET, 0); r != LUA_OK) {
        if (lua_isstring(lua, -1))
            error_message = fmt::Format(arena, "{}", LuaString(lua, -1));
        else
            error_message = "unknown error"_s;
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    auto const table_index = lua_gettop(lua);

    // We're expecting the result to be a table.
    if (!table_index || !lua_istable(lua, table_index)) {
        error_message = "Expected a table as the result"_s;
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    // We now need to read the table and extract the tags. The table is expected to be like this: {
    //  ["instrument name"] = { "tag1", "tag2", ... }
    //  ["another instrument name"] = { "tag3", "tag4", ... }
    // }
    TagsByInstrument tags {};

    IterateTableAtTop(lua, [&]() {
        // key is at -2 and vlaue is at -1

        // We expect the key to be a string (the instrument name).
        // We expect the value to be a table (the tags).

        if (!lua_isstring(lua, -2) || !lua_istable(lua, -1)) {
            error_message = "Expected a string key and a table value"_s;
            return;
        }

        auto const instrument_name = arena.Clone(LuaString(lua, -2));

        auto& inst_tags = tags.FindOrInsertGrowIfNeeded(arena, instrument_name, Set<String> {}).element.data;

        // Now we need to iterate over the tags in the value table.
        IterateTableAtTop(lua, [&]() {
            // We expect the value to be a string (the tag).
            if (!lua_isstring(lua, -1)) {
                error_message = "Expected a string tag"_s;
                return;
            }

            auto const tag = arena.Clone(LuaString(lua, -1));
            inst_tags.InsertGrowIfNeeded(arena, tag);
        });
    });

    lua_pop(lua, 1); // Pop the table
    return tags;
}

static ErrorCodeOr<void> WriteTagsFile(TagsByInstrument const& tags,
                                       sample_lib::Instrument const& inst,
                                       TagsArray const& inst_tags,
                                       ArenaAllocator& arena) {
    auto const path = path::Join(arena, Array {*path::Directory(inst.library.path), GENERATED_TAGS_FILENAME});
    TRY(CreateDirectory(*path::Directory(path), {.fail_if_exists = false}));

    auto file = TRY(OpenFile(path, FileMode::Write()));

    BufferedWriter<Kb(4)> buffered_writer {
        .unbuffered_writer = file.Writer(),
    };
    auto writer = buffered_writer.Writer();

    TRY(fmt::FormatToWriter(writer, "return {{\n"));

    // The instrument that has changed
    TRY(fmt::FormatToWriter(writer, "  [\"{}\"] = {{ ", inst.name));
    for (auto const& tag : inst_tags)
        TRY(fmt::FormatToWriter(writer, "\"{}\", ", tag));
    TRY(fmt::FormatToWriter(writer, "}},\n"));

    // All other instruments
    for (auto const& [instrument_name, tags_set, _] : tags) {
        if (instrument_name == inst.name) continue;
        if (tags_set.size == 0) continue;
        TRY(fmt::FormatToWriter(writer, "  [\"{}\"] = {{ ", instrument_name));
        for (auto const& [tag, _] : tags_set)
            TRY(fmt::FormatToWriter(writer, "\"{}\", ", tag));
        TRY(fmt::FormatToWriter(writer, "}},\n"));
    }

    TRY(fmt::FormatToWriter(writer, "}}\n"));

    TRY(buffered_writer.Flush());
    TRY(file.Flush());

    return k_success;
}

static void
DoTagBuilderPanel(GuiBoxSystem& box_system, LibraryDevPanelContext& context, LibraryDevPanelState&) {
    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = style::k_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(
        box_system,
        {
            .parent = root,
            .text =
                "Select tags for the 1st layer's instrument. These are written to \"" GENERATED_TAGS_FILENAME
                "\" in the library's folder. Use this file when doing floe.new_instrument().",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    if (context.engine.Layer(0).instrument.tag != InstrumentType::Sampler) return;

    auto const& inst = *context.engine.Layer(0).instrument.GetFromTag<InstrumentType::Sampler>();

    if (inst.instrument.library.file_format_specifics.tag != sample_lib::FileFormat::Lua) return;

    String error_message;
    auto const tags = TRY_OR(LoadExistingTagsFile(inst.instrument, box_system.arena, error_message), {
        DoBox(box_system,
              {
                  .parent = root,
                  .text =
                      fmt::Format(box_system.arena, "Error loading tags file: {}, {}", error, error_message),
                  .wrap_width = k_wrap_to_parent,
                  .text_fill = style::Colour::Red,
                  .size_from_text = true,
              });
        return;
    });

    TagsArray this_inst_tags {};
    if (auto i = tags.Find(inst.instrument.name))
        for (auto const [tag, _] : *i)
            dyn::AppendIfNotAlreadyThere(this_inst_tags, tag);
    for (auto const [tag, _] : inst.instrument.tags)
        dyn::AppendIfNotAlreadyThere(this_inst_tags, tag);

    auto const initial_size = this_inst_tags.size;
    DoTagsGui(box_system, this_inst_tags, root);

    if (this_inst_tags.size != initial_size)
        auto _ = WriteTagsFile(tags, inst.instrument, this_inst_tags, box_system.arena);
}

static void DoPanel(GuiBoxSystem& box_system, LibraryDevPanelContext& context, LibraryDevPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(LibraryDevPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<LibraryDevPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case LibraryDevPanelState::Tab::TagBuilder:
                    tabs[index] = {.icon = ICON_FA_TAG, .text = "Tag Builder"};
                    break;
                case LibraryDevPanelState::Tab::Count: PanicIfReached();
            }
            tabs[index].index = index;
        }
        return tabs;
    }();

    auto const root = DoModal(box_system,
                              {
                                  .title = "Library Developer Panel"_s,
                                  .on_close = [&state] { state.open = false; },
                                  .modeless = &state.modeless,
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBoxSystem&, LibraryDevPanelContext&, LibraryDevPanelState&);
    AddPanel(box_system,
             Panel {
                 .run = ({
                     TabPanelFunction f {};
                     switch (state.tab) {
                         case LibraryDevPanelState::Tab::TagBuilder: f = DoTagBuilderPanel; break;
                         case LibraryDevPanelState::Tab::Count: PanicIfReached();
                     }
                     [f, &context, &state](GuiBoxSystem& box_system) { f(box_system, context, state); };
                 }),
                 .data =
                     Subpanel {
                         .id = DoBox(box_system,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_fill_parent},
                                         },
                                     })
                                   .layout_id,
                         .imgui_id = box_system.imgui.GetID((u64)state.tab + 999999),
                     },
             });
}

void DoLibraryDevPanel(GuiBoxSystem& box_system,
                       LibraryDevPanelContext& context,
                       LibraryDevPanelState& state) {
    // While the tag builder panel is open we want to disable file watching so that the instrument doesn't
    // reload with every change of tags.
    context.engine.shared_engine_systems.sample_library_server.disable_file_watching.Store(
        state.open && state.tab == LibraryDevPanelState::Tab::TagBuilder,
        StoreMemoryOrder::Relaxed);

    if (!state.open) return;

    f32x2 const size = {box_system.imgui.VwToPixels(350), box_system.imgui.VwToPixels(570)};
    auto const window_size = box_system.imgui.frame_input.window_size.ToFloat2();
    f32x2 pos = 0;
    pos.x += window_size.x - size.x;
    pos.y += (window_size.y - size.y) / 2;

    RunPanel(box_system,
             Panel {
                 .run = [&context, &state](GuiBoxSystem& box_system) { DoPanel(box_system, context, state); },
                 .data =
                     ModalPanel {
                         .r = {.pos = pos, .size = size},
                         .imgui_id = box_system.imgui.GetID("libdev-panel"),
                         .on_close = [&state]() { state.open = false; },
                         .close_on_click_outside = !state.modeless,
                         .darken_background = !state.modeless,
                         .disable_other_interaction = !state.modeless,
                     },
             });
}
