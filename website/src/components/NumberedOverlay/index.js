// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import './NumberedOverlay.css';

/**
 * Numbered Overlay Component
 * Displays an image with numbered overlays positioned at specified coordinates
 * and optional box outlines around sections of the image
 * 
 * @param {string} src - Image source URL
 * @param {string} alt - Image alt text
 * @param {Array} overlays - Array of overlay objects with { number, top, left }
 *   - number: The number to display in the overlay
 *   - top: Top position as percentage (e.g., "10%")
 *   - left: Left position as percentage (e.g., "50%")
 * @param {Array} boxes - Array of box outline objects with { number, top, left, width, height }
 *   - number: The number to display on the box outline
 *   - top: Top position as percentage (e.g., "10%")
 *   - left: Left position as percentage (e.g., "20%")
 *   - width: Width as percentage (e.g., "30%")
 *   - height: Height as percentage (e.g., "15%")
 * @param {string} className - Optional additional CSS class
 */
export default function NumberedOverlay({ src, alt, overlays = [], boxes = [], className = '' }) {
    return (
        <div className={`numbered-overlay-container ${className}`}>
            <img src={src} alt={alt} />
            {overlays.map((overlay, index) => (
                <span
                    key={`overlay-${index}`}
                    className="numbered-overlay-badge"
                    style={{
                        top: overlay.top,
                        left: overlay.left
                    }}
                >
                    {overlay.number}
                </span>
            ))}
            {boxes.map((box, index) => (
                <div
                    key={`box-${index}`}
                    className="numbered-overlay-box"
                    style={{
                        top: box.top,
                        left: box.left,
                        width: box.width,
                        height: box.height
                    }}
                >
                    <span className="numbered-overlay-box-number">
                        {box.number}
                    </span>
                </div>
            ))}
        </div>
    );
}