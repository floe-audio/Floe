// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

const path = require('path');
const fs = require('fs');

/**
 * Custom Docusaurus plugin to generate individual package pages
 * 
 * This plugin reads the package database JSON file and creates dedicated routes
 * for each package with clean URLs like /packages/celtic-harp, /packages/dulcitone.
 * 
 * Why custom plugin instead of dynamic pages?
 * - Clean URLs without query parameters (/packages/slug vs /package?slug=...)
 * - Better SEO with proper static routes
 * - Each package gets its own HTML file during build
 * - Integrates naturally with Docusaurus routing system
 */
module.exports = function packagesPlugin(context, options) {
    return {
        name: 'packages-plugin',
        async loadContent() {
            // Load package database from static folder
            const packageDatabasePath = path.join(context.siteDir, 'static', 'package-database.json');
            const packageDatabase = JSON.parse(fs.readFileSync(packageDatabasePath, 'utf8'));

            return {
                packages: packageDatabase
            };
        },
        async contentLoaded({ content, actions }) {
            const { addRoute } = actions;
            const { packages } = content;

            // Generate a static route for each package using its slug
            // This creates routes like /packages/celtic-harp, /packages/dulcitone, etc.
            packages.forEach((pkg) => {
                addRoute({
                    path: `/packages/${pkg.slug}`,
                    component: '@site/src/components/PackageDetailPage',
                    exact: true,
                    props: {
                        package: pkg, // Pass package data as props to the component
                    },
                });
            });
        },
    };
};
