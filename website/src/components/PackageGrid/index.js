// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import PackageCard from '../PackageCard';
import styles from './styles.module.css';

function PackageGrid({ packages, title }) {
    return (
        <div className={styles.packageSection}>
            {title && <h2 className={styles.sectionTitle}>{title}</h2>}
            <div className={styles.packageGrid}>
                {packages.filter(pkg => !pkg.hidden).map((pkg, index) => (
                    <PackageCard key={index} pkg={pkg} />
                ))}
            </div>
        </div>
    );
}

export default PackageGrid;
