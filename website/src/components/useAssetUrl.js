// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import useDocusaurusContext from '@docusaurus/useDocusaurusContext';

// Maps a path relative to static/images to where it's served from. See plugins/asset-urls.mjs.
export default function useAssetUrl() {
    const { siteConfig } = useDocusaurusContext();
    const assetUrls = siteConfig.customFields.assetUrls;
    return (relPath) => {
        const url = assetUrls[relPath];
        if (url === undefined) throw new Error(`'${relPath}' isn't in static/images or the asset manifest`);
        return url;
    };
}
