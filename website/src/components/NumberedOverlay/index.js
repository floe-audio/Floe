// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import './NumberedOverlay.css';

/**
 * Numbered Overlay Component
 * Displays an image with numbered overlays positioned at specified coordinates
 * 
 * @param {string} src - Image source URL
 * @param {string} alt - Image alt text
 * @param {Array} overlays - Array of overlay objects with { number, top, left }
 *   - number: The number to display in the overlay
 *   - top: Top position as percentage (e.g., "10%")
 *   - left: Left position as percentage (e.g., "50%")
 * @param {string} className - Optional additional CSS class
 */
export default function NumberedOverlay({ src, alt, overlays = [], className = '' }) {
    return (
        <div className={`numbered-overlay-container ${className}`}>
            <img src={src} alt={alt} />
            {overlays.map((overlay, index) => (
                <span
                    key={index}
                    className="numbered-overlay-badge"
                    style={{
                        top: overlay.top,
                        left: overlay.left
                    }}
                >
                    {overlay.number}
                </span>
            ))}
        </div>
    );
}