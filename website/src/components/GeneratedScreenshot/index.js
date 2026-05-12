// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import NumberedOverlay from '@site/src/components/NumberedOverlay';

/**
 * Renders a build-time-generated screenshot, optionally with an adjacent overlay JSON.
 * The PNG (and optional JSON) are produced by `zig build script:gen-doc-screenshots` and
 * live in static/images/screenshots/. When the JSON is present, each overlay entry becomes
 * a numbered box, in JSON order. When absent, the image is rendered with no boxes.
 *
 * @param {string} name - Base filename (without extension), e.g. "perform"
 * @param {string} alt  - Image alt text
 */
export default function GeneratedScreenshot({ name, alt }) {
    let overlays = [];
    try {
        overlays = require(`@site/static/images/screenshots/${name}.json`).overlays;
    } catch (e) {
        // No overlay JSON for this screenshot — render the image with no boxes.
    }
    const src = `/images/screenshots/${name}.png`;
    const boxes = overlays.map((o, i) => ({
        number: i + 1,
        top: `${o.y * 100}%`,
        left: `${o.x * 100}%`,
        width: `${o.w * 100}%`,
        height: `${o.h * 100}%`,
    }));
    return <NumberedOverlay src={src} alt={alt} boxes={boxes} />;
}
