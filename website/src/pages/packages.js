// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import Layout from '@theme/Layout';
import PackageGrid from '../components/PackageGrid';
import packageDatabase from '../../static/package-database.json';
import styles from './packages.module.css';

export default function Packages() {
    return (
        <Layout
            title="Package Directory"
            description="Curated Floe packages - free community libraries and premium content"
        >
            <main style={{ padding: '2rem 0' }}>
                <div className={styles.pageContainer}>
                    {/* Header Section */}
                    <section className={styles.headerSection}>
                        <div className={styles.headerContent}>
                            <h1>Packages</h1>
                            <p>Our growing collection of <em>Floe packages</em> (sample libraries & presets).</p>
                            <p>Use Floe's <a href="/docs/packages/install-packages"><em>Install package</em></a> button to install them.
                            </p>
                        </div>
                    </section>

                    {/* Professional Section */}
                    <section className={styles.section}>
                        <div className={styles.sectionHeader}>
                            <h2>Professional Packages ({packageDatabase.filter(pkg => pkg.category === 'professional' && !pkg.hidden).length})</h2>
                        </div>
                        <div className={styles.wideContent}>
                            <PackageGrid packages={packageDatabase.filter(pkg => pkg.category === 'professional').sort((a, b) => new Date(b.last_updated) - new Date(a.last_updated))} />
                        </div>
                    </section>

                    {/* Community Section */}
                    <section className={styles.section}>
                        <div className={styles.sectionHeader}>
                            <h2>Community Packages ({packageDatabase.filter(pkg => pkg.category === 'community' && !pkg.hidden).length})</h2>
                            <p>
                                Open-source libraries, passion projects, and community conversions. Perfect for getting started with Floe or expanding your sound palette at no cost.
                            </p>
                        </div>
                        <div className={styles.wideContent}>
                            <PackageGrid packages={packageDatabase.filter(pkg => pkg.category === 'community').sort((a, b) => new Date(b.last_updated) - new Date(a.last_updated))} />
                        </div>
                    </section>

                    {/* For Developers Section */}
                    <section className={styles.section}>
                        <div className={styles.content}>
                            <h2>For Library Creators</h2>
                            <div className={styles.developerGrid}>
                                <div className={styles.developerCard}>
                                    <h3>Submit a Package</h3>
                                    <p>
                                        We welcome quality submissions; see our <a href="../docs/develop/package-database">package submission documentation</a>. <a href="/docs/about-the-project/sponsorship">Sponsor Floe</a> to submit commercial packages.
                                    </p>
                                </div>
                                <div className={styles.developerCard}>
                                    <h3>Distribution Partnership</h3>
                                    <p>
                                        Looking to sell your libraries? <a href="https://www.frozenPlain.com">FrozenPlain</a> offers a revenue-sharing partnership for high-quality sample libraries, handling payment processing, marketing, and distribution.
                                    </p>
                                </div>
                            </div>
                        </div>
                    </section>
                </div>
            </main>
        </Layout>
    );
}
