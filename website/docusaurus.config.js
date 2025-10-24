// @ts-check

// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import { themes as prismThemes } from 'prism-react-renderer';


/** @type {import('@docusaurus/types').Config} */
const config = {
    title: 'Floe',
    tagline: 'Floe is a free, open-source sample library engine. Available as an audio plugin for Windows, macOS and Linux.',
    favicon: 'images/favicon.svg',

    // Future flags, see https://docusaurus.io/docs/api/docusaurus-config#future
    future: {
        v4: true, // Improve compatibility with the upcoming Docusaurus v4
    },

    // Set the production url of your site here
    url: 'https://floe.audio',
    // Set the /<baseUrl>/ pathname under which your site is served
    // For GitHub pages deployment, it is often '/<projectName>/'
    baseUrl: '/',

    // GitHub pages deployment config.
    organizationName: 'floe-audio', // Usually your GitHub org/user name.
    projectName: 'Floe', // Usually your repo name.

    onBrokenLinks: 'throw',

    // Global scripts
    scripts: [
        {
            src: 'https://analytics.frozenplain.com/script.js',
            defer: true,
            'data-website-id': '09786971-9957-41b7-ad9c-fafdefed73f6'
        }
    ],

    // Even if you don't use internationalization, you can use this field to set
    // useful metadata like html lang. For example, if your site is Chinese, you
    // may want to replace "en" with "zh-Hans".
    i18n: {
        defaultLocale: 'en',
        locales: ['en'],
    },

    presets: [
        [
            'classic',
            /** @type {import('@docusaurus/preset-classic').Options} */
            ({
                docs: {
                    sidebarPath: './sidebars.js',
                    routeBasePath: 'docs',
                    sidebarCollapsed: false,
                },
                blog: {
                    showReadingTime: true,
                    feedOptions: {
                        type: ['rss', 'atom'],
                        xslt: true,
                    },
                    // Useful options to enforce blogging best practices
                    onInlineTags: 'warn',
                    onInlineAuthors: 'warn',
                    onUntruncatedBlogPosts: 'warn',
                },
                theme: {
                    customCss: './src/css/custom.css',
                },
            }),
        ],
    ],

    themeConfig:
        /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
        ({
            colorMode: {
                respectPrefersColorScheme: true,
            },
            navbar: {
                // title: '',
                logo: {
                    alt: 'Floe Logo',
                    src: 'images/logo-light-mode.svg',
                    srcDark: 'images/logo-dark-mode.svg',
                },
                items: [
                    {
                        to: 'download',
                        position: 'left',
                        label: 'Download',
                    },
                    {
                        to: 'packages',
                        position: 'left',
                        label: 'Packages',
                    },
                    {
                        type: 'docSidebar',
                        sidebarId: 'tutorialSidebar',
                        position: 'left',
                        label: 'Docs',
                    },
                    { to: 'blog', label: 'Blog', position: 'left' },
                ],
            },
            footer: {
                style: 'dark',
                links: [
                    {
                        items: [
                            {
                                label: 'Home',
                                to: '/',
                            },
                            {
                                label: 'Download',
                                to: '/download',
                            },
                        ],
                    },
                    {
                        items: [
                            {
                                label: 'Docs',
                                to: '/docs/getting-started/quick-start-guide',
                            },
                            {
                                label: 'Packages',
                                to: '/packages',
                            },
                            {
                                label: 'About',
                                to: '/about',
                            },
                        ],
                    },
                    {
                        items: [
                            {
                                label: 'Blog',
                                to: '/blog',
                            },
                            {
                                label: 'GitHub',
                                href: 'https://github.com/floe-audio/Floe',
                            },
                            {
                                label: 'Press',
                                to: '/press',
                            },
                        ],
                    },
                ],
                // copyright: `Copyright © ${new Date().getFullYear()} Sam Windell.`,
            },
            prism: {
                theme: prismThemes.github,
                darkTheme: prismThemes.dracula,
                additionalLanguages: ['lua'],
            },
        }),
    plugins: [
        './plugins/packages-plugin',
        [
            '@docusaurus/plugin-client-redirects',
            {
                fromExtensions: ['html', 'htm'], // /myPage.html -> /myPage
                redirects: [
                    // Our old routes from our previous mdbook-based site.
                    { to: '/', from: ['/home', '/home.html'], },
                    { to: '/packages', from: ['/packages/available-packages', '/packages/available-packages.html'], },
                    {
                        to: '/download',
                        from: [
                            '/installation/download-and-install-floe.html',
                            '/installation/download-and-install-floe', '/installation/updating', '/installation/updating.html', '/installation/requirements', '/installation/requirements.html'],
                    },
                    { to: '/docs/installation/download-and-install-floe', from: ['/docs/installation/requirements'], },
                    { to: '/docs/getting-started/quick-start-guide', from: ['/docs/introduction'], },
                    { to: '/docs/installation/uninstalling', from: ['/installation/uninstalling', '/installation/uninstalling.html'], },
                    { to: '/docs/installation/install-packages', from: ['/docs/packages/install-packages', '/packages/install-packages', '/packages/install-packages.html'], },
                    {
                        to: '/docs/key-concepts/components',
                        from: [
                            '/docs/usage/presets',
                            '/usage/presets',
                            '/usage/presets.html',
                            '/docs/usage/sample-libraries',
                            '/usage/sample-libraries',
                            '/usage/sample-libraries.html',
                            '/docs/packages/about-packages',
                            '/packages/about-packages',
                            '/packages/about-packages.html',
                        ],
                    },
                    { to: '/docs/usage/layers', from: ['/usage/layers', '/usage/layers.html'], },
                    { to: '/docs/usage/effects', from: ['/usage/effects', '/usage/effects.html'], },
                    { to: '/docs/usage/key-ranges', from: ['/usage/key-ranges', '/usage/key-ranges.html'], },
                    { to: '/docs/usage/midi', from: ['/usage/midi', '/usage/midi.html'], },
                    { to: '/docs/usage/looping', from: ['/usage/looping', '/usage/looping.html'], },
                    { to: '/docs/reference/parameters', from: ['/docs/usage/parameters', '/usage/parameters', '/usage/parameters.html'], },
                    { to: '/docs/usage/macros', from: ['/usage/macros', '/usage/macros.html'], },
                    { to: '/docs/reference/autosave', from: ['/docs/usage/autosave', '/usage/autosave', '/usage/autosave.html'], },
                    { to: '/docs/reference/attribution', from: ['/docs/usage/attribution', '/usage/attribution', '/usage/attribution.html'], },
                    { to: '/docs/reference/error-reporting', from: ['/docs/usage/error-reporting', '/usage/error-reporting', '/usage/error-reporting.html'], },
                    { to: '/docs/develop/develop-libraries', from: ['/develop/develop-libraries', '/develop/develop-libraries.html'], },
                    { to: '/docs/develop/library-lua-scripts', from: ['/develop/library-lua-scripts', '/develop/library-lua-scripts.html'], },
                    { to: '/docs/develop/develop-preset-packs', from: ['/develop/develop-preset-packs', '/develop/develop-preset-packs.html'], },
                    { to: '/docs/develop/packaging', from: ['/develop/packaging', '/develop/packaging.html'], },
                    { to: '/docs/develop/tags-and-folders', from: ['/develop/tags-and-folders', '/develop/tags-and-folders.html'], },
                    { to: '/docs/about-the-project/mirage', from: ['/about-the-project/mirage', '/about-the-project/mirage.html'], },
                    { to: '/docs/about-the-project/roadmap', from: ['/about-the-project/roadmap', '/about-the-project/roadmap.html'], },
                    { to: '/docs/changelog', from: ['/changelog', '/changelog.html'], },
                ],
            },
        ],
    ],
    markdown: {
        mermaid: true,
    },
    themes: ['@docusaurus/theme-mermaid'],
};

export default config;
