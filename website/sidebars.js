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
                'getting-started/quick-start-guide',
                'getting-started/overview',
                'getting-started/faq',
                'getting-started/glossary',
            ],
        },
        {
            type: 'category',
            label: 'Installation',
            items: [
                'installation/download-and-install-floe',
                'installation/install-packages',
                'installation/uninstalling',
            ],
        },
        {
            type: 'category',
            label: 'Usage',
            items: [
                'usage/perform',
                'usage/layers',
                'usage/effects',
                'usage/browsers',
                'usage/granular',
                'usage/arpeggiator',
                'usage/presets',
                'usage/folders',
                'usage/key-ranges',
                'usage/randomisation',
                'usage/reproducibility',
                'usage/midi',
                'usage/looping',
                'usage/macros',
            ],
        },
        {
            type: 'category',
            label: 'Reference',
            items: [
                'reference/instruments',
                'reference/sample-libraries',
                'reference/parameters',
                'reference/attribution',
                'reference/autosave',
                'reference/error-reporting',
                'reference/legacy-parameters',
                'reference/file-locations',
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
        'website-changelog',
    ],
};

export default sidebars;
