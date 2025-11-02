// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import Layout from '@theme/Layout';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import { faDownload } from '@fortawesome/free-solid-svg-icons';
import data from '@site/static/generated-data.json';
import staticData from '@site/static/generated-data.json';
import OSTabs, { OSPanel } from '@site/src/components/OSTabs';
import styles from './download.module.css';

function DownloadButton({ href, dataEvent, children, size }) {
    return (
        <a
            href={href}
            className={styles.downloadButton}
            data-umami-event={dataEvent}
        >
            <div className={styles.downloadContent}>
                <span className={styles.downloadText}>{children}</span>
                <span className={styles.downloadSize}>({size})</span>
            </div>
        </a>
    );
}

function CurrentVersionInfo() {
    return (
        <p style={{ textAlign: 'center', margin: '2rem 0' }}>
            <em>
                Current version: v{data["latest-stable-release-version"]} • <a href="/docs/installation/download-and-install-floe#requirements">Requirements</a> • <a href="/docs/installation/download-and-install-floe">Installation Help</a>
            </em>
        </p>
    );
}

function AdditionalDownloads({ os }) {
    return (
        <div className={styles.additionalDownloads}>
            <h4>Additional Downloads</h4>

            <h5>Manual Installation (Advanced):</h5>
            <ul>
                {os === 'windows' && (
                    <li>
                        <a href={staticData['Floe-Manual-Install-Windows'].url} data-umami-event="Download Windows Manual Install">
                            {staticData['Floe-Manual-Install-Windows'].name}
                        </a> ({staticData['Floe-Manual-Install-Windows'].size})
                    </li>
                )}
                {os === 'macos' && (
                    <>
                        <li>
                            <a href={staticData['Floe-Manual-Install-macOS-Apple-Silicon'].url} data-umami-event="Download macOS Apple Silicon Manual Install">
                                {staticData['Floe-Manual-Install-macOS-Apple-Silicon'].name}
                            </a> ({staticData['Floe-Manual-Install-macOS-Apple-Silicon'].size})
                        </li>
                        <li>
                            <a href={staticData['Floe-Manual-Install-macOS-Intel'].url} data-umami-event="Download macOS Intel Manual Install">
                                {staticData['Floe-Manual-Install-macOS-Intel'].name}
                            </a> ({staticData['Floe-Manual-Install-macOS-Intel'].size})
                        </li>
                    </>
                )}
                {os === 'linux' && (
                    <>
                        <li>
                            <a href={staticData['Floe-CLAP-Linux'].url} data-umami-event="Download Linux CLAP">
                                {staticData['Floe-CLAP-Linux'].name}
                            </a> ({staticData['Floe-CLAP-Linux'].size})
                        </li>
                        <li>
                            <a href={staticData['Floe-VST3-Linux'].url} data-umami-event="Download Linux VST3">
                                {staticData['Floe-VST3-Linux'].name}
                            </a> ({staticData['Floe-VST3-Linux'].size})
                        </li>
                    </>
                )}
            </ul>

            <h5>
                Floe Beta
                {data["latest-beta-release-version"] && (
                    <> — v{data["latest-beta-release-version"].version}</>
                )}
            </h5>
            {data["latest-beta-release-version"] ? (
                <>
                    <p className={styles.betaDescription}>Help us test new features ahead of the full release. <a href="/docs/about-the-project/beta-testing">Learn more</a></p>
                    <ul>
                        {os === 'windows' && (
                            <>
                                {data["latest-beta-release-version"]["Floe-Installer-Windows"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Installer-Windows"].url} data-umami-event="Download Beta Windows Installer">
                                            {data["latest-beta-release-version"]["Floe-Installer-Windows"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Installer-Windows"].size})
                                    </li>
                                )}
                                {data["latest-beta-release-version"]["Floe-Manual-Install-Windows"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Manual-Install-Windows"].url} data-umami-event="Download Beta Windows Manual Install">
                                            {data["latest-beta-release-version"]["Floe-Manual-Install-Windows"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Manual-Install-Windows"].size})
                                    </li>
                                )}
                            </>
                        )}
                        {os === 'macos' && (
                            <>
                                {data["latest-beta-release-version"]["Floe-Installer-macOS-Apple-Silicon"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Installer-macOS-Apple-Silicon"].url} data-umami-event="Download Beta macOS Apple Silicon Installer">
                                            {data["latest-beta-release-version"]["Floe-Installer-macOS-Apple-Silicon"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Installer-macOS-Apple-Silicon"].size})
                                    </li>
                                )}
                                {data["latest-beta-release-version"]["Floe-Installer-macOS-Intel"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Installer-macOS-Intel"].url} data-umami-event="Download Beta macOS Intel Installer">
                                            {data["latest-beta-release-version"]["Floe-Installer-macOS-Intel"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Installer-macOS-Intel"].size})
                                    </li>
                                )}
                                {data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Apple-Silicon"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Apple-Silicon"].url} data-umami-event="Download Beta macOS Apple Silicon Manual Install">
                                            {data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Apple-Silicon"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Apple-Silicon"].size})
                                    </li>
                                )}
                                {data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Intel"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Intel"].url} data-umami-event="Download Beta macOS Intel Manual Install">
                                            {data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Intel"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-Manual-Install-macOS-Intel"].size})
                                    </li>
                                )}
                            </>
                        )}
                        {os === 'linux' && (
                            <>
                                {data["latest-beta-release-version"]["Floe-CLAP-Linux"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-CLAP-Linux"].url} data-umami-event="Download Beta Linux CLAP">
                                            {data["latest-beta-release-version"]["Floe-CLAP-Linux"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-CLAP-Linux"].size})
                                    </li>
                                )}
                                {data["latest-beta-release-version"]["Floe-VST3-Linux"] && (
                                    <li>
                                        <a href={data["latest-beta-release-version"]["Floe-VST3-Linux"].url} data-umami-event="Download Beta Linux VST3">
                                            {data["latest-beta-release-version"]["Floe-VST3-Linux"].name}
                                        </a> ({data["latest-beta-release-version"]["Floe-VST3-Linux"].size})
                                    </li>
                                )}
                            </>
                        )}
                    </ul>
                </>
            ) : (
                <p>No beta versions are currently available. <a href="/docs/about-the-project/beta-testing">Learn more</a></p>
            )}

            <h5>Download Archive</h5>
            <p>Find all versions on the <a href="https://github.com/floe-audio/Floe/releases" target="_blank" rel="noopener noreferrer">GitHub releases page</a>; download links are under the <em>Assets</em> sections.</p>
        </div>
    );
}

export default function Download() {
    return (
        <Layout
            title="Download"
            description="Download and install Floe"
        >
            <div className="container">
                <div className="row">
                    <div className="col">
                        <main style={{ padding: '2rem 0' }}>
                            <h1>Download</h1>

                            <OSTabs>
                                <OSPanel os="windows">
                                    <div className={styles.installerImage}>
                                        <img src="/images/installer-windows-gui.png" alt="Windows installer screenshot" />
                                    </div>

                                    <div className={styles.downloadButtons}>
                                        <DownloadButton
                                            href={staticData['Floe-Installer-Windows'].url}
                                            dataEvent="Download Windows Installer"
                                            size={staticData['Floe-Installer-Windows'].size}
                                        >
                                            <FontAwesomeIcon icon={faDownload} />Download Floe
                                        </DownloadButton>
                                    </div>

                                    <CurrentVersionInfo />
                                    <AdditionalDownloads os="windows" />
                                </OSPanel>

                                <OSPanel os="macos">
                                    <div className={styles.installerImage}>
                                        <img src="/images/installer-macos-gui.png" alt="macOS installer screenshot" />
                                    </div>

                                    <div className={styles.downloadButtons}>
                                        <DownloadButton
                                            href={staticData['Floe-Installer-macOS-Apple-Silicon'].url}
                                            dataEvent="Download macOS Apple Silicon Installer"
                                            size={staticData['Floe-Installer-macOS-Apple-Silicon'].size}
                                        >
                                            <FontAwesomeIcon icon={faDownload} />Floe — Apple Silicon
                                        </DownloadButton>
                                        <DownloadButton
                                            href={staticData['Floe-Installer-macOS-Intel'].url}
                                            dataEvent="Download macOS Intel Installer"
                                            size={staticData['Floe-Installer-macOS-Intel'].size}
                                        >
                                            <FontAwesomeIcon icon={faDownload} />Floe — Intel
                                        </DownloadButton>
                                    </div>

                                    <CurrentVersionInfo />
                                    <AdditionalDownloads os="macos" />
                                </OSPanel>

                                <OSPanel os="linux">
                                    <p>Run the following command in a terminal to install or update Floe.</p>
                                    <p>CLAP:</p>
                                    <pre><code>mkdir -p ~/.clap && curl -L {staticData['Floe-CLAP-Linux'].url} | tar -xzf - -C ~/.clap</code></pre>
                                    <p>VST3:</p>
                                    <pre><code>mkdir -p ~/.vst3 && curl -L {staticData['Floe-VST3-Linux'].url} | tar -xzf - -C ~/.vst3</code></pre>

                                    <CurrentVersionInfo />
                                    <AdditionalDownloads os="linux" />
                                </OSPanel>
                            </OSTabs>

                        </main>
                    </div>
                </div>
            </div>
        </Layout>
    );
}
