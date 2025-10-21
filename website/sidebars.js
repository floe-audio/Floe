// @ts-check

// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

// @type {import('@docusaurus/plugin-content-docs').SidebarsConfig}
const sidebars = {
    tutorialSidebar: [
        'introduction',
        {
            type: 'category',
            label: 'Installation',
            items: [
                'installation/requirements',
                'installation/download-and-install-floe',
                'installation/updating',
                'installation/uninstalling',
            ],
        },
        {
            type: 'category',
            label: 'Packages',
            items: [
                'packages/about-packages',
                'packages/install-packages',
            ],
        },
        {
            type: 'category',
            label: 'Usage',
            items: [
                'usage/sample-libraries',
                'usage/presets',
                'usage/layers',
                'usage/effects',
                'usage/key-ranges',
                'usage/midi',
                'usage/looping',
                'usage/parameters',
                'usage/macros',
                'usage/autosave',
                'usage/attribution',
                'usage/error-reporting',
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
                'about-the-project/mirage',
                'about-the-project/roadmap',
                'about-the-project/sponsorship',
                'about-the-project/beta-testing',
            ],
        },
        'changelog',
    ],
};

export default sidebars;
