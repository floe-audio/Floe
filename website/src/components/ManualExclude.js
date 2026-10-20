// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useManualType } from './ManualContext';

const k_manual_types = ['floe', 'library'];

export function checkManualTypes(types, context) {
    for (const type of types) {
        if (!k_manual_types.includes(type)) {
            throw new Error(`${context}: unknown manual type '${type}'`);
        }
    }
}

// Hides part of a page from the given manual types, like the `manual_exclude` front matter does
// for a whole page. Always shown on the website.
export default function ManualExclude({ types, children }) {
    checkManualTypes(types, 'ManualExclude');
    const manualType = useManualType();
    if (manualType !== null && types.includes(manualType)) return null;
    return children;
}
