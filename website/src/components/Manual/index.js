// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import React, { useEffect, useRef } from 'react';
import Head from '@docusaurus/Head';
import LayoutProvider from '@theme/Layout/Provider';
import { MDXProvider } from '@mdx-js/react';
import MDXComponents from '@theme/MDXComponents';
import { ManualContext } from '@site/src/components/ManualContext';
import { paginate } from './paginate';
import useAssetUrl from '@site/src/components/useAssetUrl';
// Static (non-variable) fonts: Chrome embeds variable fonts in PDFs as Type 3, which some
// viewers render poorly.
import '@fontsource/source-sans-3/400.css';
import '@fontsource/source-sans-3/400-italic.css';
import '@fontsource/source-sans-3/600.css';
import '@fontsource/source-sans-3/700.css';
import '@fontsource/jetbrains-mono/400.css';
import '@fontsource/jetbrains-mono/600.css';

// Docusaurus's link component resolves a doc's relative links (./midi) against the /manual route,
// losing which doc they came from. Plain anchors keep the href as written so the paginator can
// resolve it against the doc's own permalink.
const k_manual_mdx_components = { ...MDXComponents, a: (props) => <a {...props} /> };

function ManualMdxContent({ children }) {
    return <MDXProvider components={k_manual_mdx_components}>{children}</MDXProvider>;
}

/**
 * The whole stable documentation as one page, paginated for A4 by Paged.js. Only routed in the
 * manual build (see plugins/manual-plugin). The React tree renders into a hidden source element;
 * once everything has rendered, a transformed copy is flowed into pages and
 * window.__floeManualReady is set for the manual script.
 */
export default function Manual({ manual, chapterContents, frontPages, backPages, libraryInserts }) {
    const sourceRef = useRef(null);
    const pagesRef = useRef(null);

    useEffect(() => {
        paginate({ source: sourceRef.current, target: pagesRef.current, manual })
            .then(() => {
                window.__floeManualReady = true;
            })
            .catch((error) => {
                console.error(error);
                window.__floeManualError = String(error?.stack ?? error);
            });
    }, []);

    return (
        <LayoutProvider>
            <ManualContext.Provider
                value={{
                    type: manual.type,
                    library: manual.library,
                    libraryInserts: Object.fromEntries(
                        Object.entries(libraryInserts ?? {}).map(([slot, insert]) => [slot, insert.content])
                    ),
                }}
            >
                <Head>
                    <html className="floe-manual" data-theme="light" />
                    <title>{manual.title}</title>
                </Head>

                <div ref={sourceRef} className="manual-source">
                    {manual.showCover && <Cover manual={manual} />}

                    <TableOfContents chapters={manual.chapters} />

                    {frontPages.map((page, index) => (
                        <MdxPage key={index} position="front" Content={page.content} />
                    ))}

                    {manual.chapters.map((chapter, chapterIndex) =>
                        chapter.sections.map((section, sectionIndex) => (
                            <Section
                                key={section.anchor}
                                section={section}
                                chapterLabel={chapter.label}
                                isGrouped={chapter.sections.length > 1}
                                isFirstInGroup={sectionIndex === 0}
                                Content={chapterContents[chapterIndex].sections[sectionIndex].content}
                            />
                        ))
                    )}

                    {backPages.map((page, index) => (
                        <MdxPage key={index} position="back" Content={page.content} />
                    ))}
                </div>

                <div ref={pagesRef} className="manual-pages" />
            </ManualContext.Provider>
        </LayoutProvider>
    );
}

function Cover({ manual }) {
    switch (manual.type) {
        case 'floe':
            return <FloeCover version={manual.version} />;
        case 'library':
            return <LibraryCover title={manual.title} />;
    }
    throw new Error(`Unknown manual type: ${manual.type}`);
}

// A plain placeholder: libraries are expected to prepend their own designed cover.
function LibraryCover({ title }) {
    return (
        <section className="manual-cover">
            <div className="manual-cover-title">
                <h1>{title}</h1>
            </div>
        </section>
    );
}

function FloeCover({ version }) {
    const assetUrl = useAssetUrl();
    return (
        <section className="manual-cover">
            <img className="manual-cover-logo" src="/images/logo-light-mode.svg" alt="Floe" />
            <div className="manual-cover-title">
                <h1>User Manual</h1>
                <p className="manual-cover-tagline">Free, open-source sample library engine</p>
            </div>
            <img
                className="manual-cover-screenshot"
                src={assetUrl('screenshots/floe-whole-gui-1.png')}
                alt="Floe's main window"
            />
            <div className="manual-cover-footer">
                <span>Version {version}</span>
                <span>floe.audio</span>
            </div>
        </section>
    );
}

function TableOfContents({ chapters }) {
    return (
        <nav className="manual-toc">
            <h1 className="manual-toc-title">Contents</h1>
            <ol className="manual-toc-chapters">
                {chapters.map((chapter) =>
                    chapter.sections.length === 1 ? (
                        <li key={chapter.label} className="manual-toc-chapter">
                            <a href={`#${chapter.sections[0].anchor}`}>{chapter.label}</a>
                        </li>
                    ) : (
                        <li key={chapter.label} className="manual-toc-chapter">
                            <span className="manual-toc-chapter-label">{chapter.label}</span>
                            <ol className="manual-toc-sections">
                                {chapter.sections.map((section) => (
                                    <li key={section.anchor}>
                                        <a href={`#${section.anchor}`}>{section.title}</a>
                                    </li>
                                ))}
                            </ol>
                        </li>
                    )
                )}
            </ol>
        </nav>
    );
}

// A grouped section's title is an h2 under its chapter's h1 so the PDF outline keeps the grouping;
// the chapter h1 is hidden on the page, where the running header already names the chapter.
function Section({ section, chapterLabel, isGrouped, isFirstInGroup, Content }) {
    const TitleTag = isGrouped ? 'h2' : 'h1';
    return (
        <article
            className="manual-section"
            id={section.anchor}
            data-chapter={chapterLabel}
            data-permalink={section.permalink}
            data-lists-headings-in-contents={section.listsHeadingsInContents || undefined}
            data-keeps-sections-together={section.keepsSectionsTogether || undefined}
        >
            {isGrouped && isFirstInGroup && <h1 className="manual-chapter-outline-heading">{chapterLabel}</h1>}
            <TitleTag className="manual-section-title">{section.title}</TitleTag>
            <div className="manual-section-body theme-doc-markdown markdown">
                <ManualMdxContent>
                    <Content />
                </ManualMdxContent>
            </div>
        </article>
    );
}

function MdxPage({ position, Content }) {
    return (
        <section className="manual-mdx-page markdown" data-position={position}>
            <ManualMdxContent>
                <Content />
            </ManualMdxContent>
        </section>
    );
}
