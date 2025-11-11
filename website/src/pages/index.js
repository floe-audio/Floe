// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faGem, faSeedling, faMusic, faBalanceScale } from '@fortawesome/free-solid-svg-icons';
import PackageCard from '../components/PackageCard';
import packageDatabase from '@site/static/package-database.json';
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
                            to="/docs/getting-started/quick-start-guide">
                            Quick Start Guide
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
                <Heading as="h2">Protected creative freedom</Heading>
                <p>
                    Floe is built as open-source software (GPL licensed), meaning its code is freely available for anyone to see and improve. This isn't solely a technical choice — it reflects our values while simultaneously offering real benefits for your music-making workflow.
                </p>

                <p>
                    <strong>Your tools won't disappear.</strong> Open source means Floe can keep working on future computers indefinitely — any developer can maintain it. You'll never lose access to your creative workflow because of business decisions or discontinued products.
                </p>

                <p>
                    <strong>Better development through collaboration.</strong> Musicians and developers worldwide can contribute improvements, fix issues, and add features that benefit everyone. This collective approach helps Floe evolve faster and serve real creative needs.
                </p>

                <p>
                    <strong>Aligned incentives.</strong> Being open source means we can't rely on lock-in or artificial limitations — if we did, developers could fork the code or users could switch. We succeed only by making Floe genuinely useful for your music, which keeps our interests aligned with yours rather than opposed to them.
                </p>
            </div>
        </section>
    );
}

export default function Home() {
    const { siteConfig } = useDocusaurusContext();
    return (
        <>
            <Layout
                title="Sample Library Platform"
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
                        <Heading as="h2">Floe's Workflow</Heading>

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
                                        Expressively play sample-based instruments: velocity, modulation, and pitch bend work as expected. Use MIDI controllers, DAW automation and Floe's macro knobs to further create lively performances.
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
                                        Take sounds beyond their natural boundaries. Layer instruments across libraries, sculpt with loop and crossfade controls that bridge multisampling and synthesis, then process through built-in effects.
                                    </p>
                                </div>
                            </div>
                        </div>
                    </div>
                </section>

                {/* Core Values section */}
                <section className={styles.coreValuesSection}>
                    <div className="container">
                        <Heading as="h2">Key Benefits</Heading>

                        <div className={styles.coreValuesGrid}>
                            <div className={styles.coreValueItem}>
                                <div className={styles.coreValueIcon}>🎛️</div>
                                <div className={styles.coreValueContent}>
                                    <h3>Layer across libraries</h3>
                                    <p>
                                        Break free from single-library limitations. Floe's 3-layer architecture lets you blend instruments from completely different sample libraries, creating rich, complex textures.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.coreValueItem}>
                                <div className={styles.coreValueIcon}>🎚️</div>
                                <div className={styles.coreValueContent}>
                                    <h3>Sample-based synthesis, not just playback</h3>
                                    <p>
                                        More than a sample player — Floe features synthesis capabilities with filters, envelopes, LFOs, and crossfade controls. Take sounds beyond their natural boundaries, creating textures impossible with the original recordings.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.coreValueItem}>
                                <div className={styles.coreValueIcon}>🔊</div>
                                <div className={styles.coreValueContent}>
                                    <h3>Professional effects rack</h3>
                                    <p>
                                        Shape your sound with 10 high-quality effects in customizable order, including pro-standard reverb and delay. Each layer processes individually before flowing through the shared effects chain.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.coreValueItem}>
                                <div className={styles.coreValueIcon}>📁</div>
                                <div className={styles.coreValueContent}>
                                    <h3>Works with your workflow</h3>
                                    <p>
                                        Flexible folder management adapts to your organization, supports external drives, and instantly detects changes. No rigid requirements — Floe allows you to manage files your way.
                                    </p>
                                </div>
                            </div>

                            <div className={styles.coreValueItem}>
                                <div className={styles.coreValueIcon}>🎵</div>
                                <div className={styles.coreValueContent}>
                                    <h3>No accounts, no subscriptions, no interruptions</h3>
                                    <p>
                                        Focus entirely on your creative process. Floe works offline, respects your privacy, and never interrupts your workflow with authentication prompts or payment reminders.
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
                        <Heading as="h2">About Floe</Heading>

                        <div className={styles.highlightGrid}>
                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faGem} />
                                </div>
                                <h3>Professional-grade indie software</h3>
                                <p>Floe is a passion project by Sam Windell, a developer & sound-designer who also runs sample library company <a href="https://www.frozenplain.com">FrozenPlain</a>. Built on the proven architecture of FrozenPlain's Mirage, used in professional productions for years, Floe is the next evolution. It offers a curated, streamlined approach focused on what matters: performance, simplicity, and usability.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <div className={styles.iconContainer}>
                                    <FontAwesomeIcon icon={faSeedling} />
                                </div>
                                <h3>Built for evolution</h3>
                                <p>Floe's architecture is designed for extensibility and growth. Every feature addition maintains backward compatibility with your existing DAW projects, libraries, and presets, ensuring your creative investments remain protected as the platform evolves.</p>
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
                                    <FontAwesomeIcon icon={faBalanceScale} />
                                </div>
                                <h3>Sustainable business</h3>
                                <p>The plugin is free, sample libraries vary by creator — some free, some paid. This established industry model sustains ongoing development through paid library partnerships while keeping the core platform accessible to everyone.</p>
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
