// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import React, { useState, useRef, useEffect, useCallback } from 'react';
import { useLocation } from '@docusaurus/router';
import { FontAwesomeIcon } from '@fortawesome/react-fontawesome';
import styles from './styles.module.css';

export default function CopyMarkdownButton() {
    const location = useLocation();
    const [open, setOpen] = useState(false);
    const [copied, setCopied] = useState(false);
    const wrapperRef = useRef(null);

    const mdUrl = `${location.pathname.replace(/\/$/, '')}.md`;

    useEffect(() => {
        function onDocClick(e) {
            if (wrapperRef.current && !wrapperRef.current.contains(e.target)) {
                setOpen(false);
            }
        }
        document.addEventListener('mousedown', onDocClick);
        return () => document.removeEventListener('mousedown', onDocClick);
    }, []);

    const copyMarkdown = useCallback(async () => {
        try {
            const res = await fetch(mdUrl);
            if (!res.ok) throw new Error(`fetch failed: ${res.status}`);
            const text = await res.text();
            await navigator.clipboard.writeText(text);
            setCopied(true);
            setTimeout(() => setCopied(false), 1500);
        } catch (err) {
            console.error('Copy as markdown failed', err);
        }
        setOpen(false);
    }, [mdUrl]);

    const viewMarkdown = () => {
        window.open(mdUrl, '_blank', 'noopener');
        setOpen(false);
    };

    return (
        <div className={styles.wrapper} ref={wrapperRef}>
            <button
                type="button"
                className={styles.mainButton}
                onClick={copyMarkdown}
                title="Copy page as Markdown"
                aria-label="Copy page as Markdown"
            >
                <FontAwesomeIcon icon={copied ? ['fas', 'check'] : ['fas', 'copy']} />
                {copied && <span className={styles.label}>Copied</span>}
            </button>
            <button
                type="button"
                className={styles.caretButton}
                onClick={() => setOpen((v) => !v)}
                aria-haspopup="menu"
                aria-expanded={open}
                title="More options"
            >
                <FontAwesomeIcon icon={['fas', 'chevron-down']} />
            </button>
            {open && (
                <div className={styles.menu} role="menu">
                    <button type="button" role="menuitem" className={styles.menuItem} onClick={copyMarkdown}>
                        <FontAwesomeIcon icon={['fas', 'copy']} className={styles.menuIcon} />
                        <span className={styles.menuTitle}>Copy as Markdown</span>
                    </button>
                    <button type="button" role="menuitem" className={styles.menuItem} onClick={viewMarkdown}>
                        <FontAwesomeIcon icon={['fas', 'file-lines']} className={styles.menuIcon} />
                        <span className={styles.menuTitle}>View as Markdown</span>
                    </button>
                </div>
            )}
        </div>
    );
}
