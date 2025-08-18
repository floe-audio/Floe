<!--
SPDX-FileCopyrightText: 2024 Sam Windell
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Mirage Compatibility

Floe includes full compatibility with Mirage, a sample library platform developed by FrozenPlain from 2018 to 2024. Floe evolved from this foundation while expanding into open-source development and broader platform compatibility.

## Technical Details

Mirage libraries use a custom `.mdata` binary format for storing samples and metadata. The **Mirage Compatibility package** provides additional assets such as images and audio files that were previously embedded within Mirage but are handled separately in Floe. This package is automatically included with FrozenPlain's Mirage-format products and is also available separately on the [packages page](../packages/available-packages.md) if needed.

Floe automatically detects existing Mirage installations and loads libraries without requiring conversion or reinstallation. Both Mirage and Floe can coexist on the same system without conflicts.

For information about FrozenPlain's transition from Mirage to Floe, including product catalogue details and customer migration guidance, see [FrozenPlain's transition page](https://www.frozenplain.com/support/mirage/mirage-floe-transition).
