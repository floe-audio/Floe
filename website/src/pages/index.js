// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
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
                                        Shape your sound with 11 high-quality effects in customizable order, including pro-standard reverb and delay. Each layer processes individually before flowing through the shared effects chain.
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

                        <div className={styles.moreFeatures}>
                            <h3>More Features</h3>
                            <ul className={styles.featureList}>
                                <li>Per-layer arpeggiators (coming in v2)</li>
                                <li>Comprehensive granular synthesis (coming in v2)</li>
                                <li>Visual 3-band EQs (coming in v2)</li>
                                <li>Undo/redo system (coming in v2)</li>
                                <li>A/B comparison for preset edits (coming in v2)</li>
                                <li>Autosave</li>
                                <li>Random variation generator (coming in v2)</li>
                                <li>Core per-layer controls: ADSR, filter, LFO, EQ, tuning, stereo width</li>
                                <li>Sustain pedal support</li>
                                <li>Customisable MIDI CC mapping</li>
                                <li>Pitchbend with controllable range</li>
                                <li>Velocity-volume curve</li>
                                <li>Split sounds across keyboard ranges</li>
                                <li>11 reorderable effects: reverb, distortion, delay, 2 compressors, and more</li>
                                <li>Install libraries on separate drives</li>
                                <li>Offline installation</li>
                                <li>Load a random sound</li>
                                <li>Options for fully reproducible recordings (coming in v2)</li>
                                <li>4 renamable macro knobs</li>
                                <li>Resizable vector UI</li>
                                <li>Copy/paste/reset for all parameters and sections (coming in v2)</li>
                            </ul>
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
                                <h3>Widely supported</h3>
                                <p>A sample-based synthesiser/ROMpler available as a CLAP, VST3, or AU plugin for Windows, macOS, and Linux. Compatible with all major DAWs (Logic Pro, Cubase, Studio One, FL Studio, Ableton Live, Reaper, and more), and uses the open Floe sample library format.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <h3>Yours to keep</h3>
                                <p>No accounts, no subscriptions, no interruptions — your libraries live on your machine in an open format. Because Floe is open-source (GPL), it can keep working indefinitely, so your creative workflow won't disappear because of business decisions.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <h3>FrozenPlain</h3>
                                <p>Floe and <a href="https://www.frozenplain.com">FrozenPlain</a> are companion projects — both created by Sam Windell, with FrozenPlain's cinematic and ambient libraries primarily shaping Floe's direction. However, Floe is intentionally encapsulated as its own open platform, free to explore wider applications and serve the broader music-making community.</p>
                                <a href="https://www.frozenplain.com" aria-label="FrozenPlain" className={styles.tileLogoLink}>
                                    <img
                                        src="https://www.frozenplain.com/icons/logo-adj.svg"
                                        alt="FrozenPlain"
                                        className={styles.tileLogo}
                                    />
                                </a>
                            </div>

                            <div className={styles.highlightItem}>
                                <h3>Openly built</h3>
                                <p>Floe is an outlet for a broader passion — open-source, ethical software built for real longevity — with its library-creation tooling (in Lua) freely available to other developers and the wider open-source community.</p>
                            </div>


                            <div className={styles.highlightItem}>
                                <h3>Built on a proven foundation</h3>
                                <p>Floe builds on the architecture of FrozenPlain's Mirage, refined through years of professional use and shaped by direct feedback from composers working in film and television.</p>
                            </div>

                        </div>

                    </div>
                </section>
            </Layout>
        </>
    );
}
