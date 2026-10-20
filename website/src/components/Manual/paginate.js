// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

const k_site_url = 'https://floe.audio';

async function waitFor(condition, description, timeoutMs = 60000) {
    const start = performance.now();
    while (!condition()) {
        if (performance.now() - start > timeoutMs) throw new Error(`Timed out waiting for ${description}`);
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
}

// The docs are written as standalone web pages; in a single document their heading ids collide,
// their links point at other web pages, and their h2s sit directly under the section titles.
function prepareForManual(content, manual) {
    content.querySelectorAll('.hash-link, button').forEach((element) => element.remove());
    content.querySelectorAll('img').forEach((img) => {
        img.loading = 'eager';
    });

    const anchorForPermalink = new Map(
        manual.chapters.flatMap((chapter) => chapter.sections.map((section) => [section.permalink, section.anchor]))
    );

    for (const article of content.querySelectorAll('.manual-section')) {
        const sectionAnchor = article.id;
        const body = article.querySelector('.manual-section-body');

        // Some docs carry their own H1; the section title already covers it.
        body.querySelector('h1')?.remove();

        for (const video of body.querySelectorAll('video')) {
            const note = document.createElement('p');
            note.className = 'manual-video-note';
            const link = document.createElement('a');
            link.href = `${k_site_url}${article.dataset.permalink}`;
            link.textContent = `${k_site_url.replace('https://', '')}${article.dataset.permalink}`;
            note.append('Video available in the online docs: ', link);
            video.replaceWith(note);
        }

        for (const element of body.querySelectorAll('[id]')) {
            if (element.closest('svg')) continue;
            element.id = `${sectionAnchor}--${element.id}`;
        }
        for (const link of body.querySelectorAll('a[href^="#"]')) {
            link.setAttribute('href', `#${sectionAnchor}--${link.getAttribute('href').slice(1)}`);
        }

        // Read from the rendered body rather than the doc's static TOC so curated-out headings are left out.
        if (article.dataset.listsHeadingsInContents) {
            const contentsEntry = content.querySelector(`.manual-toc a[href="#${sectionAnchor}"]`).parentElement;
            const headingList = document.createElement('ol');
            headingList.className = 'manual-toc-headings';
            for (const heading of body.querySelectorAll('h2')) {
                const item = document.createElement('li');
                const link = document.createElement('a');
                link.href = `#${heading.id}`;
                link.textContent = heading.textContent.trim();
                item.append(link);
                headingList.append(item);
            }
            contentsEntry.append(headingList);
        }

        if (article.dataset.keepsSectionsTogether) {
            for (const heading of body.querySelectorAll('h2')) {
                const group = document.createElement('div');
                group.className = 'manual-keep-together';
                heading.before(group);
                let node = heading;
                while (node && !(node !== heading && node.tagName === 'H2')) {
                    const next = node.nextSibling;
                    group.append(node);
                    node = next;
                }
            }
        }

        for (const heading of body.querySelectorAll('h2, h3, h4, h5')) {
            const demoted = document.createElement(`h${Number(heading.tagName[1]) + 1}`);
            for (const attribute of heading.attributes) demoted.setAttribute(attribute.name, attribute.value);
            demoted.replaceChildren(...heading.childNodes);
            heading.replaceWith(demoted);
        }
    }

    // Front and back pages have no metadata, so their contents entries come from their rendered H1s.
    const contentsChapters = content.querySelector('.manual-toc-chapters');
    const firstDocsEntry = contentsChapters.firstElementChild;
    content.querySelectorAll('.manual-mdx-page').forEach((page, pageIndex) => {
        page.querySelectorAll('h1').forEach((heading, headingIndex) => {
            heading.id = `page-${pageIndex}-${headingIndex}`;
            const item = document.createElement('li');
            item.className = 'manual-toc-chapter';
            const link = document.createElement('a');
            link.href = `#${heading.id}`;
            link.textContent = heading.textContent.trim();
            item.append(link);
            switch (page.dataset.position) {
                case 'front':
                    contentsChapters.insertBefore(item, firstDocsEntry);
                    break;
                case 'back':
                    contentsChapters.append(item);
                    break;
                default:
                    throw new Error(`Unknown page position: ${page.dataset.position}`);
            }
        });
    });

    for (const link of content.querySelectorAll('a[href]')) {
        const href = link.getAttribute('href');
        if (href.startsWith('#')) continue;
        // Docs use page-relative links such as ./midi, so resolve against the doc's own URL.
        const basePath = link.closest('.manual-section')?.dataset.permalink ?? '/';
        const url = new URL(href, window.location.origin + basePath);
        if (url.origin !== window.location.origin) continue;

        const sectionAnchor = anchorForPermalink.get(url.pathname.replace(/\/$/, ''));
        if (sectionAnchor) {
            const hash = url.hash.slice(1);
            link.setAttribute('href', hash ? `#${sectionAnchor}--${hash}` : `#${sectionAnchor}`);
        } else {
            link.setAttribute('href', `${k_site_url}${url.pathname}${url.hash}`);
        }
    }
}

// Doc images are mostly 2x screenshots; without this a 580px crop of one panel is as wide as the
// page. With this scale the 1800px full-window screenshots still fill the text width.
async function sizeImagesByPixelWidth(content) {
    const images = [...content.querySelectorAll('.manual-section-body img:not(.generated-screenshot img)')];
    await Promise.all(images.map((img) => img.decode().catch(() => {})));
    for (const img of images) {
        if (img.naturalWidth) img.style.width = `${img.naturalWidth * 0.15}mm`;
    }
}

export async function paginate({ source, target, manual }) {
    await waitFor(
        () => source.querySelectorAll('.docusaurus-mermaid-container svg').length >= manual.mermaidCount,
        'Mermaid diagrams'
    );
    await document.fonts.ready;

    const content = document.createDocumentFragment();
    for (const child of source.children) content.appendChild(child.cloneNode(true));
    prepareForManual(content, manual);
    await sizeImagesByPixelWidth(content);

    // The hidden source must not compete with the paginated copy for link targets.
    source.querySelectorAll('[id]').forEach((element) => element.removeAttribute('id'));

    const { Previewer, Handler, registerHandlers } = await import('pagedjs');

    // Paged.js only honours a heading's break-after: avoid when the content after it starts below the
    // page; content it moves whole (images, admonitions) still leaves the heading stranded. So when
    // the overflow starts straight after a heading that isn't the page's first content, start the
    // overflow at the heading instead.
    class KeepHeadingsWithNext extends Handler {
        onOverflow(overflow, rendered) {
            if (!overflow || overflow.startContainer.nodeType !== Node.ELEMENT_NODE) return;
            let node = overflow.startContainer.childNodes[overflow.startOffset];
            while (node && node !== rendered && !node.previousElementSibling) node = node.parentNode;
            let heading = node && node !== rendered ? node.previousElementSibling : null;
            if (!heading || !/^H[1-6]$/.test(heading.tagName)) return;
            while (heading.previousElementSibling && /^H[1-6]$/.test(heading.previousElementSibling.tagName)) {
                heading = heading.previousElementSibling;
            }

            const before = document.createRange();
            before.setStart(rendered, 0);
            before.setEndBefore(heading);
            if (!before.toString().trim()) return;

            const moved = overflow.cloneRange();
            moved.setStartBefore(heading);
            return moved;
        }
    }
    registerHandlers(KeepHeadingsWithNext);

    // Only the manual stylesheet goes through Paged.js; the site's CSS stays live in the document.
    await new Previewer().preview(content, [{ [`${window.location.origin}/manual.css`]: manual.css }], target);
}
