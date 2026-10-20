// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import { useDoc } from '@docusaurus/plugin-content-docs/client';
import { useIsManual } from '@site/src/components/ManualContext';
import { LayoutCallouts } from './calloutLayout.mjs';
import './GeneratedScreenshot.css';

// The figure is one scalable SVG drawn over the image, so it lays out the same in server rendering,
// the browser and the paginated manual. Its numbers are sized for the width of the column it's shown
// in, so they're the same size in every figure and only shrink when the column does.
const k_manual_sizes = {
    unit: 'mm',
    natural_units_per_pixel: 0.15,
    content_width: 170,
    badge_diameter: 4.2,
    line_width: 0.35,
    corner_radius: 0.5,
};

// Docs content column widths at full container width (--ifm-container-width-xl minus padding).
const k_web_sizes = {
    unit: 'px',
    natural_units_per_pixel: 1,
    content_width_without_toc: 968,
    content_width_with_toc: 718,
    badge_diameter: 22,
    line_width: 1.5,
    corner_radius: 3,
};

/**
 * Renders a build-time-generated screenshot, optionally with overlay boxes. The PNG and its
 * optional overlay JSON are produced by `zig build script:gen-doc-screenshots`. When there's an
 * overlay, each entry becomes a box, numbered in JSON order, with its number in a margin outside
 * the image and a line to the box, or on the box when that line would be long or blocked. A lone box
 * is drawn without a number.
 *
 * @param {string} name - Base filename (without extension), e.g. "perform"
 * @param {string} src - The PNG's URL, added by the asset-urls remark plugin; not written by hand
 * @param {string} overlayJson - The overlay, added by the asset-urls remark plugin; not written by hand
 * @param {string} alt  - Image alt text
 */
export default function GeneratedScreenshot(props) {
    if (useIsManual()) return <Figure {...props} sizes={k_manual_sizes} />;
    return <WebFigure {...props} />;
}

function WebFigure(props) {
    const { frontMatter, toc } = useDoc();
    const has_toc = !frontMatter.hide_table_of_contents && toc.length > 0;
    const content_width = has_toc ? k_web_sizes.content_width_with_toc : k_web_sizes.content_width_without_toc;
    return <Figure {...props} sizes={{ ...k_web_sizes, content_width }} />;
}

function Figure({ src, overlayJson, alt, sizes }) {
    const overlay_data = overlayJson ? JSON.parse(overlayJson) : null;
    if (!overlay_data?.width) return <img className="generated-screenshot-plain" src={src} alt={alt} />;

    const image = { width: overlay_data.width, height: overlay_data.height };
    const boxes = overlay_data.overlays.map((o) => ({
        x: o.x * image.width,
        y: o.y * image.height,
        w: o.w * image.width,
        h: o.h * image.height,
    }));
    const numbered = boxes.length > 1;

    const Layout = (badge_diameter) =>
        numbered
            ? LayoutCallouts({ image, boxes, badge_diameter })
            : {
                  margins: { left: 0, right: 0, top: 0, bottom: 0 },
                  width: image.width,
                  height: image.height,
                  callouts: [],
              };

    // Margins grow in proportion to the badge, so a probe layout tells us how many badge-widths they
    // add to the figure's width; the rest of the column is what the image itself gets.
    const probe_badge_diameter = sizes.badge_diameter / sizes.natural_units_per_pixel;
    const margin_badge_widths = (Layout(probe_badge_diameter).width - image.width) / probe_badge_diameter;
    const display_units_per_pixel = Math.min(
        sizes.natural_units_per_pixel,
        (sizes.content_width - margin_badge_widths * sizes.badge_diameter) / image.width
    );
    const PixelsFor = (display_size) => display_size / display_units_per_pixel;
    const badge_diameter = PixelsFor(sizes.badge_diameter);
    const layout = Layout(badge_diameter);
    const { margins } = layout;

    const line_width = PixelsFor(sizes.line_width);
    const Percent = (value, total) => `${(value / total) * 100}%`;
    const Round = (value) => Math.round(value * 10) / 10;
    const PointsString = (points) => points.map((point) => `${Round(point.x)},${Round(point.y)}`).join(' ');

    return (
        <div
            className="generated-screenshot"
            style={{
                width: `${layout.width * sizes.natural_units_per_pixel}${sizes.unit}`,
                aspectRatio: `${layout.width} / ${layout.height}`,
            }}
        >
            <img
                src={src}
                alt={alt}
                style={{
                    left: Percent(margins.left, layout.width),
                    top: Percent(margins.top, layout.height),
                    width: Percent(image.width, layout.width),
                    height: Percent(image.height, layout.height),
                }}
            />
            <svg viewBox={`0 0 ${layout.width} ${layout.height}`} aria-hidden="true">
                {boxes.map((box, box_index) => (
                    <rect
                        key={`box-${box_index}`}
                        className="generated-screenshot-line"
                        x={Round(margins.left + box.x)}
                        y={Round(margins.top + box.y)}
                        width={Round(box.w)}
                        height={Round(box.h)}
                        rx={Round(PixelsFor(sizes.corner_radius))}
                        strokeWidth={Round(line_width)}
                    />
                ))}
                {layout.callouts.map((callout, box_index) => (
                    <g key={`callout-${box_index}`}>
                        {callout.leader && (
                            <polyline
                                className="generated-screenshot-line"
                                points={PointsString(callout.leader)}
                                strokeWidth={Round(line_width)}
                            />
                        )}
                        <circle
                            className="generated-screenshot-badge"
                            cx={Round(callout.badge.x)}
                            cy={Round(callout.badge.y)}
                            r={Round(badge_diameter / 2)}
                        />
                        <text
                            className="generated-screenshot-number"
                            x={Round(callout.badge.x)}
                            y={Round(callout.badge.y)}
                            fontSize={Round(badge_diameter * 0.62)}
                        >
                            {box_index + 1}
                        </text>
                    </g>
                ))}
            </svg>
        </div>
    );
}
