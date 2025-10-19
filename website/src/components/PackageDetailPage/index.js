// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import Layout from '@theme/Layout';
import Breadcrumbs from '../Breadcrumbs';
import styles from './styles.module.css';

/**
 * Individual package detail page component
 * 
 * This component is used by our custom packages plugin to render dedicated pages
 * for each package in the database. Each package gets its own route and HTML file.
 * 
 * The package data is passed as props by the plugin when it generates the routes.
 */
function PackageDetailPage(props) {
    const pkg = props.package; // Package data passed from the custom plugin
    const hasDownload = pkg.direct_download_link || pkg.external_product_page;

    return (
        <Layout
            title={pkg.name}
            description={pkg.description?.replace(/<[^>]*>/g, '') || `${pkg.name} - Floe package by ${pkg.developer}`}
        >
            <div className="container margin-vert--lg">
                <div className="row">
                    <div className="col col--8 col--offset-2">
                        {/* Breadcrumb */}
                        <Breadcrumbs items={[
                            { label: 'Packages', href: '/packages' },
                            { label: pkg.name }
                        ]} />

                        {/* Header */}
                        <div className={styles.header}>
                            <h1 className={styles.title}>{pkg.name}</h1>
                            <div className={styles.developer}>
                                {pkg.developer_icon && (
                                    <img
                                        src={pkg.developer_icon}
                                        alt={`${pkg.developer} icon`}
                                        className={styles.developerIcon}
                                    />
                                )}
                                <span>by {pkg.developer_website ? (
                                    <a href={pkg.developer_website} target="_blank" rel="noopener noreferrer">
                                        {pkg.developer}
                                    </a>
                                ) : pkg.developer}</span>
                            </div>
                        </div>

                        {/* Hero Image */}
                        {pkg.background_image && (
                            <div className={styles.imageSection}>
                                <div className={styles.imageContainer}>
                                    <img
                                        src={pkg.background_image}
                                        alt={`${pkg.name} interface`}
                                        className={styles.heroImage}
                                    />
                                    {pkg.image_attribution && (
                                        <div className={styles.imageAttribution}>
                                            <span
                                                className={styles.attributionText}
                                                title={
                                                    (pkg.image_attribution.source_title || '') +
                                                    (pkg.image_attribution.source_title && pkg.image_attribution.author ? ' by ' : '') +
                                                    (pkg.image_attribution.author || '') +
                                                    (pkg.image_attribution.license ? ' (' + pkg.image_attribution.license + ')' : '')
                                                }
                                            >
                                                {pkg.image_attribution.source_title && (
                                                    <a
                                                        href={pkg.image_attribution.source_url}
                                                        target="_blank"
                                                        rel="noopener noreferrer"
                                                        className={styles.attributionLink}
                                                    >
                                                        {pkg.image_attribution.source_title}
                                                    </a>
                                                )}
                                                {pkg.image_attribution.source_title && pkg.image_attribution.author && ' by '}
                                                {pkg.image_attribution.author && (
                                                    <a
                                                        href={pkg.image_attribution.author_url}
                                                        target="_blank"
                                                        rel="noopener noreferrer"
                                                        className={styles.attributionLink}
                                                    >
                                                        {pkg.image_attribution.author}
                                                    </a>
                                                )}
                                                {pkg.image_attribution.license && (
                                                    <>
                                                        {' ('}
                                                        <a
                                                            href={pkg.image_attribution.license_url}
                                                            target="_blank"
                                                            rel="noopener noreferrer"
                                                            className={styles.attributionLink}
                                                        >
                                                            {pkg.image_attribution.license}
                                                        </a>
                                                        {')'}
                                                    </>
                                                )}
                                            </span>
                                        </div>
                                    )}
                                </div>
                            </div>
                        )}

                        {/* Badges */}
                        <div className={styles.badges}>
                            {pkg.free && (
                                <span className={styles.badge + ' ' + styles.freeBadge}>FREE</span>
                            )}
                            {pkg.attribution_required && (
                                <a href="/docs/usage/attribution" className={styles.badge + ' ' + styles.attributionBadge + ' ' + styles.attributionLink}>ATTRIBUTION</a>
                            )}
                            <span className={styles.badge + ' ' + styles.categoryBadge}>
                                {pkg.category === 'community' ? 'COMMUNITY' : 'PROFESSIONAL'}
                            </span>
                            {pkg.repo_link && (
                                <a
                                    href={pkg.repo_link}
                                    className={styles.badge + ' ' + styles.repoBadge + ' ' + styles.repoLink}
                                    target="_blank"
                                    rel="noopener noreferrer"
                                >
                                    <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" style={{ marginRight: '4px' }}>
                                        <path d="M12 0c-6.626 0-12 5.373-12 12 0 5.302 3.438 9.8 8.207 11.387.599.111.793-.261.793-.577v-2.234c-3.338.726-4.033-1.416-4.033-1.416-.546-1.387-1.333-1.756-1.333-1.756-1.089-.745.083-.729.083-.729 1.205.084 1.839 1.237 1.839 1.237 1.07 1.834 2.807 1.304 3.492.997.107-.775.418-1.305.762-1.604-2.665-.305-5.467-1.334-5.467-5.931 0-1.311.469-2.381 1.236-3.221-.124-.303-.535-1.524.117-3.176 0 0 1.008-.322 3.301 1.23.957-.266 1.983-.399 3.003-.404 1.02.005 2.047.138 3.006.404 2.291-1.552 3.297-1.23 3.297-1.23.653 1.653.242 2.874.118 3.176.77.84 1.235 1.911 1.235 3.221 0 4.609-2.807 5.624-5.479 5.921.43.372.823 1.102.823 2.222v3.293c0 .319.192.694.801.576 4.765-1.589 8.199-6.086 8.199-11.386 0-6.627-5.373-12-12-12z" />
                                    </svg>
                                    REPO
                                </a>
                            )}
                        </div>

                        {/* Description */}
                        <div className={styles.description}>
                            <div dangerouslySetInnerHTML={{ __html: pkg.description }} />
                        </div>

                        {/* Stats */}
                        <div className={styles.stats}>
                            <div className={styles.stat}>
                                <span className={styles.statNumber}>{pkg.instruments}</span>
                                <span className={styles.statLabel}>instrument{pkg.instruments !== 1 ? 's' : ''}</span>
                            </div>
                            <div className={styles.stat}>
                                <span className={styles.statNumber}>{pkg.presets}</span>
                                <span className={styles.statLabel}>preset{pkg.presets !== 1 ? 's' : ''}</span>
                            </div>
                            <div className={styles.stat}>
                                <span className={styles.statNumber}>{pkg.irs}</span>
                                <span className={styles.statLabel}>IR{pkg.irs !== 1 ? 's' : ''}</span>
                            </div>
                        </div>

                        {/* Download Action */}
                        {hasDownload && (
                            <div className={styles.downloadSection}>
                                {pkg.direct_download_link ? (
                                    <a
                                        href={pkg.direct_download_link}
                                        className={styles.downloadButton}
                                        download
                                    >
                                        <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor">
                                            <path d="M19 9h-4V3H9v6H5l7 7 7-7zM5 18v2h14v-2H5z" />
                                        </svg>
                                        Download Package
                                    </a>
                                ) : (
                                    <a
                                        href={pkg.external_product_page}
                                        className={styles.downloadButton + ' ' + styles.externalButton}
                                        target="_blank"
                                        rel="noopener noreferrer"
                                    >
                                        <svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor">
                                            <path d="M19 19H5V5h7V3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2v-7h-2v7zM14 3v2h3.59l-9.83 9.83 1.41 1.41L19 6.41V10h2V3h-7z" />
                                        </svg>
                                        Visit Product Page
                                    </a>
                                )}
                                <p className={styles.installHelpText}>
                                    Install using Floe's <em>Install package</em> button. <a href="/docs/packages/install-packages" className={styles.installLink}>Learn more</a>.
                                </p>
                            </div>
                        )}
                    </div>
                </div>
            </div>
        </Layout>
    );
}

export default PackageDetailPage;
