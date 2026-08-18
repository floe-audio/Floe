// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React, { useState } from 'react';
import PackageCard from '../PackageCard';
import styles from './styles.module.css';

function PackageGrid({ packages, title, pageSize }) {
    const visiblePackages = packages.filter(pkg => !pkg.hidden);

    const initialCount = pageSize ?? visiblePackages.length;
    const [shownCount, setShownCount] = useState(initialCount);

    const shown = visiblePackages.slice(0, shownCount);
    const remaining = visiblePackages.length - shown.length;
    const canShowLess = pageSize != null && shownCount > pageSize;

    return (
        <div className={styles.packageSection}>
            {title && <h2 className={styles.sectionTitle}>{title}</h2>}
            <div className={styles.packageGrid}>
                {shown.map((pkg, index) => (
                    <PackageCard key={index} pkg={pkg} />
                ))}
            </div>
            {(remaining > 0 || canShowLess) && (
                <div className={styles.paginationControls}>
                    {remaining > 0 && (
                        <button
                            type="button"
                            className={styles.paginationButton}
                            onClick={() => setShownCount(count => Math.min(count + pageSize, visiblePackages.length))}
                        >
                            Show more ({remaining} remaining)
                        </button>
                    )}
                    {canShowLess && (
                        <button
                            type="button"
                            className={styles.paginationButton}
                            onClick={() => setShownCount(pageSize)}
                        >
                            Show less
                        </button>
                    )}
                </div>
            )}
        </div>
    );
}

export default PackageGrid;
