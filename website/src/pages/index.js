// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faGem, faSeedling, faMusic, faLayerGroup } from '@fortawesome/free-solid-svg-icons';
import PackageCard from '../components/PackageCard';
import packageDatabase from '../../static/package-database.json';
import styles from './index.module.css';

function HeroSection() {
    const { siteConfig } = useDocusaurusContext();
    return (
        <section className={styles.hero}>
            <div className="container">
                <div className={styles.heroContent}>
                    <div className={styles.logoContainer}>
                        <img
                            alt="Floe logo"
                            src="/images/logo-light-mode.svg"
                            className={styles.heroLogo}
                        />
                        <img
                            alt="Floe logo"
                            src="/images/logo-dark-mode.svg"
                            className={clsx(styles.heroLogo, styles.heroLogoDark)}
                        />
                    </div>
                    <Heading as="h1" className={styles.heroTitle}>
                        Sample library platform
                    </Heading>
                    <p className={styles.heroSubtitle}>
                        Find, perform, and shape sounds beyond their natural boundaries. A <strong>free</strong>, open platform that puts sound transformation at your fingertips – built with care and simplicity in mind.
                    </p>
                    <div className={styles.heroButtons}>
                        <Link
                            className="button button--primary button--lg"
                            to="/download">
                            Download Now
                        </Link>
                        <Link
                            className="button button--outline button--lg"
                            to="/docs/introduction">
                            Read the Manual
                        </Link>
                    </div>
                </div>
            </div>
        </section>
    );
}

function FeatureSection({ title, description, image, imageAlt, reverse = false, fullWidth = false }) {
    return (
        <section className={clsx(styles.featureSection, reverse && styles.reverse, fullWidth && styles.fullWidth)}>
            <div className={fullWidth ? "" : "container"}>
                <div className={styles.featureContent}>
                    <div className={styles.featureText}>
                        <Heading as="h2">{title}</Heading>
                        <p>{description}</p>
                    </div>
                    <div className={styles.featureImage}>
                        <img src={image} alt={imageAlt} />
                    </div>
                </div>
            </div>
        </section>
    );
}


function OpenSourceSection() {
    return (
        <section className={styles.openSourceSection}>
            <div className="container">
                <Heading as="h2">Why open-source?</Heading>
                <p>
                    Floe is built as open-source software, meaning its code is freely available for anyone to see, modify, and improve (it's GPL licensed).
                </p>
                <p>
                    While this might seem like a technical detail, it offers some potentially very valuable benefits:
                </p>
                <ul className={styles.benefitsList}>
                    <li>Any developer can pick it up and keep it running on future systems, ensuring longevity</li>
                    <li>If you need a specific feature, you could hire a developer to create your own customised version</li>
                    <li>Most importantly, open-source transparency builds trust — you or any community developer can verify that the program genuinely serves meaningful music creation rather than employing dubious business tactics</li>
                </ul>
            </div>
        </section>
    );
}

export default function Home() {
    const { siteConfig } = useDocusaurusContext();
    return (
        <>
            <Layout
                title="The Sample Library Platform"
                description="Floe empowers you to find the perfect sound across all your libraries, perform with expressive control, and transform samples beyond their natural boundaries. Free, open-source audio plugin for Windows, macOS and Linux."
                wrapperClassName="homepage-layout">
                <HeroSection />

                {/* Large hi-res image showcasing the full interface */}
                <section className={styles.showcaseSection}>
                    <div className="container">
                        <img
                            src="/images/whole-gui.png"
                            alt="Floe complete interface showing all features"
                            className={styles.showcaseImage}
                        />
                    </div>
                </section>

                {/* Three key aspects: Find, Perform, Transform */}
                <section className={styles.coreAspectsSection}>
                    <div className="container">
                        <div className={styles.aspectsGrid}>
                            <div className={styles.aspectItem}>
                                <div className={styles.aspectImageContainer}>
                                    <img
                                        src="/images/find-16-11.png"
                                        alt="Floe's search and browse interface"
                                        className={styles.aspectImage}
                                    />
                                </div>
                                <div className={styles.aspectContent}>
                                    <h3>Find</h3>
                                    <p>
                                        Floe's unified browser works across all your libraries with comprehensive search, tags (mood, type, genre), and categorisation. The sound you need is always a few clicks away, whether you're hunting for something specific or exploring new territory.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.aspectItem}>
                                <div className={styles.aspectImageContainer}>
                                    <img
                                        src="/images/perform-16-11.png"
                                        alt="Floe's performance controls and interface"
                                        className={styles.aspectImage}
                                    />
                                </div>
                                <div className={styles.aspectContent}>
                                    <h3>Perform</h3>
                                    <p>
                                        Every library shares the same intuitive interface. Velocity, modulation, and pitch bend work as expected. Comprehensive preset systems and macro controls put sound customization at your fingertips, letting you shape sounds with ease.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.aspectItem}>
                                <div className={styles.aspectImageContainer}>
                                    <img
                                        src="/images/transform-16-11.png"
                                        alt="Floe's sound transformation features"
                                        className={styles.aspectImage}
                                    />
                                </div>
                                <div className={styles.aspectContent}>
                                    <h3>Transform</h3>
                                    <p>
                                        Take sounds beyond their natural boundaries. Layer instruments across libraries, sculpt with loop and crossfade controls that bridge multisampling and synthesis, then process through built-in effects. Start with rich timbres and shape from there.
                                    </p>
                                </div>
                            </div>
                        </div>
                    </div>
                </section>

                {/* Growing ecosystem section */}
                <section className={styles.ecosystemSection}>
                    <div className="container">
                        <Heading as="h2">Growing ecosystem</Heading>
                        <p className={styles.ecosystemDescription}>
                            Already in use by professionals, Floe is alive and improving. More packages are becoming available including community libraries and professional content. <Link to="/packages">Browse all packages →</Link>
                        </p>
                        <div className={styles.packagePreview}>
                            {(() => {
                                const visiblePackages = packageDatabase.filter(pkg => !pkg.hidden);
                                const sortedByDate = [...visiblePackages].sort((a, b) => new Date(b.last_updated) - new Date(a.last_updated));

                                const latest = sortedByDate[0];
                                const latestProfessional = sortedByDate.find(pkg => pkg.category === 'professional' && pkg !== latest);
                                const latestCommunity = sortedByDate.find(pkg => pkg.category === 'community' && pkg !== latest && pkg !== latestProfessional);

                                const selectedPackages = [latest, latestProfessional, latestCommunity].filter(Boolean);

                                return selectedPackages.map((pkg, index) => (
                                    <PackageCard key={index} pkg={pkg} />
                                ));
                            })()}
                        </div>
                    </div>
                </section>

                <section className={styles.highlightSection}>
                    <div className="container">
                        <div className={styles.highlightGrid}>
                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faGem} />
                                </div>
                                <h3>Professional-grade indie software</h3>
                                <p>Floe is a passion project by Sam Windell, a developer & sound-designer who also runs sample library company <a href="https://www.frozenplain.com">FrozenPlain</a>. Shaped by direct feedback from professional composers for film and television, it offers a curated, streamlined approach focused on what matters: performance, simplicity, and usability.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faSeedling} />
                                </div>
                                <h3><em>Complete</em> but not <em>completed</em></h3>
                                <p>Floe already contains all the essential parts of a great product, but it's built for continuous refinement and expansion over time. We have ambitious goals for the project while maintaining a commitment to backwards compatibility.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faMusic} />
                                </div>
                                <h3>Focus on making music</h3>
                                <p>We want people to enjoy the meaningful creative act of music-making. No accounts, no subscriptions, no interruptions — just musical creation. Openness is at the core of Floe and its libraries.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faLayerGroup} />
                                </div>
                                <h3>Built on a solid foundation</h3>
                                <p>Built on the proven architecture of FrozenPlain's Mirage, used in professional productions for years, Floe is the next evolution, designed with careful attention to reliability and performance.</p>
                            </div>
                        </div>
                    </div>
                </section>

                <OpenSourceSection />

                {/* Technical specifications */}
                <section className={styles.technicalSection}>
                    <div className="container">
                        <Heading as="h2">Technical Details</Heading>
                        <div className={styles.technicalContent}>
                            <ul>
                                <li><strong>Plugin formats:</strong> CLAP, VST3, AU</li>
                                <li><strong>Platforms:</strong> Windows, macOS, Linux</li>
                                <li><strong>Type:</strong> Sample-based synthesiser/ROMpler</li>
                                <li><strong>Library format:</strong> <em>Floe library format</em> (open - Lua scripting required)</li>
                                <li><strong>DAW compatibility:</strong> Logic Pro, Cubase, Studio One, FL Studio, Ableton Live, Reaper, and many more</li>
                            </ul>
                        </div>
                    </div>
                </section>
            </Layout>
        </>
    );
}
