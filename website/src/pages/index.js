// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import Heading from '@theme/Heading';
import PackageCard from '../components/PackageCard';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faWindows, faApple, faLinux } from '@fortawesome/free-brands-svg-icons';
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

function AspectSection({ eyebrow, title, image, imageAlt, description, bullets, reverse = false }) {
    return (
        <section className={clsx(styles.aspectSection, reverse && styles.aspectReverse)}>
            <div className="container">
                <div className={styles.aspectGrid}>
                    <div className={styles.aspectMedia}>
                        <img src={image} alt={imageAlt} className={styles.aspectImage} />
                    </div>
                    <div className={styles.aspectBody}>
                        <Heading as="h2" className={styles.aspectTitle}>{title}</Heading>
                        <p className={styles.aspectDescription}>{description}</p>
                        {bullets && (
                            <ul className={styles.checkList}>
                                {bullets.map((item) => (
                                    <li key={item}>{item}</li>
                                ))}
                            </ul>
                        )}
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
                <section className={styles.introSection}>
                    <div className="container">
                        <div className={styles.introGrid}>
                            <div className={styles.introBlock}>
                                <Heading as="h2">Open platform</Heading>
                                <p>
                                    Floe is an open platform for <em>Floe-format sample libraries</em>, providing a streamlined workflow for <strong>finding</strong>, <strong>performing</strong> and <strong>transforming</strong> sounds.</p><p>Designed for producers, composers and musicians. Developers use Floe's open tools to build curated sample library products on top of it.
                                </p>
                            </div>
                            <div className={styles.introBlock}>
                                <Heading as="h2">User-friendly</Heading>
                                <p>
                                    Our philosophy is to be hassle-free and allow you to focus on what really matters: making beautiful music.
                                </p>
                                <ul className={styles.checkList}>
                                    <li>Offline installation</li>
                                    <li>No accounts, no subscriptions, no interruptions</li>
                                    <li>Resizable vector UI</li>
                                    <li>Flexible folders &mdash; supports external drives and instantly detects changes</li>
                                    <li>Visual UI: see what's happening in the sound</li>
                                </ul>
                            </div>
                        </div>
                    </div>
                </section>

                <AspectSection
                    eyebrow="Find"
                    title="Find the right sound"
                    image="/images/find-16-11.png"
                    imageAlt="Floe's search and browse interface"
                    description="Floe's unified browser works across all your libraries with comprehensive search, tags (mood, type, genre), and categorisation. The sound you need is always a few clicks away, whether you're hunting for something specific or exploring new territory."
                />
                <AspectSection
                    eyebrow="Perform"
                    title="Performance-ready"
                    image="/images/perform-16-11.png"
                    imageAlt="Floe's performance controls and interface"
                    description="Expressively play sample-based instruments: velocity, modulation, and pitch bend work as expected. Use MIDI controllers, DAW automation and Floe's macro knobs to shape lively performances."
                    bullets={[
                        'A/B comparison for preset edits',
                        'Undo/redo',
                        'MIDI CC mappings',
                        'Velocity to volume curve',
                        'Settings for fully reproducible recordings',
                    ]}
                    reverse
                />
                <AspectSection
                    eyebrow="Transform"
                    title="Transform with sample-based synthesis"
                    image="/images/transform-16-11.png"
                    imageAlt="Floe's sound transformation features"
                    description="Take sounds beyond their natural boundaries. Layer instruments across libraries, sculpt with loop and crossfade controls that bridge multisampling and synthesis, and process with built-in effects."
                    bullets={[
                        '3-layer architecture',
                        'Powerful granular synthesis',
                        '11 reorderable effects',
                        'Per-layer arpeggiators',
                        'Add loops with crossfade',
                        'Envelopes, filters, LFOs',
                        'Random variation generator',
                    ]}
                />

                {/* Growing ecosystem section */}
                <section className={styles.ecosystemSection}>
                    <div className="container">
                        <Heading as="h2">Growing ecosystem</Heading>
                        <p className={styles.ecosystemDescription}>
                            Already in use by professionals, Floe is alive and improving. More packages are becoming available, including community libraries and professional content. <Link to="/packages">Browse all packages →</Link>
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
                                <p>A sample-based platform available as a CLAP, VST3, or AU plugin for Windows, macOS, and Linux. Compatible with all major DAWs — Logic Pro, Cubase, Studio One, FL Studio, Ableton Live, Reaper, and more. Uses the open Floe sample library format.</p>
                                <div className={styles.osIcons} aria-label="Supported operating systems">
                                    <FontAwesomeIcon icon={faWindows} title="Windows" />
                                    <FontAwesomeIcon icon={faApple} title="macOS" />
                                    <FontAwesomeIcon icon={faLinux} title="Linux" />
                                </div>
                            </div>

                            <div className={styles.highlightItem}>
                                <h3>Yours to keep</h3>
                                <p>No accounts, no subscriptions, no interruptions — your libraries live on your machine in an open format. Because Floe is open-source (GPL), it can keep working indefinitely, so your creative workflow won't disappear because of business decisions.</p>
                            </div>

                            <div className={styles.highlightItem}>
                                <h3>FrozenPlain</h3>
                                <p>Floe and <a href="https://www.frozenplain.com">FrozenPlain</a> are companion projects — both created by Sam Windell, with FrozenPlain's cinematic and ambient libraries primarily shaping Floe's direction. However, Floe is intentionally kept as its own open platform, free to explore wider applications and serve the broader music-making community.</p>
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
