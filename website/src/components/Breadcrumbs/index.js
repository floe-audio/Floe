// SPDX-FileCopyrightText: 2025 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import HomeBreadcrumbItem from '@theme/DocBreadcrumbs/Items/Home';
import styles from './styles.module.css';

const Breadcrumbs = ({ items }) => {
    return (
        <nav className={`theme-doc-breadcrumbs ${styles.breadcrumbsContainer}`} aria-label="breadcrumbs">
            <ul className="breadcrumbs breadcrumbs--sm">
                <HomeBreadcrumbItem />
                {items.map((item, index) => {
                    const isLast = index === items.length - 1;
                    return (
                        <li
                            key={index}
                            className={`breadcrumbs__item ${isLast ? 'breadcrumbs__item--active' : ''}`}
                        >
                            {isLast ? (
                                <span className="breadcrumbs__link">{item.label}</span>
                            ) : (
                                <a className="breadcrumbs__link" href={item.href}>
                                    {item.label}
                                </a>
                            )}
                        </li>
                    );
                })}
            </ul>
        </nav>
    );
};

export default Breadcrumbs;
