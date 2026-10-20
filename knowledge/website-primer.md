How the website (`website/`, Docusaurus, GitHub Pages) is put together.

Commands (`nix develop --command zig build <step>`):
- `script:website-dev` (dev server), `script:website-build` (production).
- `script:website-generate` - writes `static/generated-data.json` (versions, download links, params, effects, Lua examples, etc. from the code). The site build runs it.
- `script:website-promote-beta-to-stable` - copies beta docs and the asset manifest to stable.
- `script:generate-manual` - PDF manuals (`scripts/generate-manual.mjs`).

Docs versions: edit beta, `docs/` (served at `/docs/beta/`). Stable, `versioned_docs/version-stable/` (served at `/docs/`), only changes by promotion or deliberate fixes.

Media:
- `static/images` media (png/jpg/gif/webp/mp4/webm, except `favicon.png`) and screenshot overlay JSONs aren't in git. They're served from assets.floe.audio (Cloudflare R2) under content-hashed names.
- `asset-manifest.json` maps each `static/images` path to `{ hash, overlay? }` (overlay = a screenshot's numbered boxes). Stable's copy is `versioned_asset_manifests/version-stable.json`.
- Reference media normally (`/images/foo.png`, `<GeneratedScreenshot name="...">`, `useAssetUrl()`). `plugins/asset-urls.mjs` resolves them to CDN URLs via the docs version's manifest, preferring a local file if present. The build fails on a reference that's neither.
- A fresh clone has no media. Download existing files from `https://assets.floe.audio/website/<path>.<hash>.<ext>`.
- Adding/changing: put the file in `static/images` and iterate locally. Only once reviewed and confirmed, run `zig build script:website-upload-assets` (uploads are permanent, never upload while iterating) and commit the manifest with the docs change. Needs `FLOE_ASSETS_ACCESS_KEY_ID`/`FLOE_ASSETS_SECRET_ACCESS_KEY` in the environment or `.env`.

Screenshots:
- `zig build script:gen-doc-screenshots` renders offscreen into `static/images/screenshots/`. The list is in `build.zig`; capture regions and overlays are in `src/plugin/gui/core/gui_screenshot.cpp`.
- Each loads `website/screenshot-presets/<output name>.floe-preset`, so tuning one can't change another. Where the preset's name or folder badge is visible, it instead uses the only `.floe-preset` in a `<output name>/` subfolder, e.g. `top-panel/Drones/Faded Silhouettes.floe-preset`. Modals that show nothing from the preset are `no_preset` in `build.zig`.
- GUI code forces only the minimum state (tab, play mode, effect enabled). Tune everything else by editing the preset, not code.
- Library manuals need `--library-config <manual.json>`, declaring what the library contains (`src/components/libraryFeatures.mjs`). Docs curate with `<LibraryFeature has|lacks="...">`; library-only wording goes in `<ManualOnly types={['library']}>` (may use `<LibraryName />`), paired with a `<ManualExclude types={['library']}>` general version.
- Library manuals: `zig build script:generate-manual -Dscreenshot-presets=<dir>` renders from a folder laid out like `website/screenshot-presets` into `website/build-manual-screenshots/`, used instead of the website's. So per-screenshot settings belong in the preset folder, not `build.zig`. Each render gets a fresh `FLOE_HOME`; the folder's optional `floe.ini` becomes the preferences, with `extra-libraries-folder`/`extra-presets-folder` relative to the folder.
