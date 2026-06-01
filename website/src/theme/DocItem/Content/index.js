// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import React from 'react';
import Content from '@theme-original/DocItem/Content';
import CopyMarkdownButton from '@site/src/components/CopyMarkdownButton';

export default function ContentWrapper(props) {
    return (
        <>
            <div style={{ display: 'flex', justifyContent: 'flex-end' }}>
                <CopyMarkdownButton />
            </div>
            <Content {...props} />
        </>
    );
}
