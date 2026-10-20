// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

// Uploads every local file in static/images that the CDN doesn't have yet, then records its hash (and
// overlay, for a screenshot that has one) in asset-manifest.json. Hashed names are never overwritten, so
// they can be cached forever. Manifest entries with no local file are left alone.
//
// Needs FLOE_ASSETS_ACCESS_KEY_ID and FLOE_ASSETS_SECRET_ACCESS_KEY: an R2 API token with write access to
// the bucket. They're read from the environment or the repo's .env file.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import {
    bucketKey,
    k_bucket_prefix,
    k_local_assets_dir,
    listLocalAssets,
    localManifestEntry,
    k_site_dir,
    readManifest,
    writeManifest,
} from "../plugins/asset-urls.mjs";

const envFilePath = path.join(k_site_dir, "../.env");
if (fs.existsSync(envFilePath)) process.loadEnvFile(envFilePath);

const accessKeyId = process.env.FLOE_ASSETS_ACCESS_KEY_ID;
const secretAccessKey = process.env.FLOE_ASSETS_SECRET_ACCESS_KEY;
if (!accessKeyId || !secretAccessKey) {
    console.error("FLOE_ASSETS_ACCESS_KEY_ID and FLOE_ASSETS_SECRET_ACCESS_KEY must be set, in the environment or .env");
    process.exit(1);
}

// The token can only access this bucket, so rclone mustn't try to create it.
const remote =
    ":s3,provider=Cloudflare,endpoint='https://86fb53a60738313e15dd22613754f614.r2.cloudflarestorage.com',no_check_bucket=true:floe-assets";

// Credentials go through the environment rather than arguments so they don't show up in process listings.
const rclone = (...args) =>
    execFileSync("rclone", args, {
        encoding: "utf8",
        stdio: ["ignore", "pipe", "inherit"],
        env: {
            ...process.env,
            RCLONE_S3_ACCESS_KEY_ID: accessKeyId,
            RCLONE_S3_SECRET_ACCESS_KEY: secretAccessKey,
        },
    });

const existingKeys = new Set(
    rclone("lsf", "--recursive", "--files-only", `${remote}/${k_bucket_prefix}/`)
        .split("\n")
        .filter(Boolean)
        .map((relKey) => `${k_bucket_prefix}/${relKey}`)
);

const manifest = readManifest("current");
let uploaded = 0;

for (const relPath of listLocalAssets()) {
    const localPath = path.join(k_local_assets_dir, relPath);
    const entry = localManifestEntry(relPath);
    const key = bucketKey(relPath, entry.hash);
    if (!existingKeys.has(key)) {
        console.log(`Uploading ${relPath} -> ${key}`);
        rclone(
            "copyto",
            localPath,
            `${remote}/${key}`,
            "--header-upload",
            "Cache-Control: public, max-age=31536000, immutable"
        );
        uploaded += 1;
    }
    manifest[relPath] = entry;
}

writeManifest("current", manifest);
console.log(`Uploaded ${uploaded} file(s); asset-manifest.json lists ${Object.keys(manifest).length}.`);
