// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "engine/check_for_update.hpp"
#include "gui2_common_modal_panel.hpp"
#include "gui2_info_panel_state.hpp"
#include "gui_framework/gui_box_system.hpp"
#include "processor/voices.hpp"
#include "sample_lib_server/sample_library_server.hpp"

struct InfoPanelContext {
    sample_lib_server::Server& server;
    VoicePool& voice_pool;
    ArenaAllocator& scratch_arena;
    check_for_update::State& check_for_update_state;
    prefs::Preferences& prefs;
    Span<sample_lib_server::RefCounted<sample_lib::Library>> libraries;
};

static void LibrariesInfoPanel(GuiBoxSystem& box_system, InfoPanelContext& context) {
    DynamicArrayBounded<char, 500> buffer {};

    // sort libraries by name
    Sort(context.libraries, [](auto a, auto b) { return a->name < b->name; });

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

    // heading
    DoBox(box_system,
          {
              .parent = root,
              .text = fmt::Assign(buffer, "Installed Libraries ({})", context.libraries.size - 1),
              .font = FontType::Heading1,
              .size_from_text = true,
          });

    for (auto lib : context.libraries) {
        if (lib->Id() == sample_lib::k_builtin_library_id) continue;

        // create a 'card' container object
        auto const card = DoBox(box_system,
                                {
                                    .parent = root,
                                    .border = style::Colour::Background2,
                                    .round_background_corners = 0b1111,
                                    .layout {
                                        .size = {layout::k_fill_parent, layout::k_hug_contents},
                                        .contents_padding = {.lrtb = 8},
                                        .contents_gap = 4,
                                        .contents_direction = layout::Direction::Column,
                                        .contents_align = layout::Alignment::Start,
                                        .contents_cross_axis_align = layout::CrossAxisAlign::Start,
                                    },
                                });
        DoBox(box_system,
              {
                  .parent = card,
                  .text = fmt::JoinInline<128>(Array {lib->name, lib->author}, " - "),
                  .font = FontType::Heading2,
                  .size_from_text = true,
              });
        DoBox(box_system,
              {
                  .parent = card,
                  .text = lib->tagline,
                  .font = FontType::Body,
                  .size_from_text = true,
              });
        if (lib->description) {
            DoBox(box_system,
                  {
                      .parent = card,
                      .text = *lib->description,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                  });
        }

        auto do_text_line = [&](String text) {
            DoBox(box_system,
                  {
                      .parent = card,
                      .text = text,
                      .size_from_text = true,
                  });
        };

        do_text_line(fmt::Assign(buffer, "Version: {}", lib->minor_version));
        if (auto const dir = path::Directory(lib->path)) do_text_line(fmt::Assign(buffer, "Folder: {}", dir));
        do_text_line(fmt::Assign(buffer,
                                 "Instruments: {} ({} samples, {} regions)",
                                 lib->insts_by_name.size,
                                 lib->num_instrument_samples,
                                 lib->num_regions));
        do_text_line(fmt::Assign(buffer, "Impulse responses: {}", lib->irs_by_name.size));
        do_text_line(fmt::Assign(buffer, "Library format: {}", ({
                                     String s {};
                                     switch (lib->file_format_specifics.tag) {
                                         case sample_lib::FileFormat::Mdata: s = "Mirage (MDATA)"; break;
                                         case sample_lib::FileFormat::Lua: s = "Floe (Lua)"; break;
                                     }
                                     s;
                                 })));

        auto const button_row = DoBox(box_system,
                                      {
                                          .parent = card,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_padding = {.t = 2},
                                              .contents_gap = 10,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                          },
                                      });
        if (lib->library_url)
            if (TextButton(box_system, button_row, "Library Website", *lib->library_url))
                OpenUrlInBrowser(*lib->library_url);

        if (lib->author_url)
            if (TextButton(box_system, button_row, "Author Website", *lib->author_url))
                OpenUrlInBrowser(*lib->author_url);

        if (auto const dir = path::Directory(lib->path))
            if (TextButton(box_system,
                           button_row,
                           "Open Folder",
                           fmt::Assign(buffer, "Open {} in {}", *dir, GetFileBrowserAppName())))
                OpenFolderInFileBrowser(*dir);
    }

    // make sure there's a gap at the end of the scroll region
    DoBox(box_system,
          {
              .parent = root,
              .layout {
                  .size = {1, 1},
              },
          });
}

static void AboutInfoPanel(GuiBoxSystem& box_system, InfoPanelContext& context) {
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
                "Floe v" FLOE_VERSION_STRING "\n\n"
                "Floe is a free, open source audio plugin that lets you find, perform and transform sounds from sample libraries - from realistic instruments to synthesised tones.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    {
        auto const button_box = DoBox(box_system,
                                      {
                                          .parent = root,
                                          .layout {
                                              .size = {layout::k_fill_parent, layout::k_hug_contents},
                                              .contents_gap = style::k_spacing,
                                              .contents_direction = layout::Direction::Row,
                                              .contents_align = layout::Alignment::Start,
                                          },
                                      });

        if (TextButton(box_system, button_box, "Website & Manual", FLOE_HOMEPAGE_URL))
            OpenUrlInBrowser(FLOE_HOMEPAGE_URL);

        if (TextButton(box_system, button_box, "Source code", FLOE_SOURCE_CODE_URL))
            OpenUrlInBrowser(FLOE_SOURCE_CODE_URL);
    }

    if (auto const new_version =
            check_for_update::NewerVersionAvailable(context.check_for_update_state, context.prefs)) {
        {
            auto const text_row = DoBox(box_system,
                                        {
                                            .parent = root,
                                            .layout {
                                                .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                .contents_gap = style::k_spacing / 4,
                                                .contents_direction = layout::Direction::Row,
                                                .contents_align = layout::Alignment::Start,
                                            },
                                        });
            if (!new_version->is_ignored) {
                DoBox(box_system,
                      {
                          .parent = text_row,
                          .background_shape = BackgroundShape::Circle,
                          .background_fill = style::Colour::Red,
                          .layout {
                              .size = 5,
                          },
                      });
            }
            DoBox(
                box_system,
                {
                    .parent = text_row,
                    .text = fmt::Format(box_system.arena, "New version available: v{}", new_version->version),
                    .size_from_text = true,
                });
        }
        {
            auto const button_box = DoBox(box_system,
                                          {
                                              .parent = root,
                                              .layout {
                                                  .size = {layout::k_fill_parent, layout::k_hug_contents},
                                                  .contents_gap = style::k_spacing,
                                                  .contents_direction = layout::Direction::Row,
                                                  .contents_align = layout::Alignment::Start,
                                              },
                                          });

            if (!new_version->is_ignored) {
                if (TextButton(box_system,
                               button_box,
                               "Ignore",
                               "Hide the red indicator dots for this version"))
                    check_for_update::IgnoreUpdatesUntilAfter(context.prefs, new_version->version);
            }

            if (TextButton(box_system, button_box, "Download page", FLOE_DOWNLOAD_URL))
                OpenUrlInBrowser(FLOE_DOWNLOAD_URL);

            if (TextButton(box_system, button_box, "Changelog", FLOE_CHANGELOG_URL))
                OpenUrlInBrowser(FLOE_CHANGELOG_URL);
        }
    }
}

static void MetricsInfoPanel(GuiBoxSystem& box_system, InfoPanelContext& context) {
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

    DynamicArrayBounded<char, 200> buffer {};

    auto do_line = [&](String text) {
        DoBox(box_system,
              {
                  .parent = root,
                  .text = text,
                  .layout =
                      {
                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                      },
              });
    };

    do_line(fmt::Assign(buffer,
                        "Active voices: {}",
                        context.voice_pool.num_active_voices.Load(LoadMemoryOrder::Relaxed)));

    do_line(fmt::Assign(
        buffer,
        "Samples RAM usage (all instances): {}",
        fmt::PrettyFileSize((f64)context.server.total_bytes_used_by_samples.Load(LoadMemoryOrder::Relaxed))));
    do_line(fmt::Assign(buffer,
                        "Num loaded instruments (all instances): {}",
                        context.server.num_insts_loaded.Load(LoadMemoryOrder::Relaxed)));
    do_line(fmt::Assign(buffer,
                        "Num loaded samples (all instances): {}",
                        context.server.num_samples_loaded.Load(LoadMemoryOrder::Relaxed)));
}

static void LegalInfoPanel(GuiBoxSystem& box_system, InfoPanelContext&) {
#include "third_party_licence_text.hpp"
    static bool open[ArraySize(k_third_party_licence_texts)];

    auto const root = DoBox(box_system,
                            {
                                .layout {
                                    .size = box_system.imgui.PixelsToVw(box_system.imgui.Size()),
                                    .contents_padding = {.lrtb = style::k_spacing},
                                    .contents_gap = 4,
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
                "Floe is free and open source under the GPLv3 licence. We also use the following third-party code.",
            .wrap_width = k_wrap_to_parent,
            .size_from_text = true,
        });

    for (auto [i, txt] : Enumerate(k_third_party_licence_texts)) {
        auto const button = DoBox(box_system,
                                  {
                                      .parent = root,
                                      .activate_on_click_button = MouseButton::Left,
                                      .activation_click_event = ActivationClickEvent::Up,
                                      .layout {
                                          .size = {layout::k_fill_parent, layout::k_hug_contents},
                                          .contents_gap = 4,
                                          .contents_direction = layout::Direction::Row,
                                          .contents_align = layout::Alignment::Start,
                                      },
                                  });
        DoBox(box_system,
              {
                  .parent = button,
                  .text = open[i] ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT,
                  .font = FontType::Icons,
                  .text_fill_hot = style::Colour::Subtext0,
                  .size_from_text = true,
                  .parent_dictates_hot_and_active = true,
              });
        DoBox(box_system,
              {
                  .parent = button,
                  .text = txt.name,
                  .size_from_text = true,
              });

        if (open[i]) {
            DoBox(box_system,
                  {
                      .parent = root,
                      .text = txt.copyright,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                  });
            DoBox(box_system,
                  {
                      .parent = root,
                      .text = txt.licence,
                      .wrap_width = k_wrap_to_parent,
                      .size_from_text = true,
                  });
        }

        // change state at the end so we don't create a new box if the button is pressed - causing layout
        // issues
        if (button.button_fired) {
            auto new_state = !open[i];
            for (auto& o : open)
                o = false;
            open[i] = new_state;
        }
    }
}

static void InfoPanel(GuiBoxSystem& box_system, InfoPanelContext& context, InfoPanelState& state) {
    constexpr auto k_tab_config = []() {
        Array<ModalTabConfig, ToInt(InfoPanelState::Tab::Count)> tabs {};
        for (auto const tab : EnumIterator<InfoPanelState::Tab>()) {
            auto const index = ToInt(tab);
            switch (tab) {
                case InfoPanelState::Tab::Libraries:
                    tabs[index] = {.icon = ICON_FA_BOOK_OPEN, .text = "Libraries"};
                    break;
                case InfoPanelState::Tab::About:
                    tabs[index] = {.icon = ICON_FA_INFO_CIRCLE, .text = "About"};
                    break;
                case InfoPanelState::Tab::Legal:
                    tabs[index] = {.icon = ICON_FA_GAVEL, .text = "Legal"};
                    break;
                case InfoPanelState::Tab::Metrics:
                    tabs[index] = {.icon = ICON_FA_MICROCHIP, .text = "Metrics"};
                    break;
                case InfoPanelState::Tab::Count: PanicIfReached();
            }
        }
        return tabs;
    }();

    auto const root = DoModal(box_system,
                              {
                                  .title = "Info"_s,
                                  .on_close = [&state] { state.open = false; },
                                  .tabs = k_tab_config,
                                  .current_tab_index = ToIntRef(state.tab),
                              });

    using TabPanelFunction = void (*)(GuiBoxSystem&, InfoPanelContext&);
    AddPanel(box_system,
             Panel {
                 .run = ({
                     TabPanelFunction f {};
                     switch (state.tab) {
                         case InfoPanelState::Tab::Libraries: f = LibrariesInfoPanel; break;
                         case InfoPanelState::Tab::About: f = AboutInfoPanel; break;
                         case InfoPanelState::Tab::Metrics: f = MetricsInfoPanel; break;
                         case InfoPanelState::Tab::Legal: f = LegalInfoPanel; break;
                         case InfoPanelState::Tab::Count: PanicIfReached();
                     }
                     [f, &context](GuiBoxSystem& box_system) { f(box_system, context); };
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

PUBLIC void DoInfoPanel(GuiBoxSystem& box_system, InfoPanelContext& context, InfoPanelState& state) {
    if (state.open) {
        if (!state.opened_before) {
            state.opened_before = true;
            if (check_for_update::ShowNewVersionIndicator(context.check_for_update_state, context.prefs))
                state.tab = InfoPanelState::Tab::About;
        }
        RunPanel(box_system,
                 Panel {
                     .run = [&context, &state](GuiBoxSystem& b) { InfoPanel(b, context, state); },
                     .data =
                         ModalPanel {
                             .r = CentredRect(
                                 {.pos = 0, .size = box_system.imgui.frame_input.window_size.ToFloat2()},
                                 f32x2 {box_system.imgui.VwToPixels(style::k_info_dialog_width),
                                        box_system.imgui.VwToPixels(style::k_info_dialog_height)}),
                             .imgui_id = box_system.imgui.GetID("new info"),
                             .on_close = [&state]() { state.open = false; },
                             .close_on_click_outside = true,
                             .darken_background = true,
                             .disable_other_interaction = true,
                         },
                 });
    }
}
