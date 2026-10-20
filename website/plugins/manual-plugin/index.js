// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

const path = require('path');
const fs = require('fs');
const { createMDXLoaderRule } = require('@docusaurus/mdx-loader');

// A doc opts out of manual types with front matter, e.g. `manual_exclude: [library]`.
const k_manual_types = ['floe', 'library'];

// A doc with `manual_contents_headings: true` lists its H2s under its entry in the contents.

// A doc with `manual_keep_sections_together: true` keeps each H2 and its content on one page where it fits.

// A doc with `manual_library_title: "Install {library} Package"` takes that title in library manuals.

// The docs version names given by versions-config.js.
const k_docs_version_names = { stable: 'stable', beta: 'current' };

/**
 * Adds a /manual route containing the whole documentation as a single page, paginated for
 * A4 by Paged.js. Only active when FLOE_MANUAL is set so the normal website build never contains
 * it; scripts/generate-manual.mjs sets it.
 *
 * FLOE_MANUAL is JSON:
 *   type: 'floe' or 'library', selects which docs are included (see manual_exclude)
 *   library: for a library manual, { name, features, productUrl } (see src/components/libraryFeatures.mjs), else null
 *   docs: 'stable' or 'beta', the docs version to use
 *   title: used on the cover, in page footers and as the PDF title
 *   cover: whether to generate a cover
 *   firstPageNumber: the page number of the first rendered page, after any pages prepended to the PDF
 *   frontPages/backPages: absolute paths of MDX files to place between the table of contents and
 *     the docs, and after the docs. They may live outside the repo.
 *   libraryInserts: { slot: absolute path of an MDX file } that LibraryInsert shows within the docs
 *     (see src/components/libraryInserts.mjs)
 *   libraryPages: { slot: [absolute paths of MDX files] }, each placed as a section after the doc the
 *     slot names (see src/components/libraryInserts.mjs)
 *   screenshotsDir: optional absolute path of a folder of screenshots that replace the website's
 *     (see plugins/asset-urls.mjs)
 */
module.exports = async function manualPlugin(context) {
    if (!process.env.FLOE_MANUAL) {
        return { name: 'manual-plugin' };
    }

    const { siteDir, siteConfig } = context;
    const options = JSON.parse(process.env.FLOE_MANUAL);
    const manualType = options.type;
    const docsVersionName = k_docs_version_names[options.docs];
    const { frontPages, backPages } = options;
    const libraryInserts = options.libraryInserts ?? {};
    const libraryPages = options.libraryPages ?? {};
    const { k_library_page_slots } = await import('../../src/components/libraryInserts.mjs');
    const partialPages = [...frontPages, ...backPages, ...Object.values(libraryInserts), ...Object.values(libraryPages).flat()];

    const countMermaidDiagrams = (filePath) =>
        (fs.readFileSync(filePath, 'utf8').match(/^```mermaid/gm) || []).length;

    const resolveSitePath = (aliasedPath) => aliasedPath.replace(/^@site/, siteDir);

    // Front and back pages and library inserts and pages are compiled as MDX partials: no
    // metadata/routes, just a component.
    const mdxPagesRule =
        partialPages.length === 0
            ? null
            : await createMDXLoaderRule({
                include: partialPages,
                options: {
                    admonitions: true,
                    siteDir,
                    staticDirs: siteConfig.staticDirectories.map((dir) => path.resolve(siteDir, dir)),
                    markdownConfig: siteConfig.markdown,
                    beforeDefaultRemarkPlugins: [
                        [(await import('../asset-urls.mjs')).remarkAssetUrls, { manualScreenshotsDir: options.screenshotsDir }],
                    ],
                    isMDXPartial: () => true,
                    isMDXPartialFrontMatterWarningDisabled: true,
                },
            });

    const manualCssPath = path.join(__dirname, 'manual.css');

    return {
        name: 'manual-plugin',

        getPathsToWatch() {
            return [manualCssPath];
        },

        configureWebpack() {
            if (!mdxPagesRule) return {};
            return { module: { rules: [mdxPagesRule] } };
        },

        async allContentLoaded({ allContent, actions }) {
            const docsContent = allContent['docusaurus-plugin-content-docs'].default;
            const version = docsContent.loadedVersions.find((v) => v.versionName === docsVersionName);
            const docsById = new Map(version.docs.map((doc) => [doc.id, doc]));

            const collectDocIds = (items) =>
                items.flatMap((item) => {
                    switch (item.type) {
                        case 'doc':
                            return [item.id];
                        case 'category':
                            return collectDocIds(item.items);
                        default:
                            return [];
                    }
                });

            const isExcludedDoc = (doc) => {
                const excludedTypes = doc.frontMatter.manual_exclude ?? [];
                for (const type of excludedTypes) {
                    if (!k_manual_types.includes(type)) {
                        throw new Error(`${doc.source}: unknown manual_exclude type '${type}'`);
                    }
                }
                return excludedTypes.includes(manualType);
            };

            const sectionAnchor = (doc) => `section-${doc.id.replace(/[^a-zA-Z0-9]+/g, '-')}`;

            const sectionTitle = (doc) => {
                const libraryTitle = doc.frontMatter.manual_library_title;
                if (!options.library || libraryTitle === undefined) return doc.title;
                if (!libraryTitle.includes('{library}')) {
                    throw new Error(`${doc.source}: manual_library_title must contain {library}`);
                }
                return libraryTitle.replaceAll('{library}', options.library.name);
            };

            const docSection = (doc) => ({
                source: doc.source,
                title: sectionTitle(doc),
                permalink: doc.permalink,
                anchor: sectionAnchor(doc),
                listsHeadingsInContents: doc.frontMatter.manual_contents_headings === true,
                keepsSectionsTogether: doc.frontMatter.manual_keep_sections_together === true,
            });

            const libraryPageAnchor = (slot, pageIndex) => `section-library-page-${slot}-${pageIndex}`;

            const libraryPageSection = (slot, pageIndex, filePath) => {
                const titles = [...fs.readFileSync(filePath, 'utf8').matchAll(/^# (.+)$/gm)].map((match) => match[1].trim());
                if (titles.length !== 1) throw new Error(`${filePath}: a library page needs exactly one H1, its title`);
                return {
                    source: filePath,
                    title: titles[0],
                    permalink: null,
                    anchor: libraryPageAnchor(slot, pageIndex),
                    listsHeadingsInContents: false,
                    keepsSectionsTogether: false,
                };
            };

            const libraryPagesAfter = (docId) =>
                Object.entries(libraryPages)
                    .filter(([slot]) => k_library_page_slots[slot] === docId)
                    .flatMap(([slot, filePaths]) =>
                        filePaths.map((filePath, pageIndex) => libraryPageSection(slot, pageIndex, filePath))
                    );

            // Each top-level sidebar item becomes a chapter, and each of its docs a section, followed by
            // any library pages placed after it.
            const chapters = version.sidebars.sidebar
                .map((item) => {
                    const sections = collectDocIds([item])
                        .map((id) => docsById.get(id))
                        .filter((doc) => !isExcludedDoc(doc))
                        .flatMap((doc) => [docSection(doc), ...libraryPagesAfter(doc.id)]);
                    return {
                        label: item.label ?? sections[0]?.title,
                        sections,
                    };
                })
                .filter((chapter) => chapter.sections.length !== 0);

            const sectionAnchors = new Set(chapters.flatMap((chapter) => chapter.sections.map((section) => section.anchor)));
            for (const [slot, filePaths] of Object.entries(libraryPages)) {
                if (filePaths.length !== 0 && !sectionAnchors.has(libraryPageAnchor(slot, 0))) {
                    throw new Error(`Library page slot '${slot}' follows '${k_library_page_slots[slot]}', which isn't in this manual`);
                }
            }

            const floeVersion = fs.readFileSync(path.join(siteDir, '..', 'version.txt'), 'utf8').trim();

            const mermaidCount =
                chapters
                    .flatMap((chapter) => chapter.sections)
                    .reduce((sum, section) => sum + countMermaidDiagrams(resolveSitePath(section.source)), 0) +
                [...frontPages, ...backPages, ...Object.values(libraryInserts)].reduce((sum, p) => sum + countMermaidDiagrams(p), 0);

            // Margin box content can't come from the page's DOM, so the footer is appended as a rule.
            const footerRule = `@page { @bottom-left { content: ${JSON.stringify(options.title)}; } }`;
            // The first page counts as several, for the pages that will be prepended to the PDF.
            const firstPageNumberRule = `.pagedjs_first_page { --pagedjs-page-counter-increment: ${options.firstPageNumber ?? 1}; }`;

            actions.addRoute({
                path: '/manual',
                component: '@site/src/components/Manual',
                exact: true,
                modules: {
                    chapterContents: chapters.map((chapter) => ({
                        sections: chapter.sections.map((section) => ({ content: section.source })),
                    })),
                    frontPages: frontPages.map((filePath) => ({ content: filePath })),
                    backPages: backPages.map((filePath) => ({ content: filePath })),
                    libraryInserts: Object.fromEntries(
                        Object.entries(libraryInserts).map(([slot, filePath]) => [slot, { content: filePath }])
                    ),
                },
                props: {
                    manual: {
                        type: manualType,
                        library: options.library ?? null,
                        title: options.title,
                        version: floeVersion,
                        css: `${fs.readFileSync(manualCssPath, 'utf8')}\n${footerRule}\n${firstPageNumberRule}\n`,
                        showCover: options.cover,
                        mermaidCount,
                        chapters: chapters.map((chapter) => ({
                            label: chapter.label,
                            sections: chapter.sections.map(({ source, ...section }) => section),
                        })),
                    },
                },
            });
        },
    };
};
