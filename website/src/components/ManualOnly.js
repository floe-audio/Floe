// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useManualType } from './ManualContext';
import { checkManualTypes } from './ManualExclude';

// Shows part of a page only in the given manual types: the opposite of ManualExclude. Never shown on
// the website. Pair it with a ManualExclude holding the general version.
export default function ManualOnly({ types, children }) {
    checkManualTypes(types, 'ManualOnly');
    const manualType = useManualType();
    if (manualType === null || !types.includes(manualType)) return null;
    return children;
}
