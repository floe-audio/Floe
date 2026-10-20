// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Images and videos in static/images aren't committed; they're hosted on assets.floe.audio under
// content-hashed names. A manifest maps each path (relative to static/images) to the hash it currently
// refers to. Stable docs have their own manifest so that new beta media doesn't change them.
//
// A generated screenshot can have an overlay JSON sidecar, written by the same capture, giving its
// numbered boxes. The manifest stores the overlay alongside the image's hash so the pair stays matched.
//
// A file that exists locally is served from static/images rather than the CDN, so freshly generated
// media shows up before it's uploaded.
//
// A manual build can replace screenshots with ones rendered from a different folder of screenshot
// presets. docusaurus.config.js adds manualScreenshotsDir's parent as a static directory, so they're
// served from /<folder name>/.

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const k_site_dir = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
export const k_local_assets_dir = path.join(k_site_dir, "static/images");
export const k_manifest_paths = {
    current: path.join(k_site_dir, "asset-manifest.json"),
    stable: path.join(k_site_dir, "versioned_asset_manifests/version-stable.json"),
};
export const k_cdn_url = "https://assets.floe.audio";
export const k_bucket_prefix = "website";

const k_media_extensions = [".png", ".jpg", ".jpeg", ".gif", ".webp", ".mp4", ".webm"];

// Referenced by absolute URL from outside the website's own pages.
const k_kept_in_repo = ["favicon.png"];

export function isHostedAsset(relPath) {
    return k_media_extensions.includes(path.extname(relPath).toLowerCase()) && !k_kept_in_repo.includes(relPath);
}

export function listLocalAssets() {
    if (!fs.existsSync(k_local_assets_dir)) return [];
    return fs
        .readdirSync(k_local_assets_dir, { recursive: true })
        .map((relPath) => relPath.split(path.sep).join("/"))
        .filter((relPath) => isHostedAsset(relPath))
        .sort();
}

export function hashFile(filePath) {
    return crypto.createHash("sha256").update(fs.readFileSync(filePath)).digest("hex").slice(0, 16);
}

export function bucketKey(relPath, hash) {
    const extension = path.posix.extname(relPath);
    return `${k_bucket_prefix}/${relPath.slice(0, -extension.length)}.${hash}${extension}`;
}

function readOverlayBeside(pngPath) {
    const overlayPath = pngPath.replace(/\.png$/, ".json");
    if (!pngPath.endsWith(".png") || !fs.existsSync(overlayPath)) return undefined;
    return JSON.parse(fs.readFileSync(overlayPath, "utf8"));
}

export function readLocalOverlay(relPath) {
    return readOverlayBeside(path.join(k_local_assets_dir, relPath));
}

// The manifest entry a local file should have once it's uploaded.
export function localManifestEntry(relPath) {
    const overlay = readLocalOverlay(relPath);
    return {
        hash: hashFile(path.join(k_local_assets_dir, relPath)),
        ...(overlay !== undefined && { overlay }),
    };
}

export function readManifest(version) {
    const manifestPath = k_manifest_paths[version];
    if (!fs.existsSync(manifestPath)) return {};
    return JSON.parse(fs.readFileSync(manifestPath, "utf8"));
}

export function writeManifest(version, manifest) {
    const sorted = Object.fromEntries(Object.entries(manifest).sort(([a], [b]) => a.localeCompare(b)));
    fs.mkdirSync(path.dirname(k_manifest_paths[version]), { recursive: true });
    fs.writeFileSync(k_manifest_paths[version], JSON.stringify(sorted, null, 4) + "\n");
}

// Map of every known asset path (relative to static/images) to { url, overlay? }.
export function resolveAssets(version, { manualScreenshotsDir } = {}) {
    const assets = {};
    for (const [relPath, { hash, overlay }] of Object.entries(readManifest(version)))
        assets[relPath] = { url: `${k_cdn_url}/${bucketKey(relPath, hash)}`, overlay };
    for (const relPath of listLocalAssets())
        assets[relPath] = { url: `/images/${relPath}`, overlay: readLocalOverlay(relPath) };
    if (manualScreenshotsDir) {
        for (const name of fs.readdirSync(manualScreenshotsDir).filter((name) => name.endsWith(".png")))
            assets[`screenshots/${name}`] = {
                url: `/${path.basename(manualScreenshotsDir)}/${name}`,
                overlay: readOverlayBeside(path.join(manualScreenshotsDir, name)),
            };
    }
    return assets;
}

// The remark plugin bakes assets into compiled MDX, but the bundler's persistent cache only tracks the
// MDX files themselves, so it would keep serving stale overlays and URLs. Writing every resolved asset
// to a file that's declared as a cache build dependency evicts the cache whenever any of them change.
export function writeResolvedAssetsCacheKey(options) {
    const keyPath = path.join(k_site_dir, "node_modules/.cache/asset-urls/resolved-assets.json");
    const contents = JSON.stringify({
        current: resolveAssets("current", options),
        stable: resolveAssets("stable", options),
    });
    fs.mkdirSync(path.dirname(keyPath), { recursive: true });
    if (!fs.existsSync(keyPath) || fs.readFileSync(keyPath, "utf8") !== contents) fs.writeFileSync(keyPath, contents);
    return keyPath;
}

export function resolveAssetUrls(version, options) {
    return Object.fromEntries(
        Object.entries(resolveAssets(version, options)).map(([relPath, { url }]) => [relPath, url])
    );
}

export function warnAboutUnuploadedAssets() {
    const manifest = readManifest("current");
    const unuploaded = listLocalAssets().filter(
        (relPath) => JSON.stringify(manifest[relPath]) !== JSON.stringify(localManifestEntry(relPath))
    );
    if (unuploaded.length === 0) return;
    console.warn(
        `[asset-urls] ${unuploaded.length} file(s) in static/images aren't uploaded, so the deployed site won't ` +
            `show them. Run: zig build script:website-upload-assets\n  ${unuploaded.join("\n  ")}`
    );
}

const k_url_prefix = "/images/";

function versionOfFile(filePath) {
    return filePath && filePath.split(path.sep).includes("versioned_docs") ? "stable" : "current";
}

function visit(node, callback) {
    callback(node);
    for (const child of node.children ?? []) visit(child, callback);
}

// Rewrites /images/... references in Markdown images and JSX src attributes, and gives each
// GeneratedScreenshot its src and overlay, since only here do we know which docs version the file
// belongs to.
export function remarkAssetUrls(options) {
    const assetsByVersion = { current: resolveAssets("current", options), stable: resolveAssets("stable", options) };

    return (tree, file) => {
        const assets = assetsByVersion[versionOfFile(file.path)];
        const resolveAsset = (relPath) => {
            const asset = assets[relPath];
            if (asset === undefined)
                throw new Error(`${file.path}: '${relPath}' isn't in static/images or the asset manifest`);
            return asset;
        };
        const resolve = (relPath) => resolveAsset(relPath).url;

        visit(tree, (node) => {
            if (node.type === "image" && node.url.startsWith(k_url_prefix)) {
                node.url = resolve(node.url.slice(k_url_prefix.length));
                return;
            }
            if (node.type !== "mdxJsxFlowElement" && node.type !== "mdxJsxTextElement") return;

            for (const attribute of node.attributes) {
                if (attribute.type !== "mdxJsxAttribute") continue;
                if ((attribute.name === "src" || attribute.name === "poster") &&
                    typeof attribute.value === "string" &&
                    attribute.value.startsWith(k_url_prefix))
                    attribute.value = resolve(attribute.value.slice(k_url_prefix.length));
            }

            if (node.name === "GeneratedScreenshot") {
                const nameAttribute = node.attributes.find((attribute) => attribute.name === "name");
                if (typeof nameAttribute?.value !== "string")
                    throw new Error(`${file.path}: GeneratedScreenshot needs a literal name prop`);
                const asset = resolveAsset(`screenshots/${nameAttribute.value}.png`);
                node.attributes.push({ type: "mdxJsxAttribute", name: "src", value: asset.url });
                if (asset.overlay !== undefined)
                    node.attributes.push({
                        type: "mdxJsxAttribute",
                        name: "overlayJson",
                        value: JSON.stringify(asset.overlay),
                    });
            }
        });
    };
}
