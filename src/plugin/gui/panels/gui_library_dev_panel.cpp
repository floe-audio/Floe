// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/panels/gui_library_dev_panel.hpp"

#include <lauxlib.h>
#include <lua.h>

#include "common_infrastructure/common_errors.hpp"
#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/state/instrument.hpp"

#include "engine/engine.hpp"
#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/overlays/gui_notifications.hpp"
#include "gui/panels/gui_save_preset_panel.hpp"
#include "gui_framework/gui_builder.hpp"

#define GENERATED_TAGS_FILENAME   "Lua/instrument_tags.lua"
#define GENERATED_TUNING_FILENAME "Lua/instrument_tuning_corrections.lua"

static void DoUtilitiesPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState&) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    if (TextButton(
            builder,
            root,
            {
                .text = "Install Lua definitions",
                .tooltip =
                    "Generate Lua LSP definitions for Floe's API - used for autocompletion and diagnostics when editing floe.lua files"_s,
            })) {
        auto const path = sample_lib::LuaDefinitionsFilepath(builder.arena);

        auto const try_install = [&]() -> ErrorCodeOr<void> {
            auto file = TRY(OpenFile(path, FileMode::Write()));
            TRY(sample_lib::WriteLuaLspDefintionsFile(file.Writer()));
            return k_success;
        };

        auto const outcome = try_install();
        auto const notification_id = SourceLocationHash();
        if (outcome.Succeeded()) {
            context.notifications.AddOrUpdate(notification_id,
                                              [p = DynamicArrayBounded<char, 200>(path)](ArenaAllocator&) {
                                                  return NotificationDisplayInfo {
                                                      .title = "Installed Lua definitions",
                                                      .message = p,
                                                      .dismissable = true,
                                                      .icon = NotificationDisplayInfo::IconType::Success,
                                                  };
                                              });
        } else {
            context.notifications.AddOrUpdate(notification_id,
                                              [error = outcome.Error()](ArenaAllocator& arena) {
                                                  return NotificationDisplayInfo {
                                                      .title = "Error installing Lua definitions",
                                                      .message = fmt::Format(arena, "{}", error),
                                                      .dismissable = true,
                                                      .icon = NotificationDisplayInfo::IconType::Error,
                                                  };
                                              });
        }
    }

    if (TextButton(builder,
                   root,
                   {.text = "Copy Lua definitions path",
                    .tooltip = "Copy the path to the Lua definitions file to the clipboard"_s})) {
        auto const path = sample_lib::LuaDefinitionsFilepath(builder.arena);
        dyn::Assign(GuiIo().out.set_clipboard_text, path);
        auto const notification_id = SourceLocationHash();
        context.notifications.AddOrUpdate(notification_id,
                                          [p = DynamicArrayBounded<char, 200>(path)](ArenaAllocator&) {
                                              return NotificationDisplayInfo {
                                                  .title = "Copied to clipboard",
                                                  .message = p,
                                                  .dismissable = true,
                                                  .icon = NotificationDisplayInfo::IconType::Success,
                                              };
                                          });
    }
}

using TagsArray = DynamicArrayBounded<DynamicArrayBounded<char, k_max_tag_size>, k_max_num_tags>;
using TagsByInstrument = OrderedHashTable<String, OrderedSet<String>>;

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
    //  ["instrument ID"] = { "tag1", "tag2", ... }
    //  ["another instrument ID"] = { "tag3", "tag4", ... }
    // }
    auto tags = TagsByInstrument::Create(arena, 16);

    ErrorCodeOr<void> error_code = k_success;

    IterateTableAtTop(lua, [&]() {
        if (error_code.HasError()) return;

        // key is at -2 and value is at -1

        // We expect the key to be a string (the instrument ID).
        // We expect the value to be a table (the tags).

        if (!lua_isstring(lua, -2) || !lua_istable(lua, -1)) {
            error_message = "Expected a string key and a table value"_s;
            error_code = ErrorCode {CommonError::InvalidFileFormat};
            return;
        }

        auto const instrument_id = LuaString(lua, -2);
        if (!IsValidUtf8(instrument_id) || instrument_id.size > k_max_instrument_name_size) {
            error_message = "invalid instrument name"_s;
            error_code = ErrorCode {CommonError::InvalidFileFormat};
            return;
        }

        auto& inst_tags = tags.FindOrInsertGrowIfNeeded(arena, arena.Clone(instrument_id), {}).element.data;

        // Now we need to iterate over the tags in the value table.
        IterateTableAtTop(lua, [&]() {
            if (error_code.HasError()) return;

            // We expect the value to be a string (the tag).
            if (!lua_isstring(lua, -1)) {
                error_message = "Expected a string tag"_s;
                error_code = ErrorCode {CommonError::InvalidFileFormat};
                return;
            }

            auto const tag = LuaString(lua, -1);
            if (!IsValidUtf8(tag) || tag.size > k_max_tag_size) {
                error_message = "invalid tag"_s;
                error_code = ErrorCode {CommonError::InvalidFileFormat};
                return;
            }

            inst_tags.InsertGrowIfNeeded(arena, arena.Clone(tag));
        });
    });

    lua_pop(lua, 1); // Pop the table

    if (error_code.HasError()) return error_code.Error();

    return tags;
}

static ErrorCodeOr<void>
WriteTagsFile(TagsByInstrument const& tags, sample_lib::Library const& library, ArenaAllocator& arena) {
    auto const temp_suffix = ".tmp"_ca;
    auto const temp_path = path::Join(
        arena,
        Array {*path::Directory(library.path), ConcatArrays(GENERATED_TAGS_FILENAME ""_ca, temp_suffix)});
    auto const path = temp_path.SubSpan(0, temp_path.size - temp_suffix.size);

    TRY(CreateDirectory(*path::Directory(path), {.fail_if_exists = false}));

    // Write to a temp file then move it to the final path to avoid issues with incomplete writes.

    {
        auto file = TRY(OpenFile(temp_path, FileMode::Write()));

        BufferedWriter<Kb(4)> buffered_writer {
            .unbuffered_writer = file.Writer(),
        };
        DEFER { buffered_writer.Reset(); };
        auto writer = buffered_writer.Writer();

        TRY(fmt::FormatToWriter(
            writer,
            "-- This file is generated by Floe's tag builder.\n-- Keys are instrument IDs.\nreturn {{\n"));

        for (auto const& [instrument_id, tags_set, _] : tags) {
            if (tags_set.size == 0) continue;
            ASSERT(IsValidUtf8(instrument_id));
            TRY(fmt::FormatToWriter(writer, "  [\"{}\"] = {{ ", instrument_id));
            for (auto const& [tag, _] : tags_set) {
                ASSERT(IsValidUtf8(tag));
                TRY(fmt::FormatToWriter(writer, "\"{}\", ", tag));
            }
            TRY(fmt::FormatToWriter(writer, "}},\n"));
        }

        TRY(fmt::FormatToWriter(writer, "}}\n"));

        TRY(buffered_writer.Flush());
        TRY(file.Flush());
    }

    TRY(Rename(temp_path, path));

    return k_success;
}

static void DoTagBuilderPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState&) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(
        builder,
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
    auto tags_result = LoadExistingTagsFile(inst.instrument, builder.arena, error_message);
    if (tags_result.HasError()) {
        context.notifications.AddOrUpdate(
            SourceLocationHash(),
            [error = tags_result.Error(),
             msg = DynamicArrayBounded<char, 200>(error_message)](ArenaAllocator& arena) {
                return NotificationDisplayInfo {
                    .title = "Error loading tags file",
                    .message = fmt::Format(arena, "{}: {}", msg, error),
                    .dismissable = true,
                    .icon = NotificationDisplayInfo::IconType::Error,
                };
            });
        return;
    }

    auto& tags = tags_result.Value();

    TagsBitset this_inst_tags {};
    // Fill with tags from the existing tags file.
    if (auto i = tags.Find(inst.instrument.id)) {
        for (auto const [tag, _] : *i) {
            ASSERT(IsValidUtf8(tag));
            if (auto const t = LookupTagName(tag)) this_inst_tags.Set(ToInt(t->tag));
        }
    }

    if (DoTagsGui(builder, this_inst_tags, root)) {
        // Update the tags for the changed instrument.
        auto& result = tags.FindOrInsertGrowIfNeeded(builder.arena, inst.instrument.id, {}).element.data;
        result.DeleteAll();
        this_inst_tags.ForEachSetBit([&](usize bit) {
            auto const tag_name = GetTagInfo((TagType)bit).name;
            result.InsertGrowIfNeeded(builder.arena, tag_name);
        });

        if (auto const o = WriteTagsFile(tags, inst.instrument.library, builder.arena); o.HasError()) {
            context.notifications.AddOrUpdate(SourceLocationHash(),
                                              [error = o.Error()](ArenaAllocator& arena) {
                                                  return NotificationDisplayInfo {
                                                      .title = "Error writing tags file",
                                                      .message = fmt::Format(arena, "{}", error),
                                                      .dismissable = true,
                                                      .icon = NotificationDisplayInfo::IconType::Error,
                                                  };
                                              });
        }
    }
}

struct InstrumentTuning {
    int semitones;
    f32 cents;
};
using TuningByInstrument = OrderedHashTable<String, InstrumentTuning>;

static ErrorCodeOr<TuningByInstrument>
LoadExistingTuningFile(sample_lib::Library const& library, ArenaAllocator& arena, String& error_message) {
    auto const path = path::Join(arena, Array {*path::Directory(library.path), GENERATED_TUNING_FILENAME});

    auto const data = ({
        auto const o = ReadEntireFile(path, arena);
        if (o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist) return TuningByInstrument {};
            return o.Error();
        }
        o.Value();
    });

    auto lua = luaL_newstate();
    DEFER { lua_close(lua); };

    if (auto const r = luaL_loadbuffer(lua, data.data, data.size, "tuning builder"); r != LUA_OK) {
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
    if (!table_index || !lua_istable(lua, table_index)) {
        error_message = "Expected a table as the result"_s;
        return ErrorCode {CommonError::InvalidFileFormat};
    }

    auto tuning = TuningByInstrument::Create(arena, 16);
    ErrorCodeOr<void> error_code = k_success;

    IterateTableAtTop(lua, [&]() {
        if (error_code.HasError()) return;

        if (!lua_isstring(lua, -2) || !lua_istable(lua, -1)) {
            error_message = "Expected a string key and a table value"_s;
            error_code = ErrorCode {CommonError::InvalidFileFormat};
            return;
        }

        auto const instrument_id = LuaString(lua, -2);
        if (!IsValidUtf8(instrument_id) || instrument_id.size > k_max_instrument_name_size) {
            error_message = "invalid instrument name"_s;
            error_code = ErrorCode {CommonError::InvalidFileFormat};
            return;
        }

        InstrumentTuning entry {};
        lua_getfield(lua, -1, "semitones");
        if (lua_isnumber(lua, -1)) entry.semitones = (int)lua_tointeger(lua, -1);
        lua_pop(lua, 1);
        lua_getfield(lua, -1, "cents");
        if (lua_isnumber(lua, -1)) entry.cents = (f32)lua_tonumber(lua, -1);
        lua_pop(lua, 1);

        tuning.FindOrInsertGrowIfNeeded(arena, arena.Clone(instrument_id), entry).element.data = entry;
    });

    lua_pop(lua, 1);

    if (error_code.HasError()) return error_code.Error();
    return tuning;
}

static ErrorCodeOr<void>
WriteTuningFile(TuningByInstrument const& tuning, sample_lib::Library const& library, ArenaAllocator& arena) {
    auto const temp_suffix = ".tmp"_ca;
    auto const temp_path = path::Join(
        arena,
        Array {*path::Directory(library.path), ConcatArrays(GENERATED_TUNING_FILENAME ""_ca, temp_suffix)});
    auto const path = temp_path.SubSpan(0, temp_path.size - temp_suffix.size);

    TRY(CreateDirectory(*path::Directory(path), {.fail_if_exists = false}));

    {
        auto file = TRY(OpenFile(temp_path, FileMode::Write()));

        BufferedWriter<Kb(4)> buffered_writer {.unbuffered_writer = file.Writer()};
        DEFER { buffered_writer.Reset(); };
        auto writer = buffered_writer.Writer();

        TRY(fmt::FormatToWriter(
            writer,
            "-- This file is generated by Floe's tuning builder.\n"
            "-- Keys are instrument IDs.\n"
            "-- semitones and cents are the CORRECTION to apply to bring the instrument in tune\n"
            "-- (already negated from the detected offset). Apply directly, e.g.:\n"
            "--   tune_cents = t.semitones * 100 + t.cents\n"
            "-- or, for a single-sample region:\n"
            "--   root_key = base_root - t.semitones, tune_cents = t.cents\n"
            "return {{\n"));

        for (auto const& [instrument_id, entry, _] : tuning) {
            ASSERT(IsValidUtf8(instrument_id));
            TRY(fmt::FormatToWriter(writer,
                                    "  [\"{}\"] = {{ semitones = {}, cents = {} }},\n",
                                    instrument_id,
                                    entry.semitones,
                                    entry.cents));
        }

        TRY(fmt::FormatToWriter(writer, "}}\n"));

        TRY(buffered_writer.Flush());
        TRY(file.Flush());
    }

    TRY(Rename(temp_path, path));
    return k_success;
}

static void
DoTuningBuilderPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState&) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_gap = k_default_spacing,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                    .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                },
                            });

    DoBox(builder,
          {
              .parent = root,
              .text =
                  "Build a pitch-correction table for instruments that play slightly out of tune. Setup: "
                  "load the instrument to correct into layer 1 (its Pitch and Detune must be 0), and load "
                  "the Built-in sine into layer 2. Adjust layer 2's Pitch and Detune until the sine matches "
                  "the instrument's pitch, then click Write tuning. The correction (i.e. the inverse of the "
                  "sine's offset) is appended to \"" GENERATED_TUNING_FILENAME
                  "\" in the library's folder, keyed by instrument ID, ready to be applied in floe.lua."_s,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
          });

    auto const& target_inst = context.engine.Layer(0).instrument;
    auto const& ref_inst = context.engine.Layer(1).instrument;
    auto& params = context.engine.processor.main_params;

    auto const target_is_sampler = target_inst.tag == InstrumentType::Sampler;
    sample_lib::Instrument const* target_inst_def = nullptr;
    if (target_is_sampler) target_inst_def = &(*target_inst.GetFromTag<InstrumentType::Sampler>()).instrument;

    auto const target_is_lua =
        target_inst_def && target_inst_def->library.file_format_specifics.tag == sample_lib::FileFormat::Lua;
    auto const target_cents = params.ProjectedValue(0, LayerParamIndex::TuneCents);
    auto const target_semis = params.ProjectedValue(0, LayerParamIndex::TuneSemitone);
    auto const target_tuning_zero = target_cents == 0 && target_semis == 0;

    auto const ref_is_sine =
        ref_inst.tag == InstrumentType::WaveformSynth && ref_inst.Get<WaveformType>() == WaveformType::Sine;

    auto const all_ok = target_is_lua && target_tuning_zero && ref_is_sine;

    auto const status_line = [&](String text, bool ok, u64 id_extra) {
        DoBox(builder,
              {
                  .parent = root,
                  .id_extra = id_extra,
                  .text = fmt::Format(builder.arena, "{} {}", ok ? "[OK]"_s : "[--]"_s, text),
                  .size_from_text = true,
              });
    };

    status_line("Layer 1: sampler instrument from a Lua sample library"_s, target_is_lua, 1);
    status_line("Layer 1: Pitch and Detune are both 0"_s, target_tuning_zero, 2);
    status_line("Layer 2: Built-in sine waveform"_s, ref_is_sine, 3);

    if (!all_ok) return;

    auto const ref_cents = params.ProjectedValue(1, LayerParamIndex::TuneCents);
    auto const ref_semis = (int)Round(params.ProjectedValue(1, LayerParamIndex::TuneSemitone));
    auto const total_cents = -(((f32)ref_semis * 100.0f) + ref_cents);
    auto const out_semitones = (int)Round(total_cents / 100.0f);
    auto const out_cents = total_cents - ((f32)out_semitones * 100.0f);

    DoBox(builder,
          {
              .parent = root,
              .text = fmt::Format(builder.arena,
                                  "Will write: semitones = {}, cents = {}",
                                  out_semitones,
                                  out_cents),
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
          });

    if (!TextButton(builder,
                    root,
                    {
                        .text = "Write tuning"_s,
                        .tooltip = "Write the negated reference offset to "_s GENERATED_TUNING_FILENAME,
                    }))
        return;

    auto const& library = target_inst_def->library;
    String error_message;
    auto tuning_result = LoadExistingTuningFile(library, builder.arena, error_message);
    if (tuning_result.HasError()) {
        context.notifications.AddOrUpdate(
            SourceLocationHash(),
            [error = tuning_result.Error(),
             msg = DynamicArrayBounded<char, 200>(error_message)](ArenaAllocator& arena) {
                return NotificationDisplayInfo {
                    .title = "Error loading tuning file",
                    .message = fmt::Format(arena, "{}: {}", msg, error),
                    .dismissable = true,
                    .icon = NotificationDisplayInfo::IconType::Error,
                };
            });
        return;
    }

    auto& tuning = tuning_result.Value();
    auto const& inst_id = target_inst_def->id;

    if (out_semitones == 0 && out_cents == 0) {
        tuning.Delete(inst_id);
    } else {
        auto& entry =
            tuning.FindOrInsertGrowIfNeeded(builder.arena, inst_id, InstrumentTuning {}).element.data;
        entry = {.semitones = out_semitones, .cents = out_cents};
    }

    if (auto const o = WriteTuningFile(tuning, library, builder.arena); o.HasError()) {
        context.notifications.AddOrUpdate(SourceLocationHash(), [error = o.Error()](ArenaAllocator& arena) {
            return NotificationDisplayInfo {
                .title = "Error writing tuning file",
                .message = fmt::Format(arena, "{}", error),
                .dismissable = true,
                .icon = NotificationDisplayInfo::IconType::Error,
            };
        });
    } else {
        context.notifications.AddOrUpdate(SourceLocationHash(), [](ArenaAllocator&) {
            return NotificationDisplayInfo {
                .title = "Wrote tuning",
                .message = GENERATED_TUNING_FILENAME ""_s,
                .dismissable = true,
                .icon = NotificationDisplayInfo::IconType::Success,
            };
        });
    }
}

static void DoPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(LibraryDevPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<LibraryDevPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case LibraryDevPanelState::Tab::TagBuilder:
                    tabs[index] = {.icon = ICON_FA_TAG, .text = "Tags"};
                    break;
                case LibraryDevPanelState::Tab::TuningBuilder:
                    tabs[index] = {.icon = ICON_FA_MUSIC, .text = "Tuning"};
                    break;
                case LibraryDevPanelState::Tab::Utilities:
                    tabs[index] = {.icon = ICON_FA_TOOLBOX, .text = "Utilities"};
                    break;
                case LibraryDevPanelState::Tab::Count: PanicIfReached();
            }
            tabs[index].index = index;
        }
        return tabs;
    }();

    auto const root = DoModal(builder,
                              {
                                  .title = "Library Developer Tools"_s,
                                  .modeless = &state.modeless,
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBuilder&, LibraryDevPanelContext&, LibraryDevPanelState&);
    DoBoxViewport(builder,
                  {
                      .run = ({
                          TabPanelFunction f {};
                          switch (state.tab) {
                              case LibraryDevPanelState::Tab::TagBuilder: f = DoTagBuilderPanel; break;
                              case LibraryDevPanelState::Tab::TuningBuilder: f = DoTuningBuilderPanel; break;
                              case LibraryDevPanelState::Tab::Utilities: f = DoUtilitiesPanel; break;
                              case LibraryDevPanelState::Tab::Count: PanicIfReached();
                          }
                          [f, &context, &state](GuiBuilder& builder) { f(builder, context, state); };
                      }),
                      .bounds = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          },
                                      }),
                      .imgui_id = builder.imgui.MakeId((u64)state.tab + 999999),
                      .viewport_config = k_default_modal_subviewport,
                  });
}

void DoLibraryDevPanel(GuiBuilder& builder, LibraryDevPanelContext& context, LibraryDevPanelState& state) {
    auto const is_open = builder.imgui.IsModalOpen(state.k_panel_id);
    // While the tag builder panel is open we want to disable file watching so that the instrument doesn't
    // reload with every change of tags.
    context.engine.shared_engine_systems.sample_library_server.disable_file_watching.Store(
        is_open && state.tab == LibraryDevPanelState::Tab::TagBuilder,
        StoreMemoryOrder::Relaxed);

    if (!is_open) return;

    f32x2 const size = {WwToPixels(350.0f), WwToPixels(570.0f)};
    auto const window_size = GuiIo().in.window_size.ToFloat2();
    f32x2 pos = 0;
    pos.x += window_size.x - size.x;
    pos.y += (window_size.y - size.y) / 2;

    DoBoxViewport(builder,
                  {
                      .run = [&context, &state](GuiBuilder& builder) { DoPanel(builder, context, state); },
                      .bounds = Rect {.pos = pos, .size = size},
                      .imgui_id = state.k_panel_id,
                      .viewport_config = ({
                          auto cfg = k_default_modal_viewport;
                          if (state.modeless) cfg.draw_background = DrawOverlayViewportBackground;
                          cfg.exclusive_focus = !state.modeless;
                          cfg.close_on_click_outside = !state.modeless;
                          cfg.close_on_escape = !state.modeless;
                          cfg;
                      }),
                  });
}
