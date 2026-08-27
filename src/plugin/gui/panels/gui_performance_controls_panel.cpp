// Copyright 2025-2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_performance_controls_panel.hpp"

#include <IconsFontAwesome6.h>

#include "utils/logger/logger.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/performance_profile.hpp"

#include "gui/elements/gui_constants.hpp"
#include "gui/elements/gui_modal.hpp"
#include "gui/elements/gui_popup_menu.hpp"
#include "gui_framework/gui_builder.hpp"
#include "gui_framework/layout.hpp"
#include "processing_utils/midi.hpp"
#include "processor/processor.hpp"

constexpr f32 k_cc_col_width = 60.0f;
constexpr f32 k_icon_col_width = 25.0f;
constexpr f32 k_table_row_height = 20.0f;
constexpr f32 k_field_width = 60.0f;

static Box TableRow(GuiBuilder& builder, Box parent, u64 id_extra = SourceLocationHash()) {
    return DoBox(builder,
                 {
                     .parent = parent,
                     .id_extra = id_extra,
                     .layout {
                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                         .contents_gap = 0,
                         .contents_direction = layout::Direction::Row,
                         .contents_align = layout::Alignment::Start,
                         .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                     },
                 });
}

static void TableHeaderText(GuiBuilder& builder,
                            Box parent,
                            String text,
                            f32 width,
                            TextJustification justify = TextJustification::CentredLeft,
                            u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .font = FontType::Body,
              .text_colours = Col {.c = Col::Subtext0},
              .text_justification = justify,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {width, k_font_body_size},
              },
          });
}

static void
TableCellText(GuiBuilder& builder, Box parent, String text, f32 width, u64 id_extra = SourceLocationHash()) {
    DoBox(builder,
          {
              .parent = parent,
              .id_extra = id_extra,
              .text = text,
              .text_justification = TextJustification::CentredLeft,
              .text_overflow = TextOverflowType::ShowDotsOnRight,
              .layout {
                  .size = {width, k_font_body_size},
              },
          });
}

static String NameErrorString(perf_profile::NameError error) {
    switch (error) {
        case perf_profile::NameError::Empty: return "Name can't be empty"_s;
        case perf_profile::NameError::TooLong: return "Name is too long"_s;
        case perf_profile::NameError::InvalidCharacters: return "Name contains invalid characters"_s;
        case perf_profile::NameError::AlreadyExists: return "A profile with this name already exists"_s;
    }
    PanicIfReached();
}

static void ClearTextEntry(PerformanceControlsPanelState& state) {
    state.text_entry_mode = PerformanceControlsPanelState::TextEntryMode::None;
    dyn::Clear(state.name_input);
    dyn::Clear(state.rename_target);
    state.name_error = k_nullopt;
}

// The body of the profile dropdown while naming a new profile or renaming the selected one. Replaces the
// profile list + actions until submitted or cancelled.
static void TextEntryRow(GuiBuilder& builder,
                         PerformanceControlsPanelContext& context,
                         PerformanceControlsPanelState& state,
                         Box root) {
    bool const is_rename = state.text_entry_mode == PerformanceControlsPanelState::TextEntryMode::Rename;

    auto const container = DoBox(builder,
                                 {
                                     .parent = root,
                                     .layout {
                                         .size = {layout::k_fill_parent, layout::k_hug_contents},
                                         .contents_padding = {.lr = k_menu_item_padding_x,
                                                              .t = k_small_gap,
                                                              .b = k_menu_item_padding_y},
                                         .contents_gap = k_small_gap,
                                         .contents_direction = layout::Direction::Column,
                                         .contents_align = layout::Alignment::Start,
                                     },
                                 });

    DoBox(builder,
          {
              .parent = container,
              .text = is_rename ? "Rename profile"_s : "Save current settings as a new profile"_s,
              .size_from_text = true,
              .font = FontType::BodyItalic,
              .text_colours = Col {.c = Col::Subtext0},
          });

    auto const row = DoBox(builder,
                           {
                               .parent = container,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_gap = k_small_gap,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    bool submit = false;
    bool cancel = false;

    auto const input = TextInput(builder,
                                 row,
                                 {
                                     .text = state.name_input,
                                     .tooltip = "Profile name"_s,
                                     .size = f32x2 {200, k_font_body_size * 1.3f},
                                 });
    if (input.result) {
        if (input.result->buffer_changed) {
            dyn::AssignFitInCapacity(state.name_input, input.result->text);
            state.name_error = k_nullopt;
        }
        if (input.result->enter_pressed) submit = true;
    }

    if (TextButton(builder, row, {.text = is_rename ? "Rename"_s : "Save"_s, .is_default = true}))
        submit = true;

    if (TextButton(builder, row, {.text = "Cancel"_s})) cancel = true;

    if (cancel) {
        ClearTextEntry(state);
        builder.imgui.CloseTopPopupOnly();
    }

    if (submit) {
        auto const trimmed = WhitespaceStripped((String)state.name_input);
        auto const ignore = is_rename ? Optional<String> {state.rename_target} : k_nullopt;
        if (auto const error = perf_profile::ValidateProfileName(context.prefs, trimmed, ignore)) {
            state.name_error = error;
        } else if (is_rename) {
            perf_profile::RenameProfile(context.prefs, state.rename_target, trimmed);
            ClearTextEntry(state);
            builder.imgui.CloseTopPopupOnly();
        } else {
            auto profile = CaptureCurrentPerformanceProfile(context.processor);
            dyn::AssignFitInCapacity(profile.name, trimmed);
            perf_profile::SaveProfile(context.prefs, profile);
            ClearTextEntry(state);
            builder.imgui.CloseTopPopupOnly();
        }
    }

    if (state.name_error) {
        DoBox(builder,
              {
                  .parent = container,
                  .text = NameErrorString(*state.name_error),
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Red},
              });
    }
}

// One row in the profile list: click the name to load that profile into the current instance. The icon
// buttons manage the profile itself without ever needing to know which profile is currently "active" — there
// is no such notion; the current instance settings are always unnamed.
static void ProfileRow(GuiBuilder& builder,
                       PerformanceControlsPanelContext& context,
                       PerformanceControlsPanelState& state,
                       Box root,
                       String name) {
    builder.imgui.PushId(Hash(name));
    DEFER { builder.imgui.PopId(); };

    auto const row = DoBox(builder,
                           {
                               .parent = root,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    if (DoBox(builder,
              {
                  .parent = row,
                  .text = name,
                  .font = FontType::Body,
                  .text_justification = TextJustification::CentredLeft,
                  .text_overflow = TextOverflowType::ShowDotsOnRight,
                  .background_fill_auto_hot_active_overlay = true,
                  .layout {
                      .size = {layout::k_fill_parent, k_table_row_height},
                      .margins = {.l = k_menu_item_padding_x},
                  },
                  .tooltip = "Load this profile, applying it to the current instance"_s,
                  .button_behaviour = imgui::ButtonConfig {.closes_popup_or_modal = true},
              })
            .button_fired) {
        if (auto const profile = perf_profile::LookupProfile(context.prefs, name))
            ApplyPerformanceProfile(context.processor, *profile);
    }

    if (IconButton(builder,
                   row,
                   ICON_FA_FLOPPY_DISK,
                   "Overwrite this profile with the current settings"_s,
                   k_font_icons_size * 0.8f,
                   f32x2 {k_icon_col_width, k_table_row_height},
                   SourceLocationHash(),
                   true)
            .button_fired) {
        auto cloned_name = Malloc::Instance().Clone(name);

        dyn::AssignFitInCapacity(context.confirmation_dialog_state.title, "Overwrite Profile"_s);
        fmt::Assign(
            context.confirmation_dialog_state.body_text,
            "Are you sure you want to overwrite the profile '{}' with the current settings? This can't be undone.",
            name);
        context.confirmation_dialog_state.callback = [&prefs = context.prefs,
                                                      &processor = context.processor,
                                                      cloned_name](ConfirmationDialogResult result) {
            DEFER { Malloc::Instance().Free(cloned_name.ToByteSpan()); };
            if (result == ConfirmationDialogResult::Ok) {
                auto profile = CaptureCurrentPerformanceProfile(processor);
                dyn::AssignFitInCapacity(profile.name, cloned_name);
                perf_profile::SaveProfile(prefs, profile);
            }
        };
        builder.imgui.OpenModalViewport(context.confirmation_dialog_state.k_id);
    }

    if (IconButton(builder,
                   row,
                   ICON_FA_PEN,
                   "Rename this profile"_s,
                   k_font_icons_size * 0.8f,
                   f32x2 {k_icon_col_width, k_table_row_height})
            .button_fired) {
        state.text_entry_mode = PerformanceControlsPanelState::TextEntryMode::Rename;
        dyn::AssignFitInCapacity(state.rename_target, name);
        dyn::AssignFitInCapacity(state.name_input, name);
        state.name_error = k_nullopt;
    }

    if (IconButton(builder,
                   row,
                   ICON_FA_TRASH,
                   "Delete this profile"_s,
                   k_font_icons_size * 0.8f,
                   f32x2 {k_icon_col_width, k_table_row_height},
                   SourceLocationHash(),
                   true)
            .button_fired) {
        auto cloned_name = Malloc::Instance().Clone(name);

        dyn::AssignFitInCapacity(context.confirmation_dialog_state.title, "Delete Profile"_s);
        fmt::Assign(context.confirmation_dialog_state.body_text,
                    "Are you sure you want to delete the profile '{}'? This can't be undone.",
                    name);
        context.confirmation_dialog_state.callback = [&prefs = context.prefs,
                                                      cloned_name](ConfirmationDialogResult result) {
            DEFER { Malloc::Instance().Free(cloned_name.ToByteSpan()); };
            if (result == ConfirmationDialogResult::Ok) perf_profile::DeleteProfile(prefs, cloned_name);
        };
        builder.imgui.OpenModalViewport(context.confirmation_dialog_state.k_id);
    }
}

// The full content of the header's profile dropdown: pick a profile to apply, or manage profiles. Profiles
// are deliberately tucked away here rather than given their own panel section — picking one is occasional,
// not the primary reason someone opens this panel.
static void ProfileMenuContent(GuiBuilder& builder,
                               PerformanceControlsPanelContext& context,
                               PerformanceControlsPanelState& state) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_hug_contents,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    if (state.text_entry_mode != PerformanceControlsPanelState::TextEntryMode::None) {
        TextEntryRow(builder, context, state, root);
        return;
    }

    auto const names = perf_profile::ListProfileNames(context.prefs, builder.arena);

    if (names.size == 0) {
        DoBox(builder,
              {
                  .parent = root,
                  .text = "No profiles yet"_s,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Subtext0},
                  .layout {
                      .margins = {.lr = k_menu_item_padding_x, .tb = k_menu_item_padding_y},
                  },
              });
    } else {
        for (auto const name : names)
            ProfileRow(builder, context, state, root, name);
    }

    MenuDivider(builder, root);

    if (MenuItem(builder, root, {.text = "Save current settings as new profile…"_s, .close_on_click = false})
            .button_fired) {
        state.text_entry_mode = PerformanceControlsPanelState::TextEntryMode::SaveAsNew;
        dyn::Clear(state.name_input);
        state.name_error = k_nullopt;
    }
}

// Compact profile picker shown in the panel header, next to the title.
static void HeaderProfileControl(GuiBuilder& builder,
                                 Box parent,
                                 PerformanceControlsPanelContext& context,
                                 PerformanceControlsPanelState& state) {
    auto const popup_id = builder.imgui.MakeId("perf-profile-picker"_s);

    auto const btn = MenuOpenButton(builder,
                                    parent,
                                    {
                                        .text = "Profiles"_s,
                                        .tooltip = "Load, save, or manage performance profiles"_s,
                                        .width = 100,
                                    });
    if (btn.button_fired) builder.imgui.OpenPopupMenu(popup_id, btn.imgui_id);

    if (builder.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(builder,
                      {
                          .run = [&context, &state](
                                     GuiBuilder& builder) { ProfileMenuContent(builder, context, state); },
                          .bounds = btn,
                          .imgui_id = popup_id,
                          .viewport_config = k_default_popup_menu_viewport,
                      });
}

static void ReproducibilityTab(GuiBuilder& builder, PerformanceControlsPanelContext& context) {
    auto settings = context.processor.performance_settings.Load(LoadMemoryOrder::Relaxed);
    auto const initial_settings = settings;

    auto const controls = DoBox(builder,
                                {
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.lrtb = k_default_spacing},
                                        .contents_gap = 14,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

    if (CheckboxButton(
            builder,
            controls,
            "Reset on transport"_s,
            settings.reset_on_transport,
            "Reset round robin positions and random sequences when the DAW transport starts playing"_s))
        settings.reset_on_transport = !settings.reset_on_transport;

    {
        auto const row = DoBox(builder,
                               {
                                   .parent = controls,
                                   .layout {
                                       .size = {layout::k_fill_parent, layout::k_hug_contents},
                                       .contents_gap = k_medium_gap,
                                       .contents_direction = layout::Direction::Row,
                                       .contents_align = layout::Alignment::Start,
                                       .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                                   },
                               });

        bool const keyswitch_enabled = settings.reset_keyswitch.HasValue();

        if (CheckboxButton(builder,
                           row,
                           "Reset keyswitch"_s,
                           keyswitch_enabled,
                           "Enable a MIDI note that resets round robin positions and random sequences"_s)) {
            if (keyswitch_enabled)
                settings.reset_keyswitch = k_nullopt;
            else
                settings.reset_keyswitch = (u7)0;
        }

        bool const ks_active = settings.reset_keyswitch.HasValue();
        auto const keyswitch_value = (s64)settings.reset_keyswitch.ValueOr((u7)0);

        if (auto const v = IntField(builder,
                                    row,
                                    {
                                        .tooltip = "MIDI note that triggers a reset"_s,
                                        .width = k_field_width,
                                        .value = keyswitch_value,
                                        .constrainer = [](s64 value) { return Clamp<s64>(value, 0, 127); },
                                        .midi_note_names = true,
                                        .greyed_out = !ks_active,
                                    })) {
            settings.reset_keyswitch = (u7)*v;
        }
    }

    if (auto const v = IntField(
            builder,
            controls,
            {
                .label = "Seed",
                .tooltip =
                    "Different seeds produce different round robin starting positions and random sequences"_s,
                .width = k_field_width,
                .value = settings.seed,
                .constrainer = [](s64 value) { return Clamp(value, (s64)0, (s64)99); },
            })) {
        settings.seed = (u8)*v;
    }

    if (settings != initial_settings) context.processor.performance_settings.Store(settings, StoreMemoryOrder::Release);
}

static void MpeTab(GuiBuilder& builder, PerformanceControlsPanelContext& context) {
    auto settings = context.processor.performance_settings.Load(LoadMemoryOrder::Relaxed);
    auto const initial_settings = settings;

    auto const controls = DoBox(builder,
                                {
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.lrtb = k_default_spacing},
                                        .contents_gap = 14,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });

    if (CheckboxButton(builder,
                       controls,
                       "MPE (MIDI Polyphonic Expression)"_s,
                       settings.mpe_enabled,
                       "Interpret incoming MIDI as MPE (MIDI Polyphonic Expression): each note gets "
                       "per-note pitch bend, pressure ('press') and CC74 ('slide')"_s))
        settings.mpe_enabled = !settings.mpe_enabled;

    if (auto const v = IntField(
            builder,
            controls,
            {
                .label = "MPE smoothing (ms)"_s,
                .tooltip =
                    "How gradually per-note press and slide changes are applied. Lower is more responsive; higher is smoother"_s,
                .width = k_field_width,
                .value = settings.mpe_smoothing_ms,
                .constrainer = [](s64 value) { return Clamp<s64>(value, 0, 500); },
                .greyed_out = !settings.mpe_enabled,
            })) {
        settings.mpe_smoothing_ms = (u16)*v;
    }

    DoBox(builder,
          {
              .parent = controls,
              .text = "Configure what press and slide control on each layer's CONFIG page."_s,
              .wrap_width = k_wrap_to_parent,
              .size_from_text = true,
              .font = FontType::BodyItalic,
              .text_colours = Col {.c = Col::Subtext0},
          });

    if (settings != initial_settings) context.processor.performance_settings.Store(settings, StoreMemoryOrder::Release);
}

// Defined further below; forward-declared here so the table content can append the add-assignment row/button
// as the last row of the table.
static void AddCcAssignmentRow(GuiBuilder& builder,
                               PerformanceControlsPanelContext& context,
                               PerformanceControlsPanelState& state,
                               Box parent);

static void MidiCcTableContent(GuiBuilder& builder,
                               PerformanceControlsPanelContext& context,
                               PerformanceControlsPanelState& state) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding =
                                        {
                                            .tb = 6,
                                        },
                                    .contents_gap = 0,
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    bool has_any_assignments = false;

    for (auto const param_index_int : Range<u16>(k_num_parameters)) {
        auto const param_index = (ParamIndex)param_index_int;
        auto const& descriptor = k_param_descriptors[param_index_int];

        if (descriptor.flags.not_automatable) continue;

        auto const ccs_bitset = GetLearnedCCsBitsetForParam(context.processor, param_index);
        if (!ccs_bitset.AnyValuesSet()) continue;

        for (auto const cc_num : Range(128uz)) {
            if (!ccs_bitset.Get(cc_num)) continue;

            has_any_assignments = true;

            builder.imgui.PushId(((u64)param_index_int * 128) + cc_num);
            DEFER { builder.imgui.PopId(); };

            auto const row = TableRow(builder, root);

            {
                auto const cc_container = DoBox(builder,
                                                {
                                                    .parent = row,
                                                    .layout {
                                                        .size = {k_cc_col_width, layout::k_hug_contents},
                                                        .contents_direction = layout::Direction::Row,
                                                        .contents_align = layout::Alignment::Start,
                                                    },
                                                });
                DoBox(builder,
                      {
                          .parent = cc_container,
                          .text = fmt::Format(builder.arena, "CC {}", cc_num),
                          .size_from_text = true,
                          .text_justification = TextJustification::CentredLeft,
                      });
            }

            TableCellText(
                builder,
                row,
                fmt::Format(builder.arena, "{} ({})", descriptor.name, descriptor.ModuleString(" › ")),
                layout::k_fill_parent);

            {
                auto const remove_btn =
                    DoBox(builder,
                          {
                              .parent = row,
                              .text = ICON_FA_TRASH,
                              .font = FontType::Icons,
                              .font_size = k_font_icons_size * 0.8f,
                              .text_colours =
                                  ColSet {
                                      .base = Col {.c = Col::Subtext0},
                                      .hot = Col {.c = Col::Text},
                                      .active = Col {.c = Col::Text},
                                  },
                              .text_justification = TextJustification::Centred,
                              .background_fill_auto_hot_active_overlay = true,
                              .round_background_corners = 0b1111,
                              .layout {
                                  .size = {k_icon_col_width, k_table_row_height},
                              },
                              .tooltip = "Remove this MIDI CC mapping from this instance"_s,
                              .button_behaviour = imgui::ButtonConfig {},
                              .extra_margin_for_mouse_events = 2,
                          });

                if (remove_btn.button_fired) UnlearnMidiCC(context.processor, param_index, (u7)cc_num);
            }
        }
    }

    if (!has_any_assignments) {
        DoBox(
            builder,
            {
                .parent = root,
                .text =
                    "No MIDI CC assignments. Right-click a parameter and select 'MIDI CC Learn' to create one.",
                .wrap_width = k_wrap_to_parent,
                .size_from_text = true,
                .font = FontType::BodyItalic,
                .text_colours = Col {.c = Col::Subtext0},
            });
    }

    auto const add_container = DoBox(builder,
                                     {
                                         .parent = root,
                                         .layout {
                                             .size = {layout::k_fill_parent, layout::k_hug_contents},
                                             .margins = {.t = 8},
                                         },
                                     });

    if (state.add_cc_expanded) {
        AddCcAssignmentRow(builder, context, state, add_container);
    } else {
        auto const row = TableRow(builder, add_container);
        if (TextButton(
                builder,
                row,
                {
                    .text = "Add"_s,
                    .tooltip =
                        "Manually create a MIDI CC assignment, as an alternative to right-clicking a parameter and using 'MIDI CC Learn'"_s,
                }))
            state.add_cc_expanded = true;
    }
}

// Snaps to the nearest CC number that Floe's live MIDI CC learn actually listens to (excludes bank-select,
// RPN/NRPN, and channel-mode CCs — see k_midi_learn_controller_bitset). A manually-picked CC outside this
// set would be stored but silently never fire at runtime.
static s64 NearestLearnableCc(s64 value) {
    value = Clamp<s64>(value, 0, 127);
    for (s64 offset = 0; offset < 128; ++offset) {
        if (auto const below = value - offset; below >= 0 && k_midi_learn_controller_bitset.Get((usize)below))
            return below;
        if (auto const above = value + offset;
            above <= 127 && k_midi_learn_controller_bitset.Get((usize)above))
            return above;
    }
    PanicIfReached();
}

// The content of the parameter-picker popup used by the "add assignment" row: a search box plus every
// automatable parameter that matches it.
static void AddCcParamMenuContent(GuiBuilder& builder, PerformanceControlsPanelState& state) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = f32x2 {240, 380},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    SearchBox(builder,
              DoBox(builder,
                    {
                        .parent = root,
                        .layout {
                            .size = {layout::k_fill_parent, layout::k_hug_contents},
                            .margins = {.lr = 4},
                        },
                    }),
              {
                  .text = state.add_cc_param_search,
                  .tooltip = "Search parameters"_s,
                  .placeholder = "Search parameters"_s,
                  .size = {layout::k_fill_parent, k_font_body_size * 1.3f},
              });

    MenuDivider(builder, root);

    DoBoxViewport(
        builder,
        {
            .run =
                [&state](GuiBuilder& builder) {
                    auto const root = DoBox(builder,
                                            {
                                                .layout {
                                                    .size = layout::k_fill_parent,
                                                    .contents_direction = layout::Direction::Column,
                                                    .contents_align = layout::Alignment::Start,
                                                },
                                            });

                    bool any_match = false;

                    for (auto const param_index_int : Range<u16>(k_num_parameters)) {
                        auto const param_index = (ParamIndex)param_index_int;
                        auto const& descriptor = k_param_descriptors[param_index_int];

                        if (descriptor.flags.not_automatable) continue;

                        auto const module_string = descriptor.ModuleString(" › ");
                        if (state.add_cc_param_search.size &&
                            !ContainsCaseInsensitiveAscii(descriptor.name, state.add_cc_param_search) &&
                            !ContainsCaseInsensitiveAscii(module_string, state.add_cc_param_search))
                            continue;

                        any_match = true;

                        if (MenuItem(
                                builder,
                                root,
                                {
                                    .text = descriptor.name,
                                    .subtext = (String)module_string,
                                    .is_selected = state.add_cc_param && *state.add_cc_param == param_index,
                                },
                                param_index_int)
                                .button_fired) {
                            state.add_cc_param = param_index;
                        }
                    }

                    if (!any_match) {
                        DoBox(builder,
                              {
                                  .parent = root,
                                  .text = "No matching parameters"_s,
                                  .size_from_text = true,
                                  .font = FontType::BodyItalic,
                                  .text_colours = Col {.c = Col::Subtext0},
                                  .layout {
                                      .margins = {.lr = k_menu_item_padding_x, .tb = k_menu_item_padding_y},
                                  },
                              });
                    }
                },
            .bounds = DoBox(builder,
                            {
                                .parent = root,
                                .layout {.size = layout::k_fill_parent},
                            }),
            .imgui_id = SourceLocationHash(),
            .viewport_config = k_default_modal_subviewport,
        });
}

static void CloseAddCcAssignment(PerformanceControlsPanelState& state) {
    state.add_cc_expanded = false;
    state.add_cc_param = k_nullopt;
    dyn::Clear(state.add_cc_param_search);
    state.add_cc_number = 1;
}

// The last row of the MIDI CC table for manually creating a new CC-to-parameter assignment, as an
// alternative to right-clicking a parameter and using 'MIDI CC Learn'. Only shown once the user opts in via
// the "Add" button, so the table isn't permanently cluttered with this secondary workflow.
static void AddCcAssignmentRow(GuiBuilder& builder,
                               PerformanceControlsPanelContext& context,
                               PerformanceControlsPanelState& state,
                               Box parent) {
    auto const row = DoBox(builder,
                           {
                               .parent = parent,
                               .layout {
                                   .size = {layout::k_fill_parent, layout::k_hug_contents},
                                   .contents_gap = k_medium_gap,
                                   .contents_direction = layout::Direction::Row,
                                   .contents_align = layout::Alignment::Start,
                                   .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                               },
                           });

    DoBox(builder, {.parent = row, .text = "Assign CC"_s, .size_from_text = true});

    if (auto const v = IntField(builder,
                                row,
                                {
                                    .tooltip = "The MIDI CC number to assign"_s,
                                    .width = 40,
                                    .value = state.add_cc_number,
                                    .constrainer = [](s64 value) { return NearestLearnableCc(value); },
                                })) {
        state.add_cc_number = *v;
    }

    DoBox(builder, {.parent = row, .text = "to"_s, .size_from_text = true});

    auto const popup_id = builder.imgui.MakeId("perf-add-cc-param-picker"_s);

    auto const picker_btn =
        MenuOpenButton(builder,
                       row,
                       {
                           .text = state.add_cc_param ? k_param_descriptors[ToInt(*state.add_cc_param)].name
                                                      : "Choose Parameter…"_s,
                           .tooltip = "The parameter that the new MIDI CC will control"_s,
                           .width = 190,
                       });
    if (picker_btn.button_fired) builder.imgui.OpenPopupMenu(popup_id, picker_btn.imgui_id);

    if (builder.imgui.IsPopupMenuOpen(popup_id))
        DoBoxViewport(builder,
                      {
                          .run = [&state](GuiBuilder& builder) { AddCcParamMenuContent(builder, state); },
                          .bounds = picker_btn,
                          .imgui_id = popup_id,
                          .viewport_config = ({
                              auto cfg = k_default_popup_menu_viewport;
                              cfg.scrollbar_visibility.y = imgui::ViewportScrollbarVisibility::Always;
                              cfg;
                          }),
                      });

    if (TextButton(builder,
                   row,
                   {
                       .text = "Add"_s,
                       .tooltip = "Add this MIDI CC assignment to this instance"_s,
                       .disabled = !state.add_cc_param.HasValue(),
                       .is_default = true,
                   })) {
        if (state.add_cc_param) {
            AddLearnedMidiCC(context.processor, *state.add_cc_param, (u7)state.add_cc_number);
            CloseAddCcAssignment(state);
        }
    }

    if (TextButton(builder, row, {.text = "Cancel"_s})) CloseAddCcAssignment(state);
}

static void
MidiCcTab(GuiBuilder& builder, PerformanceControlsPanelContext& context, PerformanceControlsPanelState& state) {
    auto const root = DoBox(builder,
                            {
                                .layout {
                                    .size = layout::k_fill_parent,
                                    .contents_padding = {.lrtb = k_default_spacing},
                                    .contents_direction = layout::Direction::Column,
                                    .contents_align = layout::Alignment::Start,
                                },
                            });

    {
        auto const header_container = DoBox(builder,
                                            {
                                                .parent = root,
                                                .layout {
                                                    .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                },
                                            });

        auto const header_row = TableRow(builder, header_container);
        TableHeaderText(builder, header_row, "CC"_s, k_cc_col_width);
        TableHeaderText(builder, header_row, "Parameter"_s, layout::k_fill_parent);
        DoBox(builder,
              {
                  .parent = header_row,
                  .layout {.size = {k_icon_col_width, k_font_body_size}},
              });
    }

    DoBoxViewport(
        builder,
        {
            .run = [&context, &state](GuiBuilder& builder) { MidiCcTableContent(builder, context, state); },
            .bounds = DoBox(builder,
                            {
                                .parent = root,
                                .layout {
                                    .size = {layout::k_fill_parent, layout::k_fill_parent},
                                },
                            }),
            .imgui_id = builder.imgui.MakeId("PerformanceControlsCcContent"_s),
            .viewport_config = ({
                auto cfg = k_default_modal_subviewport;
                cfg.scrollbar_inside_padding = true;
                cfg;
            }),
        });
}

static void PerformanceControlsPanel(GuiBuilder& builder,
                                   PerformanceControlsPanelContext& context,
                                   PerformanceControlsPanelState& state) {
    auto const root = DoModalRootBox(builder);

    DoModalHeader(builder,
                  {
                      .parent = root,
                      .title = "Performance Controls"_s,
                      .trailing_content =
                          [&context, &state](GuiBuilder& builder, Box parent) {
                              HeaderProfileControl(builder, parent, context, state);
                          },
                  });

    // Explains the instance-only scope of the panel, alongside the one common cross-instance action: making
    // the current settings the ones new instances start with. The row has a fixed height so swapping the
    // button for the confirmation chip doesn't shift the content below.
    {
        auto const row =
            DoBox(builder,
                  {
                      .parent = root,
                      .layout {
                          .size = {layout::k_fill_parent, k_font_body_size + (2 * k_button_padding_y)},
                          .margins = {.b = 10},
                          .contents_padding = {.lr = k_default_spacing},
                          .contents_gap = k_medium_gap,
                          .contents_direction = layout::Direction::Row,
                          .contents_align = layout::Alignment::Start,
                          .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                      },
                  });

        if (!state.new_instance_defaults_cache)
            state.new_instance_defaults_cache = perf_profile::DefaultOrFallback(context.prefs);

        auto const current = CaptureCurrentPerformanceProfile(context.processor);
        bool const matches_defaults = current == *state.new_instance_defaults_cache;

        DoBox(builder,
              {
                  .parent = row,
                  .text = "Settings on this page apply to this instance only."_s,
                  .size_from_text = true,
                  .font = FontType::BodyItalic,
                  .text_colours = Col {.c = Col::Subtext0},
              });

        if (matches_defaults) {
            // Inert confirmation chip occupying the same slot as the action button, so the control reads as
            // one thing changing state rather than a button being swapped for a sentence.
            auto const chip =
                DoBox(builder,
                      {
                          .parent = row,
                          .background_fill_colours = Col {.c = Col::Background2},
                          .round_background_corners = 0b1111,
                          .layout {
                              .size = layout::k_hug_contents,
                              .contents_padding = {.lr = k_button_padding_x, .tb = k_button_padding_y},
                              .contents_gap = k_small_gap,
                              .contents_direction = layout::Direction::Row,
                              .contents_align = layout::Alignment::Start,
                              .contents_cross_axis_align = layout::CrossAxisAlign::Middle,
                          },
                          .tooltip = "New Floe instances start with these settings"_s,
                      });
            DoBox(builder,
                  {
                      .parent = chip,
                      .text = ICON_FA_CHECK,
                      .size_from_text = true,
                      .font = FontType::Icons,
                      .font_size = k_font_icons_size * 0.8f,
                      .text_colours = Col {.c = Col::Subtext0},
                  });
            DoBox(builder,
                  {
                      .parent = chip,
                      .text = "Default for new instances"_s,
                      .size_from_text = true,
                      .font = FontType::Body,
                      .text_colours = Col {.c = Col::Subtext0},
                  });
        } else if (
            TextButton(
                builder,
                row,
                {
                    .text = "Make default"_s,
                    .tooltip =
                        "Make this page's current settings the ones new Floe instances start with"_s,
                })) {
            perf_profile::SaveDefault(context.prefs, current);
            state.new_instance_defaults_cache = current;
        }
    }

    DynamicArrayBounded<ModalTabConfig, ToInt(PerformanceControlsPanelState::Tab::Count)> tabs {};
    for (auto const tab : EnumIterator<PerformanceControlsPanelState::Tab>()) {
        ModalTabConfig cfg {};
        switch (tab) {
            case PerformanceControlsPanelState::Tab::Reproducibility:
                cfg = {.icon = ICON_FA_REPEAT, .text = "Reproducibility"};
                break;
            case PerformanceControlsPanelState::Tab::Mpe: cfg = {.icon = ICON_FA_HAND, .text = "MPE"}; break;
            case PerformanceControlsPanelState::Tab::MidiCc:
                cfg = {.icon = ICON_FA_SLIDERS, .text = "MIDI CC"};
                break;
            case PerformanceControlsPanelState::Tab::Count: continue;
        }
        cfg.index = ToInt(tab);
        dyn::Append(tabs, cfg);
    }
    DoModalTabBar(builder, {.parent = root, .tabs = tabs, .current_tab_index = ToIntRef(state.tab)});

    // The tab dispatch happens inside the viewport's deferred `run`, which executes only after the tab
    // bar's click handling above has fully resolved `state.tab` for this frame. Reading `state.tab`
    // directly here instead would let the two layout passes build different box trees on the frame a tab
    // is clicked (the bounds box below is always created either way, so it stays a valid anchor).
    DoBoxViewport(builder,
                  {
                      .run =
                          [&context, &state](GuiBuilder& builder) {
                              switch (state.tab) {
                                  case PerformanceControlsPanelState::Tab::Reproducibility:
                                      ReproducibilityTab(builder, context);
                                      break;
                                  case PerformanceControlsPanelState::Tab::Mpe: MpeTab(builder, context); break;
                                  case PerformanceControlsPanelState::Tab::MidiCc:
                                      MidiCcTab(builder, context, state);
                                      break;
                                  case PerformanceControlsPanelState::Tab::Count: PanicIfReached();
                              }
                          },
                      .bounds = DoBox(builder,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_fill_parent},
                                          },
                                      }),
                      .imgui_id = builder.imgui.MakeId((u64)ToInt(state.tab) + 999999),
                      .viewport_config = k_default_modal_subviewport,
                  });
}

void DoPerformanceControlsPanel(GuiBuilder& builder,
                              PerformanceControlsPanelContext& context,
                              PerformanceControlsPanelState& state) {
    if (!builder.imgui.IsModalOpen(state.k_panel_id)) {
        state.new_instance_defaults_cache = k_nullopt;
        return;
    }

    auto const bounds = CentredModalRect(f32x2 {640, 360});
    builder.imgui.RegisterNamedRect("performance-controls-panel.modal"_s,
                                    builder.imgui.ViewportRectToWindowRect(bounds));

    DoBoxViewport(builder,
                  {
                      .run = [&context, &state](GuiBuilder& b) { PerformanceControlsPanel(b, context, state); },
                      .bounds = bounds,
                      .imgui_id = state.k_panel_id,
                      .viewport_config = k_default_modal_viewport,
                  });
}
