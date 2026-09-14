// Copyright 2026 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Icons from build_resources/fonts/custom_icons/custom_icons.ttf, merged into FontType::Icons alongside Font
// Awesome. Codepoints must stay below ICON_MIN_FA (U+E005). Keep in sync with generate_ttf.py.

#define ICON_CUSTOM_MIN          0xE000
#define ICON_CUSTOM_MAX          0xE002
#define ICON_CUSTOM_MIDI         "\xee\x80\x80" // U+E000
#define ICON_CUSTOM_SQUARE       "\xee\x80\x81" // U+E001
#define ICON_CUSTOM_SQUARE_CHECK "\xee\x80\x82" // U+E002
