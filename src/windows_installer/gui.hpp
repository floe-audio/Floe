// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

// A native GUI framework with a GTK-like layout system. It very heavily uses tagged unions rather than an
// object-oriented approach.
//
// Originally this was designed for multiple backends: Win32, Cocoa and GTK, but now we only have a Win32
// backend. A native-looking 'installer wizard' is a very familiar concept to users and so it's a good choice
// for the installer. We want people to feel comfortable and safe when installing a new piece of software.

struct Application;
struct GuiFramework;

constexpr u32 k_timer_ms = 20;
constexpr usize k_max_widgets = 150;

#define WIDEN_MACRO_HELPER(x)   L##x
#define WIDEN_STRING_LITERAL(x) WIDEN_MACRO_HELPER(x)

struct AppConfig {
    u16 window_width;
    u16 window_height;
    wchar_t const* window_title;
    String description;
};

enum class Orientation { Vertical, Horizontal };
enum class Alignment { Start, End };
enum class TextAlignment { Left, Right, Centre };
enum class LabelStyle { Regular, DullColour, Bold, Heading };
enum class ButtonStyle { Push, ExpandCollapseToggle };
struct WidgetMargins {
    u16 l {}, r {}, t {}, b {};
};

enum class WidgetType {
    None,
    ProgressBar,
    ReadOnlyTextbox,
    TextInput,
    RadioButtons,
    Button,
    Hyperlink,
    Label,
    Image,
    Divider,
    CheckboxTable,
    Container,
};

struct WidgetOptions {
    Optional<UiSize> fixed_size {};
    WidgetMargins margins {};
    bool expand_x = false;
    bool expand_y = false;
    String text {};
    char const* debug_name = "";

    struct TextInput {
        bool password = false;
    };
    struct RadioButtons {
        Span<String> labels {};
    };
    struct Button {
        ButtonStyle style {ButtonStyle::Push};
        bool is_default {};
    };
    struct Hyperlink {
        String url {};
    };
    struct Label {
        LabelStyle style {LabelStyle::Regular};
        TextAlignment text_alignment {TextAlignment::Left};
    };
    struct Image {
        u8 const* rgba_data {};
        UiSize size {};
    };
    struct Divider {
        Orientation orientation {};
    };
    struct CheckboxTable {
        struct Column {
            String label {};
            u16 default_width {};
        };
        Span<Column> columns;
    };
    struct Container {
        // If Tabs, create it and then add pages to it using CreateStackLayoutWidget()
        enum class Type { StackLayout, Frame, Tabs };
        Type type {Type::StackLayout};
        int spacing = 0;
        Orientation orientation {Orientation::Vertical};
        Alignment alignment {Alignment::Start};
        bool has_vertical_scrollbar = false;
        String tab_label = {}; // Required if type is Tabs
    };

    TaggedUnion<WidgetType,
                TypeAndTag<TextInput, WidgetType::TextInput>,
                TypeAndTag<RadioButtons, WidgetType::RadioButtons>,
                TypeAndTag<Button, WidgetType::Button>,
                TypeAndTag<Hyperlink, WidgetType::Hyperlink>,
                TypeAndTag<Label, WidgetType::Label>,
                TypeAndTag<Image, WidgetType::Image>,
                TypeAndTag<Divider, WidgetType::Divider>,
                TypeAndTag<CheckboxTable, WidgetType::CheckboxTable>,
                TypeAndTag<Container, WidgetType::Container>>
        type {WidgetType::None};
};

u32 CreateStackLayoutWidget(GuiFramework& framework, Optional<u32> parent, WidgetOptions options);

u32 CreateWidget(GuiFramework& framework, u32 parent, WidgetOptions options);

void RecalculateLayout(GuiFramework&);

struct UserInteraction {
    enum class Type {
        ButtonPressed,
        RadioButtonSelected,
        TextInputChanged,
        TextInputEnterPressed,
        CheckboxTableItemToggled,
    };
    Type type {};
    u32 widget_id {};
    bool button_state {};
    String text {};
    u32 button_index {}; // used if the widget contains multiple buttons
};

void HandleUserInteraction(Application& app, GuiFramework& framework, UserInteraction const&);

struct EditWidgetOptions {
    struct CheckboxTableItem {
        bool state {};
        Span<String> items {};
    };

    Optional<bool> simulate_button_press;
    Optional<bool> visible;
    Optional<bool> enabled;
    Optional<String> text;
    Optional<f64> progress_bar_position;
    Optional<bool> progress_bar_pulse;
    Optional<bool> clear_checkbox_table;
    Optional<LabelStyle> label_style;
    Optional<CheckboxTableItem> add_checkbox_table_item;
};
void EditWidget(GuiFramework&, u32 id, EditWidgetOptions const& options);

String GetText(GuiFramework& framework, u32 id);
bool AutorunMode(GuiFramework& framework);

// Defined in installer code
AppConfig GetAppConfig();
Application* CreateApplication(GuiFramework& framework, u32 root_layout);
void OnTimer(Application& app, GuiFramework& framework);
[[nodiscard]] int DestroyApplication(Application& app, GuiFramework& framework); // returns main return code

void ExitProgram(GuiFramework& framework);
void ErrorDialog(GuiFramework& framework, String title);
