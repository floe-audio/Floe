// SPDX-FileCopyrightText: 2026 Sam Windell
// SPDX-License-Identifier: MIT

import { useManualLibrary } from './ManualContext';

// The name of the sample library a library manual is for. Elsewhere it shows its children, a general
// phrase such as "a sample library", so it can sit in prose shared by every manual. Without children,
// it's only for use inside <ManualOnly types={['library']}>.
export default function LibraryName({ children }) {
    const library = useManualLibrary();
    if (library !== null) return library.name;
    if (children === undefined) throw new Error("LibraryName: needs a general version as children outside <ManualOnly types={['library']}>");
    return children;
}
