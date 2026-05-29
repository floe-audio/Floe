#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Generates per-page .md files plus llms.txt / llms-full.txt from the rendered
// HTML produced by `docusaurus build`. Working from the rendered output rather
// than the .mdx source means tables, components and {data[...]} expressions are
// already resolved by Docusaurus' MDX/React pipeline.

const path = require('path');
const fs = require('fs');
const cheerio = require('cheerio');
const TurndownService = require('turndown');
const turndownPluginGfm = require('turndown-plugin-gfm');

const siteDir = path.resolve(__dirname, '..');
const buildDir = path.join(siteDir, 'build');
const docsRoot = path.join(buildDir, 'docs');
const siteUrl = 'https://floe.audio';

const turndown = new TurndownService({
    headingStyle: 'atx',
    codeBlockStyle: 'fenced',
    bulletListMarker: '-',
    emDelimiter: '_',
});
turndown.use(turndownPluginGfm.gfm);

// Drop interactive controls that would otherwise become noisy markdown.
turndown.remove(['script', 'style', 'button', 'input', 'video', 'audio']);
turndown.addRule('codeBlocks', {
    filter: (node) => node.nodeName === 'PRE',
    replacement: (_content, node) => {
        const text = node.textContent.replace(/\n+$/, '');
        const codeEl = node.querySelector('code');
        const langMatch = codeEl && (codeEl.className || '').match(/language-([\w-]+)/);
        const lang = langMatch ? langMatch[1] : '';
        return `\n\n\`\`\`${lang}\n${text}\n\`\`\`\n\n`;
    },
});

function walkHtml(dir, out) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) walkHtml(full, out);
        else if (entry.isFile() && entry.name === 'index.html') out.push(full);
    }
}

function extractPage(htmlPath) {
    const html = fs.readFileSync(htmlPath, 'utf8');
    const $ = cheerio.load(html);

    // Docs articles live under <div class="theme-doc-markdown markdown">.
    // Anything else (homepage, /packages, /download) is skipped.
    const article = $('.theme-doc-markdown.markdown').first();
    if (!article.length) return null;

    // Strip in-content widgets that don't carry textual information.
    article.find('.theme-edit-this-page, .pagination-nav, .theme-doc-toc-mobile, .theme-doc-toc-desktop').remove();

    // Heading anchor links Docusaurus injects ("Direct link to ...") become noise in markdown.
    article.find('a.hash-link').remove();

    const title = $('h1').first().text().trim() || $('title').text().trim();
    const description = $('meta[name="description"]').attr('content') || '';
    const markdown = turndown.turndown(article.html() || '').trim();
    return { title, description, markdown };
}

function htmlPathToUrlPath(htmlPath) {
    // build/docs/usage/effects/index.html -> /docs/usage/effects
    const rel = path.relative(buildDir, path.dirname(htmlPath));
    return '/' + rel.split(path.sep).join('/');
}

function htmlPathToMdPath(htmlPath) {
    // build/docs/usage/effects/index.html -> build/docs/usage/effects.md
    const dir = path.dirname(htmlPath);
    return dir + '.md';
}

if (!fs.existsSync(docsRoot)) {
    console.error('[generate-llms] build/docs not found; run docusaurus build first');
    process.exit(1);
}

const htmlFiles = [];
walkHtml(docsRoot, htmlFiles);
htmlFiles.sort();

const pages = [];
for (const htmlPath of htmlFiles) {
    const page = extractPage(htmlPath);
    if (!page) continue;

    const urlPath = htmlPathToUrlPath(htmlPath);
    const mdPath = htmlPathToMdPath(htmlPath);

    // The article already starts with an <h1>, so the page title is in `markdown`.
    // Only inject the description (from meta) which isn't part of the body.
    const body = page.description
        ? page.markdown.replace(/^(# .*\n)/, `$1\n> ${page.description}\n`) + '\n'
        : page.markdown + '\n';
    fs.writeFileSync(mdPath, body);
    pages.push({ ...page, urlPath, mdUrl: `${siteUrl}${urlPath}.md` });
}

// llms.txt — index of pages with .md links per llmstxt.org convention.
const indexLines = [
    '# Floe',
    '',
    '> Floe is a free, open-source sample library engine. Available as an audio plugin for Windows, macOS and Linux.',
    '',
    '## Documentation',
    '',
];
for (const p of pages) {
    indexLines.push(`- [${p.title}](${p.mdUrl})${p.description ? ` — ${p.description}` : ''}`);
}
fs.writeFileSync(path.join(buildDir, 'llms.txt'), indexLines.join('\n') + '\n');

// llms-full.txt — all pages concatenated.
const fullParts = [
    '# Floe',
    '',
    '> Floe is a free, open-source sample library engine. Available as an audio plugin for Windows, macOS and Linux.',
    '',
];
for (const p of pages) {
    fullParts.push('\n---\n\n');
    fullParts.push(p.description
        ? p.markdown.replace(/^(# .*\n)/, `$1\n> ${p.description}\n`)
        : p.markdown);
    fullParts.push('\n');
}
fs.writeFileSync(path.join(buildDir, 'llms-full.txt'), fullParts.join(''));

console.log(`[generate-llms] wrote ${pages.length} markdown pages, llms.txt, llms-full.txt`);
