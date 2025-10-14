// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import styles from './styles.module.css';

function PackageCard({ pkg }) {
    const hasDownload = pkg.direct_download_link || pkg.external_product_page;

    return (
        <div className={styles.packageCard}>
            {pkg.background_image && (
                <a href={`/packages/${pkg.slug}`} className={styles.imageContainer}>
                    <img
                        src={pkg.background_image}
                        alt={`${pkg.name} interface`}
                        className={styles.packageImage}
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
                </a>
            )}

            <div className={styles.content}>
                <div className={styles.header}>
                    <div className={styles.titleSection}>
                        <h3 className={styles.title}>
                            <a href={`/packages/${pkg.slug}`} className={styles.titleLink}>
                                {pkg.name}
                            </a>
                        </h3>
                        <div className={styles.developerInfo}>
                            <span className={styles.developer}>by</span>
                            {pkg.developer_icon && (
                                <img
                                    src={pkg.developer_icon}
                                    alt={`${pkg.developer} icon`}
                                    className={styles.developerIcon}
                                />
                            )}
                            {pkg.developer_website ? (
                                <a href={pkg.developer_website} className={styles.developerLink}>
                                    {pkg.developer}
                                </a>
                            ) : (
                                <span className={styles.developer}>{pkg.developer}</span>
                            )}
                        </div>
                    </div>

                    <div className={styles.badges}>
                        {pkg.free && (
                            <span className={styles.badge + ' ' + styles.freeBadge}>FREE</span>
                        )}
                        {pkg.attribution_required && (
                            <a href="/docs/usage/attribution" className={styles.badge + ' ' + styles.attributionBadge + ' ' + styles.attributionLink}>ATTRIBUTION</a>
                        )}
                    </div>
                </div>

                <div
                    className={styles.description}
                    dangerouslySetInnerHTML={{ __html: pkg.description }}
                />

                <div className={styles.stats}>
                    <span className={styles.stat}>
                        <span className={styles.statNumber}>{pkg.instruments}</span>
                        <span className={styles.statLabel}>instrument{pkg.instruments !== 1 ? 's' : ''}</span>
                    </span>
                    <span className={styles.stat}>
                        <span className={styles.statNumber}>{pkg.presets}</span>
                        <span className={styles.statLabel}>preset{pkg.presets !== 1 ? 's' : ''}</span>
                    </span>
                    <span className={styles.stat}>
                        <span className={styles.statNumber}>{pkg.irs}</span>
                        <span className={styles.statLabel}>IR{pkg.irs !== 1 ? 's' : ''}</span>
                    </span>
                </div>

                {hasDownload && (
                    <div className={styles.actions}>
                        {pkg.direct_download_link ? (
                            <a
                                href={pkg.direct_download_link}
                                className={styles.downloadButton}
                                download
                            >
                                <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
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
                                <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
                                    <path d="M19 19H5V5h7V3H5c-1.11 0-2 .9-2 2v14c0 1.1.89 2 2 2h14c1.1 0 2-.9 2-2v-7h-2v7zM14 3v2h3.59l-9.83 9.83 1.41 1.41L19 6.41V10h2V3h-7z" />
                                </svg>
                                Visit Page
                            </a>
                        )}
                    </div>
                )}
            </div>
        </div>
    );
}

export default PackageCard;
