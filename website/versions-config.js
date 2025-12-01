// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

// This file contains Docusaurus version configuration that can be temporarily
// replaced during the 'website-promote-beta-to-stable' process. The promotion
// command needs to create a new "stable" version, but Docusaurus fails if the
// config references a "stable" version that doesn't exist yet (after the rm commands
// clear the versioned files). The script temporarily replaces this file with an
// empty config during version creation.

export default {
    lastVersion: "stable",
    versions: {
        current: {
            label: "Beta",
            path: "beta",
            banner: "unreleased",
            badge: true,
            noIndex: true,
        },
        stable: {
            label: "Stable",
            path: "/",
            badge: false,
        },
    }
};
