#!/usr/bin/env node
// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Generates an A4 PDF user manual from the docs. Builds the site with the /manual route
// enabled (plugins/manual-plugin), serves it locally, waits for Paged.js to paginate it in headless
// Chrome, then prints it to PDF.
//
// Usage: node website/scripts/generate-manual.mjs [options]
//   --library <name>         Make a sample library's manual rather than Floe's. Docs opt out of a
//                            manual with `manual_exclude: [floe, library]` front matter.
//   --library-config <file>  Required with --library: the library's manual.json, declaring what
//                            the library contains so its manual only explains what applies, and
//                            where its product page is, e.g.
//                            { "features": ["impulse-responses"], "product_url": "https://..." }.
//                            See src/components/libraryFeatures.mjs. Optionally, "inserts" maps slots
//                            to MDX files (relative to manual.json) that the library adds within the
//                            docs, e.g. { "faq": "faq.mdx" }, and "pages" does the same for whole pages
//                            between docs, e.g. { "after-quick-start": ["story.mdx", "presets.mdx"] }. See
//                            src/components/libraryInserts.mjs.
//   --docs <version>         stable (default) or beta
//   --front <file>           Before the docs: a .pdf is joined at the very start in place of the
//                            generated cover, an .mdx page follows the contents. Repeatable, PDFs first.
//   --back <file>            After the docs: an .mdx page, or a .pdf joined at the very end.
//                            Repeatable, PDFs last.
//   --out <file.pdf>         Output path (default: zig-out/<title-as-filename>.pdf)
//   --screenshots-dir <dir>  Use these screenshots instead of the website's. zig build passes this
//                            when given -Dscreenshot-presets.
//   --skip-build             Reuse the previous manual build of the site
//
// Chrome is found via PUPPETEER_EXECUTABLE_PATH, then PATH (chromium, google-chrome-stable, ...).

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PDFDocument } from 'pdf-lib';
import puppeteer from 'puppeteer-core';
import { checkLibraryFeature } from '../src/components/libraryFeatures.mjs';
import { checkLibraryInsertSlot, checkLibraryPageSlot } from '../src/components/libraryInserts.mjs';

const siteDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const repoDir = path.resolve(siteDir, '..');
const buildDir = path.join(siteDir, 'build-manual');

const isPdf = (file) => path.extname(file).toLowerCase() === '.pdf';

// Each flag's PDFs and MDX pages go to different places, so require the order to match.
function splitPdfsAndPages(files, flag, pdfsFirst) {
    for (const file of files) {
        if (!isPdf(file) && path.extname(file) !== '.mdx') throw new Error(`${flag} takes a .pdf or .mdx file: ${file}`);
        if (!fs.existsSync(file)) throw new Error(`File not found: ${file}`);
    }
    const pdfs = files.filter(isPdf);
    const pages = files.filter((file) => !isPdf(file));
    const expectedOrder = pdfsFirst ? [...pdfs, ...pages] : [...pages, ...pdfs];
    if (expectedOrder.some((file, index) => file !== files[index])) {
        throw new Error(`Give ${flag} .pdf files ${pdfsFirst ? 'before' : 'after'} .mdx pages`);
    }
    return { pdfs, pages };
}

// A library's manual.json: { "features": [...], "product_url": "...", "inserts": { slot: "file.mdx" },
// "pages": { slot: ["file.mdx", ...] } }, see src/components/libraryFeatures.mjs and
// src/components/libraryInserts.mjs. Insert and page paths are returned absolute.
function readLibraryConfig(file) {
    const config = JSON.parse(fs.readFileSync(file, 'utf8'));
    for (const key of Object.keys(config)) {
        if (!['features', 'product_url', 'inserts', 'pages'].includes(key)) throw new Error(`${file}: unknown key '${key}'`);
    }
    const resolveSlotFile = (slot, slotFile, kind) => {
        const slotPath = path.resolve(path.dirname(file), slotFile);
        if (path.extname(slotPath) !== '.mdx') throw new Error(`${file}: ${kind} '${slot}' must be an .mdx file`);
        if (!fs.existsSync(slotPath)) throw new Error(`${file}: ${kind} '${slot}' not found: ${slotPath}`);
        return slotPath;
    };
    const inserts = {};
    for (const [slot, insertFile] of Object.entries(config.inserts ?? {})) {
        checkLibraryInsertSlot(slot, file);
        inserts[slot] = resolveSlotFile(slot, insertFile, 'insert');
    }
    config.inserts = inserts;
    const pages = {};
    for (const [slot, pageFiles] of Object.entries(config.pages ?? {})) {
        checkLibraryPageSlot(slot, file);
        if (!Array.isArray(pageFiles)) throw new Error(`${file}: page slot '${slot}' must be an array of files`);
        pages[slot] = pageFiles.map((pageFile) => resolveSlotFile(slot, pageFile, 'page'));
    }
    config.pages = pages;
    if (!Array.isArray(config.features)) throw new Error(`${file}: features must be an array`);
    if (typeof config.product_url !== 'string' || !config.product_url.startsWith('https://')) {
        throw new Error(`${file}: product_url must be an https:// URL`);
    }
    for (const feature of config.features) checkLibraryFeature(feature, file);
    return config;
}

function parseArgs(argv) {
    let library;
    let libraryConfigFile;
    let docs = 'stable';
    let out;
    let screenshotsDir;
    let build = true;
    const frontFiles = [];
    const backFiles = [];
    for (let index = 0; index < argv.length; ++index) {
        const flag = argv[index];
        const takeValue = () => {
            const value = argv[++index];
            if (!value) throw new Error(`${flag} needs a value`);
            return value;
        };
        switch (flag) {
            case '--library': library = takeValue(); break;
            case '--library-config': libraryConfigFile = path.resolve(takeValue()); break;
            case '--docs': docs = takeValue(); break;
            case '--front': frontFiles.push(path.resolve(takeValue())); break;
            case '--back': backFiles.push(path.resolve(takeValue())); break;
            case '--out': out = path.resolve(takeValue()); break;
            case '--screenshots-dir': screenshotsDir = path.resolve(takeValue()); break;
            case '--skip-build': build = false; break;
            default: throw new Error(`Unknown argument: ${flag}`);
        }
    }
    if (!['stable', 'beta'].includes(docs)) throw new Error('--docs needs stable or beta');
    if (screenshotsDir && !fs.statSync(screenshotsDir, { throwIfNoEntry: false })?.isDirectory()) {
        throw new Error(`--screenshots-dir needs a folder: ${screenshotsDir}`);
    }
    if (Boolean(library) !== Boolean(libraryConfigFile)) throw new Error('--library and --library-config go together');
    const libraryConfig = libraryConfigFile ? readLibraryConfig(libraryConfigFile) : null;
    const front = splitPdfsAndPages(frontFiles, '--front', true);
    const back = splitPdfsAndPages(backFiles, '--back', false);
    const title = library ? `${library} User Manual` : 'Floe User Manual';
    return {
        type: library ? 'library' : 'floe',
        library: library ? { name: library, features: libraryConfig.features, productUrl: libraryConfig.product_url } : null,
        libraryInserts: libraryConfig?.inserts ?? {},
        libraryPages: libraryConfig?.pages ?? {},
        docs,
        title,
        prependPdfs: front.pdfs,
        frontPages: front.pages,
        backPages: back.pages,
        appendPdfs: back.pdfs,
        screenshotsDir,
        build,
        out: out ?? path.join(repoDir, 'zig-out', `${title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '')}.pdf`),
    };
}

function findChrome() {
    if (process.env.PUPPETEER_EXECUTABLE_PATH) return process.env.PUPPETEER_EXECUTABLE_PATH;
    for (const dir of (process.env.PATH || '').split(path.delimiter)) {
        for (const name of ['chromium', 'chromium-browser', 'google-chrome-stable', 'google-chrome']) {
            const candidate = path.join(dir, name);
            if (fs.existsSync(candidate)) return candidate;
        }
    }
    const macChrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
    if (fs.existsSync(macChrome)) return macChrome;
    throw new Error('Chrome/Chromium not found; set PUPPETEER_EXECUTABLE_PATH');
}

function run(command, commandArgs, options) {
    return new Promise((resolve, reject) => {
        const child = spawn(command, commandArgs, { stdio: 'inherit', ...options });
        child.on('exit', (code) =>
            code === 0 ? resolve() : reject(new Error(`${command} exited with code ${code}`))
        );
    });
}

const k_mime_types = {
    '.html': 'text/html',
    '.js': 'text/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.jpg': 'image/jpeg',
    '.webp': 'image/webp',
    '.woff2': 'font/woff2',
};

function serveDirectory(dir) {
    const server = http.createServer((request, response) => {
        const urlPath = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
        const candidates = [path.join(dir, urlPath), path.join(dir, urlPath, 'index.html'), path.join(dir, `${urlPath}.html`)];
        const filePath = candidates.find((candidate) => candidate.startsWith(dir) && fs.statSync(candidate, { throwIfNoEntry: false })?.isFile());
        if (!filePath) {
            response.writeHead(404).end();
            return;
        }
        response.writeHead(200, { 'Content-Type': k_mime_types[path.extname(filePath)] || 'application/octet-stream' });
        fs.createReadStream(filePath).pipe(response);
    });
    return new Promise((resolve) => server.listen(0, '127.0.0.1', () => resolve(server)));
}

async function renderPdf(url, outPath) {
    const browser = await puppeteer.launch({ executablePath: findChrome(), headless: true });
    try {
        const page = await browser.newPage();
        page.on('response', (response) => {
            if (response.status() >= 400) console.error(`[page] ${response.status()} ${response.url()}`);
        });
        page.on('console', (message) => {
            if (message.type() === 'error' && !message.text().startsWith('Failed to load resource')) {
                console.error(`[page] ${message.text()}`);
            }
        });
        await page.emulateMediaFeatures([{ name: 'prefers-color-scheme', value: 'light' }]);
        await page.goto(url, { waitUntil: 'networkidle0', timeout: 120000 });
        console.log('Paginating...');
        await page.waitForFunction('window.__floeManualReady === true || window.__floeManualError !== undefined', {
            timeout: 600000,
            polling: 500,
        });
        const error = await page.evaluate(() => window.__floeManualError);
        if (error) throw new Error(`Pagination failed:\n${error}`);
        console.log('Printing...');
        await page.pdf({
            path: outPath,
            preferCSSPageSize: true,
            printBackground: true,
            outline: true,
            tagged: true,
            timeout: 0,
        });
    } finally {
        await browser.close();
    }
}

const args = parseArgs(process.argv.slice(2));

// Prepended PDFs (such as a cover) count towards the page numbers, so they match a printer's.
let prependedPageCount = 0;
for (const file of args.prependPdfs) {
    prependedPageCount += (await PDFDocument.load(fs.readFileSync(file))).getPageCount();
}

if (args.build) {
    await run('npx', ['docusaurus', 'build', '--out-dir', buildDir], {
        cwd: siteDir,
        env: {
            ...process.env,
            FLOE_MANUAL: JSON.stringify({
                type: args.type,
                library: args.library,
                docs: args.docs,
                title: args.title,
                cover: args.prependPdfs.length === 0,
                firstPageNumber: prependedPageCount + 1,
                frontPages: args.frontPages,
                backPages: args.backPages,
                libraryInserts: args.libraryInserts,
                libraryPages: args.libraryPages,
                screenshotsDir: args.screenshotsDir,
            }),
        },
    });
}

const joinPdfs = args.prependPdfs.length + args.appendPdfs.length > 0;
const manualPdf = joinPdfs ? path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'floe-manual-')), 'manual.pdf') : args.out;
fs.mkdirSync(path.dirname(args.out), { recursive: true });

const server = await serveDirectory(buildDir);
try {
    await renderPdf(`http://127.0.0.1:${server.address().port}/manual`, manualPdf);
} finally {
    server.close();
}

// The extra PDFs are inserted into the manual's own document rather than concatenating files, so
// that its outline and named destinations (internal links) survive.
if (joinPdfs) {
    const manual = await PDFDocument.load(fs.readFileSync(manualPdf));
    const copyAllPages = async (file) => {
        const source = await PDFDocument.load(fs.readFileSync(file));
        return manual.copyPages(source, source.getPageIndices());
    };
    let insertIndex = 0;
    for (const file of args.prependPdfs) {
        for (const page of await copyAllPages(file)) manual.insertPage(insertIndex++, page);
    }
    for (const file of args.appendPdfs) {
        for (const page of await copyAllPages(file)) manual.addPage(page);
    }
    fs.writeFileSync(args.out, await manual.save());
}

console.log(`Manual written to ${args.out}`);
