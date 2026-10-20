// @ts-check

// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import fs from "node:fs";
import path from "node:path";
import { themes as prismThemes } from "prism-react-renderer";
import versionsConfig from "./versions-config.js";
import {
    remarkAssetUrls,
    resolveAssetUrls,
    warnAboutUnuploadedAssets,
    writeResolvedAssetsCacheKey,
} from "./plugins/asset-urls.mjs";

warnAboutUnuploadedAssets();

// Set by scripts/generate-manual.mjs; see plugins/manual-plugin.
const k_manual_options = process.env.FLOE_MANUAL ? JSON.parse(process.env.FLOE_MANUAL) : undefined;
const k_asset_options = { manualScreenshotsDir: k_manual_options?.screenshotsDir };
const k_remark_asset_urls = [remarkAssetUrls, k_asset_options];

// Docs moved to a new path in the beta docs. Stable redirects only exist once the move has been
// promoted to stable, because Docusaurus rejects redirects to routes that don't exist.
const k_moved_docs = {
    "getting-started/overview": "introduction/about-floe",
    "usage/presets": "presets/loading-presets",
    "usage/folders": "installation/folders",
    "reference/file-locations": "installation/file-locations",
    "getting-started/faq": "support/faq",
    "reference/error-reporting": "support/error-reporting",
    "reference/autosave": "support/autosave",
    "getting-started/glossary": "reference/glossary",
    "usage/workflow": "usage/layers",
    "usage/granular": "usage/playback-tab",
    "usage/looping": "usage/playback-tab",
    "usage/arpeggiator": "usage/arp-tab",
    "usage/key-ranges": "usage/config-tab",
    "usage/the-window": "getting-started/quick-start-guide",
};

const isInStableDocs = (docPath) =>
    fs.existsSync(new URL(`./versioned_docs/version-stable/${docPath}.mdx`, import.meta.url));

// The stable route of a doc, following its move once that's been promoted.
const stableDocRoute = (docPath) => {
    const movedTo = k_moved_docs[docPath];
    return `/docs/${movedTo && isInStableDocs(movedTo) ? movedTo : docPath}`;
};

const movedDocRedirects = Object.entries(k_moved_docs).flatMap(([from, to]) => [
    { to: `/docs/beta/${to}`, from: `/docs/beta/${from}` },
    ...(isInStableDocs(to) ? [{ to: `/docs/${to}`, from: `/docs/${from}` }] : []),
]);

/** @type {import('@docusaurus/types').Config} */
const config = {
    title: "Floe",
    tagline:
        "Floe is a free, open-source sample library engine. Available as an audio plugin for Windows, macOS and Linux.",
    favicon: "images/favicon.svg",

    // Read through the useAssetUrl hook.
    customFields: {
        assetUrls: resolveAssetUrls("current", k_asset_options),
    },

    // Future flags, see https://docusaurus.io/docs/api/docusaurus-config#future
    future: {
        v4: true, // Improve compatibility with the upcoming Docusaurus v4
    },

    // Set the production url of your site here
    url: "https://floe.audio",
    // Set the /<baseUrl>/ pathname under which your site is served
    // For GitHub pages deployment, it is often '/<projectName>/'
    baseUrl: "/",

    // GitHub pages deployment config.
    organizationName: "floe-audio", // Usually your GitHub org/user name.
    projectName: "Floe", // Usually your repo name.

    // The manual build's /manual page holds every doc, so the docs' relative links only resolve once
    // the manual page rewrites them in the browser.
    onBrokenLinks: k_manual_options ? "ignore" : "throw",

    staticDirectories: ["static", ...(k_asset_options.manualScreenshotsDir ? [path.dirname(k_asset_options.manualScreenshotsDir)] : [])],

    headTags: [
        {
            tagName: "meta",
            attributes: {
                name: "algolia-site-verification",
                content: "5322EB0EFA41644D",
            },
        },
    ],

    // Global scripts
    scripts: [
        {
            src: "https://analytics.frozenplain.com/script.js",
            defer: true,
            "data-website-id": "09786971-9957-41b7-ad9c-fafdefed73f6",
        },
    ],

    // Even if you don't use internationalization, you can use this field to set
    // useful metadata like html lang. For example, if your site is Chinese, you
    // may want to replace "en" with "zh-Hans".
    i18n: {
        defaultLocale: "en",
        locales: ["en"],
    },

    presets: [
        [
            "classic",
            /** @type {import('@docusaurus/preset-classic').Options} */
            ({
                docs: {
                    sidebarPath: "./sidebars.js",
                    routeBasePath: "docs",
                    sidebarCollapsed: false,
                    beforeDefaultRemarkPlugins: [k_remark_asset_urls],
                    ...versionsConfig,
                },
                blog: {
                    beforeDefaultRemarkPlugins: [k_remark_asset_urls],
                    showReadingTime: true,
                    feedOptions: {
                        type: ["rss", "atom"],
                        xslt: true,
                    },
                    // Useful options to enforce blogging best practices
                    onInlineTags: "warn",
                    onInlineAuthors: "warn",
                    onUntruncatedBlogPosts: "warn",
                },
                pages: {
                    beforeDefaultRemarkPlugins: [k_remark_asset_urls],
                },
                theme: {
                    customCss: "./src/css/custom.css",
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
            algolia: {
                appId: "C36D44T8FT",
                apiKey: "bf9029d8547ce0a0bb10048fe56ea698",
                indexName: "Docs Website",
            },
            navbar: {
                // title: '',
                logo: {
                    alt: "Floe Logo",
                    src: "images/logo-light-mode.svg",
                    srcDark: "images/logo-dark-mode.svg",
                },
                items: [
                    {
                        to: "download",
                        position: "left",
                        label: "Download",
                    },
                    {
                        to: "packages",
                        position: "left",
                        label: "Packages",
                    },
                    {
                        type: "docSidebar",
                        sidebarId: "sidebar",
                        position: "left",
                        label: "Docs",
                    },
                    { to: "blog", label: "News", position: "left" },
                    {
                        type: "docsVersionDropdown",
                        position: "right",
                    },
                ],
            },
            footer: {
                style: "dark",
                links: [
                    {
                        items: [
                            {
                                label: "Home",
                                to: "/",
                            },
                            {
                                label: "Download",
                                to: "/download",
                            },
                        ],
                    },
                    {
                        items: [
                            {
                                label: "Docs",
                                to: "/docs/getting-started/quick-start-guide",
                            },
                            {
                                label: "Packages",
                                to: "/packages",
                            },
                        ],
                    },
                    {
                        items: [
                            {
                                label: "News",
                                to: "/blog",
                            },
                            {
                                label: "GitHub",
                                href: "https://github.com/floe-audio/Floe",
                            },
                            {
                                label: "Press",
                                to: "/press",
                            },
                        ],
                    },
                ],
                // copyright: `Copyright © ${new Date().getFullYear()} Sam Windell.`,
            },
            prism: {
                theme: prismThemes.github,
                darkTheme: prismThemes.dracula,
                additionalLanguages: ["lua", "ini", "bash"],
            },
        }),
    plugins: [
        "./plugins/packages-plugin",
        "./plugins/manual-plugin",
        () => ({
            name: "asset-urls-cache-key",
            configureWebpack: () => ({
                experiments: { cache: { buildDependencies: [writeResolvedAssetsCacheKey(k_asset_options)] } },
            }),
        }),
        [
            "@docusaurus/plugin-client-redirects",
            {
                fromExtensions: ["html", "htm"], // /myPage.html -> /myPage
                redirects: [
                    ...movedDocRedirects,
                    {
                        to: "/docs/beta/introduction/about-floe",
                        from: [
                            "/docs/beta/key-concepts/general-usage",
                        ],
                    },
                    {
                        to: "/docs/beta/usage/browsers",
                        from: [
                            "/docs/beta/key-concepts/browsers",
                        ],
                    },
                    {
                        to: "/docs/beta/installation/folders",
                        from: [
                            "/docs/beta/key-concepts/folders",
                        ],
                    },
                    {
                        to: "/docs/beta/reference/glossary",
                        from: [
                            "/docs/beta/key-concepts/components",
                        ],
                    },
                    {
                        to: stableDocRoute("getting-started/overview"),
                        from: [
                            "/docs/key-concepts/general-usage",
                        ],
                    },
                    {
                        to: "/docs/usage/browsers",
                        from: [
                            "/docs/key-concepts/browsers",
                        ],
                    },
                    {
                        to: stableDocRoute("usage/folders"),
                        from: [
                            "/docs/key-concepts/folders",
                        ],
                    },
                    {
                        to: stableDocRoute("getting-started/glossary"),
                        from: [
                            "/docs/key-concepts/components",
                        ],
                    },
                    // Mostly old routes from our previous mdbook-based site.
                    {
                        to: "/docs/installation/install-packages",
                        from: [
                            "/docs/packages/install-packages",
                            "/packages/install-packages",
                            "/packages/install-packages.html",
                        ],
                    },
                    {
                        to: stableDocRoute("usage/presets"),
                        from: [
                            "/usage/presets",
                            "/usage/presets.html",
                        ],
                    },
                    {
                        to: stableDocRoute("getting-started/glossary"),
                        from: [
                            "/docs/usage/sample-libraries",
                            "/usage/sample-libraries",
                            "/usage/sample-libraries.html",
                            "/docs/packages/about-packages",
                            "/packages/about-packages",
                            "/packages/about-packages.html",
                        ],
                    },
                    {
                        to: stableDocRoute("reference/autosave"),
                        from: [
                            "/docs/usage/autosave",
                            "/usage/autosave",
                            "/usage/autosave.html",
                        ],
                    },
                    {
                        to: "/docs/reference/attribution",
                        from: [
                            "/docs/usage/attribution",
                            "/usage/attribution",
                            "/usage/attribution.html",
                        ],
                    },
                    {
                        to: stableDocRoute("reference/error-reporting"),
                        from: [
                            "/docs/usage/error-reporting",
                            "/usage/error-reporting",
                            "/usage/error-reporting.html",
                        ],
                    },
                    {
                        to: "/docs/reference/parameters",
                        from: [
                            "/docs/usage/parameters",
                            "/usage/parameters",
                            "/usage/parameters.html",
                        ],
                    },
                    { to: "/", from: ["/home", "/home.html", "/about", "/about.html"] },
                    {
                        to: "/packages",
                        from: [
                            "/packages/available-packages",
                            "/packages/available-packages.html",
                        ],
                    },
                    {
                        to: "/download",
                        from: [
                            "/installation/download-and-install-floe.html",
                            "/installation/download-and-install-floe",
                            "/installation/updating",
                            "/installation/updating.html",
                            "/installation/requirements",
                            "/installation/requirements.html",
                        ],
                    },
                    {
                        to: "/docs/installation/download-and-install-floe",
                        from: ["/docs/installation/requirements"],
                    },
                    {
                        to: "/docs/getting-started/quick-start-guide",
                        from: ["/docs/introduction"],
                    },
                    {
                        to: "/docs/installation/uninstalling",
                        from: [
                            "/installation/uninstalling",
                            "/installation/uninstalling.html",
                        ],
                    },
                    {
                        to: "/docs/usage/layers",
                        from: ["/usage/layers", "/usage/layers.html"],
                    },
                    {
                        to: "/docs/usage/effects",
                        from: ["/usage/effects", "/usage/effects.html"],
                    },
                    {
                        to: "/docs/usage/key-ranges",
                        from: ["/usage/key-ranges", "/usage/key-ranges.html"],
                    },
                    { to: "/docs/usage/midi", from: ["/usage/midi", "/usage/midi.html"] },
                    {
                        to: "/docs/usage/looping",
                        from: ["/usage/looping", "/usage/looping.html"],
                    },
                    {
                        to: "/docs/usage/macros",
                        from: ["/usage/macros", "/usage/macros.html"],
                    },
                    {
                        to: "/docs/develop/develop-libraries",
                        from: [
                            "/develop/develop-libraries",
                            "/develop/develop-libraries.html",
                        ],
                    },
                    {
                        to: "/docs/develop/library-lua-scripts",
                        from: [
                            "/develop/library-lua-scripts",
                            "/develop/library-lua-scripts.html",
                        ],
                    },
                    {
                        to: "/docs/develop/develop-preset-packs",
                        from: [
                            "/develop/develop-preset-packs",
                            "/develop/develop-preset-packs.html",
                        ],
                    },
                    {
                        to: "/docs/develop/packaging",
                        from: ["/develop/packaging", "/develop/packaging.html"],
                    },
                    {
                        to: "/docs/develop/tags-and-folders",
                        from: [
                            "/develop/tags-and-folders",
                            "/develop/tags-and-folders.html",
                        ],
                    },
                    {
                        to: "/docs/about-the-project/mirage",
                        from: [
                            "/about-the-project/mirage",
                            "/about-the-project/mirage.html",
                        ],
                    },
                    { to: "/docs/changelog", from: ["/changelog", "/changelog.html"] },
                ],
            },
        ],
    ],
    markdown: {
        mermaid: true,
        // A doc with `manual_only: true` is left out of website builds as a draft. Drafts still show in
        // the dev server.
        parseFrontMatter: async (params) => {
            const result = await params.defaultParseFrontMatter(params);
            if (!k_manual_options && result.frontMatter.manual_only === true) {
                result.frontMatter.draft = true;
            }
            return result;
        },
    },
    themes: ["@docusaurus/theme-mermaid"],
};

export default config;
