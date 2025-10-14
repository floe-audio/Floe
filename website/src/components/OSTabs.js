// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React, { useState, useEffect } from 'react';
import './OSTabs.css';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';

/**
 * OS Detection and Tab Management Component for Download Page
 * Automatically detects user's OS and shows appropriate download options
 */
export default function OSTabs({ children }) {
    const [activeOS, setActiveOS] = useState('windows');

    // Detect user's operating system
    useEffect(() => {
        function detectOS() {
            const userAgent = navigator.userAgent;
            const platform = navigator.platform;

            if (userAgent.indexOf('Mac OS X') !== -1 || platform.indexOf('Mac') !== -1) {
                return 'macos';
            } else if (userAgent.indexOf('Windows') !== -1 || platform.indexOf('Win') !== -1) {
                return 'windows';
            } else if (userAgent.indexOf('Linux') !== -1 || platform.indexOf('Linux') !== -1) {
                return 'linux';
            }
            return 'windows'; // Default fallback
        }

        setActiveOS(detectOS());
    }, []);

    return (
        <div className="os-tabs-container">
            {/* Tab Navigation */}
            <div className="download-tabs">
                <div
                    className={`download-tab ${activeOS === 'windows' ? 'active' : ''}`}
                    onClick={() => setActiveOS('windows')}
                >
                    <FontAwesomeIcon icon="fa-brands fa-windows" />
                    Windows
                </div>
                <div
                    className={`download-tab ${activeOS === 'macos' ? 'active' : ''}`}
                    onClick={() => setActiveOS('macos')}
                >
                    <FontAwesomeIcon icon="fa-brands fa-apple" />
                    macOS
                </div>
                <div
                    className={`download-tab ${activeOS === 'linux' ? 'active' : ''}`}
                    onClick={() => setActiveOS('linux')}
                >
                    <FontAwesomeIcon icon="fa-brands fa-linux" />
                    Linux
                </div>
            </div>

            {/* Tab Content */}
            <div className="download-panels">
                {React.Children.map(children, (child) => {
                    if (React.isValidElement(child) && child.props.os === activeOS) {
                        return React.cloneElement(child, {
                            className: `download-panel active ${child.props.className || ''}`
                        });
                    }
                    return null;
                })}
            </div>
        </div>
    );
}

/**
 * Individual OS Panel Component
 */
export function OSPanel({ os, className, children }) {
    return (
        <div className={`download-panel ${className || ''}`} data-os={os}>
            {children}
        </div>
    );
}
