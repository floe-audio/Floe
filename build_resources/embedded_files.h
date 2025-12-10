// SPDX-FileCopyrightText: 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char const* data;
    uint64_t size;
} EmbeddedString;

typedef struct {
    uint8_t const* data;
    uint64_t size;
    EmbeddedString filename;
} BinaryData;

typedef struct {
    BinaryData data;
    EmbeddedString id;
    EmbeddedString folder;
    EmbeddedString tag1;
    EmbeddedString tag2;
    EmbeddedString description;
} EmbeddedIr;

typedef struct {
    EmbeddedIr const* irs;
    uint32_t count;
} EmbeddedIrs;

BinaryData EmbeddedFontAwesome();
BinaryData EmbeddedRoboto();
BinaryData EmbeddedRobotoItalic();
BinaryData EmbeddedDefaultBackground();
BinaryData EmbeddedLogoImage();
BinaryData EmbeddedIconImage();
BinaryData EmbeddedUnknownLibraryIcon();
BinaryData EmbeddedAboutLibraryTemplateRtf();
BinaryData EmbeddedPackageInstallationRtf();

EmbeddedIrs GetEmbeddedIrs();

#ifdef __cplusplus
}
#endif
