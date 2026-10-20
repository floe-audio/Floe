// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { createContext, useContext } from 'react';

// When rendering inside the manual page: { type: 'floe' | 'library', library: { name, features, productUrl } | null,
// libraryInserts: { slot: MDX component } }, otherwise null. Lets interactive components show a static,
// paper-friendly version of themselves.
export const ManualContext = createContext(null);

export function useManualType() {
    return useContext(ManualContext)?.type ?? null;
}

export function useIsManual() {
    return useManualType() !== null;
}

// The sample library a library manual is for, otherwise null.
export function useManualLibrary() {
    return useContext(ManualContext)?.library ?? null;
}
