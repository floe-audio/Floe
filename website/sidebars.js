// @ts-check

// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

// @type {import('@docusaurus/plugin-content-docs').SidebarsConfig}
const sidebars = {
    sidebar: [
        {
            type: 'category',
            label: 'Getting Started',
            items: [
                'introduction/about-floe',
                'getting-started/system-requirements',
                'getting-started/quick-start-guide',
            ],
        },
        { type: 'doc', id: 'usage/perform', label: 'PERFORM' },
        {
            type: 'category',
            label: 'LAYERS',
            items: [
                'usage/layers',
                'usage/playback-tab',
                'usage/main-tab',
                'usage/lfo-tab',
                'usage/eq-tab',
                'usage/arp-tab',
                'usage/config-tab',
            ],
        },
        { type: 'doc', id: 'usage/effects', label: 'EFFECTS' },
        {
            type: 'category',
            label: 'Working with Floe',
            items: [
                'usage/controls',
                'usage/top-panel',
                'usage/browsers',
                'usage/keyboard',
                'usage/macros',
                'usage/randomisation',
                'usage/reproducibility',
                'usage/performance-controls',
                'usage/midi',
            ],
        },
        {
            type: 'category',
            label: 'Presets',
            items: [
                'presets/loading-presets',
                'presets/saving-presets',
                'presets/preset-storage',
            ],
        },
        {
            type: 'category',
            label: 'Installation & Libraries',
            items: [
                'installation/download-and-install-floe',
                'installation/install-packages',
                'installation/folders',
                'installation/file-locations',
                'installation/uninstalling',
            ],
        },
        {
            type: 'category',
            label: 'Support',
            items: [
                'support/getting-help',
                'support/faq',
                'support/error-reporting',
                'support/autosave',
            ],
        },
        {
            type: 'category',
            label: 'Reference',
            items: [
                'reference/glossary',
                'reference/instruments',
                'reference/sample-libraries',
                'reference/attribution',
                'reference/legacy-parameters',
                'reference/parameters',
            ],
        },
        {
            type: 'category',
            label: 'Library Development',
            collapsed: true,
            items: [
                'develop/develop-libraries',
                'develop/library-lua-scripts',
                'develop/develop-preset-packs',
                'develop/packaging',
                'develop/package-database',
                'develop/tags-and-folders',
            ],
        },
        {
            type: 'category',
            label: 'About the Project',
            items: [
                'about-the-project/sponsorship',
                'about-the-project/beta-testing',
                'about-the-project/mirage',
            ],
        },
        'changelog',
    ],
};

export default sidebars;
